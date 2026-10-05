// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pthread.h>

#include <csignal>
#include <iostream>

#include "bfb/backend.hpp"
#include "bfb/bridge_service.hpp"
#include "bfb/config.hpp"
#include "bfb/http_server.hpp"
int main() try {
  // Block before creating plugin/HTTP threads; sigwait handles shutdown without
  // running non-signal-safe C++ code in an asynchronous signal handler.
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &signals, nullptr);
  auto config = bfb::load_config();
  bfb::ensure_config(config);
  auto backend = bfb::make_plugin_backend(config);
  std::cout << "[bridge] version=0.1.1 backend=" << backend->name()
            << " pluginVersion=" << backend->version() << " printerId=" << config.printer_id
            << std::endl;
  bfb::BridgeService service(*backend);
  bfb::HttpServer server(service, config);
  server.start();
  service.start();
  std::cout << "[http] listening on " << config.http_bind << ":" << server.port() << std::endl;
  int signal = 0;
  sigwait(&signals, &signal);
  service.stop();
  server.stop();
  std::cout << "[bridge] stopped" << std::endl;
  return 0;
} catch (const std::exception& e) {
  std::cerr << "[fatal] " << e.what() << std::endl;
  return 1;
}
