#include <algorithm>
#include <cstddef>
#include <vector>

#include "doctest/doctest.h"
#include "gitar/metronome.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;

std::vector<float> render(gitar::Metronome& metronome, std::size_t frames,
                          std::size_t chunk = 256) {
    std::vector<float> out(frames, 0.0f);
    std::size_t offset = 0;
    while (offset < frames) {
        const std::size_t count = std::min(chunk, frames - offset);
        metronome.process(out.data() + offset, count);
        offset += count;
    }
    return out;
}

double block_energy(const std::vector<float>& signal, std::size_t start, std::size_t count) {
    double energy = 0.0;
    for (std::size_t i = start; i < start + count && i < signal.size(); ++i) {
        energy += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    }
    return energy;
}

// Clicks are detected as contiguous groups of 5 ms blocks whose energy is a
// quarter of the loudest block; the onset is the first block of each group.
std::vector<std::size_t> click_onsets(const std::vector<float>& signal) {
    constexpr std::size_t kBlock = 240;  // 5 ms at 48 kHz
    std::vector<double> energies;
    for (std::size_t start = 0; start + kBlock <= signal.size(); start += kBlock) {
        energies.push_back(block_energy(signal, start, kBlock));
    }
    const double peak =
        energies.empty() ? 0.0 : *std::max_element(energies.begin(), energies.end());

    std::vector<std::size_t> onsets;
    bool in_burst = false;
    for (std::size_t block = 0; block < energies.size(); ++block) {
        const bool loud = peak > 0.0 && energies[block] > peak * 0.25;
        if (loud && !in_burst) {
            onsets.push_back(block * kBlock);
        }
        in_burst = loud;
    }
    return onsets;
}

}  // namespace

TEST_CASE("a disabled metronome leaves the signal untouched") {
    gitar::Metronome metronome(kSampleRate, 120.0f);
    std::vector<float> signal(2048, 0.25f);
    const std::vector<float> original = signal;

    metronome.process(signal.data(), signal.size());

    CHECK(signal == original);
}

TEST_CASE("enabled at 120 BPM clicks every half second") {
    gitar::Metronome metronome(kSampleRate, 120.0f);
    metronome.set_enabled(true);

    const std::vector<float> signal = render(metronome, 24000 * 4);
    const std::vector<std::size_t> onsets = click_onsets(signal);

    REQUIRE(onsets.size() == 4);
    CHECK(onsets.front() == 0);
    for (std::size_t i = 1; i < onsets.size(); ++i) {
        CHECK(onsets[i] - onsets[i - 1] == 24000);
    }
}

TEST_CASE("the bpm is clamped to the supported range") {
    gitar::Metronome metronome(kSampleRate);

    metronome.set_bpm(1.0f);
    CHECK(metronome.bpm() == doctest::Approx(20.0f));

    metronome.set_bpm(1000.0f);
    CHECK(metronome.bpm() == doctest::Approx(400.0f));

    metronome.set_bpm(-5.0f);
    CHECK(metronome.bpm() == doctest::Approx(20.0f));
}

TEST_CASE("reset restarts the beat") {
    gitar::Metronome metronome(kSampleRate, 120.0f);
    metronome.set_enabled(true);

    std::vector<float> first(2000, 0.0f);
    metronome.process(first.data(), first.size());
    CHECK(block_energy(first, 0, 240) > 0.0);

    std::vector<float> middle(2000, 0.0f);
    metronome.process(middle.data(), middle.size());
    CHECK(block_energy(middle, 0, 240) == doctest::Approx(0.0));

    metronome.reset();
    std::vector<float> after(2000, 0.0f);
    metronome.process(after.data(), after.size());
    CHECK(block_energy(after, 0, 240) > 0.0);
}
