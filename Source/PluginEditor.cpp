#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ARBManager.h"
#include <cmath>
#include <algorithm>

// Helper function to format frequency with appropriate unit (Hz, kHz, MHz)
// Uses smart precision: more decimals for small values, fewer for large values
static juce::String formatFrequency(double freqHz)
{
    if (freqHz >= 1e6)
    {
        // MHz range: show up to 6 decimals for precision, trim trailing zeros
        double mhz = freqHz / 1e6;
        if (mhz >= 10.0)
            return juce::String(mhz, 4) + " MHz";
        else
            return juce::String(mhz, 6) + " MHz";
    }
    else if (freqHz >= 1e3)
    {
        // kHz range: precision based on value
        double khz = freqHz / 1e3;
        if (khz >= 100.0)
            return juce::String(khz, 3) + " kHz";
        else if (khz >= 10.0)
            return juce::String(khz, 4) + " kHz";
        else
            return juce::String(khz, 5) + " kHz";
    }
    else if (freqHz >= 1.0)
    {
        // Hz range
        if (freqHz >= 100.0)
            return juce::String(freqHz, 2) + " Hz";
        else if (freqHz >= 10.0)
            return juce::String(freqHz, 3) + " Hz";
        else
            return juce::String(freqHz, 4) + " Hz";
    }
    else if (freqHz >= 0.001)
    {
        // mHz range
        return juce::String(freqHz * 1000.0, 3) + " mHz";
    }
    else
    {
        // µHz range (sub-millihertz)
        return juce::String(freqHz * 1e6, 2) + " uHz";
    }
}

// Helper function to parse frequency string with units (e.g., "15kHz", "10Hz", "20MHz", "100mHz")
static double parseFrequency(const juce::String& text)
{
    juce::String trimmed = text.trim().toUpperCase();
    if (trimmed.isEmpty())
        return 0.0;
    
    // Remove spaces
    trimmed = trimmed.removeCharacters(" ");
    
    double multiplier = 1.0;
    juce::String numStr = trimmed;
    
    // Check for unit suffixes (order matters - check longer suffixes first)
    if (trimmed.endsWith("MHZ")) { multiplier = 1e6; numStr = trimmed.substring(0, trimmed.length() - 3); }
    else if (trimmed.endsWith("KHZ")) { multiplier = 1e3; numStr = trimmed.substring(0, trimmed.length() - 3); }
    else if (trimmed.endsWith("UHZ")) { multiplier = 1e-6; numStr = trimmed.substring(0, trimmed.length() - 3); }  // µHz
    else if (trimmed.endsWith("MHZ")) { multiplier = 1e-3; numStr = trimmed.substring(0, trimmed.length() - 3); }  // mHz
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
    
    // Clamp to HP33120A range: 100 µHz to 15 MHz
    result = std::max(0.0001, std::min(15e6, result));
    
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
    // Apply vim console aesthetic
    setLookAndFeel(&vimLookAndFeel);
    
    // Fixed size plugin - wider to fit labels properly
    setResizable(false, false);
    setSize(1300, 900);  // Wider window for proper label display
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
    frequencySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    // HP33120A: 100 µHz to 15 MHz - use logarithmic-like skew for better control
    juce::NormalisableRange<double> freqRange(0.0001, 15e6);
    freqRange.setSkewForCentre(1000.0);  // Center around 1 kHz for good audio frequency control
    frequencySlider.setNormalisableRange(freqRange);
    frequencySlider.setValue(1000.0);
    frequencySlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    frequencySlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    frequencySlider.setTextBoxIsEditable(true);
    frequencySlider.addListener(this);
    addAndMakeVisible(&frequencySlider);
    
    amplitudeLabel.setText("Amplitude", juce::dontSendNotification);
    addAndMakeVisible(&amplitudeLabel);
    amplitudeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amplitudeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    amplitudeSlider.setRange(0.01, 10.0, 0.01);
    amplitudeSlider.setValue(1.0);
    amplitudeSlider.setTextValueSuffix(" Vpp");
    amplitudeSlider.setNumDecimalPlacesToDisplay(3);
    amplitudeSlider.setTextBoxIsEditable(true);
    amplitudeSlider.addListener(this);
    addAndMakeVisible(&amplitudeSlider);
    
    offsetLabel.setText("Offset", juce::dontSendNotification);
    addAndMakeVisible(&offsetLabel);
    offsetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    offsetSlider.setRange(-5.0, 5.0, 0.01);
    offsetSlider.setValue(0.0);
    offsetSlider.setTextValueSuffix(" V");
    offsetSlider.setNumDecimalPlacesToDisplay(3);
    offsetSlider.setTextBoxIsEditable(true);
    offsetSlider.addListener(this);
    addAndMakeVisible(&offsetSlider);
    
    phaseLabel.setText("Phase", juce::dontSendNotification);
    addAndMakeVisible(&phaseLabel);
    phaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    phaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    phaseSlider.setRange(0.0, 359.999, 0.001);
    phaseSlider.setValue(0.0);
    phaseSlider.setTextValueSuffix(" deg");
    phaseSlider.setNumDecimalPlacesToDisplay(3);
    phaseSlider.setTextBoxIsEditable(true);
    phaseSlider.addListener(this);
    addAndMakeVisible(&phaseSlider);
    
    dutyCycleLabel.setText("Duty Cycle", juce::dontSendNotification);
    addAndMakeVisible(&dutyCycleLabel);
    dutyCycleSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    dutyCycleSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    dutyCycleSlider.setRange(0.1, 99.9, 0.1);
    dutyCycleSlider.setValue(50.0);
    dutyCycleSlider.setTextValueSuffix(" %");
    dutyCycleSlider.setNumDecimalPlacesToDisplay(1);
    dutyCycleSlider.setTextBoxIsEditable(true);
    dutyCycleSlider.addListener(this);
    addAndMakeVisible(&dutyCycleSlider);
    
    outputToggle.setButtonText("Output: OFF");
    outputToggle.addListener(this);
    addAndMakeVisible(&outputToggle);
    
    // AM/FM Settings
    amfmGroup.setText("AM / FM Settings");
    addAndMakeVisible(&amfmGroup);
    
    amEnabledToggle.setButtonText("AM Enable: OFF");
    amEnabledToggle.addListener(this);
    addAndMakeVisible(&amEnabledToggle);
    
    amDepthLabel.setText("Depth", juce::dontSendNotification);
    addAndMakeVisible(&amDepthLabel);
    amDepthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amDepthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
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
    
    amIntFreqLabel.setText("Int Freq", juce::dontSendNotification);
    addAndMakeVisible(&amIntFreqLabel);
    amIntFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amIntFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    juce::NormalisableRange<double> amFreqRange(0.01, 20000.0);
    amFreqRange.setSkewForCentre(100.0);
    amIntFreqSlider.setNormalisableRange(amFreqRange);
    amIntFreqSlider.setValue(100.0);
    amIntFreqSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    amIntFreqSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    amIntFreqSlider.setTextBoxIsEditable(true);
    amIntFreqSlider.addListener(this);
    addAndMakeVisible(&amIntFreqSlider);
    
    fmEnabledToggle.setButtonText("FM Enable: OFF");
    fmEnabledToggle.addListener(this);
    addAndMakeVisible(&fmEnabledToggle);
    
    fmDevLabel.setText("Deviation", juce::dontSendNotification);
    addAndMakeVisible(&fmDevLabel);
    fmDevSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fmDevSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
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
    
    fmIntFreqLabel.setText("Int Freq", juce::dontSendNotification);
    addAndMakeVisible(&fmIntFreqLabel);
    fmIntFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fmIntFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
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
    
    fskEnabledToggle.setButtonText("FSK Enable: OFF");
    fskEnabledToggle.addListener(this);
    addAndMakeVisible(&fskEnabledToggle);
    
    fskFreqLabel.setText("Hop Freq:", juce::dontSendNotification);
    addAndMakeVisible(&fskFreqLabel);
    fskFreqSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fskFreqSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
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
    fskRateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
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
    
    sweepEnabledToggle.setButtonText("Sweep Enable: OFF");
    sweepEnabledToggle.addListener(this);
    addAndMakeVisible(&sweepEnabledToggle);
    
    sweepStartLabel.setText("Start Freq", juce::dontSendNotification);
    addAndMakeVisible(&sweepStartLabel);
    sweepStartSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sweepStartSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    juce::NormalisableRange<double> sweepStartRange(1.0, 15e6);
    sweepStartRange.setSkewForCentre(1000.0);
    sweepStartSlider.setNormalisableRange(sweepStartRange);
    sweepStartSlider.setValue(1000.0);
    sweepStartSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    sweepStartSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    sweepStartSlider.setTextBoxIsEditable(true);
    sweepStartSlider.addListener(this);
    addAndMakeVisible(&sweepStartSlider);
    
    sweepStopLabel.setText("Stop Freq", juce::dontSendNotification);
    addAndMakeVisible(&sweepStopLabel);
    sweepStopSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sweepStopSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    juce::NormalisableRange<double> sweepStopRange(1.0, 15e6);
    sweepStopRange.setSkewForCentre(10000.0);
    sweepStopSlider.setNormalisableRange(sweepStopRange);
    sweepStopSlider.setValue(10000.0);
    sweepStopSlider.textFromValueFunction = [](double value) { return formatFrequency(value); };
    sweepStopSlider.valueFromTextFunction = [](const juce::String& text) { return parseFrequency(text); };
    sweepStopSlider.setTextBoxIsEditable(true);
    sweepStopSlider.addListener(this);
    addAndMakeVisible(&sweepStopSlider);
    
    sweepTimeLabel.setText("Time", juce::dontSendNotification);
    addAndMakeVisible(&sweepTimeLabel);
    sweepTimeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    sweepTimeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    sweepTimeSlider.setRange(0.001, 3600.0, 0.001);
    sweepTimeSlider.setValue(1.0);
    sweepTimeSlider.setTextValueSuffix(" s");
    sweepTimeSlider.setNumDecimalPlacesToDisplay(3);
    sweepTimeSlider.setTextBoxIsEditable(true);
    sweepTimeSlider.addListener(this);
    addAndMakeVisible(&sweepTimeSlider);
    
    burstEnabledToggle.setButtonText("Burst Enable: OFF");
    burstEnabledToggle.addListener(this);
    addAndMakeVisible(&burstEnabledToggle);
    
    burstCyclesLabel.setText("Cycles:", juce::dontSendNotification);
    addAndMakeVisible(&burstCyclesLabel);
    burstCyclesSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    burstCyclesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    burstCyclesSlider.setRange(1.0, 50000.0, 1.0);
    burstCyclesSlider.setValue(1.0);
    burstCyclesSlider.setTextValueSuffix("");
    burstCyclesSlider.setNumDecimalPlacesToDisplay(0);
    burstCyclesSlider.setTextBoxIsEditable(true);
    burstCyclesSlider.addListener(this);
    addAndMakeVisible(&burstCyclesSlider);
    
    burstPhaseLabel.setText("Phase", juce::dontSendNotification);
    addAndMakeVisible(&burstPhaseLabel);
    burstPhaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    burstPhaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    burstPhaseSlider.setRange(-360.0, 360.0, 1.0);
    burstPhaseSlider.setValue(0.0);
    burstPhaseSlider.setTextValueSuffix(" deg");
    burstPhaseSlider.setNumDecimalPlacesToDisplay(1);
    burstPhaseSlider.setTextBoxIsEditable(true);
    burstPhaseSlider.addListener(this);
    addAndMakeVisible(&burstPhaseSlider);
    
    burstIntPeriodLabel.setText("Int Period", juce::dontSendNotification);
    addAndMakeVisible(&burstIntPeriodLabel);
    burstIntPeriodSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    burstIntPeriodSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    burstIntPeriodSlider.setRange(1e-6, 3600.0, 1e-6);
    burstIntPeriodSlider.setValue(0.1);
    burstIntPeriodSlider.setTextValueSuffix(" s");
    burstIntPeriodSlider.setNumDecimalPlacesToDisplay(6);
    burstIntPeriodSlider.setTextBoxIsEditable(true);
    burstIntPeriodSlider.addListener(this);
    addAndMakeVisible(&burstIntPeriodSlider);
    
    burstSourceLabel.setText("Source", juce::dontSendNotification);
    addAndMakeVisible(&burstSourceLabel);
    burstSourceCombo.addItem("INT", 1);
    burstSourceCombo.addItem("EXT", 2);
    burstSourceCombo.setSelectedId(1);
    burstSourceCombo.addListener(this);
    addAndMakeVisible(&burstSourceCombo);
    
    syncEnabledToggle.setButtonText("Sync Enable: OFF");
    syncEnabledToggle.addListener(this);
    addAndMakeVisible(&syncEnabledToggle);
    
    syncPhaseLabel.setText("Sync Phase", juce::dontSendNotification);
    addAndMakeVisible(&syncPhaseLabel);
    syncPhaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    syncPhaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 16);
    syncPhaseSlider.setRange(0.0, 359.999, 0.001);
    syncPhaseSlider.setValue(0.0);
    syncPhaseSlider.setTextValueSuffix(" deg");
    syncPhaseSlider.setNumDecimalPlacesToDisplay(3);
    syncPhaseSlider.setTextBoxIsEditable(true);
    syncPhaseSlider.addListener(this);
    addAndMakeVisible(&syncPhaseSlider);
    
    // Initialize 4 ARB slots (avoid built-in names: USER, VOLATILE, SINC, NEG_RAMP, EXP_RISE, EXP_FALL, CARDIAC)
    const char* defaultNames[] = {"MYARB", "ARB_2", "ARB_3", "CUSTOM"};
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
        arbSlotUIs[i].pointsSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 16);
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
    
    // Create parameter attachments - these expose parameters to DAW for automation
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
    
    // NOTE: waveformCombo does NOT use ComboBoxAttachment because the parameter only has 7 built-in
    // waveforms, but we dynamically add ARBs (IDs 8+). The attachment would force the combo to reset
    // whenever an ARB is selected because the parameter can't represent it. Manual handling in 
    // comboBoxChanged() is used instead.
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::OUTPUT_ENABLED, outputToggle));
    
    // AM Parameter attachments - visible in DAW automation
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::AM_ENABLED, amEnabledToggle));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::AM_DEPTH, amDepthSlider));
    comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, Parameters::AM_SOURCE, amSourceCombo));
    // NOTE: amIntWaveformCombo does NOT use ComboBoxAttachment - we add ARBs dynamically
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::AM_INT_FREQ, amIntFreqSlider));
    
    // FM Parameter attachments - visible in DAW automation
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::FM_ENABLED, fmEnabledToggle));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::FM_DEVIATION, fmDevSlider));
    comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, Parameters::FM_SOURCE, fmSourceCombo));
    // NOTE: fmIntWaveformCombo does NOT use ComboBoxAttachment - we add ARBs dynamically
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::FM_INT_FREQ, fmIntFreqSlider));
    
    // FSK Parameter attachments - visible in DAW automation
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::FSK_ENABLED, fskEnabledToggle));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::FSK_FREQUENCY, fskFreqSlider));
    comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, Parameters::FSK_SOURCE, fskSourceCombo));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::FSK_RATE, fskRateSlider));
    
    // Sweep Parameter attachments - visible in DAW automation
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::SWEEP_ENABLED, sweepEnabledToggle));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::SWEEP_START, sweepStartSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::SWEEP_STOP, sweepStopSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::SWEEP_TIME, sweepTimeSlider));
    
    // Burst Parameter attachments - visible in DAW automation
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::BURST_ENABLED, burstEnabledToggle));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::BURST_CYCLES, burstCyclesSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::BURST_PHASE, burstPhaseSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::BURST_INT_PERIOD, burstIntPeriodSlider));
    comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, Parameters::BURST_SOURCE, burstSourceCombo));
    
    // Sync Parameter attachments - visible in DAW automation
    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.parameters, Parameters::SYNC_ENABLED, syncEnabledToggle));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::SYNC_PHASE, syncPhaseSlider));
    
    // Trigger Parameter attachments - visible in DAW automation
    comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.parameters, Parameters::TRIGGER_SOURCE, triggerSourceCombo));
    
    // ARB Slot parameter attachments
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT1_POINTS, arbSlotUIs[0].pointsSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT2_POINTS, arbSlotUIs[1].pointsSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT3_POINTS, arbSlotUIs[2].pointsSlider));
    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.parameters, Parameters::ARB_SLOT4_POINTS, arbSlotUIs[3].pointsSlider));
    
    // Connect driver log callback to status window
    // This will display device errors and messages in the UI
    audioProcessor.device.logCallback = [this](const std::string& msg) {
        // Use MessageManager to safely update UI from any thread
        juce::MessageManager::callAsync([this, msg]() {
            appendStatus(juce::String(msg));
        });
    };
}

HP33120APluginAudioProcessorEditor::~HP33120APluginAudioProcessorEditor()
{
    // Clear the log callback to avoid dangling pointer
    audioProcessor.device.logCallback = nullptr;
    
    setLookAndFeel(nullptr);  // Clean up look and feel
}

//==============================================================================
void HP33120APluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Fill background with pure black
    g.fillAll(juce::Colour(0xFF000000));
    
    // Layout Constants (Must match resized())
    const int headerHeight = 32;
    const int statusBarHeight = 20;
    const int messagesHeight = 120;
    const int padding = 8;
    
    const int ctrlHeight = 40;
    const int spacing = 6;
    // const int toggleHeight = 22; // Unused in paint layout calculation currently, simplified
    // const int sectionHeaderPadding = 10; 
    // const int sectionGap = 12;
    
    // We will re-calculate section heights manually here to match resized() perfectly
    // or use the same variables.
    // For simplicity in this non-shared environment, we will define the heights directly.
    
    // ============================================
    // Draw header bar background
    // ============================================
    auto headerArea = juce::Rectangle<int>(0, 0, getWidth(), headerHeight);
    g.setColour(juce::Colour(0xFF0A1F0A));
    g.fillRect(headerArea);
    
    // Header border
    g.setColour(juce::Colour(0xFF4ADE80));
    g.drawLine(0.0f, (float)headerHeight, (float)getWidth(), (float)headerHeight, 2.0f);
    
    // Draw HP33120A title in header
    g.setColour(juce::Colour(0xFFFACC15));
    g.setFont(juce::Font(16.0f).boldened());
    g.drawText(">_ HP33120A Function Generator", 
               headerArea.reduced(padding, 0), 
               juce::Justification::centredLeft);
    
    // ============================================
    // Draw status bar at bottom
    // ============================================
    auto statusBarArea = juce::Rectangle<int>(0, getHeight() - statusBarHeight, getWidth(), statusBarHeight);
    g.setColour(juce::Colour(0xFF0A1F0A));
    g.fillRect(statusBarArea);
    
    g.setColour(juce::Colour(0xFF4ADE80));
    g.drawLine(0.0f, (float)(getHeight() - statusBarHeight), (float)getWidth(), (float)(getHeight() - statusBarHeight), 1.0f);
    
    // Status text
    g.setColour(juce::Colour(0xFF6B7280));
    g.setFont(juce::Font(11.0f));
    g.drawText("hp33120a.conf | NORMAL MODE", 
               statusBarArea.reduced(110, 0),
               juce::Justification::centredLeft);
    
    juce::String statusRight = juce::String(statusMessages.size()) + " messages";
    g.drawText(statusRight, 
               statusBarArea.reduced(padding, 0),
               juce::Justification::centredRight);
    
    // ============================================
    // Draw messages area separator
    // ============================================
    int messagesTop = getHeight() - statusBarHeight - messagesHeight;
    g.setColour(juce::Colour(0xFF4ADE80));
    g.drawLine(0.0f, (float)messagesTop, (float)getWidth(), (float)messagesTop, 2.0f);
    
    // ============================================
    // Helper lambda to draw section box
    // ============================================
    auto drawSectionBox = [&](juce::Rectangle<int> bounds, const juce::String& title) {
        g.setColour(juce::Colour(0xFFFACC15));
        g.drawRect(bounds, 1);
        
        g.setFont(juce::Font(10.0f).boldened());
        int textWidth = g.getCurrentFont().getStringWidth("■ " + title) + 6;
        auto titleBounds = juce::Rectangle<int>(bounds.getX() + 6, bounds.getY() - 5, textWidth, 10);
        g.setColour(juce::Colour(0xFF000000));
        g.fillRect(titleBounds);
        g.setColour(juce::Colour(0xFFFACC15));
        g.drawText("■ " + title, titleBounds, juce::Justification::centredLeft);
    };
    
    // ============================================
    // Calculate section bounds (Using Stack Logic matching resized)
    // ============================================
    // Constants from resized()
    const int toggleHeight = 22;
    const int sectionHeaderPadding = 10;
    const int sectionGap = 12;
    
    // Main content area
    auto mainArea = juce::Rectangle<int>(0, headerHeight, getWidth(), messagesTop - headerHeight);
    
    // Margins (must match resized)
    mainArea.removeFromTop(8); 
    mainArea.removeFromBottom(4);
    
    int columnWidth = getWidth() / 3;
    
    // --- Left Column ---
    // We create a fresh copy of rect for each column to match removeFromLeft logic if we were using a single main rect
    // But resized() constructs per-column rects. Let's do the same.
    // Actually, resized() splits a main rect.
    auto mainColumns = mainArea;
    
    // Left
    auto leftCol = mainColumns.removeFromLeft(columnWidth).reduced(padding, 0);
    
    // SIGNAL GENERATOR: 1 Combo + 5 Sliders
    int sigGenH = sectionHeaderPadding * 2 + (ctrlHeight + spacing) * 6;
    drawSectionBox(leftCol.removeFromTop(sigGenH), "SIGNAL GENERATOR");
    
    leftCol.removeFromTop(sectionGap);
    
    // SYNC OUTPUT: 1 Toggle + 1 Slider
    int syncH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing);
    drawSectionBox(leftCol.removeFromTop(syncH), "SYNC OUTPUT");
    
    leftCol.removeFromTop(sectionGap);
    
    // ARB WAVEFORMS: 4 Slots
    int arbSlotH = 85; 
    int arbH = sectionHeaderPadding * 2 + (arbSlotH + 8) * 4; 
    drawSectionBox(leftCol.removeFromTop(arbH), "ARBITRARY WAVEFORMS");
    
    // --- Middle Column ---
    auto midCol = mainColumns.removeFromLeft(columnWidth).reduced(padding, 0);
    
    // AM MODULATION
    int amH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 4;
    drawSectionBox(midCol.removeFromTop(amH), "AM MODULATION");
    
    midCol.removeFromTop(sectionGap);
    
    // FM MODULATION
    int fmH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 4;
    drawSectionBox(midCol.removeFromTop(fmH), "FM MODULATION");
    
    midCol.removeFromTop(sectionGap);
    
    // FSK MODULATION
    int fskH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 1;
    drawSectionBox(midCol.removeFromTop(fskH), "FSK MODULATION");
    
    // --- Right Column ---
    auto rightCol = mainColumns.removeFromLeft(columnWidth).reduced(padding, 0);
    
    // SWEEP
    int sweepH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 3;
    drawSectionBox(rightCol.removeFromTop(sweepH), "SWEEP");
    
    rightCol.removeFromTop(sectionGap);
    
    // BURST
    int burstH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 4;
    drawSectionBox(rightCol.removeFromTop(burstH), "BURST");
    
    // Column dividers
    int mainTop = headerHeight + 8;
    g.setColour(juce::Colour(0xFF4ADE80).withAlpha(0.3f));
    g.drawLine((float)columnWidth, (float)mainTop, (float)columnWidth, (float)messagesTop - 4, 1.0f);
    g.drawLine((float)(columnWidth * 2), (float)mainTop, (float)(columnWidth * 2), (float)messagesTop - 4, 1.0f);
}

void HP33120APluginAudioProcessorEditor::resized()
{
    // Layout constants
    const int headerHeight = 32;
    const int statusBarHeight = 20;
    const int messagesHeight = 120;
    const int padding = 8;
    
    const int ctrlHeight = 40;  // Height for Slider/Combo "block"
    const int spacing = 6;
    const int toggleHeight = 22;
    const int sectionHeaderPadding = 10;
    const int sectionGap = 12;
    
    auto area = getLocalBounds();
    
    // Hide group components
    basicGroup.setVisible(false);
    amfmGroup.setVisible(false);
    fskGroup.setVisible(false);
    advGroup.setVisible(false);
    
    // ============================================
    // HEADER BAR
    // ============================================
    auto headerArea = area.removeFromTop(headerHeight);
    
    // Right side: Connection controls
    auto connRight = headerArea.removeFromRight(450); 
    connectButton.setBounds(connRight.removeFromLeft(90).reduced(2));
    disconnectButton.setBounds(connRight.removeFromLeft(100).reduced(2));
    gpibAddressEditor.setBounds(connRight.removeFromLeft(140).reduced(2));
    midiStatusLabel.setBounds(connRight.reduced(2));
    
    // IDN label - Moved further right to avoid title overlap
    idnLabel.setBounds(headerArea.withTrimmedLeft(350).reduced(2)); 
    gpibAddressLabel.setBounds(0, 0, 0, 0);
    
    // ============================================
    // STATUS & MESSAGES (Bottom)
    // ============================================
    auto statusBarArea = area.removeFromBottom(statusBarHeight);
    outputToggle.setBounds(statusBarArea.removeFromLeft(100).reduced(2));
    
    auto messagesArea = area.removeFromBottom(messagesHeight);
    statusBox.setBounds(messagesArea.reduced(padding, 20).withTrimmedTop(4));
    
    // ============================================
    // MAIN COLUMNS
    // ============================================
    // area now contains the middle main section
    area.removeFromTop(8); // Top margin
    area.removeFromBottom(4); // Bottom margin above messages
    
    int columnWidth = getWidth() / 3; // Use absolute width for columns
    // We use a separate rectangle for columns to ensure alignment
    auto mainColumns = juce::Rectangle<int>(0, headerHeight + 8, getWidth(), messagesArea.getY() - (headerHeight + 8) - 4);
    
    // Helpers
    auto layoutSlider = [&](juce::Label& label, juce::Slider& slider, juce::Rectangle<int>& container) {
        auto row = container.removeFromTop(ctrlHeight);
        label.setBounds(row.removeFromTop(14));
        slider.setBounds(row);
        container.removeFromTop(spacing);
    };
    
    auto layoutCombo = [&](juce::Label& label, juce::ComboBox& combo, juce::Rectangle<int>& container) {
        auto row = container.removeFromTop(ctrlHeight);
        label.setBounds(row.removeFromTop(14));
        combo.setBounds(row.removeFromTop(22)); // Combo height
        container.removeFromTop(spacing);
    };
    
    auto layoutToggle = [&](juce::ToggleButton& toggle, juce::Rectangle<int>& container) {
        toggle.setBounds(container.removeFromTop(toggleHeight));
        container.removeFromTop(spacing);
    };
    
    // --- LEFT COLUMN ---
    auto leftCol = mainColumns.removeFromLeft(columnWidth).reduced(padding, 0);
    
    // SIGNAL GENERATOR
    // Height match paint(): sectionHeaderPadding*2 + (ctrlHeight+spacing)*6
    int sigGenH = sectionHeaderPadding * 2 + (ctrlHeight + spacing) * 6;
    auto sigGenArea = leftCol.removeFromTop(sigGenH);
    sigGenArea.removeFromTop(sectionHeaderPadding); // Inner padding
    sigGenArea.removeFromBottom(sectionHeaderPadding);
    
    layoutCombo(waveformLabel, waveformCombo, sigGenArea);
    layoutSlider(frequencyLabel, frequencySlider, sigGenArea);
    layoutSlider(amplitudeLabel, amplitudeSlider, sigGenArea);
    layoutSlider(offsetLabel, offsetSlider, sigGenArea);
    layoutSlider(phaseLabel, phaseSlider, sigGenArea);
    layoutSlider(dutyCycleLabel, dutyCycleSlider, sigGenArea);
    
    leftCol.removeFromTop(sectionGap);
    
    // SYNC OUTPUT
    int syncH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing);
    auto syncArea = leftCol.removeFromTop(syncH);
    syncArea.removeFromTop(sectionHeaderPadding);
    syncArea.removeFromBottom(sectionHeaderPadding);
    
    layoutToggle(syncEnabledToggle, syncArea);
    layoutSlider(syncPhaseLabel, syncPhaseSlider, syncArea);
    
    leftCol.removeFromTop(sectionGap);
    
    // ARB WAVEFORMS
    int arbSlotH = 85; 
    int arbH = sectionHeaderPadding * 2 + (arbSlotH + 8) * 4; 
    auto arbArea = leftCol.removeFromTop(arbH);
    arbArea.removeFromTop(sectionHeaderPadding);
    
    for (int i = 0; i < 4; ++i)
    {
        auto slotArea = arbArea.removeFromTop(arbSlotH);
        arbArea.removeFromTop(8); // Spacing between slots
        
        arbSlotUIs[i].bounds = slotArea;
        
        // Row 1: Name (100px) | Points Label (50px) | Points Slider (Rest)
        auto row1 = slotArea.removeFromTop(26);
        arbSlotUIs[i].nameEditor.setBounds(row1.removeFromLeft(100).reduced(1));
        
        // Points sub-section
        auto pointsArea = row1; // Remaining width
        arbSlotUIs[i].pointsLabel.setBounds(pointsArea.removeFromLeft(50).reduced(0, 4)); // Center vertically roughly
        arbSlotUIs[i].pointsSlider.setBounds(pointsArea.reduced(1));
        
        slotArea.removeFromTop(2);
        
        // Row 2: Buttons
        auto row2 = slotArea.removeFromTop(26);
        int btnW = row2.getWidth() / 3;
        arbSlotUIs[i].loadButton.setBounds(row2.removeFromLeft(btnW).reduced(1));
        arbSlotUIs[i].uploadButton.setBounds(row2.removeFromLeft(btnW).reduced(1));
        arbSlotUIs[i].deleteButton.setBounds(row2.reduced(1));
        
        slotArea.removeFromTop(2);
        
        // Row 3: Status / File
        auto row3 = slotArea.removeFromTop(20);
        arbSlotUIs[i].statusLabel.setBounds(row3.removeFromRight(120).reduced(1));
        arbSlotUIs[i].fileNameLabel.setBounds(row3.reduced(1));
    }
    
    // --- MIDDLE COLUMN ---
    auto midCol = mainColumns.removeFromLeft(columnWidth).reduced(padding, 0);
    
    // AM MODULATION
    int amH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 4;
    auto amArea = midCol.removeFromTop(amH);
    amArea.removeFromTop(sectionHeaderPadding);
    amArea.removeFromBottom(sectionHeaderPadding);
    
    layoutToggle(amEnabledToggle, amArea);
    layoutSlider(amDepthLabel, amDepthSlider, amArea);
    layoutCombo(amSourceLabel, amSourceCombo, amArea);
    layoutCombo(amIntWaveformLabel, amIntWaveformCombo, amArea);
    layoutSlider(amIntFreqLabel, amIntFreqSlider, amArea);
    
    midCol.removeFromTop(sectionGap);
    
    // FM MODULATION
    int fmH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 4;
    auto fmArea = midCol.removeFromTop(fmH);
    fmArea.removeFromTop(sectionHeaderPadding);
    fmArea.removeFromBottom(sectionHeaderPadding);
    
    layoutToggle(fmEnabledToggle, fmArea);
    layoutSlider(fmDevLabel, fmDevSlider, fmArea);
    layoutCombo(fmSourceLabel, fmSourceCombo, fmArea);
    layoutCombo(fmIntWaveformLabel, fmIntWaveformCombo, fmArea);
    layoutSlider(fmIntFreqLabel, fmIntFreqSlider, fmArea);
    
    midCol.removeFromTop(sectionGap);
    
    // FSK MODULATION
    int fskH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 1;
    auto fskArea = midCol.removeFromTop(fskH);
    fskArea.removeFromTop(sectionHeaderPadding);
    fskArea.removeFromBottom(sectionHeaderPadding);
    
    layoutToggle(fskEnabledToggle, fskArea);
    layoutSlider(fskFreqLabel, fskFreqSlider, fskArea);
    
    // Hide unused
    fskSourceLabel.setBounds(0, 0, 0, 0); fskSourceCombo.setBounds(0, 0, 0, 0);
    fskRateLabel.setBounds(0, 0, 0, 0); fskRateSlider.setBounds(0, 0, 0, 0);
    
    // --- RIGHT COLUMN ---
    auto rightCol = mainColumns.removeFromLeft(columnWidth).reduced(padding, 0);
    
    // SWEEP
    int sweepH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 3;
    auto sweepArea = rightCol.removeFromTop(sweepH);
    sweepArea.removeFromTop(sectionHeaderPadding);
    sweepArea.removeFromBottom(sectionHeaderPadding);
    
    layoutToggle(sweepEnabledToggle, sweepArea);
    layoutSlider(sweepStartLabel, sweepStartSlider, sweepArea);
    layoutSlider(sweepStopLabel, sweepStopSlider, sweepArea);
    layoutSlider(sweepTimeLabel, sweepTimeSlider, sweepArea);
    
    rightCol.removeFromTop(sectionGap);
    
    // BURST
    int burstH = sectionHeaderPadding * 2 + (toggleHeight + spacing) + (ctrlHeight + spacing) * 4;
    auto burstArea = rightCol.removeFromTop(burstH);
    burstArea.removeFromTop(sectionHeaderPadding);
    burstArea.removeFromBottom(sectionHeaderPadding);
    
    layoutToggle(burstEnabledToggle, burstArea);
    layoutSlider(burstCyclesLabel, burstCyclesSlider, burstArea);
    layoutSlider(burstPhaseLabel, burstPhaseSlider, burstArea);
    layoutCombo(burstSourceLabel, burstSourceCombo, burstArea);
    layoutSlider(burstIntPeriodLabel, burstIntPeriodSlider, burstArea);
    
    // Hide unused
    triggerSourceLabel.setBounds(0, 0, 0, 0); triggerSourceCombo.setBounds(0, 0, 0, 0);
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
            // HP33120A: FUNC:USER <name> only selects which ARB is active, doesn't change main waveform shape
            // This allows using a specific ARB for AM modulation while keeping a different carrier (e.g., SIN)
            int selectedId = amIntWaveformCombo.getSelectedId();
            if (selectedId >= 1 && selectedId <= 6)
            {
                juce::StringArray waves = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "USER"};
                int idx = selectedId - 1;
                if (idx >= 0 && idx < waves.size())
                {
                    device.setAMInternalWaveform(waves[idx].toStdString());
                    if (idx == 5) // USER
                        appendStatus("AM modulation: Using currently active ARB");
                }
            }
            else if (selectedId >= 7)
            {
                // ARB waveform selected - first select which ARB, then set AM internal to USER
                juce::String arbName = amIntWaveformCombo.getItemText(amIntWaveformCombo.getSelectedItemIndex());
                if (!arbName.isEmpty())
                {
                    // Check if main waveform is set to USER/ARB - in that case this will also change the main output
                    int mainWaveform = waveformCombo.getSelectedId();
                    if (mainWaveform >= 7)  // USER or ARB
                    {
                        appendStatus("Warning: Main waveform is ARB - this will change the main output too!");
                    }
                    
                    device.selectUserWaveform(arbName.toStdString());  // This does NOT change main waveform shape
                    device.setAMInternalWaveform("USER");
                    appendStatus("AM modulation: Using ARB '" + arbName + "'");
                }
            }
        }
        else if (comboBox == &fmIntWaveformCombo)
        {
            // HP33120A: FUNC:USER <name> only selects which ARB is active, doesn't change main waveform shape
            // This allows using a specific ARB for FM modulation while keeping a different carrier (e.g., SIN)
            int selectedId = fmIntWaveformCombo.getSelectedId();
            if (selectedId >= 1 && selectedId <= 6)
            {
                juce::StringArray waves = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "USER"};
                int idx = selectedId - 1;
                if (idx >= 0 && idx < waves.size())
                {
                    device.setFMInternalWaveform(waves[idx].toStdString());
                    if (idx == 5) // USER
                        appendStatus("FM modulation: Using currently active ARB");
                }
            }
            else if (selectedId >= 7)
            {
                // ARB waveform selected - first select which ARB, then set FM internal to USER
                juce::String arbName = fmIntWaveformCombo.getItemText(fmIntWaveformCombo.getSelectedItemIndex());
                if (!arbName.isEmpty())
                {
                    // Check if main waveform is set to USER/ARB - in that case this will also change the main output
                    int mainWaveform = waveformCombo.getSelectedId();
                    if (mainWaveform >= 7)  // USER or ARB
                    {
                        appendStatus("Warning: Main waveform is ARB - this will change the main output too!");
                    }
                    
                    device.selectUserWaveform(arbName.toStdString());  // This does NOT change main waveform shape
                    device.setFMInternalWaveform("USER");
                    appendStatus("FM modulation: Using ARB '" + arbName + "'");
                }
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
    
    // Get the background command thread for non-blocking updates
    auto* cmdThread = audioProcessor.getDeviceCommandThread();
    if (!cmdThread) return;
    
    // PERFORMANCE: Queue updates to background thread for INSTANT UI response
    // The slider updates immediately, background thread sends to device asynchronously
    // This eliminates ALL lag from device communication
    
    if (slider == &frequencySlider) 
    {
        cmdThread->queueFrequencyUpdate(frequencySlider.getValue());
    }
    else if (slider == &amplitudeSlider) 
    {
        cmdThread->queueAmplitudeUpdate(amplitudeSlider.getValue());
    }
    else if (slider == &offsetSlider) 
    {
        cmdThread->queueOffsetUpdate(offsetSlider.getValue());
    }
    else if (slider == &phaseSlider) 
    {
        cmdThread->queuePhaseUpdate(phaseSlider.getValue());
    }
    else if (slider == &dutyCycleSlider) 
    {
        cmdThread->queueDutyCycleUpdate(dutyCycleSlider.getValue());
    }
    else
    {
        // For other sliders, use direct call but on a async thread to not block UI
        // These are less frequently changed so a minor delay is acceptable
        juce::MessageManager::callAsync([this, slider]() {
            if (!audioProcessor.isDeviceConnected()) return;
            HP33120ADriver& device = audioProcessor.getDevice();
            try
            {
                if (slider == &amDepthSlider) device.setAMDepth(amDepthSlider.getValue());
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
            catch (...) { }
        });
    }
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
    
    // NO THROTTLING needed - all updates go to background thread and are non-blocking
    // The UI updates instantly, the background thread handles device communication
    
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
    
    // Collect all ARB names from slots
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
    
    // Clear and rebuild the combo box to avoid stale/duplicate entries
    // Save built-in items
    waveformCombo.clear(juce::dontSendNotification);
    
    // Re-add built-in waveforms
    waveformCombo.addItem("SIN", 1);
    waveformCombo.addItem("SQU", 2);
    waveformCombo.addItem("TRI", 3);
    waveformCombo.addItem("RAMP", 4);
    waveformCombo.addItem("NOIS", 5);
    waveformCombo.addItem("DC", 6);
    waveformCombo.addItem("USER", 7);
    
    // Add ARB waveforms from slots (IDs 8-11)
    for (int i = 0; i < arbNames.size(); ++i)
    {
        waveformCombo.addItem(arbNames[i], 8 + i);
    }
    
    // Restore selection if it's still valid
    if (waveformCombo.indexOfItemId(currentWaveformSelection) >= 0)
    {
        waveformCombo.setSelectedId(currentWaveformSelection, juce::dontSendNotification);
    }
    else
    {
        waveformCombo.setSelectedId(1, juce::dontSendNotification);  // Default to SIN
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
        // Store current selection TEXT (not just ID, since IDs can shift)
        juce::String currentWaveformText = waveformCombo.getText();
        int currentWaveformSelection = waveformCombo.getSelectedId();
        
        // Completely rebuild the combo box to avoid stale/duplicate entries
        waveformCombo.clear(juce::dontSendNotification);
        
        // Re-add built-in waveforms (IDs 1-7)
        waveformCombo.addItem("SIN", 1);
        waveformCombo.addItem("SQU", 2);
        waveformCombo.addItem("TRI", 3);
        waveformCombo.addItem("RAMP", 4);
        waveformCombo.addItem("NOIS", 5);
        waveformCombo.addItem("DC", 6);
        waveformCombo.addItem("USER", 7);
        
        // Add ARB waveforms from device catalog (IDs 8+)
        int nextID = 8;
        for (const auto& arb : allARBs)
        {
            waveformCombo.addItem(arb, nextID++);
        }
        
        // Restore selection: first try to find by text, then by original ID
        bool selectionRestored = false;
        if (!currentWaveformText.isEmpty())
        {
            // Find item by text (handles renamed/reordered items)
            for (int i = 0; i < waveformCombo.getNumItems(); ++i)
            {
                if (waveformCombo.getItemText(i).compareIgnoreCase(currentWaveformText) == 0)
                {
                    waveformCombo.setSelectedItemIndex(i, juce::dontSendNotification);
                    selectionRestored = true;
                    break;
                }
            }
        }
        
        // If text match failed, try original ID (for built-in waveforms)
        if (!selectionRestored && waveformCombo.indexOfItemId(currentWaveformSelection) >= 0)
        {
            waveformCombo.setSelectedId(currentWaveformSelection, juce::dontSendNotification);
            selectionRestored = true;
        }
        
        // If still no match, default to SIN
        if (!selectionRestored)
        {
            waveformCombo.setSelectedId(1, juce::dontSendNotification);
        }
        
        // Update AM/FM internal waveform combo boxes
        // NOTE: We do NOT add ARBs here because the HP33120A shares the USER waveform
        // between carrier and modulating signal. Selecting "USER" in AM/FM uses whatever
        // ARB is currently set as the main waveform.
        for (auto* combo : {&amIntWaveformCombo, &fmIntWaveformCombo})
        {
            // Store current selection text (not just ID, since IDs can shift)
            juce::String comboCurrentText = combo->getText();
            int comboCurrentSelection = combo->getSelectedId();
            
            // Completely rebuild: clear and re-add
            combo->clear(juce::dontSendNotification);
            
            // Add built-in waveforms (IDs 1-6)
            combo->addItem("SIN", 1);
            combo->addItem("SQU", 2);
            combo->addItem("TRI", 3);
            combo->addItem("RAMP", 4);
            combo->addItem("NOIS", 5);
            combo->addItem("USER", 6);  // Uses currently active ARB
            
            // Add ARB waveforms from device (IDs 7+)
            // This allows selecting specific ARBs for modulation without changing the carrier shape
            int arbID = 7;
            for (const auto& arb : allARBs)
            {
                combo->addItem(arb, arbID++);
            }
            
            // Restore selection: first try by text, then by ID
            bool restored = false;
            if (!comboCurrentText.isEmpty())
            {
                for (int i = 0; i < combo->getNumItems(); ++i)
                {
                    if (combo->getItemText(i).compareIgnoreCase(comboCurrentText) == 0)
                    {
                        combo->setSelectedItemIndex(i, juce::dontSendNotification);
                        restored = true;
                        break;
                    }
                }
            }
            
            if (!restored && combo->indexOfItemId(comboCurrentSelection) >= 0)
            {
                combo->setSelectedId(comboCurrentSelection, juce::dontSendNotification);
            }
            else if (!restored)
            {
                combo->setSelectedId(1, juce::dontSendNotification);  // Default to SIN
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
                    
                    // Give the device 500ms to update its catalog before refreshing
                    juce::Timer::callAfterDelay(500, [this]() {
                        refreshWaveformComboBoxesFromDevice();
                    });
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