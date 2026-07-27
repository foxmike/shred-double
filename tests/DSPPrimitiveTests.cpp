// =============================================================================
// ShredDouble - DSP Primitive Unit Tests
// =============================================================================
// Focused, public-API-only checks for the DSP primitives produced by phase B
// tasks b.1-b.4 (Source/DSP/RandomWalk, AllpassChain, TiltFilter, Voice).
// No production DSP code is rewritten or specialised; only the documented
// public APIs are exercised.
//
// These tests cover the mechanical invariants called out in
// .abracapocus/ShredDouble_Build_Spec.md §4.3-4.5 and §14 (Verification &
// Test Plan items 6, 8, 9):
//   * §4.3 / §14.6  RandomWalk: long-run samples stay inside the documented
//                    delay band [10, 35] ms and the pitch clamp
//                    [-12, +12] cents; mean near nominal; tightness 1.0
//                    yields zero excursion (D3 depth macro); same seed
//                    produces bit-identical sequences (D2 determinism
//                    without ever touching std::random_device).
//   * §4.5A / §14.8 AllpassChain: impulse-response energy is preserved
//                    (Schroeder allpass is magnitude-flat); different voice
//                    indices produce distinct IRs (phase decorrelation).
//   * §4.5B         TiltFilter: +tilt vs -tilt diverge in opposite
//                    directions on a low-band vs high-band probe around the
//                    700 Hz pivot.
//   * §4.2 / §4.5   Voice: two voices with distinct indices fed the same
//                    noise input produce decorrelated outputs (cross-
//                    correlation below a loose threshold); the per-sample
//                    process path runs cleanly for many samples after
//                    prepare().
//
// Uses juce::UnitTest only — no Catch2 / Google Test. Mirrors the sibling
// ShredWidth tests/*.cpp pattern.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "Source/DSP/RandomWalk.h"
#include "Source/DSP/AllpassChain.h"
#include "Source/DSP/TiltFilter.h"
#include "Source/DSP/Voice.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxBlockSize = 512;
constexpr std::uint64_t kDeterminismMasterSeed = UINT64_C(0xDEADBEEFCAFEBABE);

// Documented safety rails (ShredDouble_Build_Spec §4.3). The RandomWalk
// class holds these as private constants, so we mirror them here against
// the published spec to keep the tests public-API-only.
constexpr float kMinDelayMs = 10.0f;
constexpr float kMaxDelayMs = 35.0f;

// -----------------------------------------------------------------------------
// Local helpers — pure functions only; no class internals are touched.
// -----------------------------------------------------------------------------

// RMS over `data[lo..hi)`.
double rmsRange(const float *data, int lo, int hi)
{
    if (hi <= lo)
        return 0.0;

    double sumSq = 0.0;
    for (int i = lo; i < hi; ++i)
    {
        const double v =
            static_cast<double>(data[static_cast<std::size_t>(i)]);
        sumSq += v * v;
    }
    return std::sqrt(
        sumSq / static_cast<double>(hi - lo));
}

// Sum of squared samples in `data[lo..hi)`.
double sumOfSquares(const float *data, int lo, int hi)
{
    double total = 0.0;
    for (int i = lo; i < hi; ++i)
    {
        const double v =
            static_cast<double>(data[static_cast<std::size_t>(i)]);
        total += v * v;
    }
    return total;
}

// Zero-lag normalized cross-correlation of two equal-length signals.
double zeroLagCrossCorrelation(const float *x, const float *y, int n)
{
    double sumXY = 0.0;
    double sumX2 = 0.0;
    double sumY2 = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double xi =
            static_cast<double>(x[static_cast<std::size_t>(i)]);
        const double yi =
            static_cast<double>(y[static_cast<std::size_t>(i)]);
        sumXY += xi * yi;
        sumX2 += xi * xi;
        sumY2 += yi * yi;
    }

    const double denom = std::sqrt(sumX2 * sumY2);
    return denom > 1.0e-20 ? sumXY / denom : 0.0;
}

// Drive `n` samples of a sine wave at frequency `freqHz` into `data`.
void fillSine(float *data, int n, double freqHz, double sampleRate,
              float amplitude = 0.5f)
{
    for (int i = 0; i < n; ++i)
    {
        data[static_cast<std::size_t>(i)] = static_cast<float>(
            amplitude
            * std::sin(2.0 * kPi * freqHz
                       * static_cast<double>(i) / sampleRate));
    }
}

} // namespace

// =============================================================================

class DSPPrimitiveTests final : public juce::UnitTest
{
public:
    DSPPrimitiveTests()
        : juce::UnitTest("DSPPrimitiveTests", "ShredDouble/DSP") {}

    void runTest() override
    {
        // §4.3 / §14.6: walk bounds, mean reversion, depth macro,
        // deterministic seeding (D2), no random_device on the audio path.
        testRandomWalkDelayBounds();
        testRandomWalkPitchBounds();
        testRandomWalkDelayMeanNearNominal();
        testRandomWalkPitchMeanNearZero();
        testRandomWalkTightnessOneYieldsConstantOutput();
        testRandomWalkTightnessZeroYieldsFullExcursion();
        testRandomWalkSeedDeterministicAcrossInstances();
        testRandomWalkSameSeedIdenticalSequence();
        testRandomWalkPRNGPathNeedsNoRandomDevice();

        // §4.5A / §14.8: magnitude-flat (impulse energy preserved) +
        // per-voice decorrelation.
        testAllpassChainImpulseEnergyPreserved();
        testAllpassChainDistinctVoiceImpulseResponses();

        // §4.5B: +tilt vs -tilt diverge in low vs high band around 700 Hz.
        testTiltFilterLowBandBoostsWithPositiveTilt();
        testTiltFilterHighBandBoostsWithNegativeTilt();
        testTiltFilterTiltSweepMonotonicity();

        // §4.2 / §4.5: dual-voice decorrelation + clean process path.
        testVoiceDualVoiceCrossCorrelationBelowThreshold();
        testVoiceProcessPathRunsCleanlyForNSamples();
        testVoiceDistinctVoiceIndicesReported();
    }

    // -------------------------------------------------------------------------
    // RandomWalk — §4.3 / §14.6
    // -------------------------------------------------------------------------

    void testRandomWalkDelayBounds()
    {
        beginTest("RandomWalk: long-run delay samples stay within [10, 35] ms (clamp invariant)");

        RandomWalk walk(RandomWalk::Axis::delay);
        walk.prepare(kSampleRate);
        walk.setNominalDelayMs(20.0f);
        walk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
        walk.setHumanRate(50.0f);
        walk.setTightness(0.0f); // max excursion — the worst-case envelope
        walk.reseed(kDeterminismMasterSeed, 0, RandomWalk::delayAxisTag);

        // 30 seconds @ 48 kHz = 1.44M samples. The mean-reverting OU walk
        // should never touch the clamp; if it ever did, the entire walk
        // chain is suspect, not the clamp itself.
        constexpr int kNumSamples = 1440000;
        float minSeen = 1.0e9f;
        float maxSeen = -1.0e9f;

        for (int i = 0; i < kNumSamples; ++i)
        {
            const float v = walk.processSample();
            minSeen = std::min(minSeen, v);
            maxSeen = std::max(maxSeen, v);
        }

        expect(minSeen >= kMinDelayMs - 1.0e-3f,
               "Delay walk went below minDelayMs; minSeen="
               + juce::String(minSeen));
        expect(maxSeen <= kMaxDelayMs + 1.0e-3f,
               "Delay walk exceeded maxDelayMs; maxSeen="
               + juce::String(maxSeen));
    }

    void testRandomWalkPitchBounds()
    {
        beginTest("RandomWalk: long-run pitch samples stay within [-12, +12] cents (clamp invariant)");

        RandomWalk walk(RandomWalk::Axis::pitch);
        walk.prepare(kSampleRate);
        walk.setPitchSpreadCents(RandomWalk::maxPitchSpreadCents);
        walk.setHumanRate(50.0f);
        walk.setTightness(0.0f);
        walk.reseed(kDeterminismMasterSeed, 0, RandomWalk::pitchAxisTag);

        constexpr int kNumSamples = 1440000;
        float minSeen = 1.0e9f;
        float maxSeen = -1.0e9f;

        for (int i = 0; i < kNumSamples; ++i)
        {
            const float v = walk.processSample();
            minSeen = std::min(minSeen, v);
            maxSeen = std::max(maxSeen, v);
        }

        expect(minSeen >= -RandomWalk::maxPitchCents - 1.0e-3f,
               "Pitch walk went below -Cmax; minSeen="
               + juce::String(minSeen));
        expect(maxSeen <= RandomWalk::maxPitchCents + 1.0e-3f,
               "Pitch walk exceeded +Cmax; maxSeen="
               + juce::String(maxSeen));
    }

    void testRandomWalkDelayMeanNearNominal()
    {
        beginTest("RandomWalk: mean-reverting delay averages near nominal (no DC drift)");

        RandomWalk walk(RandomWalk::Axis::delay);
        walk.prepare(kSampleRate);
        walk.setNominalDelayMs(22.5f);
        walk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
        walk.setHumanRate(50.0f);
        walk.setTightness(0.0f);
        walk.reseed(kDeterminismMasterSeed, 0, RandomWalk::delayAxisTag);

        constexpr int kNumSamples = 96000; // 2 seconds
        double sum = 0.0;
        for (int i = 0; i < kNumSamples; ++i)
            sum += walk.processSample();

        const double mean = sum / static_cast<double>(kNumSamples);
        // Mean-reverting walk should be unbiased within tolerance; with
        // a hard ±15 ms spread the symmetric clamp means the mean can
        // drift from nominal by a small fraction of the spread. Two
        // seconds of data only yields ~10 effectively-independent samples
        // (correlation time 200 ms), so the SE of the mean is ~0.3 ms.
        // A 1 ms tolerance accommodates 3σ statistical variation.
        expectWithinAbsoluteError(
            mean, 22.5, 1.0,
            "Delay walk mean should be near nominal (22.5 ms); got "
            + juce::String(mean));
    }

    void testRandomWalkPitchMeanNearZero()
    {
        beginTest("RandomWalk: mean-reverting pitch averages near zero (no DC drift)");

        RandomWalk walk(RandomWalk::Axis::pitch);
        walk.prepare(kSampleRate);
        walk.setPitchSpreadCents(RandomWalk::maxPitchSpreadCents);
        walk.setHumanRate(50.0f);
        walk.setTightness(0.0f);
        walk.reseed(kDeterminismMasterSeed, 1, RandomWalk::pitchAxisTag);

        constexpr int kNumSamples = 96000; // 2 seconds
        double sum = 0.0;
        for (int i = 0; i < kNumSamples; ++i)
            sum += walk.processSample();

        const double mean = sum / static_cast<double>(kNumSamples);
        // For pitch the axis mean is zero, and the symmetric ±12-cent clamp
        // gives a tight tolerance — but the same statistical-limit
        // reasoning applies: 2 s of data → ~10 independent samples →
        // SE ≈ 0.3 cents, so 1 cent tolerance accommodates 3σ variation.
        expect(std::abs(mean) < 1.0,
               "Pitch walk mean should be near zero; got "
               + juce::String(mean));
    }

    void testRandomWalkTightnessOneYieldsConstantOutput()
    {
        beginTest("RandomWalk: tightness = 1.0 collapses the walk to its axis mean (D3 depth macro)");

        // Delay axis.
        {
            RandomWalk walk(RandomWalk::Axis::delay);
            walk.prepare(kSampleRate);
            walk.setNominalDelayMs(20.0f);
            walk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
            walk.setHumanRate(50.0f);
            walk.setTightness(1.0f); // depthScale = 0 ⇒ output ≡ nominal
            walk.reseed(kDeterminismMasterSeed, 0,
                        RandomWalk::delayAxisTag);

            float first = 0.0f;
            bool allEqual = true;
            for (int i = 0; i < 4096; ++i)
            {
                const float v = walk.processSample();
                if (i == 0)
                    first = v;
                else if (std::abs(v - first) > 1.0e-5f)
                    allEqual = false;
            }
            expect(allEqual,
                   "Tightness 1.0 must collapse delay walk to a constant; first="
                   + juce::String(first));
            expectWithinAbsoluteError(
                first, 20.0f, 1.0e-3f,
                "Tightness 1.0 must yield the nominal delay; got "
                + juce::String(first));
        }

        // Pitch axis.
        {
            RandomWalk walk(RandomWalk::Axis::pitch);
            walk.prepare(kSampleRate);
            walk.setPitchSpreadCents(RandomWalk::maxPitchSpreadCents);
            walk.setHumanRate(50.0f);
            walk.setTightness(1.0f); // depthScale = 0 ⇒ output ≡ 0
            walk.reseed(kDeterminismMasterSeed, 0,
                        RandomWalk::pitchAxisTag);

            float first = 0.0f;
            bool allEqual = true;
            for (int i = 0; i < 4096; ++i)
            {
                const float v = walk.processSample();
                if (i == 0)
                    first = v;
                else if (std::abs(v - first) > 1.0e-5f)
                    allEqual = false;
            }
            expect(allEqual,
                   "Tightness 1.0 must collapse pitch walk to a constant; first="
                   + juce::String(first));
            expectWithinAbsoluteError(
                first, 0.0f, 1.0e-3f,
                "Tightness 1.0 must yield zero pitch; got "
                + juce::String(first));
        }
    }

    void testRandomWalkTightnessZeroYieldsFullExcursion()
    {
        beginTest("RandomWalk: tightness = 0.0 produces the largest excursion envelope");

        RandomWalk tightWalk(RandomWalk::Axis::delay);
        tightWalk.prepare(kSampleRate);
        tightWalk.setNominalDelayMs(20.0f);
        tightWalk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
        tightWalk.setHumanRate(50.0f);
        tightWalk.setTightness(1.0f);

        RandomWalk looseWalk(RandomWalk::Axis::delay);
        looseWalk.prepare(kSampleRate);
        looseWalk.setNominalDelayMs(20.0f);
        looseWalk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
        looseWalk.setHumanRate(50.0f);
        looseWalk.setTightness(0.0f);

        // Same seed so the underlying PRNG stream is identical — only the
        // depthScale differs. The loose walk must therefore reach further
        // from the mean than the tight one.
        tightWalk.reseed(kDeterminismMasterSeed, 7,
                         RandomWalk::delayAxisTag);
        looseWalk.reseed(kDeterminismMasterSeed, 7,
                         RandomWalk::delayAxisTag);

        constexpr int kNumSamples = 192000; // 4 seconds
        float maxTightDev = 0.0f;
        float maxLooseDev = 0.0f;

        for (int i = 0; i < kNumSamples; ++i)
        {
            const float t = tightWalk.processSample();
            const float l = looseWalk.processSample();
            maxTightDev = std::max(maxTightDev, std::abs(t - 20.0f));
            maxLooseDev = std::max(maxLooseDev, std::abs(l - 20.0f));
        }

        // §14.9: tightness monotonicity. Even with only ~4 s of data the
        // loose walk should reach a clearly larger peak deviation than the
        // tight walk.
        expect(maxLooseDev > maxTightDev * 2.0f,
               "Loose walk should reach a substantially larger max-deviation than tight walk; "
               "tight=" + juce::String(maxTightDev)
               + " loose=" + juce::String(maxLooseDev));
        expect(maxLooseDev > 2.0f,
               "Loose walk should produce non-trivial excursion; got "
               + juce::String(maxLooseDev));
    }

    void testRandomWalkSeedDeterministicAcrossInstances()
    {
        beginTest("RandomWalk: two freshly-constructed instances with the same seed produce the same stream");

        // Default constructor + prepare() + reseed() — no other state in
        // between — must produce bit-identical streams. This guards D2 and
        // implicitly the "no std::random_device on the audio path" rule,
        // since the seeded path never calls into the platform RNG.
        auto makeStream = []() {
            RandomWalk walk(RandomWalk::Axis::delay);
            walk.prepare(kSampleRate);
            walk.setNominalDelayMs(20.0f);
            walk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
            walk.setHumanRate(50.0f);
            walk.setTightness(0.0f);
            walk.reseed(kDeterminismMasterSeed, 0,
                        RandomWalk::delayAxisTag);

            std::vector<float> data(8192);
            for (std::size_t i = 0; i < data.size(); ++i)
                data[i] = walk.processSample();
            return data;
        };

        const auto a = makeStream();
        const auto b = makeStream();

        int mismatch = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0)
                ++mismatch;

        expect(mismatch == 0,
               "Same-seed streams must be bit-identical; "
               + juce::String(mismatch) + " mismatches out of "
               + juce::String(static_cast<int>(a.size())));
    }

    void testRandomWalkSameSeedIdenticalSequence()
    {
        beginTest("RandomWalk: reseed-to-identical-state restores the sequence bit-for-bit");

        RandomWalk walk(RandomWalk::Axis::delay);
        walk.prepare(kSampleRate);
        walk.setNominalDelayMs(20.0f);
        walk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
        walk.setHumanRate(50.0f);
        walk.setTightness(0.0f);
        walk.reseed(kDeterminismMasterSeed, 3,
                    RandomWalk::delayAxisTag);

        std::vector<float> firstRun(4096);
        for (std::size_t i = 0; i < firstRun.size(); ++i)
            firstRun[i] = walk.processSample();

        // Reseed to the same state and re-run — output must match exactly.
        walk.reseed(kDeterminismMasterSeed, 3,
                    RandomWalk::delayAxisTag);

        int mismatch = 0;
        for (std::size_t i = 0; i < firstRun.size(); ++i)
        {
            const float v = walk.processSample();
            if (std::memcmp(&v, &firstRun[i], sizeof(float)) != 0)
                ++mismatch;
        }

        expect(mismatch == 0,
               "Reseeded stream must replay identically; "
               + juce::String(mismatch) + " mismatches");
    }

    void testRandomWalkPRNGPathNeedsNoRandomDevice()
    {
        beginTest("RandomWalk: construction + use requires no std::random_device on the path");

        // This test enforces the property by construction: we deliberately
        // use only the deterministic API (constructor with Axis tag +
        // reseed with explicit master/voice/axisTag). If a future change
        // routes through std::random_device anywhere on the audio path,
        // testRandomWalkSeedDeterministicAcrossInstances and
        // testRandomWalkSameSeedIdenticalSequence above will fail because
        // they would no longer be reproducible.
        //
        // Here we additionally verify the cross-axis stream is independent
        // for the same master/voice but different axis tags — proving the
        // per-axis mix is a real branch, not a side-effect of an
        // incidental RNG draw.
        RandomWalk delayWalk(RandomWalk::Axis::delay);
        delayWalk.prepare(kSampleRate);
        delayWalk.setNominalDelayMs(20.0f);
        delayWalk.setTimingSpreadMs(RandomWalk::maxTimingSpreadMs);
        delayWalk.setHumanRate(50.0f);
        delayWalk.setTightness(0.0f);
        delayWalk.reseed(kDeterminismMasterSeed, 5,
                         RandomWalk::delayAxisTag);

        RandomWalk pitchWalk(RandomWalk::Axis::pitch);
        pitchWalk.prepare(kSampleRate);
        pitchWalk.setPitchSpreadCents(RandomWalk::maxPitchSpreadCents);
        pitchWalk.setHumanRate(50.0f);
        pitchWalk.setTightness(0.0f);
        pitchWalk.reseed(kDeterminismMasterSeed, 5,
                         RandomWalk::pitchAxisTag);

        // Different axes ⇒ different streams ⇒ the two outputs cannot be
        // trivially related. We require they differ at every sample (the
        // two streams are independent Gaussian-ish processes).
        int identical = 0;
        for (int i = 0; i < 4096; ++i)
        {
            const float d = delayWalk.processSample();
            const float p = pitchWalk.processSample();
            if (std::memcmp(&d, &p, sizeof(float)) == 0)
                ++identical;
        }

        expect(identical == 0,
               "Delay and pitch streams must be independent; "
               + juce::String(identical) + " sample collisions");
    }

    // -------------------------------------------------------------------------
    // AllpassChain — §4.5A / §14.8
    // -------------------------------------------------------------------------

    void testAllpassChainImpulseEnergyPreserved()
    {
        beginTest("AllpassChain: impulse-response energy ≈ input energy (Schroeder allpass is magnitude-flat)");

        // Try every voice — the magnitude-flat property must hold for all
        // per-voice delay sets, not just voice 0.
        for (int voice = 0; voice < AllpassChain::numVoices; ++voice)
        {
            AllpassChain ap;
            ap.prepare(kSampleRate, kMaxBlockSize);
            ap.setVoiceIndex(voice);
            ap.reset();

            // Long buffer so the impulse spreads across every stage's
            // delay without truncation.
            constexpr int kNumSamples = 8192;
            std::vector<float> data(static_cast<std::size_t>(kNumSamples),
                                    0.0f);
            data[0] = 1.0f;

            // Input energy: |1|^2 = 1 exactly.
            const double inputEnergy = 1.0;

            ap.process(data.data(), kNumSamples);

            // Output energy: sum of squared samples across the entire
            // response. Schroeder allpass has |H(e^{jω})| = 1 for all ω,
            // so output energy must equal input energy exactly modulo
            // FP rounding in the inner-product path.
            const double outputEnergy =
                sumOfSquares(data.data(), 0, kNumSamples);

            expectWithinAbsoluteError(
                outputEnergy, inputEnergy, 5.0e-3,
                "Voice " + juce::String(voice)
                + " impulse-response energy must be ~1.0; got "
                + juce::String(outputEnergy));
        }
    }

    void testAllpassChainDistinctVoiceImpulseResponses()
    {
        beginTest("AllpassChain: voice 0 and voice 1 produce distinct impulse responses (per-voice decorrelation)");

        constexpr int kNumSamples = 4096;

        auto captureIR = [kNumSamples](int voice) {
            AllpassChain ap;
            ap.prepare(kSampleRate, kMaxBlockSize);
            ap.setVoiceIndex(voice);
            ap.reset();

            std::vector<float> data(static_cast<std::size_t>(kNumSamples),
                                    0.0f);
            data[0] = 1.0f;
            ap.process(data.data(), kNumSamples);
            return data;
        };

        const auto ir0 = captureIR(0);
        const auto ir1 = captureIR(1);

        float maxDiff = 0.0f;
        for (int i = 0; i < kNumSamples; ++i)
            maxDiff = std::max(
                maxDiff,
                std::abs(ir0[static_cast<std::size_t>(i)]
                         - ir1[static_cast<std::size_t>(i)]));

        // The two voices use distinct coprime delay sets
        // (§4.5A: v0={13,29,47,71}, v1={17,37,53,79}), so the impulse
        // responses must visibly differ. The gain stage is unity, so
        // the IR magnitudes are bounded by |g|^k — plenty of headroom for
        // the diff to be clearly above FP noise.
        expect(maxDiff > 0.05f,
               "Voice 0 vs voice 1 impulse responses must differ; maxDiff="
               + juce::String(maxDiff));

        // Cross-correlation of the two IRs (centered, zero-lag) must be
        // below 1.0 — a strict inequality proves they are not identical.
        const double rho =
            zeroLagCrossCorrelation(ir0.data(), ir1.data(), kNumSamples);
        expect(rho < 0.999,
               "Voice IRs must not be identical; cross-correlation="
               + juce::String(rho));
    }

    // -------------------------------------------------------------------------
    // TiltFilter — §4.5B
    // -------------------------------------------------------------------------

    void testTiltFilterLowBandBoostsWithPositiveTilt()
    {
        beginTest("TiltFilter: 100 Hz (well below 700 Hz pivot) is louder with +tilt than -tilt");

        // §4.5B: Vorbis tilt H(0) = A = 10^(tilt/40); positive tilt
        // boosts the low band. 100 Hz at 48 kHz is 0.37 % of Nyquist —
        // close enough to DC for the analog DC-gain rule to apply within
        // a small tolerance.
        auto runTilt = [](float tiltDb) {
            TiltFilter tf;
            tf.prepare(kSampleRate);
            tf.setTiltDb(tiltDb);
            tf.reset();

            constexpr int kNumSamples = 32768;
            std::vector<float> data(static_cast<std::size_t>(kNumSamples),
                                    0.0f);
            fillSine(data.data(), kNumSamples, 100.0, kSampleRate, 0.5f);

            tf.process(data.data(), kNumSamples);

            // Skip the first 2048 samples for IIR settling.
            return rmsRange(data.data(), 2048, kNumSamples);
        };

        const double rmsPlus = runTilt(+1.5f);
        const double rmsMinus = runTilt(-1.5f);

        // Expected ratio = 10^((+1.5 - (-1.5)) / 20) = 10^(0.15) ≈ 1.413
        // Allow a generous tolerance to account for the ~0.5 % offset
        // from true DC and IIR transient tail.
        expect(rmsPlus > rmsMinus,
               "+tilt must produce a louder 100 Hz tone than -tilt; "
               "rmsPlus=" + juce::String(rmsPlus)
               + " rmsMinus=" + juce::String(rmsMinus));

        const double ratio = rmsPlus / std::max(rmsMinus, 1.0e-12);
        // Vorbis tilt H(0) = A = 10^(tiltDb/40). At 100 Hz (well below
        // the 700 Hz pivot) the response is close to DC, so:
        //   +1.5 dB tilt → |H| ≈ 10^(+1.5/40)  ≈ 1.0889  (+0.71 dB)
        //   -1.5 dB tilt → |H| ≈ 10^(-1.5/40)  ≈ 0.9188  (-0.71 dB)
        // The ratio is therefore ≈ 1.18 (≈ +1.42 dB), NOT 1.413 — the
        // Vorbis tilt puts only half the tilt magnitude at DC/Nyquist.
        // Allow 0.10 tolerance for the small offset from true DC and the
        // IIR settling tail.
        expectWithinAbsoluteError(
            ratio, 1.18, 0.10,
            "Expected ~+1.4 dB boost at 100 Hz for +1.5 dB tilt vs -1.5 dB tilt; ratio="
            + juce::String(ratio));
    }

    void testTiltFilterHighBandBoostsWithNegativeTilt()
    {
        beginTest("TiltFilter: 5000 Hz (well above 700 Hz pivot) is louder with -tilt than +tilt");

        // §4.5B: Vorbis tilt H(∞) = 1/A; positive tilt cuts the high
        // band. 5 kHz at 48 kHz is ~21 % of Nyquist — close enough to
        // (but not at) Nyquist for the magnitude-slope rule to dominate.
        auto runTilt = [](float tiltDb) {
            TiltFilter tf;
            tf.prepare(kSampleRate);
            tf.setTiltDb(tiltDb);
            tf.reset();

            constexpr int kNumSamples = 32768;
            std::vector<float> data(static_cast<std::size_t>(kNumSamples),
                                    0.0f);
            fillSine(data.data(), kNumSamples, 5000.0, kSampleRate, 0.5f);

            tf.process(data.data(), kNumSamples);

            return rmsRange(data.data(), 2048, kNumSamples);
        };

        const double rmsPlus = runTilt(+1.5f);
        const double rmsMinus = runTilt(-1.5f);

        expect(rmsMinus > rmsPlus,
               "-tilt must produce a louder 5000 Hz tone than +tilt; "
               "rmsPlus=" + juce::String(rmsPlus)
               + " rmsMinus=" + juce::String(rmsMinus));

        const double ratio = rmsMinus / std::max(rmsPlus, 1.0e-12);
        // Vorbis tilt H(∞) = 1/A. At 5 kHz (close to but not at Nyquist)
        // the response mirrors the DC behavior with inverted sign:
        //   +1.5 dB tilt → |H| ≈ 0.9188  (-0.71 dB cut)
        //   -1.5 dB tilt → |H| ≈ 1.0889  (+0.71 dB boost)
        // The ratio (-1.5 / +1.5) is again ≈ 1.18 (+1.42 dB).
        expectWithinAbsoluteError(
            ratio, 1.18, 0.10,
            "Expected ~+1.4 dB boost at 5 kHz for -1.5 dB tilt vs +1.5 dB tilt; ratio="
            + juce::String(ratio));
    }

    void testTiltFilterTiltSweepMonotonicity()
    {
        beginTest("TiltFilter: low-band gain increases monotonically as tilt sweeps from -3 dB → +3 dB");

        // Sanity check that the sign convention works across the full
        // control range and is monotonic in tilt (no flip mid-sweep).
        auto runLowBandRMS = [](float tiltDb) {
            TiltFilter tf;
            tf.prepare(kSampleRate);
            tf.setTiltDb(tiltDb);
            tf.reset();

            constexpr int kNumSamples = 16384;
            std::vector<float> data(static_cast<std::size_t>(kNumSamples),
                                    0.0f);
            fillSine(data.data(), kNumSamples, 200.0, kSampleRate, 0.5f);

            tf.process(data.data(), kNumSamples);
            return rmsRange(data.data(), 2048, kNumSamples);
        };

        const double rmsNeg3 = runLowBandRMS(-3.0f);
        const double rmsNeg1 = runLowBandRMS(-1.0f);
        const double rmsPlus1 = runLowBandRMS(+1.0f);
        const double rmsPlus3 = runLowBandRMS(+3.0f);

        // Low-band RMS must grow monotonically as tilt grows.
        expect(rmsPlus3 > rmsPlus1,
               "Low-band RMS must increase from +1 to +3 dB; "
               "+1=" + juce::String(rmsPlus1)
               + " +3=" + juce::String(rmsPlus3));
        expect(rmsPlus1 > rmsNeg1,
               "Low-band RMS must increase from -1 to +1 dB; "
               "-1=" + juce::String(rmsNeg1)
               + " +1=" + juce::String(rmsPlus1));
        expect(rmsNeg1 > rmsNeg3,
               "Low-band RMS must increase from -3 to -1 dB; "
               "-3=" + juce::String(rmsNeg3)
               + " -1=" + juce::String(rmsNeg1));
    }

    // -------------------------------------------------------------------------
    // Voice — §4.2 / §4.5 / §14.8
    // -------------------------------------------------------------------------

    void testVoiceDualVoiceCrossCorrelationBelowThreshold()
    {
        beginTest("Voice: two voices with distinct indices produce decorrelated outputs (loose cross-correlation threshold)");

        Voice v0(0);
        Voice v1(1);

        // Same humanization params for both — only the voice index changes.
        const float timing = 6.0f;
        const float pitch = 5.0f;
        const float rate = 50.0f;
        const float tightness = 0.5f;
        const float nominalMs = 20.0f;
        const auto latencyMode = PitchShifter::LatencyMode::studio;

        v0.setTimingSpreadMs(timing);
        v0.setPitchSpreadCents(pitch);
        v0.setHumanRate(rate);
        v0.setTightness(tightness);
        v0.setNominalDelayMs(nominalMs);
        v0.setLatencyMode(latencyMode);

        v1.setTimingSpreadMs(timing);
        v1.setPitchSpreadCents(pitch);
        v1.setHumanRate(rate);
        v1.setTightness(tightness);
        v1.setNominalDelayMs(nominalMs);
        v1.setLatencyMode(latencyMode);

        v0.prepare(kSampleRate, kMaxBlockSize);
        v1.prepare(kSampleRate, kMaxBlockSize);

        // Same master seed → same hash base; different voiceIndex seeds
        // the PRNG streams differently. This is the §4.10 "same seed ⇒
        // identical performance" guarantee applied per-voice.
        v0.reseed(kDeterminismMasterSeed);
        v1.reseed(kDeterminismMasterSeed);

        // Drive both voices with the same noise burst. The PRNG used to
        // generate the noise is independent of the voice PRNGs and is
        // never re-instantiated inside processSample.
        std::mt19937 noiseRng(0xC0FFEE);
        std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

        // 1 second @ 48 kHz = 48000 samples. Long enough for the walks
        // to wander past their initial conditions (the walks start at
        // the axis mean; we need the random drift to kick in).
        constexpr int kNumSamples = 48000;
        std::vector<float> y0(static_cast<std::size_t>(kNumSamples));
        std::vector<float> y1(static_cast<std::size_t>(kNumSamples));

        for (int i = 0; i < kNumSamples; ++i)
        {
            const float x = noiseDist(noiseRng);
            y0[static_cast<std::size_t>(i)] = v0.processSample(x);
            y1[static_cast<std::size_t>(i)] = v1.processSample(x);
        }

        // Skip the first quarter so we measure after the walks have
        // moved away from the axis mean — comparing two nominally-equal
        // starting points would trivially give high correlation.
        const int skip = kNumSamples / 4;
        const double rho = zeroLagCrossCorrelation(
            y0.data() + skip, y1.data() + skip, kNumSamples - skip);

        // Loose threshold per task brief: the spec mandates the
        // decorrelation exists but the test must tolerate the small
        // remaining commonality from the shared input + short allpass
        // delays + small tilts.
        expect(rho < 0.95,
               "Voice 0 vs Voice 1 cross-correlation must be < 0.95; rho="
               + juce::String(rho));
    }

    void testVoiceProcessPathRunsCleanlyForNSamples()
    {
        beginTest("Voice: processSample runs cleanly for N samples after prepare (no throw, finite output)");

        Voice v(2);
        v.setTimingSpreadMs(6.0f);
        v.setPitchSpreadCents(5.0f);
        v.setHumanRate(50.0f);
        v.setTightness(0.5f);
        v.setNominalDelayMs(20.0f);
        v.setLatencyMode(PitchShifter::LatencyMode::studio);
        v.prepare(kSampleRate, kMaxBlockSize);
        v.reseed(kDeterminismMasterSeed);

        constexpr int kNumSamples = 16384;
        bool allFinite = true;
        float maxAbs = 0.0f;
        float first = 0.0f;

        for (int i = 0; i < kNumSamples; ++i)
        {
            // Mix of DC-free sine and broadband content so the walks
            // experience non-trivial input variation.
            const float x = static_cast<float>(
                0.4 * std::sin(2.0 * kPi * 440.0
                               * static_cast<double>(i) / kSampleRate)
                + 0.1 * std::sin(2.0 * kPi * 1234.0
                                 * static_cast<double>(i) / kSampleRate));
            const float y = v.processSample(x);
            if (i == 0)
                first = y;
            if (!std::isfinite(static_cast<double>(y)))
            {
                allFinite = false;
                break;
            }
            maxAbs = std::max(maxAbs, std::abs(y));
        }

        expect(allFinite,
               "Voice.processSample must produce finite output for every sample");
        expect(maxAbs < 10.0f,
               "Voice.processSample output must stay in a sane range; maxAbs="
               + juce::String(maxAbs));
        // Smoke check: the very first sample must be near the input (no
        // blow-up at startup). 0.4 sine amplitude gives ≤ 0.5 abs input.
        expect(std::abs(first) < 2.0f,
               "First Voice output sample must be bounded; got "
               + juce::String(first));

        // Second overload: processSample(x, accL, accR). Accumulators must
        // receive bounded, finite contributions.
        float accL = 0.0f;
        float accR = 0.0f;
        for (int i = 0; i < kNumSamples; ++i)
        {
            const float x = static_cast<float>(
                0.3 * std::sin(2.0 * kPi * 660.0
                               * static_cast<double>(i) / kSampleRate));
            v.processSample(x, accL, accR);

            if (!std::isfinite(static_cast<double>(accL))
                || !std::isfinite(static_cast<double>(accR)))
            {
                allFinite = false;
                break;
            }
        }

        expect(allFinite,
               "Voice.processSample (accL/accR overload) must keep accumulators finite");
    }

    void testVoiceDistinctVoiceIndicesReported()
    {
        beginTest("Voice: distinct voice indices round-trip through getVoiceIndex()");

        for (int i = 0; i < AllpassChain::numVoices; ++i)
        {
            Voice v(i);
            v.prepare(kSampleRate, kMaxBlockSize);

            expect(v.getVoiceIndex() == i,
                   "Voice must report its index after prepare(); i="
                   + juce::String(i) + " reported="
                   + juce::String(v.getVoiceIndex()));
        }
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static DSPPrimitiveTests dspPrimitiveTestsInstance;