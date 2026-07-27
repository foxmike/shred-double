#include "ShredDoubleEngine.h"

#include "RandomWalk.h"

#include <algorithm>
#include <cmath>

//==============================================================================
void ShredDoubleEngine::DryAlignDelay::prepare() {
  leftBuffer.allocate(static_cast<size_t>(capacity), true);
  rightBuffer.allocate(static_cast<size_t>(capacity), true);
  reset();
}

void ShredDoubleEngine::DryAlignDelay::reset() noexcept {
  if (leftBuffer.getData() == nullptr || rightBuffer.getData() == nullptr)
    return;

  std::fill(leftBuffer.getData(), leftBuffer.getData() + capacity, 0.0f);
  std::fill(rightBuffer.getData(), rightBuffer.getData() + capacity, 0.0f);
  writePosition = 0;
}

void ShredDoubleEngine::DryAlignDelay::process(float inputLeft,
                                               float inputRight, int delayA,
                                               int delayB, float blend,
                                               float &outputLeft,
                                               float &outputRight) noexcept {
  leftBuffer[writePosition] = inputLeft;
  rightBuffer[writePosition] = inputRight;

  const int indexA = readIndex(delayA);
  const int indexB = readIndex(delayB);
  const float clampedBlend = juce::jlimit(0.0f, 1.0f, blend);
  const float inverseBlend = 1.0f - clampedBlend;

  outputLeft =
      inverseBlend * leftBuffer[indexA] + clampedBlend * leftBuffer[indexB];
  outputRight =
      inverseBlend * rightBuffer[indexA] + clampedBlend * rightBuffer[indexB];

  if (++writePosition >= capacity)
    writePosition = 0;
}

int ShredDoubleEngine::DryAlignDelay::readIndex(
    int delaySamples) const noexcept {
  const int clampedDelay = juce::jlimit(0, maximumDelaySamples, delaySamples);
  int index = writePosition - clampedDelay;
  if (index < 0)
    index += capacity;
  return index;
}

//==============================================================================
void ShredDoubleEngine::prepare(double newSampleRate, int maximumBlockSize,
                                LatencyMode latencyMode) {
  jassert(newSampleRate > 0.0);
  jassert(maximumBlockSize >= 0);

  sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
  requestedLatencyMode = latencyMode;
  processingLatencyMode = latencyMode;

  // Voice::prepare reserves every PitchShifter at Studio length and every
  // drifting delay for at least 50 ms, covering the specified 35 ms rail.
  for (auto &voice : voices)
    voice.prepare(sampleRate, maximumBlockSize);

  crossover.prepare(sampleRate, maximumBlockSize);
  widthProcessor.prepare(sampleRate, maximumBlockSize);
  dryAlign.prepare();
  meter.prepare(sampleRate, maximumBlockSize);

  for (int voiceIndex = 0; voiceIndex < maximumVoices; ++voiceIndex) {
    auto &gain = voiceGains[static_cast<size_t>(voiceIndex)];
    gain.reset(sampleRate, switchCrossfadeSeconds);
    const float initialGain = voiceIndex < voiceCount ? 1.0f : 0.0f;
    gain.setCurrentAndTargetValue(initialGain);
    voices[static_cast<size_t>(voiceIndex)].setMuted(initialGain <= 0.0f);
  }

  mixSmoothed.reset(sampleRate, gainRampSeconds);
  mixSmoothed.setCurrentAndTargetValue(targetMix);
  outTrimSmoothed.reset(sampleRate, gainRampSeconds);
  outTrimSmoothed.setCurrentAndTargetValue(targetOutTrimGain);

  transitionLengthSamples = juce::jmax(
      2, static_cast<int>(std::lround(sampleRate * switchCrossfadeSeconds)));

  applyNominalDelayLayout();
  for (int voiceIndex = 0; voiceIndex < maximumVoices; ++voiceIndex)
    voices[static_cast<size_t>(voiceIndex)].setPanPercent(
        panPercents[static_cast<size_t>(voiceIndex)]);
  applyVoiceParameters();
  applyProcessingLatencyMode(processingLatencyMode);
  crossover.setCutoffHz(monoXoverHz);
  crossover.beginBlock();
  widthProcessor.setWidth(width);

  prepared = true;
  seed(masterSeed);
  reset();
}

void ShredDoubleEngine::reset() noexcept {
  if (!prepared)
    return;

  processingLatencyMode = requestedLatencyMode;
  applyProcessingLatencyMode(processingLatencyMode);

  for (int voiceIndex = 0; voiceIndex < maximumVoices; ++voiceIndex) {
    auto &voice = voices[static_cast<size_t>(voiceIndex)];
    voice.reset();

    const float gain = voiceIndex < voiceCount ? 1.0f : 0.0f;
    voiceGains[static_cast<size_t>(voiceIndex)].setCurrentAndTargetValue(gain);
    voice.setMuted(gain <= 0.0f);
  }

  crossover.reset();
  widthProcessor.reset();
  dryAlign.reset();
  meter.reset();
  mixSmoothed.setCurrentAndTargetValue(targetMix);
  outTrimSmoothed.setCurrentAndTargetValue(targetOutTrimGain);

  transitionSamplesRemaining = 0;
  transitionOldDryLatency = getLatencySamples();
  transitionModeApplied = true;
  driftHopCounter = 0;
  hasProcessedAudio = false;
  publishVoiceDrift();
}

void ShredDoubleEngine::seed(std::uint64_t newMasterSeed) noexcept {
  masterSeed = newMasterSeed;
  for (auto &voice : voices)
    voice.reseed(masterSeed);

  driftHopCounter = 0;
  if (prepared)
    publishVoiceDrift();
}

//==============================================================================
void ShredDoubleEngine::setVoiceCount(int newVoiceCount) noexcept {
  if ((newVoiceCount != 2 && newVoiceCount != 4) || newVoiceCount == voiceCount)
    return;

  voiceCount = newVoiceCount;
  applyNominalDelayLayout();

  for (int voiceIndex = 2; voiceIndex < maximumVoices; ++voiceIndex) {
    auto &voice = voices[static_cast<size_t>(voiceIndex)];
    auto &gain = voiceGains[static_cast<size_t>(voiceIndex)];
    const float target = voiceCount == 4 ? 1.0f : 0.0f;

    // A muted Voice still advances its DSP when processSample() is called,
    // but it must be unmuted before the ramp-in can become audible.
    if (target > 0.0f)
      voice.setMuted(false);

    if (prepared)
      gain.setTargetValue(target);
    else
      gain.setCurrentAndTargetValue(target);
  }
}

void ShredDoubleEngine::setLatencyMode(LatencyMode mode) noexcept {
  if (mode == requestedLatencyMode && transitionSamplesRemaining == 0)
    return;

  requestedLatencyMode = mode;

  if (!prepared || !hasProcessedAudio) {
    processingLatencyMode = requestedLatencyMode;
    if (prepared)
      applyProcessingLatencyMode(processingLatencyMode);

    transitionSamplesRemaining = 0;
    transitionOldDryLatency = getLatencySamples();
    transitionModeApplied = true;
    return;
  }

  if (requestedLatencyMode == processingLatencyMode) {
    transitionSamplesRemaining = 0;
    transitionOldDryLatency = getLatencySamples();
    transitionModeApplied = true;
    return;
  }

  transitionOldDryLatency = latencyForMode(processingLatencyMode);
  transitionSamplesRemaining = transitionLengthSamples;
  transitionModeApplied = false;
}

int ShredDoubleEngine::getLatencySamples() const noexcept {
  return latencyForMode(requestedLatencyMode);
}

//==============================================================================
void ShredDoubleEngine::setTimingSpreadMs(float milliseconds) noexcept {
  if (!std::isfinite(milliseconds))
    return;

  timingSpreadMs =
      juce::jlimit(0.0f, RandomWalk::maxTimingSpreadMs, milliseconds);
  applyVoiceParameters();
}

void ShredDoubleEngine::setPitchSpreadCents(float cents) noexcept {
  if (!std::isfinite(cents))
    return;

  pitchSpreadCents = juce::jlimit(0.0f, RandomWalk::maxPitchSpreadCents, cents);
  applyVoiceParameters();
}

void ShredDoubleEngine::setHumanRate(float percent) noexcept {
  if (!std::isfinite(percent))
    return;

  humanRate = juce::jlimit(0.0f, 100.0f, percent);
  applyVoiceParameters();
}

void ShredDoubleEngine::setTightness(float proportion) noexcept {
  if (!std::isfinite(proportion))
    return;

  tightness = juce::jlimit(0.0f, 1.0f, proportion);
  applyVoiceParameters();
}

void ShredDoubleEngine::setPanPercent(int voiceIndex, float percent) noexcept {
  if (voiceIndex < 0 || voiceIndex >= maximumVoices || !std::isfinite(percent))
    return;

  panPercents[static_cast<size_t>(voiceIndex)] =
      juce::jlimit(-100.0f, 100.0f, percent);
  voices[static_cast<size_t>(voiceIndex)].setPanPercent(
      panPercents[static_cast<size_t>(voiceIndex)]);
}

void ShredDoubleEngine::setPanPercents(float voice1, float voice2, float voice3,
                                       float voice4) noexcept {
  setPanPercent(0, voice1);
  setPanPercent(1, voice2);
  setPanPercent(2, voice3);
  setPanPercent(3, voice4);
}

void ShredDoubleEngine::setMonoXoverHz(float hertz) noexcept {
  if (!std::isfinite(hertz))
    return;

  monoXoverHz = juce::jmax(1.0f, hertz);
  crossover.setCutoffHz(monoXoverHz);
}

void ShredDoubleEngine::setWidth(float factor) noexcept {
  if (!std::isfinite(factor))
    return;

  width =
      juce::jlimit(WidthProcessor::minWidth, WidthProcessor::maxWidth, factor);
  widthProcessor.setWidth(width);
}

void ShredDoubleEngine::setWidthPercent(float percent) noexcept {
  if (std::isfinite(percent))
    setWidth(percent * 0.01f);
}

void ShredDoubleEngine::setMix(float wetProportion) noexcept {
  if (!std::isfinite(wetProportion))
    return;

  targetMix = juce::jlimit(0.0f, 1.0f, wetProportion);
  if (prepared)
    mixSmoothed.setTargetValue(targetMix);
  else
    mixSmoothed.setCurrentAndTargetValue(targetMix);
}

void ShredDoubleEngine::setOutTrimDb(float decibels) noexcept {
  if (!std::isfinite(decibels))
    return;

  targetOutTrimGain =
      juce::Decibels::decibelsToGain(juce::jlimit(-100.0f, 24.0f, decibels));
  if (prepared)
    outTrimSmoothed.setTargetValue(targetOutTrimGain);
  else
    outTrimSmoothed.setCurrentAndTargetValue(targetOutTrimGain);
}

//==============================================================================
void ShredDoubleEngine::process(const float *inL, const float *inR, float *outL,
                                float *outR, int numSamples) noexcept {
  juce::ScopedNoDenormals noDenormals;

  jassert(prepared);
  jassert(numSamples >= 0);
  jassert(numSamples == 0 ||
          (inL != nullptr && outL != nullptr && outR != nullptr));

  if (!prepared || numSamples <= 0 || inL == nullptr || outL == nullptr ||
      outR == nullptr)
    return;

  crossover.beginBlock();
  const bool stereoInput = inR != nullptr;

  for (int sample = 0; sample < numSamples; ++sample) {
    // Capture both inputs before writing outputs so in-place processing is
    // safe even when outL/outR alias inL/inR.
    const float inputLeft = inL[sample];
    const float inputRight = stereoInput ? inR[sample] : inputLeft;
    processFrame(inputLeft, inputRight, stereoInput, outL[sample],
                 outR[sample]);
  }
}

void ShredDoubleEngine::process(float *left, float *right, int numSamples,
                                int numInputChannels) noexcept {
  jassert(numInputChannels == 1 || numInputChannels == 2);
  process(left, numInputChannels > 1 ? right : nullptr, left, right,
          numSamples);
}

//==============================================================================
int ShredDoubleEngine::latencyForMode(LatencyMode mode) noexcept {
  return (mode == LatencyMode::Studio ? studioPitchLength : livePitchLength) /
         2;
}

PitchShifter::LatencyMode
ShredDoubleEngine::pitchModeFor(LatencyMode mode) noexcept {
  return mode == LatencyMode::Studio ? PitchShifter::LatencyMode::studio
                                     : PitchShifter::LatencyMode::live;
}

void ShredDoubleEngine::applyVoiceParameters() noexcept {
  const auto pitchMode = pitchModeFor(processingLatencyMode);
  for (int voiceIndex = 0; voiceIndex < maximumVoices; ++voiceIndex) {
    const float gain =
        voiceGains[static_cast<size_t>(voiceIndex)].getTargetValue();
    voices[static_cast<size_t>(voiceIndex)].setParams(
        timingSpreadMs, pitchSpreadCents, humanRate, tightness, pitchMode,
        gain);
  }
}

void ShredDoubleEngine::applyNominalDelayLayout() noexcept {
  static constexpr std::array<float, maximumVoices> twoVoiceDelays{
      14.0f, 22.0f, 26.0f, 33.0f};
  static constexpr std::array<float, maximumVoices> fourVoiceDelays{
      12.0f, 19.0f, 26.0f, 33.0f};

  const auto &layout = voiceCount == 4 ? fourVoiceDelays : twoVoiceDelays;
  for (int voiceIndex = 0; voiceIndex < maximumVoices; ++voiceIndex)
    voices[static_cast<size_t>(voiceIndex)].setNominalDelayMs(
        layout[static_cast<size_t>(voiceIndex)]);
}

void ShredDoubleEngine::applyProcessingLatencyMode(LatencyMode mode) noexcept {
  const auto pitchMode = pitchModeFor(mode);
  for (auto &voice : voices)
    voice.setLatencyMode(pitchMode);

  processingLatencyMode = mode;
}

void ShredDoubleEngine::publishVoiceDrift() noexcept {
  for (int voiceIndex = 0; voiceIndex < maximumVoices; ++voiceIndex) {
    const auto &voice = voices[static_cast<size_t>(voiceIndex)];
    meter.setVoiceDrift(voiceIndex, voice.getCurrentDelayMs(),
                        voice.getCurrentDetuneCents());
  }
}

void ShredDoubleEngine::processFrame(float inputLeft, float inputRight,
                                     bool stereoInput, float &outputLeft,
                                     float &outputRight) noexcept {
  hasProcessedAudio = true;

  const float dryInputRight = stereoInput ? inputRight : inputLeft;
  const float mono = stereoInput ? 0.5f * (inputLeft + inputRight) : inputLeft;

  float transitionProgress = 1.0f;
  float wetModeGain = 1.0f;
  int dryLatencyA = getLatencySamples();
  int dryLatencyB = dryLatencyA;

  if (transitionSamplesRemaining > 0) {
    const int elapsed = transitionLengthSamples - transitionSamplesRemaining;
    transitionProgress =
        static_cast<float>(elapsed) /
        static_cast<float>(juce::jmax(1, transitionLengthSamples - 1));

    dryLatencyA = transitionOldDryLatency;
    dryLatencyB = getLatencySamples();

    // At the midpoint the wet path is silent and the two integer dry taps
    // are crossfaded, masking the unavoidable shifter-ring repoint.
    if (!transitionModeApplied && transitionProgress >= 0.5f) {
      applyProcessingLatencyMode(requestedLatencyMode);
      transitionModeApplied = true;
    }

    wetModeGain = transitionProgress < 0.5f ? 1.0f - 2.0f * transitionProgress
                                            : 2.0f * transitionProgress - 1.0f;
    wetModeGain = juce::jlimit(0.0f, 1.0f, wetModeGain);
  }

  float wetAccumulatorLeft = 0.0f;
  float wetAccumulatorRight = 0.0f;

  for (int voiceIndex = 0; voiceIndex < maximumVoices; ++voiceIndex) {
    auto &voice = voices[static_cast<size_t>(voiceIndex)];
    auto &gain = voiceGains[static_cast<size_t>(voiceIndex)];
    const float currentGain = gain.getNextValue();

    voice.setParams(timingSpreadMs, pitchSpreadCents, humanRate, tightness,
                    pitchModeFor(processingLatencyMode), currentGain);
    voice.processSample(mono, wetAccumulatorLeft, wetAccumulatorRight);

    if (voiceIndex >= 2 && currentGain <= 0.0f && !gain.isSmoothing() &&
        gain.getTargetValue() <= 0.0f)
      voice.setMuted(true);
  }

  const auto bands =
      crossover.process(mono, wetAccumulatorLeft, wetAccumulatorRight);
  const float assembledLeft = bands.low + bands.highLeft;
  const float assembledRight = bands.low + bands.highRight;

  float wetLeft = 0.0f;
  float wetRight = 0.0f;
  widthProcessor.processSample(assembledLeft, assembledRight, wetLeft,
                               wetRight);

  float dryLeft = 0.0f;
  float dryRight = 0.0f;
  dryAlign.process(inputLeft, dryInputRight, dryLatencyA, dryLatencyB,
                   transitionProgress, dryLeft, dryRight);

  const float trimGain = outTrimSmoothed.getNextValue();
  if (bypass) {
    // Contractor choice: true bypass is the original stereo input delayed
    // by the currently reported PDC. Output trim and wet processing do not
    // affect it; the internal DSP still advances for click-free un-bypass.
    outputLeft = dryLeft;
    outputRight = dryRight;
  } else {
    const float wetMix = mixSmoothed.getNextValue() * wetModeGain;
    const float dryMix = 1.0f - wetMix;
    outputLeft = trimGain * (dryMix * dryLeft + wetMix * wetLeft);
    outputRight = trimGain * (dryMix * dryRight + wetMix * wetRight);
  }

  meter.processSample(outputLeft, outputRight);

  if (++driftHopCounter >= RandomWalk::controlHop) {
    driftHopCounter = 0;
    publishVoiceDrift();
  }

  if (transitionSamplesRemaining > 0) {
    --transitionSamplesRemaining;
    if (transitionSamplesRemaining == 0) {
      if (!transitionModeApplied)
        applyProcessingLatencyMode(requestedLatencyMode);
      transitionOldDryLatency = getLatencySamples();
      transitionModeApplied = true;
    }
  }
}
