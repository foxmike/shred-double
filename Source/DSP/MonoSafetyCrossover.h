#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

/**
    Linkwitz-Riley crossover for the mono-safe wet path.

    The lowpass is one shared cascade fed from the dry mono source. The two
    highpass cascades have independent state and are fed from the left and
    right shifted/delayed signals.

    SHREDWIDTH_XOVER_ORDER=4 selects two cascaded second-order Butterworth
    sections (LR4). An undefined macro, or a value of 2, selects two cascaded
    first-order sections (LR2).

    setCutoffHz() only marks the coefficients dirty. Call beginBlock() once at
    the start of a sample-by-sample processing block, or use processBlock(),
    which does this automatically. Coefficient storage and filter state are
    allocated in prepare(); processing and coefficient updates do not allocate.
*/
class MonoSafetyCrossover {
public:
    struct Outputs {
        float low = 0.0f;
        float highLeft = 0.0f;
        float highRight = 0.0f;
    };

    /** Prepares all fixed filter state using the current cutoff. */
    void prepare(double sampleRate, int maximumBlockSize);

    /** juce::dsp::ProcessSpec convenience overload. */
    void prepare(const juce::dsp::ProcessSpec &spec);

    /** Clears all filter histories without changing the cutoff. */
    void reset() noexcept;

    /** Marks a new cutoff for application at the next block boundary. */
    void setCutoffHz(float newCutoffHz) noexcept;

    /**
        Applies dirty coefficients once. Call exactly once before a block when
        using process() sample by sample.
    */
    void beginBlock() noexcept;

    /** Processes one dry mono sample and two shifted/delayed wet samples. */
    Outputs process(float dryMono, float wetLeft, float wetRight) noexcept;

    /**
        Processes separate input/output arrays. Inputs and corresponding
        outputs may alias. Dirty coefficients are applied once before sample 0.
    */
    void processBlock(const float *dryMono,
                      const float *wetLeft,
                      const float *wetRight,
                      float *low,
                      float *highLeft,
                      float *highRight,
                      int numSamples) noexcept;

    float processLowpass(float dryMono) noexcept;
    float processHighpassLeft(float wetLeft) noexcept;
    float processHighpassRight(float wetRight) noexcept;

    float getCutoffHz() const noexcept { return cutoffHz; }
    bool isPrepared() const noexcept { return prepared; }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;
    using CoefficientsPtr = Coefficients::Ptr;

    struct Cascade {
        std::array<Filter, 2> sections;

        void setCoefficients(const CoefficientsPtr &newCoefficients) noexcept;
        void prepare(const juce::dsp::ProcessSpec &spec) noexcept;
        void reset() noexcept;
        float process(float sample) noexcept;
    };

    static constexpr float defaultCutoffHz = 180.0f;

    Cascade lowpass;
    Cascade highpassLeft;
    Cascade highpassRight;

    CoefficientsPtr lowpassCoefficients;
    CoefficientsPtr highpassCoefficients;

    double sampleRate = 0.0;
    float cutoffHz = defaultCutoffHz;
    bool cutoffDirty = true;
    bool prepared = false;

    void updateCoefficients() noexcept;
};
