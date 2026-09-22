#include "gitar/recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>

namespace gitar {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

void write_le16(std::ostream& out, std::uint16_t value) {
    const char bytes[2] = {static_cast<char>(value & 0xFFu),
                           static_cast<char>((value >> 8) & 0xFFu)};
    out.write(bytes, 2);
}

void write_le32(std::ostream& out, std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu), static_cast<char>((value >> 24) & 0xFFu)};
    out.write(bytes, 4);
}

}  // namespace

Recorder::Recorder() : input_(std::make_unique<SpscRingBuffer<float, kCapacity>>()) {}

Recorder::~Recorder() {
    stop();
}

bool Recorder::start(const std::filesystem::path& path, float sample_rate, std::uint32_t channels,
                     std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    stop();

    if (channels == 0) {
        channels = 1;
    }
    std::uint32_t rate = 48000;
    if (sample_rate > 0.0f && std::isfinite(sample_rate)) {
        rate = static_cast<std::uint32_t>(std::lround(sample_rate));
    }
    if (rate == 0) {
        rate = 48000;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        set_error(error, "failed to open recording file: " + path.string());
        return false;
    }

    channels_.store(channels, std::memory_order_relaxed);
    sample_rate_.store(rate, std::memory_order_relaxed);
    frames_written_.store(0, std::memory_order_relaxed);
    frames_dropped_.store(0, std::memory_order_relaxed);
    file_ = std::move(file);
    write_header(0);
    if (!file_) {
        file_.close();
        set_error(error, "failed to write the WAV header: " + path.string());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = false;
    }
    path_ = path.string();
    running_.store(true, std::memory_order_relaxed);
    active_.store(true, std::memory_order_relaxed);
    worker_ = std::thread([this] { worker_main(); });
    return true;
}

void Recorder::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_.store(false, std::memory_order_relaxed);
        if (!worker_.joinable()) {
            running_.store(false, std::memory_order_relaxed);
            return;
        }
        stop_requested_ = true;
    }
    cv_.notify_all();
    worker_.join();
    running_.store(false, std::memory_order_relaxed);
}

bool Recorder::recording() const {
    return running_.load(std::memory_order_relaxed);
}

void Recorder::write(const float* interleaved, std::size_t frames) {
    if (!active_.load(std::memory_order_acquire) || interleaved == nullptr || frames == 0) {
        return;
    }
    const std::size_t channels = channels_.load(std::memory_order_relaxed);
    if (channels == 0) {
        return;
    }
    std::uint64_t dropped = 0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        // Drop whole frames so the channel interleaving stays aligned even though
        // the ring buffer stores flat samples.
        if (input_->size() + channels > kCapacity) {
            ++dropped;
            continue;
        }
        const float* const source = interleaved + frame * channels;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            input_->push(source[channel]);
        }
    }
    if (dropped != 0) {
        frames_dropped_.fetch_add(dropped, std::memory_order_relaxed);
    }
}

std::uint64_t Recorder::frames_written() const {
    return frames_written_.load(std::memory_order_relaxed);
}

std::uint64_t Recorder::frames_dropped() const {
    return frames_dropped_.load(std::memory_order_relaxed);
}

std::string Recorder::path() const {
    return path_;
}

void Recorder::worker_main() {
    const std::size_t channels = channels_.load(std::memory_order_relaxed);
    std::vector<float> scratch(kDrainFrames * channels);
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_requested_) {
        cv_.wait_for(lock, std::chrono::milliseconds(kDrainIntervalMs),
                     [this] { return stop_requested_; });
        lock.unlock();
        pump(scratch);
        lock.lock();
    }
    lock.unlock();
    pump(scratch);
    finalize();
}

void Recorder::pump(std::vector<float>& scratch) {
    const std::size_t channels = channels_.load(std::memory_order_relaxed);
    if (channels == 0 || !file_.is_open()) {
        return;
    }
    const std::size_t max_frames = scratch.size() / channels;
    while (input_->size() >= channels) {
        const std::size_t frames = std::min(max_frames, input_->size() / channels);
        if (frames == 0) {
            break;
        }
        const std::size_t count = frames * channels;
        for (std::size_t i = 0; i < count; ++i) {
            float sample = 0.0f;
            input_->pop(sample);
            scratch[i] = sample;
        }
        write_samples(scratch.data(), count);
        frames_written_.fetch_add(frames, std::memory_order_relaxed);
    }
}

void Recorder::write_header(std::uint32_t data_bytes) {
    const std::uint32_t channels = channels_.load(std::memory_order_relaxed);
    const std::uint32_t rate = sample_rate_.load(std::memory_order_relaxed);
    const std::uint16_t bits = 16;
    const std::uint16_t block_align = static_cast<std::uint16_t>(channels * bits / 8);
    const std::uint32_t byte_rate = rate * block_align;

    file_.write("RIFF", 4);
    write_le32(file_, 36u + data_bytes);
    file_.write("WAVE", 4);

    file_.write("fmt ", 4);
    write_le32(file_, 16);
    write_le16(file_, 1);  // PCM
    write_le16(file_, static_cast<std::uint16_t>(channels));
    write_le32(file_, rate);
    write_le32(file_, byte_rate);
    write_le16(file_, block_align);
    write_le16(file_, bits);

    file_.write("data", 4);
    write_le32(file_, data_bytes);
}

void Recorder::write_samples(const float* samples, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const float clamped = std::clamp(samples[i], -1.0f, 1.0f);
        const auto value = static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
        const auto bits = static_cast<std::uint16_t>(value);
        const char bytes[2] = {static_cast<char>(bits & 0xFFu),
                               static_cast<char>((bits >> 8) & 0xFFu)};
        file_.write(bytes, 2);
    }
}

void Recorder::finalize() {
    if (!file_.is_open()) {
        return;
    }
    const std::uint64_t frames = frames_written_.load(std::memory_order_relaxed);
    const std::uint64_t channels = channels_.load(std::memory_order_relaxed);
    const std::uint64_t data_bytes = frames * channels * 2;
    const auto data_size =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(data_bytes, 0xFFFFFFFFull));

    file_.seekp(4, std::ios::beg);
    write_le32(file_, 36u + data_size);
    file_.seekp(40, std::ios::beg);
    write_le32(file_, data_size);
    file_.flush();
    file_.close();
}

}  // namespace gitar
