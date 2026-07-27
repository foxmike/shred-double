#include "MeterTap.h"

#include <algorithm>
#include <cmath>

void MeterTap::prepare(double sampleRate, int maximumBlockSize) {
  jassert(sampleRate > 0.0);
  jassert(maximumBlockSize >= 0);
  juce::ignoreUnused(maximumBlockSize);

  const double validSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  correlationAlpha = static_cast<float>(
      1.0 - std::exp(-1.0 / (correlationTimeSeconds * validSampleRate)));
  decimationFactor =
      juce::jmax(1, static_cast<int>(std::lround(validSampleRate /
                                                 goniometerPairsPerSecond)));

  prepared = true;
  reset();
}

void MeterTap::reset() noexcept {
  pairFifo.reset();
  smoothedLeftRight = 0.0f;
  smoothedLeftSquared = 0.0f;
  smoothedRightSquared = 0.0f;
  decimationCounter = 0;
  correlation.store(0.0f, std::memory_order_relaxed);

  for (auto &value : voiceDelayMs)
    value.store(0.0f, std::memory_order_relaxed);
  for (auto &value : voiceDetuneCents)
    value.store(0.0f, std::memory_order_relaxed);
}

void MeterTap::processSample(float left, float right) noexcept {
  if (!prepared)
    return;

  smoothedLeftRight += correlationAlpha * (left * right - smoothedLeftRight);
  smoothedLeftSquared += correlationAlpha * (left * left - smoothedLeftSquared);
  smoothedRightSquared +=
      correlationAlpha * (right * right - smoothedRightSquared);

  const float denominator =
      std::sqrt(juce::jmax(0.0f, smoothedLeftSquared * smoothedRightSquared)) +
      silenceEpsilon;
  const float rho = juce::jlimit(-1.0f, 1.0f, smoothedLeftRight / denominator);
  correlation.store(rho, std::memory_order_relaxed);

  if (++decimationCounter < decimationFactor)
    return;

  decimationCounter = 0;

  int start1 = 0;
  int size1 = 0;
  int start2 = 0;
  int size2 = 0;
  pairFifo.prepareToWrite(1, start1, size1, start2, size2);

  if (size1 > 0) {
    leftPairs[static_cast<size_t>(start1)] = left;
    rightPairs[static_cast<size_t>(start1)] = right;
    pairFifo.finishedWrite(1);
  } else if (size2 > 0) {
    leftPairs[static_cast<size_t>(start2)] = left;
    rightPairs[static_cast<size_t>(start2)] = right;
    pairFifo.finishedWrite(1);
  }
  // Drop the newest display pair when full; the audio thread never waits.
}

void MeterTap::process(const float *left, const float *right,
                       int numSamples) noexcept {
  if (left == nullptr || right == nullptr || numSamples <= 0)
    return;

  for (int sample = 0; sample < numSamples; ++sample)
    processSample(left[sample], right[sample]);
}

float MeterTap::getCorrelation() const noexcept {
  return correlation.load(std::memory_order_relaxed);
}

void MeterTap::setVoiceDrift(int voiceIndex, float delayMs,
                             float detuneCents) noexcept {
  if (voiceIndex < 0 || voiceIndex >= maximumVoices)
    return;

  const auto index = static_cast<size_t>(voiceIndex);
  voiceDelayMs[index].store(delayMs, std::memory_order_relaxed);
  voiceDetuneCents[index].store(detuneCents, std::memory_order_relaxed);
}

float MeterTap::getVoiceDelayMs(int voiceIndex) const noexcept {
  if (voiceIndex < 0 || voiceIndex >= maximumVoices)
    return 0.0f;

  return voiceDelayMs[static_cast<size_t>(voiceIndex)].load(
      std::memory_order_relaxed);
}

float MeterTap::getVoiceDetuneCents(int voiceIndex) const noexcept {
  if (voiceIndex < 0 || voiceIndex >= maximumVoices)
    return 0.0f;

  return voiceDetuneCents[static_cast<size_t>(voiceIndex)].load(
      std::memory_order_relaxed);
}

int MeterTap::popPairs(float *leftDestination, float *rightDestination,
                       int maxPairs) noexcept {
  if (maxPairs <= 0)
    return 0;

  int start1 = 0;
  int size1 = 0;
  int start2 = 0;
  int size2 = 0;
  pairFifo.prepareToRead(maxPairs, start1, size1, start2, size2);

  if (leftDestination != nullptr) {
    std::copy_n(leftPairs.data() + start1, size1, leftDestination);
    std::copy_n(leftPairs.data() + start2, size2, leftDestination + size1);
  }

  if (rightDestination != nullptr) {
    std::copy_n(rightPairs.data() + start1, size1, rightDestination);
    std::copy_n(rightPairs.data() + start2, size2, rightDestination + size1);
  }

  const int numRead = size1 + size2;
  pairFifo.finishedRead(numRead);
  return numRead;
}

int MeterTap::getNumPairsReady() const noexcept {
  return pairFifo.getNumReady();
}
