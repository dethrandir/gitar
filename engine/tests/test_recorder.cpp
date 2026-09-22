#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/recorder.hpp"

namespace {

struct WavFile {
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits = 0;
    std::vector<std::int16_t> samples;
};

std::uint16_t read_u16(std::istream& in) {
    unsigned char bytes[2] = {0, 0};
    in.read(reinterpret_cast<char*>(bytes), 2);
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::uint32_t read_u32(std::istream& in) {
    unsigned char bytes[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(bytes), 4);
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::string read_tag(std::istream& in) {
    char bytes[4] = {0, 0, 0, 0};
    in.read(bytes, 4);
    return std::string(bytes, 4);
}

WavFile read_wav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    REQUIRE(read_tag(in) == "RIFF");
    (void)read_u32(in);
    REQUIRE(read_tag(in) == "WAVE");
    REQUIRE(read_tag(in) == "fmt ");
    REQUIRE(read_u32(in) == 16);
    REQUIRE(read_u16(in) == 1);  // PCM

    WavFile wav;
    wav.channels = read_u16(in);
    wav.sample_rate = read_u32(in);
    (void)read_u32(in);  // byte rate
    (void)read_u16(in);  // block align
    wav.bits = read_u16(in);
    REQUIRE(read_tag(in) == "data");
    const std::uint32_t data_size = read_u32(in);
    wav.samples.resize(data_size / 2);
    for (std::size_t i = 0; i < wav.samples.size(); ++i) {
        wav.samples[i] = static_cast<std::int16_t>(read_u16(in));
    }
    return wav;
}

std::filesystem::path temp_wav(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

TEST_CASE("recording writes a 16-bit WAV that round-trips the samples") {
    const std::filesystem::path path = temp_wav("gitar_recorder_basic.wav");
    std::filesystem::remove(path);

    gitar::Recorder recorder;
    std::string error;
    REQUIRE(recorder.start(path, 48000.0f, 2, &error));
    CHECK(error.empty());
    CHECK(recorder.recording());
    CHECK(recorder.path() == path.string());

    const std::vector<float> frames = {
        0.0f, 0.5f, -0.25f, 1.0f, -1.0f, 0.125f, -0.75f, 0.0f,
    };
    recorder.write(frames.data(), frames.size() / 2);
    recorder.stop();

    CHECK_FALSE(recorder.recording());
    CHECK(recorder.frames_written() == 4);
    CHECK(recorder.frames_dropped() == 0);

    const WavFile wav = read_wav(path);
    CHECK(wav.channels == 2);
    CHECK(wav.sample_rate == 48000);
    CHECK(wav.bits == 16);
    REQUIRE(wav.samples.size() == frames.size());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const float decoded = static_cast<float>(wav.samples[i]) / 32767.0f;
        CHECK(std::abs(decoded - frames[i]) <= 1.0f / 32767.0f);
    }

    std::filesystem::remove(path);
}

TEST_CASE("stop without start is safe and write without start is a no-op") {
    gitar::Recorder recorder;
    CHECK_NOTHROW(recorder.stop());
    CHECK_FALSE(recorder.recording());
    CHECK(recorder.frames_written() == 0);
    CHECK(recorder.frames_dropped() == 0);

    const std::vector<float> frame = {0.5f, -0.5f};
    CHECK_NOTHROW(recorder.write(frame.data(), 1));
    CHECK(recorder.frames_written() == 0);
    CHECK(recorder.frames_dropped() == 0);
}

TEST_CASE("starting twice finalizes the first file and begins a new one") {
    const std::filesystem::path first = temp_wav("gitar_recorder_first.wav");
    const std::filesystem::path second = temp_wav("gitar_recorder_second.wav");
    std::filesystem::remove(first);
    std::filesystem::remove(second);

    gitar::Recorder recorder;
    std::string error;
    REQUIRE(recorder.start(first, 44100.0f, 1, &error));

    const std::vector<float> one = {0.5f};
    const std::vector<float> two = {-0.5f};
    recorder.write(one.data(), 1);

    REQUIRE(recorder.start(second, 44100.0f, 1, &error));
    CHECK(recorder.recording());
    CHECK(recorder.path() == second.string());
    recorder.write(two.data(), 1);
    recorder.stop();

    CHECK(recorder.frames_written() == 1);

    const WavFile first_wav = read_wav(first);
    CHECK(first_wav.channels == 1);
    CHECK(first_wav.sample_rate == 44100);
    REQUIRE(first_wav.samples.size() == 1);
    CHECK(static_cast<float>(first_wav.samples[0]) / 32767.0f == doctest::Approx(0.5f));

    const WavFile second_wav = read_wav(second);
    REQUIRE(second_wav.samples.size() == 1);
    CHECK(static_cast<float>(second_wav.samples[0]) / 32767.0f == doctest::Approx(-0.5f));

    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST_CASE("start reports a file-open failure without starting the thread") {
    gitar::Recorder recorder;
    std::string error;
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "gitar_missing_dir_xyz" / "out.wav";

    CHECK_FALSE(recorder.start(bad, 48000.0f, 1, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(recorder.recording());
}
