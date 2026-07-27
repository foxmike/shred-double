#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/MeterTap.h"
#include <vector>

/**
    A real-time-safe visualizer for the stereo output stage (Lissajous / vectorscope).

    It decimes/drains sample pairs from MeterTap over a lock-free FIFO on the
    UI thread (via update()), plotting a 45-degree rotated Mid/Side vectorscope
    with a phosphor alpha-decay trail. Underflow holds the last frame.
*/
class GoniometerComponent : public juce::Component
{
public:
    explicit GoniometerComponent (MeterTap* tap = nullptr);
    ~GoniometerComponent() override;

    /** Sets the MeterTap to read audio data from. */
    void setMeterTap (MeterTap* tap) noexcept;

    /** Drains pending sample pairs from the MeterTap FIFO, updates the trace, and triggers repaint. */
    void update();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    MeterTap* meterTap = nullptr;

    // Configuration constants
    static constexpr size_t maxPoints = 1024;
    const float invSqrt2 = static_cast<float> (1.0 / std::sqrt (2.0));

    // Preallocated buffers to eliminate steady-path allocations
    std::vector<float> leftBuffer;
    std::vector<float> rightBuffer;
    std::vector<juce::Point<float>> plotPoints;
    size_t numActivePoints = 0;

    // Layout values computed during resized()
    juce::Rectangle<float> plotBounds;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float plotRadius = 0.0f;
    float plotScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoniometerComponent)
};
