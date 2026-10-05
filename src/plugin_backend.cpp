// SPDX-License-Identifier: AGPL-3.0-or-later
#include <dlfcn.h>

#include <iostream>
#include <stdexcept>

#include "bfb/backend.hpp"
#include "plugin_loader.hpp"
namespace bfb {
namespace pr = obn::plugin_runner;
namespace {
class PluginBackend final : public Backend {
 public:
  explicit PluginBackend(Config config) : config_(std::move(config)) {
    exports_ = pr::load(config_.plugin);
    name_ = dlsym(exports_.dl_handle, "obn_get_lan_serial") ? "open-bamboo-networking"
                                                            : "user-provided";
    if (!exports_.bind_detect || !exports_.send_message_to_printer || !exports_.change_user ||
        !exports_.enable_multi_machine || !exports_.start_discovery ||
        !exports_.set_extra_http_header || !exports_.install_device_cert) {
      pr::unload(exports_);
      throw std::runtime_error("Plugin lacks required LAN/filament ABI operations");
    }
  }
  ~PluginBackend() override {
    stop();
    pr::unload(exports_);
  }
  std::string name() const override { return name_; }
  std::string version() const override { return exports_.version; }
  void start(BackendCallbacks callbacks) override {
    callbacks_ = std::move(callbacks);
    agent_ = exports_.create_agent(config_.data_dir);
    if (!agent_) throw std::runtime_error("create_agent returned nullptr");
    auto& ex = exports_;
    ex.set_on_local_message_fn(agent_, [this](std::string device, std::string message) {
      if (device == config_.printer_id) callbacks_.message(message);
    });
    ex.set_on_message_fn(agent_, [this](std::string device, std::string message) {
      if (device == config_.printer_id && message == "device_cert_installed")
        callbacks_.certificate();
    });
    ex.set_on_user_message_fn(agent_, [](std::string, std::string) {});
    ex.set_on_local_connect_fn(agent_, [this](int status, std::string device, std::string) {
      if (device == config_.printer_id) callbacks_.connection(status == BBL::ConnectStatusOk);
    });
    ex.set_on_printer_connected_fn(agent_, [](std::string) {});
    ex.set_on_server_connected_fn(agent_, [](int, int) {});
    ex.set_on_http_error_fn(agent_, [](unsigned status, std::string) {
      std::cerr << "[backend] plugin HTTP error status=" << status << std::endl;
    });
    ex.set_on_subscribe_failure_fn(
        agent_, [](std::string) { std::cerr << "[backend] subscribe failure" << std::endl; });
    ex.set_get_country_code_fn(agent_, [] { return std::string("CZ"); });
    if (ex.set_queue_on_main_fn)
      ex.set_queue_on_main_fn(agent_, [](std::function<void()> fn) {
        if (fn) fn();
      });
    ex.set_server_callback(agent_, [](std::string, int) {});
    ex.set_on_ssdp_msg_fn(agent_, [](std::string) {});
    ex.set_config_dir(agent_, config_.data_dir);
    ex.init_log(agent_);
    ex.set_cert_file(agent_, config_.cert_dir, "slicer_base64.cer");
    ex.set_extra_http_header(agent_, {{"X-BBL-Client-Type", "slicer"},
                                      {"X-BBL-Client-Name", "BambuBridge"},
                                      {"X-BBL-Client-Version", "02.08.02.61"},
                                      {"X-BBL-OS-Type", "linux"},
                                      {"X-BBL-OS-Version", "1.0.0"},
                                      {"X-BBL-Language", "en"}});
    ex.set_country_code(agent_, "CZ");
    if (ex.start(agent_) != 0) throw std::runtime_error("bambu_network_start failed");
    ex.enable_multi_machine(agent_, false);
    ex.start_discovery(agent_, true, false);
    if (ex.change_user(agent_, "") != 0) throw std::runtime_error("change_user failed");
  }
  void stop() noexcept override {
    if (!agent_) return;
    // destroy_agent joins the plugin's callback threads. Do not clear callbacks
    // before destruction, or unload the library while those threads exist.
    exports_.disconnect_printer(agent_);
    exports_.start_discovery(agent_, false, false);
    exports_.destroy_agent(agent_);
    agent_ = nullptr;
    callbacks_ = {};
  }
  int detect(std::string& firmware) override {
    BBL::detectResult detect{};
    int rc = exports_.bind_detect(agent_, config_.printer_ip, "secure", detect);
    firmware = detect.version;
    return rc;
  }
  int connect() override {
    return exports_.connect_printer(agent_, config_.printer_id, config_.printer_ip, "bblp",
                                    config_.access_code, true);
  }
  void disconnect() override { exports_.disconnect_printer(agent_); }
  void subscribe() override { exports_.start_subscribe(agent_, "app"); }
  int send(const std::string& payload) override {
    return exports_.send_message_to_printer(agent_, config_.printer_id, payload, 1, 0);
  }
  bool provision(bool lan_only) override {
    exports_.install_device_cert(agent_, config_.printer_id, lan_only);
    return true;
  }
  // The open backend's Developer Mode path needs no private slicer signing
  // material and emits no device_cert_installed event in that mode. Stock
  // plugins retain the prototype's mandatory acknowledgement wait.
  bool requires_certificate_ack() const override { return name_ != "open-bamboo-networking"; }
  void refresh() override {
    if (exports_.refresh_connection) exports_.refresh_connection(agent_);
  }

 private:
  Config config_;
  pr::PluginExports exports_;
  void* agent_ = nullptr;  // opaque ABI handle owned until destroy_agent
  BackendCallbacks callbacks_;
  std::string name_;
};
}  // namespace
std::unique_ptr<Backend> make_plugin_backend(const Config& config) {
  return std::make_unique<PluginBackend>(config);
}
}  // namespace bfb
