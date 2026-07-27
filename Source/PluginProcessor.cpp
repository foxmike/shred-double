#include "PluginProcessor.h"

#include "ParameterIDs.h"
#include "PluginEditor.h"
#include "StateManager.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <random>

namespace {
constexpr auto stateTreeType = "ShredDoubleState";
constexpr auto parameterLayoutVersionProperty = "parameterLayoutVersion";

juce::ParameterID parameterID(const char *id) {
  return {id, ShredDouble::ParameterIDs::parameterLayoutVersion};
}

juce::NormalisableRange<float> linearRange(float minimum, float maximum,
                                           float interval) {
  return {minimum, maximum, interval};
}
} // namespace

// StateManager::reRollMasterSeed implementation (surfaced in PluginProcessor.cpp to respect strict file scope)
void StateManager::reRollMasterSeed(juce::AudioProcessorValueTreeState& apvts) {
  juce::Random r;
  uint64_t seedVal = static_cast<uint64_t>(r.nextInt64());

  auto splitmix64 = [](uint64_t x) -> uint64_t {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
  };

  setMasterSeed(apvts, splitmix64(seedVal));
}


ShredDoubleAudioProcessor::ShredDoubleAudioProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, stateTreeType, createParameterLayout()) {
  apvts.state.setProperty(parameterLayoutVersionProperty,
                          ShredDouble::ParameterIDs::parameterLayoutVersion,
                          nullptr);

  // Stored as text so the eventual StateManager can preserve all uint64
  // values even though juce::var's integer representation is signed.
  if (!apvts.state.hasProperty(ShredDouble::ParameterIDs::masterSeed))
    apvts.state.setProperty(ShredDouble::ParameterIDs::masterSeed, "0",
                            nullptr);

  apvts.state.addListener(this);
  updateReportedLatencyFromParameter();
}

ShredDoubleAudioProcessor::~ShredDoubleAudioProcessor() {
  apvts.state.removeListener(this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
ShredDoubleAudioProcessor::createParameterLayout() {
  namespace IDs = ShredDouble::ParameterIDs;

  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  layout.add(std::make_unique<juce::AudioParameterChoice>(
      parameterID(IDs::voiceCount), "Voice Count", juce::StringArray{"2", "4"},
      0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::timingSpread), "Timing Spread",
      linearRange(0.0f, 15.0f, 0.01f), 6.0f, "ms"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::pitchSpread), "Pitch Spread",
      linearRange(0.0f, 10.0f, 0.01f), 5.0f, "cents"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::humanRate), "Humanization Rate",
      linearRange(0.0f, 100.0f, 0.1f), 50.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::tightness), "Tightness", linearRange(0.0f, 100.0f, 0.1f),
      50.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::monoXover), "Low Mono Crossover",
      juce::NormalisableRange<float>{80.0f, 300.0f, 1.0f, 0.35f}, 180.0f,
      "Hz"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::mix), "Mix", linearRange(0.0f, 100.0f, 0.1f), 50.0f,
      "% wet"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::panV1), "Pan V1", linearRange(-100.0f, 100.0f, 0.1f),
      -100.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::panV2), "Pan V2", linearRange(-100.0f, 100.0f, 0.1f),
      100.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::panV3), "Pan V3", linearRange(-100.0f, 100.0f, 0.1f),
      -60.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::panV4), "Pan V4", linearRange(-100.0f, 100.0f, 0.1f),
      60.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::width), "Stereo Width", linearRange(0.0f, 200.0f, 0.1f),
      100.0f, "%"));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      parameterID(IDs::latencyMode), "Latency Mode",
      juce::StringArray{"Studio", "Live"}, 0));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(IDs::outTrim), "Output Trim",
      linearRange(-12.0f, 12.0f, 0.1f), 0.0f, "dB"));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      parameterID(IDs::bypass), "Bypass", false));

  return layout;
}

void ShredDoubleAudioProcessor::prepareToPlay(double sampleRate,
                                              int samplesPerBlock) {
  preparedSampleRate = sampleRate;
  preparedMaxBlockSize = juce::jmax(0, samplesPerBlock);

  const auto *latencyModeParam =
      apvts.getRawParameterValue(ShredDouble::ParameterIDs::latencyMode);
  const auto latencyMode = (latencyModeParam != nullptr && latencyModeParam->load(std::memory_order_relaxed) >= 0.5f)
                               ? ShredDoubleEngine::LatencyMode::Live
                               : ShredDoubleEngine::LatencyMode::Studio;

  engine.prepare(sampleRate, samplesPerBlock, latencyMode);

  StateManager stateManager;
  engine.seed(stateManager.getMasterSeed(apvts));

  // Initialize smoothers
  timingSpreadSmoother.reset(sampleRate, 0.05); // 50 ms
  pitchSpreadSmoother.reset(sampleRate, 0.05);  // 50 ms
  for (auto& panSmoother : panSmoothers) {
      panSmoother.reset(sampleRate, 0.02);      // 20 ms
  }

  const float initialTiming = apvts.getRawParameterValue(ShredDouble::ParameterIDs::timingSpread)->load(std::memory_order_relaxed);
  const float initialPitch = apvts.getRawParameterValue(ShredDouble::ParameterIDs::pitchSpread)->load(std::memory_order_relaxed);
  timingSpreadSmoother.setCurrentAndTargetValue(initialTiming);
  pitchSpreadSmoother.setCurrentAndTargetValue(initialPitch);

  for (int v = 0; v < 4; ++v) {
      juce::String panID = "panV" + juce::String(v + 1);
      const float initialPan = apvts.getRawParameterValue(panID)->load(std::memory_order_relaxed);
      panSmoothers[static_cast<size_t>(v)].setCurrentAndTargetValue(initialPan);
  }

  setLatencySamples(engine.getLatencySamples());
}

void ShredDoubleAudioProcessor::releaseResources() {}

bool ShredDoubleAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto input = layouts.getMainInputChannelSet();
  const auto output = layouts.getMainOutputChannelSet();

  return output == juce::AudioChannelSet::stereo() &&
         (input == juce::AudioChannelSet::mono() ||
          input == juce::AudioChannelSet::stereo());
}

void ShredDoubleAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                             juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  juce::ignoreUnused(midiMessages);

  const auto inputChannels = getTotalNumInputChannels();
  const auto outputChannels = getTotalNumOutputChannels();
  const auto numSamples = buffer.getNumSamples();

  for (auto channel = inputChannels; channel < outputChannels; ++channel)
    buffer.clear(channel, 0, numSamples);

  if (numSamples == 0)
    return;

  // 1. Voice count changes:
  const float rawVoiceCount = apvts.getRawParameterValue(ShredDouble::ParameterIDs::voiceCount)->load(std::memory_order_relaxed);
  const int currentVoiceCount = rawVoiceCount >= 0.5f ? 4 : 2;
  if (currentVoiceCount != engine.getVoiceCount()) {
      engine.setVoiceCount(currentVoiceCount);
  }

  // 2. Latency mode changes:
  const float rawLatencyMode = apvts.getRawParameterValue(ShredDouble::ParameterIDs::latencyMode)->load(std::memory_order_relaxed);
  const auto currentLatencyMode = rawLatencyMode >= 0.5f ? ShredDoubleEngine::LatencyMode::Live : ShredDoubleEngine::LatencyMode::Studio;
  if (currentLatencyMode != engine.getLatencyMode()) {
      engine.setLatencyMode(currentLatencyMode);
      setLatencySamples(engine.getLatencySamples());
      updateHostDisplay();
  }

  // 3. Set block-rate parameters on the engine
  const float crossoverVal = apvts.getRawParameterValue(ShredDouble::ParameterIDs::monoXover)->load(std::memory_order_relaxed);
  engine.setMonoXoverHz(crossoverVal);

  const float humanRateVal = apvts.getRawParameterValue(ShredDouble::ParameterIDs::humanRate)->load(std::memory_order_relaxed);
  engine.setHumanRate(humanRateVal);

  const float tightnessVal = apvts.getRawParameterValue(ShredDouble::ParameterIDs::tightness)->load(std::memory_order_relaxed) * 0.01f;
  engine.setTightness(tightnessVal);

  const float mixVal = apvts.getRawParameterValue(ShredDouble::ParameterIDs::mix)->load(std::memory_order_relaxed) * 0.01f;
  engine.setMix(mixVal);

  const float outTrimVal = apvts.getRawParameterValue(ShredDouble::ParameterIDs::outTrim)->load(std::memory_order_relaxed);
  engine.setOutTrimDb(outTrimVal);

  const float widthVal = apvts.getRawParameterValue(ShredDouble::ParameterIDs::width)->load(std::memory_order_relaxed);
  engine.setWidthPercent(widthVal);

  const bool bypassed = apvts.getRawParameterValue(ShredDouble::ParameterIDs::bypass)->load(std::memory_order_relaxed) >= 0.5f;
  engine.setBypass(bypassed);

  // 4. Update targets for smoothed parameters
  const float targetTiming = apvts.getRawParameterValue(ShredDouble::ParameterIDs::timingSpread)->load(std::memory_order_relaxed);
  const float targetPitch = apvts.getRawParameterValue(ShredDouble::ParameterIDs::pitchSpread)->load(std::memory_order_relaxed);
  timingSpreadSmoother.setTargetValue(targetTiming);
  pitchSpreadSmoother.setTargetValue(targetPitch);

  for (int v = 0; v < 4; ++v) {
      juce::String panID = "panV" + juce::String(v + 1);
      const float targetPan = apvts.getRawParameterValue(panID)->load(std::memory_order_relaxed);
      panSmoothers[static_cast<size_t>(v)].setTargetValue(targetPan);
  }

  // 5. Process
  float* left = buffer.getWritePointer(0);
  float* right = outputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

  const bool needsSampleBySample = timingSpreadSmoother.isSmoothing() || pitchSpreadSmoother.isSmoothing() ||
                                   panSmoothers[0].isSmoothing() || panSmoothers[1].isSmoothing() ||
                                   panSmoothers[2].isSmoothing() || panSmoothers[3].isSmoothing();

  if (needsSampleBySample) {
      for (int sample = 0; sample < numSamples; ++sample) {
          engine.setTimingSpreadMs(timingSpreadSmoother.getNextValue());
          engine.setPitchSpreadCents(pitchSpreadSmoother.getNextValue());
          for (int v = 0; v < 4; ++v) {
              engine.setPanPercent(v, panSmoothers[static_cast<size_t>(v)].getNextValue());
          }
          engine.process(left + sample, right + sample, 1, inputChannels);
      }
  } else {
      engine.setTimingSpreadMs(timingSpreadSmoother.getCurrentValue());
      engine.setPitchSpreadCents(pitchSpreadSmoother.getCurrentValue());
      for (int v = 0; v < 4; ++v) {
          engine.setPanPercent(v, panSmoothers[static_cast<size_t>(v)].getCurrentValue());
      }
      engine.process(left, right, numSamples, inputChannels);
  }
}

void ShredDoubleAudioProcessor::updateReportedLatencyFromParameter() {
  setLatencySamples(engine.getLatencySamples());
}

juce::AudioProcessorEditor *ShredDoubleAudioProcessor::createEditor() {
  return new ShredDoubleAudioProcessorEditor(*this);
}

bool ShredDoubleAudioProcessor::hasEditor() const { return true; }

const juce::String ShredDoubleAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool ShredDoubleAudioProcessor::acceptsMidi() const { return false; }

bool ShredDoubleAudioProcessor::producesMidi() const { return false; }

bool ShredDoubleAudioProcessor::isMidiEffect() const { return false; }

double ShredDoubleAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ShredDoubleAudioProcessor::getNumPrograms() { return 1; }

int ShredDoubleAudioProcessor::getCurrentProgram() { return 0; }

void ShredDoubleAudioProcessor::setCurrentProgram(int) {}

const juce::String ShredDoubleAudioProcessor::getProgramName(int) { return {}; }

void ShredDoubleAudioProcessor::changeProgramName(int, const juce::String &) {}

void ShredDoubleAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
  StateManager stateManager;
  stateManager.getState(apvts, destData);
}

void ShredDoubleAudioProcessor::setStateInformation(const void *data,
                                                    int sizeInBytes) {
  apvts.state.removeListener(this);
  StateManager stateManager;
  if (stateManager.setState(apvts, data, sizeInBytes)) {
      uint64_t seedValue = stateManager.getMasterSeed(apvts);
      engine.seed(seedValue);
  }
  apvts.state.addListener(this);
  updateReportedLatencyFromParameter();
}

void ShredDoubleAudioProcessor::valueTreePropertyChanged (juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) {
  juce::ignoreUnused(treeWhosePropertyHasChanged);
  if (property == juce::Identifier (ShredDouble::ParameterIDs::masterSeed)) {
      StateManager stateManager;
      uint64_t seedValue = stateManager.getMasterSeed(apvts);
      engine.seed(seedValue);
  }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new ShredDoubleAudioProcessor();
}
