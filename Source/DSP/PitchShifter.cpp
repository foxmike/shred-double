#include "PitchShifter.h"

#include <algorithm>
#include <cmath>

void PitchShifter::prepare(double sampleRate, int maxBlockSize) {
    juce::ignoreUnused(maxBlockSize);
    jassert(sampleRate > 0.0);

    buffer.allocate(
        static_cast<size_t>(studioLength + lagrangeGuard), true);

    detuneSmoothed.reset(sampleRate, detuneRampSeconds);
    detuneSmoothed.setCurrentAndTargetValue(targetDetuneCents);
    prepared = true;
    reset();
}

void PitchShifter::reset() noexcept {
    if (!prepared)
        return;

    std::fill(buffer.getData(),
              buffer.getData() + studioLength + lagrangeGuard, 0.0f);
    writePosition = 0;
    tau = 0.0;
    detuneSmoothed.setCurrentAndTargetValue(targetDetuneCents);
}

void PitchShifter::setLatencyMode(LatencyMode mode) noexcept {
    setBufferLength(mode == LatencyMode::studio ? studioLength : liveLength);
}

void PitchShifter::setBufferLength(int newLength) noexcept {
    jassert(newLength == studioLength || newLength == liveLength);
    if (newLength != studioLength && newLength != liveLength)
        return;

    if (bufferLength == newLength)
        return;

    bufferLength = newLength;
    writePosition %= bufferLength;

    // Re-anchor both grains for the new ring. The later engine masks this
    // unavoidable discontinuity during a live latency-mode transition.
    tau = 0.0;
}

void PitchShifter::setDetuneCents(float deltaC) noexcept {
    targetDetuneCents = deltaC;

    if (prepared)
        detuneSmoothed.setTargetValue(deltaC);
}

float PitchShifter::processSample(float x) noexcept {
    jassert(prepared);

    buffer[writePosition] = x;

    const double deltaC =
        static_cast<double>(detuneSmoothed.getNextValue());
    const double dv = 1.0 - std::pow(2.0, deltaC / 1200.0);

    // deltaC is a micro-detune, so |dv| < 1 and one correction per side is
    // sufficient. Avoid fmod in this per-sample path.
    tau += dv;
    if (tau >= static_cast<double>(bufferLength))
        tau -= static_cast<double>(bufferLength);
    if (tau < 0.0)
        tau += static_cast<double>(bufferLength);

    double tauB = tau + 0.5 * static_cast<double>(bufferLength);
    if (tauB >= static_cast<double>(bufferLength))
        tauB -= static_cast<double>(bufferLength);

    const float headA =
        readLagrange(static_cast<double>(writePosition) - tau);
    const float headB =
        readLagrange(static_cast<double>(writePosition) - tauB);

    const double phase = twoPi * tau / static_cast<double>(bufferLength);
    const float weightA =
        static_cast<float>(0.5 * (1.0 - std::cos(phase)));

    // tauB is L/2 from tau, so its Hann weight is mathematically 1-weightA.
    // Computing it this way makes the unity-gain sum exact in float.
    const float weightB = 1.0f - weightA;
    const float output = weightA * headA + weightB * headB;

    if (++writePosition >= bufferLength)
        writePosition = 0;

    return output;
}

void PitchShifter::process(float *data, int numSamples) noexcept {
    jassert(data != nullptr || numSamples == 0);

    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}

int PitchShifter::wrapIndex(int index) const noexcept {
    int wrapped = index % bufferLength;
    if (wrapped < 0)
        wrapped += bufferLength;
    return wrapped;
}

float PitchShifter::readLagrange(double position) const noexcept {
    const double integerPosition = std::floor(position);
    const float fraction =
        static_cast<float>(position - integerPosition);
    const int i = static_cast<int>(integerPosition);

    // Every tap is wrapped independently; the interpolation window may
    // straddle either end of the active circular buffer.
    const float yM1 = buffer[wrapIndex(i - 1)];
    const float y0 = buffer[wrapIndex(i)];
    const float y1 = buffer[wrapIndex(i + 1)];
    const float y2 = buffer[wrapIndex(i + 2)];

    const float cM1 = -(1.0f / 6.0f) * fraction
                    * (fraction - 1.0f) * (fraction - 2.0f);
    const float c0 = 0.5f * (fraction + 1.0f)
                   * (fraction - 1.0f) * (fraction - 2.0f);
    const float c1 = -0.5f * (fraction + 1.0f)
                   * fraction * (fraction - 2.0f);
    const float c2 = (1.0f / 6.0f) * (fraction + 1.0f)
                   * fraction * (fraction - 1.0f);

    return cM1 * yM1 + c0 * y0 + c1 * y1 + c2 * y2;
}
