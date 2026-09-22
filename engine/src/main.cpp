#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "gitar/cli.hpp"
#include "gitar/control_server.hpp"
#include "gitar/devices.hpp"
#include "gitar/engine.hpp"
#include "gitar/version.hpp"

namespace {

std::atomic<bool> g_interrupted{false};

void on_sigint(int) {
    g_interrupted.store(true);
}

void print_version() {
    std::cout << "gitar-engine " << gitar::kEngineVersion << '\n';
}

void print_device_group(const std::vector<gitar::DeviceInfo>& devices, bool input) {
    std::cout << (input ? "capture devices:" : "playback devices:") << '\n';
    for (const gitar::DeviceInfo& device : devices) {
        if (device.is_input != input) {
            continue;
        }
        std::cout << "  " << device.name << (device.is_default ? " [default]" : "") << '\n';
    }
}

int run_devices() {
    const std::vector<gitar::DeviceInfo> devices = gitar::list_devices();
    if (devices.empty()) {
        std::cout << "no audio devices found\n";
        return 0;
    }
    print_device_group(devices, true);
    print_device_group(devices, false);
    return 0;
}

int run_engine(const gitar::cli::Options& options) {
    if (!options.control && !options.has_audio) {
        std::cerr << "error: --no-control requires at least one audio option\n";
        return 2;
    }

    gitar::Engine engine;
    gitar::ControlServer server(engine);

    if (options.control) {
        std::string error;
        if (!server.start(options.control_host, options.control_port, &error)) {
            std::cerr << "error: failed to start the control server: " << error << '\n';
            return 1;
        }
        std::cout << "control listening on " << options.control_host << ':' << server.port()
                  << '\n';
        std::cout.flush();
    }

    if (options.has_audio) {
        std::string error;
        if (engine.start(options.audio, &error)) {
            std::cout << "engine started: " << engine.sample_rate() << " Hz, "
                      << engine.period_frames() << " frames\n";
            std::cout.flush();
        } else {
            std::cerr << "warning: failed to start audio: " << error << '\n';
        }
    }

    std::signal(SIGINT, on_sigint);
    while (!g_interrupted.load() && (!options.control || server.running())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    server.stop();
    engine.stop();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const gitar::cli::Options options = gitar::cli::parse_args(argc, argv);

    if (!options.error.empty()) {
        std::cerr << "error: " << options.error << '\n' << gitar::cli::usage();
        return 2;
    }

    switch (options.command) {
        case gitar::cli::Command::Run:
            return run_engine(options);
        case gitar::cli::Command::Devices:
            return run_devices();
        case gitar::cli::Command::Version:
            print_version();
            return 0;
        case gitar::cli::Command::Help:
        case gitar::cli::Command::None:
        default:
            std::cout << gitar::cli::usage();
            return 0;
    }
}
