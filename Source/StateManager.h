#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <string>

/**
    StateManager manages schema-versioned state serializations and A/B slots.
    
    ValueTree copying, XML conversion, and state restoration are message-thread 
    operations and are not real-time safe.
*/
class StateManager
{
public:
    static constexpr int schemaVersion = 1;

    enum class Slot
    {
        A = 0,
        B = 1
    };

    StateManager() = default;
    ~StateManager() = default;

    //==============================================================================
    // A/B Slot Operations (message thread; RT-unsafe)
    //==============================================================================

    /** Captures a complete APVTS snapshot in the selected slot. */
    void captureSlot (Slot slot, juce::AudioProcessorValueTreeState& apvts);

    /** Captures a complete APVTS snapshot in the currently active slot. */
    void captureSlot (juce::AudioProcessorValueTreeState& apvts);

    /** Applies the selected slot. Returns false when that slot is empty or invalid. */
    bool applySlot (Slot slot, juce::AudioProcessorValueTreeState& apvts);

    /** Applies the currently active slot. */
    bool applySlot (juce::AudioProcessorValueTreeState& apvts);

    /** Marks a slot active. */
    void switchActiveSlot (Slot target) noexcept;

    /**
        Captures the outgoing state, marks target active, and applies its snapshot.
    */
    bool switchActiveSlot (Slot target,
                           juce::AudioProcessorValueTreeState& apvts,
                           bool captureOutgoingState = true);

    Slot getActiveSlot() const noexcept { return activeSlot.load(); }

    void copyAtoB();
    void copyBtoA();

    /** Clears a slot. Applying an empty slot is a safe no-op. */
    void resetSlot (Slot slot);

    /** Replaces a slot with an APVTS snapshot whose parameters are at defaults. */
    void resetSlot (Slot slot, juce::AudioProcessorValueTreeState& apvts);

    //==============================================================================
    // Legacy / Convenience wrappers
    //==============================================================================
    void captureFrom (juce::AudioProcessorValueTreeState& apvts);
    void applyTo (juce::AudioProcessorValueTreeState& apvts);
    void swapAB();

    //==============================================================================
    // APVTS XML Serialization (message thread; RT-unsafe)
    //==============================================================================

    /** Returns an aggregate ValueTree containing both slots, active-slot index, and version. */
    juce::ValueTree createComparisonState (juce::AudioProcessorValueTreeState& apvts);

    /** Restores slots and active index; rejects unsupported future schemas. */
    bool restoreComparisonState (const juce::ValueTree& state,
                                 juce::AudioProcessorValueTreeState& apvts);

    std::unique_ptr<juce::XmlElement> createComparisonXml (juce::AudioProcessorValueTreeState& apvts);
    bool restoreComparisonXml (const juce::XmlElement& xml,
                               juce::AudioProcessorValueTreeState& apvts);

    //==============================================================================
    // Processor Serialization compatibility helpers
    //==============================================================================
    void getState (juce::AudioProcessorValueTreeState& apvts, juce::MemoryBlock& destData);
    bool setState (juce::AudioProcessorValueTreeState& apvts, const void* data, int sizeInBytes);

    //==============================================================================
    // masterSeed and Re-roll (message thread; RT-unsafe)
    //==============================================================================
    uint64_t getMasterSeed (juce::AudioProcessorValueTreeState& apvts) const;
    void setMasterSeed (juce::AudioProcessorValueTreeState& apvts, uint64_t newSeed);
    void reRollMasterSeed (juce::AudioProcessorValueTreeState& apvts);

private:
    static juce::ValueTree makeDefaultState (juce::AudioProcessorValueTreeState& apvts);
    
    const juce::ValueTree& getSlotState (Slot slot) const noexcept;
    juce::ValueTree& getSlotState (Slot slot) noexcept;

    void migrateState (juce::ValueTree& state, int loadedVersion) const;

    juce::ValueTree slotA;
    juce::ValueTree slotB;
    std::atomic<Slot> activeSlot { Slot::A };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateManager)
};
