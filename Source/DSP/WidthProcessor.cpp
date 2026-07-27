#include "WidthProcessor.h"

//==============================================================================
void WidthProcessor::prepare(double sampleRateIn, int maxBlockSize) {
    juce::ignoreUnused(maxBlockSize);
    jassert(sampleRateIn > 0.0);

    sampleRate = sampleRateIn;

    // Width is ramped over 20 ms per §6 so live automation does not zip.
    // SmoothedValue::reset configures internal storage; this is the only
    // point in the class where allocation is allowed (see class header).
    widthSmoothed.reset(sampleRate, rampSeconds);

    // Snap to the current logical default so the smoother is well-defined
    // even before the engine calls setWidth().
    targetWidth = unityWidth;
    currentWidth = unityWidth;
    widthSmoothed.setCurrentAndTargetValue(targetWidth);

    prepared = true;
}

void WidthProcessor::prepare(const juce::dsp::ProcessSpec &spec) noexcept {
    prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
}

void WidthProcessor::reset() noexcept {
    if (!prepared)
        return;

    // Snap the smoother to the current target without ramping; the next
    // processSample call uses the exact target width. No allocation.
    widthSmoothed.setCurrentAndTargetValue(targetWidth);
    currentWidth = targetWidth;
}

//==============================================================================
void WidthProcessor::setWidth(float width) noexcept {
    targetWidth = juce::jlimit(minWidth, maxWidth, width);

    if (prepared)
        widthSmoothed.setTargetValue(targetWidth);
}

void WidthProcessor::setWidthPercent(float percent) noexcept {
    setWidth(percent * 0.01f);
}

//==============================================================================
void WidthProcessor::processSample(float inL,
                                   float inR,
                                   float &outL,
                                   float &outR) noexcept {
    jassert(prepared);

    // §4.7 math verbatim. The smoother advances by one step per call so a
    // block of N samples produces N ramping width values, exactly matching
    // PitchShifter::processSample's per-sample smoother use.
    currentWidth = widthSmoothed.getNextValue();

    const float m = 0.5f * (inL + inR);
    const float s = 0.5f * (inL - inR);

    outL = m + currentWidth * s;
    outR = m - currentWidth * s;
}

void WidthProcessor::process(float *inOutL,
                             float *inOutR,
                             int numSamples) noexcept {
    jassert(inOutL != nullptr || numSamples == 0);
    jassert(inOutR != nullptr || numSamples == 0);

    for (int i = 0; i < numSamples; ++i) {
        const float inL = inOutL[i];
        const float inR = inOutR[i];
        processSample(inL, inR, inOutL[i], inOutR[i]);
    }
}

void WidthProcessor::process(const float *inL,
                             const float *inR,
                             float *outL,
                             float *outR,
                             int numSamples) noexcept {
    jassert(numSamples >= 0);
    jassert(numSamples == 0
            || (inL != nullptr && inR != nullptr
                && outL != nullptr && outR != nullptr));

    for (int i = 0; i < numSamples; ++i)
        processSample(inL[i], inR[i], outL[i], outR[i]);
}
