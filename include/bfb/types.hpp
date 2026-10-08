// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
namespace bfb {
constexpr int external_left_id = 254;
constexpr int external_right_id = 255;
inline bool is_external_slot(int ams_id) {
  return ams_id == external_left_id || ams_id == external_right_id;
}
using Clock = std::chrono::steady_clock;
struct Filament {
  std::string profile, setting, type, color;
  int temp_min = 0, temp_max = 0;
};
struct Health {
  bool connected = false, ready = false, reconnecting = true;
  std::string firmware;
  long long last_message_age_ms = -1;
};
struct WriteResult {
  std::string error, sequence_id;
  bool verified = false;
  long long elapsed_ms = 0;
  int ams_id = 0, tray_id = 0;
  nlohmann::json diagnostics = nlohmann::json::object();
};
struct Timing {
  std::chrono::milliseconds reply{4000}, verification{4000}, stale{6000};
  std::chrono::milliseconds connect{15000}, provision_delay{5000}, cert{10000}, telemetry{3000};
  std::chrono::milliseconds disconnect_delay{500}, refresh{1000}, retry_min{5000}, retry_max{30000};
};
}  // namespace bfb
