#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "plugin_loader.hpp"

namespace pr = obn::plugin_runner;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int)
{
    g_stop = true;
}

std::string env_required(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || !*value)
        throw std::runtime_error(std::string("Missing environment variable: ") + name);
    return value;
}

std::string env_or(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    return (value && *value) ? value : fallback;
}

class SignalLatch {
public:
    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        signalled_ = false;
    }

    void signal()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signalled_ = true;
        }
        cv_.notify_all();
    }

    bool wait_for(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] {
            return signalled_;
        });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool signalled_ = false;
};

void ensure_config(const std::string& data_dir)
{
    std::filesystem::create_directories(data_dir);

    const std::string filename = data_dir + "/BambuStudio.conf";

    if (std::filesystem::exists(filename))
        return;

    std::ofstream out(filename);
    if (!out)
        throw std::runtime_error("Cannot create " + filename);

    // Same minimal config shape used by plugin_runner.
    out <<
R"({
  "app": {
    "country_code": "CZ",
    "language": "en_US",
    "region": "CZ"
  }
}
)";
}

} // namespace

int main()
try {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const std::string dev_id =
        env_required("BAMBU_DEV_ID");

    const std::string dev_ip =
        env_required("BAMBU_DEV_IP");

    const std::string access_code =
        env_required("BAMBU_ACCESS_CODE");

    const std::string plugin_path =
        env_or(
            "BAMBU_PLUGIN",
            "/home/runner/.cache/obn-plugin-runner/"
            "02.08.02.54/libbambu_networking.so"
        );

    const std::string cert_dir =
        env_or(
            "BAMBU_CERT_DIR",
            "/home/runner/bambu-certs"
        );

    const std::string data_dir =
        env_or(
            "BAMBU_DATA_DIR",
            "/home/runner/.local/share/bambu-bridge"
        );

    ensure_config(data_dir);

    std::cout << "[bridge] loading plugin: "
              << plugin_path << std::endl;

    pr::PluginExports ex = pr::load(plugin_path);

    std::cout << "[bridge] plugin version: "
              << ex.version << std::endl;

    void* agent = ex.create_agent(data_dir);
    if (!agent)
        throw std::runtime_error("create_agent returned nullptr");

    SignalLatch connected;
    SignalLatch cert_installed;

    /*
     * The stock plugin expects all callbacks to be installed before start().
     * Most are intentionally quiet here — unlike plugin_runner we do not want
     * to dump every push_status packet to stdout.
     */

    ex.set_on_local_message_fn(
        agent,
        [](std::string, std::string) {
            // Intentionally ignored in v0.1.
            // Later this is where we'll parse AMS push_status.
        }
    );

    ex.set_on_message_fn(
        agent,
        [&cert_installed](std::string dev_id_cb, std::string msg) {
            if (msg == "device_cert_installed") {
                std::cout
                    << "[security] device_cert_installed for "
                    << dev_id_cb
                    << std::endl;

                cert_installed.signal();
            }
        }
    );

    ex.set_on_user_message_fn(
        agent,
        [](std::string, std::string) {}
    );

    ex.set_on_local_connect_fn(
        agent,
        [&connected](int status,
                     std::string dev_id_cb,
                     std::string msg) {

            if (status == BBL::ConnectStatusOk) {
                std::cout
                    << "[state] CONNECTED: "
                    << dev_id_cb
                    << std::endl;

                connected.signal();
            }
            else if (status == BBL::ConnectStatusLost) {
                std::cout
                    << "[state] DISCONNECTED: "
                    << dev_id_cb
                    << " (" << msg << ")"
                    << std::endl;
            }
            else {
                std::cout
                    << "[state] connection status="
                    << status
                    << " dev=" << dev_id_cb
                    << " msg=" << msg
                    << std::endl;
            }
        }
    );

    ex.set_on_printer_connected_fn(
        agent,
        [](std::string) {}
    );

    ex.set_on_server_connected_fn(
        agent,
        [](int, int) {}
    );

    ex.set_on_http_error_fn(
        agent,
        [](unsigned status, std::string body) {
            std::cerr
                << "[http] error "
                << status
                << ": "
                << body
                << std::endl;
        }
    );

    ex.set_on_subscribe_failure_fn(
        agent,
        [](std::string topic) {
            std::cerr
                << "[mqtt] subscribe failure: "
                << topic
                << std::endl;
        }
    );

    ex.set_get_country_code_fn(
        agent,
        [] {
            return std::string("CZ");
        }
    );

    if (ex.set_queue_on_main_fn) {
        ex.set_queue_on_main_fn(
            agent,
            [](std::function<void()> fn) {
                if (fn)
                    fn();
            }
        );
    }

    ex.set_server_callback(
        agent,
        [](std::string, int) {}
    );

    ex.set_on_ssdp_msg_fn(
        agent,
        [](std::string) {}
    );

    /*
     * Same init order as Bambu Studio / plugin_runner.
     */
    ex.set_config_dir(agent, data_dir);
    ex.init_log(agent);

    ex.set_cert_file(
        agent,
        cert_dir,
        "slicer_base64.cer"
    );

    std::map<std::string, std::string> headers{
        {"X-BBL-Client-Type",    "slicer"},
        {"X-BBL-Client-Name",    "BambuBridge"},
        {"X-BBL-Client-Version", "02.08.02.61"},
        {"X-BBL-OS-Type",        "linux"},
        {"X-BBL-OS-Version",     "1.0.0"},
        {"X-BBL-Language",       "en"}
    };

    ex.set_extra_http_header(agent, headers);
    ex.set_country_code(agent, "CZ");

    const int start_rc = ex.start(agent);
    if (start_rc != 0)
        throw std::runtime_error(
            "bambu_network_start failed: " +
            std::to_string(start_rc)
        );

    ex.enable_multi_machine(agent, false);
    ex.start_discovery(agent, true, false);

    // Required even for a logged-out local session.
    const int user_rc = ex.change_user(agent, "");
    if (user_rc != 0)
        throw std::runtime_error(
            "change_user failed: " +
            std::to_string(user_rc)
        );

    std::cout << "[state] CONNECTING" << std::endl;

    /*
     * bind_detect must happen before connect_printer.
     * It tells the stock plugin that this is a cloud-bound printer.
     */
    BBL::detectResult detect{};

    const int detect_rc =
        ex.bind_detect(
            agent,
            dev_ip,
            "secure",
            detect
        );

    if (detect_rc != 0)
        throw std::runtime_error(
            "bind_detect failed: " +
            std::to_string(detect_rc)
        );

    std::cout
        << "[bridge] detected "
        << detect.dev_name
        << " model=" << detect.model_id
        << " firmware=" << detect.version
        << std::endl;

    const int connect_rc =
        ex.connect_printer(
            agent,
            dev_id,
            dev_ip,
            "bblp",
            access_code,
            true
        );

    if (connect_rc != 0)
        throw std::runtime_error(
            "connect_printer failed: " +
            std::to_string(connect_rc)
        );

    if (!connected.wait_for(15s))
        throw std::runtime_error(
            "Timed out waiting for LAN MQTT connection"
        );

    ex.start_subscribe(agent, "app");

    /*
     * Same three startup commands Studio sends.
     */
    const std::string pushall =
        R"({"pushing":{"sequence_id":"0","command":"pushall","version":1,"push_target":1}})";

    const std::string get_version =
        R"({"info":{"sequence_id":"1","command":"get_version"}})";

    const std::string get_access_code =
        R"({"system":{"sequence_id":"2","command":"get_access_code"}})";

    if (ex.send_message_to_printer(
            agent, dev_id, pushall, 1, 0) != 0)
        throw std::runtime_error("pushall failed");

    ex.send_message_to_printer(
        agent, dev_id, get_version, 1, 0);

    ex.send_message_to_printer(
        agent, dev_id, get_access_code, 1, 0);

    /*
     * Cloud-bound secured printer:
     *
     * The runner experiments showed that these two calls need to happen
     * in this order. The second one installs the app trust material into
     * the printer and causes "device_cert_installed".
     */
    std::cout << "[state] PROVISIONING" << std::endl;

    if (!ex.install_device_cert)
        throw std::runtime_error(
            "Plugin does not export install_device_cert"
        );

    cert_installed.reset();

    ex.install_device_cert(
        agent,
        dev_id,
        true
    );

    std::this_thread::sleep_for(5s);

    ex.install_device_cert(
        agent,
        dev_id,
        false
    );

    if (!cert_installed.wait_for(10s))
        throw std::runtime_error(
            "Timed out waiting for device_cert_installed"
        );

    std::cout << "[state] READY" << std::endl;
    std::cout
        << "[bridge] Persistent MQTT session is running. "
        << "Press Ctrl+C to stop."
        << std::endl;

    /*
     * v0.1 intentionally does nothing else.
     * Next version will accept AMS commands here.
     */
    while (!g_stop)
        std::this_thread::sleep_for(250ms);

    std::cout << "[bridge] stopping" << std::endl;

    /*
     * Stock plugin shutdown can take a long time while its worker threads
     * drain. For this dedicated process there is no useful state that needs
     * flushing; process exit cleans everything up.
     */
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(0);
}
catch (const std::exception& e) {
    std::cerr
        << "[fatal] "
        << e.what()
        << std::endl;

    return 1;
}
