#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/ShredDoubleEngine.h"

class ShredDoubleAudioProcessorEditor;

class ShredDoubleAudioProcessor final : public juce::AudioProcessor,
                                        private juce::ValueTree::Listener {
public:
  ShredDoubleAudioProcessor();
  ~ShredDoubleAudioProcessor() override;

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

  ShredDoubleEngine& getEngine() noexcept { return engine; }
  const ShredDoubleEngine& getEngine() const noexcept { return engine; }

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout
  createParameterLayout();
  void updateReportedLatencyFromParameter();

  // ValueTree::Listener override
  void valueTreePropertyChanged (juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;

  juce::AudioProcessorValueTreeState apvts;

  ShredDoubleEngine engine;

  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> timingSpreadSmoother;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pitchSpreadSmoother;
  std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, 4> panSmoothers;

  double preparedSampleRate = 0.0;
  int preparedMaxBlockSize = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShredDoubleAudioProcessor)
};
