#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace gitar {

class Engine;

// Line-delimited JSON-RPC control server bound to a localhost TCP port. Runs an
// accept loop on a background thread; the audio callbacks never touch it.
class ControlServer {
   public:
    explicit ControlServer(Engine& engine);
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(const std::string& host, std::uint16_t port, std::string* error = nullptr);
    void stop();
    bool running() const;
    std::uint16_t port() const;

    void wait();

    static constexpr std::uint16_t kDefaultPort = 7344;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gitar
