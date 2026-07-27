#include "RotarySlider.h"

RotarySlider::RotarySlider()
{
    init ("", "", true);
}

RotarySlider::RotarySlider (const juce::String& labelText, const juce::String& valueSuffix, bool useLocalLookAndFeel)
{
    init (labelText, valueSuffix, useLocalLookAndFeel);
}

RotarySlider::~RotarySlider()
{
    setLookAndFeel (nullptr);
}

void RotarySlider::setLabelText (const juce::String& newText)
{
    labelText = newText;
    labelComponent.setText (labelText, juce::dontSendNotification);
    
    if (labelText.isNotEmpty())
    {
        if (! labelComponent.getParentComponent())
            addAndMakeVisible (labelComponent);
    }
    else
    {
        removeChildComponent (&labelComponent);
    }
    
    resized();
}

juce::String RotarySlider::getLabelText() const
{
    return labelText;
}

void RotarySlider::setAccentColor (juce::Colour color)
{
    setColour (juce::Slider::rotarySliderFillColourId, color);
    setColour (juce::Slider::thumbColourId, color);
}

void RotarySlider::resized()
{
    juce::Slider::resized();
    
    if (labelText.isNotEmpty())
    {
        labelComponent.setBounds (0, 0, getWidth(), 14);
    }
}

void RotarySlider::init (const juce::String& name, const juce::String& suffix, bool useLocalLookAndFeel)
{
    labelText = name;
    
    if (useLocalLookAndFeel)
    {
        localLookAndFeel = std::make_unique<CustomLookAndFeel>();
        setLookAndFeel (localLookAndFeel.get());
    }

    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    setTextValueSuffix (suffix);
    
    // Default accent colour configuration (can be overridden)
    juce::Colour defaultAccent = CustomLookAndFeel::accentCyan;
    auto lowerName = labelText.toLowerCase();
    if (lowerName.contains ("detune") || lowerName.contains ("width"))
    {
        defaultAccent = CustomLookAndFeel::accentCyan;
    }
    else if (lowerName.contains ("delay") || lowerName.contains ("xover") || lowerName.contains ("crossover") || lowerName.contains ("mono"))
    {
        defaultAccent = CustomLookAndFeel::accentOrange;
    }
    else if (lowerName.contains ("mix") || lowerName.contains ("trim") || lowerName.contains ("output") || lowerName.contains ("gain"))
    {
        defaultAccent = CustomLookAndFeel::accentGreen;
    }
    
    setAccentColor (defaultAccent);

    // Setup label component
    labelComponent.setText (labelText, juce::dontSendNotification);
    labelComponent.setJustificationType (juce::Justification::centred);
    labelComponent.setColour (juce::Label::textColourId, CustomLookAndFeel::shredMutedText);
    labelComponent.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f).withStyle ("Bold")));
    
    if (labelText.isNotEmpty())
    {
        addAndMakeVisible (labelComponent);
    }
}
