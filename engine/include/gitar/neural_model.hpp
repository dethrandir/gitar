#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace gitar {

// Owns a loaded NAM DSP (a .nam model or a WAV impulse response). The pimpl
// keeps NAM, Eigen, and nlohmann/json out of every translation unit that only
// routes audio through this class.
class NeuralModel {
   public:
    NeuralModel();
    ~NeuralModel();
    NeuralModel(NeuralModel&&) noexcept;
    NeuralModel& operator=(NeuralModel&&) noexcept;
    NeuralModel(const NeuralModel&) = delete;
    NeuralModel& operator=(const NeuralModel&) = delete;

    // Loads a .nam model or a WAV impulse response. Returns false and fills
    // *error on failure. May allocate; never call from the audio thread.
    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    bool loaded() const;
    void reset();

    int input_channels() const;
    int output_channels() const;
    double expected_sample_rate() const;

    // Processes mono audio (channel 0 of the model). `input` and `output` may
    // alias. Real-time safe: never allocates or locks.
    void process(const float* input, float* output, int frames);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gitar
