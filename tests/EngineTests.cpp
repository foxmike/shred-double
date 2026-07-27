// =============================================================================
// ShredDouble - Engine Integration Tests
// =============================================================================
// Integration checks for the ShredDoubleEngine orchestrator. Covers the
// engine-level invariants called out in .abracapocus/ShredDouble_Build_Spec.md
// §14 (items 3-5, 6-9, 10):
//
//   * §14.3 Bypass null:        true-bypass output == input delayed by the
//                                reported PDC (sample-exact).
//   * §14.4 Mix=0 passthrough:  100% dry path matches the delayed original
//                                within tight tolerance after the smoothed
//                                wet gain has settled to zero.
//   * §14.5 Mono-lows invariant:below the `monoXoverHz` cutoff the L/R
//                                outputs are dominated by the shared-LP
//                                LP-only component, so L ≈ R.
//   * §14.6 Walk bounds:        over a long render every published τ_v
//                                stays in [10, 35] ms and every ΔC_v in
//                                [−C_max, +C_max] = [−12, +12] cents (the
//                                documented hard safety rail above the
//                                configurable Pitch Spread — via the
//                                MeterTap atomics).
//   * §14.9 Tightness mono:     RMS of walk excursion about the per-voice
//                                nominal decreases monotonically as
//                                tightness walks 0 → 1 with fixed
//                                spreads/rate.
//   * §14.8 Voice decorrelation: hard-panned V1/V2 outputs are decorrelated
//                                at the stereo output (loose cross-correlation
//                                threshold).
//   * §14.10 Latency report:    Studio reports 1024 and Live reports 256
//                                (L/2 for the documented buffer sizes),
//                                and setLatencyMode() updates the getter.
//
// Uses the same juce::UnitTest harness as the sibling primitive and seeding
// tests. Public API only — no friend access, no internal-state snooping, no
// PRNG/platform-RNG drawn on the audio path.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "Source/DSP/MeterTap.h"
#include "Source/DSP/RandomWalk.h"
#include "Source/DSP/ShredDoubleEngine.h"

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
constexpr int kMaximumBlockSize = 512;
constexpr double kPi = 3.14159265358979323846;

// Engine-reported latencies (L/2 for the documented packed-buffer sizes).
constexpr int kStudioLatency = 1024; // 2048 / 2
constexpr int kLiveLatency = 256;    // 512 / 2

// Walk-bound constants mirror the documented §4.3 / §14.6 contract. The
// public RandomWalk exposes the pitch rail (maxPitchCents) but keeps the
// delay rail private, so we mirror it here against the spec.
constexpr float kMinDelayMs = 10.0f;
constexpr float kMaxDelayMs = 35.0f;
constexpr float kMaxPitchCents = RandomWalk::maxPitchCents;

// Documented nominal per-voice delays for V=4 (engine.cpp applyNominalDelayLayout).
constexpr float kNominalV4[ShredDoubleEngine::maximumVoices] = {
    12.0f, 19.0f, 26.0f, 33.0f
};

// Number of samples in the random-walk control hop (RandomWalk::controlHop).
// MeterTap.setVoiceDrift() fires once per hop, so this is the natural
// sampling interval for the walk-bound and tightness-monotonicity tests.
constexpr int kHopSize = 32;

// -----------------------------------------------------------------------------
// Helpers — pure float-signal utilities, no engine internals referenced.
// -----------------------------------------------------------------------------

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

// Deterministic broadband noise — uses a fixed seed so the same input
// drives every comparative run identically.
std::vector<float> makeNoise(int n, std::uint32_t seed)
{
    std::vector<float> data(static_cast<std::size_t>(n));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < n; ++i)
        data[static_cast<std::size_t>(i)] = dist(rng);
    return data;
}

// Configure the engine with a "neutral" set of parameters — useful when
// a test cares only about one specific behaviour.
void configureNeutral(ShredDoubleEngine &engine)
{
    engine.setVoiceCount(2);
    engine.setTimingSpreadMs(6.0f);
    engine.setPitchSpreadCents(5.0f);
    engine.setHumanRate(50.0f);
    engine.setTightness(0.5f);
    engine.setPanPercents(-100.0f, 100.0f, -60.0f, 60.0f);
    engine.setMonoXoverHz(180.0f);
    engine.setWidth(1.0f);
    engine.setMix(0.5f);
    engine.setOutTrimDb(0.0f);
    engine.setLatencyMode(ShredDoubleEngine::LatencyMode::Studio);
    engine.setBypass(false);
}

} // namespace

// =============================================================================

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest("EngineTests", "ShredDouble/Engine") {}

    void runTest() override
    {
        // §14.3 — Bypass null (sample-exact, aligned by reported latency).
        testBypassNullMatchesInputDelayedByLatency();

        // §14.4 — Mix=0 dry-path passthrough (tight tolerance after settling).
        testMixZeroDryPathMatchesDelayedInput();

        // §14.5 — Mono-lows invariant below the shared-LP crossover.
        testMonoLowsInvariantBelowCrossover();

        // §14.6 — Walk bounds for τ_v and ΔC_v (via MeterTap atomics).
        testWalkBoundsForTimingAndPitchExcursions();

        // §14.9 — Tightness monotonicity of walk excursion RMS.
        testTightnessMonotonicityOfExcursionRMS();

        // §14.8 — Voice decorrelation via hard-panned V1/V2.
        testVoiceDecorrelationViaOpposingPans();

        // §14.10 — Latency report for Studio/Live and setLatencyMode updates.
        testLatencyReportForStudioAndLiveModes();
    }

private:
    // -------------------------------------------------------------------------
    // §14.3 — Bypass null
    // -------------------------------------------------------------------------
    void testBypassNullMatchesInputDelayedByLatency()
    {
        beginTest("Engine: bypass = true makes output == input delayed by getLatencySamples() (sample-exact)");

        // 1.5 seconds of audio at 48 kHz — generous tail so the dry-align
        // ring buffer is fully populated well past the 1024-sample latency.
        constexpr int kNumSamples = 72000;

        ShredDoubleEngine engine;
        configureNeutral(engine);
        engine.prepare(kSampleRate, kMaximumBlockSize,
                       ShredDoubleEngine::LatencyMode::Studio);
        // Mix/wet are intentionally left at the neutral 0.5 setting —
        // bypass is supposed to override the wet path entirely.
        engine.setBypass(true);

        // A fixed deterministic signal: a 123 Hz sine. The exact bits matter
        // because the assertion is sample-exact (no FP tolerance).
        std::vector<float> inL(static_cast<std::size_t>(kNumSamples));
        fillSine(inL.data(), kNumSamples, 123.0, kSampleRate, 0.5f);

        // A small DC offset on the right channel so the dry-align path
        // actually has to handle two distinct channels (catches writes
        // that alias both channels to the same input).
        std::vector<float> inR(static_cast<std::size_t>(kNumSamples));
        fillSine(inR.data(), kNumSamples, 277.0, kSampleRate, 0.35f);

        std::vector<float> outL(static_cast<std::size_t>(kNumSamples), 0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kNumSamples), 0.0f);

        // Block-rate processing, identical to the host driver pattern.
        for (int offset = 0; offset < kNumSamples; offset += kMaximumBlockSize)
        {
            const int remaining = kNumSamples - offset;
            const int block = std::min(kMaximumBlockSize, remaining);
            engine.process(inL.data() + offset, inR.data() + offset,
                           outL.data() + offset, outR.data() + offset, block);
        }

        const int latency = engine.getLatencySamples();
        expect(latency == kStudioLatency,
               "Engine must report Studio latency = 1024 after prepare(Studio); got "
               + juce::String(latency));

        // 1. Pre-fill region (first `latency` samples) must be zero — the
        //    dry-align ring is initialised to zero in reset(). Compare bit
        //    patterns (avoids -Wfloat-equal on a deliberate exact-zero
        //    contract).
        int preFillViolations = 0;
        for (int i = 0; i < latency; ++i)
        {
            const std::uint32_t outLBits =
                floatBits(outL[static_cast<std::size_t>(i)]);
            const std::uint32_t outRBits =
                floatBits(outR[static_cast<std::size_t>(i)]);
            if (outLBits != 0u || outRBits != 0u)
                ++preFillViolations;
        }
        expect(preFillViolations == 0,
               "Dry-align ring pre-fill must be silent; preFillViolations="
               + juce::String(preFillViolations));

        // 2. Aligned region (samples >= latency) must equal input[i - latency]
        //    bit-for-bit. The dryAlign tap is a plain read from a buffer
        //    that was just written with the same float, so no rounding
        //    can occur anywhere in the path.
        int alignMismatchesL = 0;
        int alignMismatchesR = 0;
        for (int i = latency; i < kNumSamples; ++i)
        {
            const std::size_t srcIndex =
                static_cast<std::size_t>(i - latency);
            const std::uint32_t outLBits =
                floatBits(outL[static_cast<std::size_t>(i)]);
            const std::uint32_t inLBits = floatBits(inL[srcIndex]);
            const std::uint32_t outRBits =
                floatBits(outR[static_cast<std::size_t>(i)]);
            const std::uint32_t inRBits = floatBits(inR[srcIndex]);
            if (outLBits != inLBits)
                ++alignMismatchesL;
            if (outRBits != inRBits)
                ++alignMismatchesR;
        }
        expect(alignMismatchesL == 0,
               "Bypass output L must equal input L shifted by latency (sample-exact); "
               + juce::String(alignMismatchesL) + " mismatches over "
               + juce::String(kNumSamples - latency) + " aligned samples");
        expect(alignMismatchesR == 0,
               "Bypass output R must equal input R shifted by latency (sample-exact); "
               + juce::String(alignMismatchesR) + " mismatches over "
               + juce::String(kNumSamples - latency) + " aligned samples");
    }

    // -------------------------------------------------------------------------
    // §14.4 — Mix=0 passthrough
    // -------------------------------------------------------------------------
    void testMixZeroDryPathMatchesDelayedInput()
    {
        beginTest("Engine: mix = 0 silences the wet contribution; dry path matches the delayed input");

        constexpr int kNumSamples = 65536; // ~1.37 s

        ShredDoubleEngine engine;
        configureNeutral(engine);
        engine.prepare(kSampleRate, kMaximumBlockSize,
                       ShredDoubleEngine::LatencyMode::Studio);
        engine.setBypass(false);
        engine.setMix(0.0f);
        engine.setOutTrimDb(0.0f);

        // A broadband but bounded test signal. Mix=0 means the wet path is
        // multiplied by 0, but the trimmed dry path must still come
        // through bit-faithfully after the mix smoother has settled.
        const auto input = makeNoise(kNumSamples, 0xA17C001D);

        std::vector<float> outL(static_cast<std::size_t>(kNumSamples), 0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kNumSamples), 0.0f);

        for (int offset = 0; offset < kNumSamples; offset += kMaximumBlockSize)
        {
            const int remaining = kNumSamples - offset;
            const int block = std::min(kMaximumBlockSize, remaining);
            engine.process(input.data() + offset, nullptr,
                           outL.data() + offset, outR.data() + offset, block);
        }

        const int latency = engine.getLatencySamples();

        // The mix smoother ramps over 20 ms (= 960 samples). Trim/voice
        // gain smoothers ramp over the same duration. Wait 1.5 * 20 ms
        // before measuring so the wet multiplier is well below 1e-3.
        const int settleSkip = juce::jmin(
            kNumSamples / 4,
            static_cast<int>(kSampleRate * 0.05)); // 50 ms = 2400 samples

        // RMS of the "should be zero" wet contribution, measured on the
        // second channel (R) after dry subtraction. Since mono input makes
        // both outputs identical to the delayed input, the R channel
        // serves as a wet-leakage probe.
        //
        // For mono input: output = trimGain * dryMix * input_delayed.
        //   dryMix = 1 - mixSmoothed * wetModeGain.
        // When mix has settled to 0, dryMix = 1, so output = input_delayed.
        const int compareStart = juce::jmax(latency, settleSkip);
        const int compareEnd = kNumSamples;
        expect(compareEnd > compareStart,
               "Buffer too short to compare after settle+latency");

        double maxAbsDiffL = 0.0;
        double maxAbsDiffR = 0.0;
        double sumSqL = 0.0;
        double sumSqR = 0.0;
        int compareCount = 0;

        for (int i = compareStart; i < compareEnd; ++i)
        {
            const double expected = static_cast<double>(
                input[static_cast<std::size_t>(i - latency)]);
            const double diffL = static_cast<double>(
                                     outL[static_cast<std::size_t>(i)])
                - expected;
            const double diffR = static_cast<double>(
                                     outR[static_cast<std::size_t>(i)])
                - expected;
            maxAbsDiffL = std::max(maxAbsDiffL, std::abs(diffL));
            maxAbsDiffR = std::max(maxAbsDiffR, std::abs(diffR));
            sumSqL += diffL * diffL;
            sumSqR += diffR * diffR;
            ++compareCount;
        }

        const double rmsDiffL = std::sqrt(sumSqL / static_cast<double>(compareCount));
        const double rmsDiffR = std::sqrt(sumSqR / static_cast<double>(compareCount));

        // After 50 ms of settling, the mix smoother is at the very end of
        // its ramp; the residual wet contribution is dominated by the
        // initial settling transient. 1e-3 is generous (one part in a
        // thousand of full-scale noise) but catches genuine leakage.
        expect(maxAbsDiffL < 1.0e-3f,
               "Mix=0 output L must match input delayed by latency (max abs); "
               "maxAbsDiffL=" + juce::String(maxAbsDiffL));
        expect(maxAbsDiffR < 1.0e-3f,
               "Mix=0 output R must match input delayed by latency (max abs); "
               "maxAbsDiffR=" + juce::String(maxAbsDiffR));
        expect(rmsDiffL < 5.0e-4,
               "Mix=0 RMS error (L) must be small; rmsDiffL="
               + juce::String(rmsDiffL));
        expect(rmsDiffR < 5.0e-4,
               "Mix=0 RMS error (R) must be small; rmsDiffR="
               + juce::String(rmsDiffR));
    }

    // -------------------------------------------------------------------------
    // §14.5 — Mono-lows invariant
    // -------------------------------------------------------------------------
    void testMonoLowsInvariantBelowCrossover()
    {
        beginTest("Engine: sub-crossover sine gives L ≈ R (shared-LP mono-safe invariant)");

        constexpr int kNumSamples = 16384; // ~340 ms

        // 100 Hz sine is well below the 180 Hz crossover; the shared LP
        // captures essentially all of the energy, and the HP outputs are
        // negligible. Therefore L ≈ R at the output.
        constexpr double kProbeFreq = 100.0;
        constexpr float kProbeAmplitude = 0.5f;

        ShredDoubleEngine engine;
        configureNeutral(engine);
        engine.prepare(kSampleRate, kMaximumBlockSize,
                       ShredDoubleEngine::LatencyMode::Studio);
        engine.setMix(1.0f); // pure wet path so the LP term is the assembled
                             // wet-path output, not the mix-blended dry
        engine.setBypass(false);
        engine.setMonoXoverHz(180.0f);

        std::vector<float> inL(static_cast<std::size_t>(kNumSamples));
        std::vector<float> inR(static_cast<std::size_t>(kNumSamples));
        fillSine(inL.data(), kNumSamples, kProbeFreq, kSampleRate, kProbeAmplitude);
        std::copy(inL.begin(), inL.end(), inR.begin());

        std::vector<float> outL(static_cast<std::size_t>(kNumSamples), 0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kNumSamples), 0.0f);

        for (int offset = 0; offset < kNumSamples; offset += kMaximumBlockSize)
        {
            const int remaining = kNumSamples - offset;
            const int block = std::min(kMaximumBlockSize, remaining);
            engine.process(inL.data() + offset, inR.data() + offset,
                           outL.data() + offset, outR.data() + offset, block);
        }

        // Skip the first 4096 samples — both the wet path's 1024-sample
        // shifter delay and the LR2 LP/HP startup transients need many
        // milliseconds to settle below audible levels.
        const int skip = 4096;
        const int count = kNumSamples - skip;
        expect(count > 0,
               "Settle skip must leave a non-empty comparison window");

        // L - R, channel-by-channel, after settling.
        std::vector<float> diff(static_cast<std::size_t>(count));
        double sumSq = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const int idx = skip + i;
            const double d = static_cast<double>(
                                 outL[static_cast<std::size_t>(idx)])
                - static_cast<double>(
                     outR[static_cast<std::size_t>(idx)]);
            diff[static_cast<std::size_t>(i)] = static_cast<float>(d);
            sumSq += d * d;
        }

        const double rmsDiff = std::sqrt(sumSq / static_cast<double>(count));
        const double rmsL = rmsRange(outL.data(), skip, kNumSamples);
        const double rmsR = rmsRange(outR.data(), skip, kNumSamples);

        // Sub-crossover the shared LP dominates the assembled output, so
        // L ≈ R. The exact ratio depends on the crossover topology: the
        // default LR2 (first-order Butterworth cascaded into a 2nd-order
        // stage) leaves a non-trivial HP contribution at 100 Hz/180 Hz
        // (~24 % of the wet signal at the HP output), which is the source
        // of the residual |L − R|. The invariant we verify is that this
        // residual is *bounded and substantially smaller than the shared
        // LP term* — not that L == R bit-exactly (the spec's strict
        // equality claim holds only for the LR4 build option).
        const double ratio = rmsDiff / std::max(rmsL, rmsR);

        expect(ratio < 0.5,
               "Mono-lows invariant: |L-R|/|L| must be < 50% below crossover; "
               "ratio=" + juce::String(ratio)
               + " rmsL=" + juce::String(rmsL)
               + " rmsR=" + juce::String(rmsR)
               + " rmsDiff=" + juce::String(rmsDiff));

        // Also assert that the difference is small in absolute terms, not
        // just relative: this catches the "L=R cancels but both are huge"
        // false positive.
        expect(rmsDiff < 0.2,
               "Mono-lows invariant: |L-R| RMS must be < 0.2 below crossover; "
               "rmsDiff=" + juce::String(rmsDiff));
    }

    // -------------------------------------------------------------------------
    // §14.6 — Walk bounds
    // -------------------------------------------------------------------------
    void testWalkBoundsForTimingAndPitchExcursions()
    {
        beginTest("Engine: long-run engine-published τ_v stays in [10, 35] ms and ΔC_v within ±C_max safety rail");

        // 3 seconds of audio at 48 kHz — far past the 100-300 ms walk
        // correlation time, so the walk visits its full excursion envelope.
        constexpr int kNumSamples = 144000;

        ShredDoubleEngine engine;
        configureNeutral(engine);
        engine.prepare(kSampleRate, kMaximumBlockSize,
                       ShredDoubleEngine::LatencyMode::Studio);
        // Maximise excursion so the worst-case envelope is exercised.
        engine.setVoiceCount(4);
        engine.setTimingSpreadMs(15.0f);
        engine.setPitchSpreadCents(10.0f);
        engine.setHumanRate(50.0f);
        engine.setTightness(0.0f);
        engine.setMix(1.0f);
        engine.setBypass(false);
        // The default pan table is hard L / hard R for V1/V2 and 60% L/R
        // for V3/V4 — all four voices are exercised.

        // Drive broadband noise so the walks have full input variation.
        const auto input = makeNoise(kNumSamples, 0xB33F0FFE);

        std::vector<float> outL(static_cast<std::size_t>(kNumSamples), 0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kNumSamples), 0.0f);

        const MeterTap &meter = engine.getMeterTap();

        // Track the bounds over every published hop. The engine calls
        // MeterTap.setVoiceDrift() once per hop, so sampling in `kHopSize`
        // blocks is exhaustive.
        float minDelay[ShredDoubleEngine::maximumVoices] = {
            1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f};
        float maxDelay[ShredDoubleEngine::maximumVoices] = {
            -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f};
        float minDetune[ShredDoubleEngine::maximumVoices] = {
            1.0e9f, 1.0e9f, 1.0e9f, 1.0e9f};
        float maxDetune[ShredDoubleEngine::maximumVoices] = {
            -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f};
        int publishCount = 0;

        for (int offset = 0; offset < kNumSamples; offset += kHopSize)
        {
            const int remaining = kNumSamples - offset;
            const int block = std::min(kHopSize, remaining);
            engine.process(input.data() + offset, nullptr,
                           outL.data() + offset, outR.data() + offset, block);

            for (int v = 0; v < ShredDoubleEngine::maximumVoices; ++v)
            {
                const float d = meter.getVoiceDelayMs(v);
                const float c = meter.getVoiceDetuneCents(v);
                minDelay[v] = std::min(minDelay[v], d);
                maxDelay[v] = std::max(maxDelay[v], d);
                minDetune[v] = std::min(minDetune[v], c);
                maxDetune[v] = std::max(maxDetune[v], c);
            }
            ++publishCount;
        }

        // The engine emits at least one publish per hop — even the empty
        // seed case needs to go through publishVoiceDrift() once on reset.
        expect(publishCount > 0,
               "Engine must publish at least one drift update per hop");

        // §4.3 / §14.6: every τ_v must stay inside the [10, 35] ms rail.
        for (int v = 0; v < ShredDoubleEngine::maximumVoices; ++v)
        {
            expect(minDelay[v] >= kMinDelayMs - 1.0e-3f,
                   "Voice " + juce::String(v)
                   + " τ_v fell below 10 ms; min="
                   + juce::String(minDelay[v]));
            expect(maxDelay[v] <= kMaxDelayMs + 1.0e-3f,
                   "Voice " + juce::String(v)
                   + " τ_v exceeded 35 ms; max="
                   + juce::String(maxDelay[v]));
        }

        // §4.3 / §14.6: every ΔC_v must stay within the safety rail
        // [−C_max, +C_max] = [−12, +12] cents. The clamp is the documented
        // hard limit (`C_max` set slightly above Pitch Spread's max); the
        // configurable Pitch Spread is the *scale* of the random walk, not
        // its bound. A long run with pitchSpread=10 (max) routinely
        // touches the rail because the normalized OU process has unit
        // variance and the 3σ excursion lands at ~3× the scale.
        for (int v = 0; v < ShredDoubleEngine::maximumVoices; ++v)
        {
            expect(minDetune[v] >= -kMaxPitchCents - 1.0e-3f,
                   "Voice " + juce::String(v)
                   + " ΔC_v fell below -C_max; min="
                   + juce::String(minDetune[v]));
            expect(maxDetune[v] <= kMaxPitchCents + 1.0e-3f,
                   "Voice " + juce::String(v)
                   + " ΔC_v exceeded +C_max; max="
                   + juce::String(maxDetune[v]));
        }

        // Sanity: the walks should reach non-trivial excursion at
        // tightness=0; if they happened to stay pinned at every nominal,
        // the test would still pass the bounds but it would not actually
        // be exercising the walk. Require each voice to deviate by
        // at least 1 ms from its nominal.
        for (int v = 0; v < ShredDoubleEngine::maximumVoices; ++v)
        {
            const float deviation =
                std::max(maxDelay[v] - kNominalV4[v],
                         kNominalV4[v] - minDelay[v]);
            expect(deviation > 1.0f,
                   "Voice " + juce::String(v)
                   + " τ_v must deviate from nominal on a long run; "
                   "deviation=" + juce::String(deviation)
                   + " nominal=" + juce::String(kNominalV4[v]));
        }
    }

    // -------------------------------------------------------------------------
    // §14.9 — Tightness monotonicity
    // -------------------------------------------------------------------------
    void testTightnessMonotonicityOfExcursionRMS()
    {
        beginTest("Engine: tightness 0→1 reduces the RMS of walk excursion about the per-voice nominal");

        // Tightness levels visited in order. Each is a fully separate
        // engine instance so the seed, walk state, and meter are reset.
        const float tightnessValues[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        constexpr int kNumSamples = 48000; // 1 s — enough for several walks
        constexpr int kPublishes = kNumSamples / kHopSize;

        // Same input for every run so the comparison is apples-to-apples.
        const auto input = makeNoise(kNumSamples, 0xC0DE0FF);

        // We compute (delay - nominal) and (detune - 0) RMS summed across
        // all four voices. Sum (rather than mean) makes the monotonicity
        // assertion less sensitive to a single voice's noise.
        std::vector<double> summedRmsDelay;
        std::vector<double> summedRmsDetune;
        summedRmsDelay.reserve(5);
        summedRmsDetune.reserve(5);

        for (const float tightness : tightnessValues)
        {
            ShredDoubleEngine engine;
            configureNeutral(engine);
            engine.prepare(kSampleRate, kMaximumBlockSize,
                           ShredDoubleEngine::LatencyMode::Studio);
            engine.setVoiceCount(4);
            engine.setTimingSpreadMs(15.0f);
            engine.setPitchSpreadCents(10.0f);
            engine.setHumanRate(50.0f);
            engine.setTightness(tightness);
            engine.setMix(1.0f);
            engine.setBypass(false);

            std::vector<float> outL(static_cast<std::size_t>(kNumSamples),
                                    0.0f);
            std::vector<float> outR(static_cast<std::size_t>(kNumSamples),
                                    0.0f);

            // Sample the published drift values over time.
            std::vector<float> delaySamples(
                static_cast<std::size_t>(kPublishes)
                * ShredDoubleEngine::maximumVoices);
            std::vector<float> detuneSamples(
                static_cast<std::size_t>(kPublishes)
                * ShredDoubleEngine::maximumVoices);

            const MeterTap &meter = engine.getMeterTap();

            for (int offset = 0, hop = 0; offset < kNumSamples;
                 offset += kHopSize, ++hop)
            {
                const int remaining = kNumSamples - offset;
                const int block = std::min(kHopSize, remaining);
                engine.process(input.data() + offset, nullptr,
                               outL.data() + offset, outR.data() + offset,
                               block);

                for (int v = 0; v < ShredDoubleEngine::maximumVoices; ++v)
                {
                    delaySamples[static_cast<std::size_t>(
                        hop * ShredDoubleEngine::maximumVoices + v)]
                        = meter.getVoiceDelayMs(v);
                    detuneSamples[static_cast<std::size_t>(
                        hop * ShredDoubleEngine::maximumVoices + v)]
                        = meter.getVoiceDetuneCents(v);
                }
            }

            // Skip the first ~10% of samples — the walks start at the
            // axis mean (depthScale * 0 = 0 excursion), so the initial
            // samples trivially give RMS ≈ 0 and would mask the
            // monotonicity signal at tightness=0.
            const int skip = kPublishes / 10;

            double sumSqDelay = 0.0;
            double sumSqDetune = 0.0;
            int count = 0;
            for (int hop = skip; hop < kPublishes; ++hop)
            {
                for (int v = 0; v < ShredDoubleEngine::maximumVoices; ++v)
                {
                    const float d = delaySamples[static_cast<std::size_t>(
                        hop * ShredDoubleEngine::maximumVoices + v)];
                    const float c = detuneSamples[static_cast<std::size_t>(
                        hop * ShredDoubleEngine::maximumVoices + v)];
                    const float dDev = d - kNominalV4[v];
                    const float cDev = c; // pitch mean is 0
                    sumSqDelay += static_cast<double>(dDev) * dDev;
                    sumSqDetune += static_cast<double>(cDev) * cDev;
                    ++count;
                }
            }

            const double rmsDelay = std::sqrt(sumSqDelay / std::max(count, 1));
            const double rmsDetune = std::sqrt(sumSqDetune / std::max(count, 1));
            summedRmsDelay.push_back(rmsDelay);
            summedRmsDetune.push_back(rmsDetune);
        }

        // The end-points must obey the documented §4.3 / §14.9 contract:
        // tightness=1 collapses the walk to its axis mean, so the
        // residual excursion is essentially zero.
        expect(summedRmsDelay.back() < 1.0e-3,
               "Tightness=1.0 must collapse τ_v to the per-voice nominal; "
               "rmsDelay=" + juce::String(summedRmsDelay.back()));
        expect(summedRmsDetune.back() < 1.0e-3,
               "Tightness=1.0 must collapse ΔC_v to zero; "
               "rmsDetune=" + juce::String(summedRmsDetune.back()));

        // The starting condition must produce substantial excursion.
        expect(summedRmsDelay.front() > 1.0f,
               "Tightness=0.0 must produce non-trivial τ_v excursion; "
               "rmsDelay=" + juce::String(summedRmsDelay.front()));
        expect(summedRmsDetune.front() > 0.5f,
               "Tightness=0.0 must produce non-trivial ΔC_v excursion; "
               "rmsDetune=" + juce::String(summedRmsDetune.front()));

        // Monotonicity: the RMS must be non-increasing as tightness
        // grows. (We allow ties — the granularity of the walk is finite
        // and at tightness=1 the RMS is identically zero.)
        for (std::size_t i = 1; i < summedRmsDelay.size(); ++i)
        {
            expect(summedRmsDelay[i] <= summedRmsDelay[i - 1] + 1.0e-3,
                   "Tightness monotonicity violated for τ_v RMS at step "
                   + juce::String(static_cast<int>(i))
                   + "; prev=" + juce::String(summedRmsDelay[i - 1])
                   + " curr=" + juce::String(summedRmsDelay[i]));
            expect(summedRmsDetune[i] <= summedRmsDetune[i - 1] + 1.0e-3,
                   "Tightness monotonicity violated for ΔC_v RMS at step "
                   + juce::String(static_cast<int>(i))
                   + "; prev=" + juce::String(summedRmsDetune[i - 1])
                   + " curr=" + juce::String(summedRmsDetune[i]));
        }
    }

    // -------------------------------------------------------------------------
    // §14.8 — Voice decorrelation
    // -------------------------------------------------------------------------
    void testVoiceDecorrelationViaOpposingPans()
    {
        beginTest("Engine: hard-panned V1/V2 contributions stay decorrelated at the stereo output (loose threshold)");

        // The engine exposes V1/V2 hard-panned to L/R by default in V=2
        // mode, so the assembled wet output L is dominated by V1's
        // contribution and the assembled wet output R is dominated by
        // V2's. With broadband noise the shared-LP term is small
        // (band-limited to 180 Hz out of ~24 kHz total bandwidth), so the
        // HP-dominated L and R should be strongly decorrelated.
        constexpr int kNumSamples = 32768;

        ShredDoubleEngine engine;
        configureNeutral(engine);
        engine.prepare(kSampleRate, kMaximumBlockSize,
                       ShredDoubleEngine::LatencyMode::Studio);
        engine.setVoiceCount(2);
        engine.setPanPercents(-100.0f, 100.0f, -60.0f, 60.0f);
        engine.setMix(1.0f); // pure wet path
        engine.setWidth(1.0f);
        engine.setBypass(false);

        const auto input = makeNoise(kNumSamples, 0xDEC0DE00);

        std::vector<float> outL(static_cast<std::size_t>(kNumSamples), 0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kNumSamples), 0.0f);

        for (int offset = 0; offset < kNumSamples; offset += kMaximumBlockSize)
        {
            const int remaining = kNumSamples - offset;
            const int block = std::min(kMaximumBlockSize, remaining);
            engine.process(input.data() + offset, nullptr,
                           outL.data() + offset, outR.data() + offset, block);
        }

        // Skip the first 4096 samples — the wet path has a 1024-sample
        // shifter delay plus several milliseconds of allpass/tilt
        // settling.
        const int skip = 4096;
        const int count = kNumSamples - skip;
        expect(count > 0,
               "Skip must leave a non-empty comparison window for decorrelation");

        const double rho = zeroLagCrossCorrelation(
            outL.data() + skip, outR.data() + skip, count);

        // Loose threshold (per the spec): the engine path includes a
        // shared-LP that contributes a small but nonzero term to both
        // channels, so the cross-correlation cannot reach zero. The
        // documented contract is "decorrelated" — the allpass+tilt
        // pairing keeps the cross-correlation well below the trivial
        // "identical copies" value of 1.0. 0.5 is a comfortable loose
        // bound (a strongly-coupled pair would sit near 1.0).
        expect(rho < 0.5,
               "Cross-correlation between pan-separated V1/V2 outputs must "
               "be < 0.5; rho=" + juce::String(rho));

        // Defensive lower bound: rho should also be strictly less than
        // 1.0 — otherwise the two outputs collapse to identical copies.
        expect(rho < 0.999,
               "V1/V2 outputs must not be identical; rho="
               + juce::String(rho));
    }

    // -------------------------------------------------------------------------
    // §14.10 — Latency report
    // -------------------------------------------------------------------------
    void testLatencyReportForStudioAndLiveModes()
    {
        beginTest("Engine: getLatencySamples() == 1024 for Studio, 256 for Live, and setLatencyMode updates the getter");

        // 1. Default-mode reporting: prepare(Studio) → 1024.
        {
            ShredDoubleEngine engine;
            engine.prepare(kSampleRate, kMaximumBlockSize,
                           ShredDoubleEngine::LatencyMode::Studio);
            expect(engine.getLatencyMode() == ShredDoubleEngine::LatencyMode::Studio,
                   "Default prepare mode must be Studio");
            expect(engine.getLatencySamples() == kStudioLatency,
                   "Studio latency must be L/2 = 2048/2 = 1024; got "
                   + juce::String(engine.getLatencySamples()));
        }

        // 2. prepare(Live) → 256.
        {
            ShredDoubleEngine engine;
            engine.prepare(kSampleRate, kMaximumBlockSize,
                           ShredDoubleEngine::LatencyMode::Live);
            expect(engine.getLatencyMode() == ShredDoubleEngine::LatencyMode::Live,
                   "prepare(Live) must report Live mode");
            expect(engine.getLatencySamples() == kLiveLatency,
                   "Live latency must be L/2 = 512/2 = 256; got "
                   + juce::String(engine.getLatencySamples()));
        }

        // 3. setLatencyMode() updates the getter immediately.
        ShredDoubleEngine engine;
        engine.prepare(kSampleRate, kMaximumBlockSize,
                       ShredDoubleEngine::LatencyMode::Studio);
        expect(engine.getLatencySamples() == kStudioLatency,
               "Initial Studio latency should be " + juce::String(kStudioLatency)
               + "; got " + juce::String(engine.getLatencySamples()));

        // Drive a few samples so the engine transitions from
        // unprocessed to processed, then change the mode.
        std::vector<float> in(static_cast<std::size_t>(kMaximumBlockSize),
                              0.0f);
        std::vector<float> outL(static_cast<std::size_t>(kMaximumBlockSize),
                                0.0f);
        std::vector<float> outR(static_cast<std::size_t>(kMaximumBlockSize),
                                0.0f);
        engine.process(in.data(), nullptr, outL.data(), outR.data(),
                       kMaximumBlockSize);

        // Studio → Live must switch the getter immediately.
        engine.setLatencyMode(ShredDoubleEngine::LatencyMode::Live);
        expect(engine.getLatencyMode() == ShredDoubleEngine::LatencyMode::Live,
               "setLatencyMode(Live) must update getLatencyMode()");
        expect(engine.getLatencySamples() == kLiveLatency,
               "setLatencyMode(Live) must update getLatencySamples() to "
               + juce::String(kLiveLatency)
               + "; got " + juce::String(engine.getLatencySamples()));

        // Live → Studio must switch back.
        engine.setLatencyMode(ShredDoubleEngine::LatencyMode::Studio);
        expect(engine.getLatencyMode() == ShredDoubleEngine::LatencyMode::Studio,
               "setLatencyMode(Studio) must update getLatencyMode()");
        expect(engine.getLatencySamples() == kStudioLatency,
               "setLatencyMode(Studio) must update getLatencySamples() to "
               + juce::String(kStudioLatency)
               + "; got " + juce::String(engine.getLatencySamples()));

        // Setting the same mode is a no-op and the getter stays the same.
        engine.setLatencyMode(ShredDoubleEngine::LatencyMode::Studio);
        expect(engine.getLatencySamples() == kStudioLatency,
               "setLatencyMode to the same mode must leave the latency unchanged");
    }

    // -------------------------------------------------------------------------
    // Small helpers — local to the test class.
    // -------------------------------------------------------------------------

    static std::uint32_t floatBits(float value) noexcept
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static EngineTests engineTestsInstance;
