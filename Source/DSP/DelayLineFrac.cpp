#include "DelayLineFrac.h"

#include <algorithm>
#include <cmath>

//==============================================================================
void DelayLineFrac::prepare(double sampleRateIn, int maxBlockSize) {
    juce::ignoreUnused(maxBlockSize);
    jassert(sampleRateIn > 0.0);

    sampleRate = sampleRateIn;

    // §8 sizing: ceil(50 ms * sampleRate) + Lagrange guard (≥ 4).
    // The host's true maximum sample rate may not be known at prepare
    // time, so we size for the prepare sample rate; the engine (later
    // phase) is expected to re-prepare if the rate changes.
    const auto requestedCapacity =
        static_cast<int>(std::ceil(0.05 * sampleRate)) + lagrangeGuard;
    bufferSize = juce::jmax(requestedCapacity, lagrangeGuard + 2);

    buffer.allocate(static_cast<size_t>(bufferSize), true);
    writePos = 0;
    prepared = true;

    // The maximum allowed delay leaves headroom for the four Lagrange taps
    // (i-1, i, i+1, i+2). With bufferSize = N and maxDelay = N - guard,
    // the i+2 tap is at most 2 samples past the integer read position
    // and therefore always inside the pre-allocated buffer.
    maxDelaySamples = static_cast<float>(bufferSize - lagrangeGuard);

    // Initialize the smoothed read distance at the minimum legal value so
    // the first samples are valid even before the engine calls setDelayMs.
    targetDelaySamples = minDelaySamples;
    currentDelaySamples = minDelaySamples;

    delaySmoothed.reset(sampleRate, rampSeconds);
    delaySmoothed.setCurrentAndTargetValue(targetDelaySamples);
}

void DelayLineFrac::prepare(const juce::dsp::ProcessSpec &spec) noexcept {
    prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
}

void DelayLineFrac::reset() noexcept {
    if (!prepared)
        return;

    // Zero the circular storage so a tail of stale audio does not bleed
    // into the new run. No allocation, no syscalls.
    std::fill(buffer.getData(), buffer.getData() + bufferSize, 0.0f);
    writePos = 0;

    // Snap the smoothed read distance to the current target so a reset
    // mid-ramp does not produce a discontinuity on the next processSample.
    delaySmoothed.setCurrentAndTargetValue(targetDelaySamples);
    currentDelaySamples = targetDelaySamples;
}

void DelayLineFrac::setDelayMs(float ms) noexcept {
    if (!prepared)
        return;

    const float requestedSamples = static_cast<float>(
        static_cast<double>(ms) * 0.001 * sampleRate);
    targetDelaySamples =
        juce::jlimit(minDelaySamples, maxDelaySamples, requestedSamples);
    delaySmoothed.setTargetValue(targetDelaySamples);
}

float DelayLineFrac::processSample(float x) noexcept {
    jassert(prepared);

    // D9: only the *read distance* is ramped. The write pointer advances
    // unconditionally at the audio sample rate.
    currentDelaySamples = delaySmoothed.getNextValue();

    // Write the new sample at the current write head.
    buffer[writePos] = x;

    // Read with 3rd-order (4-point) Lagrange interpolation, §4.3.
    const float output = readLagrange3rd(currentDelaySamples);

    // Advance the write head with a branch on the rare wrap.
    if (++writePos >= bufferSize)
        writePos = 0;

    return output;
}

void DelayLineFrac::process(float *data, int n) noexcept {
    jassert(prepared);
    jassert(data != nullptr || n == 0);

    for (int i = 0; i < n; ++i)
        data[i] = processSample(data[i]);
}

//==============================================================================
int DelayLineFrac::wrapIndex(int i) const noexcept {
    // Branchless wrap into [0, bufferSize). All four Lagrange taps pass
    // through here so the read path is safe at the buffer edges.
    int idx = i % bufferSize;
    if (idx < 0)
        idx += bufferSize;
    return idx;
}

float DelayLineFrac::readLagrange3rd(float delaySamples) const noexcept {
    // The read position is computed in double precision: the write pointer
    // is integer-valued (advances by 1 per sample) but the subtracted
    // delay is a float, and we want a numerically tight floor/fraction
    // split at high sample rates. The Lagrange tap reads and the final
    // output remain single precision.

    const double d = static_cast<double>(delaySamples);
    const double readPos = static_cast<double>(writePos) - d;
    const double iDouble = std::floor(readPos);
    const float f = static_cast<float>(readPos - iDouble);
    const int i = static_cast<int>(iDouble);

    // Four neighbors, each individually wrapped so the interpolation
    // window is safe at the circular-buffer boundary (§4.3).
    const float yM1 = buffer[wrapIndex(i - 1)];
    const float yP0 = buffer[wrapIndex(i)];
    const float yP1 = buffer[wrapIndex(i + 1)];
    const float yP2 = buffer[wrapIndex(i + 2)];

    // 3rd-order (4-point) Lagrange coefficients, §4.3 verbatim.
    const float cM1 = -(1.0f / 6.0f) * f * (f - 1.0f) * (f - 2.0f);
    const float cP0 =  (1.0f / 2.0f) * (f + 1.0f) * (f - 1.0f) * (f - 2.0f);
    const float cP1 = -(1.0f / 2.0f) * (f + 1.0f) * f * (f - 2.0f);
    const float cP2 =  (1.0f / 6.0f) * (f + 1.0f) * f * (f - 1.0f);

    return cM1 * yM1 + cP0 * yP0 + cP1 * yP1 + cP2 * yP2;
}
