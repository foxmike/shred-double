#include "DriftDisplay.h"
#include "CustomLookAndFeel.h"
#include <cmath>

DriftDisplay::DriftDisplay (MeterTap* tap)
    : meterTap (tap)
{
}

DriftDisplay::~DriftDisplay() = default;

void DriftDisplay::setMeterTap (MeterTap* tap) noexcept
{
    meterTap = tap;
}

void DriftDisplay::setVoiceCount (int count)
{
    activeVoiceCount = juce::jlimit (1, 4, count);
}

void DriftDisplay::setActiveVoices (int count)
{
    setVoiceCount (count);
}

void DriftDisplay::update()
{
    if (meterTap != nullptr)
    {
        for (int v = 0; v < activeVoiceCount; ++v)
        {
            float delay = meterTap->getVoiceDelayMs (v);
            float detune = meterTap->getVoiceDetuneCents (v);
            voiceTrails[static_cast<size_t> (v)].push (delay, detune);
        }
        updateRanges();
        repaint();
    }
}

void DriftDisplay::setMeterValues (const float* values, int count)
{
    if (values == nullptr || count <= 0)
        return;

    int voicesToUpdate = juce::jmin (activeVoiceCount, count / 2);
    for (int v = 0; v < voicesToUpdate; ++v)
    {
        float delay = values[2 * v];
        float detune = values[2 * v + 1];
        voiceTrails[static_cast<size_t> (v)].push (delay, detune);
    }
    updateRanges();
    repaint();
}

void DriftDisplay::updateRanges() noexcept
{
    float maxDelay = 0.0f;
    float maxDetune = 0.0f;
    for (int v = 0; v < activeVoiceCount; ++v)
    {
        const auto& trail = voiceTrails[static_cast<size_t> (v)];
        if (trail.size > 0)
        {
            auto pt = trail.get (trail.size - 1);
            maxDelay = juce::jmax (maxDelay, std::abs (pt.x));
            maxDetune = juce::jmax (maxDetune, std::abs (pt.y));
        }
    }

    // Adapt X range to fit maximum delay offset, with 1.2x safety margin and a minimum floor of 20 ms.
    float targetXRange = juce::jmax (20.0f, maxDelay * 1.2f);
    if (targetXRange > currentXRange)
    {
        currentXRange = targetXRange; // Instant expansion
    }
    else
    {
        currentXRange += (targetXRange - currentXRange) * 0.02f; // Slow contraction/hysteresis
    }

    // Adapt Y range to fit maximum detune offset, with 1.2x safety margin and a minimum floor of 12 cents.
    float targetYRange = juce::jmax (12.0f, maxDetune * 1.2f);
    if (targetYRange > currentYRange)
    {
        currentYRange = targetYRange; // Instant expansion
    }
    else
    {
        currentYRange += (targetYRange - currentYRange) * 0.02f; // Slow contraction/hysteresis
    }
}

juce::Point<float> DriftDisplay::valueToScreen (float x, float y) const noexcept
{
    float normX = (x + currentXRange) / (2.0f * currentXRange);
    float normY = (y + currentYRange) / (2.0f * currentYRange);
    
    float screenX = plotBounds.getX() + plotBounds.getWidth() * normX;
    float screenY = plotBounds.getBottom() - plotBounds.getHeight() * normY;
    return { screenX, screenY };
}

juce::Point<float> DriftDisplay::getScreenPoint (float x, float y) const noexcept
{
    float clampedX = juce::jlimit (-currentXRange, currentXRange, x);
    float clampedY = juce::jlimit (-currentYRange, currentYRange, y);
    return valueToScreen (clampedX, clampedY);
}

juce::Colour DriftDisplay::getVoiceColor (int voiceIndex) const noexcept
{
    switch (voiceIndex)
    {
        case 0: return CustomLookAndFeel::accentCyan;
        case 1: return CustomLookAndFeel::accentOrange;
        case 2: return CustomLookAndFeel::accentGreen;
        case 3: return juce::Colour (0xffe040fb); // Beautiful Neon Purple/Pink
        default: return juce::Colours::white;
    }
}

void DriftDisplay::resized()
{
    auto bounds = getLocalBounds().toFloat();
    auto area = bounds.reduced (10.0f);
    
    // Allocate space for labels around the plot
    float labelLeftWidth = 45.0f;
    float labelBottomHeight = 35.0f;
    float labelTopHeight = 15.0f;
    float labelRightWidth = 10.0f;
    
    plotBounds = area;
    plotBounds.removeFromLeft (labelLeftWidth);
    plotBounds.removeFromBottom (labelBottomHeight);
    plotBounds.removeFromTop (labelTopHeight);
    plotBounds.removeFromRight (labelRightWidth);
}

void DriftDisplay::paint (juce::Graphics& g)
{
    // Draw outer panel background
    g.fillAll (CustomLookAndFeel::shredPanelBg);

    // Draw inner plot background
    g.setColour (CustomLookAndFeel::shredDarkBg);
    g.fillRect (plotBounds);

    // Draw plot outline
    g.setColour (CustomLookAndFeel::shredOutline.withAlpha (0.5f));
    g.drawRect (plotBounds, 1.5f);

    // Draw center crosshairs (0,0)
    auto centerPoint = valueToScreen (0.0f, 0.0f);
    
    g.setColour (CustomLookAndFeel::shredOutline.withAlpha (0.6f));
    g.drawVerticalLine (static_cast<int> (centerPoint.getX()), plotBounds.getY(), plotBounds.getBottom());
    g.drawHorizontalLine (static_cast<int> (centerPoint.getY()), plotBounds.getX(), plotBounds.getRight());

    // Draw minor gridlines at half-ranges
    g.setColour (CustomLookAndFeel::shredOutline.withAlpha (0.25f));
    
    float xHalfMinus = valueToScreen (-currentXRange * 0.5f, 0.0f).getX();
    float xHalfPlus = valueToScreen (currentXRange * 0.5f, 0.0f).getX();
    g.drawVerticalLine (static_cast<int> (xHalfMinus), plotBounds.getY(), plotBounds.getBottom());
    g.drawVerticalLine (static_cast<int> (xHalfPlus), plotBounds.getY(), plotBounds.getBottom());

    float yHalfMinus = valueToScreen (0.0f, -currentYRange * 0.5f).getY();
    float yHalfPlus = valueToScreen (0.0f, currentYRange * 0.5f).getY();
    g.drawHorizontalLine (static_cast<int> (yHalfMinus), plotBounds.getX(), plotBounds.getRight());
    g.drawHorizontalLine (static_cast<int> (yHalfPlus), plotBounds.getX(), plotBounds.getRight());

    // Setup text labels
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.0f)));
    g.setColour (CustomLookAndFeel::shredMutedText);

    // Draw X-axis label values
    auto formatMs = [] (float ms) {
        if (std::abs (ms) < 0.05f) return juce::String ("0");
        return juce::String (ms, 1) + " ms";
    };
    
    drawXLabel (g, formatMs (-currentXRange), -currentXRange);
    drawXLabel (g, formatMs (-currentXRange * 0.5f), -currentXRange * 0.5f);
    drawXLabel (g, "0", 0.0f);
    drawXLabel (g, formatMs (currentXRange * 0.5f), currentXRange * 0.5f);
    drawXLabel (g, formatMs (currentXRange), currentXRange);

    // Draw Y-axis label values
    auto formatCents = [] (float cents) {
        if (std::abs (cents) < 0.05f) return juce::String ("0");
        return juce::String (cents, 1) + " c";
    };

    drawYLabel (g, formatCents (-currentYRange), -currentYRange);
    drawYLabel (g, formatCents (-currentYRange * 0.5f), -currentYRange * 0.5f);
    drawYLabel (g, "0", 0.0f);
    drawYLabel (g, formatCents (currentYRange * 0.5f), currentYRange * 0.5f);
    drawYLabel (g, formatCents (currentYRange), currentYRange);

    // Axis titles
    g.drawText ("Timing Offset", 
                static_cast<int> (plotBounds.getX()), 
                static_cast<int> (plotBounds.getBottom() + 18.0f), 
                static_cast<int> (plotBounds.getWidth()), 
                15, 
                juce::Justification::centred, 
                false);

    g.drawText ("Pitch (cents)", 
                0, 
                static_cast<int> (plotBounds.getY() - 15.0f), 
                static_cast<int> (plotBounds.getX() - 5.0f), 
                15, 
                juce::Justification::right, 
                false);

    // Draw trails and markers, clipped strictly to the plot area
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plotBounds.toNearestInt());

        for (int v = 0; v < activeVoiceCount; ++v)
        {
            const auto& trail = voiceTrails[static_cast<size_t> (v)];
            if (trail.size == 0)
                continue;

            juce::Colour baseColor = getVoiceColor (v);

            // Draw historical fading trail segments
            if (trail.size > 1)
            {
                for (int i = 0; i < trail.size - 1; ++i)
                {
                    auto pt1 = getScreenPoint (trail.get (i).x, trail.get (i).y);
                    auto pt2 = getScreenPoint (trail.get (i + 1).x, trail.get (i + 1).y);
                    
                    float progress = static_cast<float> (i) / static_cast<float> (trail.size - 1);
                    float alpha = 0.05f + 0.65f * progress;
                    
                    g.setColour (baseColor.withAlpha (alpha));
                    g.drawLine (pt1.getX(), pt1.getY(), pt2.getX(), pt2.getY(), 1.2f + 1.2f * progress);
                }
            }

            // Draw current location marker (newest value)
            auto currentVal = trail.get (trail.size - 1);
            auto markerPos = getScreenPoint (currentVal.x, currentVal.y);

            // Neon glow outer circle
            g.setColour (baseColor.withAlpha (0.25f));
            g.fillEllipse (markerPos.getX() - 6.5f, markerPos.getY() - 6.5f, 13.0f, 13.0f);
            
            // Solid inner circle
            g.setColour (baseColor);
            g.fillEllipse (markerPos.getX() - 3.5f, markerPos.getY() - 3.5f, 7.0f, 7.0f);
            
            // Bright white hotspot center
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (markerPos.getX() - 1.0f, markerPos.getY() - 1.0f, 2.0f, 2.0f);
        }
    }
}

void DriftDisplay::drawXLabel (juce::Graphics& g, const juce::String& text, float val)
{
    float x = valueToScreen (val, 0.0f).getX();
    g.drawText (text, 
                static_cast<int> (x - 30.0f), 
                static_cast<int> (plotBounds.getBottom() + 3.0f), 
                60, 
                12, 
                juce::Justification::centred, 
                false);
}

void DriftDisplay::drawYLabel (juce::Graphics& g, const juce::String& text, float val)
{
    float y = valueToScreen (0.0f, val).getY();
    g.drawText (text, 
                0, 
                static_cast<int> (y - 6.0f), 
                static_cast<int> (plotBounds.getX() - 5.0f), 
                12, 
                juce::Justification::right, 
                false);
}
