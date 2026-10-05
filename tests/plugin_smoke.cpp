// SPDX-License-Identifier: AGPL-3.0-or-later
#include <iostream>

#include "bfb/backend.hpp"
// Explicit packaging check only: not registered in CTest, no printer connection.
int main(int argc, char** argv) try {
  if (argc != 2) return 2;
  bfb::Config config;
  config.plugin = argv[1];
  auto plugin = bfb::make_plugin_backend(config);
  if (plugin->name() != "open-bamboo-networking" || plugin->version().substr(0, 8) != "02.08.02")
    return 1;
  std::cout << plugin->name() << ' ' << plugin->version() << '\n';
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << '\n';
  return 1;
}
