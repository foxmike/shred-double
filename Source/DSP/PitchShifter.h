#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

/**
    Single-channel dual-head granular pitch shifter.

    One instance processes one detune polarity. The active circular-buffer
    length is 2048 samples in Studio mode or 512 samples in Live mode, while
    prepare() always allocates the Studio maximum plus four guard samples.
    The nominal delay exposed for later engine integration is L / 2 samples.
*/
class PitchShifter {
public:
    enum class LatencyMode {
        studio,
        live
    };

    /** Allocates all storage and configures the 20 ms detune ramp. */
    void prepare(double sampleRate, int maxBlockSize);

    /** Clears buffered audio and restores both read heads to their initial
        positions without allocating. */
    void reset() noexcept;

    /** Selects Studio (L=2048) or Live (L=512) without reallocating. */
    void setLatencyMode(LatencyMode mode) noexcept;

    /** Sets the active ring length. Only 2048 and 512 are valid. */
    void setBufferLength(int newLength) noexcept;

    /** Sets the target signed detune in cents. It is ramped over 20 ms. */
    void setDetuneCents(float deltaC) noexcept;

    /** Processes one sample. Real-time safe and O(1). */
    float processSample(float x) noexcept;

    /** In-place block convenience wrapper. */
    void process(float *data, int numSamples) noexcept;

    int getBufferLength() const noexcept { return bufferLength; }
    int getNominalDelaySamples() const noexcept { return bufferLength / 2; }
    bool isPrepared() const noexcept { return prepared; }

private:
    static constexpr int studioLength = 2048;
    static constexpr int liveLength = 512;
    static constexpr int lagrangeGuard = 4;
    static constexpr double detuneRampSeconds = 0.02;
    static constexpr double twoPi =
        6.283185307179586476925286766559;

    juce::HeapBlock<float> buffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        detuneSmoothed;

    int bufferLength = studioLength;
    int writePosition = 0;
    double tau = 0.0;
    float targetDetuneCents = 0.0f;
    bool prepared = false;

    int wrapIndex(int index) const noexcept;
    float readLagrange(double position) const noexcept;
};
