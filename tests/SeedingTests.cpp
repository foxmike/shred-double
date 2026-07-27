// =============================================================================
// ShredDouble - Engine Seeding and Determinism Tests
// =============================================================================
// Covers ShredDouble_Build_Spec.md sections 4.10 and 14.2 (D2): the complete
// engine must be reproducible from its stored master seed, while a new seed
// must produce a genuinely different performance.  These tests use only the
// public ShredDoubleEngine seed/process API and the existing JUCE UnitTest
// harness; no third-party test framework or platform RNG is involved.
//
// The deterministic seed used by the re-roll test is generated in this test
// harness, which stands in for the message-thread/UI action.  The simulated
// audio path below only processes samples and never calls std::random_device
// (or any other seed generator).
// =============================================================================

#include <juce_core/juce_core.h>

#include "Source/DSP/ShredDoubleEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kMaximumBlockSize = 512;
constexpr int kNumSamples = 32768;

constexpr std::uint64_t kInitialSeed =
    UINT64_C(0x0123456789ABCDEF);
constexpr std::uint64_t kDifferentSeed =
    UINT64_C(0xFEDCBA9876543210);

struct RenderedAudio
{
    std::vector<float> left;
    std::vector<float> right;
};

struct DifferenceStats
{
    std::size_t bitMismatches = 0;
    std::size_t meaningfulMismatches = 0;
    float maximumAbsoluteDifference = 0.0f;
    double rmsDifference = 0.0;
};

// Configure every engine control used by the render.  Keeping this in one
// helper makes the "same params" precondition explicit for every comparison.
void configureEngine(ShredDoubleEngine& engine)
{
    engine.setVoiceCount(4);
    engine.setTimingSpreadMs(12.0f);
    engine.setPitchSpreadCents(8.0f);
    engine.setHumanRate(65.0f);
    engine.setTightness(0.15f);
    engine.setPanPercents(-100.0f, 100.0f, -60.0f, 60.0f);
    engine.setMonoXoverHz(180.0f);
    engine.setWidth(1.0f);
    engine.setMix(1.0f);
    engine.setOutTrimDb(0.0f);
    engine.setLatencyMode(ShredDoubleEngine::LatencyMode::Studio);
    engine.setBypass(false);
}

// A fixed, broadband test signal makes changes in the seeded delay/pitch walks
// observable without introducing a second source of nondeterminism.
std::vector<float> makeInput()
{
    std::vector<float> input(static_cast<std::size_t>(kNumSamples));
    std::uint32_t state = UINT32_C(0xC001D00D);

    for (int i = 0; i < kNumSamples; ++i)
    {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        const float unit = static_cast<float>(state >> 8)
                         * (1.0f / 16777216.0f);
        const float noise = 2.0f * unit - 1.0f;

        // The sparse impulses complement the noise and make fractional delay
        // differences easy to distinguish even during the initial transient.
        const float impulse = (i % 257 == 0) ? 0.65f : 0.0f;
        input[static_cast<std::size_t>(i)] = 0.55f * noise + impulse;
    }

    return input;
}

void render(ShredDoubleEngine& engine,
            const std::vector<float>& input,
            RenderedAudio& output)
{
    output.left.assign(input.size(), 0.0f);
    output.right.assign(input.size(), 0.0f);

    // Use the block API in the same way a host would call processBlock.  The
    // block boundaries are identical for every render being compared.
    for (int offset = 0; offset < kNumSamples; offset += kMaximumBlockSize)
    {
        const int remaining = kNumSamples - offset;
        const int blockSize = std::min(kMaximumBlockSize, remaining);

        engine.process(input.data() + offset,
                       nullptr,
                       output.left.data() + offset,
                       output.right.data() + offset,
                       blockSize);
    }
}

RenderedAudio renderFromFreshEngine(std::uint64_t seed,
                                    const std::vector<float>& input)
{
    ShredDoubleEngine engine;
    configureEngine(engine);
    engine.prepare(kSampleRate, kMaximumBlockSize,
                   ShredDoubleEngine::LatencyMode::Studio);

    // seed() is the explicit, non-audio-thread seed application point.  The
    // reset makes the start state unambiguous while retaining the seed.
    engine.seed(seed);
    engine.reset();

    RenderedAudio output;
    render(engine, input, output);
    return output;
}

std::uint32_t floatBits(float value) noexcept
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

DifferenceStats compareAudio(const RenderedAudio& first,
                             const RenderedAudio& second)
{
    DifferenceStats stats;
    double sumOfSquares = 0.0;

    const auto compareChannel = [&stats, &sumOfSquares](
        const std::vector<float>& a, const std::vector<float>& b) {
        const auto count = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < count; ++i)
        {
            if (floatBits(a[i]) != floatBits(b[i]))
                ++stats.bitMismatches;

            const float difference = a[i] - b[i];
            const float absoluteDifference = std::abs(difference);
            stats.maximumAbsoluteDifference = std::max(
                stats.maximumAbsoluteDifference, absoluteDifference);
            if (absoluteDifference > 1.0e-6f)
                ++stats.meaningfulMismatches;

            const double differenceAsDouble =
                static_cast<double>(difference);
            sumOfSquares += differenceAsDouble * differenceAsDouble;
        }
    };

    compareChannel(first.left, second.left);
    compareChannel(first.right, second.right);

    const std::size_t sampleCount = first.left.size() + first.right.size();
    if (sampleCount > 0)
        stats.rmsDifference = std::sqrt(
            sumOfSquares / static_cast<double>(sampleCount));

    return stats;
}

// Deterministic stand-in for a message-thread Re-roll seed generator.  It is
// intentionally not called from render() or any process callback.
std::uint64_t makeTestRerollSeed(std::uint64_t previousSeed) noexcept
{
    std::uint64_t candidate = previousSeed
                            ^ UINT64_C(0xA5A5A5A5F00DBAAD);
    candidate += UINT64_C(0x9E3779B97F4A7C15);
    if (candidate == previousSeed)
        candidate ^= UINT64_C(1);
    return candidate;
}

juce::String statsDescription(const DifferenceStats& stats)
{
    return "bitMismatches="
         + juce::String(static_cast<int>(stats.bitMismatches))
         + " meaningfulMismatches="
         + juce::String(static_cast<int>(stats.meaningfulMismatches))
         + " maxAbs="
         + juce::String(stats.maximumAbsoluteDifference)
         + " rms="
         + juce::String(stats.rmsDifference);
}

} // namespace

// =============================================================================

class SeedingTests final : public juce::UnitTest
{
public:
    SeedingTests()
        : juce::UnitTest("SeedingTests", "ShredDouble/Seeding") {}

    void runTest() override
    {
        testSameSeedIsBitIdentical();
        testDifferentSeedsProduceDifferentAudio();
        testRerollAppliesDistinctSeedAndChangesAudio();
    }

private:
    void testSameSeedIsBitIdentical()
    {
        beginTest("Same masterSeed and parameters produce bit-identical engine output");

        const auto input = makeInput();
        const auto first = renderFromFreshEngine(kInitialSeed, input);
        const auto second = renderFromFreshEngine(kInitialSeed, input);
        const auto stats = compareAudio(first, second);

        // renderFromFreshEngine uses two independently constructed engines;
        // equality here therefore covers prepare, seed, reset, and process.
        expect(stats.bitMismatches == 0,
               "Same-seed renders must be bit-identical: "
               + statsDescription(stats));
    }

    void testDifferentSeedsProduceDifferentAudio()
    {
        beginTest("Different masterSeeds produce materially different audio");

        const auto input = makeInput();
        const auto first = renderFromFreshEngine(kInitialSeed, input);
        const auto second = renderFromFreshEngine(kDifferentSeed, input);
        const auto stats = compareAudio(first, second);

        // Require both a bit-level difference and a signal-sized difference,
        // rather than accepting a handful of insignificant floating-point
        // round-off changes.
        expect(stats.bitMismatches > 0,
               "Different seeds must not render bit-identical audio: "
               + statsDescription(stats));
        expect(stats.meaningfulMismatches > 32,
               "Different seeds must change more than trivial float noise: "
               + statsDescription(stats));
        expect(stats.maximumAbsoluteDifference > 1.0e-4f,
               "Different seeds must produce a measurable waveform change: "
               + statsDescription(stats));
        expect(stats.rmsDifference > 1.0e-6,
               "Different seeds must produce a non-trivial RMS difference: "
               + statsDescription(stats));
    }

    void testRerollAppliesDistinctSeedAndChangesAudio()
    {
        beginTest("Re-roll seed is distinct and changes subsequent engine audio");

        const auto input = makeInput();
        ShredDoubleEngine engine;
        configureEngine(engine);
        engine.prepare(kSampleRate, kMaximumBlockSize,
                       ShredDoubleEngine::LatencyMode::Studio);
        engine.seed(kInitialSeed);
        engine.reset();

        RenderedAudio beforeReroll;
        render(engine, input, beforeReroll);
        const auto previousSeed = engine.getMasterSeed();

        // This helper represents the message-thread/UI side of Re-roll.  No
        // seed generation occurs in render() or on the simulated audio path.
        const auto newSeed = makeTestRerollSeed(previousSeed);
        engine.seed(newSeed);
        const auto appliedSeed = engine.getMasterSeed();
        engine.reset();

        expect(appliedSeed != previousSeed,
               "Re-roll must apply a masterSeed distinct from the previous one");
        expect(appliedSeed == newSeed,
               "Engine must report the seed supplied by the Re-roll action");

        RenderedAudio afterReroll;
        render(engine, input, afterReroll);
        const auto stats = compareAudio(beforeReroll, afterReroll);

        expect(stats.bitMismatches > 0,
               "A re-rolled seed must change subsequent audio: "
               + statsDescription(stats));
        expect(stats.meaningfulMismatches > 32,
               "A re-rolled seed must change audio beyond float noise: "
               + statsDescription(stats));
        expect(stats.maximumAbsoluteDifference > 1.0e-4f,
               "A re-rolled seed must produce a measurable waveform change: "
               + statsDescription(stats));
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static SeedingTests seedingTestsInstance;
