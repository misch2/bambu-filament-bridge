// SPDX-License-Identifier: AGPL-3.0-or-later
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "bfb/bridge_service.hpp"
#include "bfb/http_server.hpp"
#include "fake_backend.hpp"
using namespace bfb;
using namespace bfb::test;
using namespace std::chrono_literals;
#define CHECK(value)                                                                    \
  do {                                                                                  \
    if (!(value))                                                                       \
      throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + \
                               " " #value);                                             \
  } while (false)
template <class F>
void wait_until(F predicate) {
  std::mutex mutex;
  std::condition_variable cv;
  std::unique_lock<std::mutex> lock(mutex);
  auto end = Clock::now() + 2s;
  while (!predicate()) {
    CHECK(Clock::now() < end);
    cv.wait_for(lock, 1ms);
  }
}
Config config() {
  Config c;
  c.printer_id = "TEST_DEVICE";
  c.printer_ip = "127.0.0.1";
  c.access_code = "TEST_ONLY";
  c.http_token = std::string(32, 'x');
  c.http_bind = "127.0.0.1";
  return c;
}
Timing timing() {
  Timing t;
  t.reply = t.verification = 60ms;
  t.connect = t.cert = t.telemetry = 30ms;
  t.provision_delay = t.disconnect_delay = 0ms;
  t.refresh = 5ms;
  t.retry_min = 10ms;
  t.retry_max = 20ms;
  t.stale = 5s;
  return t;
}
struct Fixture {
  FakeBackend fake;
  Config cfg = config();
  BridgeService service;
  explicit Fixture(Timing t = timing(), bool start_ready = true, bool certificate_ack = true,
                   int ams = 0, int layout = 0)
      : service(fake, t, {cfg.http_token, cfg.access_code}) {
    fake.auto_connect = start_ready;
    fake.require_certificate = fake.emit_certificate = certificate_ack;
    fake.ams = ams;
    fake.status_layout = layout;
    service.start();
    if (start_ready) wait_until([&] { return service.health().ready; });
  }
  ~Fixture() { service.stop(); }
};
json body() {
  return {{"profile", "GFG99"},  {"setting", "GFSG99_15"}, {"type", "PETG"},
          {"color", "808080FF"}, {"tempMin", 220},         {"tempMax", 260}};
}
HttpRequest request(const Config& c) {
  return {"POST",
          "/api/v1/ams/0/trays/3/filament",
          {{"authorization", "Bearer " + c.http_token}, {"content-type", "application/json"}},
          body().dump()};
}
void configuration() {
  std::map<std::string, std::string> env{{"BAMBU_DEV_ID", "TEST_DEVICE"},
                                         {"BAMBU_DEV_IP", "127.0.0.1"},
                                         {"BAMBU_ACCESS_CODE", "TEST_ACCESS"},
                                         {"BAMBU_HTTP_TOKEN", std::string(32, 'x')}};
  auto load = [&] {
    return load_config([&](const char* k) -> const char* {
      auto i = env.find(k);
      return i == env.end() ? nullptr : i->second.c_str();
    });
  };
  auto c = load();
  CHECK(c.http_port == 8080);
  CHECK(c.http_bind == "0.0.0.0");
  CHECK(c.data_dir == "/data");
  CHECK(c.cert_dir == "/data/certs");
  auto invalid = [&](const std::string& key, const std::string& value) {
    auto previous = env;
    env[key] = value;
    bool threw = false;
    try {
      load();
    } catch (const std::exception& e) {
      threw = true;
      CHECK(std::string(e.what()).find("TEST_ACCESS") == std::string::npos);
      CHECK(std::string(e.what()).find(std::string(32, 'x')) == std::string::npos);
    }
    env = previous;
    CHECK(threw);
  };
  for (auto key : {"BAMBU_DEV_ID", "BAMBU_DEV_IP", "BAMBU_ACCESS_CODE", "BAMBU_HTTP_TOKEN"})
    invalid(key, "");
  for (auto port : {"0", "65536", "-1", "abc", "8080oops", "999999999999999999999"})
    invalid("BAMBU_HTTP_PORT", port);
  invalid("BAMBU_HTTP_TOKEN", "short");
  invalid("BAMBU_DEV_IP", "invalid");
  invalid("BAMBU_HTTP_BIND", "invalid");
  invalid("BAMBU_DEV_ID", "id\nlog");
  auto path =
      std::filesystem::temp_directory_path() / ("bfb-secret-test-" + std::to_string(getpid()));
  {
    std::ofstream f(path);
    f << std::string(40, 's') << "\r\n";
  }
  env["BAMBU_HTTP_TOKEN_FILE"] = path.string();
  CHECK(load().http_token == std::string(32, 'x'));
  env.erase("BAMBU_HTTP_TOKEN");
  CHECK(load().http_token == std::string(40, 's'));
  env["BAMBU_ACCESS_CODE_FILE"] = path.string();
  env.erase("BAMBU_ACCESS_CODE");
  CHECK(load().access_code == std::string(40, 's'));
  env["BAMBU_HTTP_TOKEN"] = "";
  bool threw = false;
  try {
    load();
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
  env.erase("BAMBU_HTTP_TOKEN");
  std::filesystem::remove(path);
  threw = false;
  try {
    load();
  } catch (const std::exception& e) {
    threw = true;
    CHECK(std::string(e.what()).find(path.string()) == std::string::npos);
  }
  CHECK(threw);
}
void validation() {
  Fixture f;
  auto r = request(f.cfg);
  auto check = [&](int code) { CHECK(handle_request(r, f.service, f.cfg).status == code); };
  r.body = "{";
  check(400);
  r.body = "[]";
  check(400);
  for (auto key : {"profile", "setting", "type", "color", "tempMin", "tempMax"}) {
    auto b = body();
    b.erase(key);
    r.body = b.dump();
    check(400);
  }
  for (auto key : {"profile", "setting", "type"}) {
    auto b = body();
    b[key] = "";
    r.body = b.dump();
    check(400);
    b[key] = 42;
    r.body = b.dump();
    check(400);
  }
  for (auto color : {"FFFFFF", "ABCDEFGH", "#808080FF"}) {
    auto b = body();
    b["color"] = color;
    r.body = b.dump();
    check(400);
  }
  for (auto key : {"tempMin", "tempMax"})
    for (json temp : {json(-1), json(401), json(2.5), json("220"), json(true), json(1e30)}) {
      auto b = body();
      b[key] = temp;
      r.body = b.dump();
      check(400);
    }
  auto b = body();
  b["tempMin"] = 300;
  r.body = b.dump();
  check(400);
  r.body = body().dump();
  for (auto path : {"/api/v1/ams/-1/trays/0/filament", "/api/v1/ams/256/trays/0/filament",
                    "/api/v1/ams/0/trays/4/filament", "/api/v1/ams/999999999999/trays/0/filament",
                    "/api/v1/ams/a/trays/0/filament"}) {
    r.path = path;
    check(400);
  }
  r = request(f.cfg);
  r.body = std::string(max_body_size + 1, ' ');
  check(413);
  r = request(f.cfg);
  r.headers["content-type"] = "text/plain";
  check(400);
  r = request(f.cfg);
  b = body();
  b["color"] = "abcdefFF";
  r.body = b.dump();
  check(200);
}
void state() {
  Fixture developer(timing(), true, false);
  CHECK(developer.service.health().ready);
  Fixture f(timing(), false);
  f.fake.telemetry = false;
  auto h = handle_request({"GET", "/health", {}, ""}, f.service, f.cfg);
  CHECK(h.status == 503);
  auto j = json::parse(h.body);
  CHECK(j["ready"] == false);
  CHECK(j["reconnecting"] == true);
  CHECK(f.service.set_filament(0, 3, f.fake.loaded).error == "printer_not_ready");
  auto cap_request = request(f.cfg);
  cap_request.method = "GET";
  cap_request.path = "/api/v1/capabilities";
  CHECK(handle_request(cap_request, f.service, f.cfg).status == 200);
  f.fake.callbacks.connection(true);
  CHECK(!f.service.health().ready);
  f.fake.callbacks.message("{bad");
  f.fake.callbacks.message(R"({"print":{"command":42,"ams":false}})");
  CHECK(!f.service.health().ready);
  f.fake.telemetry = true;
  f.fake.auto_connect = true;
  wait_until([&] { return f.service.health().ready; });
  h = handle_request({"GET", "/health", {}, ""}, f.service, f.cfg);
  CHECK(h.status == 200);
  j = json::parse(h.body);
  for (auto key : {"status", "connected", "ready", "reconnecting", "printerId", "printerIp",
                   "pluginVersion", "firmware", "lastMessageAgeMs"})
    CHECK(j.contains(key));
  f.fake.auto_connect = false;
  f.fake.callbacks.connection(false);
  CHECK(!f.service.health().ready);
  f.fake.auto_connect = true;
  wait_until([&] { return f.service.health().ready; });
  CHECK(f.fake.connects >= 2);
  auto t = timing();
  t.stale = 10ms;
  t.retry_min = t.retry_max = 500ms;
  Fixture stale(t);
  stale.fake.auto_connect = false;
  stale.fake.telemetry = false;
  wait_until([&] { return !stale.service.health().ready; });
  CHECK(handle_request({"GET", "/health", {}, ""}, stale.service, stale.cfg).status == 503);
}
void verification() {
  struct Case {
    const char* error;
    int code;
    std::function<void(FakeBackend&)> setup;
  };
  std::vector<Case> cases{
      {"send_failed", 502, [](auto& b) { b.fail_send = true; }},
      {"printer_rejected", 502, [](auto& b) { b.reject = true; }},
      {"printer_reply_timeout", 504, [](auto& b) { b.no_reply = true; }},
      {"printer_reply_timeout", 504, [](auto& b) { b.wrong_sequence = true; }},
      {"printer_reply_timeout", 504, [](auto& b) { b.wrong_command = true; }},
      {"verification_request_failed", 502, [](auto& b) { b.fail_verify = true; }},
      {"verification_timeout", 504, [](auto& b) { b.telemetry = false; }},
      {"verification_timeout", 504, [](auto& b) { b.wrong_tray = true; }},
      {"verification_timeout", 504, [](auto& b) { b.pre_reply_only = true; }},
      {"verification_timeout", 504,
       [](auto& b) {
         b.omit_result = true;
         b.telemetry = false;
       }},
      {"verification_timeout", 504,
       [](auto& b) {
         b.omit_result = true;
         b.pre_reply_only = true;
       }},
      {"printer_not_ready", 503, [](auto& b) { b.disconnect_write = true; }}};
  for (int ams : {0, external_left_id, external_right_id})
    for (auto& c : cases) {
      Fixture f;
      c.setup(f.fake);
      auto req = request(f.cfg);
      req.path = "/api/v1/ams/" + std::to_string(ams) + "/trays/0/filament";
      auto r = handle_request(req, f.service, f.cfg);
      CHECK(r.status == c.code);
      CHECK(json::parse(r.body)["error"] == c.error);
    }
  for (int mismatch : {1, 2, 3, 4, 5, 6, 7, 9, 10, 11}) {
    Fixture mismatch_fixture;
    mismatch_fixture.fake.mismatch = mismatch;
    CHECK(mismatch_fixture.service.set_filament(0, 3, mismatch_fixture.fake.loaded).error ==
          "verification_timeout");
  }
  Fixture absent_setting;
  absent_setting.fake.mismatch = 8;
  CHECK(absent_setting.service.set_filament(0, 3, absent_setting.fake.loaded).verified);
  Fixture f;
  auto r = handle_request(request(f.cfg), f.service, f.cfg);
  CHECK(r.status == 200);
  auto j = json::parse(r.body);
  CHECK(j["status"] == "synced");
  CHECK(j["verified"] == true);
  CHECK(j["amsId"] == 0);
  CHECK(j["trayId"] == 3);
  CHECK(j["sequenceId"].is_string());
  CHECK(j["elapsedMs"].is_number_integer());
  for (bool omit_result : {false, true})
    for (int tray = 0; tray < 4; ++tray) {
      Fixture ack;
      ack.fake.omit_result = omit_result;
      auto req = request(ack.cfg);
      req.path = "/api/v1/ams/0/trays/" + std::to_string(tray) + "/filament";
      auto response = handle_request(req, ack.service, ack.cfg);
      CHECK(response.status == 200);
      CHECK(json::parse(response.body)["verified"] == true);
      std::lock_guard<std::mutex> lock(ack.fake.mutex);
      CHECK(ack.fake.last_command["ams_id"] == 0);
      CHECK(ack.fake.last_command["slot_id"] == tray);
      CHECK(ack.fake.last_command["tray_id"] == tray);
    }
}
void external() {
  for (int ams : {external_left_id, external_right_id}) {
    // External-only status must also establish readiness.
    Fixture f(timing(), true, true, ams);
    auto r = request(f.cfg);
    r.path = "/api/v1/ams/" + std::to_string(ams) + "/trays/0/filament";
    auto response = handle_request(r, f.service, f.cfg);
    CHECK(response.status == 200);
    auto j = json::parse(response.body);
    CHECK(j["status"] == "synced");
    CHECK(j["verified"] == true);
    CHECK(j["amsId"] == ams);
    CHECK(j["trayId"] == 0);
    {
      std::lock_guard<std::mutex> lock(f.fake.mutex);
      CHECK(f.fake.last_command["ams_id"] == ams);
      CHECK(f.fake.last_command["slot_id"] == 0);
      CHECK(f.fake.last_command["tray_id"] == 254);
    }
    f.fake.omit_result = true;
    CHECK(handle_request(r, f.service, f.cfg).status == 200);
    f.fake.omit_result = false;
    for (int mismatch : {1, 2, 3, 4, 5, 6, 7, 9, 10, 11}) {
      f.fake.mismatch = mismatch;
      CHECK(handle_request(r, f.service, f.cfg).status == 504);
    }
    f.fake.mismatch = 8;
    CHECK(handle_request(r, f.service, f.cfg).status == 200);
    f.fake.mismatch = 0;
    // Matching values from the other holder or the regular AMS tree cannot verify.
    f.fake.external_status_id = ams == external_left_id ? external_right_id : external_left_id;
    CHECK(handle_request(r, f.service, f.cfg).status == 504);
    f.fake.external_status_id = -1;
    f.fake.numeric_slot_id = true;
    CHECK(handle_request(r, f.service, f.cfg).status == 200);
    f.fake.numeric_slot_id = false;
    f.fake.packed_slot_id = true;
    CHECK(handle_request(r, f.service, f.cfg).status == 200);
    f.fake.packed_slot_id = false;
    f.fake.wrong_tray = f.fake.conflicting_legacy = true;
    CHECK(handle_request(r, f.service, f.cfg).status == 504);
    f.fake.wrong_tray = f.fake.conflicting_legacy = false;
    // A missing target in vir_slot must not fall back to matching legacy metadata.
    f.fake.external_status_id = ams == external_left_id ? external_right_id : external_left_id;
    f.fake.conflicting_legacy = true;
    CHECK(handle_request(r, f.service, f.cfg).status == 504);
    f.fake.external_status_id = -1;
    f.fake.conflicting_legacy = false;
    f.fake.status_layout = 2;
    CHECK(handle_request(r, f.service, f.cfg).status == 504);
    f.fake.status_layout = 1;
    CHECK(handle_request(r, f.service, f.cfg).status == (ams == external_right_id ? 200 : 504));
  }
  Fixture legacy(timing(), true, true, external_right_id, 1);
  CHECK(legacy.service.set_filament(external_right_id, 0, legacy.fake.loaded).verified);
}
void protocol() {
  // Golden single-color commands: Bambu Studio command_ams_filament_settings,
  // da8b44ee34dd349f2ae0df3f1cbae366df482354, DeviceManager.cpp:1691-1722.
  struct Address {
    int ams, tray;
    const char* fixture;
  };
  for (const auto address :
       {Address{0, 2, "ams-0-tray-2.json"}, Address{254, 0, "external-deputy.json"},
        Address{255, 0, "external-main.json"}}) {
    std::ifstream file(std::filesystem::path(BFB_PROTOCOL_GOLDEN_DIR) / address.fixture);
    CHECK(file.good());
    const auto golden = json::parse(file);
    CHECK(golden.at("print").at("sequence_id") == "<sequence_id>");
    for (bool omit_result : {false, true}) {
      Fixture f;
      f.fake.omit_result = omit_result;
      const Filament filament{"GFL99", "GFSL99_17", "PLA", "FFCF98FF", 190, 240};
      std::string previous_sequence;
      for (int write = 0; write < 2; ++write) {
        const auto result = f.service.set_filament(address.ams, address.tray, filament);
        CHECK(result.verified);
        CHECK(!result.sequence_id.empty());
        CHECK(result.sequence_id.find_first_not_of("0123456789") == std::string::npos);
        CHECK(result.sequence_id != previous_sequence);
        previous_sequence = result.sequence_id;
        auto expected = golden;
        // Only the runtime sequence varies. Never derive fixture addresses or metadata from send.
        expected["print"]["sequence_id"] = result.sequence_id;
        std::lock_guard<std::mutex> lock(f.fake.mutex);
        // Canonical JSON comparison also preserves scalar types (190 differs from 190.0).
        CHECK(f.fake.last_write.dump() == expected.dump());
      }
    }
  }
}
class LogCapture {
 public:
  LogCapture() {
    std::cout.flush();
    file_ = std::tmpfile();
    CHECK(file_ != nullptr);
    original_ = dup(STDOUT_FILENO);
    CHECK(original_ >= 0);
    CHECK(dup2(fileno(file_), STDOUT_FILENO) >= 0);
  }
  ~LogCapture() {
    std::cout.flush();
    dup2(original_, STDOUT_FILENO);
    close(original_);
    std::fclose(file_);
  }
  std::string contents() {
    std::cout.flush();
    std::rewind(file_);
    std::string result;
    char buffer[4096];
    while (auto count = std::fread(buffer, 1, sizeof(buffer), file_)) result.append(buffer, count);
    return result;
  }

 private:
  FILE* file_ = nullptr;
  int original_ = -1;
};
void logging() {
  Fixture f;
  LogCapture capture;
  auto r = request(f.cfg);
  auto payload = body();
  payload["profile"] = "prefix:" + f.cfg.access_code;
  payload["setting"] = f.cfg.http_token;
  payload["private_extra"] = "UNLOGGED_PRIVATE_EXTRA";
  r.body = payload.dump();
  CHECK(handle_request(r, f.service, f.cfg).status == 200);
  r.headers["authorization"] = "Bearer UNLOGGED_WRONG_TOKEN";
  CHECK(handle_request(r, f.service, f.cfg).status == 401);
  r = request(f.cfg);
  r.path = "/api/v1/ams/" + f.cfg.access_code + "/trays/0/filament";
  CHECK(handle_request(r, f.service, f.cfg).status == 400);
  r.path = "/unknown?token=" + f.cfg.http_token;
  CHECK(handle_request(r, f.service, f.cfg).status == 404);
  r = request(f.cfg);
  payload = body();
  payload.erase("profile");
  r.body = payload.dump();
  CHECK(handle_request(r, f.service, f.cfg).status == 400);
  payload = body();
  payload["tempMin"] = 401;
  r.body = payload.dump();
  CHECK(handle_request(r, f.service, f.cfg).status == 400);
  r = request(f.cfg);
  r.path = "/api/v1/ams/254/trays/0/filament";
  f.fake.no_reply = true;
  CHECK(handle_request(r, f.service, f.cfg).status == 504);
  f.fake.no_reply = false;
  f.fake.telemetry = false;
  CHECK(handle_request(r, f.service, f.cfg).status == 504);
  f.fake.telemetry = true;
  f.fake.status_layout = 2;
  CHECK(handle_request(r, f.service, f.cfg).status == 504);
  f.fake.status_layout = 0;
  f.fake.wrong_tray = true;
  CHECK(handle_request(r, f.service, f.cfg).status == 504);
  f.fake.wrong_tray = false;
  f.fake.omit_result = true;
  CHECK(handle_request(r, f.service, f.cfg).status == 200);
  f.fake.omit_result = false;
  f.fake.reject = true;
  CHECK(handle_request(r, f.service, f.cfg).status == 502);
  auto output = capture.contents();
  for (const auto& secret :
       {f.cfg.http_token, f.cfg.access_code, std::string("UNLOGGED_PRIVATE_EXTRA"),
        std::string("UNLOGGED_WRONG_TOKEN")})
    CHECK(output.find(secret) == std::string::npos);
  CHECK(output.find("UNLOGGED_REPLY_PAYLOAD") == std::string::npos);
  std::map<unsigned long long, json> received;
  std::set<std::string> sent_sequences, timeout_reasons;
  bool saw_filament = false, saw_missing_profile = false, saw_temp = false;
  bool saw_reply_timeout = false, saw_correlated_success = false;
  bool saw_missing_result = false, saw_success_result = false, saw_rejected_reply = false;
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("[http] ", 0) != 0 && line.rfind("[command] ", 0) != 0) continue;
    auto event = json::parse(line.substr(line.find(' ') + 1));
    CHECK(event["time"].is_string());
    if (event["event"] == "request_received") {
      auto id = event["requestId"].get<unsigned long long>();
      CHECK(received.emplace(id, event).second);
    }
    if (event["event"] == "filament_requested") {
      CHECK(event["amsId"].is_number_integer());
      CHECK(event["trayId"].is_number_integer());
      CHECK(event["filament"]["color"] == "808080FF");
      CHECK(event["filament"]["tempMin"] == 220);
      saw_filament = true;
    }
    if (event["event"] == "validation_failed") {
      saw_missing_profile |= event["field"] == "missing_profile";
      saw_temp |= event["field"] == "tempMin";
    }
    if (event["event"] == "send") {
      sent_sequences.insert(event["sequenceId"].get<std::string>());
      CHECK(event["wireSlotId"] == (event["amsId"] == 254 ? 0 : 3));
      if (event["amsId"] == 254) CHECK(event["wireTrayId"] == 254);
    }
    if (event["event"] == "reply_received") {
      CHECK(event["reply_received"] == true);
      if (!event.contains("result")) {
        CHECK(event["accepted"] == true);
        saw_missing_result = true;
      } else if (event["result"] == "success") {
        CHECK(event["accepted"] == true);
        saw_success_result = true;
      } else if (event["result"] == "fail") {
        CHECK(event["accepted"] == false);
        CHECK(event["reason"] == "test rejection");
        CHECK(event["errCode"] == 42);
        CHECK(event["errno"] == 7);
        saw_rejected_reply = true;
      }
    }
    if (event["event"] == "request_completed") {
      CHECK(received.count(event["requestId"].get<unsigned long long>()) == 1);
      CHECK(event["elapsedMs"].is_number_integer());
      if (event["status"] == 200 && event.contains("sequenceId")) {
        CHECK(sent_sequences.count(event["sequenceId"].get<std::string>()) == 1);
        saw_correlated_success = true;
      }
    }
    if (event.value("outcome", "") == "printer_reply_timeout") {
      CHECK(event["replyReceived"] == false);
      saw_reply_timeout = true;
    }
    if (event.value("outcome", "") == "verification_timeout") {
      CHECK(event["replyAccepted"] == true);
      timeout_reasons.insert(event["reason"].get<std::string>());
      if (event["reason"] == "slot_metadata_mismatch")
        CHECK(event["mismatchedFields"] == "tray_info_idx");
    }
  }
  CHECK(saw_filament && saw_missing_profile && saw_temp && saw_reply_timeout &&
        saw_correlated_success);
  CHECK(saw_missing_result && saw_success_result && saw_rejected_reply);
  CHECK(timeout_reasons == std::set<std::string>({"no_fresh_status", "target_slot_missing",
                                                  "slot_metadata_mismatch"}));
}
void failure_diagnostics() {
  for (bool clear : {false, true}) {
    Fixture f;
    LogCapture capture;
    f.fake.reject = true;
    f.fake.reply_overrides = {{"code", 19},
                              {"message", "slot unavailable"},
                              {"msg", json::object()},
                              {"unknown", "PRIVATE_PAYLOAD"}};
    auto r = request(f.cfg);
    if (clear) {
      r.method = "DELETE";
      r.body = "";
    }
    auto response = handle_request(r, f.service, f.cfg);
    CHECK(response.status == 502);
    auto body = json::parse(response.body);
    CHECK(body["error"] == "printer_rejected");
    CHECK(body["verified"] == false);
    CHECK(body["sequenceId"].is_string() && !body["sequenceId"].get<std::string>().empty());
    CHECK(body["amsId"] == 0 && body["trayId"] == 3);
    CHECK(body["elapsedMs"].is_number_integer());
    auto detail = body["diagnostics"];
    CHECK(detail["operation"] == (clear ? "clear" : "set"));
    CHECK(detail["replyReceived"] == true && detail["replyAccepted"] == false);
    CHECK(detail["freshStatuses"] == 0 && detail["targetSeen"] == false);
    CHECK(detail["printerReply"]["result"] == "fail");
    CHECK(detail["printerReply"]["errCode"] == 42);
    CHECK(detail["printerReply"]["code"] == 19);
    CHECK(detail["printerReply"]["message"] == "slot unavailable");
    CHECK(!detail["printerReply"].contains("msg"));
    CHECK(response.body.find("PRIVATE_PAYLOAD") == std::string::npos);
    CHECK(capture.contents().find("PRIVATE_PAYLOAD") == std::string::npos);
    CHECK(f.fake.writes == 1);  // Explicit rejection never causes a blind retry.
    // A failure must not poison the next command or leak an earlier reply.
    f.fake.reject = false;
    f.fake.reply_overrides = json::object();
    CHECK(handle_request(r, f.service, f.cfg).status == 200);
    f.fake.no_reply = true;
    auto next_failure = json::parse(handle_request(r, f.service, f.cfg).body);
    CHECK(next_failure["error"] == "printer_reply_timeout");
    CHECK(!next_failure["diagnostics"].contains("printerReply"));
  }
  {
    Fixture f;
    LogCapture capture;
    f.fake.reject = true;
    f.fake.reply_overrides = {{"message", f.cfg.http_token}, {"reason", std::string(300, 'z')}};
    auto body = json::parse(handle_request(request(f.cfg), f.service, f.cfg).body);
    CHECK(body["diagnostics"]["printerReply"]["message"] == "[redacted]");
    CHECK(body["diagnostics"]["printerReply"]["reason"].get<std::string>().size() == 256);
    CHECK(capture.contents().find(f.cfg.http_token) == std::string::npos);
  }
  {
    Fixture f;
    LogCapture capture;
    f.fake.reject = true;
    f.fake.reply_overrides = {{"message", std::string(255, 'z') + "\xc4\x9b"},
                              {"reason", std::string(250, 'z') + f.cfg.http_token},
                              {"msg", "prefix:" + f.cfg.access_code + ":" + f.cfg.http_token}};
    auto response = handle_request(request(f.cfg), f.service, f.cfg);
    auto body = json::parse(response.body);
    CHECK(body["diagnostics"]["printerReply"]["message"] == std::string(255, 'z'));
    CHECK(body["diagnostics"]["printerReply"]["reason"].get<std::string>().size() == 256);
    CHECK(body["diagnostics"]["printerReply"]["msg"] == "prefix:[redacted]:[redacted]");
    CHECK(capture.contents().find(f.cfg.access_code) == std::string::npos);
    CHECK(capture.contents().find(f.cfg.http_token) == std::string::npos);
  }
  {
    Fixture f;
    f.fake.telemetry = false;
    auto body = json::parse(handle_request(request(f.cfg), f.service, f.cfg).body);
    CHECK(body["diagnostics"]["reason"] == "no_fresh_status");
    CHECK(body["diagnostics"]["replyAccepted"] == true);
  }
  {
    Fixture f;
    f.fake.callbacks.message(json{{"print",
                                   {{"command", "push_status"},
                                    {"gcode_state", "IDLE"},
                                    {"ams_status", 0},
                                    {"print_error", 0},
                                    {"unknown", "PRIVATE_STATUS"}}}}
                                 .dump());
    f.fake.reject = true;
    auto body = json::parse(handle_request(request(f.cfg), f.service, f.cfg).body);
    CHECK(body["diagnostics"]["printerState"] ==
          json({{"gcode_state", "IDLE"}, {"ams_status", 0}, {"print_error", 0}}));
    CHECK(body["diagnostics"]["printerStateAgeMs"].get<long long>() >= 0);
    CHECK(body.dump().find("PRIVATE_STATUS") == std::string::npos);
  }
}
void concurrency() {
  auto t = timing();
  t.reply = 1s;
  Fixture f(t);
  f.fake.hold_write = true;
  auto first =
      std::async(std::launch::async, [&] { return f.service.set_filament(0, 3, f.fake.loaded); });
  f.fake.wait_write(1);
  auto second = std::async(std::launch::async, [&] {
    return f.service.set_filament(external_left_id, 0,
                                  {"GFG99", "GFSG99_15", "PETG", "808080FF", 220, 260});
  });
  CHECK(handle_request({"GET", "/health", {}, ""}, f.service, f.cfg).status == 200);
  f.fake.hold_write = false;
  f.fake.reply();
  CHECK(first.get().verified);
  CHECK(second.get().verified);
  CHECK(f.fake.writes == 2);
  {
    std::lock_guard<std::mutex> lock(f.fake.mutex);
    CHECK(std::set<std::string>(f.fake.sequences.begin(), f.fake.sequences.end()).size() ==
          f.fake.sequences.size());
  }
  f.fake.hold_write = true;
  auto interrupted = std::async(std::launch::async, [&] {
    return f.service.set_filament(external_right_id, 0,
                                  {"GFG99", "GFSG99_15", "PETG", "808080FF", 220, 260});
  });
  f.fake.wait_write(3);
  f.fake.callbacks.connection(false);
  CHECK(interrupted.get().error == "printer_not_ready");
  wait_until([&] { return f.service.health().ready; });
  // Late success from the interrupted operation cannot verify the next one.
  f.fake.reply();
  f.fake.hold_write = false;
  CHECK(f.service.set_filament(0, 3, f.fake.loaded).verified);
}
void clear_filament() {
  auto clear_request = [](const Config& c, int ams = 0, int tray = 2) {
    return HttpRequest{
        "DELETE",
        "/api/v1/ams/" + std::to_string(ams) + "/trays/" + std::to_string(tray) + "/filament",
        {{"authorization", "Bearer " + c.http_token}},
        ""};
  };
  Fixture f;
  auto r = clear_request(f.cfg);
  r.headers.clear();
  CHECK(handle_request(r, f.service, f.cfg).status == 401);
  r.headers["authorization"] = "Bearer wrong";
  CHECK(handle_request(r, f.service, f.cfg).status == 401);
  for (const auto* path : {"/api/v1/ams/256/trays/0/filament", "/api/v1/ams/0/trays/4/filament",
                           "/api/v1/ams/bad/trays/0/filament"}) {
    r = clear_request(f.cfg);
    r.path = path;
    CHECK(handle_request(r, f.service, f.cfg).status == 400);
  }
  for (int ams : {0, 254, 255}) {
    Fixture target;
    auto response = handle_request(clear_request(target.cfg, ams, ams == 0 ? 2 : 0), target.service,
                                   target.cfg);
    CHECK(response.status == 200);
    auto result = json::parse(response.body);
    CHECK(result["status"] == "cleared");
    CHECK(result["verified"] == true);
    CHECK(result["amsId"] == ams);
    CHECK(result["trayId"] == (ams == 0 ? 2 : 0));
    auto payload = target.fake.last_write;
    payload["print"]["sequence_id"] = "<sequence_id>";
    std::string name = ams == 0     ? "reset-ams-0-tray-2"
                       : ams == 254 ? "reset-external-deputy"
                                    : "reset-external-main";
    std::ifstream input(std::string(BFB_PROTOCOL_GOLDEN_DIR) + "/" + name + ".json");
    json golden;
    input >> golden;
    CHECK(payload == golden);
    for (int layout : {1, 2}) {
      target.fake.clear_layout = layout;
      CHECK(target.service.clear_filament(ams, 0).verified);
    }
  }
  for (int scenario = 0; scenario < 13; ++scenario) {
    Fixture failure;
    switch (scenario) {
      case 0:
        failure.fake.fail_send = true;
        break;
      case 1:
        failure.fake.reject = true;
        break;
      case 2:
        failure.fake.no_reply = true;
        break;
      case 3:
        failure.fake.telemetry = false;
        break;
      case 4:
        failure.fake.mismatch = 6;
        break;
      case 5:
        failure.fake.wrong_tray = true;
        break;
      case 6:
        failure.fake.pre_reply_only = true;
        break;
      case 7:
        failure.fake.fail_verify = true;
        break;
      case 8:
        failure.fake.wrong_sequence = true;
        break;
      case 9:
        failure.fake.wrong_command = true;
        break;
      case 10:
        failure.fake.clear_layout = 3;
        break;
      case 11:
        failure.fake.clear_layout = 4;
        break;
      case 12:
        failure.fake.clear_layout = 5;
        break;
    }
    auto response = handle_request(clear_request(failure.cfg), failure.service, failure.cfg);
    CHECK(response.status == (scenario == 0 || scenario == 1 || scenario == 7 ? 502 : 504));
    CHECK(json::parse(response.body)["status"] == "error");
  }
  for (int mismatch : {1, 5, 7, 9}) {
    Fixture malformed;
    malformed.fake.mismatch = mismatch;
    CHECK(malformed.service.clear_filament(0, 3).error == "verification_timeout");
  }
  {
    Fixture disconnected(timing(), false);
    CHECK(handle_request(clear_request(disconnected.cfg), disconnected.service, disconnected.cfg)
              .status == 503);
  }
  {
    LogCapture capture;
    CHECK(handle_request(clear_request(f.cfg), f.service, f.cfg).status == 200);
    CHECK(capture.contents().find("\"method\":\"DELETE\"") != std::string::npos);
  }
  // Matching cleared state from before the command cannot verify a new reset.
  f.fake.loaded = {"", "", "", "FFFFFF00", 0, 0};
  f.fake.emit_status();
  f.fake.telemetry = false;
  CHECK(f.service.clear_filament(0, 3).error == "verification_timeout");
  Fixture serialized;
  serialized.fake.hold_write = true;
  auto first =
      std::async(std::launch::async, [&] { return serialized.service.clear_filament(0, 3); });
  serialized.fake.wait_write(1);
  auto second = std::async(std::launch::async, [&] {
    return serialized.service.set_filament(0, 3,
                                           {"GFG99", "GFSG99_15", "PETG", "808080FF", 220, 260});
  });
  CHECK(second.wait_for(5ms) == std::future_status::timeout);
  CHECK(serialized.fake.writes == 1);
  serialized.fake.hold_write = false;
  serialized.fake.reply();
  CHECK(first.get().verified);
  CHECK(second.get().verified);
  CHECK(serialized.fake.writes == 2);
}
std::string wire(int port, const std::string& request) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  timeval timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  CHECK(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  std::size_t sent = 0;
  while (sent < request.size()) {
    auto n = send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
    CHECK(n > 0);
    sent += static_cast<std::size_t>(n);
  }
  std::string response;
  char buffer[4096];
  ssize_t n;
  while ((n = recv(fd, buffer, sizeof(buffer), 0)) > 0)
    response.append(buffer, static_cast<std::size_t>(n));
  close(fd);
  return response;
}
void http() {
  Fixture f;
  CHECK(handle_request({"GET", "/health", {}, ""}, f.service, f.cfg).status == 200);
  auto r = request(f.cfg);
  r.headers.erase("authorization");
  CHECK(handle_request(r, f.service, f.cfg).status == 401);
  r.headers["authorization"] = "Bearer wrong";
  CHECK(handle_request(r, f.service, f.cfg).status == 401);
  r.headers["authorization"] = "Bearer " + std::string(32, 'y');
  CHECK(handle_request(r, f.service, f.cfg).status == 401);
  r = request(f.cfg);
  r.method = "GET";
  r.path = "/api/v1/capabilities";
  auto cap = handle_request(r, f.service, f.cfg);
  CHECK(cap.status == 200);
  auto j = json::parse(cap.body);
  CHECK(j["apiVersion"] == 1);
  CHECK(j["features"]["amsFilamentWrite"] == true);
  CHECK(j["features"]["externalFilamentWrite"] == true);
  r.headers.clear();
  CHECK(handle_request(r, f.service, f.cfg).status == 401);
  r = request(f.cfg);
  r.path = "/api/v1/external/0/filament";
  CHECK(handle_request(r, f.service, f.cfg).status == 404);
  Config c = f.cfg;
  c.http_port = 0;
  HttpServer server(f.service, c);
  server.start();
  auto response = wire(server.port(), "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n");
  CHECK(response.find("HTTP/1.1 200") == 0);
  response = wire(server.port(),
                  "POST /api/v1/ams/0/trays/3/filament HTTP/1.1\r\nContent-Length: 65537\r\n\r\n");
  CHECK(response.find("HTTP/1.1 413") == 0);
  response = wire(server.port(),
                  "POST /api/v1/ams/0/trays/3/filament HTTP/1.1\r\nContent-Length: 1oops\r\n\r\n");
  CHECK(response.find("HTTP/1.1 400") == 0);
  response = wire(server.port(), "GET /api/v1/capabilities HTTP/1.1\r\n\r\n");
  CHECK(response.find("HTTP/1.1 401") == 0);
  CHECK(response.find("WWW-Authenticate: Bearer") != std::string::npos);
  auto raw = "POST /api/v1/ams/0/trays/3/filament HTTP/1.1\r\nAuthorization: Bearer " +
             c.http_token + "\r\nContent-Type: application/json\r\nContent-Length: " +
             std::to_string(body().dump().size()) + "\r\n\r\n" + body().dump();
  response = wire(server.port(), raw);
  CHECK(response.find("HTTP/1.1 200") == 0);
  CHECK(json::parse(response.substr(response.find("\r\n\r\n") + 4))["verified"] == true);
  for (int ams : {external_left_id, external_right_id}) {
    auto external_raw = raw;
    auto start = external_raw.find("/ams/0/");
    CHECK(start != std::string::npos);
    external_raw.replace(start, std::string("/ams/0/trays/3/").size(),
                         "/ams/" + std::to_string(ams) + "/trays/0/");
    response = wire(server.port(), external_raw);
    CHECK(response.find("HTTP/1.1 200") == 0);
    auto result = json::parse(response.substr(response.find("\r\n\r\n") + 4));
    CHECK(result["verified"] == true);
    CHECK(result["amsId"] == ams);
    CHECK(result["trayId"] == 0);
  }
  response = wire(server.port(),
                  "DELETE /api/v1/ams/255/trays/0/filament HTTP/1.1\r\nAuthorization: Bearer " +
                      c.http_token + "\r\n\r\n");
  CHECK(response.find("HTTP/1.1 200") == 0);
  CHECK(json::parse(response.substr(response.find("\r\n\r\n") + 4))["status"] == "cleared");
  server.stop();
}
int main(int argc, char** argv) try {
  CHECK(argc == 2);
  std::string suite = argv[1];
  if (suite == "config")
    configuration();
  else if (suite == "validation")
    validation();
  else if (suite == "service")
    state();
  else if (suite == "verification")
    verification();
  else if (suite == "external")
    external();
  else if (suite == "protocol")
    protocol();
  else if (suite == "logging") {
    logging();
    failure_diagnostics();
  } else if (suite == "concurrency")
    concurrency();
  else if (suite == "clear")
    clear_filament();
  else if (suite == "http")
    http();
  else
    CHECK(false);
  std::cout << suite << " passed\n";
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
