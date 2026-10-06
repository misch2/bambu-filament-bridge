// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>

#include "bfb/backend.hpp"
#include "bfb/types.hpp"
namespace bfb::test {
using json = nlohmann::json;
class FakeBackend : public Backend {
 public:
  BackendCallbacks callbacks;
  std::atomic<bool> auto_connect{true}, telemetry{true}, fail_send{false}, reject{false};
  std::atomic<bool> no_reply{false}, wrong_tray{false}, fail_verify{false}, disconnect_write{false};
  std::atomic<bool> pre_reply_only{false}, hold_write{false}, wrong_sequence{false};
  std::atomic<bool> emit_certificate{true}, require_certificate{true};
  std::atomic<int> mismatch{0};
  // 0: native shape (AMS or vir_slot), 1: legacy vt_tray, 2: AMS-shaped telemetry.
  std::atomic<int> status_layout{0}, external_status_id{-1};
  std::atomic<bool> conflicting_legacy{false}, numeric_slot_id{false};
  std::atomic<int> connects{0}, sends{0}, writes{0}, overlapping{0};
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> sequences;
  json last_command;
  Filament loaded{"GFG99", "GFSG99_15", "PETG", "808080FF", 220, 260};
  std::atomic<int> ams{0}, tray{3};
  void start(BackendCallbacks c) override { callbacks = std::move(c); }
  void stop() noexcept override {}
  std::string name() const override { return "fake"; }
  std::string version() const override { return "02.08.02.99"; }
  int detect(std::string& firmware) override {
    firmware = "test";
    return 0;
  }
  int connect() override {
    ++connects;
    if (auto_connect) callbacks.connection(true);
    return 0;
  }
  void disconnect() override { callbacks.connection(false); }
  void subscribe() override {}
  bool provision(bool) override {
    if (emit_certificate) callbacks.certificate();
    return true;
  }
  bool requires_certificate_ack() const override { return require_certificate; }
  void refresh() override {}
  void emit_status() {
    Filament f;
    {
      std::lock_guard<std::mutex> lock(mutex);
      f = loaded;
    }
    const int slot_id = is_external_slot(ams) ? ams.load() : tray.load();
    json slot{{"id", std::to_string(slot_id)},
              {"tray_info_idx", wrong_tray ? "wrong" : f.profile},
              {"tray_type", f.type},
              {"tray_color", f.color},
              {"nozzle_temp_min", std::to_string(f.temp_min)},
              {"nozzle_temp_max", std::to_string(f.temp_max)},
              {"setting_id", f.setting}};
    switch (mismatch.load()) {
      case 1:
        slot["tray_type"] = "ABS";
        break;
      case 2:
        slot["tray_color"] = "000000FF";
        break;
      case 3:
        slot["nozzle_temp_min"] = "0";
        break;
      case 4:
        slot["nozzle_temp_max"] = "0";
        break;
      case 5:
        slot["setting_id"] = "wrong";
        break;
      case 6:
        slot["id"] = "99";
        break;
      case 7:
        slot["nozzle_temp_max"] = json::object();
        break;
      case 8:
        slot.erase("setting_id");
        break;
      default:
        break;
    }
    json print{{"command", "push_status"}};
    if (status_layout == 1) {
      slot.erase("id");
      print["vt_tray"] = slot;
    } else if (is_external_slot(ams) && status_layout != 2) {
      if (external_status_id >= 0) slot["id"] = external_status_id.load();
      if (numeric_slot_id) slot["id"] = slot_id;
      auto other = slot;
      other["id"] = ams == external_left_id ? external_right_id : external_left_id;
      other["tray_info_idx"] = "other-holder";
      // Include both holders, with the unrelated one first.
      print["vir_slot"] = json::array({other, slot});
      if (conflicting_legacy) {
        print["vt_tray"] = slot;
        print["vt_tray"]["tray_info_idx"] = f.profile;
      }
    } else {
      print["ams"] = {{"ams", json::array({{{"id", std::to_string(ams.load())},
                                            {"tray", json::array({slot})}}})}};
    }
    callbacks.message(json{{"print", print}}.dump());
  }
  void reply() {
    json command;
    {
      std::lock_guard<std::mutex> lock(mutex);
      command = last_command;
    }
    callbacks.message(
        json{{"print",
              {{"command", "ams_filament_setting"},
               {"sequence_id",
                wrong_sequence ? "unrelated" : command.at("sequence_id").get<std::string>()},
               {"result", reject ? "fail" : "success"}}}}
            .dump());
  }
  int send(const std::string& message) override {
    auto root = json::parse(message);
    ++sends;
    const auto& data = root.begin().value();
    {
      std::lock_guard<std::mutex> lock(mutex);
      sequences.push_back(data.at("sequence_id").get<std::string>());
    }
    if (root.contains("print")) {
      if (++overlapping != 1) throw std::runtime_error("overlapping send");
      ++writes;
      {
        std::lock_guard<std::mutex> lock(mutex);
        last_command = data;
        loaded = {data.at("tray_info_idx"), data.at("setting_id"),      data.at("tray_type"),
                  data.at("tray_color"),    data.at("nozzle_temp_min"), data.at("nozzle_temp_max")};
      }
      ams = data.at("ams_id").get<int>();
      tray = data.at("tray_id").get<int>();
      cv.notify_all();
      if (disconnect_write)
        callbacks.connection(false);
      else if (!fail_send) {
        if (pre_reply_only) emit_status();
        if (!no_reply && !hold_write) reply();
      }
      --overlapping;
      return fail_send ? -1 : 0;
    }
    if (root.contains("pushing")) {
      if (writes > 0 && fail_verify) return -1;
      if (telemetry && !(writes > 0 && pre_reply_only)) emit_status();
    }
    return 0;
  }
  void wait_write(int count) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!cv.wait_for(lock, std::chrono::seconds(2), [&] { return writes >= count; }))
      throw std::runtime_error("write not sent");
  }
};
}  // namespace bfb::test
