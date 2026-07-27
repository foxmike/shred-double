#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

/**
    Stereo width stage that applies a Mid/Side scale factor to an already
    assembled stereo pair (ShredWidth_Build_Spec §4.7).

    The wet pair is converted to M/S:
        M = (yL + yR) / 2
        S = (yL - yR) / 2
    and reconstructed with a width factor w in [0, 2]:
        yL' = M + w * S
        yR' = M - w * S

    Why this preserves mono lows
    ----------------------------
    The MonoSafetyCrossover feeds a *shared* lowpass into both channels, so
    the low band contributes the same value `lp` to yL and yR. Substituting:
        S = ( (lp + hpL) - (lp + hpR) ) / 2 = (hpL - hpR) / 2
    The `lp` term cancels exactly, so the low band lives entirely in M and
    scaling S cannot move the lows off-center. This is the §4.7 invariant:
    width = 0 mono, width = 1 identity, width = 2 side-doubled, and at every
    setting 0–200% the lows stay dead-center.

    Width units
    -----------
    setWidth() takes the linear factor in [0, 2] (this is what the unit tests
    use). setWidthPercent() is a convenience that maps the APVTS 0–200% range
    to [0, 2] by dividing by 100. Either path clamps to [0, 2].

    Smoothing
    ---------
    Width is ramped with juce::SmoothedValue<float, Linear> over 20 ms (§6),
    so live automation never zips. The smoother is the only state that
    allocate() touches; no filter, no crossover, no extra storage — this is
    pure M/S arithmetic.

    Real-time safety
    ----------------
    prepare() configures the smoother and is the only function that may
    allocate (via SmoothedValue::reset). reset(), setWidth(),
    setWidthPercent(), and all process*() functions are heap-free, lock-free,
    log-free, and safe on the audio thread.
*/
class WidthProcessor {
public:
    /** Width range lower bound (mono). */
    static constexpr float minWidth = 0.0f;

    /** Width range upper bound (side doubled). */
    static constexpr float maxWidth = 2.0f;

    /** Unity width (passthrough). */
    static constexpr float unityWidth = 1.0f;

    /** Smoothing ramp duration in seconds (§6). */
    static constexpr double rampSeconds = 0.02;

    //==========================================================================
    // Lifecycle
    //==========================================================================

    /** Configures the width smoother for the given sample rate. Must be
        called from the message thread before audio starts. sampleRate must be
        > 0; maxBlockSize is accepted for API symmetry but unused — the
        smoother's state is sample-accurate regardless of block size. */
    void prepare(double sampleRate, int maxBlockSize);

    /** juce::dsp::ProcessSpec overload; delegates to the (double, int) form. */
    void prepare(const juce::dsp::ProcessSpec &spec) noexcept;

    /** Snaps the smoother to its current target without ramping. Safe on
        the audio thread (no allocation). */
    void reset() noexcept;

    //==========================================================================
    // Control
    //==========================================================================

    /** Sets the target linear width factor. Values are clamped to [0, 2].
        The smoother ramps to the new target over 20 ms (§6). Safe on the
        audio thread. */
    void setWidth(float width) noexcept;

    /** Convenience setter that maps the APVTS percent range [0, 200] to the
        internal factor [0, 2]. Equivalent to setWidth(percent * 0.01f).
        Safe on the audio thread. */
    void setWidthPercent(float percent) noexcept;

    //==========================================================================
    // Audio
    //==========================================================================

    /** Processes one stereo frame in place. Applies M=(L+R)/2, S=(L-R)/2,
        outL = M + w * S, outR = M - w * S with w clamped to [0, 2]. The
        smoothed width advances by one step. */
    void processSample(float inL, float inR, float &outL, float &outR) noexcept;

    /** In-place block processing. inL/inR may alias outL/outR; any pair of
        inputs and outputs may share storage. Width is smoothed per-sample
        across the block. Real-time safe. */
    void process(float *inOutL,
                 float *inOutR,
                 int numSamples) noexcept;

    /** Separate-channel block processing. outL/outR may alias inL/inR. */
    void process(const float *inL,
                 const float *inR,
                 float *outL,
                 float *outR,
                 int numSamples) noexcept;

    //==========================================================================
    // Introspection
    //==========================================================================

    /** Returns the target width factor after clamping (last set value). */
    float getTargetWidth() const noexcept { return targetWidth; }

    /** Returns the most recently produced smoothed width factor. */
    float getCurrentWidth() const noexcept { return currentWidth; }

    /** Returns true after a successful prepare() call. */
    bool isPrepared() const noexcept { return prepared; }

private:
    double sampleRate = 0.0;
    float targetWidth = unityWidth;
    float currentWidth = unityWidth;
    bool prepared = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>
        widthSmoothed;
};
