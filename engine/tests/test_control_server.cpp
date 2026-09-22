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
#include <sys/time.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "doctest/doctest.h"
#include "gitar/control_server.hpp"
#include "gitar/engine.hpp"
#include "nlohmann/json.hpp"

namespace {

using json = nlohmann::json;

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;

// Socket calls need Winsock initialized in this translation unit too, not just
// inside the control server, because the tests open their own client sockets.
struct WinsockInitializer {
    WinsockInitializer() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
};
[[maybe_unused]] const WinsockInitializer g_winsock_initializer;

void close_socket(socket_t socket) {
    ::closesocket(socket);
}
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;

void close_socket(socket_t socket) {
    ::close(socket);
}
#endif

std::string fixture_path(const char* name) {
    return (std::filesystem::path(GITAR_TEST_FIXTURES_DIR) / name).string();
}

class LoopbackClient {
   public:
    explicit LoopbackClient(std::uint16_t port) : fd_(::socket(AF_INET, SOCK_STREAM, 0)) {
        REQUIRE(fd_ != kInvalidSocket);
        timeval timeout{};
        timeout.tv_sec = 5;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                     static_cast<int>(sizeof(timeout)));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
        REQUIRE(::connect(fd_, reinterpret_cast<sockaddr*>(&address),
                          static_cast<int>(sizeof(address))) == 0);
    }

    ~LoopbackClient() {
        if (fd_ != kInvalidSocket) {
            close_socket(fd_);
        }
    }

    LoopbackClient(const LoopbackClient&) = delete;
    LoopbackClient& operator=(const LoopbackClient&) = delete;

    void send_raw(const std::string& line) {
        const std::string payload = line + "\n";
        std::size_t sent = 0;
        while (sent < payload.size()) {
            const auto written =
                ::send(fd_, payload.data() + sent, static_cast<int>(payload.size() - sent), 0);
            REQUIRE(written > 0);
            sent += static_cast<std::size_t>(written);
        }
    }

    std::string read_line() {
        std::size_t newline = std::string::npos;
        while ((newline = buffer_.find('\n')) == std::string::npos) {
            char chunk[1024];
            const auto received = ::recv(fd_, chunk, static_cast<int>(sizeof(chunk)), 0);
            REQUIRE(received > 0);
            buffer_.append(chunk, static_cast<std::size_t>(received));
        }
        std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        return line;
    }

    json exchange(const json& request) {
        send_raw(request.dump());
        return json::parse(read_line());
    }

   private:
    socket_t fd_ = kInvalidSocket;
    std::string buffer_;
};

json request_with_id(const json& id, const std::string& method) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
}

}  // namespace

TEST_CASE("ping returns ok and echoes the id") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));
    CHECK(error.empty());
    CHECK(server.running());
    CHECK(server.port() != 0);

    LoopbackClient client(server.port());
    const json response = client.exchange(request_with_id(42, "ping"));
    CHECK(response["id"] == 42);
    CHECK(response["result"]["ok"] == true);

    server.stop();
    CHECK_FALSE(server.running());
}

TEST_CASE("status on a fresh server is stopped with numeric defaults") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());
    const json response = client.exchange(request_with_id("status-1", "status"));
    CHECK(response["id"] == "status-1");
    const json& result = response["result"];
    CHECK(result["running"] == false);
    CHECK(result["gain"] == doctest::Approx(1.0));
    CHECK(result["input_peak_db"].is_number());
    CHECK(result["output_peak_db"].is_number());
    CHECK(result["pitch_hz"] == doctest::Approx(0.0));
    CHECK(result["pitch_confidence"] == doctest::Approx(0.0));
    CHECK(result["spectrum_db"].is_array());
    CHECK(result["spectrum_db"].size() == gitar::SpectrumAnalyzer::kBandCount);
    for (const auto& value : result["spectrum_db"]) {
        CHECK(value.is_number());
    }
    CHECK(result["latency_ms"].is_number());
    CHECK(result["sample_rate"].is_number_integer());
    CHECK(result["period_frames"].is_number_integer());
    CHECK(result["overrun_frames"].is_number_integer());
    CHECK(result["underrun_frames"].is_number_integer());

    server.stop();
}

TEST_CASE("set_gain applies a value and clamps out-of-range values") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());

    json response = client.exchange(
        json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "set_gain"}, {"params", {{"gain", 0.5}}}});
    CHECK(response["result"]["gain"] == doctest::Approx(0.5));

    response = client.exchange(
        json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "set_gain"}, {"params", {{"gain", 9.0}}}});
    CHECK(response["result"]["gain"] == doctest::Approx(4.0));

    response = client.exchange(request_with_id(3, "set_gain"));
    CHECK(response["error"]["code"] == -32602);

    server.stop();
}

TEST_CASE("list_devices returns an array without error") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());
    const json response = client.exchange(request_with_id(1, "list_devices"));
    CHECK_FALSE(response.contains("error"));
    CHECK(response["result"]["devices"].is_array());

    server.stop();
}

TEST_CASE("load_model loads a fixture and clear_model resets it") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());
    json response = client.exchange(json{{"jsonrpc", "2.0"},
                                         {"id", 1},
                                         {"method", "load_model"},
                                         {"params", {{"path", fixture_path("identity.nam")}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["model_loaded"] == true);
    CHECK(response["result"]["model_path"] == fixture_path("identity.nam"));

    response = client.exchange(request_with_id(2, "clear_model"));
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["model_loaded"] == false);
    CHECK(response["result"]["model_path"] == "");

    server.stop();
}

TEST_CASE("load_cab loads a fixture and set_eq round-trips") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());
    json response = client.exchange(json{{"jsonrpc", "2.0"},
                                         {"id", 1},
                                         {"method", "load_cab"},
                                         {"params", {{"path", fixture_path("impulse.wav")}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["cab_ir_loaded"] == true);
    CHECK(response["result"]["cab_ir_path"] == fixture_path("impulse.wav"));

    response = client.exchange(json{{"jsonrpc", "2.0"},
                                    {"id", 2},
                                    {"method", "set_eq"},
                                    {"params", {{"low_db", 3.0}, {"high_db", -3.0}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["eq_low_db"] == doctest::Approx(3.0));
    CHECK(response["result"]["eq_mid_db"] == doctest::Approx(0.0));
    CHECK(response["result"]["eq_high_db"] == doctest::Approx(-3.0));

    response = client.exchange(
        json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "set_eq"}, {"params", json::object()}});
    CHECK(response["error"]["code"] == -32602);

    server.stop();
}

TEST_CASE("load_cab reports a missing impulse response") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());
    const json response =
        client.exchange(json{{"jsonrpc", "2.0"},
                             {"id", 1},
                             {"method", "load_cab"},
                             {"params", {{"path", fixture_path("does_not_exist.wav")}}}});
    CHECK(response["error"]["code"] == -32000);

    server.stop();
}

TEST_CASE("start validates the eq and cabinet parameters") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());

    json response = client.exchange(json{
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "start"}, {"params", {{"eq_low_db", "loud"}}}});
    CHECK(response["error"]["code"] == -32602);

    response = client.exchange(
        json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "start"}, {"params", {{"cab_ir_path", 5}}}});
    CHECK(response["error"]["code"] == -32602);

    server.stop();
}

TEST_CASE("set_gate updates the status and requires at least one parameter") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());

    json response =
        client.exchange(json{{"jsonrpc", "2.0"},
                             {"id", 1},
                             {"method", "set_gate"},
                             {"params", {{"enabled", false}, {"threshold_db", -40.0}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["gate_enabled"] == false);
    CHECK(response["result"]["gate_threshold_db"] == doctest::Approx(-40.0));

    response = client.exchange(json{
        {"jsonrpc", "2.0"}, {"id", 2}, {"method", "set_gate"}, {"params", {{"enabled", true}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["gate_enabled"] == true);

    response = client.exchange(
        json{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "set_gate"}, {"params", json::object()}});
    CHECK(response["error"]["code"] == -32602);

    server.stop();
}

TEST_CASE("start_recording, stop_recording and the status counters round-trip") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());

    json status = client.exchange(request_with_id(1, "status"));
    CHECK(status["result"]["recording"] == false);
    CHECK(status["result"]["record_path"] == "");
    CHECK(status["result"]["record_frames"] == 0);

    const std::string path =
        (std::filesystem::temp_directory_path() / "gitar_server_rec.wav").string();
    std::filesystem::remove(path);
    json response = client.exchange(json{{"jsonrpc", "2.0"},
                                         {"id", 2},
                                         {"method", "start_recording"},
                                         {"params", {{"path", path}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["recording"] == true);
    CHECK(response["result"]["record_path"] == path);

    response = client.exchange(request_with_id(3, "stop_recording"));
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["recording"] == false);

    response = client.exchange(json{{"jsonrpc", "2.0"},
                                    {"id", 4},
                                    {"method", "start_recording"},
                                    {"params", {{"path", "/nonexistent_dir_xyz/out.wav"}}}});
    CHECK(response["error"]["code"] == -32000);

    response = client.exchange(json{
        {"jsonrpc", "2.0"}, {"id", 5}, {"method", "start_recording"}, {"params", json::object()}});
    CHECK(response["error"]["code"] == -32602);

    std::filesystem::remove(path);
    server.stop();
}

TEST_CASE("set_metronome updates the status and requires at least one parameter") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());

    json response = client.exchange(json{{"jsonrpc", "2.0"},
                                         {"id", 1},
                                         {"method", "set_metronome"},
                                         {"params", {{"enabled", true}, {"bpm", 90.0}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["metronome_enabled"] == true);
    CHECK(response["result"]["metronome_bpm"] == doctest::Approx(90.0));

    response = client.exchange(json{
        {"jsonrpc", "2.0"}, {"id", 2}, {"method", "set_metronome"}, {"params", {{"bpm", 1000.0}}}});
    REQUIRE_FALSE(response.contains("error"));
    CHECK(response["result"]["metronome_bpm"] == doctest::Approx(400.0));

    response = client.exchange(json{
        {"jsonrpc", "2.0"}, {"id", 3}, {"method", "set_metronome"}, {"params", json::object()}});
    CHECK(response["error"]["code"] == -32602);

    response = client.exchange(json{
        {"jsonrpc", "2.0"}, {"id", 4}, {"method", "set_metronome"}, {"params", {{"enabled", 1}}}});
    CHECK(response["error"]["code"] == -32602);

    server.stop();
}

TEST_CASE("start validates the metronome parameters") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());

    json response = client.exchange(json{{"jsonrpc", "2.0"},
                                         {"id", 1},
                                         {"method", "start"},
                                         {"params", {{"metronome_bpm", "fast"}}}});
    CHECK(response["error"]["code"] == -32602);

    response = client.exchange(json{{"jsonrpc", "2.0"},
                                    {"id", 2},
                                    {"method", "start"},
                                    {"params", {{"metronome_enabled", "yes"}}}});
    CHECK(response["error"]["code"] == -32602);

    server.stop();
}

TEST_CASE("protocol errors are reported and do not kill the connection") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());

    json response = client.exchange(request_with_id(1, "does_not_exist"));
    CHECK(response["error"]["code"] == -32601);

    client.send_raw("[1,2,3]");
    response = json::parse(client.read_line());
    CHECK(response["error"]["code"] == -32600);

    client.send_raw("{not json");
    response = json::parse(client.read_line());
    CHECK(response["error"]["code"] == -32700);

    response = client.exchange(request_with_id(2, "ping"));
    CHECK(response["result"]["ok"] == true);

    server.stop();
}

TEST_CASE("start with a missing input device reports an engine error") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());
    const json response =
        client.exchange(json{{"jsonrpc", "2.0"},
                             {"id", 7},
                             {"method", "start"},
                             {"params", {{"input_device", "__gitar_missing__"}}}});
    CHECK(response["error"]["code"] == -32000);
    CHECK(response["error"]["message"].is_string());

    const json status = client.exchange(request_with_id(8, "status"));
    CHECK(status["result"]["running"] == false);

    server.stop();
}

TEST_CASE("quit acknowledges and unblocks wait()") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));

    LoopbackClient client(server.port());
    const json response = client.exchange(request_with_id(1, "quit"));
    CHECK(response["result"]["ok"] == true);

    server.wait();
    CHECK_FALSE(server.running());

    server.stop();
}

TEST_CASE("the server is unreachable after stop") {
    gitar::Engine engine;
    gitar::ControlServer server(engine);
    std::string error;
    REQUIRE(server.start("127.0.0.1", 0, &error));
    const std::uint16_t port = server.port();

    server.stop();

    const socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd != kInvalidSocket);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    const int result =
        ::connect(fd, reinterpret_cast<sockaddr*>(&address), static_cast<int>(sizeof(address)));
    close_socket(fd);
    CHECK(result != 0);
}
