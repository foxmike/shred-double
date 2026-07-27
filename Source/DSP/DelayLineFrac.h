#pragma once

#include <juce_dsp/juce_dsp.h>

/**
    Single-channel fractional-sample delay line with 3rd-order (4-point)
    Lagrange interpolation (ShredWidth_Build_Spec §4.3) and a smoothed read
    distance (D9: 50 ms linear ramp).

    One instance is used per shifted channel by the ShredWidthEngine for the
    Haas delay offset (§4.4). This class is a passive DSP primitive; the
    engine (in a later phase) is responsible for owning instances, calling
    prepare() once during prepareToPlay, and routing audio through
    processSample() / process().

    Allocation policy
    -----------------
    prepare() pre-allocates the internal storage to hold at least
        ceil(0.05 * sampleRate) + 4
    floats (50 ms of delay plus a Lagrange-guard of 4 samples; see §8). The
    pre-allocate sample rate is the value passed to prepare(). If the host
    later changes the rate, the engine must re-prepare. No allocation
    happens outside prepare(); processSample() and process() are
    heap-free, lock-free, and log-free and are safe to call from the audio
    thread.

    Read-distance smoothing (D9)
    ---------------------------
    setDelayMs() updates a target read distance; the actual read distance
    advances linearly to the target over 50 ms (set in prepare), so live
    parameter automation does not cause audible pitch/glitch artifacts
    (D9). The write pointer advances at the audio sample rate; only the
    read distance is ramped — the write pointer is not (D9).

    Clamping
    --------
    setDelayMs() clamps the requested delay to the valid range
    [1 sample, bufferSize - 4] (the upper bound leaves headroom for the
    3rd-order Lagrange taps at i-1, i, i+1, i+2).
*/
class DelayLineFrac {
public:
    //==========================================================================
    // Lifecycle
    //==========================================================================

    /** Allocates the buffer and configures the smoothing ramp.
        Allocates ceil(0.05 * sampleRate) + 4 floats and configures a
        50 ms linear ramp on the read distance. Must be called from the
        message thread before audio starts. sampleRate must be > 0;
        maxBlockSize is accepted for API symmetry with juce::dsp::ProcessSpec
        but is not used for sizing. */
    void prepare(double sampleRate, int maxBlockSize);

    /** juce::dsp::ProcessSpec overload; delegates to the (double, int) form. */
    void prepare(const juce::dsp::ProcessSpec &spec) noexcept;

    /** Clears the buffer to silence and snaps the smoothed delay to its
        current target with no ramp. Safe on the audio thread (no allocation). */
    void reset() noexcept;

    //==========================================================================
    // Control
    //==========================================================================

    /** Sets the target delay in milliseconds. The read distance is ramped
        to the new target over the smoothing duration configured in prepare
        (50 ms; D9). Out-of-range values are clamped. Safe on the audio thread. */
    void setDelayMs(float ms) noexcept;

    //==========================================================================
    // Audio
    //==========================================================================

    /** Pushes one sample and returns the Lagrange-interpolated delayed
        sample. Real-time safe. */
    float processSample(float x) noexcept;

    /** In-place block processing. data[0..n) is overwritten with the
        delayed signal. Real-time safe. */
    void process(float *data, int n) noexcept;

    //==========================================================================
    // Introspection
    //==========================================================================

    /** Returns the current storage size in samples. */
    int getBufferSize() const noexcept { return bufferSize; }

    /** Returns the current target delay in samples (last set by setDelayMs,
        after clamping). */
    float getTargetDelaySamples() const noexcept { return targetDelaySamples; }

    /** Returns the most recently produced smoothed read distance in samples. */
    float getCurrentDelaySamples() const noexcept { return currentDelaySamples; }

    /** Returns true after a successful prepare() call. */
    bool isPrepared() const noexcept { return prepared; }

private:
    //==========================================================================
    // Constants
    //==========================================================================

    /** Lagrange-guard: extra samples past the 50 ms capacity so the four
        Lagrange taps at i-1, i, i+1, i+2 stay safely within the buffer
        when the read distance is at its maximum. */
    static constexpr int lagrangeGuard = 4;

    /** Lower clamp on the read distance (in samples). 1 sample keeps the
        interpolation strictly in the past of the write head. */
    static constexpr float minDelaySamples = 1.0f;

    /** Smoothing ramp duration in seconds (D9: ≥ 50 ms). */
    static constexpr double rampSeconds = 0.05;

    //==========================================================================
    // State
    //==========================================================================

    juce::HeapBlock<float> buffer;
    int bufferSize = 0;
    int writePos = 0;

    double sampleRate = 0.0;
    float targetDelaySamples = minDelaySamples;
    float currentDelaySamples = minDelaySamples;
    float maxDelaySamples = 0.0f;
    bool prepared = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySmoothed;

    //==========================================================================
    // Helpers
    //==========================================================================

    int wrapIndex(int i) const noexcept;

    float readLagrange3rd(float delaySamples) const noexcept;
};
