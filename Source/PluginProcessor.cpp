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
    parameterListener = std::make_unique<ParameterListener>(*this);
    parameters.addParameterListener(Parameters::FREQUENCY, parameterListener.get());
    parameters.addParameterListener(Parameters::AMPLITUDE, parameterListener.get());
    parameters.addParameterListener(Parameters::OFFSET, parameterListener.get());
    parameters.addParameterListener(Parameters::PHASE, parameterListener.get());
    parameters.addParameterListener(Parameters::DUTY_CYCLE, parameterListener.get());
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
void HP33120APluginAudioProcessor::DeviceCommandThread::run()
{
    while (!threadShouldExit())
    {
        // Wait for a command - process immediately when signaled
        commandPending.wait();
        
        if (!device.isConnected()) continue;
        
        // Process all pending parameter updates
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

void HP33120APluginAudioProcessor::DeviceCommandThread::stopThreadSafely()
{
    signalThreadShouldExit();
    commandPending.signal();
    stopThread(1000); // Wait up to 1 second for thread to finish
}

//==============================================================================
// Parameter Listener Implementation for Automation/LFO
// Throttles updates to 20 Hz (50ms) like the Python version for smooth operation
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
    if (parameterID == Parameters::FREQUENCY)
    {
        if (currentTime - lastFreqUpdate >= UPDATE_INTERVAL_MS)
        {
            lastFreqUpdate = currentTime;
            cmdThread->queueFrequencyUpdate((double)newValue);
        }
        // PERFORMANCE: If throttled, we skip this update (expected - prevents spam)
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
    // PERFORMANCE: No else clause needed - unknown parameters are ignored (fast path)
}