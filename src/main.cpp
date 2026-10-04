#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cctype>
#include <cerrno>
#include <cstring>
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

std::string lower(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(
            std::tolower(
                static_cast<unsigned char>(c)));

    return value;
}

std::string trim(const std::string& value)
{
    const auto first =
        value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
        return "";

    const auto last =
        value.find_last_not_of(" \t\r\n");

    return value.substr(
        first,
        last - first + 1);
}

std::string uppercase(std::string value)
{
    for (char& c : value)
        c = static_cast<char>(
            std::toupper(
                static_cast<unsigned char>(c)));

    return value;
}

bool valid_color(const std::string& color)
{
    if (color.size() != 8)
        return false;

    for (char c : color) {
        if (!std::isxdigit(
                static_cast<unsigned char>(c)))
            return false;
    }

    return true;
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
    std::string reply_result;

    bool verify_push = false;
    unsigned long long verify_after_counter = 0;

    bool push_received = false;
    bool push_matches = false;

    std::string observed_profile;
    std::string observed_type;
    std::string observed_color;
    std::string observed_temp_min;
    std::string observed_temp_max;
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
        request_.reply_result.clear();

        request_.verify_push = false;
        request_.push_received = false;
        request_.push_matches = false;

        push_counter_ = 0;
    }

    void cancel()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_.active = false;
        }

        cv_.notify_all();
    }

    void prepare_push_verification()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        request_.verify_push = true;
        request_.verify_after_counter =
            push_counter_;

        request_.push_received = false;
        request_.push_matches = false;
    }

    void handle_message(const std::string& msg)
    {
        json root =
            json::parse(
                msg,
                nullptr,
                false);

        if (root.is_discarded() ||
            !root.contains("print") ||
            !root["print"].is_object())
            return;

        std::lock_guard<std::mutex> lock(mutex_);

        if (!request_.active)
            return;

        const json& print =
            root["print"];

        handle_reply(print);
        handle_push_status(print);

        cv_.notify_all();
    }

    bool wait_for_reply(
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
                    request_.reply_received;
            });

        result = request_;

        return
            result.reply_received &&
            result.reply_success;
    }

    bool wait_for_verified_push(
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
                    request_.push_matches;
            });

        result = request_;

        request_.active = false;

        return result.push_matches;
    }

private:
    void handle_reply(const json& print)
    {
        if (print.value(
                "command",
                "") !=
            "ams_filament_setting")
            return;

        if (print.value(
                "sequence_id",
                "") !=
            request_.sequence_id)
            return;

        request_.reply_received = true;

        request_.reply_result =
            print.value(
                "result",
                "");

        request_.reply_success =
            request_.reply_result ==
            "success";

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

            if (ams.value(
                    "id",
                    "") !=
                std::to_string(
                    request_.ams_id))
                continue;

            if (!ams.contains("tray") ||
                !ams["tray"].is_array())
                continue;

            for (const auto& tray : ams["tray"]) {
                if (!tray.is_object())
                    continue;

                if (tray.value(
                        "id",
                        "") !=
                    std::to_string(
                        request_.tray_id))
                    continue;

                ++push_counter_;

                request_.observed_profile =
                    tray.value(
                        "tray_info_idx",
                        "");

                request_.observed_type =
                    tray.value(
                        "tray_type",
                        "");

                request_.observed_color =
                    tray.value(
                        "tray_color",
                        "");

                request_.observed_temp_min =
                    tray.value(
                        "nozzle_temp_min",
                        "");

                request_.observed_temp_max =
                    tray.value(
                        "nozzle_temp_max",
                        "");

                if (!request_.verify_push)
                    return;

                if (push_counter_ <=
                    request_.verify_after_counter)
                    return;

                request_.push_received = true;

                request_.push_matches =
                    request_.observed_profile ==
                        request_.tray_info_idx &&
                    request_.observed_type ==
                        request_.tray_type &&
                    request_.observed_color ==
                        request_.tray_color &&
                    request_.observed_temp_min ==
                        std::to_string(
                            request_.nozzle_temp_min) &&
                    request_.observed_temp_max ==
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

    unsigned long long push_counter_ = 0;
};

void ensure_config(
    const std::string& data_dir)
{
    std::filesystem::create_directories(
        data_dir);

    const std::string filename =
        data_dir +
        "/BambuStudio.conf";

    if (std::filesystem::exists(filename))
        return;

    std::ofstream out(filename);

    if (!out)
        throw std::runtime_error(
            "Cannot create " +
            filename);

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

/* ---------------------------------------------------------
 * Minimal HTTP server
 * --------------------------------------------------------- */

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string body;
};

bool send_all(
    int fd,
    const std::string& data)
{
    size_t sent = 0;

    while (sent < data.size()) {
        const ssize_t rc =
            ::send(
                fd,
                data.data() + sent,
                data.size() - sent,
                MSG_NOSIGNAL);

        if (rc <= 0)
            return false;

        sent += static_cast<size_t>(rc);
    }

    return true;
}

bool read_http_request(
    int fd,
    HttpRequest& request,
    std::string& error)
{
    constexpr size_t MAX_REQUEST =
        64 * 1024;

    std::string data;

    char buffer[4096];

    size_t header_end =
        std::string::npos;

    while (
        (header_end =
            data.find("\r\n\r\n")) ==
        std::string::npos)
    {
        const ssize_t rc =
            ::recv(
                fd,
                buffer,
                sizeof(buffer),
                0);

        if (rc <= 0) {
            error =
                "connection closed while reading headers";
            return false;
        }

        data.append(
            buffer,
            static_cast<size_t>(rc));

        if (data.size() >
            MAX_REQUEST)
        {
            error =
                "request too large";
            return false;
        }
    }

    const std::string header_text =
        data.substr(
            0,
            header_end);

    std::istringstream input(
        header_text);

    std::string request_line;

    if (!std::getline(
            input,
            request_line))
    {
        error =
            "missing request line";
        return false;
    }

    if (!request_line.empty() &&
        request_line.back() == '\r')
        request_line.pop_back();

    {
        std::istringstream line(
            request_line);

        std::string version;

        line
            >> request.method
            >> request.path
            >> version;

        if (!line ||
            version.rfind(
                "HTTP/",
                0) != 0)
        {
            error =
                "invalid request line";
            return false;
        }
    }

    std::string line;

    while (std::getline(
        input,
        line))
    {
        if (!line.empty() &&
            line.back() == '\r')
            line.pop_back();

        const auto colon =
            line.find(':');

        if (colon ==
            std::string::npos)
            continue;

        std::string key =
            lower(
                trim(
                    line.substr(
                        0,
                        colon)));

        std::string value =
            trim(
                line.substr(
                    colon + 1));

        request.headers[key] =
            value;
    }

    size_t content_length = 0;

    const auto it =
        request.headers.find(
            "content-length");

    if (it !=
        request.headers.end())
    {
        try {
            content_length =
                static_cast<size_t>(
                    std::stoul(
                        it->second));
        }
        catch (...) {
            error =
                "invalid Content-Length";
            return false;
        }
    }

    if (content_length >
        MAX_REQUEST)
    {
        error =
            "request body too large";
        return false;
    }

    const size_t body_start =
        header_end + 4;

    if (data.size() >
        body_start)
    {
        request.body =
            data.substr(
                body_start);
    }

    while (
        request.body.size() <
        content_length)
    {
        const ssize_t rc =
            ::recv(
                fd,
                buffer,
                sizeof(buffer),
                0);

        if (rc <= 0) {
            error =
                "connection closed while reading body";
            return false;
        }

        request.body.append(
            buffer,
            static_cast<size_t>(rc));

        if (request.body.size() >
            MAX_REQUEST)
        {
            error =
                "request body too large";
            return false;
        }
    }

    if (request.body.size() >
        content_length)
    {
        request.body.resize(
            content_length);
    }

    return true;
}

void send_http_response(
    int fd,
    const HttpResponse& response)
{
    std::ostringstream out;

    out
        << "HTTP/1.1 "
        << response.status
        << " "
        << response.reason
        << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: "
        << response.body.size()
        << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << response.body;

    send_all(
        fd,
        out.str());
}

HttpResponse json_response(
    int status,
    const std::string& reason,
    const json& body)
{
    return {
        status,
        reason,
        body.dump()
    };
}

} // namespace

int main()
try {
    std::signal(
        SIGINT,
        on_signal);

    std::signal(
        SIGTERM,
        on_signal);

    const std::string dev_id =
        env_required(
            "BAMBU_DEV_ID");

    const std::string dev_ip =
        env_required(
            "BAMBU_DEV_IP");

    const std::string access_code =
        env_required(
            "BAMBU_ACCESS_CODE");

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

    const std::string http_bind =
        env_or(
            "BAMBU_HTTP_BIND",
            "0.0.0.0");

    const int http_port =
        std::stoi(
            env_or(
                "BAMBU_HTTP_PORT",
                "8080"));

    ensure_config(
        data_dir);

    std::cout
        << "[bridge] loading plugin: "
        << plugin_path
        << std::endl;

    pr::PluginExports ex =
        pr::load(
            plugin_path);

    const std::string plugin_version =
        ex.version;

    std::cout
        << "[bridge] plugin version: "
        << plugin_version
        << std::endl;

    void* agent =
        ex.create_agent(
            data_dir);

    if (!agent)
        throw std::runtime_error(
            "create_agent returned nullptr");

    SignalLatch connected;
    SignalLatch cert_installed;

    FilamentTracker tracker;

    std::atomic<bool>
        mqtt_connected{false};

    std::atomic<bool>
        ready{false};

    std::mutex command_mutex;

    std::atomic<unsigned long long>
        sequence{20000};

    ex.set_on_local_message_fn(
        agent,
        [&tracker](
            std::string,
            std::string msg)
        {
            tracker.handle_message(
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
            &mqtt_connected,
            &ready
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
            else {
                mqtt_connected = false;
                ready = false;

                std::cout
                    << "[state] DISCONNECTED: "
                    << dev_id_cb
                    << " status="
                    << status
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
                << "[http] plugin error "
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
            return std::string(
                "CZ");
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

    ex.set_config_dir(
        agent,
        data_dir);

    ex.init_log(
        agent);

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

    if (ex.start(agent) != 0)
        throw std::runtime_error(
            "bambu_network_start failed");

    ex.enable_multi_machine(
        agent,
        false);

    ex.start_discovery(
        agent,
        true,
        false);

    if (ex.change_user(
            agent,
            "") != 0)
    {
        throw std::runtime_error(
            "change_user failed");
    }

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
            std::to_string(
                detect_rc));

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
            std::to_string(
                connect_rc));

    if (!connected.wait_for(
            15s))
    {
        throw std::runtime_error(
            "Timed out waiting for "
            "LAN MQTT connection");
    }

    ex.start_subscribe(
        agent,
        "app");

    const std::string startup_pushall =
        R"({"pushing":{"sequence_id":"0","command":"pushall","version":1,"push_target":1}})";

    const std::string get_version =
        R"({"info":{"sequence_id":"1","command":"get_version"}})";

    const std::string get_access_code =
        R"({"system":{"sequence_id":"2","command":"get_access_code"}})";

    if (
        ex.send_message_to_printer(
            agent,
            dev_id,
            startup_pushall,
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

    std::this_thread::sleep_for(
        5s);

    ex.install_device_cert(
        agent,
        dev_id,
        false);

    if (!cert_installed.wait_for(
            10s))
    {
        throw std::runtime_error(
            "Timed out waiting for "
            "device_cert_installed");
    }

    ready = true;

    std::cout
        << "[state] READY"
        << std::endl;

    /* -----------------------------------------------------
     * HTTP listener
     * ----------------------------------------------------- */

    const int server_fd =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (server_fd < 0)
        throw std::runtime_error(
            "socket() failed");

    int reuse = 1;

    ::setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse));

    sockaddr_in address{};
    address.sin_family =
        AF_INET;

    address.sin_port =
        htons(
            static_cast<uint16_t>(
                http_port));

    if (
        ::inet_pton(
            AF_INET,
            http_bind.c_str(),
            &address.sin_addr) != 1)
    {
        throw std::runtime_error(
            "Invalid BAMBU_HTTP_BIND");
    }

    if (
        ::bind(
            server_fd,
            reinterpret_cast<
                sockaddr*>(
                    &address),
            sizeof(address)) < 0)
    {
        throw std::runtime_error(
            std::string(
                "bind() failed: ") +
            std::strerror(errno));
    }

    if (
        ::listen(
            server_fd,
            16) < 0)
    {
        throw std::runtime_error(
            "listen() failed");
    }

    std::cout
        << "[http] listening on "
        << http_bind
        << ":"
        << http_port
        << std::endl;

    while (!g_stop) {
        pollfd pfd{};
        pfd.fd =
            server_fd;
        pfd.events =
            POLLIN;

        const int poll_rc =
            ::poll(
                &pfd,
                1,
                500);

        if (poll_rc < 0) {
            if (errno ==
                EINTR)
                continue;

            throw std::runtime_error(
                "poll() failed");
        }

        if (poll_rc == 0)
            continue;

        const int client_fd =
            ::accept(
                server_fd,
                nullptr,
                nullptr);

        if (client_fd < 0)
            continue;

        HttpRequest request;
        std::string parse_error;

        if (!read_http_request(
                client_fd,
                request,
                parse_error))
        {
            send_http_response(
                client_fd,
                json_response(
                    400,
                    "Bad Request",
                    {
                        {
                            "error",
                            parse_error
                        }
                    }));

            ::close(
                client_fd);

            continue;
        }

        HttpResponse response;

        if (
            request.method ==
                "GET" &&
            request.path ==
                "/health")
        {
            response =
                json_response(
                    ready ? 200 : 503,
                    ready
                        ? "OK"
                        : "Service Unavailable",
                    {
                        {
                            "status",
                            ready
                                ? "ready"
                                : "not_ready"
                        },
                        {
                            "connected",
                            mqtt_connected.load()
                        },
                        {
                            "ready",
                            ready.load()
                        },
                        {
                            "printerId",
                            dev_id
                        },
                        {
                            "printerIp",
                            dev_ip
                        },
                        {
                            "pluginVersion",
                            plugin_version
                        },
                        {
                            "firmware",
                            detect.version
                        }
                    });
        }
        else if (
            request.method ==
            "POST")
        {
            int ams_id = -1;
            int tray_id = -1;
            int consumed = 0;

            const int matches =
                std::sscanf(
                    request.path.c_str(),
                    "/api/v1/ams/%d/trays/%d/filament%n",
                    &ams_id,
                    &tray_id,
                    &consumed);

            if (
                matches != 2 ||
                consumed !=
                    static_cast<int>(
                        request.path.size()))
            {
                response =
                    json_response(
                        404,
                        "Not Found",
                        {
                            {
                                "error",
                                "not_found"
                            }
                        });
            }
            else if (!ready) {
                response =
                    json_response(
                        503,
                        "Service Unavailable",
                        {
                            {
                                "status",
                                "failed"
                            },
                            {
                                "error",
                                "printer_not_ready"
                            }
                        });
            }
            else {
                std::lock_guard<std::mutex>
                    command_lock(
                        command_mutex);

                json input =
                    json::parse(
                        request.body,
                        nullptr,
                        false);

                if (!input.is_object()) {
                    response =
                        json_response(
                            400,
                            "Bad Request",
                            {
                                {
                                    "error",
                                    "invalid_json"
                                }
                            });
                }
                else {
                    try {
                        FilamentRequest cmd;

                        cmd.ams_id =
                            ams_id;

                        cmd.tray_id =
                            tray_id;

                        cmd.tray_info_idx =
                            input.at(
                                "profile")
                            .get<std::string>();

                        cmd.setting_id =
                            input.at(
                                "setting")
                            .get<std::string>();

                        cmd.tray_type =
                            input.at(
                                "type")
                            .get<std::string>();

                        cmd.tray_color =
                            uppercase(
                                input.at(
                                    "color")
                                .get<std::string>());

                        cmd.nozzle_temp_min =
                            input.at(
                                "tempMin")
                            .get<int>();

                        cmd.nozzle_temp_max =
                            input.at(
                                "tempMax")
                            .get<int>();

                        if (
                            ams_id < 0 ||
                            tray_id < 0)
                        {
                            throw std::runtime_error(
                                "invalid AMS/tray");
                        }

                        if (
                            cmd.tray_info_idx.empty() ||
                            cmd.setting_id.empty() ||
                            cmd.tray_type.empty())
                        {
                            throw std::runtime_error(
                                "profile, setting and type "
                                "must not be empty");
                        }

                        if (!valid_color(
                                cmd.tray_color))
                        {
                            throw std::runtime_error(
                                "color must be RRGGBBAA");
                        }

                        if (
                            cmd.nozzle_temp_min < 0 ||
                            cmd.nozzle_temp_max > 400 ||
                            cmd.nozzle_temp_min >
                                cmd.nozzle_temp_max)
                        {
                            throw std::runtime_error(
                                "invalid temperature range");
                        }

                        cmd.sequence_id =
                            std::to_string(
                                ++sequence);

                        json payload;

                        payload["print"] = {
                            {
                                "sequence_id",
                                cmd.sequence_id
                            },
                            {
                                "command",
                                "ams_filament_setting"
                            },
                            {
                                "ams_id",
                                cmd.ams_id
                            },
                            {
                                "tray_id",
                                cmd.tray_id
                            },
                            {
                                "tray_info_idx",
                                cmd.tray_info_idx
                            },
                            {
                                "setting_id",
                                cmd.setting_id
                            },
                            {
                                "tray_color",
                                cmd.tray_color
                            },
                            {
                                "nozzle_temp_min",
                                cmd.nozzle_temp_min
                            },
                            {
                                "nozzle_temp_max",
                                cmd.nozzle_temp_max
                            },
                            {
                                "tray_type",
                                cmd.tray_type
                            }
                        };

                        tracker.begin(
                            cmd);

                        const auto started =
                            std::chrono::
                                steady_clock::
                                now();

                        const int send_rc =
                            ex.send_message_to_printer(
                                agent,
                                dev_id,
                                payload.dump(),
                                1,
                                0);

                        if (send_rc != 0) {
                            tracker.cancel();

                            response =
                                json_response(
                                    502,
                                    "Bad Gateway",
                                    {
                                        {
                                            "status",
                                            "failed"
                                        },
                                        {
                                            "error",
                                            "send_failed"
                                        },
                                        {
                                            "sendRc",
                                            send_rc
                                        }
                                    });
                        }
                        else {
                            FilamentRequest result;

                            const bool reply_ok =
                                tracker.wait_for_reply(
                                    4s,
                                    result);

                            if (!result.reply_received) {
                                tracker.cancel();

                                response =
                                    json_response(
                                        504,
                                        "Gateway Timeout",
                                        {
                                            {
                                                "status",
                                                "failed"
                                            },
                                            {
                                                "error",
                                                "printer_reply_timeout"
                                            },
                                            {
                                                "sequenceId",
                                                cmd.sequence_id
                                            }
                                        });
                            }
                            else if (!reply_ok) {
                                tracker.cancel();

                                response =
                                    json_response(
                                        502,
                                        "Bad Gateway",
                                        {
                                            {
                                                "status",
                                                "failed"
                                            },
                                            {
                                                "error",
                                                "printer_rejected"
                                            },
                                            {
                                                "printerResult",
                                                result.reply_result
                                            }
                                        });
                            }
                            else {
                                /*
                                 * Strong verification:
                                 * ignore all previous push_status messages,
                                 * then explicitly request a new one.
                                 */
                                tracker.prepare_push_verification();

                                const std::string verify_sequence =
                                    std::to_string(
                                        ++sequence);

                                json verify_pushall = {
                                    {
                                        "pushing",
                                        {
                                            {
                                                "sequence_id",
                                                verify_sequence
                                            },
                                            {
                                                "command",
                                                "pushall"
                                            },
                                            {
                                                "version",
                                                1
                                            },
                                            {
                                                "push_target",
                                                1
                                            }
                                        }
                                    }
                                };

                                const int verify_rc =
                                    ex.send_message_to_printer(
                                        agent,
                                        dev_id,
                                        verify_pushall.dump(),
                                        1,
                                        0);

                                if (verify_rc != 0) {
                                    tracker.cancel();

                                    response =
                                        json_response(
                                            502,
                                            "Bad Gateway",
                                            {
                                                {
                                                    "status",
                                                    "failed"
                                                },
                                                {
                                                    "error",
                                                    "verification_request_failed"
                                                },
                                                {
                                                    "sendRc",
                                                    verify_rc
                                                }
                                            });
                                }
                                else {
                                    const bool verified =
                                        tracker.wait_for_verified_push(
                                            4s,
                                            result);

                                    const auto elapsed =
                                        std::chrono::
                                            duration_cast<
                                                std::chrono::
                                                    milliseconds>(
                                                std::chrono::
                                                    steady_clock::
                                                    now() -
                                                started)
                                        .count();

                                    if (verified) {
                                        response =
                                            json_response(
                                                200,
                                                "OK",
                                                {
                                                    {
                                                        "status",
                                                        "synced"
                                                    },
                                                    {
                                                        "verified",
                                                        true
                                                    },
                                                    {
                                                        "elapsedMs",
                                                        elapsed
                                                    },
                                                    {
                                                        "sequenceId",
                                                        cmd.sequence_id
                                                    },
                                                    {
                                                        "amsId",
                                                        cmd.ams_id
                                                    },
                                                    {
                                                        "trayId",
                                                        cmd.tray_id
                                                    }
                                                });
                                    }
                                    else {
                                        response =
                                            json_response(
                                                504,
                                                "Gateway Timeout",
                                                {
                                                    {
                                                        "status",
                                                        "failed"
                                                    },
                                                    {
                                                        "error",
                                                        "verification_timeout"
                                                    },
                                                    {
                                                        "verified",
                                                        false
                                                    },
                                                    {
                                                        "elapsedMs",
                                                        elapsed
                                                    },
                                                    {
                                                        "observed",
                                                        {
                                                            {
                                                                "profile",
                                                                result.observed_profile
                                                            },
                                                            {
                                                                "type",
                                                                result.observed_type
                                                            },
                                                            {
                                                                "color",
                                                                result.observed_color
                                                            },
                                                            {
                                                                "tempMin",
                                                                result.observed_temp_min
                                                            },
                                                            {
                                                                "tempMax",
                                                                result.observed_temp_max
                                                            }
                                                        }
                                                    }
                                                });
                                    }
                                }
                            }
                        }
                    }
                    catch (
                        const std::exception& e)
                    {
                        tracker.cancel();

                        response =
                            json_response(
                                400,
                                "Bad Request",
                                {
                                    {
                                        "error",
                                        "invalid_request"
                                    },
                                    {
                                        "message",
                                        e.what()
                                    }
                                });
                    }
                }
            }
        }
        else {
            response =
                json_response(
                    404,
                    "Not Found",
                    {
                        {
                            "error",
                            "not_found"
                        }
                    });
        }

        send_http_response(
            client_fd,
            response);

        ::close(
            client_fd);
    }

    ::close(
        server_fd);

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
