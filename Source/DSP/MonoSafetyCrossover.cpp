#include "MonoSafetyCrossover.h"

#include <cmath>

//==============================================================================
void MonoSafetyCrossover::Cascade::setCoefficients(
    const CoefficientsPtr &newCoefficients) noexcept {
    for (auto &section : sections)
        section.coefficients = newCoefficients;
}

void MonoSafetyCrossover::Cascade::prepare(
    const juce::dsp::ProcessSpec &spec) noexcept {
    for (auto &section : sections)
        section.prepare(spec);
}

void MonoSafetyCrossover::Cascade::reset() noexcept {
    for (auto &section : sections)
        section.reset();
}

float MonoSafetyCrossover::Cascade::process(float sample) noexcept {
    for (auto &section : sections)
        sample = section.processSample(sample);

    return sample;
}

//==============================================================================
void MonoSafetyCrossover::prepare(double newSampleRate,
                                  int maximumBlockSize) {
    jassert(newSampleRate > 0.0);
    jassert(maximumBlockSize >= 0);

    sampleRate = newSampleRate;

    const auto initialCutoff = juce::jlimit(
        1.0f,
        static_cast<float>(sampleRate * 0.5),
        cutoffHz);

#if SHREDWIDTH_XOVER_ORDER == 4
    lowpassCoefficients = new Coefficients(
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(
            sampleRate, initialCutoff));
    highpassCoefficients = new Coefficients(
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(
            sampleRate, initialCutoff));
#else
    lowpassCoefficients = new Coefficients(
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass(
            sampleRate, initialCutoff));
    highpassCoefficients = new Coefficients(
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass(
            sampleRate, initialCutoff));
#endif

    lowpass.setCoefficients(lowpassCoefficients);
    highpassLeft.setCoefficients(highpassCoefficients);
    highpassRight.setCoefficients(highpassCoefficients);

    const juce::dsp::ProcessSpec spec{
        sampleRate,
        static_cast<juce::uint32>(juce::jmax(0, maximumBlockSize)),
        1
    };

    lowpass.prepare(spec);
    highpassLeft.prepare(spec);
    highpassRight.prepare(spec);

    cutoffDirty = false;
    prepared = true;
    reset();
}

void MonoSafetyCrossover::prepare(
    const juce::dsp::ProcessSpec &spec) {
    prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
}

void MonoSafetyCrossover::reset() noexcept {
    lowpass.reset();
    highpassLeft.reset();
    highpassRight.reset();
}

void MonoSafetyCrossover::setCutoffHz(float newCutoffHz) noexcept {
    jassert(std::isfinite(newCutoffHz));

    if (!std::isfinite(newCutoffHz))
        return;

    newCutoffHz = juce::jmax(1.0f, newCutoffHz);

    if (newCutoffHz != cutoffHz) {
        cutoffHz = newCutoffHz;
        cutoffDirty = true;
    }
}

void MonoSafetyCrossover::beginBlock() noexcept {
    if (prepared && cutoffDirty)
        updateCoefficients();
}

MonoSafetyCrossover::Outputs MonoSafetyCrossover::process(
    float dryMono,
    float wetLeft,
    float wetRight) noexcept {
    jassert(prepared);

    return {
        processLowpass(dryMono),
        processHighpassLeft(wetLeft),
        processHighpassRight(wetRight)
    };
}

void MonoSafetyCrossover::processBlock(const float *dryMono,
                                       const float *wetLeft,
                                       const float *wetRight,
                                       float *low,
                                       float *highLeft,
                                       float *highRight,
                                       int numSamples) noexcept {
    jassert(prepared);
    jassert(numSamples >= 0);
    jassert(numSamples == 0
            || (dryMono != nullptr && wetLeft != nullptr && wetRight != nullptr
                && low != nullptr && highLeft != nullptr
                && highRight != nullptr));

    beginBlock();

    for (int i = 0; i < numSamples; ++i) {
        const auto output = process(dryMono[i], wetLeft[i], wetRight[i]);
        low[i] = output.low;
        highLeft[i] = output.highLeft;
        highRight[i] = output.highRight;
    }
}

float MonoSafetyCrossover::processLowpass(float dryMono) noexcept {
    jassert(prepared);
    return lowpass.process(dryMono);
}

float MonoSafetyCrossover::processHighpassLeft(float wetLeft) noexcept {
    jassert(prepared);

#if SHREDWIDTH_XOVER_ORDER == 4
    // LR4 lowpass and highpass cascades are already in phase.
    return highpassLeft.process(wetLeft);
#else
    // LR2 bands are 180 degrees apart; D2 standardises reconstruction by
    // inverting the high band internally.
    return -highpassLeft.process(wetLeft);
#endif
}

float MonoSafetyCrossover::processHighpassRight(float wetRight) noexcept {
    jassert(prepared);

#if SHREDWIDTH_XOVER_ORDER == 4
    return highpassRight.process(wetRight);
#else
    return -highpassRight.process(wetRight);
#endif
}

//==============================================================================
void MonoSafetyCrossover::updateCoefficients() noexcept {
    jassert(prepared);
    jassert(lowpassCoefficients != nullptr);
    jassert(highpassCoefficients != nullptr);

    const auto clampedCutoff = juce::jlimit(
        1.0f,
        static_cast<float>(sampleRate * 0.5),
        cutoffHz);

    // ArrayCoefficients returns fixed std::array values. Assigning those into
    // the coefficient objects created in prepare() reuses their reserved
    // storage, avoiding reference-counted-object allocation on the audio
    // thread. Each object is shared by every matched section of that band.
#if SHREDWIDTH_XOVER_ORDER == 4
    *lowpassCoefficients =
        juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(
            sampleRate, clampedCutoff);
    *highpassCoefficients =
        juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(
            sampleRate, clampedCutoff);
#else
    *lowpassCoefficients =
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass(
            sampleRate, clampedCutoff);
    *highpassCoefficients =
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass(
            sampleRate, clampedCutoff);
#endif

    cutoffDirty = false;
}
