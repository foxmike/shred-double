#include "CustomLookAndFeel.h"
#include "RotarySlider.h"
#include <cmath>

const juce::Colour CustomLookAndFeel::shredDarkBg    (0xff080a0f);
const juce::Colour CustomLookAndFeel::shredPanelBg   (0xff141923);
const juce::Colour CustomLookAndFeel::shredOutline   (0xff283042);
const juce::Colour CustomLookAndFeel::shredText      (0xffe2e8f0);
const juce::Colour CustomLookAndFeel::shredMutedText (0xff68748a);

const juce::Colour CustomLookAndFeel::accentCyan     (0xff00e5ff);
const juce::Colour CustomLookAndFeel::accentOrange   (0xffffa500);
const juce::Colour CustomLookAndFeel::accentGreen    (0xff34c759);

CustomLookAndFeel::CustomLookAndFeel()
{
    setColourScheme (juce::LookAndFeel_V4::ColourScheme {
        shredDarkBg,
        shredPanelBg,
        shredDarkBg,
        shredOutline,
        shredText,
        accentCyan,
        shredText,
        shredOutline.brighter (0.1f),
        shredText
    });

    setColour (juce::Slider::thumbColourId, accentCyan);
    setColour (juce::Slider::trackColourId, accentCyan);
    setColour (juce::Slider::backgroundColourId, shredPanelBg);
    setColour (juce::Slider::rotarySliderOutlineColourId, shredOutline);
    setColour (juce::Slider::rotarySliderFillColourId, accentCyan);

    setColour (juce::Label::textColourId, shredText);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ComboBox::backgroundColourId, shredPanelBg);
    setColour (juce::ComboBox::outlineColourId, shredOutline);
    setColour (juce::ComboBox::textColourId, shredText);
    setColour (juce::ComboBox::buttonColourId, shredPanelBg);
    setColour (juce::ComboBox::arrowColourId, accentCyan);

    setColour (juce::PopupMenu::backgroundColourId, shredDarkBg);
    setColour (juce::PopupMenu::textColourId, shredText);
    setColour (juce::PopupMenu::headerTextColourId, shredMutedText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, shredOutline.brighter (0.1f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

CustomLookAndFeel::~CustomLookAndFeel() = default;

void CustomLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    
    if (auto* rotarySlider = dynamic_cast<RotarySlider*> (&slider))
    {
        if (rotarySlider->hasLabel())
        {
            bounds.removeFromTop (16.0f);
        }
    }

    auto centreX = bounds.getCentreX();
    auto centreY = bounds.getCentreY();

    auto outerRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
    auto knobRadius = outerRadius - 6.0f;

    if (knobRadius <= 0.0f)
        return;

    juce::Colour accentColour = CustomLookAndFeel::accentCyan;
    if (slider.isColourSpecified (juce::Slider::rotarySliderFillColourId))
    {
        accentColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    }
    else
    {
        auto name = slider.getName().toLowerCase() + " " + slider.getComponentID().toLowerCase();
        if (name.contains ("detune") || name.contains ("width"))
        {
            accentColour = CustomLookAndFeel::accentCyan;
        }
        else if (name.contains ("delay") || name.contains ("xover") || name.contains ("crossover") || name.contains ("mono"))
        {
            accentColour = CustomLookAndFeel::accentOrange;
        }
        else if (name.contains ("mix") || name.contains ("trim") || name.contains ("output") || name.contains ("gain"))
        {
            accentColour = CustomLookAndFeel::accentGreen;
        }
    }

    if (! slider.isEnabled())
    {
        accentColour = juce::Colours::grey;
    }

    // 1. Chassis cutout background
    g.setColour (CustomLookAndFeel::shredDarkBg);
    g.fillEllipse (centreX - outerRadius, centreY - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f);

    g.setColour (CustomLookAndFeel::shredOutline);
    g.drawEllipse (centreX - outerRadius, centreY - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f, 1.5f);

    // 2. Rotary track/arc
    auto currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    auto trackRadius = outerRadius - 2.5f;

    if (trackRadius > 0.0f)
    {
        juce::Path backgroundArc;
        backgroundArc.addCentredArc (centreX, centreY, trackRadius, trackRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (CustomLookAndFeel::shredOutline);
        g.strokePath (backgroundArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valueArc;
        valueArc.addCentredArc (centreX, centreY, trackRadius, trackRadius, 0.0f, rotaryStartAngle, currentAngle, true);
        g.setColour (accentColour);
        g.strokePath (valueArc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 3. Knob Body (Precision Brushed Aluminum)
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (centreX - knobRadius + 1.0f, centreY - knobRadius + 1.5f, knobRadius * 2.0f, knobRadius * 2.0f);

    // Anisotropic metal face gradient
    juce::ColourGradient metalGradient (
        juce::Colour (0xFFE5E5EA), centreX - knobRadius, centreY - knobRadius,
        juce::Colour (0xFF3A3D42), centreX + knobRadius, centreY + knobRadius,
        false
    );
    metalGradient.addColour (0.12, juce::Colour (0xFFF2F2F7));
    metalGradient.addColour (0.28, juce::Colour (0xFFAEAEB2));
    metalGradient.addColour (0.45, juce::Colour (0xFFD1D1D6));
    metalGradient.addColour (0.62, juce::Colour (0xFF636366));
    metalGradient.addColour (0.78, juce::Colour (0xFFC7C7CC));
    metalGradient.addColour (0.92, juce::Colour (0xFF48484A));

    g.setGradientFill (metalGradient);
    g.fillEllipse (centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

    // Concentric brushed sub-pixel rings
    g.setColour (juce::Colours::black.withAlpha (0.06f));
    g.drawEllipse (centreX - knobRadius * 0.85f, centreY - knobRadius * 0.85f, knobRadius * 1.7f, knobRadius * 1.7f, 0.5f);
    g.drawEllipse (centreX - knobRadius * 0.70f, centreY - knobRadius * 0.70f, knobRadius * 1.4f, knobRadius * 1.4f, 0.5f);
    g.drawEllipse (centreX - knobRadius * 0.55f, centreY - knobRadius * 0.55f, knobRadius * 1.1f, knobRadius * 1.1f, 0.5f);
    g.drawEllipse (centreX - knobRadius * 0.40f, centreY - knobRadius * 0.40f, knobRadius * 0.8f, knobRadius * 0.8f, 0.5f);
    g.drawEllipse (centreX - knobRadius * 0.25f, centreY - knobRadius * 0.25f, knobRadius * 0.5f, knobRadius * 0.5f, 0.5f);

    // Bevel outer rim
    juce::ColourGradient rimGradient (
        juce::Colours::white.withAlpha (0.6f), centreX - knobRadius, centreY - knobRadius,
        juce::Colours::black.withAlpha (0.35f), centreX + knobRadius, centreY + knobRadius,
        false
    );
    g.setGradientFill (rimGradient);
    g.drawEllipse (centreX - knobRadius, centreY - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.0f);

    // 4. Indicator pointer needle
    auto pointerLength = knobRadius - 1.0f;
    auto pointerThickness = 2.0f;
    float pointerStart = knobRadius * 0.20f;

    auto startX = centreX + std::sin (currentAngle) * pointerStart;
    auto startY = centreY - std::cos (currentAngle) * pointerStart;
    auto endX = centreX + std::sin (currentAngle) * pointerLength;
    auto endY = centreY - std::cos (currentAngle) * pointerLength;

    // Needle shadow
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawLine (startX + 0.5f, startY + 0.75f, endX + 0.5f, endY + 0.75f, pointerThickness);

    // Needle accent line
    g.setColour (accentColour);
    g.drawLine (startX, startY, endX, endY, pointerThickness);
}

void CustomLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                          const juce::String& text, const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon, const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        auto r = area.reduced (5, 0);
        r.removeFromTop (juce::roundToInt (((float) r.getHeight() * 0.5f) - 0.5f));

        g.setColour (shredOutline);
        g.fillRect (r.removeFromTop (1));
    }
    else
    {
        auto textColour = (textColourToUse == nullptr ? findColour (juce::PopupMenu::textColourId)
                                                      : *textColourToUse);

        auto r = area.reduced (1);

        if (isHighlighted && isActive)
        {
            g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRect (r);

            // Left highlighted indicator line
            g.setColour (accentCyan);
            g.fillRect (r.getX(), r.getY(), 3, r.getHeight());

            g.setColour (findColour (juce::PopupMenu::highlightedTextColourId));
        }
        else
        {
            g.setColour (textColour.withMultipliedAlpha (isActive ? 1.0f : 0.5f));
        }

        r.reduce (juce::jmin (5, area.getWidth() / 20), 0);

        auto font = getPopupMenuFont();
        auto maxFontHeight = (float) r.getHeight() / 1.3f;

        if (font.getHeight() > maxFontHeight)
            font = font.withHeight (maxFontHeight);

        g.setFont (font);

        auto iconArea = r.removeFromLeft (juce::roundToInt (maxFontHeight)).toFloat();

        if (icon != nullptr)
        {
            icon->drawWithin (g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
            r.removeFromLeft (juce::roundToInt (maxFontHeight * 0.5f));
        }
        else if (isTicked)
        {
            auto tick = getTickShape (1.0f);
            g.fillPath (tick, tick.getTransformToScaleToFit (iconArea.reduced (iconArea.getWidth() / 5.0f, 0.0f), true));
        }

        if (hasSubMenu)
        {
            auto arrowH = 0.6f * getPopupMenuFont().getAscent();

            auto x = static_cast<float> (r.removeFromRight ((int) arrowH).getX());
            auto halfH = static_cast<float> (r.getCentreY());

            juce::Path path;
            path.startNewSubPath (x, halfH - arrowH * 0.5f);
            path.lineTo (x + arrowH * 0.6f, halfH);
            path.lineTo (x, halfH + arrowH * 0.5f);

            g.strokePath (path, juce::PathStrokeType (1.5f));
        }

        r.removeFromRight (10);
        g.drawFittedText (text, r, juce::Justification::centredLeft, 1);
        
        if (shortcutKeyText.isNotEmpty())
        {
            g.drawFittedText (shortcutKeyText, r, juce::Justification::centredRight, 1);
        }
    }
}

juce::Font CustomLookAndFeel::getLabelFont (juce::Label& label)
{
    return juce::Font (juce::FontOptions{}.withHeight (juce::jmin (15.0f, (float) label.getHeight() * 0.85f)));
}

juce::Font CustomLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions{}.withHeight (14.0f));
}

void CustomLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                     int buttonX, int buttonY, int buttonW, int buttonH,
                                     juce::ComboBox& box)
{
    auto cornerSize = 4.0f;
    auto boxBounds = juce::Rectangle<int> (0, 0, width, height);

    g.setColour (shredPanelBg);
    g.fillRoundedRectangle (boxBounds.toFloat(), cornerSize);

    g.setColour (shredOutline);
    g.drawRoundedRectangle (boxBounds.toFloat().reduced (0.5f, 0.5f), cornerSize, 1.0f);

    juce::Rectangle<int> arrowZone (width - 25, 0, 20, height);
    juce::Path path;
    path.startNewSubPath ((float) arrowZone.getX() + 3.0f, (float) arrowZone.getCentreY() - 2.0f);
    path.lineTo ((float) arrowZone.getCentreX(), (float) arrowZone.getCentreY() + 3.0f);
    path.lineTo ((float) arrowZone.getRight() - 3.0f, (float) arrowZone.getCentreY() - 2.0f);

    g.setColour (accentCyan.withAlpha ((box.isEnabled() ? 0.9f : 0.2f)));
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

void CustomLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getLabelFont (label));
    label.setColour (juce::Label::textColourId, shredText);
}
