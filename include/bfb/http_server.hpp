// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "bfb/bridge_service.hpp"
#include "bfb/config.hpp"
namespace bfb {
constexpr std::size_t max_body_size = 64 * 1024;
struct HttpRequest {
  std::string method, path;
  std::map<std::string, std::string> headers;
  std::string body;
};
struct HttpResponse {
  int status = 200;
  std::string body;
};
HttpResponse error_response(int status, const std::string& error);
HttpResponse handle_request(const HttpRequest& request, BridgeService& service,
                            const Config& config);
class HttpServer {
 public:
  HttpServer(BridgeService& service, const Config& config);
  ~HttpServer();
  void start();
  void stop();
  int port() const { return port_; }

 private:
  void serve();
  void worker();
  BridgeService& service_;
  Config config_;
  int fd_ = -1, port_ = 0;
  std::atomic<bool> stopping_{false};
  std::thread listener_;
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<int> queue_, active_;
};
}  // namespace bfb
