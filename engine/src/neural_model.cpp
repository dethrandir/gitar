#include "gitar/neural_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "NAM/get_dsp.h"

namespace gitar {
namespace {

// Upper bound on a single DSP process() call. Longer blocks are split so the
// per-channel scratch buffers stay bounded and no allocation is needed here.
constexpr int kMaxBlockFrames = 4096;

static_assert(std::is_same_v<NAM_SAMPLE, float>,
              "gitar_nam must be built with NAM_SAMPLE_FLOAT so float audio can be processed");

}  // namespace

struct NeuralModel::Impl {
    std::unique_ptr<nam::DSP> dsp;
    int in_channels = 0;
    int out_channels = 0;
    double expected_rate = -1.0;

    // Preallocated for load()ed channel counts so process() is allocation-free.
    std::vector<NAM_SAMPLE*> in_ptrs;
    std::vector<NAM_SAMPLE*> out_ptrs;
    std::vector<std::vector<NAM_SAMPLE>> in_scratch;
    std::vector<std::vector<NAM_SAMPLE>> out_scratch;

    void clear() {
        dsp.reset();
        in_channels = 0;
        out_channels = 0;
        expected_rate = -1.0;
        in_ptrs.clear();
        out_ptrs.clear();
        in_scratch.clear();
        out_scratch.clear();
    }
};

NeuralModel::NeuralModel() : impl_(std::make_unique<Impl>()) {}

NeuralModel::~NeuralModel() = default;

NeuralModel::NeuralModel(NeuralModel&&) noexcept = default;

NeuralModel& NeuralModel::operator=(NeuralModel&&) noexcept = default;

bool NeuralModel::load(const std::filesystem::path& path, std::string* error) {
    impl_->clear();
    if (error != nullptr) {
        error->clear();
    }
    try {
        std::unique_ptr<nam::DSP> dsp = nam::get_dsp(path);
        if (dsp == nullptr) {
            if (error != nullptr) {
                *error = "model loader returned no DSP for: " + path.string();
            }
            return false;
        }

        impl_->in_channels = dsp->NumInputChannels();
        impl_->out_channels = dsp->NumOutputChannels();
        impl_->expected_rate = dsp->GetExpectedSampleRate();

        impl_->in_ptrs.assign(static_cast<std::size_t>(impl_->in_channels), nullptr);
        impl_->out_ptrs.assign(static_cast<std::size_t>(impl_->out_channels), nullptr);
        impl_->in_scratch.assign(
            static_cast<std::size_t>(std::max(0, impl_->in_channels - 1)),
            std::vector<NAM_SAMPLE>(static_cast<std::size_t>(kMaxBlockFrames), NAM_SAMPLE{0}));
        impl_->out_scratch.assign(
            static_cast<std::size_t>(std::max(0, impl_->out_channels - 1)),
            std::vector<NAM_SAMPLE>(static_cast<std::size_t>(kMaxBlockFrames), NAM_SAMPLE{0}));
        impl_->dsp = std::move(dsp);
        return true;
    } catch (const std::exception& ex) {
        impl_->clear();
        if (error != nullptr) {
            *error = ex.what();
        }
        return false;
    }
}

bool NeuralModel::loaded() const {
    return impl_->dsp != nullptr;
}

void NeuralModel::reset() {
    if (impl_->dsp == nullptr) {
        return;
    }
    // Models with an unknown training rate cannot be reset through the NAM API;
    // they still process, they just keep their current history.
    if (std::isfinite(impl_->expected_rate) && impl_->expected_rate > 0.0) {
        impl_->dsp->Reset(impl_->expected_rate, kMaxBlockFrames);
    }
}

int NeuralModel::input_channels() const {
    return impl_->in_channels;
}

int NeuralModel::output_channels() const {
    return impl_->out_channels;
}

double NeuralModel::expected_sample_rate() const {
    return impl_->expected_rate;
}

void NeuralModel::process(const float* input, float* output, int frames) {
    if (frames <= 0) {
        return;
    }
    if (impl_->dsp == nullptr || input == nullptr || output == nullptr) {
        if (output != nullptr) {
            std::fill(output, output + frames, 0.0f);
        }
        return;
    }

    const float* in = input;
    float* out = output;
    int remaining = frames;
    while (remaining > 0) {
        const int block = std::min(remaining, kMaxBlockFrames);

        impl_->in_ptrs[0] = const_cast<NAM_SAMPLE*>(in);
        for (std::size_t ch = 1; ch < impl_->in_ptrs.size(); ++ch) {
            impl_->in_ptrs[ch] = impl_->in_scratch[ch - 1].data();
        }
        impl_->out_ptrs[0] = out;
        for (std::size_t ch = 1; ch < impl_->out_ptrs.size(); ++ch) {
            impl_->out_ptrs[ch] = impl_->out_scratch[ch - 1].data();
        }

        impl_->dsp->process(impl_->in_ptrs.data(), impl_->out_ptrs.data(), block);

        in += block;
        out += block;
        remaining -= block;
    }
}

}  // namespace gitar
