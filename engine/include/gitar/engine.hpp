#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "gitar/spectrum_analyzer.hpp"

namespace gitar {

struct EngineConfig {
    std::string input_device;   // empty = system default capture
    std::string output_device;  // empty = system default playback
    std::uint32_t sample_rate = 48000;
    std::uint32_t period_frames = 128;
    std::uint32_t channels = 2;
    float gain = 1.0f;
    std::string model_path;            // empty = no neural model
    std::string cab_ir_path;           // empty = no cabinet IR
    bool gate_enabled = true;          // noise gate enabled on start
    float gate_threshold_db = -60.0f;  // noise gate threshold in dBFS
    float eq_low_db = 0.0f;            // three-band EQ low-shelf gain
    float eq_mid_db = 0.0f;            // three-band EQ mid-peaking gain
    float eq_high_db = 0.0f;           // three-band EQ high-shelf gain
    bool metronome_enabled = false;    // metronome click in the output
    float metronome_bpm = 120.0f;      // metronome tempo
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

    // Loads (or clears, with an empty path) the cabinet impulse response. A WAV
    // IR is handled by the same neural model loader. May allocate; never call
    // from the audio thread.
    bool load_cab_ir(const std::string& path, std::string* error = nullptr);
    bool cab_ir_loaded() const;
    std::string cab_ir_path() const;

    // Three-band EQ gains in dB, clamped to [-24, +24].
    void set_eq(float low_db, float mid_db, float high_db);
    float eq_low_db() const;
    float eq_mid_db() const;
    float eq_high_db() const;

    void set_gate_enabled(bool enabled);
    bool gate_enabled() const;
    void set_gate_threshold_db(float db);
    float gate_threshold_db() const;

    // Recording taps the final interleaved output. start_recording() may
    // allocate and fails (with *error) if the WAV cannot be opened. Recording is
    // independent of the engine run state: a recording keeps its format until it
    // is stopped or the engine is destroyed.
    bool start_recording(const std::string& path, std::string* error = nullptr);
    void stop_recording();
    bool recording() const;
    std::string recording_path() const;
    std::uint64_t recorded_frames() const;
    std::uint64_t dropped_record_frames() const;

    // The metronome is mixed into the output, never replacing it. bpm is clamped
    // to [20, 400].
    void set_metronome(bool enabled, float bpm);
    bool metronome_enabled() const;
    float metronome_bpm() const;

    float input_peak_db() const;
    float output_peak_db() const;

    // Monophonic pitch of the input, 0 Hz when no confident pitch is present
    // (including while the engine is not running).
    float pitch_hz() const;
    float pitch_confidence() const;

    // Smoothed log-band input spectrum in dB, each band mapped from -120 to 0.
    // All bands read -120 while the engine is not running.
    std::array<float, SpectrumAnalyzer::kBandCount> spectrum_db() const;

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
