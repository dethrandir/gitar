#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "gitar/control_server.hpp"
#include "gitar/engine.hpp"
#include "nlohmann/json.hpp"

namespace {

using json = nlohmann::json;

class LoopbackClient {
   public:
    explicit LoopbackClient(std::uint16_t port) : fd_(::socket(AF_INET, SOCK_STREAM, 0)) {
        REQUIRE(fd_ >= 0);
        timeval timeout{};
        timeout.tv_sec = 5;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
        REQUIRE(::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    }

    ~LoopbackClient() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    LoopbackClient(const LoopbackClient&) = delete;
    LoopbackClient& operator=(const LoopbackClient&) = delete;

    void send_raw(const std::string& line) {
        const std::string payload = line + "\n";
        std::size_t sent = 0;
        while (sent < payload.size()) {
            const auto written = ::send(fd_, payload.data() + sent, payload.size() - sent, 0);
            REQUIRE(written > 0);
            sent += static_cast<std::size_t>(written);
        }
    }

    std::string read_line() {
        std::size_t newline = std::string::npos;
        while ((newline = buffer_.find('\n')) == std::string::npos) {
            char chunk[1024];
            const auto received = ::recv(fd_, chunk, sizeof(chunk), 0);
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
    int fd_ = -1;
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

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    ::close(fd);
    CHECK(result != 0);
}
