#include "TiltFilter.h"

#include <algorithm>
#include <cmath>

//==============================================================================
// Lifecycle
//==============================================================================

void TiltFilter::prepare(double newSampleRate) {
    jassert(newSampleRate > 0.0);

    // Guard against the (degenerate but possible) call with a non-positive
    // sample rate. Fall back to 48 kHz so updateCoefficients() still
    // produces something finite, but mark the filter prepared only when we
    // were handed a sensible rate so the host can't make it silently wrong.
    sampleRate = (std::isfinite(newSampleRate) && newSampleRate > 0.0)
                     ? newSampleRate
                     : 48000.0;

    prepared = std::isfinite(newSampleRate) && newSampleRate > 0.0;
    updateCoefficients();
    reset();
}

void TiltFilter::prepare(const juce::dsp::ProcessSpec &spec) {
    prepare(spec.sampleRate);
}

void TiltFilter::reset() noexcept {
    x1 = 0.0f;
    y1 = 0.0f;
}

//==============================================================================
// Control
//==============================================================================

void TiltFilter::setVoiceIndex(int newVoiceIndex) noexcept {
    jassert(newVoiceIndex >= 0 && newVoiceIndex < numVoices);

    const int clamped = juce::jlimit(0, numVoices - 1, newVoiceIndex);
    if (clamped == voiceIndex)
        return;

    voiceIndex = clamped;
    tiltDb = voiceTiltDb[static_cast<size_t>(clamped)];

    if (prepared)
        updateCoefficients();
}

void TiltFilter::setTiltDb(float newTiltDb) noexcept {
    jassert(std::isfinite(newTiltDb));

    if (!std::isfinite(newTiltDb))
        return;

    if (newTiltDb == tiltDb)
        return;

    tiltDb = newTiltDb;

    if (prepared)
        updateCoefficients();
}

void TiltFilter::setPivotHz(float newPivotHz) noexcept {
    jassert(std::isfinite(newPivotHz) && newPivotHz > 0.0f);

    if (!std::isfinite(newPivotHz) || newPivotHz <= 0.0f)
        return;

    if (newPivotHz == pivotHz)
        return;

    pivotHz = newPivotHz;

    if (prepared)
        updateCoefficients();
}

//==============================================================================
// Audio
//==============================================================================

float TiltFilter::processSample(float x) noexcept {
    if (!prepared) {
        // Keep the state consistent with the input even when unprepared,
        // so a later prepare() starts from a sane baseline.
        x1 = x;
        return x;
    }

    const float y = b0 * x + b1 * x1 - a1 * y1;
    x1 = x;
    y1 = y;
    return y;
}

void TiltFilter::process(float *data, int numSamples) noexcept {
    jassert(numSamples >= 0);
    jassert(numSamples == 0 || data != nullptr);

    if (data == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}

//==============================================================================
// Coefficient update (Vorbis-style first-order tilt + BLT prewarping)
//==============================================================================

void TiltFilter::updateCoefficients() noexcept {
    // Vorbis-style first-order tilt prototype:
    //     H(s) = (s + A*ω0) / (A*s + ω0)
    // with A = 10^(tiltDb/40) and ω0 = 2π*pivotHz. Properties:
    //     H(0)   = A        ≈ +tiltDb/2 dB (DC)
    //     H(∞)   = 1/A      ≈ -tiltDb/2 dB (Nyquist)
    //     H(ω0)  = 1         (pivot, flat)
    // The prewarped BLT substitutes s = (2/T) * (1 - z^-1) / (1 + z^-1)
    // and uses K = tan(π * pivotHz / fs) so the digital pivot lands at
    // exactly pivotHz regardless of sample rate.

    const double fs = (sampleRate > 0.0) ? sampleRate : 48000.0;

    // Keep a small margin below Nyquist so K = tan(...) stays finite
    // even for user-set pivots that get close to the edge.
    const double maxPivot = fs * 0.49;
    const double requestedPivot =
        (std::isfinite(pivotHz) && pivotHz > 0.0f)
            ? static_cast<double>(pivotHz)
            : static_cast<double>(defaultPivotHz);
    const double safePivot = juce::jlimit(1.0, maxPivot, requestedPivot);

    const double phase = juce::MathConstants<double>::pi * safePivot / fs;
    const double K = std::tan(phase);

    const double A = std::pow(10.0, tiltDb / 40.0);
    const double denom = A + K;

    // From the BLT substitution, the digital coefficients are:
    //     b0 = (1 + A*K) / (A + K)
    //     b1 = (A*K - 1) / (A + K)
    //     a1 = (K - A) / (A + K)
    // Direct Form I: y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1].
    b0 = static_cast<float>((1.0 + A * K) / denom);
    b1 = static_cast<float>((A * K - 1.0) / denom);
    a1 = static_cast<float>((K - A) / denom);
}
