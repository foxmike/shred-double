#pragma once

#include "MeterTap.h"
#include "MonoSafetyCrossover.h"
#include "Voice.h"
#include "WidthProcessor.h"

#include <array>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

/**
    Real-time-safe orchestrator for the four fixed ShredDouble voices.

    prepare() is the only method that allocates. Voice-count automation only
    changes gain ramps, and latency-mode automation only repoints the Studio-
    sized PitchShifter storage already owned by each Voice.
*/
class ShredDoubleEngine final {
public:
  enum class LatencyMode { Studio, Live };

  static constexpr int maximumVoices = 4;

  /** Allocates every voice and utility for the V=4 / Studio worst case. */
  void prepare(double sampleRate, int maximumBlockSize,
               LatencyMode latencyMode = LatencyMode::Studio);

  /** Clears DSP history while retaining parameters and the master seed. */
  void reset() noexcept;

  /** Deterministically reseeds and resets every voice. */
  void seed(std::uint64_t newMasterSeed) noexcept;
  [[nodiscard]] std::uint64_t getMasterSeed() const noexcept {
    return masterSeed;
  }

  /** Selects two or four voices. Other values are ignored. */
  void setVoiceCount(int newVoiceCount) noexcept;
  [[nodiscard]] int getVoiceCount() const noexcept { return voiceCount; }

  /**
      Requests a preallocated Studio/Live shifter ring. If audio has already
      run, the repoint occurs at the silent midpoint of a 10 ms wet mask.
  */
  void setLatencyMode(LatencyMode mode) noexcept;
  [[nodiscard]] LatencyMode getLatencyMode() const noexcept {
    return requestedLatencyMode;
  }
  [[nodiscard]] int getLatencySamples() const noexcept;

  // Block-rate controls. Proportion/factor forms match the DSP primitives:
  // tightness and mix are [0, 1], width is [0, 2], pans are [-100, 100].
  void setTimingSpreadMs(float milliseconds) noexcept;
  void setPitchSpreadCents(float cents) noexcept;
  void setHumanRate(float percent) noexcept;
  void setTightness(float proportion) noexcept;
  void setPanPercent(int voiceIndex, float percent) noexcept;
  void setPanPercents(float voice1, float voice2, float voice3,
                      float voice4) noexcept;
  void setMonoXoverHz(float hertz) noexcept;
  void setWidth(float factor) noexcept;
  void setWidthPercent(float percent) noexcept;
  void setMix(float wetProportion) noexcept;
  void setOutTrimDb(float decibels) noexcept;
  void setBypass(bool shouldBypass) noexcept { bypass = shouldBypass; }

  /**
      Processes separate input/output arrays. A null inR denotes mono input;
      outL/outR may alias their corresponding inputs.
  */
  void process(const float *inL, const float *inR, float *outL, float *outR,
               int numSamples) noexcept;

  /** In-place convenience overload; numInputChannels must be one or two. */
  void process(float *left, float *right, int numSamples,
               int numInputChannels) noexcept;

  MeterTap &getMeterTap() noexcept { return meter; }
  const MeterTap &getMeterTap() const noexcept { return meter; }

private:
  /**
      Integer stereo delay with two simultaneous taps. During latency-mode
      changes the taps crossfade, so PDC alignment changes without moving a
      read pointer or allocating on the audio thread.
  */
  class DryAlignDelay {
  public:
    void prepare();
    void reset() noexcept;
    void process(float inputLeft, float inputRight, int delayA, int delayB,
                 float blend, float &outputLeft, float &outputRight) noexcept;

  private:
    static constexpr int maximumDelaySamples = 1024;
    static constexpr int capacity = maximumDelaySamples + 1;

    juce::HeapBlock<float> leftBuffer;
    juce::HeapBlock<float> rightBuffer;
    int writePosition = 0;

    [[nodiscard]] int readIndex(int delaySamples) const noexcept;
  };

  static constexpr int studioPitchLength = 2048;
  static constexpr int livePitchLength = 512;
  static constexpr double gainRampSeconds = 0.02;
  static constexpr double switchCrossfadeSeconds = 0.01;

  std::array<Voice, maximumVoices> voices{Voice{0}, Voice{1}, Voice{2},
                                          Voice{3}};
  MonoSafetyCrossover crossover;
  WidthProcessor widthProcessor;
  DryAlignDelay dryAlign;
  MeterTap meter;

  std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
             maximumVoices>
      voiceGains;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;
  juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outTrimSmoothed;

  std::uint64_t masterSeed = 0;
  double sampleRate = 0.0;
  float timingSpreadMs = 6.0f;
  float pitchSpreadCents = 5.0f;
  float humanRate = 50.0f;
  float tightness = 0.5f;
  std::array<float, maximumVoices> panPercents{-100.0f, 100.0f, -60.0f, 60.0f};
  float monoXoverHz = 180.0f;
  float width = 1.0f;
  float targetMix = 0.5f;
  float targetOutTrimGain = 1.0f;
  int voiceCount = 2;
  int driftHopCounter = 0;
  bool bypass = false;

  LatencyMode requestedLatencyMode = LatencyMode::Studio;
  LatencyMode processingLatencyMode = LatencyMode::Studio;
  int transitionLengthSamples = 0;
  int transitionSamplesRemaining = 0;
  int transitionOldDryLatency = studioPitchLength / 2;
  bool transitionModeApplied = true;
  bool prepared = false;
  bool hasProcessedAudio = false;

  static int latencyForMode(LatencyMode mode) noexcept;
  static PitchShifter::LatencyMode pitchModeFor(LatencyMode mode) noexcept;

  void applyVoiceParameters() noexcept;
  void applyNominalDelayLayout() noexcept;
  void applyProcessingLatencyMode(LatencyMode mode) noexcept;
  void publishVoiceDrift() noexcept;
  void processFrame(float inputLeft, float inputRight, bool stereoInput,
                    float &outputLeft, float &outputRight) noexcept;
};
