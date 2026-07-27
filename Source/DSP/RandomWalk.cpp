#include "RandomWalk.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double twoPi = 6.283185307179586476925286766559;
constexpr double inverseTwoTo53 = 1.0 / 9007199254740992.0;
constexpr std::uint64_t splitmixIncrement =
    UINT64_C(0x9E3779B97F4A7C15);
} // namespace

RandomWalk::RandomWalk(Axis axisToUse) noexcept
    : axis(axisToUse) {
    seedStream(0, 0, axis == Axis::delay ? delayAxisTag : pitchAxisTag);
}

void RandomWalk::prepare(double sampleRateIn) noexcept {
    jassert(sampleRateIn > 0.0);

    sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;
    prepared = true;
    updateCoefficient();
    reset();
}

void RandomWalk::prepare(const juce::dsp::ProcessSpec &spec) noexcept {
    prepare(spec.sampleRate);
}

void RandomWalk::reset() noexcept {
    for (int i = 0; i < 4; ++i)
        random.state[i] = initialRandomState[i];

    ouState = 0.0f;
    lastRawValue = 0.0f;
    previousControlValue = axisMean();
    nextControlValue = previousControlValue;
    lastInterpolatedValue = previousControlValue;
    samplesIntoHop = 0;
}

void RandomWalk::setAxis(Axis newAxis) noexcept {
    if (axis == newAxis)
        return;

    axis = newAxis;
    reset();
}

void RandomWalk::seedStream(std::uint64_t masterSeed,
                            std::uint64_t voiceIndex,
                            std::uint64_t axisTag) noexcept {
    const std::uint64_t seed =
        splitmix64(masterSeed ^ voiceIndex ^ axisTag);

    // Expand the required mixed stream seed into xoshiro256++'s four words.
    for (int i = 0; i < 4; ++i) {
        initialRandomState[i] =
            splitmix64(seed + static_cast<std::uint64_t>(i)
                                * splitmixIncrement);
    }

    // SplitMix64 can theoretically produce four zero words only with
    // negligible probability, but xoshiro's all-zero state is forbidden.
    if ((initialRandomState[0] | initialRandomState[1]
         | initialRandomState[2] | initialRandomState[3]) == 0) {
        initialRandomState[0] = splitmixIncrement;
    }

    reset();
}

void RandomWalk::reseed(std::uint64_t masterSeed,
                        std::uint64_t voiceIndex,
                        std::uint64_t axisTag) noexcept {
    seedStream(masterSeed, voiceIndex, axisTag);
}

std::uint64_t RandomWalk::splitmix64(std::uint64_t value) noexcept {
    value += splitmixIncrement;
    value = (value ^ (value >> 30))
          * UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27))
          * UINT64_C(0x94D049BB133111EB);
    return value ^ (value >> 31);
}

void RandomWalk::setHumanRate(float ratePercent) noexcept {
    const float rate = std::clamp(ratePercent, 0.0f, 100.0f);
    setCorrelationTimeMs(300.0f - 2.0f * rate);
}

void RandomWalk::setHumanizationRate(float ratePercent) noexcept {
    setHumanRate(ratePercent);
}

void RandomWalk::setCorrelationTimeMs(float correlationTimeMsIn) noexcept {
    correlationTimeMs =
        std::clamp(correlationTimeMsIn, 100.0f, 300.0f);
    updateCoefficient();
}

void RandomWalk::setTightness(float tightness01) noexcept {
    tightness = std::clamp(tightness01, 0.0f, 1.0f);
}

void RandomWalk::setTimingSpreadMs(float spreadMs) noexcept {
    timingSpreadMs =
        std::clamp(spreadMs, 0.0f, maxTimingSpreadMs);
}

void RandomWalk::setPitchSpreadCents(float spreadCents) noexcept {
    pitchSpreadCents =
        std::clamp(spreadCents, 0.0f, maxPitchSpreadCents);
}

void RandomWalk::setNominalDelayMs(float delayMs) noexcept {
    nominalDelayMs = std::clamp(delayMs, minDelayMs, maxDelayMs);
}

float RandomWalk::processSample() noexcept {
    jassert(prepared);

    if (samplesIntoHop == 0)
        advanceControlPoint();

    const float phase =
        static_cast<float>(samplesIntoHop)
        / static_cast<float>(controlHop);
    lastInterpolatedValue =
        previousControlValue
        + phase * (nextControlValue - previousControlValue);

    if (++samplesIntoHop >= controlHop)
        samplesIntoHop = 0;

    return lastInterpolatedValue;
}

std::uint64_t RandomWalk::Xoshiro256PlusPlus::next() noexcept {
    const std::uint64_t result =
        RandomWalk::rotateLeft(state[0] + state[3], 23) + state[0];
    const std::uint64_t temporary = state[1] << 17;

    state[2] ^= state[0];
    state[3] ^= state[1];
    state[1] ^= state[2];
    state[0] ^= state[3];
    state[2] ^= temporary;
    state[3] = RandomWalk::rotateLeft(state[3], 45);

    return result;
}

std::uint64_t RandomWalk::rotateLeft(std::uint64_t value,
                                     int bits) noexcept {
    return (value << bits) | (value >> (64 - bits));
}

double RandomWalk::uniformOpen01() noexcept {
    // Half an LSB offset keeps the Box-Muller logarithm away from zero.
    return (static_cast<double>(random.next() >> 11) + 0.5)
         * inverseTwoTo53;
}

float RandomWalk::normalSample() noexcept {
    const double radius =
        std::sqrt(-2.0 * std::log(uniformOpen01()));
    const double angle = twoPi * uniformOpen01();
    return static_cast<float>(radius * std::cos(angle));
}

float RandomWalk::axisMean() const noexcept {
    return axis == Axis::delay ? nominalDelayMs : 0.0f;
}

float RandomWalk::mapNormalizedValue(float normalizedValue) const noexcept {
    const float depthScale = 1.0f - tightness;

    if (axis == Axis::delay) {
        return std::clamp(
            nominalDelayMs
                + timingSpreadMs * depthScale * normalizedValue,
            minDelayMs,
            maxDelayMs);
    }

    return std::clamp(
        pitchSpreadCents * depthScale * normalizedValue,
        -maxPitchCents,
        maxPitchCents);
}

void RandomWalk::updateCoefficient() noexcept {
    if (sampleRate <= 0.0) {
        alpha = 0.0f;
        normalization = 0.0f;
        return;
    }

    // The walk runs at control rate, so fs here is audio fs / H. Thus
    // alpha = 1 - exp(-1 / (tau_c * fs_control)), equivalently
    // 1 - exp(-H / (tau_c * fs_audio)).
    const double controlSampleRate =
        sampleRate / static_cast<double>(controlHop);
    const double tauSeconds =
        static_cast<double>(correlationTimeMs) * 0.001;
    const double calculatedAlpha =
        1.0 - std::exp(-1.0 / (tauSeconds * controlSampleRate));
    alpha = static_cast<float>(calculatedAlpha);

    const double variance =
        calculatedAlpha / (2.0 - calculatedAlpha);
    normalization = variance > 1.0e-20
        ? static_cast<float>(1.0 / std::sqrt(variance))
        : 0.0f;
}

void RandomWalk::advanceControlPoint() noexcept {
    previousControlValue = nextControlValue;

    const float eta = normalSample();
    ouState = (1.0f - alpha) * ouState + alpha * eta;
    lastRawValue = normalization > 0.0f
        ? ouState * normalization
        : 0.0f;
    nextControlValue = mapNormalizedValue(lastRawValue);
}
