// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

#include "bfb/backend.hpp"
#include "bfb/types.hpp"
namespace bfb {
class BridgeService {
 public:
  BridgeService(Backend& backend, Timing timing = {});
  ~BridgeService();
  void start();
  void stop();
  Health health() const;
  std::string backend_name() const { return backend_.name(); }
  std::string plugin_version() const { return backend_.version(); }
  WriteResult set_filament(int ams_id, int tray_id, const Filament& filament);

 private:
  void on_connection(bool connected);
  void on_message(const std::string& message);
  void run();
  bool bring_up(bool reconnect);
  bool pause(std::chrono::milliseconds duration);
  std::string next_sequence();
  int push_all();
  Backend& backend_;
  Timing timing_;
  std::timed_mutex command_mutex_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread worker_;
  bool started_ = false, stopping_ = false, connected_ = false, ready_ = false;
  bool reconnecting_ = true, certificate_ = false;
  std::string firmware_;
  Clock::time_point last_status_{};
  unsigned long long sequence_ = 20000, status_counter_ = 0, epoch_ = 0;
  struct Pending {
    bool active = false, reply = false, accepted = false, verify = false, matches = false;
    std::string sequence;
    int ams = 0, tray = 0;
    Filament expected;
    unsigned long long epoch = 0, after = 0;
    Clock::time_point after_time{};
  } pending_;
};
}  // namespace bfb
