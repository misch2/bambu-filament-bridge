// SPDX-License-Identifier: AGPL-3.0-or-later
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

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
  BridgeService service;
  Config cfg = config();
  explicit Fixture(Timing t = timing(), bool start_ready = true, bool certificate_ack = true)
      : service(fake, t) {
    fake.auto_connect = start_ready;
    fake.require_certificate = fake.emit_certificate = certificate_ack;
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
  for (auto path : {"/api/v1/ams/-1/trays/0/filament", "/api/v1/ams/254/trays/0/filament",
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
      {"verification_request_failed", 502, [](auto& b) { b.fail_verify = true; }},
      {"verification_timeout", 504, [](auto& b) { b.telemetry = false; }},
      {"verification_timeout", 504, [](auto& b) { b.wrong_tray = true; }},
      {"verification_timeout", 504, [](auto& b) { b.pre_reply_only = true; }},
      {"printer_not_ready", 503, [](auto& b) { b.disconnect_write = true; }}};
  for (auto& c : cases) {
    Fixture f;
    c.setup(f.fake);
    auto r = handle_request(request(f.cfg), f.service, f.cfg);
    CHECK(r.status == c.code);
    CHECK(json::parse(r.body)["error"] == c.error);
  }
  for (int mismatch = 1; mismatch <= 7; ++mismatch) {
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
    return f.service.set_filament(0, 3, {"GFG99", "GFSG99_15", "PETG", "808080FF", 220, 260});
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
    return f.service.set_filament(0, 3, {"GFG99", "GFSG99_15", "PETG", "808080FF", 220, 260});
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
  CHECK(j["features"]["externalFilamentWrite"] == false);
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
  else if (suite == "concurrency")
    concurrency();
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
