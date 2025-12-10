#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ARBManager.h"
#include <cmath>
#include <algorithm>

// Helper function to format frequency with appropriate unit (Hz, kHz, MHz)
static juce::String formatFrequency(double freqHz)
{
    if (freqHz >= 1e6)
        return juce::String(freqHz / 1e6, 3) + " MHz";
    else if (freqHz >= 1e3)
        return juce::String(freqHz / 1e3, 3) + " kHz";
    else
        return juce::String(freqHz, 3) + " Hz";
}

// Helper function to parse frequency string with units (e.g., "15kHz", "10Hz", "20MHz")
static double parseFrequency(const juce::String& text)
{
    juce::String trimmed = text.trim().toUpperCase();
    if (trimmed.isEmpty())
        return 0.0;
    
    // Remove spaces
    trimmed = trimmed.removeCharacters(" ");
    
    double multiplier = 1.0;
    juce::String numStr = trimmed;
    
    // Check for unit suffixes
    if (trimmed.endsWith("MHZ")) { multiplier = 1e6; numStr = trimmed.substring(0, trimmed.length() - 3); }
    else if (trimmed.endsWith("KHZ")) { multiplier = 1e3; numStr = trimmed.substring(0, trimmed.length() - 3); }
    else if (trimmed.endsWith("HZ")) { multiplier = 1.0; numStr = trimmed.substring(0, trimmed.length() - 2); }
    else if (trimmed.endsWith("M") && trimmed.length() > 1)
    {
        juce::String beforeM = trimmed.substring(0, trimmed.length() - 1);
        if (beforeM.containsOnly("0123456789.,-+E")) { multiplier = 1e6; numStr = beforeM; }
    }
    else if (trimmed.endsWith("K") && trimmed.length() > 1)
    {
        juce::String beforeK = trimmed.substring(0, trimmed.length() - 1);
        if (beforeK.containsOnly("0123456789.,-+E")) { multiplier = 1e3; numStr = beforeK; }
    }
    
    double value = numStr.getDoubleValue();
    if (value == 0.0 && !numStr.containsOnly("0.,")) return 0.0;
    
    double result = value * multiplier;
    
    if (result < 1.0) result = std::round(result * 100.0) / 100.0;
    else if (result < 1e3) result = std::round(result * 10.0) / 10.0;
    else if (result < 1e6) result = std::round(result);
    else result = std::round(result / 10.0) * 10.0;
    
    return result;
}

// Helper function to snap frequency to common values
static double snapFrequency(double freqHz)
{
    double snapPoints[] = {
        1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, 10000000.0,
        2.0, 20.0, 200.0, 2000.0, 20000.0, 200000.0, 2000000.0,
        5.0, 50.0, 500.0, 5000.0, 50000.0, 500000.0, 5000000.0,
        60.0, 120.0, 440.0, 1000.0, 2000.0, 5000.0, 10000.0
    };
    
    const double snapThreshold = 0.05; 
    for (double snap : snapPoints)
    {
        if (std::abs(freqHz - snap) / snap < snapThreshold)
            return snap;
    }
    return freqHz;
}

//==============================================================================
HP33120APluginAudioProcessorEditor::HP33120APluginAudioProcessorEditor (HP33120APluginAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setResizable(true, true);
    setResizeLimits(1000, 600, 2000, 1200);
    setSize (1200, 800);  // Wider but shorter
    startTimer(100);  // Update UI every 100ms
    
    // Connection UI
    connectButton.setButtonText("Connect");
    connectButton.addListener(this);
    addAndMakeVisible(&connectButton);
    
    disconnectButton.setButtonText("Disconnect");
    disconnectButton.addListener(this);
    addAndMakeVisible(&disconnectButton);
    
    gpibAddressLabel.setText("GPIB Address:", juce::dontSendNotification);
    addAndMakeVisible(&gpibAddressLabel);
    
    gpibAddressEditor.setText("GPIB0::10::INSTR", juce::dontSendNotification);
    gpibAddressEditor.setMultiLine(false);
    addAndMakeVisible(&gpibAddressEditor);
    
    idnLabel.setText("IDN: (Not connected)", juce::dontSendNotification);
    addAndMakeVisible(&idnLabel);
    
    midiStatusLabel.setText("MIDI: Waiting...", juce::dontSendNotification);
    midiStatusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(&midiStatusLabel);
    
    // Basic Settings
    basicGroup.setText("Basic Settings");
    addAndMakeVisible(&basicGroup);
    
    waveformLabel.setText("Waveform:", juce::dontSendNotification);
    addAndMakeVisible(&waveformLabel);
    waveformCombo.addItem("SIN", 1);
    waveformCombo.addItem("SQU", 2);
    waveformCombo.addItem("TRI", 3);
    waveformCombo.addItem("RAMP", 4);
    waveformCombo.addItem("NOIS", 5);
    waveformCombo.addItem("DC", 6);
    waveformCombo.addItem("USER", 7);
    // ARB waveforms will be added dynamically (IDs 8-11)
    waveformCombo.setSelectedId(1);
    waveformCombo.addListener(this);
    addAndMakeVisible(&waveformCombo);
    
    frequencyLabel.setText("Frequency:", juce::dontSendNotification);
    addAndMakeVisible(&frequencyLabel);
    frequencySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    frequencySlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> freqRange(1.0, 15e6);
    freqRange.setSkewForCentre(1000.0);
    frequencySlider.setNormalisableRange(freqRange);
    frequencySlider.setValue(1000.0);
    frequencySlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    frequencySlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    frequencySlider.setTextBoxIsEditable(true);
    frequencySlider.addListener(this);
    addAndMakeVisible(&frequencySlider);
    
    amplitudeLabel.setText("Amplitude (Vpp):", juce::dontSendNotification);
    addAndMakeVisible(&amplitudeLabel);
    amplitudeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amplitudeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    amplitudeSlider.setRange(0.01, 10.0, 0.01);
    amplitudeSlider.setValue(1.0);
    amplitudeSlider.setTextValueSuffix(" Vpp");
    amplitudeSlider.setNumDecimalPlacesToDisplay(3);
    amplitudeSlider.setTextBoxIsEditable(true);
    amplitudeSlider.addListener(this);
    addAndMakeVisible(&amplitudeSlider);
    
    offsetLabel.setText("Offset (V):", juce::dontSendNotification);
    addAndMakeVisible(&offsetLabel);
    offsetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    offsetSlider.setRange(-5.0, 5.0, 0.01);
    offsetSlider.setValue(0.0);
    offsetSlider.setTextValueSuffix(" V");
    offsetSlider.setNumDecimalPlacesToDisplay(3);
    offsetSlider.setTextBoxIsEditable(true);
    offsetSlider.addListener(this);
    addAndMakeVisible(&offsetSlider);
    
    phaseLabel.setText("Phase (deg):", juce::dontSendNotification);
    addAndMakeVisible(&phaseLabel);
    phaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    phaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    phaseSlider.setRange(0.0, 359.999, 0.001);
    phaseSlider.setValue(0.0);
    phaseSlider.setTextValueSuffix(" deg");
    phaseSlider.setNumDecimalPlacesToDisplay(3);
    phaseSlider.setTextBoxIsEditable(true);
    phaseSlider.addListener(this);
    addAndMakeVisible(&phaseSlider);
    
    dutyCycleLabel.setText("Duty (%):", juce::dontSendNotification);
    addAndMakeVisible(&dutyCycleLabel);
    dutyCycleSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    dutyCycleSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    dutyCycleSlider.setRange(0.1, 99.9, 0.1);
    dutyCycleSlider.setValue(50.0);
    dutyCycleSlider.setTextValueSuffix(" %");
    dutyCycleSlider.setNumDecimalPlacesToDisplay(1);
    dutyCycleSlider.setTextBoxIsEditable(true);
    dutyCycleSlider.addListener(this);
    addAndMakeVisible(&dutyCycleSlider);
    
    outputToggle.setButtonText("Output ON");
    outputToggle.addListener(this);
    addAndMakeVisible(&outputToggle);
    
    // AM/FM Settings
    amfmGroup.setText("AM / FM Settings");
    addAndMakeVisible(&amfmGroup);
    
    amEnabledToggle.setButtonText("AM On");
    amEnabledToggle.addListener(this);
    addAndMakeVisible(&amEnabledToggle);
    
    amDepthLabel.setText("AM Depth (%):", juce::dontSendNotification);
    addAndMakeVisible(&amDepthLabel);
    amDepthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amDepthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    amDepthSlider.setRange(0.0, 120.0, 0.1);
    amDepthSlider.setValue(50.0);
    amDepthSlider.setTextValueSuffix(" %");
    amDepthSlider.setNumDecimalPlacesToDisplay(1);
    amDepthSlider.setTextBoxIsEditable(true);
    amDepthSlider.addListener(this);
    addAndMakeVisible(&amDepthSlider);
    
    amSourceLabel.setText("AM Source:", juce::dontSendNotification);
    addAndMakeVisible(&amSourceLabel);
    amSourceCombo.addItem("BOTH", 1);
    amSourceCombo.addItem("EXT", 2);
    amSourceCombo.setSelectedId(1);
    amSourceCombo.addListener(this);
    addAndMakeVisible(&amSourceCombo);
    
    amIntWaveformLabel.setText("AM Int Waveform:", juce::dontSendNotification);
    addAndMakeVisible(&amIntWaveformLabel);
    amIntWaveformCombo.addItem("SIN", 1);
    amIntWaveformCombo.addItem("SQU", 2);
    amIntWaveformCombo.addItem("TRI", 3);
    amIntWaveformCombo.addItem("RAMP", 4);
    amIntWaveformCombo.addItem("NOIS", 5);
    amIntWaveformCombo.addItem("USER", 6);
    amIntWaveformCombo.setSelectedId(1);
    amIntWaveformCombo.addListener(this);
    addAndMakeVisible(&amIntWaveformCombo);
    
    amIntFreqLabel.setText("AM Int Freq:", juce::dontSendNotification);
    addAndMakeVisible(&amIntFreqLabel);
    amIntFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amIntFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> amFreqRange(0.01, 20000.0);
    amFreqRange.setSkewForCentre(100.0);
    amIntFreqSlider.setNormalisableRange(amFreqRange);
    amIntFreqSlider.setValue(100.0);
    amIntFreqSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    amIntFreqSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    amIntFreqSlider.setTextBoxIsEditable(true);
    amIntFreqSlider.addListener(this);
    addAndMakeVisible(&amIntFreqSlider);
    
    fmEnabledToggle.setButtonText("FM On");
    fmEnabledToggle.addListener(this);
    addAndMakeVisible(&fmEnabledToggle);
    
    fmDevLabel.setText("FM Dev:", juce::dontSendNotification);
    addAndMakeVisible(&fmDevLabel);
    fmDevSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fmDevSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> fmDevRange(0.01, 7.5e6);
    fmDevRange.setSkewForCentre(1000.0);
    fmDevSlider.setNormalisableRange(fmDevRange);
    fmDevSlider.setValue(100.0);
    fmDevSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    fmDevSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    fmDevSlider.setTextBoxIsEditable(true);
    fmDevSlider.addListener(this);
    addAndMakeVisible(&fmDevSlider);
    
    fmSourceLabel.setText("FM Source:", juce::dontSendNotification);
    addAndMakeVisible(&fmSourceLabel);
    fmSourceCombo.addItem("INT", 1);
    fmSourceCombo.addItem("EXT", 2);
    fmSourceCombo.setSelectedId(1);
    fmSourceCombo.addListener(this);
    addAndMakeVisible(&fmSourceCombo);
    
    fmIntWaveformLabel.setText("FM Int Waveform:", juce::dontSendNotification);
    addAndMakeVisible(&fmIntWaveformLabel);
    fmIntWaveformCombo.addItem("SIN", 1);
    fmIntWaveformCombo.addItem("SQU", 2);
    fmIntWaveformCombo.addItem("TRI", 3);
    fmIntWaveformCombo.addItem("RAMP", 4);
    fmIntWaveformCombo.addItem("NOIS", 5);
    fmIntWaveformCombo.addItem("USER", 6);
    fmIntWaveformCombo.setSelectedId(1);
    fmIntWaveformCombo.addListener(this);
    addAndMakeVisible(&fmIntWaveformCombo);
    
    fmIntFreqLabel.setText("FM Int Freq:", juce::dontSendNotification);
    addAndMakeVisible(&fmIntFreqLabel);
    fmIntFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fmIntFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> fmIntFreqRange(0.01, 10000.0);
    fmIntFreqRange.setSkewForCentre(100.0);
    fmIntFreqSlider.setNormalisableRange(fmIntFreqRange);
    fmIntFreqSlider.setValue(10.0);
    fmIntFreqSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    fmIntFreqSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    fmIntFreqSlider.setTextBoxIsEditable(true);
    fmIntFreqSlider.addListener(this);
    addAndMakeVisible(&fmIntFreqSlider);
    
    // FSK Settings
    fskGroup.setText("FSK Settings");
    addAndMakeVisible(&fskGroup);
    
    fskEnabledToggle.setButtonText("FSK On");
    fskEnabledToggle.addListener(this);
    addAndMakeVisible(&fskEnabledToggle);
    
    fskFreqLabel.setText("Hop Freq:", juce::dontSendNotification);
    addAndMakeVisible(&fskFreqLabel);
    fskFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fskFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> fskFreqRange(0.01, 15e6);
    fskFreqRange.setSkewForCentre(1000.0);
    fskFreqSlider.setNormalisableRange(fskFreqRange);
    fskFreqSlider.setValue(100.0);
    fskFreqSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    fskFreqSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    fskFreqSlider.setTextBoxIsEditable(true);
    fskFreqSlider.addListener(this);
    addAndMakeVisible(&fskFreqSlider);
    
    fskSourceLabel.setText("FSK Source:", juce::dontSendNotification);
    addAndMakeVisible(&fskSourceLabel);
    fskSourceCombo.addItem("INT", 1);
    fskSourceCombo.addItem("EXT", 2);
    fskSourceCombo.setSelectedId(1);
    fskSourceCombo.addListener(this);
    addAndMakeVisible(&fskSourceCombo);
    
    fskRateLabel.setText("Int Rate:", juce::dontSendNotification);
    addAndMakeVisible(&fskRateLabel);
    fskRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fskRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> fskRateRange(0.01, 50000.0);
    fskRateRange.setSkewForCentre(100.0);
    fskRateSlider.setNormalisableRange(fskRateRange);
    fskRateSlider.setValue(10.0);
    fskRateSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    fskRateSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    fskRateSlider.setTextBoxIsEditable(true);
    fskRateSlider.addListener(this);
    addAndMakeVisible(&fskRateSlider);
    
    // Advanced Settings
    advGroup.setText("Advanced (Sweep/Burst/Sync/ARB)");
    addAndMakeVisible(&advGroup);
    
    sweepEnabledToggle.setButtonText("Sweep On");
    sweepEnabledToggle.addListener(this);
    addAndMakeVisible(&sweepEnabledToggle);
    
    sweepStartLabel.setText("Sweep Start Freq:", juce::dontSendNotification);
    addAndMakeVisible(&sweepStartLabel);
    sweepStartSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sweepStartSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> sweepStartRange(1.0, 15e6);
    sweepStartRange.setSkewForCentre(1000.0);
    sweepStartSlider.setNormalisableRange(sweepStartRange);
    sweepStartSlider.setValue(1000.0);
    sweepStartSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    sweepStartSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    sweepStartSlider.setTextBoxIsEditable(true);
    sweepStartSlider.addListener(this);
    addAndMakeVisible(&sweepStartSlider);
    
    sweepStopLabel.setText("Sweep Stop Freq:", juce::dontSendNotification);
    addAndMakeVisible(&sweepStopLabel);
    sweepStopSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sweepStopSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    juce::NormalisableRange<double> sweepStopRange(1.0, 15e6);
    sweepStopRange.setSkewForCentre(10000.0);
    sweepStopSlider.setNormalisableRange(sweepStopRange);
    sweepStopSlider.setValue(10000.0);
    sweepStopSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    sweepStopSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    sweepStopSlider.setTextBoxIsEditable(true);
    sweepStopSlider.addListener(this);
    addAndMakeVisible(&sweepStopSlider);
    
    sweepTimeLabel.setText("Sweep Time (s):", juce::dontSendNotification);
    addAndMakeVisible(&sweepTimeLabel);
    sweepTimeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sweepTimeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    sweepTimeSlider.setRange(0.001, 3600.0, 0.001);
    sweepTimeSlider.setValue(1.0);
    sweepTimeSlider.setTextValueSuffix(" s");
    sweepTimeSlider.setNumDecimalPlacesToDisplay(3);
    sweepTimeSlider.setTextBoxIsEditable(true);
    sweepTimeSlider.addListener(this);
    addAndMakeVisible(&sweepTimeSlider);
    
    burstEnabledToggle.setButtonText("Burst On");
    burstEnabledToggle.addListener(this);
    addAndMakeVisible(&burstEnabledToggle);
    
    burstCyclesLabel.setText("Cycles:", juce::dontSendNotification);
    addAndMakeVisible(&burstCyclesLabel);
    burstCyclesSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    burstCyclesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    burstCyclesSlider.setRange(1.0, 50000.0, 1.0);
    burstCyclesSlider.setValue(1.0);
    burstCyclesSlider.setTextValueSuffix("");
    burstCyclesSlider.setNumDecimalPlacesToDisplay(0);
    burstCyclesSlider.setTextBoxIsEditable(true);
    burstCyclesSlider.addListener(this);
    addAndMakeVisible(&burstCyclesSlider);
    
    burstPhaseLabel.setText("Burst Phase (deg):", juce::dontSendNotification);
    addAndMakeVisible(&burstPhaseLabel);
    burstPhaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    burstPhaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    burstPhaseSlider.setRange(-360.0, 360.0, 1.0);
    burstPhaseSlider.setValue(0.0);
    burstPhaseSlider.setTextValueSuffix(" deg");
    burstPhaseSlider.setNumDecimalPlacesToDisplay(1);
    burstPhaseSlider.setTextBoxIsEditable(true);
    burstPhaseSlider.addListener(this);
    addAndMakeVisible(&burstPhaseSlider);
    
    burstIntPeriodLabel.setText("Int Period (s):", juce::dontSendNotification);
    addAndMakeVisible(&burstIntPeriodLabel);
    burstIntPeriodSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    burstIntPeriodSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    burstIntPeriodSlider.setRange(1e-6, 3600.0, 1e-6);
    burstIntPeriodSlider.setValue(0.1);
    burstIntPeriodSlider.setTextValueSuffix(" s");
    burstIntPeriodSlider.setNumDecimalPlacesToDisplay(6);
    burstIntPeriodSlider.setTextBoxIsEditable(true);
    burstIntPeriodSlider.addListener(this);
    addAndMakeVisible(&burstIntPeriodSlider);
    
    burstSourceLabel.setText("Burst Source:", juce::dontSendNotification);
    addAndMakeVisible(&burstSourceLabel);
    burstSourceCombo.addItem("INT", 1);
    burstSourceCombo.addItem("EXT", 2);
    burstSourceCombo.setSelectedId(1);
    burstSourceCombo.addListener(this);
    addAndMakeVisible(&burstSourceCombo);
    
    syncEnabledToggle.setButtonText("Sync On");
    syncEnabledToggle.addListener(this);
    addAndMakeVisible(&syncEnabledToggle);
    
    syncPhaseLabel.setText("Sync Phase (deg):", juce::dontSendNotification);
    addAndMakeVisible(&syncPhaseLabel);
    syncPhaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    syncPhaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 150, 25);
    syncPhaseSlider.setRange(0.0, 359.999, 0.001);
    syncPhaseSlider.setValue(0.0);
    syncPhaseSlider.setTextValueSuffix(" deg");
    syncPhaseSlider.setNumDecimalPlacesToDisplay(3);
    syncPhaseSlider.setTextBoxIsEditable(true);
    syncPhaseSlider.addListener(this);
    addAndMakeVisible(&syncPhaseSlider);
    
    // Initialize 4 ARB slots
    const char* defaultNames[] = {"MYARB", "USER", "VOLATILE", "CUSTOM"};
    for (int i = 0; i < 4; ++i)
    {
        arbSlotUIs[i].slotIndex = i;
        
        arbSlotUIs[i].nameEditor.setText(defaultNames[i], juce::dontSendNotification);
        arbSlotUIs[i].nameEditor.setMultiLine(false);
        arbSlotUIs[i].nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::darkgrey);
        arbSlotUIs[i].nameEditor.onTextChange = [this, i]() { 
            // Update ARBManager slot name if available
            if (audioProcessor.arbManager)
            {
                auto& slot = audioProcessor.arbManager->getSlot(i);
                juce::ScopedLock sl(slot.lock);
                slot.name = arbSlotUIs[i].nameEditor.getText();
            }
            refreshWaveformComboBox(); 
        };
        addAndMakeVisible(&arbSlotUIs[i].nameEditor);
        
        arbSlotUIs[i].pointsLabel.setText("Points:", juce::dontSendNotification);
        addAndMakeVisible(&arbSlotUIs[i].pointsLabel);
        
        arbSlotUIs[i].pointsSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        arbSlotUIs[i].pointsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, true, 100, 25);
        arbSlotUIs[i].pointsSlider.setRange(8.0, 16000.0, 1.0);
        arbSlotUIs[i].pointsSlider.setValue(1024.0);
        arbSlotUIs[i].pointsSlider.setTextValueSuffix(" pts");
        arbSlotUIs[i].pointsSlider.setNumDecimalPlacesToDisplay(0);
        arbSlotUIs[i].pointsSlider.setTextBoxIsEditable(true);
        arbSlotUIs[i].pointsSlider.addListener(this);
        addAndMakeVisible(&arbSlotUIs[i].pointsSlider);
        
        arbSlotUIs[i].loadButton.setButtonText("Load File");
        arbSlotUIs[i].loadButton.addListener(this);
        addAndMakeVisible(&arbSlotUIs[i].loadButton);
        
        arbSlotUIs[i].uploadButton.setButtonText("Upload");
        arbSlotUIs[i].uploadButton.addListener(this);
        addAndMakeVisible(&arbSlotUIs[i].uploadButton);
        
        arbSlotUIs[i].deleteButton.setButtonText("Delete");
        arbSlotUIs[i].deleteButton.addListener(this);
        addAndMakeVisible(&arbSlotUIs[i].deleteButton);
        
        arbSlotUIs[i].statusLabel.setText("Ready", juce::dontSendNotification);
        arbSlotUIs[i].statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible(&arbSlotUIs[i].statusLabel);
        
        arbSlotUIs[i].fileNameLabel.setText("No file loaded", juce::dontSendNotification);
        arbSlotUIs[i].fileNameLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(&arbSlotUIs[i].fileNameLabel);
    }
    
    // Refresh waveform combo box with ARB names (after ARB slots are initialized)
    refreshWaveformComboBox();
    
    triggerSourceLabel.setText("Trigger Source:", juce::dontSendNotification);
    addAndMakeVisible(&triggerSourceLabel);
    triggerSourceCombo.addItem("IMM", 1);
    triggerSourceCombo.addItem("EXT", 2);
    triggerSourceCombo.addItem("BUS", 3);
    triggerSourceCombo.setSelectedId(1);
    triggerSourceCombo.addListener(this);
    addAndMakeVisible(&triggerSourceCombo);
    
    // Status
    statusBox.setMultiLine(true);
    statusBox.setReadOnly(true);
    juce::Font statusFont(juce::FontOptions(12.0f));
    statusFont.setTypefaceName(juce::Font::getDefaultMonospacedFontName());
    statusBox.setFont(statusFont);
    addAndMakeVisible(&statusBox);
    
    // Set up MIDI status callback
    audioProcessor.midiStatusCallback = [this](const juce::String& message) {
        appendStatus(message);
    };
    
    // Set up device logging callback to show raw hardware responses
    audioProcessor.getDevice().logCallback = [this](const std::string& message) {
        juce::MessageManager::callAsync([this, message]() {
            appendStatus(juce::String(message));
        });
    };
    
    // Create parameter attachments
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::FREQUENCY, frequencySlider));
    // Set text conversion functions AFTER attachment
    frequencySlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    frequencySlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::AMPLITUDE, amplitudeSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::OFFSET, offsetSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::PHASE, phaseSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::DUTY_CYCLE, dutyCycleSlider));
    
    comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, Parameters::WAVEFORM, waveformCombo));
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::OUTPUT_ENABLED, outputToggle));
    
    // ARB Slot parameter attachments
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT1_POINTS, arbSlotUIs[0].pointsSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT2_POINTS, arbSlotUIs[1].pointsSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT3_POINTS, arbSlotUIs[2].pointsSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT4_POINTS, arbSlotUIs[3].pointsSlider));
}

HP33120APluginAudioProcessorEditor::~HP33120APluginAudioProcessorEditor()
{
}

//==============================================================================
void HP33120APluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void HP33120APluginAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int rowHeight = 28;
    const int labelWidth = 140;
    const int spacing = 4;
    const int groupSpacing = 10;
    
    // Connection row
    int controlWidth = (area.getWidth() - labelWidth - 20) / 2;
    auto connRow = area.removeFromTop(rowHeight);
    connectButton.setBounds(connRow.removeFromLeft(100).reduced(2));
    disconnectButton.setBounds(connRow.removeFromLeft(100).reduced(2));
    gpibAddressLabel.setBounds(connRow.removeFromLeft(labelWidth).reduced(2));
    gpibAddressEditor.setBounds(connRow.removeFromLeft(200).reduced(2));
    idnLabel.setBounds(connRow.removeFromLeft(250).reduced(2));
    midiStatusLabel.setBounds(connRow.reduced(2));
    area.removeFromTop(groupSpacing);
    
    // Basic Settings
    int basicHeight = rowHeight * 7 + spacing * 6 + 20;
    basicGroup.setBounds(area.removeFromTop(basicHeight));
    auto basicArea = basicGroup.getBounds().reduced(10, 25);
    
    auto row0 = basicArea.removeFromTop(rowHeight);
    waveformLabel.setBounds(row0.removeFromLeft(labelWidth).reduced(2));
    waveformCombo.setBounds(row0.removeFromLeft(controlWidth).reduced(2));
    basicArea.removeFromTop(spacing);
    
    auto row1 = basicArea.removeFromTop(rowHeight);
    frequencyLabel.setBounds(row1.removeFromLeft(labelWidth).reduced(2));
    frequencySlider.setBounds(row1.reduced(2));
    basicArea.removeFromTop(spacing);
    
    auto row2 = basicArea.removeFromTop(rowHeight);
    amplitudeLabel.setBounds(row2.removeFromLeft(labelWidth).reduced(2));
    amplitudeSlider.setBounds(row2.reduced(2));
    basicArea.removeFromTop(spacing);
    
    auto row3 = basicArea.removeFromTop(rowHeight);
    offsetLabel.setBounds(row3.removeFromLeft(labelWidth).reduced(2));
    offsetSlider.setBounds(row3.reduced(2));
    basicArea.removeFromTop(spacing);
    
    auto row4 = basicArea.removeFromTop(rowHeight);
    phaseLabel.setBounds(row4.removeFromLeft(labelWidth).reduced(2));
    phaseSlider.setBounds(row4.reduced(2));
    basicArea.removeFromTop(spacing);
    
    auto row5 = basicArea.removeFromTop(rowHeight);
    dutyCycleLabel.setBounds(row5.removeFromLeft(labelWidth).reduced(2));
    dutyCycleSlider.setBounds(row5.reduced(2));
    basicArea.removeFromTop(spacing);
    
    auto row6 = basicArea.removeFromTop(rowHeight);
    outputToggle.setBounds(row6.removeFromLeft(labelWidth + controlWidth).reduced(2));
    
    area.removeFromTop(groupSpacing);
    
    // AM/FM
    int amfmHeight = rowHeight * 6 + spacing * 5 + 25;
    amfmGroup.setBounds(area.removeFromTop(amfmHeight));
    auto amfmArea = amfmGroup.getBounds().reduced(10, 25);
    
    auto amRow0 = amfmArea.removeFromTop(rowHeight);
    amEnabledToggle.setBounds(amRow0.removeFromLeft(80).reduced(2));
    amDepthLabel.setBounds(amRow0.removeFromLeft(labelWidth - 80 + spacing).reduced(2));
    amDepthSlider.setBounds(amRow0.removeFromLeft(controlWidth).reduced(2));
    amSourceLabel.setBounds(amRow0.removeFromLeft(labelWidth).reduced(2));
    amSourceCombo.setBounds(amRow0.removeFromLeft(100).reduced(2));
    amfmArea.removeFromTop(spacing);
    
    auto amRow1 = amfmArea.removeFromTop(rowHeight);
    amIntWaveformLabel.setBounds(amRow1.removeFromLeft(labelWidth).reduced(2));
    amIntWaveformCombo.setBounds(amRow1.removeFromLeft(controlWidth).reduced(2));
    amIntFreqLabel.setBounds(amRow1.removeFromLeft(labelWidth).reduced(2));
    amIntFreqSlider.setBounds(amRow1.reduced(2));
    amfmArea.removeFromTop(spacing);
    
    auto fmRow0 = amfmArea.removeFromTop(rowHeight);
    fmEnabledToggle.setBounds(fmRow0.removeFromLeft(80).reduced(2));
    fmDevLabel.setBounds(fmRow0.removeFromLeft(labelWidth - 80 + spacing).reduced(2));
    fmDevSlider.setBounds(fmRow0.removeFromLeft(controlWidth).reduced(2));
    fmSourceLabel.setBounds(fmRow0.removeFromLeft(labelWidth).reduced(2));
    fmSourceCombo.setBounds(fmRow0.removeFromLeft(100).reduced(2));
    amfmArea.removeFromTop(spacing);
    
    auto fmRow1 = amfmArea.removeFromTop(rowHeight);
    fmIntWaveformLabel.setBounds(fmRow1.removeFromLeft(labelWidth).reduced(2));
    fmIntWaveformCombo.setBounds(fmRow1.removeFromLeft(controlWidth).reduced(2));
    fmIntFreqLabel.setBounds(fmRow1.removeFromLeft(labelWidth).reduced(2));
    fmIntFreqSlider.setBounds(fmRow1.reduced(2));
    
    area.removeFromTop(groupSpacing);
    
    // FSK
    int fskHeight = rowHeight * 2 + spacing + 20;
    fskGroup.setBounds(area.removeFromTop(fskHeight));
    auto fskArea = fskGroup.getBounds().reduced(10, 25);
    
    auto fskRow0 = fskArea.removeFromTop(rowHeight);
    fskEnabledToggle.setBounds(fskRow0.removeFromLeft(80).reduced(2));
    fskFreqLabel.setBounds(fskRow0.removeFromLeft(labelWidth).reduced(2));
    fskFreqSlider.setBounds(fskRow0.reduced(2));
    fskArea.removeFromTop(spacing);
    
    auto fskRow1 = fskArea.removeFromTop(rowHeight);
    fskSourceLabel.setBounds(fskRow1.removeFromLeft(labelWidth).reduced(2));
    fskSourceCombo.setBounds(fskRow1.removeFromLeft(100).reduced(2));
    fskRateLabel.setBounds(fskRow1.removeFromLeft(labelWidth).reduced(2));
    fskRateSlider.setBounds(fskRow1.reduced(2));
    
    area.removeFromTop(groupSpacing);
    
    // Advanced (includes 4 ARB slots: each takes rowHeight * 2 + spacing)
    int arbSlotsHeight = 4 * (rowHeight * 2 + spacing) + spacing;  // 4 slots + spacing between
    int advHeight = rowHeight * 6 + spacing * 5 + 20 + arbSlotsHeight;
    advGroup.setBounds(area.removeFromTop(advHeight));
    auto advArea = advGroup.getBounds().reduced(10, 25);
    
    auto sweepRow0 = advArea.removeFromTop(rowHeight);
    sweepEnabledToggle.setBounds(sweepRow0.removeFromLeft(80).reduced(2));
    sweepStartLabel.setBounds(sweepRow0.removeFromLeft(labelWidth).reduced(2));
    sweepStartSlider.setBounds(sweepRow0.removeFromLeft(controlWidth).reduced(2));
    sweepStopLabel.setBounds(sweepRow0.removeFromLeft(labelWidth).reduced(2));
    sweepStopSlider.setBounds(sweepRow0.reduced(2));
    advArea.removeFromTop(spacing);
    
    auto sweepRow1 = advArea.removeFromTop(rowHeight);
    sweepTimeLabel.setBounds(sweepRow1.removeFromLeft(labelWidth).reduced(2));
    sweepTimeSlider.setBounds(sweepRow1.reduced(2));
    advArea.removeFromTop(spacing);
    
    auto burstRow0 = advArea.removeFromTop(rowHeight);
    burstEnabledToggle.setBounds(burstRow0.removeFromLeft(80).reduced(2));
    burstCyclesLabel.setBounds(burstRow0.removeFromLeft(labelWidth).reduced(2));
    burstCyclesSlider.setBounds(burstRow0.removeFromLeft(controlWidth).reduced(2));
    burstPhaseLabel.setBounds(burstRow0.removeFromLeft(labelWidth).reduced(2));
    burstPhaseSlider.setBounds(burstRow0.reduced(2));
    advArea.removeFromTop(spacing);
    
    auto burstRow1 = advArea.removeFromTop(rowHeight);
    burstIntPeriodLabel.setBounds(burstRow1.removeFromLeft(labelWidth).reduced(2));
    burstIntPeriodSlider.setBounds(burstRow1.removeFromLeft(controlWidth).reduced(2));
    burstSourceLabel.setBounds(burstRow1.removeFromLeft(labelWidth).reduced(2));
    burstSourceCombo.setBounds(burstRow1.removeFromLeft(100).reduced(2));
    advArea.removeFromTop(spacing);
    
    auto syncRow = advArea.removeFromTop(rowHeight);
    syncEnabledToggle.setBounds(syncRow.removeFromLeft(80).reduced(2));
    syncPhaseLabel.setBounds(syncRow.removeFromLeft(labelWidth).reduced(2));
    syncPhaseSlider.setBounds(syncRow.reduced(2));
    advArea.removeFromTop(spacing);
    
    // ARB Slots (4 slots in vertical layout)
    int arbSlotHeight = rowHeight * 2 + spacing;
    for (int i = 0; i < 4; ++i)
    {
        auto slotArea = advArea.removeFromTop(arbSlotHeight);
        arbSlotUIs[i].bounds = slotArea;  // Store bounds for drag-and-drop detection
        auto slotRow1 = slotArea.removeFromTop(rowHeight);
        arbSlotUIs[i].nameEditor.setBounds(slotRow1.removeFromLeft(120).reduced(2));
        arbSlotUIs[i].pointsLabel.setBounds(slotRow1.removeFromLeft(60).reduced(2));
        arbSlotUIs[i].pointsSlider.setBounds(slotRow1.removeFromLeft(200).reduced(2));
        arbSlotUIs[i].loadButton.setBounds(slotRow1.removeFromLeft(100).reduced(2));
        arbSlotUIs[i].uploadButton.setBounds(slotRow1.removeFromLeft(80).reduced(2));
        arbSlotUIs[i].deleteButton.setBounds(slotRow1.removeFromLeft(80).reduced(2));
        
        auto slotRow2 = slotArea.removeFromTop(rowHeight);
        arbSlotUIs[i].fileNameLabel.setBounds(slotRow2.removeFromLeft(300).reduced(2));
        arbSlotUIs[i].statusLabel.setBounds(slotRow2.reduced(2));
        
        advArea.removeFromTop(spacing);
    }
    
    auto triggerRow = advArea.removeFromTop(rowHeight);
    triggerSourceLabel.setBounds(triggerRow.removeFromLeft(labelWidth).reduced(2));
    triggerSourceCombo.setBounds(triggerRow.reduced(2));
    
    area.removeFromTop(groupSpacing);
    statusBox.setBounds(area.reduced(2));
}

// PERFORMANCE: Timer runs at 100ms (10 Hz) - only for UI updates, not device communication
// This is lightweight and only updates UI labels, never queries device
void HP33120APluginAudioProcessorEditor::timerCallback()
{
    // PERFORMANCE: Use cached IDN instead of querying device every 100ms
    // Device queries are expensive (GPIB communication), so we cache on connection
    if (audioProcessor.isDeviceConnected())
    {
        if (!idnCacheValid)
        {
            // Cache IDN on first timer tick after connection (lazy load)
            cachedDeviceIDN = audioProcessor.getDeviceIDN();
            idnCacheValid = true;
        }
        idnLabel.setText("IDN: " + cachedDeviceIDN, juce::dontSendNotification);
    }
    else
    {
        idnLabel.setText("IDN: (Not connected)", juce::dontSendNotification);
        idnCacheValid = false; // Reset cache on disconnect
    }
    
    // 2. Scan ALL 16 MIDI Channels for activity
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    bool hasActiveNotes = false;
    
    for (int channel = 1; channel <= 16; ++channel)
    {
        for (int note = 0; note < 128; ++note)
        {
            if (audioProcessor.keyboardState.isNoteOn(channel, note))
            {
                hasActiveNotes = true;
                lastMidiActivityTime = currentTime;
                break;
            }
        }
        if (hasActiveNotes) break;
    }
    
    if (hasActiveNotes)
    {
        midiStatusLabel.setText("MIDI: Active", juce::dontSendNotification);
        midiStatusLabel.setColour(juce::Label::textColourId, juce::Colours::green);
    }
    else if (currentTime - lastMidiActivityTime < 500)
    {
        midiStatusLabel.setText("MIDI: Received", juce::dontSendNotification);
        midiStatusLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
    }
    else
    {
        if (!audioProcessor.isDeviceConnected())
        {
            midiStatusLabel.setText("MIDI: Waiting (Device not connected)", juce::dontSendNotification);
            midiStatusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else
        {
            midiStatusLabel.setText("MIDI: Waiting...", juce::dontSendNotification);
            midiStatusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        }
    }
}

void HP33120APluginAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &connectButton)
    {
        std::string address = gpibAddressEditor.getText().toStdString();
        if (audioProcessor.connectDevice(address))
        {
            appendStatus("Connected to: " + address);
            std::string idn = audioProcessor.getDeviceIDN();
            appendStatus("Device IDN: " + idn);
            
            // PERFORMANCE: Cache IDN for timer callback (avoids expensive GPIB query every 100ms)
            cachedDeviceIDN = juce::String(idn);
            idnCacheValid = true;
            
            // Query device for available waveforms and update combo boxes
            refreshWaveformComboBoxesFromDevice();
        }
        else
        {
            appendStatus("Connection failed to: " + address);
            appendStatus("Error: " + audioProcessor.getDevice().getLastError());
        }
    }
    else if (button == &disconnectButton)
    {
        audioProcessor.disconnectDevice();
        appendStatus("Disconnected");
        // PERFORMANCE: Clear IDN cache on disconnect
        idnCacheValid = false;
        cachedDeviceIDN.clear();
    }
    // Check ARB slot buttons
    for (int i = 0; i < 4; ++i)
    {
        if (button == &arbSlotUIs[i].loadButton)
        {
            loadAudioFileToSlot(i);
            return;
        }
        else if (button == &arbSlotUIs[i].uploadButton)
        {
            uploadSlotToDevice(i);
            return;
        }
        else if (button == &arbSlotUIs[i].deleteButton)
        {
            deleteARBFromDevice(i);
            return;
        }
    }
    
    // Legacy ARB button removed - use slot buttons instead
    
    // Other buttons...
    if (false)  // Placeholder for other button handlers
    {
        auto chooser = std::make_unique<juce::FileChooser>("Select ARB file", juce::File(), "*.txt;*.csv");
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        
        chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.exists())
            {
                appendStatus("ARB file selected: " + file.getFileName());
            }
        });
        chooser.release();
    }
    else if (button == &amEnabledToggle || button == &fmEnabledToggle || 
             button == &fskEnabledToggle || button == &sweepEnabledToggle ||
             button == &burstEnabledToggle || button == &syncEnabledToggle ||
             button == &outputToggle)
    {
        if (!audioProcessor.isDeviceConnected()) return;
        
        HP33120ADriver& device = audioProcessor.getDevice();
        try
        {
            if (button == &amEnabledToggle) device.setAMEnabled(amEnabledToggle.getToggleState());
            else if (button == &fmEnabledToggle) device.setFMEnabled(fmEnabledToggle.getToggleState());
            else if (button == &fskEnabledToggle) device.setFSKEnabled(fskEnabledToggle.getToggleState());
            else if (button == &sweepEnabledToggle) device.setSweepEnabled(sweepEnabledToggle.getToggleState());
            else if (button == &burstEnabledToggle) device.setBurstEnabled(burstEnabledToggle.getToggleState());
            else if (button == &syncEnabledToggle) device.setSyncEnabled(syncEnabledToggle.getToggleState());
            else if (button == &outputToggle) device.setOutputEnabled(outputToggle.getToggleState());
            
            // No delay - send commands immediately
        }
        catch (...) { appendStatus("Toggle error"); }
    }
}

void HP33120APluginAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBox)
{
    if (!audioProcessor.isDeviceConnected()) return;
    
    HP33120ADriver& device = audioProcessor.getDevice();
    try
    {
        if (comboBox == &waveformCombo)
        {
            int selectedId = waveformCombo.getSelectedId();
            
            // Built-in waveforms (IDs 1-7)
            if (selectedId >= 1 && selectedId <= 7)
            {
                juce::StringArray waveforms = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "DC", "USER"};
                int idx = selectedId - 1;
                if (idx >= 0 && idx < waveforms.size())
                {
                    if (idx == 6) // USER (generic)
                    {
                        // Generic USER - select VOLATILE or first available ARB
                        device.setWaveform("USER");
                    }
                    else
                    {
                        device.setWaveform(waveforms[idx].toStdString());
                    }
                }
                dutyCycleSlider.setEnabled(idx == 1); // Only for SQU
            }
            // ARB waveforms (IDs 8+)
            else if (selectedId >= 8)
            {
                // Get the ARB name from the combo box item text
                juce::String arbName = waveformCombo.getItemText(waveformCombo.getSelectedItemIndex());
                
                if (!arbName.isEmpty())
                {
                    // Select the specific ARB waveform
                    device.setUserWaveform(arbName.toStdString());
                    dutyCycleSlider.setEnabled(false); // ARB waveforms don't use duty cycle
                }
            }
        }
        else if (comboBox == &amIntWaveformCombo)
        {
            int selectedId = amIntWaveformCombo.getSelectedId();
            if (selectedId >= 1 && selectedId <= 6)
            {
                juce::StringArray waves = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "USER"};
                int idx = selectedId - 1;
                if (idx >= 0 && idx < waves.size())
                    device.setAMInternalWaveform(waves[idx].toStdString());
            }
            else if (selectedId >= 7)
            {
                // ARB waveform selected
                juce::String arbName = amIntWaveformCombo.getItemText(amIntWaveformCombo.getSelectedItemIndex());
                if (!arbName.isEmpty())
                    device.setAMInternalWaveform(arbName.toStdString());
            }
        }
        else if (comboBox == &fmIntWaveformCombo)
        {
            int selectedId = fmIntWaveformCombo.getSelectedId();
            if (selectedId >= 1 && selectedId <= 6)
            {
                juce::StringArray waves = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "USER"};
                int idx = selectedId - 1;
                if (idx >= 0 && idx < waves.size())
                    device.setFMInternalWaveform(waves[idx].toStdString());
            }
            else if (selectedId >= 7)
            {
                // ARB waveform selected
                juce::String arbName = fmIntWaveformCombo.getItemText(fmIntWaveformCombo.getSelectedItemIndex());
                if (!arbName.isEmpty())
                    device.setFMInternalWaveform(arbName.toStdString());
            }
        }
        else if (comboBox == &amSourceCombo)
        {
            juce::StringArray sources = {"BOTH", "EXT"};
            int idx = amSourceCombo.getSelectedId() - 1;
            if (idx >= 0) device.setAMSource(sources[idx].toStdString());
        }
        else if (comboBox == &fmSourceCombo)
        {
            juce::StringArray sources = {"INT", "EXT"};
            int idx = fmSourceCombo.getSelectedId() - 1;
            if (idx >= 0) device.setFMSource(sources[idx].toStdString());
        }
        else if (comboBox == &fskSourceCombo)
        {
            juce::StringArray sources = {"INT", "EXT"};
            int idx = fskSourceCombo.getSelectedId() - 1;
            if (idx >= 0) device.setFSKSource(sources[idx].toStdString());
        }
        else if (comboBox == &burstSourceCombo)
        {
            juce::StringArray sources = {"INT", "EXT"};
            int idx = burstSourceCombo.getSelectedId() - 1;
            if (idx >= 0) device.setBurstSource(sources[idx].toStdString());
        }
        else if (comboBox == &triggerSourceCombo)
        {
            juce::StringArray sources = {"IMM", "EXT", "BUS"};
            int idx = triggerSourceCombo.getSelectedId() - 1;
            if (idx >= 0) device.setTriggerSource(sources[idx].toStdString());
        }
        
        // No delay - send commands immediately
    }
    catch (...) { appendStatus("Combo error"); }
}

void HP33120APluginAudioProcessorEditor::sliderDragStarted(juce::Slider*) {}
void HP33120APluginAudioProcessorEditor::sliderDragEnded(juce::Slider* slider) { updateSingleParameter(slider); }

void HP33120APluginAudioProcessorEditor::updateSingleParameter(juce::Slider* slider)
{
    if (!audioProcessor.isDeviceConnected() || isUpdatingParameters) return;
    
    // CRITICAL FIX: Only send commands if user is physically interacting with the slider
    // This prevents MIDI updates from triggering command loops back to the device
    if (!slider->isMouseButtonDown() && !slider->hasKeyboardFocus(true))
    {
        // Slider value changed but user is not interacting - likely programmatic update (MIDI)
        // Don't send command to avoid loop
        return;
    }
    
    // Throttling is now handled in sliderValueChanged for drag updates
    // This function is called from sliderValueChanged (already throttled) or sliderDragEnded
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    lastUpdateTime = currentTime;
    isUpdatingParameters = true;
    HP33120ADriver& device = audioProcessor.getDevice();
    
    // PERFORMANCE: User interactions send directly to device for immediate feedback
    // Background thread is only for automation/LFO to prevent audio thread blocking
    // User interactions happen on UI thread, so direct calls are safe and responsive
    try
    {
        if (slider == &frequencySlider) 
        {
            device.setFrequency(frequencySlider.getValue());
        }
        else if (slider == &amplitudeSlider) 
        {
            device.setAmplitude(amplitudeSlider.getValue());
        }
        else if (slider == &offsetSlider) 
        {
            device.setOffset(offsetSlider.getValue());
        }
        else if (slider == &phaseSlider) 
        {
            device.setPhase(phaseSlider.getValue());
        }
        else if (slider == &dutyCycleSlider) 
        {
            device.setDutyCycle(dutyCycleSlider.getValue());
        }
        else if (slider == &amDepthSlider) device.setAMDepth(amDepthSlider.getValue());
        else if (slider == &amIntFreqSlider) device.setAMInternalFrequency(amIntFreqSlider.getValue());
        else if (slider == &fmDevSlider) device.setFMDeviation(fmDevSlider.getValue());
        else if (slider == &fmIntFreqSlider) device.setFMInternalFrequency(fmIntFreqSlider.getValue());
        else if (slider == &fskFreqSlider) device.setFSKFrequency(fskFreqSlider.getValue());
        else if (slider == &fskRateSlider) device.setFSKInternalRate(fskRateSlider.getValue());
        else if (slider == &sweepStartSlider) device.setSweepStartFreq(sweepStartSlider.getValue());
        else if (slider == &sweepStopSlider) device.setSweepStopFreq(sweepStopSlider.getValue());
        else if (slider == &sweepTimeSlider) device.setSweepTime(sweepTimeSlider.getValue());
        else if (slider == &burstCyclesSlider) device.setBurstCycles((int)burstCyclesSlider.getValue());
        else if (slider == &burstPhaseSlider) device.setBurstPhase(burstPhaseSlider.getValue());
        else if (slider == &burstIntPeriodSlider) device.setBurstInternalPeriod(burstIntPeriodSlider.getValue());
        else if (slider == &syncPhaseSlider) device.setSyncPhase(syncPhaseSlider.getValue());
    }
    catch (...) { appendStatus("Param update error"); }
    
    isUpdatingParameters = false;
}

void HP33120APluginAudioProcessorEditor::sliderValueChanged(juce::Slider* slider)
{
    if (isUpdatingParameters) return;
    
    // CRITICAL FIX: Ignore programmatic updates from automation/LFO
    // When automation changes a parameter, SliderAttachment updates the slider,
    // but we should NOT process it here - the ParameterListener handles device updates
    if (!slider->isMouseButtonDown() && !slider->hasKeyboardFocus(true))
    {
        // This is a programmatic update (automation/LFO), not user interaction
        // The ParameterListener will handle sending to device, so we skip here
        return;
    }
    
    // PERFORMANCE: Update during drag for smooth feedback, but throttle to avoid overwhelming device
    // Throttle to ~20 Hz (50ms) during dragging for smooth but not excessive updates
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    static constexpr int DRAG_UPDATE_INTERVAL_MS = 50; // 20 Hz during drag
    
    if (slider->isMouseButtonDown())
    {
        // During drag: throttle updates but still send them for smooth feedback
        if (currentTime - lastUpdateTime < DRAG_UPDATE_INTERVAL_MS)
            return; // Skip this update, too soon since last one
    }
    // If not dragging (keyboard input), update immediately
    
    // Check for snapped values to avoid recursive updates
    if (slider == &frequencySlider || slider == &sweepStartSlider || 
        slider == &sweepStopSlider || slider == &fskFreqSlider)
    {
        double snapped = snapFrequency(slider->getValue());
        if (std::abs(snapped - slider->getValue()) > 0.01)
        {
            slider->setValue(snapped, juce::dontSendNotification);
            return;
        }
    }
    
    // Handle ARB point count sliders
    for (int i = 0; i < 4; ++i)
    {
        if (slider == &arbSlotUIs[i].pointsSlider)
        {
            if (audioProcessor.arbManager)
            {
                int pointCount = (int)slider->getValue();
                audioProcessor.arbManager->setSlotPointCount(i, pointCount);
                arbSlotUIs[i].statusLabel.setText("Point count: " + juce::String(pointCount), juce::dontSendNotification);
                arbSlotUIs[i].statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
            }
            return;
        }
    }
    
    updateSingleParameter(slider);
}

void HP33120APluginAudioProcessorEditor::updateDeviceParameters()
{
    // Full sync implementation omitted for brevity, single param update is primary
}

void HP33120APluginAudioProcessorEditor::refreshWaveformComboBox()
{
    // Store current selection
    int currentWaveformSelection = waveformCombo.getSelectedId();
    
    // ComboBox doesn't have removeItem, so we need to rebuild
    // First, collect all ARB names
    juce::StringArray arbNames;
    for (int i = 0; i < 4; ++i)
    {
        juce::String arbName;
        if (audioProcessor.arbManager)
        {
            auto& slot = audioProcessor.arbManager->getSlot(i);
            juce::ScopedLock sl(slot.lock);
            arbName = slot.name;
        }
        else
        {
            arbName = arbSlotUIs[i].nameEditor.getText();
        }
        
        if (arbName.isEmpty())
            arbName = "ARB" + juce::String(i + 1);
        
        arbNames.add(arbName);
    }
    
    // Remove existing ARB items by checking if they exist and rebuilding
    // Since we can't remove individual items, we'll just add new ones
    // (duplicates won't be added if IDs match, but we need to avoid duplicates)
    // Actually, let's just add them - if they already exist with the same ID, JUCE will handle it
    for (int i = 0; i < arbNames.size(); ++i)
    {
        // Check if this ARB already exists
        int existingIndex = waveformCombo.indexOfItemId(8 + i);
        if (existingIndex >= 0)
        {
            // Update the text if it changed
            if (waveformCombo.getItemText(existingIndex) != arbNames[i])
            {
                waveformCombo.changeItemText(8 + i, arbNames[i]);
            }
        }
        else
        {
            // Add new ARB item
            waveformCombo.addItem(arbNames[i], 8 + i);
        }
    }
    
    // Restore selection if it's still valid
    if (waveformCombo.indexOfItemId(currentWaveformSelection) >= 0)
    {
        waveformCombo.setSelectedId(currentWaveformSelection, juce::dontSendNotification);
    }
}

void HP33120APluginAudioProcessorEditor::refreshWaveformComboBoxesFromDevice()
{
    if (!audioProcessor.isDeviceConnected()) return;
    
    try
    {
        // Query device for available waveforms
        std::vector<std::string> waveforms = audioProcessor.getDevice().queryWaveformCatalog();
        
        if (waveforms.empty())
        {
            appendStatus("No waveforms found in device catalog");
            return;
        }
        
        // Built-in waveform names (these are always available)
        juce::StringArray builtInWaveforms = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "DC"};
        juce::StringArray builtInARBs = {"SINC", "NEG_RAMP", "EXP_RISE", "EXP_FALL", "CARDIAC"};
        
        // Separate user-defined ARBs from built-ins
        juce::StringArray userARBs;
        juce::StringArray allARBs;  // All ARB waveforms (built-in + user-defined)
        
        for (const auto& wf : waveforms)
        {
            juce::String wfName = juce::String(wf).toUpperCase();
            
            // Check if it's a built-in ARB
            bool isBuiltInARB = false;
            for (const auto& builtIn : builtInARBs)
            {
                if (wfName == builtIn.toUpperCase())
                {
                    isBuiltInARB = true;
                    allARBs.add(wfName);
                    break;
                }
            }
            
            // If not a built-in ARB and not VOLATILE (which is temporary), it's a user-defined ARB
            if (!isBuiltInARB && wfName != "VOLATILE" && wfName != "USER")
            {
                // Check if it's not a standard waveform
                bool isStandard = false;
                for (const auto& std : builtInWaveforms)
                {
                    if (wfName == std.toUpperCase())
                    {
                        isStandard = true;
                        break;
                    }
                }
                if (!isStandard)
                {
                    userARBs.add(wfName);
                    allARBs.add(wfName);
                }
            }
            
            // Also add VOLATILE if it exists (temporary ARB memory)
            if (wfName == "VOLATILE")
            {
                allARBs.add("VOLATILE");
            }
        }
        
        // Update main waveform combo box
        // Store current selection
        int currentWaveformSelection = waveformCombo.getSelectedId();
        
        // Remove existing ARB items by rebuilding the combo box
        // Since ComboBox doesn't have removeItem, we'll clear and rebuild
        // But we want to keep built-in items (IDs 1-7), so we'll rebuild just the ARB section
        // First, remove all ARB items by finding their indices and clearing/rebuilding
        // Actually, simpler: just update existing or add new ARB items
        int nextID = 8;
        for (const auto& arb : allARBs)
        {
            // Check if this ARB already exists
            int existingIndex = waveformCombo.indexOfItemId(nextID);
            if (existingIndex >= 0)
            {
                // Update the text if it changed
                if (waveformCombo.getItemText(existingIndex) != arb)
                {
                    waveformCombo.changeItemText(nextID, arb);
                }
            }
            else
            {
                // Add new ARB item
                waveformCombo.addItem(arb, nextID);
            }
            nextID++;
        }
        
        // Remove any ARB items that are no longer in the device catalog
        // We can't easily remove items, so we'll just leave them (they'll be stale but harmless)
        // Or we could clear and rebuild, but that's more disruptive
        
        // Update AM/FM internal waveform combo boxes with ARB waveforms
        // Since ComboBox doesn't have removeItem, we'll update existing or add new ARB items
        for (auto* combo : {&amIntWaveformCombo, &fmIntWaveformCombo})
        {
            // Store current selection (different variable name to avoid shadowing)
            int comboCurrentSelection = combo->getSelectedId();
            
            // Add/update all ARB waveforms
            int arbStartID = 7;  // Start IDs for ARBs in AM/FM combos (after USER which is 6)
            for (const auto& arb : allARBs)
            {
                // Check if this ARB already exists
                int existingIndex = combo->indexOfItemId(arbStartID);
                if (existingIndex >= 0)
                {
                    // Update the text if it changed
                    if (combo->getItemText(existingIndex) != arb)
                    {
                        combo->changeItemText(arbStartID, arb);
                    }
                }
                else
                {
                    // Add new ARB item
                    combo->addItem(arb, arbStartID);
                }
                arbStartID++;
            }
            
            // Restore selection if it's still valid
            if (combo->indexOfItemId(comboCurrentSelection) >= 0)
            {
                combo->setSelectedId(comboCurrentSelection, juce::dontSendNotification);
            }
        }
        
        // Log the discovered waveforms
        juce::String logMsg = "Discovered waveforms: ";
        for (size_t i = 0; i < waveforms.size(); ++i)
        {
            if (i > 0) logMsg += ", ";
            logMsg += juce::String(waveforms[i]);
        }
        appendStatus(logMsg);
        
        if (!userARBs.isEmpty())
        {
            juce::String arbMsg = "User ARBs: ";
            for (int i = 0; i < userARBs.size(); ++i)
            {
                if (i > 0) arbMsg += ", ";
                arbMsg += userARBs[i];
            }
            appendStatus(arbMsg);
        }
    }
    catch (const std::exception& e)
    {
        appendStatus("Error querying waveform catalog: " + juce::String(e.what()));
    }
    catch (...)
    {
        appendStatus("Unknown error querying waveform catalog");
    }
}

// ARB Slot Management Functions
void HP33120APluginAudioProcessorEditor::loadAudioFileToSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 4) return;
    
    auto chooser = std::make_unique<juce::FileChooser>("Select Audio File (WAV/MP3)", juce::File(), "*.wav;*.mp3;*.WAV;*.MP3");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    
    chooser->launchAsync(chooserFlags, [this, slotIndex](const juce::FileChooser& fc)
    {
        try
        {
            auto file = fc.getResult();
            if (file.exists())
            {
                if (audioProcessor.arbManager && audioProcessor.audioFormatManager)
                {
                    bool success = audioProcessor.arbManager->loadAudioFile(slotIndex, file, *(audioProcessor.audioFormatManager.get()));
                    if (success)
                    {
                        arbSlotUIs[slotIndex].fileNameLabel.setText(file.getFileName(), juce::dontSendNotification);
                        arbSlotUIs[slotIndex].statusLabel.setText("Loaded", juce::dontSendNotification);
                        arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::green);
                        appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Loaded " + file.getFileName());
                    }
                    else
                    {
                        arbSlotUIs[slotIndex].statusLabel.setText("Load Failed", juce::dontSendNotification);
                        arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
                        appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Failed to load " + file.getFileName());
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            arbSlotUIs[slotIndex].statusLabel.setText("Error", juce::dontSendNotification);
            arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
            appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Exception - " + juce::String(e.what()));
        }
        catch (...)
        {
            arbSlotUIs[slotIndex].statusLabel.setText("Error", juce::dontSendNotification);
            arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
            appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Unknown error loading file");
        }
    });
    chooser.release();
}

void HP33120APluginAudioProcessorEditor::uploadSlotToDevice(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 4) return;
    if (!audioProcessor.arbManager) return;
    
    try
    {
        // Update slot name from editor
        juce::String slotName = arbSlotUIs[slotIndex].nameEditor.getText();
        if (slotName.isEmpty()) slotName = "MYARB";
        
        auto& slot = audioProcessor.arbManager->getSlot(slotIndex);
        {
            juce::ScopedLock sl(slot.lock);
            slot.name = slotName;
        }
        
        // Update point count from parameter
        int pointCount = (int)audioProcessor.parameters.getRawParameterValue(
            slotIndex == 0 ? Parameters::ARB_SLOT1_POINTS :
            slotIndex == 1 ? Parameters::ARB_SLOT2_POINTS :
            slotIndex == 2 ? Parameters::ARB_SLOT3_POINTS :
            Parameters::ARB_SLOT4_POINTS
        )->load();
        
        audioProcessor.arbManager->setSlotPointCount(slotIndex, pointCount);
        
        // Set status to "Uploading..." immediately
        arbSlotUIs[slotIndex].statusLabel.setText("Uploading...", juce::dontSendNotification);
        arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Starting upload...");
        
        // Use async upload to prevent UI freezing
        audioProcessor.arbManager->uploadSlotToDeviceAsync(slotIndex, 
            [this, slotIndex](int idx, bool success, const juce::String& message)
            {
                // This callback runs on the message thread, safe to update UI
                if (success)
                {
                    arbSlotUIs[idx].statusLabel.setText("Uploaded", juce::dontSendNotification);
                    arbSlotUIs[idx].statusLabel.setColour(juce::Label::textColourId, juce::Colours::green);
                    appendStatus("ARB Slot " + juce::String(idx + 1) + ": " + message);
                    // Refresh waveform combo boxes from device to get updated catalog
                    refreshWaveformComboBoxesFromDevice();
                }
                else
                {
                    arbSlotUIs[idx].statusLabel.setText("Upload Failed", juce::dontSendNotification);
                    arbSlotUIs[idx].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
                    appendStatus("ARB Slot " + juce::String(idx + 1) + ": " + message);
                }
            });
    }
    catch (const std::exception& e)
    {
        arbSlotUIs[slotIndex].statusLabel.setText("Error", juce::dontSendNotification);
        arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Exception - " + juce::String(e.what()));
    }
    catch (...)
    {
        arbSlotUIs[slotIndex].statusLabel.setText("Error", juce::dontSendNotification);
        arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Unknown error during upload");
    }
}

void HP33120APluginAudioProcessorEditor::deleteARBFromDevice(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 4) return;
    if (!audioProcessor.arbManager) return;
    
    juce::String slotName = arbSlotUIs[slotIndex].nameEditor.getText();
    if (slotName.isEmpty()) slotName = "MYARB";
    
    bool success = audioProcessor.arbManager->deleteARBFromDevice(slotName);
    if (success)
    {
        arbSlotUIs[slotIndex].statusLabel.setText("Deleted", juce::dontSendNotification);
        arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Deleted from device");
        // Refresh waveform combo boxes from device to update catalog
        refreshWaveformComboBoxesFromDevice();
    }
    else
    {
        arbSlotUIs[slotIndex].statusLabel.setText("Delete Failed", juce::dontSendNotification);
        arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Delete failed");
    }
}

// Drag-and-drop implementation
bool HP33120APluginAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
    {
        juce::File f(file);
        juce::String ext = f.getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3")
            return true;
    }
    return false;
}

void HP33120APluginAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty()) return;
    
    juce::File file(files[0]);
    if (!file.exists()) return;
    
    juce::String ext = file.getFileExtension().toLowerCase();
    if (ext != ".wav" && ext != ".mp3") return;
    
    // Determine which slot based on drop coordinates
    int slotIndex = -1;
    for (int i = 0; i < 4; ++i)
    {
        if (arbSlotUIs[i].bounds.contains(x, y))
        {
            slotIndex = i;
            break;
        }
    }
    
    // If no specific slot found, use slot 0
    if (slotIndex < 0) slotIndex = 0;
    
    // Load the file
    if (audioProcessor.arbManager && audioProcessor.audioFormatManager)
    {
        bool success = audioProcessor.arbManager->loadAudioFile(slotIndex, file, *(audioProcessor.audioFormatManager.get()));
        if (success)
        {
            arbSlotUIs[slotIndex].fileNameLabel.setText(file.getFileName(), juce::dontSendNotification);
            arbSlotUIs[slotIndex].statusLabel.setText("Loaded", juce::dontSendNotification);
            arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::green);
            appendStatus("ARB Slot " + juce::String(slotIndex + 1) + ": Loaded " + file.getFileName() + " (drag & drop)");
        }
        else
        {
            arbSlotUIs[slotIndex].statusLabel.setText("Load Failed", juce::dontSendNotification);
            arbSlotUIs[slotIndex].statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        }
    }
}

void HP33120APluginAudioProcessorEditor::appendStatus(const juce::String& message)
{
    // Add new message to the list
    statusMessages.add(message);
    
    // Keep only the last MAX_STATUS_MESSAGES messages
    if (statusMessages.size() > MAX_STATUS_MESSAGES)
    {
        statusMessages.remove(0);  // Remove oldest message
    }
    
    // Rebuild the entire status box with only the last 30 messages
    juce::String fullText;
    for (const auto& msg : statusMessages)
    {
        fullText += msg + "\n";
    }
    
    statusBox.setText(fullText);
    statusBox.moveCaretToEnd();
}