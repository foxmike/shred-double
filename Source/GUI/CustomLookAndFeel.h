#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();
    ~CustomLookAndFeel() override;

    // Static color constants for ShredWidth
    static const juce::Colour shredDarkBg;
    static const juce::Colour shredPanelBg;
    static const juce::Colour shredOutline;
    static const juce::Colour shredText;
    static const juce::Colour shredMutedText;
    
    // Accent colors
    static const juce::Colour accentCyan;   // Primary: Detune, Width
    static const juce::Colour accentOrange; // Secondary: Delay, Crossover
    static const juce::Colour accentGreen;  // Tertiary: Mix, Output Trim

    // LookAndFeel_V4 overrides
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
    
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustomLookAndFeel)
};
