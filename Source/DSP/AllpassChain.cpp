#include "AllpassChain.h"

#include <algorithm>
#include <cmath>
#include <numeric>

//==============================================================================
void AllpassChain::setVoiceIndex(int newVoiceIndex) noexcept {
    jassert(newVoiceIndex >= 0 && newVoiceIndex < numVoices);
    const int clampedIndex = std::clamp(newVoiceIndex, 0, numVoices - 1);

    if (voiceIndex == clampedIndex)
        return;

    voiceIndex = clampedIndex;

    if (prepared) {
        applySelectedDelays();
        reset();
    }
}

void AllpassChain::prepare(double sampleRate, int maxBlockSize) {
    juce::ignoreUnused(maxBlockSize);
    jassert(sampleRate > 0.0);

    currentSampleRate =
        std::isfinite(sampleRate) && sampleRate > 0.0
            ? sampleRate
            : referenceSampleRate;

    // Size every stage for the largest adjusted delay from every voice.
    // Consequently setVoiceIndex() remains allocation-free after prepare().
    int maximumDelay = 1;
    for (int voice = 0; voice < numVoices; ++voice) {
        const auto delays = makeScaledDelays(voice, currentSampleRate);
        for (const int delay : delays)
            maximumDelay = std::max(maximumDelay, delay);
    }

    for (auto &stage : stages) {
        stage.capacity = maximumDelay;
        stage.buffer.allocate(static_cast<size_t>(stage.capacity), true);
        stage.writePosition = 0;
    }

    prepared = true;
    applySelectedDelays();
}

void AllpassChain::prepare(const juce::dsp::ProcessSpec &spec) {
    prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
}

void AllpassChain::reset() noexcept {
    if (!prepared)
        return;

    for (auto &stage : stages) {
        std::fill(stage.buffer.getData(),
                  stage.buffer.getData() + stage.capacity,
                  0.0f);
        stage.writePosition = 0;
    }
}

//==============================================================================
float AllpassChain::processSample(float x) noexcept {
    jassert(prepared);
    if (!prepared)
        return x;

    for (auto &stage : stages)
        x = processStage(stage, x);

    return x;
}

void AllpassChain::process(float *data, int numSamples) noexcept {
    jassert(data != nullptr || numSamples == 0);
    jassert(numSamples >= 0);

    if (data == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}

int AllpassChain::getStageDelaySamples(int stageIndex) const noexcept {
    jassert(stageIndex >= 0 && stageIndex < numStages);
    if (stageIndex < 0 || stageIndex >= numStages)
        return 0;

    return stages[static_cast<size_t>(stageIndex)].delaySamples;
}

//==============================================================================
AllpassChain::DelaySet
AllpassChain::makeScaledDelays(int voice, double sampleRate) noexcept {
    DelaySet result{};
    const double scale = sampleRate / referenceSampleRate;

    for (int stage = 0; stage < numStages; ++stage) {
        int candidate = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(
                    referenceDelays[static_cast<size_t>(voice)]
                                   [static_cast<size_t>(stage)])
                * scale)));

        // Rounding at non-reference rates can introduce common factors or,
        // at very low rates, duplicate lengths. Move only upward, by the
        // smallest amount that restores pairwise coprimality.
        bool compatible = false;
        while (!compatible) {
            compatible = true;
            for (int previous = 0; previous < stage; ++previous) {
                const int previousDelay =
                    result[static_cast<size_t>(previous)];
                if (candidate == previousDelay
                    || std::gcd(candidate, previousDelay) != 1) {
                    compatible = false;
                    ++candidate;
                    break;
                }
            }
        }

        result[static_cast<size_t>(stage)] = candidate;
    }

    return result;
}

void AllpassChain::applySelectedDelays() noexcept {
    const auto delays = makeScaledDelays(voiceIndex, currentSampleRate);

    for (int stage = 0; stage < numStages; ++stage) {
        auto &state = stages[static_cast<size_t>(stage)];
        state.delaySamples = delays[static_cast<size_t>(stage)];
        jassert(state.delaySamples <= state.capacity);
        state.writePosition = 0;
    }
}

float AllpassChain::processStage(Stage &stage, float x) noexcept {
    // The stored value is x(n) + g*y(n). Reading it M samples later gives
    // x(n-M) + g*y(n-M), exactly the two delayed terms in the spec equation.
    const float delayed = stage.buffer[stage.writePosition];
    const float y = delayed - gain * x;
    stage.buffer[stage.writePosition] = x + gain * y;

    if (++stage.writePosition >= stage.delaySamples)
        stage.writePosition = 0;

    return y;
}
