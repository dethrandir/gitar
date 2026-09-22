#include "gitar/control_server.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "gitar/devices.hpp"
#include "gitar/engine.hpp"
#include "nlohmann/json.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gitar {
namespace {

using json = nlohmann::json;

#ifdef _WIN32
using socket_t = SOCKET;
using socklen_type = int;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;

std::once_flag g_winsock_once;

bool ensure_winsock() {
    bool ready = false;
    std::call_once(g_winsock_once, [&ready] {
        WSADATA data;
        ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return ready;
}
#else
using socket_t = int;
using socklen_type = socklen_t;
constexpr socket_t kInvalidSocket = -1;

bool ensure_winsock() {
    return true;
}
#endif

void close_socket(socket_t socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

void shutdown_socket(socket_t socket) {
#ifdef _WIN32
    ::shutdown(socket, SD_BOTH);
#else
    ::shutdown(socket, SHUT_RDWR);
#endif
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool send_all(socket_t socket, const std::string& data) {
#ifdef MSG_NOSIGNAL
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    std::size_t sent = 0;
    while (sent < data.size()) {
        const std::size_t remaining = data.size() - sent;
        const int chunk = static_cast<int>(std::min<std::size_t>(remaining, 1u << 20));
        const auto written = ::send(socket, data.data() + sent, chunk, flags);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

json success(const json& id, const json& result) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

json failure(const json& id, int code, const std::string& message) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

json engine_status(Engine& engine) {
    return json{
        {"running", engine.running()},
        {"gain", engine.gain()},
        {"input_peak_db", engine.input_peak_db()},
        {"output_peak_db", engine.output_peak_db()},
        {"latency_ms", engine.latency_ms()},
        {"sample_rate", engine.sample_rate()},
        {"period_frames", engine.period_frames()},
        {"overrun_frames", engine.overrun_frames()},
        {"underrun_frames", engine.underrun_frames()},
    };
}

bool apply_start_params(const json& params, EngineConfig* config, std::string* error) {
    if (params.contains("input_device")) {
        if (!params["input_device"].is_string()) {
            set_error(error, "input_device must be a string");
            return false;
        }
        config->input_device = params["input_device"].get<std::string>();
    }
    if (params.contains("output_device")) {
        if (!params["output_device"].is_string()) {
            set_error(error, "output_device must be a string");
            return false;
        }
        config->output_device = params["output_device"].get<std::string>();
    }

    const char* const integer_fields[] = {"sample_rate", "period_frames", "channels"};
    std::uint32_t* const integer_targets[] = {&config->sample_rate, &config->period_frames,
                                              &config->channels};
    for (std::size_t i = 0; i < 3; ++i) {
        const char* name = integer_fields[i];
        if (!params.contains(name)) {
            continue;
        }
        if (!params[name].is_number_integer()) {
            set_error(error, std::string(name) + " must be an integer");
            return false;
        }
        const auto value = params[name].get<long long>();
        if (value < 0) {
            set_error(error, std::string(name) + " must not be negative");
            return false;
        }
        *integer_targets[i] = static_cast<std::uint32_t>(value);
    }

    if (params.contains("gain")) {
        if (!params["gain"].is_number()) {
            set_error(error, "gain must be a number");
            return false;
        }
        config->gain = params["gain"].get<float>();
    }
    return true;
}

}  // namespace

struct ControlServer::Impl {
    explicit Impl(Engine& engine_ref) : engine(&engine_ref) {}

    Engine* engine;
    std::mutex engine_mutex;
    std::mutex socket_mutex;
    std::mutex wait_mutex;
    std::condition_variable wait_cv;

    socket_t listener = kInvalidSocket;
    socket_t client = kInvalidSocket;
    std::atomic<bool> running{false};
    std::atomic<bool> quit_requested{false};
    std::uint16_t bound_port = 0;
    std::thread thread;

    void run();
    void handle_client(socket_t client_socket);
    std::string process_line(const std::string& line);
    void shutdown_sockets();
    void request_stop();
};

void ControlServer::Impl::shutdown_sockets() {
    std::lock_guard<std::mutex> lock(socket_mutex);
    // shutdown() is what reliably wakes a thread blocked in accept()/recv();
    // the owning thread closes the descriptors afterwards.
    if (listener != kInvalidSocket) {
        shutdown_socket(listener);
        close_socket(listener);
        listener = kInvalidSocket;
    }
    if (client != kInvalidSocket) {
        shutdown_socket(client);
    }
}

void ControlServer::Impl::request_stop() {
    running.store(false);
    shutdown_sockets();
    wait_cv.notify_all();
}

std::string ControlServer::Impl::process_line(const std::string& line) {
    json request;
    try {
        request = json::parse(line);
    } catch (const json::parse_error&) {
        return failure(nullptr, -32700, "parse error").dump();
    }

    if (!request.is_object()) {
        return failure(nullptr, -32600, "invalid request").dump();
    }

    const json id = request.contains("id") ? request["id"] : json(nullptr);
    if (!request.contains("method") || !request["method"].is_string()) {
        return failure(id, -32600, "invalid request").dump();
    }

    const std::string method = request["method"].get<std::string>();
    json params = json::object();
    if (request.contains("params") && !request["params"].is_null()) {
        params = request["params"];
    }

    if (method == "ping") {
        return success(id, json{{"ok", true}}).dump();
    }
    if (method == "list_devices") {
        json devices = json::array();
        for (const DeviceInfo& device : list_devices()) {
            devices.push_back(json{{"name", device.name},
                                   {"is_input", device.is_input},
                                   {"is_output", device.is_output},
                                   {"is_default", device.is_default}});
        }
        return success(id, json{{"devices", devices}}).dump();
    }
    if (method == "status") {
        std::lock_guard<std::mutex> lock(engine_mutex);
        return success(id, engine_status(*engine)).dump();
    }
    if (method == "stop") {
        std::lock_guard<std::mutex> lock(engine_mutex);
        engine->stop();
        return success(id, engine_status(*engine)).dump();
    }
    if (method == "set_gain") {
        if (!params.is_object() || !params.contains("gain") || !params["gain"].is_number()) {
            return failure(id, -32602, "invalid params: gain is required").dump();
        }
        const float gain = params["gain"].get<float>();
        std::lock_guard<std::mutex> lock(engine_mutex);
        engine->set_gain(gain);
        return success(id, json{{"gain", engine->gain()}}).dump();
    }
    if (method == "start") {
        if (!params.is_object()) {
            return failure(id, -32602, "invalid params").dump();
        }
        EngineConfig config;
        std::string params_error;
        if (!apply_start_params(params, &config, &params_error)) {
            return failure(id, -32602, params_error).dump();
        }
        std::string engine_error;
        bool started = false;
        {
            std::lock_guard<std::mutex> lock(engine_mutex);
            started = engine->start(config, &engine_error);
        }
        if (!started) {
            return failure(id, -32000, engine_error.empty() ? "engine start failed" : engine_error)
                .dump();
        }
        std::lock_guard<std::mutex> lock(engine_mutex);
        return success(id, engine_status(*engine)).dump();
    }
    if (method == "quit") {
        quit_requested.store(true);
        return success(id, json{{"ok", true}}).dump();
    }

    return failure(id, -32601, "method not found: " + method).dump();
}

void ControlServer::Impl::handle_client(socket_t client_socket) {
    std::string buffer;
    char chunk[4096];
    while (running.load()) {
        const auto received = ::recv(client_socket, chunk, static_cast<int>(sizeof(chunk)), 0);
        if (received <= 0) {
            break;
        }
        buffer.append(chunk, static_cast<std::size_t>(received));

        std::size_t newline = std::string::npos;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            const std::string response = process_line(line);
            if (!send_all(client_socket, response + "\n")) {
                return;
            }
            if (quit_requested.load()) {
                request_stop();
                return;
            }
        }
    }
}

void ControlServer::Impl::run() {
    while (running.load()) {
        socket_t listening = kInvalidSocket;
        {
            std::lock_guard<std::mutex> lock(socket_mutex);
            listening = listener;
        }
        if (listening == kInvalidSocket) {
            break;
        }

        sockaddr_in address{};
        socklen_type address_length = sizeof(address);
        const socket_t client_socket =
            ::accept(listening, reinterpret_cast<sockaddr*>(&address), &address_length);
        if (client_socket == kInvalidSocket) {
            if (running.load()) {
                continue;
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lock(socket_mutex);
            client = client_socket;
        }
        handle_client(client_socket);
        {
            std::lock_guard<std::mutex> lock(socket_mutex);
            client = kInvalidSocket;
        }
        close_socket(client_socket);
    }
}

ControlServer::ControlServer(Engine& engine) : impl_(std::make_unique<Impl>(engine)) {}

ControlServer::~ControlServer() {
    stop();
}

bool ControlServer::start(const std::string& host, std::uint16_t port, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (!ensure_winsock()) {
        set_error(error, "failed to initialize the socket library");
        return false;
    }
    if (impl_->running.load()) {
        set_error(error, "control server is already running");
        return false;
    }

    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) {
        set_error(error, "failed to create the listen socket");
        return false;
    }

    const int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        set_error(error, "invalid host address: " + host);
        close_socket(listener);
        return false;
    }

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        set_error(error, "failed to bind " + host + ":" + std::to_string(port));
        close_socket(listener);
        return false;
    }
    if (::listen(listener, 4) != 0) {
        set_error(error, "failed to listen on " + host + ":" + std::to_string(port));
        close_socket(listener);
        return false;
    }

    sockaddr_in bound{};
    socklen_type bound_length = sizeof(bound);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &bound_length) == 0) {
        impl_->bound_port = ntohs(bound.sin_port);
    } else {
        impl_->bound_port = port;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->socket_mutex);
        impl_->listener = listener;
    }
    impl_->quit_requested.store(false);
    impl_->running.store(true);
    impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
    return true;
}

void ControlServer::stop() {
    impl_->request_stop();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

bool ControlServer::running() const {
    return impl_->running.load();
}

std::uint16_t ControlServer::port() const {
    return impl_->bound_port;
}

void ControlServer::wait() {
    std::unique_lock<std::mutex> lock(impl_->wait_mutex);
    impl_->wait_cv.wait(lock, [this] { return !impl_->running.load(); });
}

}  // namespace gitar
