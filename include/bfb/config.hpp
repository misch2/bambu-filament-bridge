// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <functional>
#include <string>
namespace bfb {
struct Config {
  std::string printer_id, printer_ip, access_code, http_token;
  std::string plugin = "/usr/local/lib/bfb/libbambu_networking.so";
  std::string cert_dir = "/data/certs", data_dir = "/data";
  std::string http_bind = "0.0.0.0";
  int http_port = 8080;
};
using Environment = std::function<const char*(const char*)>;
Config load_config(Environment env);
Config load_config();
void ensure_config(const Config& config);
}  // namespace bfb
