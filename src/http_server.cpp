// SPDX-License-Identifier: AGPL-3.0-or-later
#include "bfb/http_server.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "bfb/log.hpp"
namespace bfb {
using json = nlohmann::json;
namespace {
std::atomic<unsigned long long> request_sequence{0};
const std::regex filament_route("^/api/v1/ams/([^/]+)/trays/([^/]+)/filament$");
json request_fields(const HttpRequest& r) {
  // Never render arbitrary paths, query strings, methods or headers.
  std::string route = "unknown";
  if (r.path == "/health" || r.path == "/api/v1/capabilities")
    route = r.path;
  else if (std::regex_match(r.path, filament_route))
    route = "/api/v1/ams/{amsId}/trays/{trayId}/filament";
  return {{"method",
           r.method == "GET" || r.method == "POST" || r.method == "DELETE" ? r.method : "other"},
          {"route", route}};
}
class RequestLog {
 public:
  explicit RequestLog(const HttpRequest& r) : fields_(request_fields(r)), started_(Clock::now()) {
    fields_["requestId"] = ++request_sequence;
    event("request_received");
  }
  void event(const char* event_name, const json& detail = json::object()) {
    auto fields = fields_;
    fields.update(detail);
    fields["event"] = event_name;
    operational_log("http", fields);
  }
  void sequence(const std::string& id) {
    if (!id.empty()) fields_["sequenceId"] = id;
  }
  HttpResponse finish(const HttpResponse& response) {
    auto body = json::parse(response.body, nullptr, false);
    json detail{
        {"status", response.status},
        {"elapsedMs",
         std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_).count()}};
    if (body.is_object() && body.contains("error")) detail["error"] = body["error"];
    event("request_completed", detail);
    return response;
  }

 private:
  json fields_;
  Clock::time_point started_;
};
std::string redact(std::string value, const Config& c) {
  for (const auto& secret : {c.http_token, c.access_code}) {
    if (secret.empty()) continue;
    std::size_t pos = 0;
    while ((pos = value.find(secret, pos)) != std::string::npos) {
      value.replace(pos, secret.size(), "[redacted]");
      pos += std::string("[redacted]").size();
    }
  }
  return value;
}
std::string lower(std::string value) {
  for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}
std::string trim(const std::string& value) {
  auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos
             ? ""
             : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
bool authorized(const HttpRequest& request, const std::string& expected) {
  auto it = request.headers.find("authorization");
  if (it == request.headers.end()) return false;
  auto value = trim(it->second);
  auto split = value.find(' ');
  if (split == std::string::npos || lower(value.substr(0, split)) != "bearer") return false;
  auto token = trim(value.substr(split + 1));
  if (token.size() != expected.size()) return false;
  unsigned char diff = 0;
  for (std::size_t i = 0; i < token.size(); ++i)
    diff |= static_cast<unsigned char>(token[i]) ^ static_cast<unsigned char>(expected[i]);
  return diff == 0;
}
int index(const std::string& value, int max, const char* field) {
  if (value.empty() || value.size() > 3 ||
      value.find_first_not_of("0123456789") != std::string::npos)
    throw std::invalid_argument(field);
  int result = std::stoi(value);
  if (result > max) throw std::invalid_argument(field);
  return result;
}
Filament validate(const json& input) {
  Filament f;
  auto string = [&](const char* key) {
    auto it = input.find(key);
    if (it == input.end()) throw std::invalid_argument(std::string("missing_") + key);
    const auto& value = *it;
    if (!value.is_string()) throw std::invalid_argument(key);
    auto s = value.get<std::string>();
    if (trim(s).empty() || s.size() > 256 ||
        std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }))
      throw std::invalid_argument(key);
    return s;
  };
  f.profile = string("profile");
  f.setting = string("setting");
  f.type = string("type");
  f.color = string("color");
  if (f.color.size() != 8 ||
      f.color.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    throw std::invalid_argument("color");
  for (char& c : f.color) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  auto temperature = [&](const char* key) {
    auto it = input.find(key);
    if (it == input.end()) throw std::invalid_argument(std::string("missing_") + key);
    const auto& value = *it;
    if (!value.is_number()) throw std::invalid_argument(key);
    double n = value.get<double>();
    if (!(n >= 0 && n <= 400) || n != static_cast<int>(n)) throw std::invalid_argument(key);
    return static_cast<int>(n);
  };
  f.temp_min = temperature("tempMin");
  f.temp_max = temperature("tempMax");
  if (f.temp_min > f.temp_max) throw std::invalid_argument("temperature_range");
  return f;
}
HttpResponse read_request(int fd, HttpRequest& request) {
  std::string data;
  char buffer[4096];
  std::size_t end;
  auto receive = [&]() {
    ssize_t count;
    do {
      count = recv(fd, buffer, sizeof(buffer), 0);
    } while (count < 0 && errno == EINTR);
    if (count > 0) data.append(buffer, static_cast<std::size_t>(count));
    return count > 0;
  };
  while ((end = data.find("\r\n\r\n")) == std::string::npos) {
    if (!receive()) return error_response(400, "invalid_request");
    if (data.size() > 16 * 1024 && data.find("\r\n\r\n") == std::string::npos)
      return error_response(413, "request_too_large");
  }
  if (end > 16 * 1024) return error_response(413, "request_too_large");
  std::istringstream headers(data.substr(0, end));
  std::string line, version, extra;
  std::getline(headers, line);
  std::istringstream first(line);
  if (!(first >> request.method >> request.path >> version) || (first >> extra) ||
      (version != "HTTP/1.1" && version != "HTTP/1.0"))
    return error_response(400, "invalid_request");
  while (std::getline(headers, line)) {
    auto colon = line.find(':');
    if (colon == std::string::npos) return error_response(400, "invalid_request");
    auto key = lower(trim(line.substr(0, colon)));
    if (key.empty() || request.headers.count(key)) return error_response(400, "invalid_request");
    request.headers[key] = trim(line.substr(colon + 1));
  }
  if (request.headers.count("transfer-encoding")) return error_response(400, "invalid_request");
  std::size_t size = 0;
  if (request.headers.count("content-length")) {
    const auto& length = request.headers.at("content-length");
    if (length.empty() || length.find_first_not_of("0123456789") != std::string::npos)
      return error_response(400, "invalid_request");
    for (char c : length) {
      size = size * 10 + static_cast<unsigned>(c - '0');
      if (size > max_body_size) return error_response(413, "request_too_large");
    }
  }
  const auto body_start = end + 4;
  while (data.size() - body_start < size) {
    if (!receive()) return error_response(400, "invalid_request");
  }
  request.body = data.substr(body_start, size);
  return {0, ""};
}
void respond(int fd, const HttpResponse& response) {
  std::string reason;
  switch (response.status) {
    case 200:
      reason = "OK";
      break;
    case 400:
      reason = "Bad Request";
      break;
    case 401:
      reason = "Unauthorized";
      break;
    case 404:
      reason = "Not Found";
      break;
    case 413:
      reason = "Payload Too Large";
      break;
    case 502:
      reason = "Bad Gateway";
      break;
    case 503:
      reason = "Service Unavailable";
      break;
    case 504:
      reason = "Gateway Timeout";
      break;
    default:
      reason = "Internal Server Error";
  }
  std::string out =
      "HTTP/1.1 " + std::to_string(response.status) + " " + reason +
      "\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\n";
  if (response.status == 401) out += "WWW-Authenticate: Bearer realm=\"bambu-bridge\"\r\n";
  out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n\r\n" + response.body;
  std::size_t sent = 0;
  while (sent < out.size()) {
    ssize_t n = send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    sent += static_cast<std::size_t>(n);
  }
}
}  // namespace
HttpResponse error_response(int status, const std::string& error) {
  return {status, json{{"status", "error"},
                       {"error", error},
                       {"message", error == "printer_not_ready" ? "Printer is currently not ready"
                                                                : "Request could not be completed"}}
                      .dump()};
}
namespace {
HttpResponse handle_request_impl(const HttpRequest& r, BridgeService& service, const Config& c,
                                 RequestLog& log) {
  if (r.method == "GET" && r.path == "/health") {
    auto h = service.health();
    return {h.ready ? 200 : 503, json{{"status", h.ready ? "ready" : "not_ready"},
                                      {"connected", h.connected},
                                      {"ready", h.ready},
                                      {"reconnecting", h.reconnecting},
                                      {"printerId", c.printer_id},
                                      {"printerIp", c.printer_ip},
                                      {"pluginVersion", service.plugin_version()},
                                      {"firmware", h.firmware},
                                      {"lastMessageAgeMs", h.last_message_age_ms}}
                                     .dump()};
  }
  if (r.path.rfind("/api/v1/", 0) == 0 && !authorized(r, c.http_token))
    return error_response(401, "unauthorized");
  if (r.method == "GET" && r.path == "/api/v1/capabilities")
    return {200, json{{"apiVersion", 1},
                      {"backend", service.backend_name()},
                      {"pluginVersion", service.plugin_version()},
                      {"printerId", c.printer_id},
                      {"features", {{"amsFilamentWrite", true}, {"externalFilamentWrite", true}}}}
                     .dump()};
  std::smatch match;
  if ((r.method != "POST" && r.method != "DELETE") ||
      !std::regex_match(r.path, match, filament_route))
    return error_response(404, "not_found");
  if (r.body.size() > max_body_size) return error_response(413, "request_too_large");
  int ams, tray;
  Filament f;
  const bool clear = r.method == "DELETE";
  auto input = clear ? json::object() : json::parse(r.body, nullptr, false);
  if (!input.is_object()) return error_response(400, "invalid_json");
  try {
    ams = index(match[1], external_right_id, "amsId");
    tray = index(match[2], 3, "trayId");
    if (!clear) {
      auto content = r.headers.find("content-type");
      if (content == r.headers.end() ||
          lower(trim(content->second.substr(0, content->second.find(';')))) != "application/json")
        return error_response(400, "invalid_content_type");
      f = validate(input);
    }
  } catch (const std::invalid_argument& e) {
    log.event("validation_failed", {{"field", e.what()}});
    return error_response(400, "invalid_request");
  } catch (const json::exception&) {
    log.event("validation_failed", {{"field", "filament"}});
    return error_response(400, "invalid_request");
  }
  if (clear)
    log.event("filament_clear_requested", {{"amsId", ams}, {"trayId", tray}});
  else
    log.event("filament_requested", {{"amsId", ams},
                                     {"trayId", tray},
                                     {"filament",
                                      {{"profile", redact(f.profile, c)},
                                       {"setting", redact(f.setting, c)},
                                       {"type", redact(f.type, c)},
                                       {"color", redact(f.color, c)},
                                       {"tempMin", f.temp_min},
                                       {"tempMax", f.temp_max}}}});
  auto result = clear ? service.clear_filament(ams, tray) : service.set_filament(ams, tray, f);
  log.sequence(result.sequence_id);
  if (!result.verified) {
    int status = 502;
    if (result.error == "printer_not_ready" || result.error == "printer_busy")
      status = 503;
    else if (result.error == "printer_reply_timeout" || result.error == "verification_timeout")
      status = 504;
    return error_response(status, result.error);
  }
  return {200, json{{"status", clear ? "cleared" : "synced"},
                    {"verified", true},
                    {"elapsedMs", result.elapsed_ms},
                    {"sequenceId", result.sequence_id},
                    {"amsId", result.ams_id},
                    {"trayId", result.tray_id}}
                   .dump()};
}
}  // namespace
HttpResponse handle_request(const HttpRequest& r, BridgeService& service, const Config& c) {
  RequestLog log(r);
  try {
    return log.finish(handle_request_impl(r, service, c, log));
  } catch (const std::exception&) {
    log.finish(error_response(500, "internal_error"));
    throw;
  }
}
HttpServer::HttpServer(BridgeService& service, const Config& config)
    : service_(service), config_(config) {}
HttpServer::~HttpServer() { stop(); }
void HttpServer::start() {
  if (fd_ >= 0) throw std::logic_error("HTTP server already started");
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) throw std::runtime_error("HTTP socket failed");
  try {
    int reuse = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(config_.http_port));
    if (inet_pton(AF_INET, config_.http_bind.c_str(), &address.sin_addr) != 1 ||
        bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(fd_, 16) != 0)
      throw std::runtime_error("HTTP bind/listen failed");
    socklen_t size = sizeof(address);
    getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &size);
    port_ = ntohs(address.sin_port);
    for (int i = 0; i < 4; ++i) workers_.emplace_back([this] { worker(); });
    listener_ = std::thread([this] { serve(); });
  } catch (...) {
    stop();
    throw;
  }
}
void HttpServer::stop() {
  stopping_ = true;
  cv_.notify_all();
  if (listener_.joinable()) listener_.join();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int fd : queue_) close(fd);
    queue_.clear();
    for (int fd : active_) shutdown(fd, SHUT_RDWR);
  }
  for (auto& thread : workers_)
    if (thread.joinable()) thread.join();
  workers_.clear();
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}
void HttpServer::serve() {
  while (!stopping_) {
    pollfd poller{fd_, POLLIN, 0};
    if (poll(&poller, 1, 100) <= 0 || !(poller.revents & POLLIN)) continue;
    int fd = accept(fd_, nullptr, nullptr);
    if (fd < 0) continue;
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.size() >= 16 || stopping_) {
        close(fd);
        continue;
      }
      queue_.push_back(fd);
    }
    cv_.notify_one();
  }
}
void HttpServer::worker() {
  while (true) {
    int fd;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      fd = queue_.front();
      queue_.erase(queue_.begin());
      active_.push_back(fd);
    }
    try {
      HttpRequest request;
      auto error = read_request(fd, request);
      if (error.status) {
        RequestLog log(request);
        log.event("request_read_failed");
        log.finish(error);
      }
      respond(fd, error.status ? error : handle_request(request, service_, config_));
    } catch (const std::exception&) {
      respond(fd, error_response(500, "internal_error"));
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_.erase(std::find(active_.begin(), active_.end(), fd));
      close(fd);
    }
  }
}
}  // namespace bfb
