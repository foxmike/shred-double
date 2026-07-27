#pragma once

#include "PluginProcessor.h"
#include "StateManager.h"
#include "GUI/CustomLookAndFeel.h"

/**
    A minimal GUI Editor skeleton for ShredDouble featuring the masterSeed Re-roll control.
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

    ShredDoubleAudioProcessor& audioProcessor;
    StateManager stateManager;

    CustomLookAndFeel lookAndFeel;

    // Re-roll seed interface
    juce::TextButton reRollButton { "RE-ROLL SEED" };
    juce::Label seedTitleLabel { "seedTitle", "MASTER SEED" };
    juce::Label seedValLabel { "seedVal", "0" };

    // Resize limits and corner resizer
    std::unique_ptr<juce::ResizableCornerComponent> resizer;
    juce::ComponentBoundsConstrainer resizeLimits;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredDoubleAudioProcessorEditor)
};
