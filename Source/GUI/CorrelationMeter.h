#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/MeterTap.h"

/**
    Wave-2 correlation meter component.
    Displays a horizontal correlation bar spanning -1 to +1,
    with colored zones (green, yellow, red), a decaying peak-toward--1 marker,
    and a mono-cancellation warning flash.
*/
class CorrelationMeter : public juce::Component
{
public:
    explicit CorrelationMeter (MeterTap* tap = nullptr);
    ~CorrelationMeter() override;

    /** Sets the MeterTap to read correlation from. */
    void setMeterTap (MeterTap* tap) noexcept;

    /** Sets an alternative atomic float source for the correlation value. */
    void setCorrelationSource (const std::atomic<float>* source) noexcept;

    /** Sets an alternative float source pointer for the correlation value. */
    void setCorrelationSource (const float* source) noexcept;

    /**
        Updates the correlation reading, processes the peak hold and decay logic,
        handles the warning flash animation, and triggers a repaint.
        Should be called regularly from the UI timer callback (e.g., at 30-60 Hz).
    */
    void update();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    MeterTap* meterTap = nullptr;
    const std::atomic<float>* atomicSource = nullptr;
    const float* floatSource = nullptr;

    float currentCorrelation = 0.0f;
    float lastCorrelation = 0.0f;
    float peakHoldValue = 1.0f;
    int peakHoldTicks = 0;
    float flashAlpha = 0.0f;

    // Layout geometry
    juce::Rectangle<float> meterBounds;
    float zeroX = 0.0f;
    float halfWidth = 0.0f;

    // Color definitions
    const juce::Colour dangerRed { 0xffff3b30 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CorrelationMeter)
};
