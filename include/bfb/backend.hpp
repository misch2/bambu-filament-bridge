// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <functional>
#include <memory>
#include <string>

#include "bfb/config.hpp"
namespace bfb {
struct BackendCallbacks {
  std::function<void(bool)> connection;
  std::function<void(const std::string&)> message;
  std::function<void()> certificate;
};
// All methods are called by the serialized service command path. Callbacks
// may run synchronously or asynchronously. stop() must join callback producers.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual void start(BackendCallbacks callbacks) = 0;
  virtual void stop() noexcept = 0;
  virtual std::string name() const = 0;
  virtual std::string version() const = 0;
  virtual int detect(std::string& firmware) = 0;
  virtual int connect() = 0;
  virtual void disconnect() = 0;
  virtual void subscribe() = 0;
  virtual int send(const std::string& payload) = 0;
  virtual bool provision(bool lan_only) = 0;
  virtual bool requires_certificate_ack() const { return true; }
  virtual void refresh() = 0;
};
std::unique_ptr<Backend> make_plugin_backend(const Config& config);
}  // namespace bfb
