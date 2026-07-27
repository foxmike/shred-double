#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

/**
    First-order spectral tilt shelf for per-voice decorrelation
    (ShredDouble_Build_Spec §4.5B).

    Pairs with the per-voice allpass diffusion chain (§4.5A) to provide
    *spectral* decorrelation on top of the phase decorrelation, so the
    summed image is thick rather than comb-filtered hollow. A single
    first-order shelf sweeps gain in opposite directions at DC and
    Nyquist, pivoting at a chosen frequency, with the same shape whether
    we call it "low-shelf boost / high-shelf cut" or "Vorbis-style tilt"
    — both describe the same transfer.

    Transfer shape
    --------------
    The analog prototype is the Vorbis tilt
        H(s) = (s + A*ω0) / (A*s + ω0)
    with A = 10^(tiltDb/40) and ω0 = 2π*pivotHz. Properties:
        - DC (s = 0):        H(0) = A           ≈ +tiltDb/2 dB
        - Nyquist (s → ∞):   H(∞) = 1/A         ≈ -tiltDb/2 dB
        - Pivot (s = ω0):    H(ω0) = 1          0 dB, flat
    The sum of the DC and Nyquist deviations is zero, so the overall
    loudness is roughly preserved even though the spectrum tilts.

    For a "bright" voice (V1, tiltDb = +1.5) the low band is boosted
    and the high band is cut by ~0.75 dB each, with the spectrum flat
    near 700 Hz. For a "dark" voice (V2, tiltDb = -1.5) the opposite
    happens. V3/V4 use ±0.8 dB so the inner voices tilt less, leaving
    the outer pair with the most contrast.

    Digital form
    ------------
    After the bilinear transform with prewarping at the pivot, the
    digital transfer is first order:
        b0 = (1 + A*K) / (A + K)
        b1 = (A*K - 1) / (A + K)
        a1 = (K - A) / (A + K)
    where K = tan(π * pivotHz / fs), and the difference equation is
        y[n] = b0 * x[n] + b1 * x[n-1] - a1 * y[n-1]
    The prewarping makes the digital pivot land at exactly pivotHz
    regardless of sample rate.

    Real-time safety
    ----------------
    prepare() is the only place coefficients are recomputed from
    scratch; setters update them in place without heap activity. The
    processSample / process / reset / setter entry points do not
    allocate, do not lock, and are safe on the audio thread.
*/
class TiltFilter {
public:
    /** Number of per-voice tilt presets exposed by voiceTiltDb[]. */
    static constexpr int numVoices = 4;

    /** Default pivot frequency in Hz (§4.5B). */
    static constexpr float defaultPivotHz = 700.0f;

    /**
        Per-voice default tilt magnitudes in dB (§4.5B). Picked so the
        outer voices (V1/V2) tilt ±1.5 dB and the inner voices (V3/V4)
        tilt ±0.8 dB; the asymmetry adds spectral variety without making
        any single voice sound obviously EQ'd.
    */
    inline static constexpr std::array<float, numVoices> voiceTiltDb{{
        +1.5f,   // V1 — bright
        -1.5f,   // V2 — dark
        +0.8f,   // V3 — mild bright
        -0.8f,   // V4 — mild dark
    }};

    //==========================================================================
    // Lifecycle
    //==========================================================================

    /** Recomputes the first-order coefficients for the current sample rate,
        pivot, and tilt, then clears the filter state. sampleRate must be
        > 0. Safe to call multiple times; the only allocator is the
        implicit dsp::ProcessSpec prep, which we avoid entirely here. */
    void prepare(double sampleRate);

    /** juce::dsp::ProcessSpec convenience overload. */
    void prepare(const juce::dsp::ProcessSpec &spec);

    /** Clears the IIR state (previous input and output) to zero without
        touching the coefficients. Safe on the audio thread. */
    void reset() noexcept;

    //==========================================================================
    // Control
    //==========================================================================

    /** Selects one of the four voice presets in voiceTiltDb[]. Indices
        outside [0, numVoices) are clamped. When the index changes, the
        matching tilt magnitude is applied and the coefficients are
        recomputed if prepare() has run. No allocation. */
    void setVoiceIndex(int newVoiceIndex) noexcept;

    /** Sets an arbitrary tilt magnitude in dB. Positive tilts the
        response bright (low boost, high cut), negative tilts dark.
        Non-finite values are ignored. Coefficients are recomputed only
        when the value actually changes and only if prepare() has run. */
    void setTiltDb(float newTiltDb) noexcept;

    /** Sets the pivot frequency in Hz. Values at or below zero, and
        non-finite values, are ignored. The pivot is internally clamped
        to a safe margin below Nyquist so the BLT prewarp stays finite.
        Coefficients are recomputed only when the value actually changes
        and only if prepare() has run. */
    void setPivotHz(float newPivotHz) noexcept;

    //==========================================================================
    // Audio
    //==========================================================================

    /** Processes one sample through the first-order tilt shelf. The
        filter state is updated in place. If prepare() has not been
        called, the input is returned unchanged. No allocation. */
    float processSample(float x) noexcept;

    /** In-place block processing. The buffer must be non-null when
        numSamples > 0. */
    void process(float *data, int numSamples) noexcept;

    //==========================================================================
    // Introspection
    //==========================================================================

    /** Currently selected voice index (0..numVoices-1). */
    int getVoiceIndex() const noexcept { return voiceIndex; }

    /** Currently applied tilt magnitude in dB. */
    float getTiltDb() const noexcept { return tiltDb; }

    /** Currently applied pivot frequency in Hz. */
    float getPivotHz() const noexcept { return pivotHz; }

    /** True after prepare() has been called at least once. */
    bool isPrepared() const noexcept { return prepared; }

private:
    double sampleRate = 0.0;
    int voiceIndex = 0;
    float tiltDb = 0.0f;
    float pivotHz = defaultPivotHz;

    // First-order Direct Form I coefficients; b0/b1 feed, a1 feedback.
    // Updated only inside updateCoefficients() and never on the audio
    // thread without a preceding setter call.
    float b0 = 1.0f;
    float b1 = 0.0f;
    float a1 = 0.0f;

    // Filter state.
    float x1 = 0.0f;
    float y1 = 0.0f;

    bool prepared = false;

    /** Recomputes b0, b1, a1 from tiltDb, pivotHz, and sampleRate using
        the Vorbis tilt prototype and BLT prewarping. Clamps the pivot
        to a safe margin below Nyquist so the resulting K = tan(…)
        stays finite even at unusual sample rates. */
    void updateCoefficients() noexcept;
};
