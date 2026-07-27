#include "Voice.h"
#include <algorithm>
#include <cmath>

Voice::Voice(int index) noexcept
    : delayWalk(RandomWalk::Axis::delay),
      pitchWalk(RandomWalk::Axis::pitch),
      nominalDelayMs(20.0f),
      panL(0.7071f),
      panR(0.7071f),
      voiceIndex(index),
      masterSeed(0),
      muteGain(1.0f),
      isMuted(false),
      currentTauMs(20.0f),
      currentDeltaC(0.0f)
{
}

void Voice::prepare(double sampleRate, int maxBlockSize) {
    delayWalk.prepare(sampleRate);
    pitchWalk.prepare(sampleRate);
    shifter.prepare(sampleRate, maxBlockSize);
    delay.prepare(sampleRate, maxBlockSize);
    diffusion.prepare(sampleRate, maxBlockSize);
    tilt.prepare(sampleRate);

    diffusion.setVoiceIndex(voiceIndex);
    tilt.setVoiceIndex(voiceIndex);

    // Reseed walks with current seed & index
    delayWalk.reseed(masterSeed, static_cast<std::uint64_t>(voiceIndex), RandomWalk::delayAxisTag);
    pitchWalk.reseed(masterSeed, static_cast<std::uint64_t>(voiceIndex), RandomWalk::pitchAxisTag);

    reset();
}

void Voice::prepare(const juce::dsp::ProcessSpec& spec) {
    prepare(spec.sampleRate, static_cast<int>(spec.maximumBlockSize));
}

void Voice::reset() noexcept {
    delayWalk.reset();
    pitchWalk.reset();
    shifter.reset();
    delay.reset();
    diffusion.reset();
    tilt.reset();
    
    // Reset our tracked outputs to nominal/mean
    currentTauMs = nominalDelayMs;
    currentDeltaC = 0.0f;
}

void Voice::reseed(std::uint64_t newMasterSeed) noexcept {
    masterSeed = newMasterSeed;
    delayWalk.reseed(masterSeed, static_cast<std::uint64_t>(voiceIndex), RandomWalk::delayAxisTag);
    pitchWalk.reseed(masterSeed, static_cast<std::uint64_t>(voiceIndex), RandomWalk::pitchAxisTag);
    reset();
}

float Voice::processSample(float x) noexcept {
    const float tau_ms = delayWalk.processSample();
    const float deltaC = pitchWalk.processSample();

    currentTauMs = tau_ms;
    currentDeltaC = deltaC;

    shifter.setDetuneCents(deltaC);
    float y = shifter.processSample(x);

    delay.setDelayMs(tau_ms);
    y = delay.processSample(y);

    y = diffusion.processSample(y);
    y = tilt.processSample(y);

    return y;
}

void Voice::processSample(float x, float& accL, float& accR) noexcept {
    const float y = processSample(x);
    const float mute = isMuted ? 0.0f : muteGain;
    accL += y * panL * mute;
    accR += y * panR * mute;
}

void Voice::setPanPercent(float panPercent) noexcept {
    const float clampedPan = std::clamp(panPercent, -100.0f, 100.0f);
    const float normalizedPan = (clampedPan + 100.0f) / 200.0f; // 0.0 to 1.0
    const float angleRad = normalizedPan * 1.5707963267948966f;
    panL = std::cos(angleRad);
    panR = std::sin(angleRad);
}

void Voice::setNominalDelayMs(float ms) noexcept {
    nominalDelayMs = ms;
    delayWalk.setNominalDelayMs(ms);
}

void Voice::setTimingSpreadMs(float ms) noexcept {
    delayWalk.setTimingSpreadMs(ms);
}

void Voice::setPitchSpreadCents(float cents) noexcept {
    pitchWalk.setPitchSpreadCents(cents);
}

void Voice::setHumanRate(float rate) noexcept {
    delayWalk.setHumanRate(rate);
    pitchWalk.setHumanRate(rate);
}

void Voice::setTightness(float tightness) noexcept {
    delayWalk.setTightness(tightness);
    pitchWalk.setTightness(tightness);
}

void Voice::setLatencyMode(PitchShifter::LatencyMode mode) noexcept {
    shifter.setLatencyMode(mode);
}

void Voice::setMuted(bool shouldBeMuted) noexcept {
    isMuted = shouldBeMuted;
}

void Voice::setVoiceIndex(int index) noexcept {
    voiceIndex = index;
    diffusion.setVoiceIndex(voiceIndex);
    tilt.setVoiceIndex(voiceIndex);
    delayWalk.reseed(masterSeed, static_cast<std::uint64_t>(voiceIndex), RandomWalk::delayAxisTag);
    pitchWalk.reseed(masterSeed, static_cast<std::uint64_t>(voiceIndex), RandomWalk::pitchAxisTag);
}

void Voice::setParams(float timingSpreadMs,
                      float pitchSpreadCents,
                      float humanRate,
                      float tightness,
                      PitchShifter::LatencyMode latencyMode,
                      float muteGainVal) noexcept {
    setTimingSpreadMs(timingSpreadMs);
    setPitchSpreadCents(pitchSpreadCents);
    setHumanRate(humanRate);
    setTightness(tightness);
    setLatencyMode(latencyMode);
    muteGain = std::clamp(muteGainVal, 0.0f, 1.0f);
}
