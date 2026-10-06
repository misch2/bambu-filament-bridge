// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bfb/bridge_service.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace bfb {
using json = nlohmann::json;
namespace {
std::string text(const json& object, const char* key) {
  auto it = object.find(key);
  if (it == object.end()) return {};
  if (it->is_string()) return it->get<std::string>();
  if (it->is_number_integer()) return it->dump();
  return {};
}
bool valid_status(const json& print) {
  if (text(print, "command") != "push_status") return false;
  return (print.contains("ams") && print["ams"].is_object() && print["ams"].contains("ams") &&
          print["ams"]["ams"].is_array()) ||
         (print.contains("vir_slot") && print["vir_slot"].is_array()) ||
         (print.contains("vt_tray") && print["vt_tray"].is_object()) ||
         (print.contains("gcode_state") && print["gcode_state"].is_string());
}
const json* target_slot(const json& print, int ams_id, int tray_id) {
  if (is_external_slot(ams_id)) {
    if (print.contains("vir_slot") && print["vir_slot"].is_array()) {
      for (const auto& slot : print["vir_slot"])
        if (slot.is_object() && text(slot, "id") == std::to_string(ams_id)) return &slot;
    }
    // The legacy single external slot is always the main/right slot.
    if (ams_id == external_right_id && print.contains("vt_tray") && print["vt_tray"].is_object())
      return &print["vt_tray"];
    return nullptr;
  }
  if (!print.contains("ams") || !print["ams"].is_object() || !print["ams"].contains("ams") ||
      !print["ams"]["ams"].is_array())
    return nullptr;
  for (const auto& ams : print["ams"]["ams"]) {
    if (!ams.is_object() || text(ams, "id") != std::to_string(ams_id) || !ams.contains("tray") ||
        !ams["tray"].is_array())
      continue;
    for (const auto& tray : ams["tray"])
      if (tray.is_object() && text(tray, "id") == std::to_string(tray_id)) return &tray;
  }
  return nullptr;
}
}  // namespace
BridgeService::BridgeService(Backend& backend, Timing timing)
    : backend_(backend), timing_(timing) {}
BridgeService::~BridgeService() { stop(); }
void BridgeService::start() {
  if (started_) throw std::logic_error("Bridge already started");
  started_ = true;
  try {
    backend_.start({[this](bool c) { on_connection(c); },
                    [this](const std::string& m) { on_message(m); },
                    [this] {
                      std::lock_guard<std::mutex> lock(mutex_);
                      certificate_ = true;
                      cv_.notify_all();
                    }});
    worker_ = std::thread([this] { run(); });
  } catch (...) {
    stop();
    throw;
  }
}
void BridgeService::stop() {
  if (!started_) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    ready_ = false;
    pending_.active = false;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  // Wait for any HTTP command, then destroy plugin callback producers while
  // this object and its callback state are still alive.
  std::lock_guard<std::timed_mutex> command(command_mutex_);
  backend_.stop();
  started_ = false;
}
Health BridgeService::health() const {
  std::lock_guard<std::mutex> lock(mutex_);
  long long age =
      last_status_ == Clock::time_point{}
          ? -1
          : std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - last_status_)
                .count();
  bool ready = ready_ && connected_ && !stopping_ && age >= 0 && age <= timing_.stale.count();
  return {connected_, ready, reconnecting_ || !ready, firmware_, age};
}
void BridgeService::on_connection(bool connected) {
  bool changed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    changed = connected_ != (connected && !stopping_);
    connected_ = connected && !stopping_;
    if (!connected_) {
      ++epoch_;
      ready_ = false;
      last_status_ = {};
      pending_.active = false;
    }
  }
  if (changed) std::cout << "[state] " << (connected ? "CONNECTED" : "DISCONNECTED") << std::endl;
  cv_.notify_all();
}
void BridgeService::on_message(const std::string& message) {
  const auto received = Clock::now();
  if (message.size() > 1024 * 1024) return;
  try {
    auto root = json::parse(message, nullptr, false);
    if (!root.is_object() || !root.contains("print") || !root["print"].is_object()) return;
    const auto& print = root["print"];
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || !connected_) return;
    const bool status = valid_status(print);
    if (status) {
      last_status_ = std::max(last_status_, received);
      ++status_counter_;
    }
    if (pending_.active && pending_.epoch == epoch_) {
      if (text(print, "command") == "ams_filament_setting" &&
          text(print, "sequence_id") == pending_.sequence && !pending_.reply &&
          print.contains("result") && print["result"].is_string()) {
        pending_.reply = true;
        pending_.accepted = text(print, "result") == "success";
      }
      if (status && pending_.verify && status_counter_ > pending_.after &&
          received > pending_.after_time) {
        if (const auto* slot = target_slot(print, pending_.ams, pending_.tray)) {
          const auto& tray = *slot;
          const auto& f = pending_.expected;
          // Stock telemetry does not reliably echo setting_id. Preserve the
          // prototype's physical-slot comparison; compare it when supplied.
          bool matches = text(tray, "tray_info_idx") == f.profile &&
                         text(tray, "tray_type") == f.type && text(tray, "tray_color") == f.color &&
                         text(tray, "nozzle_temp_min") == std::to_string(f.temp_min) &&
                         text(tray, "nozzle_temp_max") == std::to_string(f.temp_max) &&
                         (!tray.contains("setting_id") || text(tray, "setting_id") == f.setting);
          pending_.matches = pending_.matches || matches;
        }
      }
    }
    cv_.notify_all();
  } catch (const json::exception&) {
    // Malformed asynchronous input is ignored; never render the payload.
  }
}
std::string BridgeService::next_sequence() { return std::to_string(++sequence_); }
int BridgeService::push_all() {
  return backend_.send(json{{"pushing",
                             {{"sequence_id", next_sequence()},
                              {"command", "pushall"},
                              {"version", 1},
                              {"push_target", 1}}}}
                           .dump());
}
bool BridgeService::pause(std::chrono::milliseconds duration) {
  std::unique_lock<std::mutex> lock(mutex_);
  return !cv_.wait_for(lock, duration, [this] { return stopping_; });
}
bool BridgeService::bring_up(bool reconnect) {
  std::unique_lock<std::timed_mutex> command(command_mutex_);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return false;
    reconnecting_ = true;
    ready_ = connected_ = certificate_ = false;
    last_status_ = {};
    ++epoch_;
    pending_.active = false;
  }
  std::cout << "[state] " << (reconnect ? "RECONNECTING" : "CONNECTING") << std::endl;
  auto fail = [&](const char* error) {
    std::cerr << "[connection] " << error << std::endl;
    backend_.disconnect();
    on_connection(false);
    return false;
  };
  if (reconnect) {
    backend_.disconnect();
    if (!pause(timing_.disconnect_delay)) return false;
  }
  std::string firmware;
  if (backend_.detect(firmware) != 0) return fail("bind_detect failed");
  {
    std::lock_guard<std::mutex> lock(mutex_);
    firmware_ = firmware.size() <= 256 ? firmware : "";
  }
  if (backend_.connect() != 0) return fail("connect_printer failed");
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timing_.connect, [this] { return stopping_ || connected_; });
    if (stopping_) return false;
    if (!connected_) {
      lock.unlock();
      return fail("LAN MQTT connection timeout");
    }
  }
  backend_.subscribe();
  if (push_all() != 0) return fail("initial pushall failed");
  backend_.send(
      json{{"info", {{"sequence_id", next_sequence()}, {"command", "get_version"}}}}.dump());
  backend_.send(
      json{{"system", {{"sequence_id", next_sequence()}, {"command", "get_access_code"}}}}.dump());
  std::cout << "[state] PROVISIONING" << std::endl;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    certificate_ = false;
  }
  if (!backend_.provision(true)) return fail("install_device_cert unavailable");
  if (!pause(timing_.provision_delay)) return false;
  if (!backend_.provision(false)) return fail("install_device_cert unavailable");
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (backend_.requires_certificate_ack())
      cv_.wait_for(lock, timing_.cert, [this] { return stopping_ || !connected_ || certificate_; });
    if (stopping_) return false;
    if ((!certificate_ && backend_.requires_certificate_ack()) || !connected_) {
      lock.unlock();
      return fail("device certificate timeout or disconnect");
    }
  }
  unsigned long long before, epoch;
  const auto requested = Clock::now();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    before = status_counter_;
    epoch = epoch_;
  }
  if (push_all() != 0) return fail("post-provision pushall failed");
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timing_.telemetry, [&] {
      return stopping_ || epoch_ != epoch || (status_counter_ > before && last_status_ > requested);
    });
    if (stopping_) return false;
    if (!connected_ || epoch_ != epoch || status_counter_ <= before || last_status_ <= requested) {
      lock.unlock();
      return fail("no fresh telemetry after provisioning");
    }
    ready_ = true;
    reconnecting_ = false;
  }
  std::cout << "[state] READY" << std::endl;
  return true;
}
void BridgeService::run() {
  bool reconnect = false;
  auto backoff = timing_.retry_min;
  while (true) {
    try {
      if (!health().ready) {
        bool ok = bring_up(reconnect);
        reconnect = true;
        if (!ok) {
          if (!pause(backoff)) break;
          backoff = std::min(backoff * 2, timing_.retry_max);
          continue;
        }
        backoff = timing_.retry_min;
      } else {
        std::lock_guard<std::timed_mutex> command(command_mutex_);
        backend_.refresh();
      }
    } catch (const std::exception&) {
      std::cerr << "[connection] backend operation failed" << std::endl;
      on_connection(false);
      if (!pause(backoff)) break;
      backoff = std::min(backoff * 2, timing_.retry_max);
    }
    if (!pause(timing_.refresh)) break;
  }
}
WriteResult BridgeService::set_filament(int ams, int tray, const Filament& f) {
  WriteResult result;
  result.ams_id = ams;
  result.tray_id = tray;
  const auto started = Clock::now();
  auto fail = [&](const char* error) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_.active = false;
    }
    result.error = error;
    result.elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    std::cout << "[command] sequence=" << result.sequence_id << " outcome=" << error
              << " elapsedMs=" << result.elapsed_ms << std::endl;
    return result;
  };
  // Quick refusal while reconnect owns the command lock. A bounded queue also
  // keeps the total request time within the existing client's 12s timeout.
  if (!health().ready) {
    result.error = "printer_not_ready";
    return result;
  }
  std::unique_lock<std::timed_mutex> command(command_mutex_, std::defer_lock);
  if (!command.try_lock_for(std::chrono::seconds(1))) {
    result.error = "printer_busy";
    return result;
  }
  if (!health().ready) return fail("printer_not_ready");
  result.sequence_id = next_sequence();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = {};
    pending_.active = true;
    pending_.sequence = result.sequence_id;
    pending_.ams = ams;
    pending_.tray = tray;
    pending_.expected = f;
    pending_.epoch = epoch_;
  }
  json payload{{"print",
                {{"sequence_id", result.sequence_id},
                 {"command", "ams_filament_setting"},
                 {"ams_id", ams},
                 // Preserve the prototype's external-slot wire encoding for both holders.
                 {"tray_id", is_external_slot(ams) ? external_left_id : tray},
                 {"tray_info_idx", f.profile},
                 {"setting_id", f.setting},
                 {"tray_color", f.color},
                 {"nozzle_temp_min", f.temp_min},
                 {"nozzle_temp_max", f.temp_max},
                 {"tray_type", f.type}}}};
  std::cout << "[command] sequence=" << result.sequence_id << " send" << std::endl;
  try {
    if (backend_.send(payload.dump()) != 0) {
      on_connection(false);
      return fail("send_failed");
    }
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, timing_.reply, [this] { return !pending_.active || pending_.reply; });
      if (!pending_.active) {
        lock.unlock();
        return fail("printer_not_ready");
      }
      if (!pending_.reply) {
        lock.unlock();
        return fail("printer_reply_timeout");
      }
      if (!pending_.accepted) {
        lock.unlock();
        return fail("printer_rejected");
      }
      pending_.verify = true;
      pending_.after = status_counter_;
      pending_.after_time = Clock::now();
    }
    if (push_all() != 0) return fail("verification_request_failed");
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, timing_.verification,
                   [this] { return !pending_.active || pending_.matches; });
      if (!pending_.active) {
        lock.unlock();
        return fail("printer_not_ready");
      }
      if (!pending_.matches) {
        lock.unlock();
        return fail("verification_timeout");
      }
      pending_.active = false;
    }
    result.verified = true;
    result.elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    std::cout << "[command] sequence=" << result.sequence_id
              << " verified elapsedMs=" << result.elapsed_ms << std::endl;
    return result;
  } catch (const std::exception&) {
    on_connection(false);
    return fail("backend_failure");
  }
}
}  // namespace bfb
