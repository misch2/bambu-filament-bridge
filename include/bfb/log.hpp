// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <chrono>
#include <ctime>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace bfb {
// Keep only explicitly allowed scalar fields from untrusted printer messages.
inline nlohmann::json diagnostic_scalars(const nlohmann::json& source,
                                         std::initializer_list<const char*> fields,
                                         const std::vector<std::string>& secrets = {}) {
  auto result = nlohmann::json::object();
  for (const auto* field : fields) {
    auto value = source.find(field);
    if (value == source.end()) continue;
    if (value->is_string()) {
      auto text = value->get<std::string>();
      // Redact before truncation so a boundary cannot expose a partial secret.
      for (const auto& secret : secrets) {
        if (secret.empty()) continue;
        std::size_t pos = 0;
        while ((pos = text.find(secret, pos)) != std::string::npos) {
          text.replace(pos, secret.size(), "[redacted]");
          pos += std::string("[redacted]").size();
        }
      }
      if (text.size() > 256) {
        std::size_t end = 256;
        // Keep a complete UTF-8 code point at the truncation boundary.
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        text.resize(end);
      }
      result[field] = text;
    } else if (value->is_number() || value->is_boolean() || value->is_null()) {
      result[field] = *value;
    }
  }
  return result;
}
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
