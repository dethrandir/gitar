#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "gitar/engine.hpp"

namespace gitar::cli {

enum class Command { None, Run, Devices, Version, Help };

struct Options {
    Command command = Command::None;
    bool control = true;
    std::string control_host = "127.0.0.1";
    std::uint16_t control_port = 7344;
    bool has_audio = false;  // true if any audio option was provided
    EngineConfig audio;      // defaults from EngineConfig
    std::string error;       // non-empty => parse error
};

namespace detail {

inline bool parse_unsigned(const char* text, unsigned long long minimum, unsigned long long maximum,
                           unsigned long long* out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < minimum || value > maximum) {
        return false;
    }
    *out = value;
    return true;
}

// Rejects NaN as well as values outside [minimum, maximum].
inline bool parse_float(const char* text, float minimum, float maximum, float* out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !(value >= minimum) || value > maximum) {
        return false;
    }
    *out = value;
    return true;
}

inline Options parse_run(int argc, const char* const* argv) {
    Options options;
    options.command = Command::Run;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--no-control") {
            options.control = false;
            continue;
        }

        const bool is_option = arg == "--control-host" || arg == "--control-port" ||
                               arg == "--input" || arg == "--output" || arg == "--rate" ||
                               arg == "--period" || arg == "--channels" || arg == "--gain";
        if (!is_option) {
            options.error = "unknown option: " + arg;
            options.command = Command::None;
            return options;
        }

        if (i + 1 >= argc) {
            options.error = "missing value for " + arg;
            options.command = Command::None;
            return options;
        }
        const char* value = argv[++i];

        if (arg == "--control-host") {
            options.control_host = value;
            continue;
        }
        if (arg == "--input") {
            options.audio.input_device = value;
            options.has_audio = true;
            continue;
        }
        if (arg == "--output") {
            options.audio.output_device = value;
            options.has_audio = true;
            continue;
        }

        unsigned long long parsed = 0;
        if (arg == "--control-port") {
            if (!parse_unsigned(value, 1, 65535, &parsed)) {
                options.error = "invalid control port (expected 1..65535): " + std::string(value);
                options.command = Command::None;
                return options;
            }
            options.control_port = static_cast<std::uint16_t>(parsed);
        } else if (arg == "--rate") {
            if (!parse_unsigned(value, 1, 0xffffffffULL, &parsed)) {
                options.error = "invalid sample rate (expected > 0): " + std::string(value);
                options.command = Command::None;
                return options;
            }
            options.audio.sample_rate = static_cast<std::uint32_t>(parsed);
            options.has_audio = true;
        } else if (arg == "--period") {
            if (!parse_unsigned(value, 1, 0xffffffffULL, &parsed)) {
                options.error = "invalid period (expected > 0): " + std::string(value);
                options.command = Command::None;
                return options;
            }
            options.audio.period_frames = static_cast<std::uint32_t>(parsed);
            options.has_audio = true;
        } else if (arg == "--channels") {
            if (!parse_unsigned(value, 1, 64, &parsed)) {
                options.error = "invalid channel count (expected 1..64): " + std::string(value);
                options.command = Command::None;
                return options;
            }
            options.audio.channels = static_cast<std::uint32_t>(parsed);
            options.has_audio = true;
        } else if (arg == "--gain") {
            float gain = 0.0f;
            if (!parse_float(value, 0.0f, 4.0f, &gain)) {
                options.error = "invalid gain (expected 0..4): " + std::string(value);
                options.command = Command::None;
                return options;
            }
            options.audio.gain = gain;
            options.has_audio = true;
        }
    }

    return options;
}

}  // namespace detail

inline Options parse_args(int argc, const char* const* argv) {
    Options options;
    if (argc <= 1) {
        options.command = Command::Help;
        return options;
    }

    const std::string first = argv[1];
    if (first == "-h" || first == "--help" || first == "help") {
        options.command = Command::Help;
    } else if (first == "-V" || first == "--version" || first == "version") {
        options.command = Command::Version;
    } else if (first == "devices") {
        options.command = Command::Devices;
    } else if (first == "run") {
        return detail::parse_run(argc, argv);
    } else {
        options.error = "unknown command: " + first;
        return options;
    }

    if (argc > 2) {
        options.error = "unexpected argument: " + std::string(argv[2]);
        options.command = Command::None;
    }
    return options;
}

inline std::string usage() {
    return "gitar-engine - real-time guitar audio engine\n"
           "\n"
           "Usage:\n"
           "  gitar-engine run [options]\n"
           "  gitar-engine devices\n"
           "  gitar-engine version | --version | -V\n"
           "  gitar-engine help | --help | -h\n"
           "\n"
           "Commands:\n"
           "  run                  start the engine, optionally with the control server\n"
           "  devices              list the available audio devices\n"
           "  version              print the engine version\n"
           "  help                 show this help\n"
           "\n"
           "Options for run:\n"
           "  --control-host HOST  control server address (default 127.0.0.1)\n"
           "  --control-port PORT  control server port (default 7344, valid 1..65535)\n"
           "  --no-control         do not start the control server\n"
           "  --input NAME         capture device name (default system capture)\n"
           "  --output NAME        playback device name (default system playback)\n"
           "  --rate N             sample rate in Hz (default 48000)\n"
           "  --period N           period size in frames (default 128)\n"
           "  --channels N         channel count, 1..64 (default 2)\n"
           "  --gain F             output gain, 0..4 (default 1.0)\n"
           "\n"
           "Other options:\n"
           "  --help, -h           show this help\n"
           "  --version, -V        show the version\n";
}

}  // namespace gitar::cli
