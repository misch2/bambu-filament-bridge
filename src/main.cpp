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
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "plugin_loader.hpp"

namespace pr = obn::plugin_runner;
using json = nlohmann::json;
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
        throw std::runtime_error(
            std::string("Missing environment variable: ") + name);

    return value;
}

std::string env_or(
    const char* name,
    const std::string& fallback)
{
    const char* value = std::getenv(name);

    return (value && *value)
        ? value
        : fallback;
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

        return cv_.wait_for(
            lock,
            timeout,
            [&] {
                return signalled_;
            });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool signalled_ = false;
};

struct FilamentRequest {
    bool active = false;

    std::string sequence_id;

    int ams_id = 0;
    int tray_id = 0;

    std::string tray_info_idx;
    std::string setting_id;
    std::string tray_type;
    std::string tray_color;

    int nozzle_temp_min = 0;
    int nozzle_temp_max = 0;

    bool reply_received = false;
    bool reply_success = false;

    bool push_received = false;
    bool push_matches = false;

    std::string reply_result;
};

class FilamentTracker {
public:
    void begin(const FilamentRequest& request)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        request_ = request;
        request_.active = true;

        request_.reply_received = false;
        request_.reply_success = false;
        request_.push_received = false;
        request_.push_matches = false;
        request_.reply_result.clear();
    }

    void cancel()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request_.active = false;
        cv_.notify_all();
    }

    void handle_message(const std::string& msg)
    {
        json root = json::parse(
            msg,
            nullptr,
            false);

        if (root.is_discarded() ||
            !root.contains("print") ||
            !root["print"].is_object())
            return;

        std::unique_lock<std::mutex> lock(mutex_);

        if (!request_.active)
            return;

        const json& print = root["print"];

        handle_reply(print);
        handle_push_status(print);

        if (
            request_.reply_received &&
            request_.push_received)
        {
            cv_.notify_all();
        }
    }

    bool wait_for_complete(
        std::chrono::milliseconds timeout,
        FilamentRequest& result)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait_for(
            lock,
            timeout,
            [&] {
                return
                    !request_.active ||
                    (
                        request_.reply_received &&
                        request_.push_received
                    );
            });

        result = request_;

        request_.active = false;

        return
            result.reply_received &&
            result.reply_success &&
            result.push_received &&
            result.push_matches;
    }

private:
    void handle_reply(const json& print)
    {
        if (print.value("command", "") !=
            "ams_filament_setting")
            return;

        if (
            print.value("sequence_id", "") !=
            request_.sequence_id)
            return;

        request_.reply_received = true;

        request_.reply_result =
            print.value("result", "");

        request_.reply_success =
            request_.reply_result == "success";

        std::cout
            << "[command] printer reply: "
            << request_.reply_result
            << std::endl;
    }

    void handle_push_status(const json& print)
    {
        if (!print.contains("ams") ||
            !print["ams"].is_object())
            return;

        const json& ams_root =
            print["ams"];

        if (!ams_root.contains("ams") ||
            !ams_root["ams"].is_array())
            return;

        for (const auto& ams : ams_root["ams"]) {
            if (!ams.is_object())
                continue;

            if (
                ams.value("id", "") !=
                std::to_string(request_.ams_id))
                continue;

            if (!ams.contains("tray") ||
                !ams["tray"].is_array())
                continue;

            for (const auto& tray : ams["tray"]) {
                if (!tray.is_object())
                    continue;

                if (
                    tray.value("id", "") !=
                    std::to_string(request_.tray_id))
                    continue;

                const std::string info_idx =
                    tray.value(
                        "tray_info_idx",
                        "");

                const std::string type =
                    tray.value(
                        "tray_type",
                        "");

                const std::string color =
                    tray.value(
                        "tray_color",
                        "");

                const std::string temp_min =
                    tray.value(
                        "nozzle_temp_min",
                        "");

                const std::string temp_max =
                    tray.value(
                        "nozzle_temp_max",
                        "");

                request_.push_received = true;

                request_.push_matches =
                    info_idx ==
                        request_.tray_info_idx &&
                    type ==
                        request_.tray_type &&
                    color ==
                        request_.tray_color &&
                    temp_min ==
                        std::to_string(
                            request_.nozzle_temp_min) &&
                    temp_max ==
                        std::to_string(
                            request_.nozzle_temp_max);

                if (request_.push_matches) {
                    std::cout
                        << "[command] push_status verified"
                        << std::endl;
                }

                return;
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    FilamentRequest request_;
};

void ensure_config(
    const std::string& data_dir)
{
    std::filesystem::create_directories(
        data_dir);

    const std::string filename =
        data_dir + "/BambuStudio.conf";

    if (std::filesystem::exists(filename))
        return;

    std::ofstream out(filename);

    if (!out)
        throw std::runtime_error(
            "Cannot create " + filename);

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
            "/home/runner/.cache/"
            "obn-plugin-runner/"
            "02.08.02.54/"
            "libbambu_networking.so");

    const std::string cert_dir =
        env_or(
            "BAMBU_CERT_DIR",
            "/home/runner/bambu-certs");

    const std::string data_dir =
        env_or(
            "BAMBU_DATA_DIR",
            "/home/runner/.local/share/"
            "bambu-bridge");

    ensure_config(data_dir);

    std::cout
        << "[bridge] loading plugin: "
        << plugin_path
        << std::endl;

    pr::PluginExports ex =
        pr::load(plugin_path);

    std::cout
        << "[bridge] plugin version: "
        << ex.version
        << std::endl;

    void* agent =
        ex.create_agent(data_dir);

    if (!agent)
        throw std::runtime_error(
            "create_agent returned nullptr");

    SignalLatch connected;
    SignalLatch cert_installed;

    FilamentTracker filament_tracker;

    std::atomic<bool> mqtt_connected{
        false};

    /*
     * Keep local_message silent except for messages
     * relevant to a pending filament command.
     */
    ex.set_on_local_message_fn(
        agent,
        [&filament_tracker](
            std::string,
            std::string msg)
        {
            filament_tracker.handle_message(
                msg);
        });

    ex.set_on_message_fn(
        agent,
        [&cert_installed](
            std::string dev_id_cb,
            std::string msg)
        {
            if (
                msg ==
                "device_cert_installed")
            {
                std::cout
                    << "[security] "
                    << "device_cert_installed for "
                    << dev_id_cb
                    << std::endl;

                cert_installed.signal();
            }
        });

    ex.set_on_user_message_fn(
        agent,
        [](std::string, std::string) {});

    ex.set_on_local_connect_fn(
        agent,
        [
            &connected,
            &mqtt_connected
        ](
            int status,
            std::string dev_id_cb,
            std::string msg)
        {
            if (
                status ==
                BBL::ConnectStatusOk)
            {
                mqtt_connected = true;

                std::cout
                    << "[state] CONNECTED: "
                    << dev_id_cb
                    << std::endl;

                connected.signal();
            }
            else if (
                status ==
                BBL::ConnectStatusLost)
            {
                mqtt_connected = false;

                std::cout
                    << "[state] DISCONNECTED: "
                    << dev_id_cb
                    << " (" << msg << ")"
                    << std::endl;
            }
            else {
                mqtt_connected = false;

                std::cout
                    << "[state] connection status="
                    << status
                    << " dev="
                    << dev_id_cb
                    << " msg="
                    << msg
                    << std::endl;
            }
        });

    ex.set_on_printer_connected_fn(
        agent,
        [](std::string) {});

    ex.set_on_server_connected_fn(
        agent,
        [](int, int) {});

    ex.set_on_http_error_fn(
        agent,
        [](
            unsigned status,
            std::string body)
        {
            std::cerr
                << "[http] error "
                << status
                << ": "
                << body
                << std::endl;
        });

    ex.set_on_subscribe_failure_fn(
        agent,
        [](std::string topic)
        {
            std::cerr
                << "[mqtt] subscribe failure: "
                << topic
                << std::endl;
        });

    ex.set_get_country_code_fn(
        agent,
        [] {
            return std::string("CZ");
        });

    if (ex.set_queue_on_main_fn) {
        ex.set_queue_on_main_fn(
            agent,
            [](
                std::function<void()> fn)
            {
                if (fn)
                    fn();
            });
    }

    ex.set_server_callback(
        agent,
        [](std::string, int) {});

    ex.set_on_ssdp_msg_fn(
        agent,
        [](std::string) {});

    /*
     * Same init order as Bambu Studio.
     */
    ex.set_config_dir(
        agent,
        data_dir);

    ex.init_log(agent);

    ex.set_cert_file(
        agent,
        cert_dir,
        "slicer_base64.cer");

    std::map<
        std::string,
        std::string
    > headers{
        {
            "X-BBL-Client-Type",
            "slicer"
        },
        {
            "X-BBL-Client-Name",
            "BambuBridge"
        },
        {
            "X-BBL-Client-Version",
            "02.08.02.61"
        },
        {
            "X-BBL-OS-Type",
            "linux"
        },
        {
            "X-BBL-OS-Version",
            "1.0.0"
        },
        {
            "X-BBL-Language",
            "en"
        }
    };

    ex.set_extra_http_header(
        agent,
        headers);

    ex.set_country_code(
        agent,
        "CZ");

    const int start_rc =
        ex.start(agent);

    if (start_rc != 0)
        throw std::runtime_error(
            "bambu_network_start failed: " +
            std::to_string(start_rc));

    ex.enable_multi_machine(
        agent,
        false);

    ex.start_discovery(
        agent,
        true,
        false);

    const int user_rc =
        ex.change_user(agent, "");

    if (user_rc != 0)
        throw std::runtime_error(
            "change_user failed: " +
            std::to_string(user_rc));

    std::cout
        << "[state] CONNECTING"
        << std::endl;

    BBL::detectResult detect{};

    const int detect_rc =
        ex.bind_detect(
            agent,
            dev_ip,
            "secure",
            detect);

    if (detect_rc != 0)
        throw std::runtime_error(
            "bind_detect failed: " +
            std::to_string(detect_rc));

    std::cout
        << "[bridge] detected "
        << detect.dev_name
        << " model="
        << detect.model_id
        << " firmware="
        << detect.version
        << std::endl;

    const int connect_rc =
        ex.connect_printer(
            agent,
            dev_id,
            dev_ip,
            "bblp",
            access_code,
            true);

    if (connect_rc != 0)
        throw std::runtime_error(
            "connect_printer failed: " +
            std::to_string(connect_rc));

    if (!connected.wait_for(15s))
        throw std::runtime_error(
            "Timed out waiting for "
            "LAN MQTT connection");

    ex.start_subscribe(
        agent,
        "app");

    const std::string pushall =
        R"({"pushing":{"sequence_id":"0","command":"pushall","version":1,"push_target":1}})";

    const std::string get_version =
        R"({"info":{"sequence_id":"1","command":"get_version"}})";

    const std::string get_access_code =
        R"({"system":{"sequence_id":"2","command":"get_access_code"}})";

    if (
        ex.send_message_to_printer(
            agent,
            dev_id,
            pushall,
            1,
            0) != 0)
    {
        throw std::runtime_error(
            "pushall failed");
    }

    ex.send_message_to_printer(
        agent,
        dev_id,
        get_version,
        1,
        0);

    ex.send_message_to_printer(
        agent,
        dev_id,
        get_access_code,
        1,
        0);

    std::cout
        << "[state] PROVISIONING"
        << std::endl;

    if (!ex.install_device_cert)
        throw std::runtime_error(
            "Plugin does not export "
            "install_device_cert");

    cert_installed.reset();

    ex.install_device_cert(
        agent,
        dev_id,
        true);

    std::this_thread::sleep_for(5s);

    ex.install_device_cert(
        agent,
        dev_id,
        false);

    if (!cert_installed.wait_for(10s))
        throw std::runtime_error(
            "Timed out waiting for "
            "device_cert_installed");

    std::cout
        << "[state] READY"
        << std::endl;

    std::cout <<
R"(
Commands:

  set <ams> <tray> <profile> <setting> <type> <RRGGBBAA> <min> <max>

Example:

  set 0 3 GFG99 GFSG99_15 PETG 1050C0FF 210 250

Other commands:

  help
  quit

)";

    std::atomic<unsigned long long>
        sequence{20000};

    std::string line;

    while (!g_stop &&
           std::getline(std::cin, line))
    {
        if (line.empty())
            continue;

        if (
            line == "quit" ||
            line == "exit")
        {
            break;
        }

        if (line == "help") {
            std::cout <<
R"(set <ams> <tray> <profile> <setting> <type> <RRGGBBAA> <min> <max>
)";
            continue;
        }

        std::istringstream input(line);

        std::string command;
        input >> command;

        if (command != "set") {
            std::cout
                << "[error] unknown command"
                << std::endl;
            continue;
        }

        FilamentRequest request;

        input
            >> request.ams_id
            >> request.tray_id
            >> request.tray_info_idx
            >> request.setting_id
            >> request.tray_type
            >> request.tray_color
            >> request.nozzle_temp_min
            >> request.nozzle_temp_max;

        if (!input) {
            std::cout
                << "[error] invalid set command"
                << std::endl;
            continue;
        }

        if (!mqtt_connected) {
            std::cout
                << "[error] printer is not connected"
                << std::endl;
            continue;
        }

        request.sequence_id =
            std::to_string(++sequence);

        json payload;

        payload["print"] = {
            {
                "sequence_id",
                request.sequence_id
            },
            {
                "command",
                "ams_filament_setting"
            },
            {
                "ams_id",
                request.ams_id
            },
            {
                "tray_id",
                request.tray_id
            },
            {
                "tray_info_idx",
                request.tray_info_idx
            },
            {
                "setting_id",
                request.setting_id
            },
            {
                "tray_color",
                request.tray_color
            },
            {
                "nozzle_temp_min",
                request.nozzle_temp_min
            },
            {
                "nozzle_temp_max",
                request.nozzle_temp_max
            },
            {
                "tray_type",
                request.tray_type
            }
        };

        filament_tracker.begin(request);

        const auto started =
            std::chrono::steady_clock::now();

        const int send_rc =
            ex.send_message_to_printer(
                agent,
                dev_id,
                payload.dump(),
                1,
                0);

        if (send_rc != 0) {
            filament_tracker.cancel();

            std::cout
                << "[command] send failed rc="
                << send_rc
                << std::endl;

            continue;
        }

        std::cout
            << "[command] sent sequence="
            << request.sequence_id
            << std::endl;

        FilamentRequest result;

        const bool verified =
            filament_tracker.wait_for_complete(
                5s,
                result);

        const auto elapsed =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                std::chrono::steady_clock::now()
                - started)
            .count();

        if (verified) {
            std::cout
                << "[command] VERIFIED in "
                << elapsed
                << " ms"
                << std::endl;
        }
        else {
            std::cout
                << "[command] FAILED/TIMEOUT in "
                << elapsed
                << " ms"
                << std::endl;

            std::cout
                << "          reply_received="
                << result.reply_received
                << " reply_success="
                << result.reply_success
                << " push_received="
                << result.push_received
                << " push_matches="
                << result.push_matches
                << std::endl;
        }
    }

    std::cout
        << "[bridge] stopping"
        << std::endl;

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
