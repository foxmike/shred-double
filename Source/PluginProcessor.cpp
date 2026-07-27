#include "PluginProcessor.h"

#include "ParameterIDs.h"
#include "PluginEditor.h"

#include <atomic>
#include <cmath>
#include <memory>

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

  updateReportedLatencyFromParameter();
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

  // Reserve all four voices at Studio worst case. Voice-count and latency
  // automation will therefore only activate/repoint storage, never allocate.
  for (auto &pitchBuffer : pitchBufferStorage)
    pitchBuffer.assign(studioPitchBufferSamples, 0.0f);

  preparedDelayBufferSamples =
      juce::jmax(0, static_cast<int>(std::ceil(0.035 * sampleRate))) +
      delayLagrangeGuard;
  for (auto &delayBuffer : driftingDelayStorage)
    delayBuffer.assign(static_cast<std::size_t>(preparedDelayBufferSamples),
                       0.0f);

  // Four fixed allpass stage slots per voice; the later engine supplies the
  // sample-rate-scaled coprime delay storage and filter histories.
  allpassChainSlots = {};

  // One shared crossover is a fixed-state DSP object in the engine phase.
  // Keep two 1024-sample dry paths ready for Studio PDC alignment.
  for (auto &dryBuffer : dryAlignmentStorage)
    dryBuffer.assign(studioLatencySamples, 0.0f);

  // 4096 stereo frames and eight POD PRNG streams (voice x delay/pitch).
  meteringFifoStorage.assign(meteringFifoFrames * 2, 0.0f);
  prngStreamStates = {};

  updateReportedLatencyFromParameter();
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

  // Phase A is deliberately DSP-free. Preserve the input safely; for the
  // supported mono-to-stereo layout, duplicate mono after clearing the spare
  // output channel.
  if (inputChannels == 1 && outputChannels == 2 && numSamples > 0)
    buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

  const auto *latencyMode =
      apvts.getRawParameterValue(ShredDouble::ParameterIDs::latencyMode);
  const auto liveMode = latencyMode != nullptr &&
                        latencyMode->load(std::memory_order_relaxed) >= 0.5f;
  if (liveMode != lastLiveMode) {
    updateReportedLatencyFromParameter();
    updateHostDisplay();
  }
}

void ShredDoubleAudioProcessor::updateReportedLatencyFromParameter() {
  const auto *latencyMode =
      apvts.getRawParameterValue(ShredDouble::ParameterIDs::latencyMode);
  lastLiveMode = latencyMode != nullptr &&
                 latencyMode->load(std::memory_order_relaxed) >= 0.5f;
  setLatencySamples(lastLiveMode ? liveLatencySamples : studioLatencySamples);
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

void ShredDoubleAudioProcessor::getStateInformation(
    juce::MemoryBlock &destData) {
  auto state = apvts.copyState();
  state.setProperty(parameterLayoutVersionProperty,
                    ShredDouble::ParameterIDs::parameterLayoutVersion, nullptr);
  if (!state.hasProperty(ShredDouble::ParameterIDs::masterSeed))
    state.setProperty(ShredDouble::ParameterIDs::masterSeed, "0", nullptr);

  if (const auto xml = state.createXml())
    copyXmlToBinary(*xml, destData);
}

void ShredDoubleAudioProcessor::setStateInformation(const void *data,
                                                    int sizeInBytes) {
  if (const auto xml = getXmlFromBinary(data, sizeInBytes)) {
    auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid() || !state.hasType(apvts.state.getType()))
      return;

    const auto loadedVersion =
        static_cast<int>(state.getProperty(parameterLayoutVersionProperty, 1));
    if (loadedVersion > ShredDouble::ParameterIDs::schemaVersion)
      return;

    state.setProperty(parameterLayoutVersionProperty,
                      ShredDouble::ParameterIDs::parameterLayoutVersion,
                      nullptr);
    if (!state.hasProperty(ShredDouble::ParameterIDs::masterSeed))
      state.setProperty(ShredDouble::ParameterIDs::masterSeed, "0", nullptr);

    apvts.replaceState(state);
    updateReportedLatencyFromParameter();
  }
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new ShredDoubleAudioProcessor();
}
