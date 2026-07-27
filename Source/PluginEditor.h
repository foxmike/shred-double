#pragma once

#include "PluginProcessor.h"
#include "StateManager.h"
#include "GUI/CustomLookAndFeel.h"
#include "GUI/RotarySlider.h"
#include "GUI/GoniometerComponent.h"
#include "GUI/CorrelationMeter.h"
#include "GUI/DriftDisplay.h"

/**
    A fully featured GUI Editor for the ShredDouble Audio Processor.
*/
class ShredDoubleAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit ShredDoubleAudioProcessorEditor (ShredDoubleAudioProcessor&);
    ~ShredDoubleAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateSeedDisplay();
    void updateABButtonStates();
    void updateVoiceCountButtonStates();
    void updateLatencyButtonStates();
    void applyPreset (int id);

    ShredDoubleAudioProcessor& audioProcessor;
    StateManager stateManager;

    CustomLookAndFeel lookAndFeel;

    // Header Components
    juce::ComboBox presetComboBox;
    juce::TextButton aButton { "A" };
    juce::TextButton bButton { "B" };
    juce::TextButton copyButton { "Copy" };
    juce::TextButton bypassButton { "BYPASS" };
    bool lastBypassedState = false;

    // Re-roll seed interface
    juce::TextButton reRollButton { "RE-ROLL" };
    juce::Label seedTitleLabel { "seedTitle", "MASTER SEED" };
    juce::Label seedValLabel { "seedVal", "0" };

    // Voice & Humanization Controls
    juce::Label voiceCountLabel { "voiceCountLabel", "VOICES" };
    juce::TextButton voice2Button { "2" };
    juce::TextButton voice4Button { "4" };

    RotarySlider panV1Slider { "Pan V1", " %", false };
    RotarySlider panV2Slider { "Pan V2", " %", false };
    RotarySlider panV3Slider { "Pan V3", " %", false };
    RotarySlider panV4Slider { "Pan V4", " %", false };

    RotarySlider timingSpreadSlider { "Timing Spread", " ms", false };
    RotarySlider pitchSpreadSlider { "Pitch Spread", " cents", false };
    RotarySlider humanRateSlider { "Humanization Rate", " %", false };
    RotarySlider tightnessSlider { "Tightness", " %", false };

    // Global Section
    RotarySlider monoXoverSlider { "Low Mono Crossover", " Hz", false };
    RotarySlider widthSlider { "Stereo Width", " %", false };
    RotarySlider mixSlider { "Mix", " %", false };
    RotarySlider outTrimSlider { "Output Trim", " dB", false };

    juce::Label latencyLabel { "latencyLabel", "LATENCY MODE" };
    juce::TextButton studioButton { "STUDIO" };
    juce::TextButton liveButton { "LIVE" };

    // Metering Column
    GoniometerComponent goniometer;
    CorrelationMeter correlationMeter;
    DriftDisplay driftDisplay;

    // APVTS Attachments
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<SliderAttachment> panV1Attachment;
    std::unique_ptr<SliderAttachment> panV2Attachment;
    std::unique_ptr<SliderAttachment> panV3Attachment;
    std::unique_ptr<SliderAttachment> panV4Attachment;
    std::unique_ptr<SliderAttachment> timingSpreadAttachment;
    std::unique_ptr<SliderAttachment> pitchSpreadAttachment;
    std::unique_ptr<SliderAttachment> humanRateAttachment;
    std::unique_ptr<SliderAttachment> tightnessAttachment;
    std::unique_ptr<SliderAttachment> monoXoverAttachment;
    std::unique_ptr<SliderAttachment> widthAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> outTrimAttachment;

    // Resize limits and corner resizer
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    juce::ComponentBoundsConstrainer resizeLimits;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredDoubleAudioProcessorEditor)
};
