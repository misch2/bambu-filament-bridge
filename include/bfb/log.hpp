// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>

namespace bfb {
inline void operational_log(const char* layer, nlohmann::json fields) {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream timestamp;
  timestamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  fields["time"] = timestamp.str();
  const auto line = std::string("[") + layer + "] " +
                    fields.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  std::cout << line + "\n";
  std::cout.flush();
}
}  // namespace bfb
