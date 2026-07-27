#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CustomLookAndFeel.h"

class RotarySlider : public juce::Slider
{
public:
    RotarySlider();
    RotarySlider (const juce::String& labelText, const juce::String& valueSuffix = {}, bool useLocalLookAndFeel = true);
    ~RotarySlider() override;

    void setLabelText (const juce::String& newText);
    juce::String getLabelText() const;
    bool hasLabel() const { return labelText.isNotEmpty(); }

    void setAccentColor (juce::Colour color);

    void resized() override;

private:
    void init (const juce::String& name, const juce::String& suffix, bool useLocalLookAndFeel);

    juce::String labelText;
    juce::Label labelComponent;
    std::unique_ptr<CustomLookAndFeel> localLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RotarySlider)
};
