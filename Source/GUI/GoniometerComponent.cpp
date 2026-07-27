#include "GoniometerComponent.h"
#include "CustomLookAndFeel.h"
#include <cmath>

GoniometerComponent::GoniometerComponent (MeterTap* tap)
    : meterTap (tap)
{
    // Pre-allocate buffer storage to avoid steady-path heap allocations.
    // MeterTap's internal AbstractFifo is capped at 4096 pairs, so 4096 is our safe upper limit.
    leftBuffer.resize (4096, 0.0f);
    rightBuffer.resize (4096, 0.0f);
    plotPoints.resize (maxPoints, juce::Point<float> (0.0f, 0.0f));
}

GoniometerComponent::~GoniometerComponent() = default;

void GoniometerComponent::setMeterTap (MeterTap* tap) noexcept
{
    meterTap = tap;
}

void GoniometerComponent::update()
{
    if (meterTap == nullptr)
        return;

    const int numReady = meterTap->getNumPairsReady();
    if (numReady <= 0)
    {
        // Underflow: hold last frame on underflow (paused audio/stopped transport)
        // do not clear the plot or modify the active points.
        return;
    }

    // Limit pop to the preallocated buffers limit
    const int numToRead = juce::jmin (numReady, 4096);
    const int numRead = meterTap->popPairs (leftBuffer.data(), rightBuffer.data(), numToRead);

    if (numRead > 0)
    {
        const size_t numReadSize = static_cast<size_t> (numRead);

        if (numReadSize >= maxPoints)
        {
            // The newly read block fills or exceeds our history capacity.
            // Just populate the history with the newest maxPoints from this block.
            const size_t offset = numReadSize - maxPoints;
            for (size_t i = 0; i < maxPoints; ++i)
            {
                const float l = leftBuffer[offset + i];
                const float r = rightBuffer[offset + i];
                
                // Rotated 45 degrees:
                // x_plot = (R - L) / sqrt(2)
                // y_plot = (L + R) / sqrt(2)
                plotPoints[i] = juce::Point<float> ((r - l) * invSqrt2, (l + r) * invSqrt2);
            }
            numActivePoints = maxPoints;
        }
        else
        {
            // The newly read block is smaller than our history capacity.
            // Slide existing points to make room, keeping chronological order.
            const size_t maxOldPoints = maxPoints - numReadSize;
            const size_t oldPointsToKeep = juce::jmin (numActivePoints, maxOldPoints);

            if (oldPointsToKeep > 0 && numActivePoints > oldPointsToKeep)
            {
                const size_t srcStart = numActivePoints - oldPointsToKeep;
                std::copy (plotPoints.begin() + static_cast<std::ptrdiff_t> (srcStart),
                           plotPoints.begin() + static_cast<std::ptrdiff_t> (numActivePoints),
                           plotPoints.begin());
            }

            // Append the new points at the end of the history
            for (size_t i = 0; i < numReadSize; ++i)
            {
                const float l = leftBuffer[i];
                const float r = rightBuffer[i];
                plotPoints[oldPointsToKeep + i] = juce::Point<float> ((r - l) * invSqrt2, (l + r) * invSqrt2);
            }

            numActivePoints = oldPointsToKeep + numReadSize;
        }

        repaint();
    }
}

void GoniometerComponent::resized()
{
    auto bounds = getLocalBounds().toFloat();
    auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    
    // Center a square plotting area within the component
    plotBounds = bounds.withSizeKeepingCentre (size, size);
    
    centerX = plotBounds.getCentreX();
    centerY = plotBounds.getCentreY();
    
    // Allocate margin for outer labels
    const float margin = size * 0.08f + 12.0f;
    plotRadius = juce::jmax (10.0f, (size - margin) * 0.5f);
    
    // Standard audio signals peak around +/- 1.0 (rotates to +/- sqrt(2)).
    // Set scale so that max value of sqrt(2) maps exactly to plotRadius.
    plotScale = plotRadius / std::sqrt (2.0f);
}

void GoniometerComponent::paint (juce::Graphics& g)
{
    // Draw background panel (clear bounds)
    g.setColour (CustomLookAndFeel::shredDarkBg);
    g.fillEllipse (centerX - plotRadius, centerY - plotRadius, plotRadius * 2.0f, plotRadius * 2.0f);

    // Draw background grid lines (subtle outline color)
    g.setColour (CustomLookAndFeel::shredOutline.withAlpha (0.4f));
    
    // Outer border
    g.drawEllipse (centerX - plotRadius, centerY - plotRadius, plotRadius * 2.0f, plotRadius * 2.0f, 1.5f);
    
    // Concentric grid circles (50% and 75% radius)
    g.drawEllipse (centerX - plotRadius * 0.5f, centerY - plotRadius * 0.5f, plotRadius, plotRadius, 1.0f);
    g.drawEllipse (centerX - plotRadius * 0.75f, centerY - plotRadius * 0.75f, plotRadius * 1.5f, plotRadius * 1.5f, 1.0f);

    // Axis lines (subtle grid color)
    g.setColour (CustomLookAndFeel::shredOutline.withAlpha (0.3f));
    
    // Vertical line (Mid / Mono)
    g.drawLine (centerX, centerY - plotRadius, centerX, centerY + plotRadius, 1.0f);
    
    // Horizontal line (Side / Anti-correlation)
    g.drawLine (centerX - plotRadius, centerY, centerX + plotRadius, centerY, 1.0f);
    
    // 45-degree diagonal lines (Left-only & Right-only axes)
    const float cos45 = 0.7071f;
    const float dOffset = plotRadius * cos45;
    g.drawLine (centerX - dOffset, centerY - dOffset, centerX + dOffset, centerY + dOffset, 1.0f);
    g.drawLine (centerX + dOffset, centerY - dOffset, centerX - dOffset, centerY + dOffset, 1.0f);

    // Draw grid labels
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f)));
    g.setColour (CustomLookAndFeel::shredMutedText);

    // M label at top
    g.drawText ("M", juce::Rectangle<int> (static_cast<int> (centerX - 10.0f), static_cast<int> (centerY - plotRadius - 18.0f), 20, 15),
                juce::Justification::centred, false);

    // S labels at sides
    g.drawText ("S", juce::Rectangle<int> (static_cast<int> (centerX - plotRadius - 18.0f), static_cast<int> (centerY - 7.0f), 15, 15),
                juce::Justification::centred, false);
    g.drawText ("S", juce::Rectangle<int> (static_cast<int> (centerX + plotRadius + 3.0f), static_cast<int> (centerY - 7.0f), 15, 15),
                juce::Justification::centred, false);

    // L / R labels at 45 degrees
    const float labelDist = plotRadius + 12.0f;
    
    // Top-left for L
    const float lx = centerX - labelDist * cos45;
    const float ly = centerY - labelDist * cos45;
    g.drawText ("L", juce::Rectangle<int> (static_cast<int> (lx - 10.0f), static_cast<int> (ly - 7.0f), 20, 15),
                juce::Justification::centred, false);

    // Top-right for R
    const float rx = centerX + labelDist * cos45;
    const float ry = centerY - labelDist * cos45;
    g.drawText ("R", juce::Rectangle<int> (static_cast<int> (rx - 10.0f), static_cast<int> (ry - 7.0f), 20, 15),
                juce::Justification::centred, false);

    // Plot the trace
    if (numActivePoints > 1)
    {
        const juce::Colour traceColor = CustomLookAndFeel::accentCyan;
        
        juce::Point<float> prevPoint;
        bool isFirst = true;

        // Draw glow layer (thicker, lower alpha)
        for (size_t i = 0; i < numActivePoints; ++i)
        {
            const float x_plot = plotPoints[i].getX();
            const float y_plot = plotPoints[i].getY();
            
            const float x_screen = centerX + x_plot * plotScale;
            const float y_screen = centerY - y_plot * plotScale;
            const juce::Point<float> currentPoint (x_screen, y_screen);
            
            if (isFirst)
            {
                prevPoint = currentPoint;
                isFirst = false;
                continue;
            }
            
            const float progress = static_cast<float> (i) / static_cast<float> (numActivePoints - 1);
            const float alpha = 0.05f + 0.95f * progress;
            
            g.setColour (traceColor.withAlpha (alpha * 0.18f));
            g.drawLine (prevPoint.getX(), prevPoint.getY(), currentPoint.getX(), currentPoint.getY(), 3.5f);
            
            prevPoint = currentPoint;
        }

        // Draw core layer (thinner, higher alpha)
        isFirst = true;
        for (size_t i = 0; i < numActivePoints; ++i)
        {
            const float x_plot = plotPoints[i].getX();
            const float y_plot = plotPoints[i].getY();
            
            const float x_screen = centerX + x_plot * plotScale;
            const float y_screen = centerY - y_plot * plotScale;
            const juce::Point<float> currentPoint (x_screen, y_screen);
            
            if (isFirst)
            {
                prevPoint = currentPoint;
                isFirst = false;
                continue;
            }
            
            const float progress = static_cast<float> (i) / static_cast<float> (numActivePoints - 1);
            const float alpha = 0.05f + 0.95f * progress;
            
            g.setColour (traceColor.withAlpha (alpha * 0.85f));
            g.drawLine (prevPoint.getX(), prevPoint.getY(), currentPoint.getX(), currentPoint.getY(), 1.2f);
            
            prevPoint = currentPoint;
        }
    }
    else if (numActivePoints == 1)
    {
        // Draw a single dot at center if there is only 1 point (paused/silence)
        const float x_plot = plotPoints[0].getX();
        const float y_plot = plotPoints[0].getY();
        const float x_screen = centerX + x_plot * plotScale;
        const float y_screen = centerY - y_plot * plotScale;
        
        g.setColour (CustomLookAndFeel::accentCyan);
        g.fillEllipse (x_screen - 1.5f, y_screen - 1.5f, 3.0f, 3.0f);
    }
}
