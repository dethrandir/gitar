#include "gitar/engine.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gitar/bridge.hpp"
#include "gitar/level_meter.hpp"
#include "gitar/neural_model.hpp"
#include "gitar/noise_gate.hpp"
#include "gitar/processor.hpp"
#include "miniaudio.h"

namespace gitar {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

// miniaudio 0.11.21 does not expose ma_device_get_latency, so derive the
// per-device latency from the internal period layout the backend selected.
double side_latency_ms(ma_uint32 period_frames, ma_uint32 periods, ma_uint32 rate,
                       ma_uint32 fallback_rate) {
    const ma_uint32 effective_rate = rate != 0 ? rate : fallback_rate;
    if (effective_rate == 0) {
        return 0.0;
    }
    const ma_uint32 total_frames = period_frames * (periods != 0 ? periods : 1u);
    return 1000.0 * static_cast<double>(total_frames) / static_cast<double>(effective_rate);
}

}  // namespace

struct Engine::Impl {
    EngineConfig config;
    ma_context context{};
    ma_device capture{};
    ma_device playback{};
    bool context_ready = false;
    bool capture_ready = false;
    bool playback_ready = false;

    std::unique_ptr<InterleavedBridge> bridge;
    std::unique_ptr<GainProcessor> processor;
    std::unique_ptr<LevelMeter> input_meter;
    std::unique_ptr<NoiseGate> gate;

    // The control thread swaps this while the playback callback reads it, so the
    // callback never observes a half-built model. shared_ptr keeps the old model
    // alive until the callback that loaded it has finished.
    std::atomic<std::shared_ptr<NeuralModel>> model;
    std::string model_path;
    std::vector<float> mono_scratch;

    std::atomic<bool> running{false};
    std::atomic<float> requested_gain{1.0f};
    std::atomic<bool> requested_gate_enabled{true};
    std::atomic<float> requested_gate_threshold_db{-60.0f};
    std::uint32_t actual_sample_rate = 48000;
    std::uint32_t actual_period_frames = 0;

    static void capture_callback(ma_device* device, void* output, const void* input,
                                 ma_uint32 frame_count);
    static void playback_callback(ma_device* device, void* output, const void* input,
                                  ma_uint32 frame_count);

    bool resolve_device_id(ma_device_type type, const std::string& name,
                           const ma_device_id** out_id);
    bool start(const EngineConfig& requested, std::string* error);
    void release();
};

void Engine::Impl::capture_callback(ma_device* device, void* output, const void* input,
                                    ma_uint32 frame_count) {
    (void)output;
    auto* self = static_cast<Impl*>(device->pUserData);
    if (self == nullptr || input == nullptr || self->bridge == nullptr) {
        return;
    }
    const auto* samples = static_cast<const float*>(input);
    const std::size_t sample_count = static_cast<std::size_t>(frame_count) * self->config.channels;
    if (self->input_meter != nullptr) {
        self->input_meter->process(samples, sample_count);
    }
    self->bridge->write(samples, frame_count);
}

void Engine::Impl::playback_callback(ma_device* device, void* output, const void* input,
                                     ma_uint32 frame_count) {
    (void)input;
    auto* self = static_cast<Impl*>(device->pUserData);
    if (self == nullptr || output == nullptr || self->bridge == nullptr) {
        return;
    }
    auto* samples = static_cast<float*>(output);
    self->bridge->read(samples, frame_count);

    NoiseGate* const gate = self->gate.get();
    const std::shared_ptr<NeuralModel> model = self->model.load(std::memory_order_acquire);
    if ((gate != nullptr && gate->enabled()) || model != nullptr) {
        const std::uint32_t channels = self->config.channels;
        const std::size_t capacity = self->mono_scratch.size();
        std::size_t offset = 0;
        while (offset < frame_count && capacity > 0) {
            const std::size_t chunk = std::min<std::size_t>(capacity, frame_count - offset);
            float* const mono = self->mono_scratch.data();
            for (std::size_t i = 0; i < chunk; ++i) {
                mono[i] = samples[(offset + i) * channels];
            }
            if (gate != nullptr) {
                gate->process(mono, chunk);
            }
            if (model != nullptr) {
                model->process(mono, mono, static_cast<int>(chunk));
            }
            for (std::size_t i = 0; i < chunk; ++i) {
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    samples[(offset + i) * channels + channel] = mono[i];
                }
            }
            offset += chunk;
        }
    }

    if (self->processor != nullptr) {
        self->processor->process(samples, frame_count);
    }
}

bool Engine::Impl::resolve_device_id(ma_device_type type, const std::string& name,
                                     const ma_device_id** out_id) {
    *out_id = nullptr;
    if (name.empty()) {
        return true;
    }

    ma_device_info* playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_infos = nullptr;
    ma_uint32 capture_count = 0;
    if (ma_context_get_devices(&context, &playback_infos, &playback_count, &capture_infos,
                               &capture_count) != MA_SUCCESS) {
        return false;
    }

    const bool capture_type = type == ma_device_type_capture;
    ma_device_info* infos = capture_type ? capture_infos : playback_infos;
    const ma_uint32 count = capture_type ? capture_count : playback_count;
    for (ma_uint32 i = 0; i < count; ++i) {
        if (name == infos[i].name) {
            *out_id = &infos[i].id;
            return true;
        }
    }
    return false;
}

bool Engine::Impl::start(const EngineConfig& requested, std::string* error) {
    release();

    config = requested;
    if (config.channels == 0) {
        config.channels = 1;
    }
    if (config.sample_rate == 0) {
        config.sample_rate = 48000;
    }
    if (config.period_frames == 0) {
        config.period_frames = 128;
    }

    if (config.model_path.empty()) {
        model.store(std::shared_ptr<NeuralModel>{}, std::memory_order_release);
        model_path.clear();
    } else {
        auto loaded = std::make_shared<NeuralModel>();
        std::string model_error;
        if (!loaded->load(config.model_path, &model_error)) {
            set_error(error, model_error.empty() ? "failed to load model: " + config.model_path
                                                 : model_error);
            release();
            return false;
        }
        model.store(std::move(loaded), std::memory_order_release);
        model_path = config.model_path;
    }

    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        set_error(error, "failed to initialize the audio context");
        release();
        return false;
    }
    context_ready = true;

    bridge = std::make_unique<InterleavedBridge>(config.channels);
    processor = std::make_unique<GainProcessor>(config.sample_rate, config.channels);
    processor->set_gain(config.gain);
    input_meter = std::make_unique<LevelMeter>(static_cast<float>(config.sample_rate));
    gate = std::make_unique<NoiseGate>(static_cast<float>(config.sample_rate),
                                       config.gate_threshold_db);
    gate->set_enabled(config.gate_enabled);
    requested_gate_enabled.store(config.gate_enabled, std::memory_order_relaxed);
    requested_gate_threshold_db.store(gate->threshold_db(), std::memory_order_relaxed);
    mono_scratch.assign(config.period_frames, 0.0f);

    const ma_device_id* input_id = nullptr;
    if (!resolve_device_id(ma_device_type_capture, config.input_device, &input_id)) {
        set_error(error, "input device not found: " + config.input_device);
        release();
        return false;
    }
    const ma_device_id* output_id = nullptr;
    if (!resolve_device_id(ma_device_type_playback, config.output_device, &output_id)) {
        set_error(error, "output device not found: " + config.output_device);
        release();
        return false;
    }

    ma_device_config capture_config = ma_device_config_init(ma_device_type_capture);
    capture_config.capture.pDeviceID = input_id;
    capture_config.capture.format = ma_format_f32;
    capture_config.capture.channels = config.channels;
    capture_config.sampleRate = config.sample_rate;
    capture_config.periodSizeInFrames = config.period_frames;
    capture_config.dataCallback = capture_callback;
    capture_config.pUserData = this;
    if (ma_device_init(&context, &capture_config, &capture) != MA_SUCCESS) {
        set_error(error, "failed to initialize the input device");
        release();
        return false;
    }
    capture_ready = true;

    ma_device_config playback_config = ma_device_config_init(ma_device_type_playback);
    playback_config.playback.pDeviceID = output_id;
    playback_config.playback.format = ma_format_f32;
    playback_config.playback.channels = config.channels;
    playback_config.sampleRate = config.sample_rate;
    playback_config.periodSizeInFrames = config.period_frames;
    playback_config.dataCallback = playback_callback;
    playback_config.pUserData = this;
    if (ma_device_init(&context, &playback_config, &playback) != MA_SUCCESS) {
        set_error(error, "failed to initialize the output device");
        release();
        return false;
    }
    playback_ready = true;

    actual_sample_rate = capture.sampleRate != 0 ? capture.sampleRate : config.sample_rate;
    actual_period_frames = capture.capture.internalPeriodSizeInFrames != 0
                               ? capture.capture.internalPeriodSizeInFrames
                               : config.period_frames;
    requested_gain.store(processor->gain(), std::memory_order_relaxed);

    if (ma_device_start(&capture) != MA_SUCCESS) {
        set_error(error, "failed to start the input device");
        release();
        return false;
    }
    if (ma_device_start(&playback) != MA_SUCCESS) {
        set_error(error, "failed to start the output device");
        release();
        return false;
    }

    running.store(true, std::memory_order_relaxed);
    return true;
}

void Engine::Impl::release() {
    if (capture_ready) {
        ma_device_uninit(&capture);
        capture_ready = false;
    }
    if (playback_ready) {
        ma_device_uninit(&playback);
        playback_ready = false;
    }
    if (context_ready) {
        ma_context_uninit(&context);
        context_ready = false;
    }

    bridge.reset();
    processor.reset();
    input_meter.reset();
    gate.reset();
    mono_scratch.clear();
    running.store(false, std::memory_order_relaxed);
    actual_period_frames = 0;
}

Engine::Engine() : impl_(std::make_unique<Impl>()) {}

Engine::~Engine() {
    stop();
}

bool Engine::start(const EngineConfig& config, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    return impl_->start(config, error);
}

void Engine::stop() {
    impl_->release();
}

bool Engine::running() const {
    return impl_->running.load(std::memory_order_relaxed);
}

void Engine::set_gain(float gain) {
    if (impl_->processor != nullptr) {
        impl_->processor->set_gain(gain);
        impl_->requested_gain.store(impl_->processor->gain(), std::memory_order_relaxed);
    } else {
        impl_->requested_gain.store(std::clamp(gain, 0.0f, 4.0f), std::memory_order_relaxed);
    }
}

float Engine::gain() const {
    if (impl_->processor != nullptr) {
        return impl_->processor->gain();
    }
    return impl_->requested_gain.load(std::memory_order_relaxed);
}

bool Engine::load_model(const std::string& path, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (path.empty()) {
        impl_->model.store(std::shared_ptr<NeuralModel>{}, std::memory_order_release);
        impl_->model_path.clear();
        return true;
    }

    auto loaded = std::make_shared<NeuralModel>();
    std::string model_error;
    if (!loaded->load(path, &model_error)) {
        if (error != nullptr) {
            *error = model_error.empty() ? "failed to load model: " + path : model_error;
        }
        return false;
    }
    impl_->model.store(std::move(loaded), std::memory_order_release);
    impl_->model_path = path;
    return true;
}

bool Engine::model_loaded() const {
    const std::shared_ptr<NeuralModel> model = impl_->model.load(std::memory_order_acquire);
    return model != nullptr;
}

std::string Engine::model_path() const {
    return impl_->model_path;
}

void Engine::set_gate_enabled(bool enabled) {
    impl_->requested_gate_enabled.store(enabled, std::memory_order_relaxed);
    if (impl_->gate != nullptr) {
        impl_->gate->set_enabled(enabled);
    }
}

bool Engine::gate_enabled() const {
    if (impl_->gate != nullptr) {
        return impl_->gate->enabled();
    }
    return impl_->requested_gate_enabled.load(std::memory_order_relaxed);
}

void Engine::set_gate_threshold_db(float db) {
    const float clamped = std::clamp(db, -96.0f, 0.0f);
    impl_->requested_gate_threshold_db.store(clamped, std::memory_order_relaxed);
    if (impl_->gate != nullptr) {
        impl_->gate->set_threshold_db(clamped);
    }
}

float Engine::gate_threshold_db() const {
    if (impl_->gate != nullptr) {
        return impl_->gate->threshold_db();
    }
    return impl_->requested_gate_threshold_db.load(std::memory_order_relaxed);
}

float Engine::input_peak_db() const {
    return impl_->input_meter != nullptr ? impl_->input_meter->peak_db() : -120.0f;
}

float Engine::output_peak_db() const {
    return impl_->processor != nullptr ? impl_->processor->meter().peak_db() : -120.0f;
}

double Engine::latency_ms() const {
    if (!impl_->running.load(std::memory_order_relaxed)) {
        return 0.0;
    }
    const double capture_latency = side_latency_ms(
        impl_->capture.capture.internalPeriodSizeInFrames, impl_->capture.capture.internalPeriods,
        impl_->capture.capture.internalSampleRate, impl_->actual_sample_rate);
    const double playback_latency =
        side_latency_ms(impl_->playback.playback.internalPeriodSizeInFrames,
                        impl_->playback.playback.internalPeriods,
                        impl_->playback.playback.internalSampleRate, impl_->actual_sample_rate);
    return capture_latency + playback_latency;
}

std::uint32_t Engine::sample_rate() const {
    return impl_->actual_sample_rate;
}

std::uint32_t Engine::period_frames() const {
    return impl_->actual_period_frames;
}

std::uint64_t Engine::overrun_frames() const {
    return impl_->bridge != nullptr ? impl_->bridge->overrun_frames() : 0;
}

std::uint64_t Engine::underrun_frames() const {
    return impl_->bridge != nullptr ? impl_->bridge->underrun_frames() : 0;
}

}  // namespace gitar
