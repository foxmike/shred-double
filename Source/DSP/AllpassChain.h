#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

/**
    Four-stage, single-channel Schroeder allpass diffusion chain.

    Each stage implements
        y(n) = -g * x(n) + x(n-M) + g * y(n-M)
    with a per-voice delay set derived from the 48 kHz reference table in
    ShredDouble_Build_Spec section 4.5A. Delay lengths are scaled in prepare()
    so their absolute times remain stable at other sample rates.

    prepare() is the only allocating operation. processSample(), process(),
    reset(), and voice selection after preparation are heap-free.
*/
class AllpassChain {
public:
    static constexpr int numStages = 4;
    static constexpr int numVoices = 4;
    static constexpr float defaultGain = 0.6f;
    static constexpr double referenceSampleRate = 48000.0;

    using DelaySet = std::array<int, numStages>;

    /** Reference delay lengths at 48 kHz, one row per voice. */
    inline static constexpr std::array<DelaySet, numVoices> referenceDelays{{
        {{13, 29, 47, 71}},
        {{17, 37, 53, 79}},
        {{19, 41, 59, 83}},
        {{23, 43, 61, 89}},
    }};

    /** Selects voice 0..3. If already prepared, applies the corresponding
        scaled delays and clears the old delay memory without allocating. */
    void setVoiceIndex(int newVoiceIndex) noexcept;

    /** Allocates all stage buffers and configures the sample-rate-scaled
        delays. maxBlockSize is accepted for process API symmetry. */
    void prepare(double sampleRate, int maxBlockSize = 0);

    /** juce::dsp::ProcessSpec overload. */
    void prepare(const juce::dsp::ProcessSpec &spec);

    /** Clears all stage delay memory. */
    void reset() noexcept;

    /** Processes one sample through all four stages in series. */
    float processSample(float x) noexcept;

    /** In-place block processing. */
    void process(float *data, int numSamples) noexcept;

    int getVoiceIndex() const noexcept { return voiceIndex; }
    int getStageDelaySamples(int stageIndex) const noexcept;
    bool isPrepared() const noexcept { return prepared; }

private:
    struct Stage {
        juce::HeapBlock<float> buffer;
        int capacity = 0;
        int delaySamples = 1;
        int writePosition = 0;
    };

    std::array<Stage, numStages> stages;
    double currentSampleRate = referenceSampleRate;
    int voiceIndex = 0;
    float gain = defaultGain;
    bool prepared = false;

    static DelaySet makeScaledDelays(int voice, double sampleRate) noexcept;
    void applySelectedDelays() noexcept;
    float processStage(Stage &stage, float x) noexcept;
};
