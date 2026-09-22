#include "gitar/engine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gitar/bridge.hpp"
#include "gitar/eq.hpp"
#include "gitar/level_meter.hpp"
#include "gitar/metronome.hpp"
#include "gitar/neural_model.hpp"
#include "gitar/noise_gate.hpp"
#include "gitar/pitch_detector.hpp"
#include "gitar/processor.hpp"
#include "gitar/recorder.hpp"
#include "gitar/spectrum_analyzer.hpp"
#include "miniaudio.h"

namespace gitar {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

float clamp_eq_db(float db) {
    const float finite = std::isfinite(db) ? db : 0.0f;
    return std::clamp(finite, -24.0f, 24.0f);
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
    std::unique_ptr<PitchDetector> pitch_detector;
    std::unique_ptr<SpectrumAnalyzer> spectrum_analyzer;
    std::unique_ptr<NoiseGate> gate;
    std::unique_ptr<ThreeBandEq> eq;
    std::unique_ptr<Metronome> metronome;
    Recorder recorder;

    // The control thread swaps this while the playback callback reads it, so the
    // callback never observes a half-built model. shared_ptr keeps the old model
    // alive until the callback that loaded it has finished.
    std::atomic<std::shared_ptr<NeuralModel>> model;
    std::string model_path;

    // Cabinet IR, loaded through the same neural model machinery (a WAV IR is a
    // linear model). Swapped off the audio thread exactly like the amp model.
    std::atomic<std::shared_ptr<NeuralModel>> cab;
    std::string cab_ir_path;
    std::vector<float> mono_scratch;
    std::vector<float> analysis_scratch;

    std::atomic<bool> running{false};
    std::atomic<float> requested_gain{1.0f};
    std::atomic<bool> requested_gate_enabled{true};
    std::atomic<float> requested_gate_threshold_db{-60.0f};
    std::atomic<float> requested_eq_low_db{0.0f};
    std::atomic<float> requested_eq_mid_db{0.0f};
    std::atomic<float> requested_eq_high_db{0.0f};
    std::atomic<bool> requested_metronome_enabled{false};
    std::atomic<float> requested_metronome_bpm{120.0f};
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
    PitchDetector* const pitch = self->pitch_detector.get();
    SpectrumAnalyzer* const spectrum = self->spectrum_analyzer.get();
    if (pitch != nullptr || spectrum != nullptr) {
        const std::uint32_t channels = self->config.channels;
        const std::size_t capacity = self->analysis_scratch.size();
        if (channels <= 1) {
            if (pitch != nullptr) {
                pitch->process(samples, frame_count);
            }
            if (spectrum != nullptr) {
                spectrum->process(samples, frame_count);
            }
        } else if (capacity > 0) {
            std::size_t offset = 0;
            while (offset < frame_count) {
                const std::size_t chunk = std::min<std::size_t>(capacity, frame_count - offset);
                for (std::size_t i = 0; i < chunk; ++i) {
                    self->analysis_scratch[i] = samples[(offset + i) * channels];
                }
                if (pitch != nullptr) {
                    pitch->process(self->analysis_scratch.data(), chunk);
                }
                if (spectrum != nullptr) {
                    spectrum->process(self->analysis_scratch.data(), chunk);
                }
                offset += chunk;
            }
        }
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
    ThreeBandEq* const eq = self->eq.get();
    const std::shared_ptr<NeuralModel> model = self->model.load(std::memory_order_acquire);
    const std::shared_ptr<NeuralModel> cab = self->cab.load(std::memory_order_acquire);
    if ((gate != nullptr && gate->enabled()) || model != nullptr || cab != nullptr ||
        eq != nullptr) {
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
            if (cab != nullptr) {
                cab->process(mono, mono, static_cast<int>(chunk));
            }
            if (eq != nullptr) {
                eq->process(mono, chunk);
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

    Metronome* const metronome = self->metronome.get();
    if (metronome != nullptr && metronome->enabled()) {
        const std::uint32_t channels = self->config.channels;
        const std::size_t capacity = self->mono_scratch.size();
        std::size_t offset = 0;
        while (offset < frame_count && capacity > 0) {
            const std::size_t chunk = std::min<std::size_t>(capacity, frame_count - offset);
            float* const mono = self->mono_scratch.data();
            std::fill(mono, mono + chunk, 0.0f);
            metronome->process(mono, chunk);
            for (std::size_t i = 0; i < chunk; ++i) {
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    samples[(offset + i) * channels + channel] += mono[i];
                }
            }
            offset += chunk;
        }
    }

    self->recorder.write(samples, frame_count);
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

    if (config.cab_ir_path.empty()) {
        cab.store(std::shared_ptr<NeuralModel>{}, std::memory_order_release);
        cab_ir_path.clear();
    } else {
        auto loaded = std::make_shared<NeuralModel>();
        std::string cab_error;
        if (!loaded->load(config.cab_ir_path, &cab_error)) {
            set_error(error, cab_error.empty() ? "failed to load cabinet IR: " + config.cab_ir_path
                                               : cab_error);
            release();
            return false;
        }
        cab.store(std::move(loaded), std::memory_order_release);
        cab_ir_path = config.cab_ir_path;
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
    pitch_detector = std::make_unique<PitchDetector>(static_cast<float>(config.sample_rate));
    spectrum_analyzer = std::make_unique<SpectrumAnalyzer>(static_cast<float>(config.sample_rate));
    gate = std::make_unique<NoiseGate>(static_cast<float>(config.sample_rate),
                                       config.gate_threshold_db);
    gate->set_enabled(config.gate_enabled);
    requested_gate_enabled.store(config.gate_enabled, std::memory_order_relaxed);
    requested_gate_threshold_db.store(gate->threshold_db(), std::memory_order_relaxed);
    eq = std::make_unique<ThreeBandEq>(static_cast<float>(config.sample_rate));
    eq->set_low_gain_db(config.eq_low_db);
    eq->set_mid_gain_db(config.eq_mid_db);
    eq->set_high_gain_db(config.eq_high_db);
    requested_eq_low_db.store(eq->low_gain_db(), std::memory_order_relaxed);
    requested_eq_mid_db.store(eq->mid_gain_db(), std::memory_order_relaxed);
    requested_eq_high_db.store(eq->high_gain_db(), std::memory_order_relaxed);
    metronome =
        std::make_unique<Metronome>(static_cast<float>(config.sample_rate), config.metronome_bpm);
    metronome->set_enabled(config.metronome_enabled);
    requested_metronome_enabled.store(metronome->enabled(), std::memory_order_relaxed);
    requested_metronome_bpm.store(metronome->bpm(), std::memory_order_relaxed);
    mono_scratch.assign(config.period_frames, 0.0f);
    analysis_scratch.assign(config.period_frames, 0.0f);

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
    pitch_detector.reset();
    spectrum_analyzer.reset();
    gate.reset();
    eq.reset();
    metronome.reset();
    mono_scratch.clear();
    analysis_scratch.clear();
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

bool Engine::load_cab_ir(const std::string& path, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    if (path.empty()) {
        impl_->cab.store(std::shared_ptr<NeuralModel>{}, std::memory_order_release);
        impl_->cab_ir_path.clear();
        return true;
    }

    auto loaded = std::make_shared<NeuralModel>();
    std::string cab_error;
    if (!loaded->load(path, &cab_error)) {
        if (error != nullptr) {
            *error = cab_error.empty() ? "failed to load cabinet IR: " + path : cab_error;
        }
        return false;
    }
    impl_->cab.store(std::move(loaded), std::memory_order_release);
    impl_->cab_ir_path = path;
    return true;
}

bool Engine::cab_ir_loaded() const {
    const std::shared_ptr<NeuralModel> cab = impl_->cab.load(std::memory_order_acquire);
    return cab != nullptr;
}

std::string Engine::cab_ir_path() const {
    return impl_->cab_ir_path;
}

void Engine::set_eq(float low_db, float mid_db, float high_db) {
    impl_->requested_eq_low_db.store(clamp_eq_db(low_db), std::memory_order_relaxed);
    impl_->requested_eq_mid_db.store(clamp_eq_db(mid_db), std::memory_order_relaxed);
    impl_->requested_eq_high_db.store(clamp_eq_db(high_db), std::memory_order_relaxed);
    if (impl_->eq != nullptr) {
        impl_->eq->set_low_gain_db(low_db);
        impl_->eq->set_mid_gain_db(mid_db);
        impl_->eq->set_high_gain_db(high_db);
    }
}

float Engine::eq_low_db() const {
    if (impl_->eq != nullptr) {
        return impl_->eq->low_gain_db();
    }
    return impl_->requested_eq_low_db.load(std::memory_order_relaxed);
}

float Engine::eq_mid_db() const {
    if (impl_->eq != nullptr) {
        return impl_->eq->mid_gain_db();
    }
    return impl_->requested_eq_mid_db.load(std::memory_order_relaxed);
}

float Engine::eq_high_db() const {
    if (impl_->eq != nullptr) {
        return impl_->eq->high_gain_db();
    }
    return impl_->requested_eq_high_db.load(std::memory_order_relaxed);
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

bool Engine::start_recording(const std::string& path, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    const auto rate = static_cast<float>(sample_rate());
    const std::uint32_t channels = impl_->config.channels != 0 ? impl_->config.channels : 1;
    return impl_->recorder.start(path, rate, channels, error);
}

void Engine::stop_recording() {
    impl_->recorder.stop();
}

bool Engine::recording() const {
    return impl_->recorder.recording();
}

std::string Engine::recording_path() const {
    return impl_->recorder.path();
}

std::uint64_t Engine::recorded_frames() const {
    return impl_->recorder.frames_written();
}

std::uint64_t Engine::dropped_record_frames() const {
    return impl_->recorder.frames_dropped();
}

void Engine::set_metronome(bool enabled, float bpm) {
    const float finite = std::isfinite(bpm) ? bpm : 120.0f;
    const float clamped = std::clamp(finite, 20.0f, 400.0f);
    impl_->requested_metronome_enabled.store(enabled, std::memory_order_relaxed);
    impl_->requested_metronome_bpm.store(clamped, std::memory_order_relaxed);
    if (impl_->metronome != nullptr) {
        impl_->metronome->set_enabled(enabled);
        impl_->metronome->set_bpm(clamped);
    }
}

bool Engine::metronome_enabled() const {
    if (impl_->metronome != nullptr) {
        return impl_->metronome->enabled();
    }
    return impl_->requested_metronome_enabled.load(std::memory_order_relaxed);
}

float Engine::metronome_bpm() const {
    if (impl_->metronome != nullptr) {
        return impl_->metronome->bpm();
    }
    return impl_->requested_metronome_bpm.load(std::memory_order_relaxed);
}

float Engine::input_peak_db() const {
    return impl_->input_meter != nullptr ? impl_->input_meter->peak_db() : -120.0f;
}

float Engine::output_peak_db() const {
    return impl_->processor != nullptr ? impl_->processor->meter().peak_db() : -120.0f;
}

float Engine::pitch_hz() const {
    return impl_->pitch_detector != nullptr ? impl_->pitch_detector->pitch_hz() : 0.0f;
}

float Engine::pitch_confidence() const {
    return impl_->pitch_detector != nullptr ? impl_->pitch_detector->confidence() : 0.0f;
}

std::array<float, SpectrumAnalyzer::kBandCount> Engine::spectrum_db() const {
    if (impl_->spectrum_analyzer != nullptr) {
        return impl_->spectrum_analyzer->bands_db();
    }
    std::array<float, SpectrumAnalyzer::kBandCount> silent{};
    silent.fill(-120.0f);
    return silent;
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
