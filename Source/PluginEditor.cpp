#include "PluginEditor.h"

ShredDoubleAudioProcessorEditor::ShredDoubleAudioProcessorEditor (ShredDoubleAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    // Initial slot setup/sync from processor state
    stateManager.captureSlot (StateManager::Slot::A, audioProcessor.getAPVTS());
    stateManager.switchActiveSlot (StateManager::Slot::A);

    // Set up Re-roll button
    addAndMakeVisible (reRollButton);
    reRollButton.onClick = [this] {
        stateManager.reRollMasterSeed (audioProcessor.getAPVTS());
        updateSeedDisplay();
    };

    // Styling the Re-roll button (matching CustomLookAndFeel accentColors)
    reRollButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff141923));
    reRollButton.setColour (juce::TextButton::buttonOnColourId, CustomLookAndFeel::accentCyan.withAlpha (0.4f));
    reRollButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe2e8f0));
    reRollButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    // Set up labels
    addAndMakeVisible (seedTitleLabel);
    seedTitleLabel.setJustificationType (juce::Justification::centred);
    seedTitleLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::shredMutedText);
    seedTitleLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f).withStyle ("Bold")));

    addAndMakeVisible (seedValLabel);
    seedValLabel.setJustificationType (juce::Justification::centred);
    seedValLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::accentCyan);
    seedValLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (14.0f).withStyle ("Bold")));

    updateSeedDisplay();

    // Configure resizing
    setResizable (true, true);
    resizeLimits.setMinimumWidth (400);
    resizeLimits.setMinimumHeight (250);
    resizeLimits.setMaximumWidth (1000);
    resizeLimits.setMaximumHeight (600);
    setConstrainer (&resizeLimits);

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &resizeLimits);
    addAndMakeVisible (resizer.get());

    // Restore size from processor value tree if saved
    auto& state = audioProcessor.getAPVTS().state;
    int initialWidth = 450;
    int initialHeight = 300;
    if (state.isValid())
    {
        initialWidth = state.getProperty ("editorWidth", 450);
        initialHeight = state.getProperty ("editorHeight", 300);
    }
    setSize (initialWidth, initialHeight);

    // Start empty timer callback
    startTimerHz (30);
}

ShredDoubleAudioProcessorEditor::~ShredDoubleAudioProcessorEditor()
{
    // Save window size on shutdown to APVTS ValueTree state
    auto& state = audioProcessor.getAPVTS().state;
    if (state.isValid())
    {
        state.setProperty ("editorWidth", getWidth(), nullptr);
        state.setProperty ("editorHeight", getHeight(), nullptr);
    }

    setLookAndFeel (nullptr);
    stopTimer();
}

void ShredDoubleAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Premium dark gradient background
    juce::ColourGradient grad (
        juce::Colour (0xff0f1219), 0.0f, 0.0f,
        juce::Colour (0xff080a0f), 0.0f, static_cast<float> (getHeight()),
        false
    );
    g.setGradientFill (grad);
    g.fillAll();

    // Title text
    g.setColour (CustomLookAndFeel::accentCyan);
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (20.0f).withStyle ("Bold")));
    g.drawText ("SHREDDOUBLE", 20, 15, 200, 24, juce::Justification::left);

    g.setColour (CustomLookAndFeel::shredMutedText);
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.0f).withStyle ("Bold")));
    g.drawText ("DOUBLE-TRACK GENERATOR", 20, 39, 250, 14, juce::Justification::left);

    // Header divider line
    auto headerH = 65.0f;
    g.setColour (CustomLookAndFeel::shredOutline);
    g.drawHorizontalLine (static_cast<int> (headerH), 0.0f, static_cast<float> (getWidth()));

    // Draw control panel container in the center
    auto containerX = 25;
    auto containerY = static_cast<int> (headerH) + 25;
    auto containerW = getWidth() - 50;
    auto containerH = getHeight() - containerY - 35;

    g.setColour (CustomLookAndFeel::shredPanelBg.withAlpha (0.4f));
    g.fillRoundedRectangle (containerX, containerY, containerW, containerH, 6.0f);

    g.setColour (CustomLookAndFeel::shredOutline);
    g.drawRoundedRectangle (containerX, containerY, containerW, containerH, 6.0f, 1.5f);
}

void ShredDoubleAudioProcessorEditor::resized()
{
    // Save size dynamically when resized
    auto& state = audioProcessor.getAPVTS().state;
    if (state.isValid())
    {
        state.setProperty ("editorWidth", getWidth(), nullptr);
        state.setProperty ("editorHeight", getHeight(), nullptr);
    }

    if (resizer != nullptr)
    {
        resizer->setBounds (getWidth() - 16, getHeight() - 16, 16, 16);
    }

    auto headerH = 65;
    auto containerX = 25;
    auto containerY = headerH + 25;
    auto containerW = getWidth() - 50;
    auto containerH = getHeight() - containerY - 35;

    // Place the Re-roll control and labels inside the container panel
    int controlW = juce::jmin (220, containerW - 40);
    int controlH = 36;
    int centerX = containerX + (containerW - controlW) / 2;
    int startY = containerY + (containerH - 120) / 2;

    seedTitleLabel.setBounds (centerX, startY, controlW, 20);
    seedValLabel.setBounds (centerX, startY + 25, controlW, 25);
    reRollButton.setBounds (centerX, startY + 65, controlW, controlH);
}

void ShredDoubleAudioProcessorEditor::timerCallback()
{
    // Stub empty timer callback for later metering
}

void ShredDoubleAudioProcessorEditor::updateSeedDisplay()
{
    uint64_t seed = stateManager.getMasterSeed (audioProcessor.getAPVTS());
    seedValLabel.setText (juce::String (std::to_string (seed)), juce::dontSendNotification);
}
