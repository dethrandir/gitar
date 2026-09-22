#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gitar/ring_buffer.hpp"

namespace gitar {

// Writes 16-bit PCM WAV files without blocking the audio thread. write() is
// real-time safe: it only copies interleaved frames into a lock-free ring buffer
// and counts the frames it had to drop. A background thread drains the buffer to
// disk, sleeps on a condition variable, and patches the RIFF sizes on close.
//
// start() has restart semantics: calling it while already recording finalizes
// the current file and begins a new one.
class Recorder {
   public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Opens path and starts the writer thread. May allocate; never call from the
    // audio thread. On failure returns false, sets *error and leaves the
    // recorder idle.
    bool start(const std::filesystem::path& path, float sample_rate, std::uint32_t channels,
               std::string* error = nullptr);
    // Signals the writer, drains the remainder, flushes and joins. Idempotent.
    void stop();
    bool recording() const;
    // Audio thread. Copies whole frames, dropping (and counting) them when full.
    void write(const float* interleaved, std::size_t frames);
    std::uint64_t frames_written() const;
    std::uint64_t frames_dropped() const;
    std::string path() const;

   private:
    static constexpr std::size_t kCapacity = 1u << 18;
    static constexpr std::size_t kDrainFrames = 4096;
    static constexpr int kDrainIntervalMs = 5;

    void worker_main();
    void pump(std::vector<float>& scratch);
    void write_header(std::uint32_t data_bytes);
    void write_samples(const float* samples, std::size_t count);
    void finalize();

    SpscRingBuffer<float, kCapacity> input_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::ofstream file_;
    bool stop_requested_ = false;

    std::atomic<bool> running_{false};
    std::atomic<bool> active_{false};
    std::atomic<std::uint64_t> frames_written_{0};
    std::atomic<std::uint64_t> frames_dropped_{0};
    std::atomic<std::uint32_t> channels_{0};
    std::atomic<std::uint32_t> sample_rate_{0};
    std::string path_;
};

}  // namespace gitar
