// =============================================================================
// ShredDouble - Pitch and Correlation Correctness Tests
// =============================================================================
// These checks exercise the public DSP APIs at the two places where a
// plausible-looking render can still be mechanically wrong:
//
//   * PitchShifter: a known static detune must put a pure tone at
//       f * 2^(deltaC / 1200).
//   * ShredDoubleEngine: width must change the stereo correlation without
//       throwing away the shared, mono-safe low band.
//
// The test intentionally uses no third-party FFT or test framework.  The
// runner and registration pattern are the same juce::UnitTest pattern used by
// DSPPrimitiveTests, EngineTests, and SeedingTests.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "Source/DSP/PitchShifter.h"
#include "Source/DSP/ShredDoubleEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int kMaximumBlockSize = 512;
constexpr double kPi = 3.14159265358979323846;

// Pitch test analysis contract:
//   FFT size: 65,536 samples (radix-2, bin spacing 0.732 Hz at 48 kHz).
//   Hop: one FFT-sized hop; this is one settled, non-overlapping frame.
//   Window: Hann, applied once before the FFT.
//   Cents tolerance: 5 cents, with a three-bin log-magnitude parabolic
//   interpolation around the largest bin.  The tolerance is deliberately
//   wider than the residual granular sidebands but much smaller than the
//   +12-cent stimulus being checked.
constexpr int kPitchFftSize = 1 << 16;
constexpr int kPitchFftHop = kPitchFftSize;
constexpr int kPitchSettleSamples = 8192;
constexpr double kPitchDeltaCents = 12.0;
constexpr double kPitchToleranceCents = 5.0;
constexpr double kPitchInputHz = 1000.0;

// MeterTap uses a 300 ms correlation smoother.  Render a long measurement
// window after a separate settling region so neither the reported shifter
// latency nor the meter's initial zero state dominates the result.
constexpr int kCorrelationSettleSamples = 8192;
constexpr int kCorrelationMeasureSamples = 144000; // 3 seconds at 48 kHz
constexpr float kWideWidthPercent = 200.0f;
constexpr float kMonoWidthPercent = 0.0f;
constexpr std::uint64_t kCorrectnessSeed = UINT64_C(0xC0DEC75EED);

// -----------------------------------------------------------------------------
// Small signal helpers.
// -----------------------------------------------------------------------------

double rmsRange(const float *data, int first, int last)
{
    if (data == nullptr || last <= first)
        return 0.0;

    double sumSquares = 0.0;
    for (int i = first; i < last; ++i)
    {
        const double sample =
            static_cast<double>(data[static_cast<std::size_t>(i)]);
        sumSquares += sample * sample;
    }

    return std::sqrt(sumSquares / static_cast<double>(last - first));
}

void fillSine(float *data, int numSamples, double frequencyHz,
              double sampleRate, float amplitude = 0.5f)
{
    for (int i = 0; i < numSamples; ++i)
    {
        data[static_cast<std::size_t>(i)] = static_cast<float>(
            amplitude
            * std::sin(2.0 * kPi * frequencyHz
                       * static_cast<double>(i) / sampleRate));
    }
}

// Fixed LCG noise is sufficient here: it is deterministic, broadband, and
// does not share any state with the engine's seeded voice walks.
std::vector<float> makeNoise(int numSamples, std::uint32_t seed)
{
    std::vector<float> noise(static_cast<std::size_t>(numSamples));
    std::uint32_t state = seed;

    for (int i = 0; i < numSamples; ++i)
    {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        const float unit = static_cast<float>(state >> 8)
                         * (1.0f / 16777216.0f);
        noise[static_cast<std::size_t>(i)] = 2.0f * unit - 1.0f;
    }

    return noise;
}

// -----------------------------------------------------------------------------
// Self-contained radix-2 FFT.
// -----------------------------------------------------------------------------

void radix2Fft(std::vector<std::complex<double>> &data)
{
    const std::size_t size = data.size();

    // In-place bit reversal permutation.
    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        std::size_t bit = size >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j)
            std::swap(data[i], data[j]);
    }

    for (std::size_t length = 2; length <= size; length <<= 1)
    {
        const double angle = -2.0 * kPi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        const std::size_t halfLength = length / 2;

        for (std::size_t start = 0; start < size; start += length)
        {
            std::complex<double> rotation(1.0, 0.0);
            for (std::size_t i = 0; i < halfLength; ++i)
            {
                const auto even = data[start + i];
                const auto odd = rotation * data[start + i + halfLength];
                data[start + i] = even + odd;
                data[start + i + halfLength] = even - odd;
                rotation *= step;
            }
        }
    }
}

// Finds the positive-frequency peak near `centerHz`.  A Hann-windowed tone
// usually straddles two bins, so interpolate the log magnitude of the peak
// bin and its immediate neighbours rather than asserting on a raw bin index.
double fftPeakFrequency(const float *signal, int offset, double sampleRate,
                        double centerHz)
{
    std::vector<std::complex<double>> spectrum(
        static_cast<std::size_t>(kPitchFftSize));

    for (int i = 0; i < kPitchFftSize; ++i)
    {
        const double phase =
            2.0 * kPi * static_cast<double>(i)
            / static_cast<double>(kPitchFftSize - 1);
        const double hann = 0.5 * (1.0 - std::cos(phase));
        spectrum[static_cast<std::size_t>(i)] = std::complex<double>(
            static_cast<double>(signal[static_cast<std::size_t>(offset + i)])
                * hann,
            0.0);
    }

    radix2Fft(spectrum);

    const double searchHalfWidthHz = 80.0;
    const auto firstBin = static_cast<std::size_t>(std::max(
        1.0, std::floor((centerHz - searchHalfWidthHz)
                        * static_cast<double>(kPitchFftSize) / sampleRate)));
    const auto lastBin = static_cast<std::size_t>(std::min(
        static_cast<double>(kPitchFftSize / 2 - 2),
        std::ceil((centerHz + searchHalfWidthHz)
                  * static_cast<double>(kPitchFftSize) / sampleRate)));

    std::size_t peakBin = firstBin;
    double peakMagnitude = 0.0;
    for (std::size_t bin = firstBin; bin <= lastBin; ++bin)
    {
        const double magnitude = std::abs(spectrum[bin]);
        if (magnitude > peakMagnitude)
        {
            peakMagnitude = magnitude;
            peakBin = bin;
        }
    }

    double binOffset = 0.0;
    if (peakBin > 0 && peakBin + 1 < spectrum.size())
    {
        const double left = std::log(
            std::max(std::abs(spectrum[peakBin - 1]), 1.0e-30));
        const double centre = std::log(
            std::max(std::abs(spectrum[peakBin]), 1.0e-30));
        const double right = std::log(
            std::max(std::abs(spectrum[peakBin + 1]), 1.0e-30));
        const double denominator = left - 2.0 * centre + right;

        if (std::abs(denominator) > 1.0e-12)
            binOffset = 0.5 * (left - right) / denominator;
    }

    binOffset = std::clamp(binOffset, -0.5, 0.5);
    return (static_cast<double>(peakBin) + binOffset)
         * sampleRate / static_cast<double>(kPitchFftSize);
}

// -----------------------------------------------------------------------------
// Engine render helpers.
// -----------------------------------------------------------------------------

struct StereoRender
{
    std::vector<float> left;
    std::vector<float> right;
    float finalCorrelation = 0.0f;
};

void configureOpenDouble(ShredDoubleEngine &engine, float widthPercent,
                         float monoXoverHz = 180.0f)
{
    // Open, meaningful double: both timing and pitch walks are at their
    // largest configurable spreads, with wet-only output and four active
    // voices.  This makes a width change observable rather than testing a
    // nearly-mono/zero-wet corner case.
    engine.setVoiceCount(4);
    engine.setTimingSpreadMs(15.0f);
    engine.setPitchSpreadCents(10.0f);
    engine.setHumanRate(100.0f);
    engine.setTightness(0.0f);
    engine.setPanPercents(-100.0f, 100.0f, -60.0f, 60.0f);
    engine.setMonoXoverHz(monoXoverHz);
    engine.setWidthPercent(widthPercent);
    engine.setMix(1.0f);
    engine.setOutTrimDb(0.0f);
    engine.setLatencyMode(ShredDoubleEngine::LatencyMode::Studio);
    engine.setBypass(false);

    engine.prepare(kSampleRate, kMaximumBlockSize,
                   ShredDoubleEngine::LatencyMode::Studio);
    engine.seed(kCorrectnessSeed);
    engine.reset();
}

// Process a mono source in host-sized blocks.  MeterTap is reset exactly at
// the end of the settling region, so its 300 ms one-pole reports only the
// measurement window while the DSP itself has already run past PDC latency.
StereoRender renderMonoEngine(ShredDoubleEngine &engine,
                              const std::vector<float> &input,
                              int settleSamples)
{
    StereoRender render;
    render.left.assign(input.size(), 0.0f);
    render.right.assign(input.size(), 0.0f);

    const int totalSamples = static_cast<int>(input.size());
    for (int offset = 0; offset < totalSamples; offset += kMaximumBlockSize)
    {
        const int blockSize = std::min(kMaximumBlockSize,
                                       totalSamples - offset);
        engine.process(input.data() + offset, nullptr,
                       render.left.data() + offset,
                       render.right.data() + offset, blockSize);

        if (offset + blockSize == settleSamples)
            engine.getMeterTap().reset();
    }

    render.finalCorrelation = engine.getMeterTap().getCorrelation();
    return render;
}

} // namespace

// =============================================================================

class CorrectnessTests final : public juce::UnitTest
{
public:
    CorrectnessTests()
        : juce::UnitTest("CorrectnessTests", "ShredDouble/Correctness") {}

    void runTest() override
    {
        testPitchAccuracyFromFftPeak();
        testWidthChangesMeterCorrelation();
        testSubCrossoverEnergyStaysMonoSafe();
    }

private:
    void testPitchAccuracyFromFftPeak()
    {
        beginTest("PitchShifter: FFT peak follows f * 2^(deltaC/1200)");

        PitchShifter shifter;
        shifter.prepare(kSampleRate, kMaximumBlockSize);
        shifter.setLatencyMode(PitchShifter::LatencyMode::studio);
        shifter.setDetuneCents(static_cast<float>(kPitchDeltaCents));

        const int totalSamples = kPitchSettleSamples + kPitchFftHop;
        std::vector<float> tone(static_cast<std::size_t>(totalSamples), 0.0f);
        fillSine(tone.data(), totalSamples, kPitchInputHz, kSampleRate);
        shifter.process(tone.data(), totalSamples);

        const double expectedHz =
            kPitchInputHz * std::pow(2.0, kPitchDeltaCents / 1200.0);
        const double measuredHz = fftPeakFrequency(
            tone.data(), kPitchSettleSamples, kSampleRate, expectedHz);
        const double centsError =
            1200.0 * std::abs(std::log2(measuredHz / expectedHz));

        expect(std::isfinite(measuredHz) && measuredHz > 0.0,
               "FFT must produce a finite positive spectral peak; measuredHz="
               + juce::String(measuredHz));
        expect(centsError <= kPitchToleranceCents,
               "Shifted peak must be within "
               + juce::String(kPitchToleranceCents) + " cents; expectedHz="
               + juce::String(expectedHz) + " measuredHz="
               + juce::String(measuredHz) + " centsError="
               + juce::String(centsError));
    }

    void testWidthChangesMeterCorrelation()
    {
        beginTest("Engine/MeterTap: Width=200% lowers rho while Width=0% folds to rho≈1");

        const int totalSamples =
            kCorrelationSettleSamples + kCorrelationMeasureSamples;
        const auto input = makeNoise(totalSamples, UINT32_C(0x51D3C0DE));

        ShredDoubleEngine wideEngine;
        configureOpenDouble(wideEngine, kWideWidthPercent);
        expect(kCorrelationSettleSamples > wideEngine.getLatencySamples(),
               "Correlation measurement must start after the reported PDC latency");
        const auto wide = renderMonoEngine(
            wideEngine, input, kCorrelationSettleSamples);

        ShredDoubleEngine monoEngine;
        configureOpenDouble(monoEngine, kMonoWidthPercent);
        const auto mono = renderMonoEngine(
            monoEngine, input, kCorrelationSettleSamples);

        // Width=200% is measured on the wet, wide, fully humanised render;
        // it must be materially below the mono-fold result.  The loose upper
        // bound accommodates the shared low band and the implementation's
        // deliberately gentle allpass/tilt decorrelation.
        expect(wide.finalCorrelation < 0.75f,
               "A 200% wide double should have substantially reduced rho; rho="
               + juce::String(wide.finalCorrelation));
        expect(wide.finalCorrelation < mono.finalCorrelation - 0.25f,
               "Width must lower rho relative to the same double folded to mono; wide="
               + juce::String(wide.finalCorrelation) + " mono="
               + juce::String(mono.finalCorrelation));

        // At width zero the M/S side is discarded, so both final channels
        // are the same even though the underlying double remains active.
        expect(mono.finalCorrelation > 0.995f,
               "Width=0% must fold the final output to nearly perfect mono; rho="
               + juce::String(mono.finalCorrelation));
    }

    void testSubCrossoverEnergyStaysMonoSafe()
    {
        beginTest("Engine: sub-crossover tone retains folded energy and stays L≈R at Width=200%");

        constexpr double probeFrequencyHz = 50.0;
        constexpr float probeAmplitude = 0.5f;
        constexpr int measurementSamples = 32768;
        constexpr float crossoverHz = 180.0f;

        ShredDoubleEngine engine;
        configureOpenDouble(engine, kWideWidthPercent, crossoverHz);

        // The explicit latency term documents that the comparison is made
        // after the reported PDC; the additional 4096 samples cover the
        // pitch/delay/allpass and crossover startup histories.
        const int settleSamples = engine.getLatencySamples() + 4096;
        const int totalSamples = settleSamples + measurementSamples;

        std::vector<float> input(static_cast<std::size_t>(totalSamples), 0.0f);
        fillSine(input.data(), totalSamples, probeFrequencyHz, kSampleRate,
                 probeAmplitude);

        const auto render = renderMonoEngine(engine, input, settleSamples);
        const double inputRms = rmsRange(input.data(), settleSamples,
                                         totalSamples);
        const double leftRms = rmsRange(render.left.data(), settleSamples,
                                        totalSamples);
        const double rightRms = rmsRange(render.right.data(), settleSamples,
                                         totalSamples);

        double sumDifferenceSquares = 0.0;
        double sumFoldedSquares = 0.0;
        for (int i = settleSamples; i < totalSamples; ++i)
        {
            const double left =
                static_cast<double>(render.left[static_cast<std::size_t>(i)]);
            const double right =
                static_cast<double>(render.right[static_cast<std::size_t>(i)]);
            const double difference = left - right;
            const double folded = 0.5 * (left + right);
            sumDifferenceSquares += difference * difference;
            sumFoldedSquares += folded * folded;
        }

        const double sampleCount = static_cast<double>(measurementSamples);
        const double differenceRms = std::sqrt(
            sumDifferenceSquares / sampleCount);
        const double foldedRms = std::sqrt(sumFoldedSquares / sampleCount);
        const double channelRms = std::max({leftRms, rightRms, 1.0e-12});

        // 50 Hz is well below the 180 Hz crossover.  The shared LP therefore
        // remains audible after mono fold-down even while the high-band
        // double is spread and width-expanded.
        expect(foldedRms > 0.35 * inputRms,
               "Sub-crossover folded energy must be retained; inputRms="
               + juce::String(inputRms) + " foldedRms="
               + juce::String(foldedRms));
        expect(foldedRms > 0.1,
               "Sub-crossover folded output must not collapse toward silence; foldedRms="
               + juce::String(foldedRms));
        expect(differenceRms / channelRms < 0.5,
               "Sub-crossover output must remain substantially mono-safe; |L-R|/channel="
               + juce::String(differenceRms / channelRms) + " leftRms="
               + juce::String(leftRms) + " rightRms="
               + juce::String(rightRms));
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static CorrectnessTests correctnessTestsInstance;
