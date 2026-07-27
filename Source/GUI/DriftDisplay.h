#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/MeterTap.h"
#include <array>

/**
    A real-time-safe visualizer that displays a 2D per-voice scatter plot (timing offset vs pitch offset).
    
    Each active voice is represented by a color-coded marker with a fading trail history.
    In 2-voice mode, the markers and trails for voices 3 and 4 are hidden and do not advance.
*/
class DriftDisplay : public juce::Component
{
public:
    explicit DriftDisplay (MeterTap* tap = nullptr);
    ~DriftDisplay() override;

    /** Sets the MeterTap to poll data from. */
    void setMeterTap (MeterTap* tap) noexcept;

    /** Sets the active voice count (either 2 or 4). */
    void setVoiceCount (int count);

    /** Alias for setVoiceCount to ensure compatibility. */
    void setActiveVoices (int count);

    /** Polls drift values from the MeterTap, updates the trail buffers, and triggers repaint. */
    void update();

    /** Manually pushes drift values (useful if the editor pulls and pushes instead of DriftDisplay polling).
        Expected layout: [v0_delay, v0_detune, v1_delay, v1_detune, ...] up to 8 values.
    */
    void setMeterValues (const float* values, int count);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct TrailPoint
    {
        float x = 0.0f; // timing delay in ms
        float y = 0.0f; // detune in cents
    };

    struct VoiceTrail
    {
        static constexpr int capacity = 32;
        std::array<TrailPoint, capacity> points{};
        int writeIndex = 0;
        int size = 0;

        void push (float x, float y) noexcept
        {
            points[static_cast<size_t> (writeIndex)] = { x, y };
            writeIndex = (writeIndex + 1) % capacity;
            if (size < capacity)
                size++;
        }

        TrailPoint get (int index) const noexcept
        {
            if (size < capacity)
                return points[static_cast<size_t> (index)];
            return points[static_cast<size_t> ((writeIndex + index) % capacity)];
        }
    };

    MeterTap* meterTap = nullptr;
    int activeVoiceCount = 2; // Default assume 2 until told otherwise

    std::array<VoiceTrail, MeterTap::maximumVoices> voiceTrails{};

    // Geometry layout computed during resized()
    juce::Rectangle<float> plotBounds;

    // Stable default ranges with dynamic auto-scaling hysteresis
    float currentXRange = 20.0f; // timing range boundary (+/- 20 ms)
    float currentYRange = 12.0f; // pitch range boundary (+/- 12 cents)

    juce::Point<float> valueToScreen (float x, float y) const noexcept;
    juce::Point<float> getScreenPoint (float x, float y) const noexcept;
    juce::Colour getVoiceColor (int voiceIndex) const noexcept;

    void updateRanges() noexcept;
    void drawXLabel (juce::Graphics& g, const juce::String& text, float val);
    void drawYLabel (juce::Graphics& g, const juce::String& text, float val);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriftDisplay)
};
