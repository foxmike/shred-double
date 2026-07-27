#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

class ShredDoubleAudioProcessorEditor;

class ShredDoubleAudioProcessor final : public juce::AudioProcessor {
public:
  ShredDoubleAudioProcessor();
  ~ShredDoubleAudioProcessor() override = default;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;
  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  juce::AudioProcessorValueTreeState &getAPVTS() noexcept { return apvts; }
  const juce::AudioProcessorValueTreeState &getAPVTS() const noexcept {
    return apvts;
  }

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();
  void updateReportedLatencyFromParameter();

  juce::AudioProcessorValueTreeState apvts;

  // Phase-A allocation spine. The later ShredDoubleEngine replaces these
  // raw reservations with its fixed four Voice objects without changing
  // the processor's worst-case, prepare-only allocation policy.
  static constexpr int maximumVoices = 4;
  static constexpr int studioPitchBufferSamples = 2048 + 4;
  static constexpr int studioLatencySamples = 1024;
  static constexpr int liveLatencySamples = 256;
  static constexpr int delayLagrangeGuard = 4;
  static constexpr int allpassStagesPerVoice = 4;
  static constexpr int meteringFifoFrames = 4096;

  std::array<std::vector<float>, maximumVoices> pitchBufferStorage;
  std::array<std::vector<float>, maximumVoices> driftingDelayStorage;
  std::array<std::array<std::uint32_t, allpassStagesPerVoice>, maximumVoices>
      allpassChainSlots{};
  std::array<std::vector<float>, 2> dryAlignmentStorage;
  std::vector<float> meteringFifoStorage;
  std::array<std::uint64_t, maximumVoices * 2> prngStreamStates{};

  double preparedSampleRate = 0.0;
  int preparedMaxBlockSize = 0;
  int preparedDelayBufferSamples = 0;
  bool lastLiveMode = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShredDoubleAudioProcessor)
};
