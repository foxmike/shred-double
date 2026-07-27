#pragma once

#include "RandomWalk.h"
#include "PitchShifter.h"
#include "DelayLineFrac.h"
#include "AllpassChain.h"
#include "TiltFilter.h"

#include <juce_dsp/juce_dsp.h>
#include <cstdint>

/**
    Voice container owning 2 RandomWalks + PitchShifter + DelayLineFrac + 
    AllpassChain + TiltFilter. Performs per-sample processing with 
    interpolated walk values and constant-power panning.
*/
class Voice {
public:
    //==========================================================================
    // Lifecycle
    //==========================================================================

    /** Constructs a Voice with a given voice index. */
    explicit Voice(int index = 0) noexcept;

    /** Prepares all child DSP components. Sizes DelayLineFrac and prepares 
        PitchShifter for Studio maximum. Sets AllpassChain and TiltFilter voice index.
        Reseeds walks and resets them to their mean.
    */
    void prepare(double sampleRate, int maxBlockSize);

    /** juce::dsp::ProcessSpec overload. */
    void prepare(const juce::dsp::ProcessSpec& spec);

    /** Resets the state of the voice and all child components. */
    void reset() noexcept;

    /** Reseeds the walks using a new master seed and resets. */
    void reseed(std::uint64_t masterSeed) noexcept;

    //==========================================================================
    // Audio Processing
    //==========================================================================

    /** Processes one sample. Advances and interpolates walks to get tau_ms 
        and deltaC, feeds them to PitchShifter and DelayLineFrac, runs 
        through AllpassChain and TiltFilter, and returns the mono result.
    */
    float processSample(float x) noexcept;

    /** Processes one sample and accumulates the panned/muted output into 
        the caller's L and R accumulators.
    */
    void processSample(float x, float& accL, float& accR) noexcept;

    //==========================================================================
    // Parameters / Control
    //==========================================================================

    /** Sets constant-power panning gains panL and panR from panPercent in [-100, 100]. */
    void setPanPercent(float panPercent) noexcept;

    /** Sets nominal delay in milliseconds (forwarded to delayWalk). */
    void setNominalDelayMs(float ms) noexcept;

    /** Sets timing spread in milliseconds (forwarded to delayWalk). */
    void setTimingSpreadMs(float ms) noexcept;

    /** Sets pitch spread in cents (forwarded to pitchWalk). */
    void setPitchSpreadCents(float cents) noexcept;

    /** Sets humanization rate in percent (forwarded to both walks). */
    void setHumanRate(float rate) noexcept;

    /** Sets tightness [0, 1] (forwarded to both walks). */
    void setTightness(float tightness) noexcept;

    /** Sets latency mode (forwarded to PitchShifter). */
    void setLatencyMode(PitchShifter::LatencyMode mode) noexcept;

    /** Sets whether the voice is muted. */
    void setMuted(bool shouldBeMuted) noexcept;

    /** Sets parameters at block rate. */
    void setParams(float timingSpreadMs,
                   float pitchSpreadCents,
                   float humanRate,
                   float tightness,
                   PitchShifter::LatencyMode latencyMode,
                   float muteGain = 1.0f) noexcept;

    //==========================================================================
    // Getters / Introspection
    //==========================================================================

    /** Returns current delay walk offset in milliseconds. */
    float getCurrentDelayMs() const noexcept { return currentTauMs; }

    /** Returns current pitch walk offset in cents. */
    float getCurrentDetuneCents() const noexcept { return currentDeltaC; }

    int getVoiceIndex() const noexcept { return voiceIndex; }
    void setVoiceIndex(int index) noexcept;

    float getNominalDelayMs() const noexcept { return nominalDelayMs; }
    float getPanL() const noexcept { return panL; }
    float getPanR() const noexcept { return panR; }
    bool isVoiceMuted() const noexcept { return isMuted; }
    float getMuteGain() const noexcept { return muteGain; }
    std::uint64_t getMasterSeed() const noexcept { return masterSeed; }

private:
    RandomWalk delayWalk;
    RandomWalk pitchWalk;
    PitchShifter shifter;
    DelayLineFrac delay;
    AllpassChain diffusion;
    TiltFilter tilt;

    float nominalDelayMs = 20.0f;
    float panL = 0.7071f;
    float panR = 0.7071f;
    int voiceIndex = 0;
    std::uint64_t masterSeed = 0;

    float muteGain = 1.0f;
    bool isMuted = false;

    // Last generated walk values, stored for diagnostics/getters
    float currentTauMs = 20.0f;
    float currentDeltaC = 0.0f;
};
