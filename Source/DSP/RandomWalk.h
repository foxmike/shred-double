#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cstdint>

/**
    Deterministic, mean-reverting humanization source.

    The OU state advances once per 32 audio samples and mapped control points
    are linearly interpolated at audio rate. All state is POD; prepare(),
    reset(), reseed(), and processSample() allocate nothing.
*/
class RandomWalk {
public:
    enum class Axis {
        delay,
        pitch
    };

    static constexpr std::uint64_t delayAxisTag =
        UINT64_C(0xD31A9E5A7C4B210D);
    static constexpr std::uint64_t pitchAxisTag =
        UINT64_C(0x91C7A5EED42B6F03);
    static constexpr int controlHop = 32;
    static constexpr float maxTimingSpreadMs = 15.0f;
    static constexpr float maxPitchSpreadCents = 10.0f;
    static constexpr float maxPitchCents = 12.0f;

    explicit RandomWalk(Axis axisToUse = Axis::delay) noexcept;

    /** Configures the audio sample rate and starts at the axis mean. */
    void prepare(double sampleRate) noexcept;

    /** ProcessSpec convenience overload; no storage is allocated. */
    void prepare(const juce::dsp::ProcessSpec &spec) noexcept;

    /** Restores the seeded stream and starts at the axis mean. */
    void reset() noexcept;

    /** Selects how the normalized walk is mapped to the output value. */
    void setAxis(Axis newAxis) noexcept;

    /**
        Seeds this stream as
        splitmix64(masterSeed XOR voiceIndex XOR axisTag).
    */
    void seedStream(std::uint64_t masterSeed,
                    std::uint64_t voiceIndex,
                    std::uint64_t axisTag) noexcept;

    /** Alias matching the voice-level integration API. */
    void reseed(std::uint64_t masterSeed,
                std::uint64_t voiceIndex,
                std::uint64_t axisTag) noexcept;

    /** Stateless SplitMix64 mixer used by seedStream(). */
    static std::uint64_t splitmix64(std::uint64_t value) noexcept;

    /** Maps Rate [0, 100] linearly to correlation time [300, 100] ms. */
    void setHumanRate(float ratePercent) noexcept;

    /** Descriptive alias for setHumanRate(). */
    void setHumanizationRate(float ratePercent) noexcept;

    /** Sets correlation time directly, clamped to [100, 300] ms. */
    void setCorrelationTimeMs(float correlationTimeMs) noexcept;

    /** Tightness [0, 1], applied to excursion as (1 - tightness). */
    void setTightness(float tightness01) noexcept;

    void setTimingSpreadMs(float spreadMs) noexcept;
    void setPitchSpreadCents(float spreadCents) noexcept;
    void setNominalDelayMs(float delayMs) noexcept;

    /**
        Returns the current delay in milliseconds or pitch in cents.
        The walk advances at H=32 and this call performs linear interpolation.
    */
    float processSample() noexcept;

    float getLastRawValue() const noexcept { return lastRawValue; }
    float getLastNormalizedValue() const noexcept { return lastRawValue; }
    float getLastControlValue() const noexcept { return nextControlValue; }
    float getLastInterpolatedValue() const noexcept {
        return lastInterpolatedValue;
    }
    float getInterpolated() const noexcept {
        return lastInterpolatedValue;
    }
    float getCorrelationTimeMs() const noexcept {
        return correlationTimeMs;
    }
    Axis getAxis() const noexcept { return axis; }
    bool isPrepared() const noexcept { return prepared; }

private:
    struct Xoshiro256PlusPlus {
        std::uint64_t state[4] {};

        std::uint64_t next() noexcept;
    };

    static constexpr float minDelayMs = 10.0f;
    static constexpr float maxDelayMs = 35.0f;

    Xoshiro256PlusPlus random;
    std::uint64_t initialRandomState[4] {};

    Axis axis = Axis::delay;
    double sampleRate = 0.0;
    float correlationTimeMs = 200.0f;
    float alpha = 0.0f;
    float normalization = 0.0f;
    float ouState = 0.0f;

    float tightness = 0.0f;
    float timingSpreadMs = 0.0f;
    float pitchSpreadCents = 0.0f;
    float nominalDelayMs = 20.0f;

    float previousControlValue = 20.0f;
    float nextControlValue = 20.0f;
    float lastRawValue = 0.0f;
    float lastInterpolatedValue = 20.0f;
    int samplesIntoHop = 0;
    bool prepared = false;

    static std::uint64_t rotateLeft(std::uint64_t value, int bits) noexcept;
    double uniformOpen01() noexcept;
    float normalSample() noexcept;
    float axisMean() const noexcept;
    float mapNormalizedValue(float normalizedValue) const noexcept;
    void updateCoefficient() noexcept;
    void advanceControlPoint() noexcept;
};
