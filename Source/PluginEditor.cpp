#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParameterIDs.h"

ShredDoubleAudioProcessorEditor::ShredDoubleAudioProcessorEditor (ShredDoubleAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      goniometer (&p.getEngine().getMeterTap()),
      correlationMeter (&p.getEngine().getMeterTap()),
      driftDisplay (&p.getEngine().getMeterTap())
{
    // Apply custom Look and Feel
    setLookAndFeel (&lookAndFeel);

    // 1. Initial slot setup/sync from processor state
    stateManager.captureSlot (StateManager::Slot::A, audioProcessor.getAPVTS());
    stateManager.switchActiveSlot (StateManager::Slot::A);

    // 2. Setup Preset ComboBox
    addAndMakeVisible (presetComboBox);
    presetComboBox.addItem ("Presets", 1);
    presetComboBox.addItem ("Tight Metal Quad", 2);
    presetComboBox.addItem ("Wide Double", 3);
    presetComboBox.addItem ("Loose Garage Double", 4);
    presetComboBox.addItem ("Subtle Thickener", 5);
    presetComboBox.addItem ("Live Quad Tracking", 6);
    presetComboBox.setSelectedId (1, juce::dontSendNotification);
    presetComboBox.onChange = [this] {
        auto id = presetComboBox.getSelectedId();
        if (id > 1)
            applyPreset (id);
    };

    // 3. Setup A/B Buttons
    addAndMakeVisible (aButton);
    addAndMakeVisible (bButton);
    addAndMakeVisible (copyButton);
    
    aButton.onClick = [this] {
        stateManager.switchActiveSlot (StateManager::Slot::A, audioProcessor.getAPVTS());
        updateABButtonStates();
        updateSeedDisplay();
    };
    bButton.onClick = [this] {
        stateManager.switchActiveSlot (StateManager::Slot::B, audioProcessor.getAPVTS());
        updateABButtonStates();
        updateSeedDisplay();
    };
    copyButton.onClick = [this] {
        if (stateManager.getActiveSlot() == StateManager::Slot::A)
            stateManager.copyAtoB();
        else
            stateManager.copyBtoA();
        updateABButtonStates();
    };

    // 4. Setup Bypass Button
    addAndMakeVisible (bypassButton);

    // 5. Setup Re-roll Button and labels
    addAndMakeVisible (reRollButton);
    reRollButton.onClick = [this] {
        stateManager.reRollMasterSeed (audioProcessor.getAPVTS());
        updateSeedDisplay();
    };

    addAndMakeVisible (seedTitleLabel);
    seedTitleLabel.setJustificationType (juce::Justification::centred);
    seedTitleLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::shredMutedText);
    seedTitleLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (10.0f).withStyle ("Bold")));

    addAndMakeVisible (seedValLabel);
    seedValLabel.setJustificationType (juce::Justification::centred);
    seedValLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::accentCyan);
    seedValLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (14.0f).withStyle ("Bold")));

    updateSeedDisplay();

    // 6. Setup Voice Section segmented controls
    addAndMakeVisible (voiceCountLabel);
    voiceCountLabel.setJustificationType (juce::Justification::centred);
    voiceCountLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::shredMutedText);
    voiceCountLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f).withStyle ("Bold")));

    addAndMakeVisible (voice2Button);
    addAndMakeVisible (voice4Button);
    
    voice2Button.onClick = [this] {
        if (auto* param = audioProcessor.getAPVTS().getParameter (ShredDouble::ParameterIDs::voiceCount))
            param->setValueNotifyingHost (0.0f);
        updateVoiceCountButtonStates();
    };
    voice4Button.onClick = [this] {
        if (auto* param = audioProcessor.getAPVTS().getParameter (ShredDouble::ParameterIDs::voiceCount))
            param->setValueNotifyingHost (1.0f);
        updateVoiceCountButtonStates();
    };

    // 7. Setup Latency Section segmented controls
    addAndMakeVisible (latencyLabel);
    latencyLabel.setJustificationType (juce::Justification::centred);
    latencyLabel.setColour (juce::Label::textColourId, CustomLookAndFeel::shredMutedText);
    latencyLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f).withStyle ("Bold")));

    addAndMakeVisible (studioButton);
    addAndMakeVisible (liveButton);
    
    studioButton.onClick = [this] {
        if (auto* param = audioProcessor.getAPVTS().getParameter (ShredDouble::ParameterIDs::latencyMode))
            param->setValueNotifyingHost (0.0f);
        updateLatencyButtonStates();
    };
    liveButton.onClick = [this] {
        if (auto* param = audioProcessor.getAPVTS().getParameter (ShredDouble::ParameterIDs::latencyMode))
            param->setValueNotifyingHost (1.0f);
        updateLatencyButtonStates();
    };

    // 8. Style buttons to fit theme
    auto setButtonColors = [] (juce::TextButton& btn, juce::Colour accent) {
        btn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff141923));
        btn.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.4f));
        btn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffe2e8f0));
        btn.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    };

    setButtonColors (aButton, CustomLookAndFeel::accentCyan);
    setButtonColors (bButton, CustomLookAndFeel::accentCyan);
    setButtonColors (copyButton, CustomLookAndFeel::accentCyan);
    setButtonColors (reRollButton, CustomLookAndFeel::accentCyan);
    setButtonColors (voice2Button, CustomLookAndFeel::accentCyan);
    setButtonColors (voice4Button, CustomLookAndFeel::accentCyan);
    setButtonColors (studioButton, CustomLookAndFeel::accentOrange);
    setButtonColors (liveButton, CustomLookAndFeel::accentOrange);
    setButtonColors (bypassButton, juce::Colour (0xffff3b30));

    // 9. Setup Sliders
    addAndMakeVisible (panV1Slider);
    addAndMakeVisible (panV2Slider);
    addAndMakeVisible (panV3Slider);
    addAndMakeVisible (panV4Slider);
    
    addAndMakeVisible (timingSpreadSlider);
    addAndMakeVisible (pitchSpreadSlider);
    addAndMakeVisible (humanRateSlider);
    addAndMakeVisible (tightnessSlider);
    
    addAndMakeVisible (monoXoverSlider);
    addAndMakeVisible (widthSlider);
    addAndMakeVisible (mixSlider);
    addAndMakeVisible (outTrimSlider);

    // 10. Setup Metering components
    addAndMakeVisible (goniometer);
    addAndMakeVisible (correlationMeter);
    addAndMakeVisible (driftDisplay);

    // 11. Establish APVTS Attachments
    auto& apvts = audioProcessor.getAPVTS();
    namespace IDs = ShredDouble::ParameterIDs;
    
    bypassAttachment = std::make_unique<ButtonAttachment> (apvts, IDs::bypass, bypassButton);
    panV1Attachment = std::make_unique<SliderAttachment> (apvts, IDs::panV1, panV1Slider);
    panV2Attachment = std::make_unique<SliderAttachment> (apvts, IDs::panV2, panV2Slider);
    panV3Attachment = std::make_unique<SliderAttachment> (apvts, IDs::panV3, panV3Slider);
    panV4Attachment = std::make_unique<SliderAttachment> (apvts, IDs::panV4, panV4Slider);
    
    timingSpreadAttachment = std::make_unique<SliderAttachment> (apvts, IDs::timingSpread, timingSpreadSlider);
    pitchSpreadAttachment = std::make_unique<SliderAttachment> (apvts, IDs::pitchSpread, pitchSpreadSlider);
    humanRateAttachment = std::make_unique<SliderAttachment> (apvts, IDs::humanRate, humanRateSlider);
    tightnessAttachment = std::make_unique<SliderAttachment> (apvts, IDs::tightness, tightnessSlider);
    
    monoXoverAttachment = std::make_unique<SliderAttachment> (apvts, IDs::monoXover, monoXoverSlider);
    widthAttachment = std::make_unique<SliderAttachment> (apvts, IDs::width, widthSlider);
    mixAttachment = std::make_unique<SliderAttachment> (apvts, IDs::mix, mixSlider);
    outTrimAttachment = std::make_unique<SliderAttachment> (apvts, IDs::outTrim, outTrimSlider);

    // 12. Configure Resizing
    setResizable (true, false);
    resizeLimits.setMinimumWidth (750);
    resizeLimits.setMinimumHeight (450);
    resizeLimits.setMaximumWidth (1500);
    resizeLimits.setMaximumHeight (900);
    resizeLimits.setFixedAspectRatio (5.0 / 3.0);
    setConstrainer (&resizeLimits);

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &resizeLimits);
    addAndMakeVisible (resizer.get());

    // 13. Restore Bounds from ValueTree
    auto& state = apvts.state;
    int initialWidth = 900;
    int initialHeight = 540;
    if (state.isValid())
    {
        initialWidth = state.getProperty ("editorWidth", 900);
        initialHeight = state.getProperty ("editorHeight", 540);
    }
    setSize (initialWidth, initialHeight);

    // 14. Initialize States
    updateABButtonStates();
    updateVoiceCountButtonStates();
    updateLatencyButtonStates();
    lastBypassedState = apvts.getRawParameterValue (IDs::bypass)->load() >= 0.5f;

    // Start UI update timer (30-60 Hz)
    startTimerHz (40);
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

//==============================================================================
void ShredDoubleAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark premium background gradient
    juce::ColourGradient grad (
        juce::Colour (0xff0f1219), 0.0f, 0.0f,
        juce::Colour (0xff080a0f), 0.0f, static_cast<float> (getHeight()),
        false
    );
    g.setGradientFill (grad);
    g.fillAll();

    // Draw title and subtitle
    g.setColour (CustomLookAndFeel::accentCyan);
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (20.0f).withStyle ("Bold")));
    g.drawText ("SHREDDOUBLE", 20, 15, 200, 24, juce::Justification::left);

    g.setColour (CustomLookAndFeel::shredMutedText);
    g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.0f).withStyle ("Bold")));
    g.drawText ("DOUBLE-TRACK GENERATOR", 20, 39, 250, 14, juce::Justification::left);

    // Header divider line
    auto headerH = getHeight() * 0.125f;
    g.setColour (CustomLookAndFeel::shredOutline);
    g.drawHorizontalLine (static_cast<int> (headerH), 0.0f, static_cast<float> (getWidth()));

    // Column panels layout
    auto margin = 10;
    auto availableWidth = getWidth() - (2 * margin) - 20;
    auto colW = availableWidth / 3;
    auto startY = static_cast<int> (headerH) + 15;
    auto colH = getHeight() - startY - 15;

    auto drawColumnPanel = [&g, startY, colH] (int x, int width, const juce::String& title, juce::Colour headerColor) {
        // Panel background
        g.setColour (CustomLookAndFeel::shredPanelBg.withAlpha (0.4f));
        g.fillRoundedRectangle (static_cast<float> (x), static_cast<float> (startY), static_cast<float> (width), static_cast<float> (colH), 6.0f);
        
        // Panel outline
        g.setColour (CustomLookAndFeel::shredOutline);
        g.drawRoundedRectangle (static_cast<float> (x), static_cast<float> (startY), static_cast<float> (width), static_cast<float> (colH), 6.0f, 1.5f);

        // Header background band
        g.setColour (CustomLookAndFeel::shredPanelBg);
        g.fillRoundedRectangle (static_cast<float> (x), static_cast<float> (startY), static_cast<float> (width), 25.0f, 6.0f);
        g.fillRect (x, startY + 20, width, 5); // flatten bottom

        // Divider
        g.setColour (CustomLookAndFeel::shredOutline);
        g.drawHorizontalLine (startY + 25, static_cast<float> (x), static_cast<float> (x + width));

        // Column title
        g.setColour (headerColor);
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (11.0f).withStyle ("Bold")));
        g.drawText (title, x, startY, width, 25, juce::Justification::centred);
    };

    int leftX = 10;
    int centerX = leftX + colW + margin;
    int rightX = centerX + colW + margin;

    drawColumnPanel (leftX, colW, "VOICE & HUMANIZATION", CustomLookAndFeel::accentCyan);
    drawColumnPanel (centerX, colW, "GLOBAL CONTROLS", CustomLookAndFeel::accentOrange);
    drawColumnPanel (rightX, colW, "STAGE MONITORING", CustomLookAndFeel::shredText);

    // Sub-divider in Left Column between Voice & Humanization
    g.setColour (CustomLookAndFeel::shredOutline);
    g.drawHorizontalLine (startY + 155, static_cast<float> (leftX), static_cast<float> (leftX + colW));

    // Draw bypass LED
    auto bypassX = getWidth() - 100;
    auto ledRect = juce::Rectangle<float> (static_cast<float> (bypassX - 22), 23.0f, 14.0f, 14.0f);
    
    const bool bypassed = audioProcessor.getAPVTS().getRawParameterValue (ShredDouble::ParameterIDs::bypass)->load() >= 0.5f;
    juce::Colour ledColor = bypassed ? juce::Colour (0xffff3b30) : juce::Colour (0xff34c759);
    
    g.setColour (ledColor.withAlpha (0.25f));
    g.fillEllipse (ledRect.expanded (3.0f));
    
    g.setColour (ledColor);
    g.fillEllipse (ledRect);
    
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillEllipse (ledRect.getX() + 3.0f, ledRect.getY() + 3.0f, 4.0f, 4.0f);
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

    auto headerH = static_cast<int> (getHeight() * 0.125f);
    auto btnY = (headerH - 30) / 2;

    // Header layout
    presetComboBox.setBounds (static_cast<int> (getWidth() * 0.28f), btnY, 140, 30);

    auto abX = static_cast<int> (getWidth() * 0.46f);
    aButton.setBounds (abX, btnY, 30, 30);
    bButton.setBounds (abX + 35, btnY, 30, 30);
    copyButton.setBounds (abX + 70, btnY, 95, 30);

    auto seedX = static_cast<int> (getWidth() * 0.67f);
    seedTitleLabel.setBounds (seedX, btnY - 4, 90, 15);
    seedValLabel.setBounds (seedX, btnY + 11, 90, 20);
    reRollButton.setBounds (seedX + 95, btnY, 100, 30);

    auto bypassX = getWidth() - 100;
    bypassButton.setBounds (bypassX, btnY, 80, 30);

    // Three-column layout below header
    auto margin = 10;
    auto availableWidth = getWidth() - (2 * margin) - 20;
    auto colW = availableWidth / 3;
    auto startY = static_cast<int> (headerH) + 15;
    auto colH = getHeight() - startY - 15;

    int leftX = 10;
    int centerX = leftX + colW + margin;
    int rightX = centerX + colW + margin;

    // --- Left Column: Voice & Humanization ---
    // Voice Section (Top, Y from startY + 30 to startY + 155)
    voiceCountLabel.setBounds (leftX + 10, startY + 32, 60, 20);
    voice2Button.setBounds (leftX + 80, startY + 30, 50, 24);
    voice4Button.setBounds (leftX + 135, startY + 30, 50, 24);

    // Pans row
    auto sliderW = (colW - 20) / 4;
    panV1Slider.setBounds (leftX + 10, startY + 65, sliderW, 80);
    panV2Slider.setBounds (leftX + 10 + sliderW, startY + 65, sliderW, 80);
    panV3Slider.setBounds (leftX + 10 + 2 * sliderW, startY + 65, sliderW, 80);
    panV4Slider.setBounds (leftX + 10 + 3 * sliderW, startY + 65, sliderW, 80);

    // Humanization Section (Bottom, Y from startY + 160 to startY + colH)
    auto gridW = (colW - 20) / 2;
    auto humY1 = startY + 170;
    auto humY2 = startY + 295;
    auto humH = 110;

    timingSpreadSlider.setBounds (leftX + 10, humY1, gridW, humH);
    pitchSpreadSlider.setBounds (leftX + 10 + gridW, humY1, gridW, humH);
    humanRateSlider.setBounds (leftX + 10, humY2, gridW, humH);
    tightnessSlider.setBounds (leftX + 10 + gridW, humY2, gridW, humH);

    // --- Center Column: Global Controls ---
    monoXoverSlider.setBounds (centerX + 10, startY + 45, gridW, humH);
    widthSlider.setBounds (centerX + 10 + gridW, startY + 45, gridW, humH);
    mixSlider.setBounds (centerX + 10, startY + 175, gridW, humH);
    outTrimSlider.setBounds (centerX + 10 + gridW, startY + 175, gridW, humH);

    latencyLabel.setBounds (centerX + 10, startY + 305, colW - 20, 20);
    studioButton.setBounds (centerX + (colW - 190) / 2, startY + 330, 90, 26);
    liveButton.setBounds (centerX + (colW - 190) / 2 + 100, startY + 330, 90, 26);

    // --- Right Column: Stage Monitoring ---
    auto gonioSize = juce::jmin (colW - 40, 140);
    goniometer.setBounds (rightX + (colW - gonioSize) / 2, startY + 35, gonioSize, gonioSize);
    
    correlationMeter.setBounds (rightX + 20, startY + 35 + gonioSize + 15, colW - 40, 28);
    
    auto driftY = startY + 35 + gonioSize + 15 + 28 + 15;
    auto driftH = colH - (35 + gonioSize + 15 + 28 + 15) - 15;
    driftDisplay.setBounds (rightX + 20, driftY, colW - 40, driftH);
}

void ShredDoubleAudioProcessorEditor::timerCallback()
{
    goniometer.update();
    correlationMeter.update();
    driftDisplay.update();

    updateABButtonStates();
    updateVoiceCountButtonStates();
    updateLatencyButtonStates();

    // Check for bypass LED repaint only when changed
    const bool bypassed = audioProcessor.getAPVTS().getRawParameterValue (ShredDouble::ParameterIDs::bypass)->load() >= 0.5f;
    if (bypassed != lastBypassedState)
    {
        lastBypassedState = bypassed;
        repaint();
    }
}

void ShredDoubleAudioProcessorEditor::updateSeedDisplay()
{
    uint64_t seed = stateManager.getMasterSeed (audioProcessor.getAPVTS());
    seedValLabel.setText (juce::String (std::to_string (seed)), juce::dontSendNotification);
}

void ShredDoubleAudioProcessorEditor::updateABButtonStates()
{
    const auto activeSlot = stateManager.getActiveSlot();
    aButton.setToggleState (activeSlot == StateManager::Slot::A, juce::dontSendNotification);
    bButton.setToggleState (activeSlot == StateManager::Slot::B, juce::dontSendNotification);

    if (activeSlot == StateManager::Slot::A)
        copyButton.setButtonText ("Copy A \u27a1 B");
    else
        copyButton.setButtonText ("Copy B \u27a1 A");
}

void ShredDoubleAudioProcessorEditor::updateVoiceCountButtonStates()
{
    if (auto* param = audioProcessor.getAPVTS().getParameter (ShredDouble::ParameterIDs::voiceCount))
    {
        const bool isV4 = param->getValue() >= 0.5f;
        voice2Button.setToggleState (! isV4, juce::dontSendNotification);
        voice4Button.setToggleState (isV4, juce::dontSendNotification);
        
        panV3Slider.setVisible (isV4);
        panV4Slider.setVisible (isV4);
        
        driftDisplay.setVoiceCount (isV4 ? 4 : 2);
    }
}

void ShredDoubleAudioProcessorEditor::updateLatencyButtonStates()
{
    if (auto* param = audioProcessor.getAPVTS().getParameter (ShredDouble::ParameterIDs::latencyMode))
    {
        const bool isLive = param->getValue() >= 0.5f;
        studioButton.setToggleState (! isLive, juce::dontSendNotification);
        liveButton.setToggleState (isLive, juce::dontSendNotification);
    }
}

void ShredDoubleAudioProcessorEditor::applyPreset (int id)
{
    auto& apvts = audioProcessor.getAPVTS();
    auto setParam = [&apvts] (const juce::String& paramId, float value) {
        if (auto* param = apvts.getParameter (paramId))
        {
            auto range = apvts.getParameterRange (paramId);
            param->setValueNotifyingHost (range.convertTo0to1 (value));
        }
    };
    auto setChoice = [&apvts] (const juce::String& paramId, int index) {
        if (auto* param = apvts.getParameter (paramId))
        {
            auto range = apvts.getParameterRange (paramId);
            param->setValueNotifyingHost (range.convertTo0to1 (static_cast<float> (index)));
        }
    };

    namespace IDs = ShredDouble::ParameterIDs;

    switch (id)
    {
        case 2: // Tight Metal Quad
            setChoice (IDs::voiceCount, 1); // 4 voices (index 1)
            setParam (IDs::timingSpread, 4.0f);
            setParam (IDs::pitchSpread, 4.0f);
            setParam (IDs::humanRate, 40.0f);
            setParam (IDs::tightness, 75.0f);
            setParam (IDs::monoXover, 200.0f);
            setParam (IDs::panV1, -100.0f); // Pan 100/100/60/60
            setParam (IDs::panV2, 100.0f);
            setParam (IDs::panV3, -60.0f);
            setParam (IDs::panV4, 60.0f);
            setParam (IDs::mix, 60.0f);
            break;
        case 3: // Wide Double
            setChoice (IDs::voiceCount, 0); // 2 voices (index 0)
            setParam (IDs::timingSpread, 6.0f);
            setParam (IDs::pitchSpread, 5.0f);
            setParam (IDs::humanRate, 50.0f);
            setParam (IDs::tightness, 50.0f);
            setParam (IDs::monoXover, 180.0f);
            setParam (IDs::panV1, -100.0f); // Pan 100/100
            setParam (IDs::panV2, 100.0f);
            setParam (IDs::mix, 50.0f);
            break;
        case 4: // Loose Garage Double
            setChoice (IDs::voiceCount, 0); // 2 voices (index 0)
            setParam (IDs::timingSpread, 12.0f);
            setParam (IDs::pitchSpread, 8.0f);
            setParam (IDs::humanRate, 70.0f);
            setParam (IDs::tightness, 20.0f);
            setParam (IDs::monoXover, 150.0f);
            setParam (IDs::mix, 55.0f);
            break;
        case 5: // Subtle Thickener
            setChoice (IDs::voiceCount, 0); // 2 voices (index 0)
            setParam (IDs::timingSpread, 3.0f);
            setParam (IDs::pitchSpread, 3.0f);
            setParam (IDs::humanRate, 30.0f);
            setParam (IDs::tightness, 80.0f);
            setParam (IDs::monoXover, 250.0f);
            setParam (IDs::mix, 35.0f);
            break;
        case 6: // Live Quad Tracking
            setChoice (IDs::voiceCount, 1); // 4 voices (index 1)
            setParam (IDs::timingSpread, 5.0f);
            setParam (IDs::pitchSpread, 4.0f);
            setParam (IDs::humanRate, 45.0f);
            setParam (IDs::tightness, 65.0f);
            setParam (IDs::mix, 50.0f);
            setChoice (IDs::latencyMode, 1); // Live (index 1)
            break;
        default:
            break;
    }

    // Reset selection to default indicator
    presetComboBox.setSelectedId (1, juce::dontSendNotification);
}
