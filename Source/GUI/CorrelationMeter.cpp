#include "CorrelationMeter.h"
#include "CustomLookAndFeel.h"
#include <cmath>

CorrelationMeter::CorrelationMeter (MeterTap* tap)
    : meterTap (tap)
{
}

CorrelationMeter::~CorrelationMeter() = default;

void CorrelationMeter::setMeterTap (MeterTap* tap) noexcept
{
    meterTap = tap;
}

void CorrelationMeter::setCorrelationSource (const std::atomic<float>* source) noexcept
{
    atomicSource = source;
}

void CorrelationMeter::setCorrelationSource (const float* source) noexcept
{
    floatSource = source;
}

void CorrelationMeter::update()
{
    float val = 0.0f;
    if (meterTap != nullptr)
    {
        val = meterTap->getCorrelation();
    }
    else if (atomicSource != nullptr)
    {
        val = atomicSource->load();
    }
    else if (floatSource != nullptr)
    {
        val = *floatSource;
    }

    currentCorrelation = juce::jlimit (-1.0f, 1.0f, val);

    // Peak hold-toward--1 logic: track the minimum correlation value (most negative/dangerous)
    if (currentCorrelation < peakHoldValue)
    {
        peakHoldValue = currentCorrelation;
        // Hold peak for approx 0.8 seconds at 30Hz, or 0.4 seconds at 60Hz.
        peakHoldTicks = 25;
    }
    else
    {
        if (peakHoldTicks > 0)
        {
            --peakHoldTicks;
        }
        else
        {
            // Decay slowly back towards +1.0f (least dangerous correlation)
            peakHoldValue += 0.03f * (1.0f - peakHoldValue);
        }
    }

    // Keep peak indicator bounded by current correlation
    if (peakHoldValue > currentCorrelation)
    {
        peakHoldValue = currentCorrelation;
    }

    // Warning flash logic: trigger warning flash when correlation dips deep into the red zone
    if (currentCorrelation < -0.3f)
    {
        if (lastCorrelation >= -0.3f || currentCorrelation < lastCorrelation)
        {
            flashAlpha = 1.0f;
        }
    }

    // Decay warning flash alpha
    if (flashAlpha > 0.0f)
    {
        flashAlpha = juce::jmax (0.0f, flashAlpha - 0.05f);
    }

    lastCorrelation = currentCorrelation;
    repaint();
}

void CorrelationMeter::resized()
{
    auto bounds = getLocalBounds().toFloat();
    
    // Allocate space for tick labels below the bar
    const float labelHeight = 14.0f;
    const float barHeight = 10.0f;
    const float totalNeededHeight = barHeight + labelHeight + 4.0f;
    
    auto area = bounds.withHeight (totalNeededHeight).withCentre (bounds.getCentre());
    
    meterBounds = area.removeFromTop (barHeight);
    
    // Add horizontal padding to prevent tick labels from being clipped at the edges
    const float horizontalPadding = 12.0f;
    meterBounds = meterBounds.reduced (horizontalPadding, 0.0f);
    
    zeroX = meterBounds.getCentreX();
    halfWidth = meterBounds.getWidth() * 0.5f;
}

void CorrelationMeter::paint (juce::Graphics& g)
{
    const float cornerSize = 3.0f;

    // 1. Draw track background
    g.setColour (CustomLookAndFeel::shredPanelBg);
    g.fillRoundedRectangle (meterBounds, cornerSize);

    // 2. Draw background color bands/zones (subtle opacity)
    // Red zone: -1.0 to -0.3
    // Yellow zone: -0.3 to +0.3
    // Green zone: +0.3 to +1.0
    const float redEndX = zeroX - 0.3f * halfWidth;
    const float yellowEndX = zeroX + 0.3f * halfWidth;

    // Red background zone
    juce::Rectangle<float> redZone (meterBounds.getX(), meterBounds.getY(), redEndX - meterBounds.getX(), meterBounds.getHeight());
    g.setColour (dangerRed.withAlpha (0.06f));
    g.fillRect (redZone);

    // Yellow background zone
    juce::Rectangle<float> yellowZone (redEndX, meterBounds.getY(), yellowEndX - redEndX, meterBounds.getHeight());
    g.setColour (CustomLookAndFeel::accentOrange.withAlpha (0.06f));
    g.fillRect (yellowZone);

    // Green background zone
    juce::Rectangle<float> greenZone (yellowEndX, meterBounds.getY(), meterBounds.getRight() - yellowEndX, meterBounds.getHeight());
    g.setColour (CustomLookAndFeel::accentGreen.withAlpha (0.06f));
    g.fillRect (greenZone);

    // 3. Draw tick/grid lines
    g.setColour (CustomLookAndFeel::shredOutline.withAlpha (0.5f));
    
    // Ticks at -1, -0.3, 0.3, 1
    g.drawVerticalLine (static_cast<int> (zeroX - halfWidth), meterBounds.getY(), meterBounds.getBottom());
    g.drawVerticalLine (static_cast<int> (zeroX - 0.3f * halfWidth), meterBounds.getY(), meterBounds.getBottom());
    g.drawVerticalLine (static_cast<int> (zeroX + 0.3f * halfWidth), meterBounds.getY(), meterBounds.getBottom());
    g.drawVerticalLine (static_cast<int> (zeroX + halfWidth), meterBounds.getY(), meterBounds.getBottom());

    // Center line (slightly brighter)
    g.setColour (CustomLookAndFeel::shredOutline.brighter (0.2f));
    g.drawVerticalLine (static_cast<int> (zeroX), meterBounds.getY(), meterBounds.getBottom());

    // 4. Draw the fill bar (from 0 to currentCorrelation)
    if (std::abs (currentCorrelation) > 1.0e-5f)
    {
        const float targetX = zeroX + currentCorrelation * halfWidth;
        
        if (currentCorrelation > 0.0f)
        {
            // Segmented fill going right
            const float fillYellowEnd = juce::jmin (targetX, zeroX + 0.3f * halfWidth);
            if (fillYellowEnd > zeroX)
            {
                g.setColour (CustomLookAndFeel::accentOrange);
                g.fillRect (juce::Rectangle<float> (zeroX, meterBounds.getY() + 1.0f, fillYellowEnd - zeroX, meterBounds.getHeight() - 2.0f));
            }
            
            if (targetX > fillYellowEnd)
            {
                g.setColour (CustomLookAndFeel::accentGreen);
                g.fillRect (juce::Rectangle<float> (fillYellowEnd, meterBounds.getY() + 1.0f, targetX - fillYellowEnd, meterBounds.getHeight() - 2.0f));
            }
        }
        else
        {
            // Segmented fill going left
            const float fillYellowEnd = juce::jmax (targetX, zeroX - 0.3f * halfWidth);
            if (fillYellowEnd < zeroX)
            {
                g.setColour (CustomLookAndFeel::accentOrange);
                g.fillRect (juce::Rectangle<float> (fillYellowEnd, meterBounds.getY() + 1.0f, zeroX - fillYellowEnd, meterBounds.getHeight() - 2.0f));
            }
            
            if (targetX < fillYellowEnd)
            {
                g.setColour (dangerRed);
                g.fillRect (juce::Rectangle<float> (targetX, meterBounds.getY() + 1.0f, fillYellowEnd - targetX, meterBounds.getHeight() - 2.0f));
            }
        }
    }

    // 5. Draw the peak-toward--1 marker
    const float peakX = zeroX + peakHoldValue * halfWidth;
    
    // Choose color for peak indicator
    juce::Colour peakColour = CustomLookAndFeel::shredMutedText;
    if (peakHoldValue < -0.3f)
    {
        peakColour = dangerRed;
    }
    else if (peakHoldValue <= 0.3f)
    {
        peakColour = CustomLookAndFeel::accentOrange;
    }
    else
    {
        peakColour = CustomLookAndFeel::accentGreen;
    }
    
    // When flashing, make the red peak marker extra bright (white/light-red)
    if (peakHoldValue < -0.3f && flashAlpha > 0.0f)
    {
        peakColour = peakColour.interpolatedWith (juce::Colours::white, flashAlpha * 0.7f);
    }
    
    g.setColour (peakColour);
    g.fillRect (juce::Rectangle<float> (peakX - 1.0f, meterBounds.getY() - 1.0f, 2.0f, meterBounds.getHeight() + 2.0f));

    // 6. Draw track outline
    g.setColour (CustomLookAndFeel::shredOutline);
    g.drawRoundedRectangle (meterBounds, cornerSize, 1.0f);

    // 7. Draw the mono-cancellation warning flash (outer glowing outline)
    if (flashAlpha > 0.0f)
    {
        g.setColour (dangerRed.withAlpha (flashAlpha * 0.4f));
        g.drawRoundedRectangle (meterBounds.expanded (1.0f), cornerSize + 1.0f, 2.0f);
    }

    // 8. Draw labels
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.0f)));
    g.setColour (CustomLookAndFeel::shredMutedText);

    const float labelY = meterBounds.getBottom() + 2.0f;
    g.drawText ("-1", juce::Rectangle<float> (zeroX - halfWidth - 15.0f, labelY, 30.0f, 14.0f), juce::Justification::centred, false);
    g.drawText ("0", juce::Rectangle<float> (zeroX - 15.0f, labelY, 30.0f, 14.0f), juce::Justification::centred, false);
    g.drawText ("+1", juce::Rectangle<float> (zeroX + halfWidth - 15.0f, labelY, 30.0f, 14.0f), juce::Justification::centred, false);
}
