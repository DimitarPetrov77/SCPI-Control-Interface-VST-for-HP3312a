#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ARBManager.h"

// Helper function
static juce::String formatFrequency(double freqHz)
{
    if (freqHz >= 1e6) return juce::String(freqHz / 1e6, 3) + " MHz";
    else if (freqHz >= 1e3) return juce::String(freqHz / 1e3, 3) + " kHz";
    else return juce::String(freqHz, 3) + " Hz";
}

//==============================================================================
HP33120APluginAudioProcessor::HP33120APluginAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
    parameters(*this, nullptr, juce::Identifier("HP33120AParameters"),
        {
            // Connection
            std::make_unique<juce::AudioParameterFloat>(Parameters::GPIB_ADDRESS, "GPIB Address", 0.0f, 30.0f, 10.0f),
            
            // Basic Settings - HP33120A: 100 µHz to 15 MHz (sine/square)
            std::make_unique<juce::AudioParameterChoice>(Parameters::WAVEFORM, "Waveform", 
                juce::StringArray("SIN", "SQU", "TRI", "RAMP", "NOIS", "DC", "USER"), 0),
            std::make_unique<juce::AudioParameterFloat>(Parameters::FREQUENCY, "Frequency", 
                juce::NormalisableRange<float>(0.0001f, 15e6f, 0.0f, 0.25f), 1000.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::AMPLITUDE, "Amplitude", 
                juce::NormalisableRange<float>(0.01f, 10.0f, 0.0f, 0.5f), 1.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::OFFSET, "Offset", 
                juce::NormalisableRange<float>(-5.0f, 5.0f), 0.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::PHASE, "Phase", 
                juce::NormalisableRange<float>(0.0f, 360.0f), 0.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::DUTY_CYCLE, "Duty Cycle", 
                juce::NormalisableRange<float>(0.1f, 99.9f), 50.0f),
            std::make_unique<juce::AudioParameterBool>(Parameters::OUTPUT_ENABLED, "Output Enabled", false),
            
            // AM Parameters - Full DAW automation
            std::make_unique<juce::AudioParameterBool>(Parameters::AM_ENABLED, "AM Enabled", false),
            std::make_unique<juce::AudioParameterFloat>(Parameters::AM_DEPTH, "AM Depth",
                juce::NormalisableRange<float>(0.0f, 120.0f), 50.0f),
            std::make_unique<juce::AudioParameterChoice>(Parameters::AM_SOURCE, "AM Source",
                juce::StringArray("BOTH", "EXT"), 0),
            std::make_unique<juce::AudioParameterChoice>(Parameters::AM_INT_WAVEFORM, "AM Int Waveform",
                juce::StringArray("SIN", "SQU", "TRI", "RAMP", "NOIS", "USER"), 0),
            std::make_unique<juce::AudioParameterFloat>(Parameters::AM_INT_FREQ, "AM Int Frequency",
                juce::NormalisableRange<float>(0.01f, 20000.0f, 0.0f, 0.3f), 100.0f),
            
            // FM Parameters - Full DAW automation
            std::make_unique<juce::AudioParameterBool>(Parameters::FM_ENABLED, "FM Enabled", false),
            std::make_unique<juce::AudioParameterFloat>(Parameters::FM_DEVIATION, "FM Deviation",
                juce::NormalisableRange<float>(0.01f, 7.5e6f, 0.0f, 0.25f), 100.0f),
            std::make_unique<juce::AudioParameterChoice>(Parameters::FM_SOURCE, "FM Source",
                juce::StringArray("INT", "EXT"), 0),
            std::make_unique<juce::AudioParameterChoice>(Parameters::FM_INT_WAVEFORM, "FM Int Waveform",
                juce::StringArray("SIN", "SQU", "TRI", "RAMP", "NOIS", "USER"), 0),
            std::make_unique<juce::AudioParameterFloat>(Parameters::FM_INT_FREQ, "FM Int Frequency",
                juce::NormalisableRange<float>(0.01f, 10000.0f, 0.0f, 0.3f), 10.0f),
            
            // FSK Parameters - Full DAW automation
            std::make_unique<juce::AudioParameterBool>(Parameters::FSK_ENABLED, "FSK Enabled", false),
            std::make_unique<juce::AudioParameterFloat>(Parameters::FSK_FREQUENCY, "FSK Frequency",
                juce::NormalisableRange<float>(0.0001f, 15e6f, 0.0f, 0.25f), 100.0f),
            std::make_unique<juce::AudioParameterChoice>(Parameters::FSK_SOURCE, "FSK Source",
                juce::StringArray("INT", "EXT"), 0),
            std::make_unique<juce::AudioParameterFloat>(Parameters::FSK_RATE, "FSK Rate",
                juce::NormalisableRange<float>(0.01f, 50000.0f, 0.0f, 0.3f), 10.0f),
            
            // Sweep Parameters - Full DAW automation
            std::make_unique<juce::AudioParameterBool>(Parameters::SWEEP_ENABLED, "Sweep Enabled", false),
            std::make_unique<juce::AudioParameterFloat>(Parameters::SWEEP_START, "Sweep Start",
                juce::NormalisableRange<float>(0.0001f, 15e6f, 0.0f, 0.25f), 100.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::SWEEP_STOP, "Sweep Stop",
                juce::NormalisableRange<float>(0.0001f, 15e6f, 0.0f, 0.25f), 10000.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::SWEEP_TIME, "Sweep Time",
                juce::NormalisableRange<float>(0.001f, 3600.0f, 0.0f, 0.3f), 1.0f),
            
            // Burst Parameters - Full DAW automation
            std::make_unique<juce::AudioParameterBool>(Parameters::BURST_ENABLED, "Burst Enabled", false),
            std::make_unique<juce::AudioParameterFloat>(Parameters::BURST_CYCLES, "Burst Cycles",
                juce::NormalisableRange<float>(1.0f, 50000.0f, 1.0f), 1.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::BURST_PHASE, "Burst Phase",
                juce::NormalisableRange<float>(-360.0f, 360.0f), 0.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::BURST_INT_PERIOD, "Burst Int Period",
                juce::NormalisableRange<float>(1e-6f, 3600.0f, 0.0f, 0.3f), 0.1f),
            std::make_unique<juce::AudioParameterChoice>(Parameters::BURST_SOURCE, "Burst Source",
                juce::StringArray("INT", "EXT"), 0),
            
            // Sync Parameters - Full DAW automation
            std::make_unique<juce::AudioParameterBool>(Parameters::SYNC_ENABLED, "Sync Enabled", false),
            std::make_unique<juce::AudioParameterFloat>(Parameters::SYNC_PHASE, "Sync Phase",
                juce::NormalisableRange<float>(0.0f, 360.0f), 0.0f),
            
            // Trigger Parameters
            std::make_unique<juce::AudioParameterChoice>(Parameters::TRIGGER_SOURCE, "Trigger Source",
                juce::StringArray("IMM", "EXT", "BUS"), 0),
            
            // ARB Slot Parameters (4 slots)
            std::make_unique<juce::AudioParameterInt>(Parameters::ARB_SLOT1_POINTS, "ARB Slot 1 Points", 8, 16000, 1024),
            std::make_unique<juce::AudioParameterInt>(Parameters::ARB_SLOT2_POINTS, "ARB Slot 2 Points", 8, 16000, 1024),
            std::make_unique<juce::AudioParameterInt>(Parameters::ARB_SLOT3_POINTS, "ARB Slot 3 Points", 8, 16000, 1024),
            std::make_unique<juce::AudioParameterInt>(Parameters::ARB_SLOT4_POINTS, "ARB Slot 4 Points", 8, 16000, 1024)
        })
{
    // Parameter value references
    waveformParam = parameters.getRawParameterValue(Parameters::WAVEFORM);
    frequencyParam = parameters.getRawParameterValue(Parameters::FREQUENCY);
    amplitudeParam = parameters.getRawParameterValue(Parameters::AMPLITUDE);
    outputEnabledParam = parameters.getRawParameterValue(Parameters::OUTPUT_ENABLED);
    
    // Start background thread for non-blocking device communication
    deviceCommandThread = std::make_unique<DeviceCommandThread>(device);
    deviceCommandThread->startThread();
    
    // Initialize ARB Manager
    arbManager = std::make_unique<ARBManager>(device);
    
    // Add parameter listener to handle automation/LFO changes
    // This enables full DAW automation for ALL device parameters
    parameterListener = std::make_unique<ParameterListener>(*this);
    
    // Basic parameters
    parameters.addParameterListener(Parameters::WAVEFORM, parameterListener.get());
    parameters.addParameterListener(Parameters::FREQUENCY, parameterListener.get());
    parameters.addParameterListener(Parameters::AMPLITUDE, parameterListener.get());
    parameters.addParameterListener(Parameters::OFFSET, parameterListener.get());
    parameters.addParameterListener(Parameters::PHASE, parameterListener.get());
    parameters.addParameterListener(Parameters::DUTY_CYCLE, parameterListener.get());
    parameters.addParameterListener(Parameters::OUTPUT_ENABLED, parameterListener.get());
    
    // AM parameters
    parameters.addParameterListener(Parameters::AM_ENABLED, parameterListener.get());
    parameters.addParameterListener(Parameters::AM_DEPTH, parameterListener.get());
    parameters.addParameterListener(Parameters::AM_SOURCE, parameterListener.get());
    parameters.addParameterListener(Parameters::AM_INT_WAVEFORM, parameterListener.get());
    parameters.addParameterListener(Parameters::AM_INT_FREQ, parameterListener.get());
    
    // FM parameters
    parameters.addParameterListener(Parameters::FM_ENABLED, parameterListener.get());
    parameters.addParameterListener(Parameters::FM_DEVIATION, parameterListener.get());
    parameters.addParameterListener(Parameters::FM_SOURCE, parameterListener.get());
    parameters.addParameterListener(Parameters::FM_INT_WAVEFORM, parameterListener.get());
    parameters.addParameterListener(Parameters::FM_INT_FREQ, parameterListener.get());
    
    // FSK parameters
    parameters.addParameterListener(Parameters::FSK_ENABLED, parameterListener.get());
    parameters.addParameterListener(Parameters::FSK_FREQUENCY, parameterListener.get());
    parameters.addParameterListener(Parameters::FSK_SOURCE, parameterListener.get());
    parameters.addParameterListener(Parameters::FSK_RATE, parameterListener.get());
    
    // Sweep parameters
    parameters.addParameterListener(Parameters::SWEEP_ENABLED, parameterListener.get());
    parameters.addParameterListener(Parameters::SWEEP_START, parameterListener.get());
    parameters.addParameterListener(Parameters::SWEEP_STOP, parameterListener.get());
    parameters.addParameterListener(Parameters::SWEEP_TIME, parameterListener.get());
    
    // Burst parameters
    parameters.addParameterListener(Parameters::BURST_ENABLED, parameterListener.get());
    parameters.addParameterListener(Parameters::BURST_CYCLES, parameterListener.get());
    parameters.addParameterListener(Parameters::BURST_PHASE, parameterListener.get());
    parameters.addParameterListener(Parameters::BURST_INT_PERIOD, parameterListener.get());
    parameters.addParameterListener(Parameters::BURST_SOURCE, parameterListener.get());
    
    // Sync parameters
    parameters.addParameterListener(Parameters::SYNC_ENABLED, parameterListener.get());
    parameters.addParameterListener(Parameters::SYNC_PHASE, parameterListener.get());
    
    // Trigger parameters
    parameters.addParameterListener(Parameters::TRIGGER_SOURCE, parameterListener.get());
}

HP33120APluginAudioProcessor::~HP33120APluginAudioProcessor()
{
    // Stop background thread safely
    if (deviceCommandThread)
    {
        deviceCommandThread->stopThreadSafely();
        deviceCommandThread = nullptr;
    }
}


const juce::String HP33120APluginAudioProcessor::getName() const { return JucePlugin_Name; }
bool HP33120APluginAudioProcessor::acceptsMidi() const 
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}
bool HP33120APluginAudioProcessor::producesMidi() const { return false; }
bool HP33120APluginAudioProcessor::isMidiEffect() const 
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}
double HP33120APluginAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int HP33120APluginAudioProcessor::getNumPrograms() { return 1; }
int HP33120APluginAudioProcessor::getCurrentProgram() { return 0; }
void HP33120APluginAudioProcessor::setCurrentProgram (int) {}
const juce::String HP33120APluginAudioProcessor::getProgramName (int) { return {}; }
void HP33120APluginAudioProcessor::changeProgramName (int, const juce::String&) {}

void HP33120APluginAudioProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Initialize audio format manager for WAV/MP3
    audioFormatManager = std::make_unique<juce::AudioFormatManager>();
    audioFormatManager->registerFormat(new juce::WavAudioFormat(), true);
    
    #if JUCE_USE_MP3AUDIOFORMAT
    audioFormatManager->registerFormat(new juce::MP3AudioFormat(), false);
    #endif
}

void HP33120APluginAudioProcessor::releaseResources()
{
    audioFormatManager = nullptr;
}

bool HP33120APluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // For synths: no input, output only (mono or stereo)
    // Audio comes from hardware, so we just clear the buffer
    if (layouts.getMainInputChannels() > 0)
        return false;  // Synths don't have audio input
    
    if (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono() ||
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo())
        return true;
    
    return false;
}

void HP33120APluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // Update keyboard state for UI
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);
    
    // Process MIDI
    if (midiMessages.getNumEvents() > 0)
    {
        handleMIDI(midiMessages);
    }
    
    buffer.clear();
}

// ==============================================================================
//  CRITICAL FIX: MIDI HANDLING & THREAD SAFETY
// ==============================================================================
void HP33120APluginAudioProcessor::handleMIDI(const juce::MidiBuffer& midiMessages)
{
    if (!device.isConnected())
    {
        // Don't log spam if not connected, just exit
        return;
    }
    
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();
        
        if (message.isNoteOn())
        {
            int note = message.getNoteNumber();
            
            if (message.getVelocity() > 0)
            {
                // 1. Calculate Frequency (A4 = 440Hz)
                double freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                
                // Rounding for display/device cleanliness
                if (freq < 1.0) freq = std::round(freq * 100.0) / 100.0;
                else if (freq < 1e3) freq = std::round(freq * 10.0) / 10.0;
                else freq = std::round(freq);
                
                // 2. Update UI Parameter immediately (fast, non-blocking)
                if (frequencyParam) *frequencyParam = (float)freq;
                
                // 3. Queue device update in background thread (non-blocking, prevents audio thread stalls)
                auto* cmdThread = getDeviceCommandThread();
                if (cmdThread)
                    cmdThread->queueFrequencyUpdate(freq);
                
                // 4. Log to UI (THREAD SAFE FIX)
                // We must perform the callback on the Message Thread, otherwise the App crashes/freezes
                if (midiStatusCallback)
                {
                    juce::String logMsg = "MIDI On: " + juce::String(note) + 
                                          " -> Freq: " + formatFrequency(freq);
                                          
                    juce::MessageManager::callAsync([this, logMsg]() {
                        if (midiStatusCallback) midiStatusCallback(logMsg);
                    });
                }
            }
        }
    }
}

// Device connection wrappers
bool HP33120APluginAudioProcessor::connectDevice(const std::string& resourceName)
{
    if (device.connect(resourceName))
    {
        // Sync ARBs from device on successful connection
        // Note: HP33120A doesn't support querying ARB names, so sync just resets state
        if (arbManager)
        {
            try
            {
                arbManager->syncFromDevice();
            }
            catch (...)
            {
                // Ignore any exceptions from sync - device doesn't support ARB queries
            }
        }
        return true;
    }
    return false;
}
void HP33120APluginAudioProcessor::disconnectDevice() { device.disconnect(); }
std::string HP33120APluginAudioProcessor::getDeviceIDN() { return device.queryIDN(); }

HP33120APluginAudioProcessor::DeviceCommandThread* HP33120APluginAudioProcessor::getDeviceCommandThread()
{
    return deviceCommandThread.get();
}

void HP33120APluginAudioProcessor::updateParameter(const juce::String& paramID, float value)
{
    if (!device.isConnected()) return;
    auto* param = parameters.getParameter(paramID);
    if (param) param->setValueNotifyingHost(value);
}

// State saving
void HP33120APluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void HP33120APluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr && xmlState->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessorEditor* HP33120APluginAudioProcessor::createEditor() { return new HP33120APluginAudioProcessorEditor (*this); }
bool HP33120APluginAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HP33120APluginAudioProcessor(); }

//==============================================================================
// Background Thread Implementation for Non-Blocking Device Communication
//==============================================================================

// Helper function to convert waveform index to SCPI string
static const char* waveformIndexToString(int index)
{
    static const char* waveforms[] = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "DC", "USER"};
    if (index >= 0 && index < 7) return waveforms[index];
    return "SIN";
}

// Helper for AM/FM internal waveforms (no DC option)
static const char* modWaveformIndexToString(int index)
{
    static const char* waveforms[] = {"SIN", "SQU", "TRI", "RAMP", "NOIS", "USER"};
    if (index >= 0 && index < 6) return waveforms[index];
    return "SIN";
}

// Helper for AM source
static const char* amSourceIndexToString(int index)
{
    static const char* sources[] = {"BOTH", "EXT"};
    if (index >= 0 && index < 2) return sources[index];
    return "BOTH";
}

// Helper for FM/FSK source
static const char* fmFskSourceIndexToString(int index)
{
    static const char* sources[] = {"INT", "EXT"};
    if (index >= 0 && index < 2) return sources[index];
    return "INT";
}

// Helper for Burst source
static const char* burstSourceIndexToString(int index)
{
    static const char* sources[] = {"INT", "EXT"};
    if (index >= 0 && index < 2) return sources[index];
    return "INT";
}

// Helper for Trigger source
static const char* triggerSourceIndexToString(int index)
{
    static const char* sources[] = {"IMM", "EXT", "BUS"};
    if (index >= 0 && index < 3) return sources[index];
    return "IMM";
}

void HP33120APluginAudioProcessor::DeviceCommandThread::run()
{
    while (!threadShouldExit())
    {
        // Wait for a command with timeout - allows periodic error checking
        commandPending.wait(100);  // 100ms timeout for responsive error checking
        
        if (!device.isConnected()) continue;
        
        // ==================== BASIC PARAMETERS ====================
        if (hasPendingWaveform.load())
        {
            int wfIndex = pendingWaveform.load();
            hasPendingWaveform = false;
            device.setWaveform(waveformIndexToString(wfIndex));
        }
        
        if (hasPendingOutput.load())
        {
            bool enabled = pendingOutput.load();
            hasPendingOutput = false;
            device.setOutputEnabled(enabled);
        }
        
        if (hasPendingFreq.load())
        {
            double freq = pendingFreq.load();
            hasPendingFreq = false;
            device.setFrequency(freq);
        }
        
        if (hasPendingAmp.load())
        {
            double amp = pendingAmp.load();
            hasPendingAmp = false;
            device.setAmplitude(amp);
        }
        
        if (hasPendingOffset.load())
        {
            double offset = pendingOffset.load();
            hasPendingOffset = false;
            device.setOffset(offset);
        }
        
        if (hasPendingPhase.load())
        {
            double phase = pendingPhase.load();
            hasPendingPhase = false;
            device.setPhase(phase);
        }
        
        if (hasPendingDuty.load())
        {
            double duty = pendingDuty.load();
            hasPendingDuty = false;
            device.setDutyCycle(duty);
        }
        
        // ==================== AM PARAMETERS ====================
        if (hasPendingAMEnabled.load())
        {
            bool enabled = pendingAMEnabled.load();
            hasPendingAMEnabled = false;
            device.setAMEnabled(enabled);
        }
        
        if (hasPendingAMDepth.load())
        {
            double depth = pendingAMDepth.load();
            hasPendingAMDepth = false;
            device.setAMDepth(depth);
        }
        
        if (hasPendingAMSource.load())
        {
            int sourceIdx = pendingAMSource.load();
            hasPendingAMSource = false;
            device.setAMSource(amSourceIndexToString(sourceIdx));
        }
        
        if (hasPendingAMIntWaveform.load())
        {
            int wfIdx = pendingAMIntWaveform.load();
            hasPendingAMIntWaveform = false;
            device.setAMInternalWaveform(modWaveformIndexToString(wfIdx));
        }
        
        if (hasPendingAMIntFreq.load())
        {
            double freq = pendingAMIntFreq.load();
            hasPendingAMIntFreq = false;
            device.setAMInternalFrequency(freq);
        }
        
        // ==================== FM PARAMETERS ====================
        if (hasPendingFMEnabled.load())
        {
            bool enabled = pendingFMEnabled.load();
            hasPendingFMEnabled = false;
            device.setFMEnabled(enabled);
        }
        
        if (hasPendingFMDeviation.load())
        {
            double dev = pendingFMDeviation.load();
            hasPendingFMDeviation = false;
            device.setFMDeviation(dev);
        }
        
        if (hasPendingFMSource.load())
        {
            int sourceIdx = pendingFMSource.load();
            hasPendingFMSource = false;
            device.setFMSource(fmFskSourceIndexToString(sourceIdx));
        }
        
        if (hasPendingFMIntWaveform.load())
        {
            int wfIdx = pendingFMIntWaveform.load();
            hasPendingFMIntWaveform = false;
            device.setFMInternalWaveform(modWaveformIndexToString(wfIdx));
        }
        
        if (hasPendingFMIntFreq.load())
        {
            double freq = pendingFMIntFreq.load();
            hasPendingFMIntFreq = false;
            device.setFMInternalFrequency(freq);
        }
        
        // ==================== FSK PARAMETERS ====================
        if (hasPendingFSKEnabled.load())
        {
            bool enabled = pendingFSKEnabled.load();
            hasPendingFSKEnabled = false;
            device.setFSKEnabled(enabled);
        }
        
        if (hasPendingFSKFrequency.load())
        {
            double freq = pendingFSKFrequency.load();
            hasPendingFSKFrequency = false;
            device.setFSKFrequency(freq);
        }
        
        if (hasPendingFSKSource.load())
        {
            int sourceIdx = pendingFSKSource.load();
            hasPendingFSKSource = false;
            device.setFSKSource(fmFskSourceIndexToString(sourceIdx));
        }
        
        if (hasPendingFSKRate.load())
        {
            double rate = pendingFSKRate.load();
            hasPendingFSKRate = false;
            device.setFSKInternalRate(rate);
        }
        
        // ==================== SWEEP PARAMETERS ====================
        if (hasPendingSweepEnabled.load())
        {
            bool enabled = pendingSweepEnabled.load();
            hasPendingSweepEnabled = false;
            device.setSweepEnabled(enabled);
        }
        
        if (hasPendingSweepStart.load())
        {
            double freq = pendingSweepStart.load();
            hasPendingSweepStart = false;
            device.setSweepStartFreq(freq);
        }
        
        if (hasPendingSweepStop.load())
        {
            double freq = pendingSweepStop.load();
            hasPendingSweepStop = false;
            device.setSweepStopFreq(freq);
        }
        
        if (hasPendingSweepTime.load())
        {
            double time = pendingSweepTime.load();
            hasPendingSweepTime = false;
            device.setSweepTime(time);
        }
        
        // ==================== BURST PARAMETERS ====================
        if (hasPendingBurstEnabled.load())
        {
            bool enabled = pendingBurstEnabled.load();
            hasPendingBurstEnabled = false;
            device.setBurstEnabled(enabled);
        }
        
        if (hasPendingBurstCycles.load())
        {
            int cycles = pendingBurstCycles.load();
            hasPendingBurstCycles = false;
            device.setBurstCycles(cycles);
        }
        
        if (hasPendingBurstPhase.load())
        {
            double phase = pendingBurstPhase.load();
            hasPendingBurstPhase = false;
            device.setBurstPhase(phase);
        }
        
        if (hasPendingBurstIntPeriod.load())
        {
            double period = pendingBurstIntPeriod.load();
            hasPendingBurstIntPeriod = false;
            device.setBurstInternalPeriod(period);
        }
        
        if (hasPendingBurstSource.load())
        {
            int sourceIdx = pendingBurstSource.load();
            hasPendingBurstSource = false;
            device.setBurstSource(burstSourceIndexToString(sourceIdx));
        }
        
        // ==================== SYNC PARAMETERS ====================
        if (hasPendingSyncEnabled.load())
        {
            bool enabled = pendingSyncEnabled.load();
            hasPendingSyncEnabled = false;
            device.setSyncEnabled(enabled);
        }
        
        if (hasPendingSyncPhase.load())
        {
            double phase = pendingSyncPhase.load();
            hasPendingSyncPhase = false;
            device.setSyncPhase(phase);
        }
        
        // ==================== TRIGGER PARAMETERS ====================
        if (hasPendingTriggerSource.load())
        {
            int sourceIdx = pendingTriggerSource.load();
            hasPendingTriggerSource = false;
            device.setTriggerSource(triggerSourceIndexToString(sourceIdx));
        }
        
        // Periodic error checking - catches errors from writeFast() calls
        // This ensures users still see device errors without blocking slider movements
        juce::int64 currentTime = juce::Time::currentTimeMillis();
        if (currentTime - lastErrorCheck >= ERROR_CHECK_INTERVAL_MS)
        {
            lastErrorCheck = currentTime;
            
            // Query device error queue
            std::string error = device.queryError();
            
            // Check if there's a real error (not "+0,No error")
            if (!error.empty() && 
                error.find("+0") == std::string::npos &&
                error.find("No error") == std::string::npos)
            {
                // Log the error via the device's log callback
                if (device.logCallback)
                {
                    device.logCallback("[DEVICE ERROR] " + error);
                }
            }
        }
    }
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFrequencyUpdate(double freq)
{
    // Atomically update the pending frequency (always stores latest value)
    // This ensures that even if multiple updates come in between processing,
    // we always send the most recent value to the device
    pendingFreq = freq;
    hasPendingFreq = true;
    
    // Signal the thread to wake up and process it
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueAmplitudeUpdate(double amp)
{
    pendingAmp = amp;
    hasPendingAmp = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueOffsetUpdate(double offset)
{
    pendingOffset = offset;
    hasPendingOffset = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queuePhaseUpdate(double phase)
{
    pendingPhase = phase;
    hasPendingPhase = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueDutyCycleUpdate(double duty)
{
    pendingDuty = duty;
    hasPendingDuty = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueWaveformUpdate(int waveformIndex)
{
    pendingWaveform = waveformIndex;
    hasPendingWaveform = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueOutputUpdate(bool enabled)
{
    pendingOutput = enabled;
    hasPendingOutput = true;
    commandPending.signal();
}

// ==================== AM QUEUE METHODS ====================
void HP33120APluginAudioProcessor::DeviceCommandThread::queueAMEnabledUpdate(bool enabled)
{
    pendingAMEnabled = enabled;
    hasPendingAMEnabled = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueAMDepthUpdate(double depth)
{
    pendingAMDepth = depth;
    hasPendingAMDepth = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueAMSourceUpdate(int sourceIndex)
{
    pendingAMSource = sourceIndex;
    hasPendingAMSource = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueAMIntWaveformUpdate(int waveformIndex)
{
    pendingAMIntWaveform = waveformIndex;
    hasPendingAMIntWaveform = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueAMIntFreqUpdate(double freq)
{
    pendingAMIntFreq = freq;
    hasPendingAMIntFreq = true;
    commandPending.signal();
}

// ==================== FM QUEUE METHODS ====================
void HP33120APluginAudioProcessor::DeviceCommandThread::queueFMEnabledUpdate(bool enabled)
{
    pendingFMEnabled = enabled;
    hasPendingFMEnabled = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFMDeviationUpdate(double deviation)
{
    pendingFMDeviation = deviation;
    hasPendingFMDeviation = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFMSourceUpdate(int sourceIndex)
{
    pendingFMSource = sourceIndex;
    hasPendingFMSource = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFMIntWaveformUpdate(int waveformIndex)
{
    pendingFMIntWaveform = waveformIndex;
    hasPendingFMIntWaveform = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFMIntFreqUpdate(double freq)
{
    pendingFMIntFreq = freq;
    hasPendingFMIntFreq = true;
    commandPending.signal();
}

// ==================== FSK QUEUE METHODS ====================
void HP33120APluginAudioProcessor::DeviceCommandThread::queueFSKEnabledUpdate(bool enabled)
{
    pendingFSKEnabled = enabled;
    hasPendingFSKEnabled = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFSKFrequencyUpdate(double freq)
{
    pendingFSKFrequency = freq;
    hasPendingFSKFrequency = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFSKSourceUpdate(int sourceIndex)
{
    pendingFSKSource = sourceIndex;
    hasPendingFSKSource = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFSKRateUpdate(double rate)
{
    pendingFSKRate = rate;
    hasPendingFSKRate = true;
    commandPending.signal();
}

// ==================== SWEEP QUEUE METHODS ====================
void HP33120APluginAudioProcessor::DeviceCommandThread::queueSweepEnabledUpdate(bool enabled)
{
    pendingSweepEnabled = enabled;
    hasPendingSweepEnabled = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueSweepStartUpdate(double freq)
{
    pendingSweepStart = freq;
    hasPendingSweepStart = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueSweepStopUpdate(double freq)
{
    pendingSweepStop = freq;
    hasPendingSweepStop = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueSweepTimeUpdate(double time)
{
    pendingSweepTime = time;
    hasPendingSweepTime = true;
    commandPending.signal();
}

// ==================== BURST QUEUE METHODS ====================
void HP33120APluginAudioProcessor::DeviceCommandThread::queueBurstEnabledUpdate(bool enabled)
{
    pendingBurstEnabled = enabled;
    hasPendingBurstEnabled = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueBurstCyclesUpdate(int cycles)
{
    pendingBurstCycles = cycles;
    hasPendingBurstCycles = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueBurstPhaseUpdate(double phase)
{
    pendingBurstPhase = phase;
    hasPendingBurstPhase = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueBurstIntPeriodUpdate(double period)
{
    pendingBurstIntPeriod = period;
    hasPendingBurstIntPeriod = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueBurstSourceUpdate(int sourceIndex)
{
    pendingBurstSource = sourceIndex;
    hasPendingBurstSource = true;
    commandPending.signal();
}

// ==================== SYNC QUEUE METHODS ====================
void HP33120APluginAudioProcessor::DeviceCommandThread::queueSyncEnabledUpdate(bool enabled)
{
    pendingSyncEnabled = enabled;
    hasPendingSyncEnabled = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueSyncPhaseUpdate(double phase)
{
    pendingSyncPhase = phase;
    hasPendingSyncPhase = true;
    commandPending.signal();
}

// ==================== TRIGGER QUEUE METHODS ====================
void HP33120APluginAudioProcessor::DeviceCommandThread::queueTriggerSourceUpdate(int sourceIndex)
{
    pendingTriggerSource = sourceIndex;
    hasPendingTriggerSource = true;
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::stopThreadSafely()
{
    signalThreadShouldExit();
    commandPending.signal();
    stopThread(1000); // Wait up to 1 second for thread to finish
}

//==============================================================================
// Parameter Listener Implementation for Automation/LFO
// Throttles updates to 20 Hz (50ms) like the Python version for smooth operation
// Enables full DAW automation for ALL device parameters
//==============================================================================
void HP33120APluginAudioProcessor::ParameterListener::parameterChanged(const juce::String& parameterID, float newValue)
{
    // PERFORMANCE CRITICAL: This is called from the audio thread for EVERY automation/LFO update
    // Can be called thousands of times per second! Must be extremely lightweight.
    
    // Early exit if device not connected - no work needed
    if (!processor.device.isConnected()) return;
    
    // Get command thread reference once (lightweight check)
    auto* cmdThread = processor.getDeviceCommandThread();
    if (!cmdThread) return; // No thread available, skip
    
    // PERFORMANCE: Use fast time check (milliseconds, not high-resolution)
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    
    // CRITICAL PERFORMANCE OPTIMIZATION: Throttle to 20 Hz (50ms) to prevent message spam
    // Without throttling, automation at 48kHz = 48,000 calls/second = device overload!
    // With throttling: Max 20 calls/second = smooth operation
    // Strategy: Only queue updates at throttled rate, skipping intermediate values
    
    // ==================== BASIC PARAMETERS ====================
    if (parameterID == Parameters::WAVEFORM)
    {
        if (currentTime - lastWaveformUpdate >= UPDATE_INTERVAL_MS)
        {
            lastWaveformUpdate = currentTime;
            cmdThread->queueWaveformUpdate((int)newValue);
        }
    }
    else if (parameterID == Parameters::FREQUENCY)
    {
        if (currentTime - lastFreqUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFreqUpdate = currentTime;
            cmdThread->queueFrequencyUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::AMPLITUDE)
    {
        if (currentTime - lastAmpUpdate >= UPDATE_INTERVAL_MS)
        {
            lastAmpUpdate = currentTime;
            cmdThread->queueAmplitudeUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::OFFSET)
    {
        if (currentTime - lastOffsetUpdate >= UPDATE_INTERVAL_MS)
        {
            lastOffsetUpdate = currentTime;
            cmdThread->queueOffsetUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::PHASE)
    {
        if (currentTime - lastPhaseUpdate >= UPDATE_INTERVAL_MS)
        {
            lastPhaseUpdate = currentTime;
            cmdThread->queuePhaseUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::DUTY_CYCLE)
    {
        if (currentTime - lastDutyUpdate >= UPDATE_INTERVAL_MS)
        {
            lastDutyUpdate = currentTime;
            cmdThread->queueDutyCycleUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::OUTPUT_ENABLED)
    {
        if (currentTime - lastOutputUpdate >= UPDATE_INTERVAL_MS)
        {
            lastOutputUpdate = currentTime;
            cmdThread->queueOutputUpdate(newValue > 0.5f);
        }
    }
    // ==================== AM PARAMETERS ====================
    else if (parameterID == Parameters::AM_ENABLED)
    {
        if (currentTime - lastAMEnabledUpdate >= UPDATE_INTERVAL_MS)
        {
            lastAMEnabledUpdate = currentTime;
            cmdThread->queueAMEnabledUpdate(newValue > 0.5f);
        }
    }
    else if (parameterID == Parameters::AM_DEPTH)
    {
        if (currentTime - lastAMDepthUpdate >= UPDATE_INTERVAL_MS)
        {
            lastAMDepthUpdate = currentTime;
            cmdThread->queueAMDepthUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::AM_SOURCE)
    {
        if (currentTime - lastAMSourceUpdate >= UPDATE_INTERVAL_MS)
        {
            lastAMSourceUpdate = currentTime;
            cmdThread->queueAMSourceUpdate((int)newValue);
        }
    }
    else if (parameterID == Parameters::AM_INT_WAVEFORM)
    {
        if (currentTime - lastAMIntWaveformUpdate >= UPDATE_INTERVAL_MS)
        {
            lastAMIntWaveformUpdate = currentTime;
            cmdThread->queueAMIntWaveformUpdate((int)newValue);
        }
    }
    else if (parameterID == Parameters::AM_INT_FREQ)
    {
        if (currentTime - lastAMIntFreqUpdate >= UPDATE_INTERVAL_MS)
        {
            lastAMIntFreqUpdate = currentTime;
            cmdThread->queueAMIntFreqUpdate((double)newValue);
        }
    }
    // ==================== FM PARAMETERS ====================
    else if (parameterID == Parameters::FM_ENABLED)
    {
        if (currentTime - lastFMEnabledUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFMEnabledUpdate = currentTime;
            cmdThread->queueFMEnabledUpdate(newValue > 0.5f);
        }
    }
    else if (parameterID == Parameters::FM_DEVIATION)
    {
        if (currentTime - lastFMDeviationUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFMDeviationUpdate = currentTime;
            cmdThread->queueFMDeviationUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::FM_SOURCE)
    {
        if (currentTime - lastFMSourceUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFMSourceUpdate = currentTime;
            cmdThread->queueFMSourceUpdate((int)newValue);
        }
    }
    else if (parameterID == Parameters::FM_INT_WAVEFORM)
    {
        if (currentTime - lastFMIntWaveformUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFMIntWaveformUpdate = currentTime;
            cmdThread->queueFMIntWaveformUpdate((int)newValue);
        }
    }
    else if (parameterID == Parameters::FM_INT_FREQ)
    {
        if (currentTime - lastFMIntFreqUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFMIntFreqUpdate = currentTime;
            cmdThread->queueFMIntFreqUpdate((double)newValue);
        }
    }
    // ==================== FSK PARAMETERS ====================
    else if (parameterID == Parameters::FSK_ENABLED)
    {
        if (currentTime - lastFSKEnabledUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFSKEnabledUpdate = currentTime;
            cmdThread->queueFSKEnabledUpdate(newValue > 0.5f);
        }
    }
    else if (parameterID == Parameters::FSK_FREQUENCY)
    {
        if (currentTime - lastFSKFrequencyUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFSKFrequencyUpdate = currentTime;
            cmdThread->queueFSKFrequencyUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::FSK_SOURCE)
    {
        if (currentTime - lastFSKSourceUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFSKSourceUpdate = currentTime;
            cmdThread->queueFSKSourceUpdate((int)newValue);
        }
    }
    else if (parameterID == Parameters::FSK_RATE)
    {
        if (currentTime - lastFSKRateUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFSKRateUpdate = currentTime;
            cmdThread->queueFSKRateUpdate((double)newValue);
        }
    }
    // ==================== SWEEP PARAMETERS ====================
    else if (parameterID == Parameters::SWEEP_ENABLED)
    {
        if (currentTime - lastSweepEnabledUpdate >= UPDATE_INTERVAL_MS)
        {
            lastSweepEnabledUpdate = currentTime;
            cmdThread->queueSweepEnabledUpdate(newValue > 0.5f);
        }
    }
    else if (parameterID == Parameters::SWEEP_START)
    {
        if (currentTime - lastSweepStartUpdate >= UPDATE_INTERVAL_MS)
        {
            lastSweepStartUpdate = currentTime;
            cmdThread->queueSweepStartUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::SWEEP_STOP)
    {
        if (currentTime - lastSweepStopUpdate >= UPDATE_INTERVAL_MS)
        {
            lastSweepStopUpdate = currentTime;
            cmdThread->queueSweepStopUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::SWEEP_TIME)
    {
        if (currentTime - lastSweepTimeUpdate >= UPDATE_INTERVAL_MS)
        {
            lastSweepTimeUpdate = currentTime;
            cmdThread->queueSweepTimeUpdate((double)newValue);
        }
    }
    // ==================== BURST PARAMETERS ====================
    else if (parameterID == Parameters::BURST_ENABLED)
    {
        if (currentTime - lastBurstEnabledUpdate >= UPDATE_INTERVAL_MS)
        {
            lastBurstEnabledUpdate = currentTime;
            cmdThread->queueBurstEnabledUpdate(newValue > 0.5f);
        }
    }
    else if (parameterID == Parameters::BURST_CYCLES)
    {
        if (currentTime - lastBurstCyclesUpdate >= UPDATE_INTERVAL_MS)
        {
            lastBurstCyclesUpdate = currentTime;
            cmdThread->queueBurstCyclesUpdate((int)newValue);
        }
    }
    else if (parameterID == Parameters::BURST_PHASE)
    {
        if (currentTime - lastBurstPhaseUpdate >= UPDATE_INTERVAL_MS)
        {
            lastBurstPhaseUpdate = currentTime;
            cmdThread->queueBurstPhaseUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::BURST_INT_PERIOD)
    {
        if (currentTime - lastBurstIntPeriodUpdate >= UPDATE_INTERVAL_MS)
        {
            lastBurstIntPeriodUpdate = currentTime;
            cmdThread->queueBurstIntPeriodUpdate((double)newValue);
        }
    }
    else if (parameterID == Parameters::BURST_SOURCE)
    {
        if (currentTime - lastBurstSourceUpdate >= UPDATE_INTERVAL_MS)
        {
            lastBurstSourceUpdate = currentTime;
            cmdThread->queueBurstSourceUpdate((int)newValue);
        }
    }
    // ==================== SYNC PARAMETERS ====================
    else if (parameterID == Parameters::SYNC_ENABLED)
    {
        if (currentTime - lastSyncEnabledUpdate >= UPDATE_INTERVAL_MS)
        {
            lastSyncEnabledUpdate = currentTime;
            cmdThread->queueSyncEnabledUpdate(newValue > 0.5f);
        }
    }
    else if (parameterID == Parameters::SYNC_PHASE)
    {
        if (currentTime - lastSyncPhaseUpdate >= UPDATE_INTERVAL_MS)
        {
            lastSyncPhaseUpdate = currentTime;
            cmdThread->queueSyncPhaseUpdate((double)newValue);
        }
    }
    // ==================== TRIGGER PARAMETERS ====================
    else if (parameterID == Parameters::TRIGGER_SOURCE)
    {
        if (currentTime - lastTriggerSourceUpdate >= UPDATE_INTERVAL_MS)
        {
            lastTriggerSourceUpdate = currentTime;
            cmdThread->queueTriggerSourceUpdate((int)newValue);
        }
    }
    // PERFORMANCE: Other parameters are ignored (fast path)
}