#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace gitar {

struct EngineConfig {
    std::string input_device;   // empty = system default capture
    std::string output_device;  // empty = system default playback
    std::uint32_t sample_rate = 48000;
    std::uint32_t period_frames = 128;
    std::uint32_t channels = 2;
    float gain = 1.0f;
    std::string model_path;            // empty = no neural model
    bool gate_enabled = true;          // noise gate enabled on start
    float gate_threshold_db = -60.0f;  // noise gate threshold in dBFS
};

// Owns a capture device and a playback device connected by a lock-free bridge.
// start() on an already running engine stops the current devices first, then
// starts the requested configuration (restart semantics).
class Engine {
   public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool start(const EngineConfig& config, std::string* error = nullptr);
    void stop();
    bool running() const;

    void set_gain(float gain);
    float gain() const;

    // Loads (or clears, with an empty path) the neural model. The swap is
    // atomic so a running audio callback always sees a complete model. May
    // allocate; never call from the audio thread.
    bool load_model(const std::string& path, std::string* error = nullptr);
    bool model_loaded() const;
    std::string model_path() const;

    void set_gate_enabled(bool enabled);
    bool gate_enabled() const;
    void set_gate_threshold_db(float db);
    float gate_threshold_db() const;

    float input_peak_db() const;
    float output_peak_db() const;

    double latency_ms() const;
    std::uint32_t sample_rate() const;
    std::uint32_t period_frames() const;
    std::uint64_t overrun_frames() const;
    std::uint64_t underrun_frames() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gitar
