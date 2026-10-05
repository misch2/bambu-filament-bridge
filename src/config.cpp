// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bfb/config.hpp"

#include <arpa/inet.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace bfb {
Config load_config(Environment env) {
  auto get = [&](const char* name, std::string fallback = "") {
    const char* value = env(name);
    return value ? std::string(value) : fallback;
  };
  auto secret = [&](const char* name) {
    // Presence, including an empty value, takes precedence over *_FILE.
    if (env(name)) return get(name);
    std::string file_key = std::string(name) + "_FILE";
    std::string path = get(file_key.c_str());
    if (path.empty()) return std::string{};
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error(file_key + " cannot be read");
    std::string value;
    char c;
    while (file.get(c)) {
      if (value.size() >= 4096) throw std::runtime_error(file_key + " is too large");
      value += c;
    }
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
  };
  Config c;
  c.printer_id = get("BAMBU_DEV_ID");
  c.printer_ip = get("BAMBU_DEV_IP");
  c.access_code = secret("BAMBU_ACCESS_CODE");
  c.http_token = secret("BAMBU_HTTP_TOKEN");
  for (auto item : {std::pair<const char*, const std::string&>{"BAMBU_DEV_ID", c.printer_id},
                    {"BAMBU_DEV_IP", c.printer_ip},
                    {"BAMBU_ACCESS_CODE", c.access_code},
                    {"BAMBU_HTTP_TOKEN", c.http_token}}) {
    if (item.second.empty()) throw std::runtime_error(std::string(item.first) + " is required");
    if (item.second.find_first_of("\r\n") != std::string::npos ||
        item.second.find('\0') != std::string::npos)
      throw std::runtime_error(std::string(item.first) + " contains invalid characters");
  }
  if (c.printer_id.size() > 128 ||
      c.printer_id.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
    throw std::runtime_error("BAMBU_DEV_ID must be an alphanumeric device ID");
  if (c.http_token.size() < 32 || c.http_token.size() > 4096)
    throw std::runtime_error("BAMBU_HTTP_TOKEN must contain 32..4096 characters");
  if (c.http_token.find_first_of(" \t\r\n") != std::string::npos)
    throw std::runtime_error("BAMBU_HTTP_TOKEN must not contain whitespace");
  c.plugin = get("BAMBU_PLUGIN", c.plugin);
  c.data_dir = get("BAMBU_DATA_DIR", c.data_dir);
  c.cert_dir = get("BAMBU_CERT_DIR", c.data_dir + "/certs");
  c.http_bind = get("BAMBU_HTTP_BIND", c.http_bind);
  auto port = get("BAMBU_HTTP_PORT", "8080");
  if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("BAMBU_HTTP_PORT must be an integer in 1..65535");
  c.http_port = std::stoi(port);
  if (c.http_port < 1 || c.http_port > 65535)
    throw std::runtime_error("BAMBU_HTTP_PORT must be in 1..65535");
  in_addr address{};
  if (inet_pton(AF_INET, c.printer_ip.c_str(), &address) != 1)
    throw std::runtime_error("BAMBU_DEV_IP must be IPv4");
  if (inet_pton(AF_INET, c.http_bind.c_str(), &address) != 1)
    throw std::runtime_error("BAMBU_HTTP_BIND must be IPv4");
  if (c.plugin.empty() || c.data_dir.empty() || c.cert_dir.empty())
    throw std::runtime_error("Runtime paths must not be empty");
  return c;
}
Config load_config() {
  return load_config([](const char* name) { return std::getenv(name); });
}
void ensure_config(const Config& c) {
  std::filesystem::create_directories(c.data_dir);
  std::filesystem::create_directories(c.cert_dir);
  auto filename = std::filesystem::path(c.data_dir) / "BambuStudio.conf";
  if (std::filesystem::exists(filename)) return;
  std::ofstream file(filename);
  if (!file) throw std::runtime_error("Cannot create runtime configuration");
  file << R"({"app":{"country_code":"CZ","language":"en_US","region":"CZ"}})";
}
}  // namespace bfb
