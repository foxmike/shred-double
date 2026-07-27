#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

/**
    Real-time-safe, single-producer/single-consumer metering transport.

    The audio thread updates the correlation, voice drift values, and
    goniometer FIFO. The UI thread reads the atomics and drains popPairs().
    prepare() and reset() must only be called while those threads are stopped.
*/
class MeterTap final {
public:
  static constexpr int maximumVoices = 4;
  static constexpr int fifoCapacity = 4096;

  /** Configures sample-rate-dependent smoothing and display decimation. */
  void prepare(double sampleRate, int maximumBlockSize = 0);

  /** Clears correlation statistics, voice values, and queued display pairs. */
  void reset() noexcept;

  /** Updates all output meters from one final stereo output sample. */
  void processSample(float left, float right) noexcept;

  /** Updates all output meters from contiguous final stereo output blocks. */
  void process(const float *left, const float *right, int numSamples) noexcept;

  /** Returns the latest smoothed Pearson correlation in [-1, 1]. */
  [[nodiscard]] float getCorrelation() const noexcept;

  /** Publishes one voice's current timing delay and pitch detune. */
  void setVoiceDrift(int voiceIndex, float delayMs, float detuneCents) noexcept;

  /** Loads one voice's most recently published timing delay. */
  [[nodiscard]] float getVoiceDelayMs(int voiceIndex) const noexcept;

  /** Loads one voice's most recently published pitch detune. */
  [[nodiscard]] float getVoiceDetuneCents(int voiceIndex) const noexcept;

  /**
      Drains up to maxPairs complete stereo frames into caller-owned storage.

      Either destination may be null; the returned number of pairs is consumed
      in either case. This is the UI-thread side of the SPSC FIFO.
  */
  [[nodiscard]] int popPairs(float *leftDestination, float *rightDestination,
                             int maxPairs) noexcept;

  [[nodiscard]] int getNumPairsReady() const noexcept;

private:
  // AbstractFifo reserves one slot to distinguish full from empty.
  static constexpr int fifoBackingSize = fifoCapacity + 1;
  static constexpr double correlationTimeSeconds = 0.3;
  static constexpr double goniometerPairsPerSecond = 6000.0;
  static constexpr float silenceEpsilon = 1.0e-12f;

  std::array<float, fifoBackingSize> leftPairs{};
  std::array<float, fifoBackingSize> rightPairs{};
  juce::AbstractFifo pairFifo{fifoBackingSize};

  std::atomic<float> correlation{0.0f};
  std::array<std::atomic<float>, maximumVoices> voiceDelayMs{};
  std::array<std::atomic<float>, maximumVoices> voiceDetuneCents{};

  float smoothedLeftRight = 0.0f;
  float smoothedLeftSquared = 0.0f;
  float smoothedRightSquared = 0.0f;
  float correlationAlpha = 0.0f;
  int decimationFactor = 1;
  int decimationCounter = 0;
  bool prepared = false;
};
