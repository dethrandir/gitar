#include <initializer_list>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/cli.hpp"

namespace {

gitar::cli::Options parse(std::initializer_list<const char*> args) {
    std::vector<const char*> argv(args);
    return gitar::cli::parse_args(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST_CASE("no arguments prints help") {
    gitar::cli::Options options = parse({"gitar-engine"});
    CHECK(options.command == gitar::cli::Command::Help);
    CHECK(options.error.empty());
}

TEST_CASE("help flags print help") {
    CHECK(parse({"gitar-engine", "--help"}).command == gitar::cli::Command::Help);
    CHECK(parse({"gitar-engine", "-h"}).command == gitar::cli::Command::Help);
    CHECK(parse({"gitar-engine", "help"}).command == gitar::cli::Command::Help);
}

TEST_CASE("version flags print the version") {
    CHECK(parse({"gitar-engine", "--version"}).command == gitar::cli::Command::Version);
    CHECK(parse({"gitar-engine", "-V"}).command == gitar::cli::Command::Version);
    CHECK(parse({"gitar-engine", "version"}).command == gitar::cli::Command::Version);
}

TEST_CASE("devices command is recognized") {
    gitar::cli::Options options = parse({"gitar-engine", "devices"});
    CHECK(options.command == gitar::cli::Command::Devices);
    CHECK(options.error.empty());
}

TEST_CASE("run with no options uses defaults") {
    gitar::cli::Options options = parse({"gitar-engine", "run"});
    CHECK(options.command == gitar::cli::Command::Run);
    CHECK(options.control);
    CHECK(options.control_host == "127.0.0.1");
    CHECK(options.control_port == 7344);
    CHECK_FALSE(options.has_audio);
    CHECK(options.error.empty());
}

TEST_CASE("run accepts a full set of options") {
    gitar::cli::Options options =
        parse({"gitar-engine", "run", "--no-control", "--input", "mic", "--output", "hp", "--rate",
               "44100", "--period", "64", "--channels", "1", "--gain", "0.5"});
    REQUIRE(options.error.empty());
    CHECK(options.command == gitar::cli::Command::Run);
    CHECK_FALSE(options.control);
    CHECK(options.has_audio);
    CHECK(options.audio.input_device == "mic");
    CHECK(options.audio.output_device == "hp");
    CHECK(options.audio.sample_rate == 44100);
    CHECK(options.audio.period_frames == 64);
    CHECK(options.audio.channels == 1);
    CHECK(options.audio.gain == doctest::Approx(0.5f));
}

TEST_CASE("run overrides the control host and port") {
    gitar::cli::Options options =
        parse({"gitar-engine", "run", "--control-host", "0.0.0.0", "--control-port", "9999"});
    REQUIRE(options.error.empty());
    CHECK(options.control_host == "0.0.0.0");
    CHECK(options.control_port == 9999);
    CHECK_FALSE(options.has_audio);
}

TEST_CASE("out of range and unknown options are errors") {
    const std::initializer_list<std::initializer_list<const char*>> bad = {
        {"gitar-engine", "run", "--control-port", "0"},
        {"gitar-engine", "run", "--control-port", "65536"},
        {"gitar-engine", "run", "--rate", "0"},
        {"gitar-engine", "run", "--channels", "100"},
        {"gitar-engine", "run", "--gain", "9"},
        {"gitar-engine", "run", "--bogus"},
        {"gitar-engine", "run", "--input"},
        {"gitar-engine", "devices", "--input", "x"},
        {"gitar-engine", "bogus"},
    };
    for (const auto& args : bad) {
        gitar::cli::Options options = parse(args);
        INFO(args.size());
        CHECK(options.command == gitar::cli::Command::None);
        CHECK_FALSE(options.error.empty());
    }
}

TEST_CASE("usage is non-empty and documents the commands") {
    const std::string text = gitar::cli::usage();
    CHECK_FALSE(text.empty());
    CHECK(text.find("run") != std::string::npos);
    CHECK(text.find("devices") != std::string::npos);
}
