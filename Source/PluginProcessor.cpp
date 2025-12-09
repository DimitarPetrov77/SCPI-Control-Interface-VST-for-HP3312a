#include "PluginProcessor.h"
#include "PluginEditor.h"

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
            // (Parameter definitions omitted for brevity - they are unchanged)
            std::make_unique<juce::AudioParameterFloat>(Parameters::GPIB_ADDRESS, "GPIB Address", 0.0f, 30.0f, 10.0f),
            std::make_unique<juce::AudioParameterChoice>(Parameters::WAVEFORM, "Waveform", 
                juce::StringArray("SIN", "SQU", "TRI", "RAMP", "NOIS", "DC", "USER"), 0),
            std::make_unique<juce::AudioParameterFloat>(Parameters::FREQUENCY, "Frequency", 
                juce::NormalisableRange<float>(1.0f, 20e6f, 0.0f, 0.3f), 1000.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::AMPLITUDE, "Amplitude", 
                juce::NormalisableRange<float>(0.01f, 10.0f), 1.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::OFFSET, "Offset", 
                juce::NormalisableRange<float>(-5.0f, 5.0f), 0.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::PHASE, "Phase", 
                juce::NormalisableRange<float>(0.0f, 360.0f), 0.0f),
            std::make_unique<juce::AudioParameterFloat>(Parameters::DUTY_CYCLE, "Duty Cycle", 
                juce::NormalisableRange<float>(0.1f, 99.9f), 50.0f),
            std::make_unique<juce::AudioParameterBool>(Parameters::OUTPUT_ENABLED, "Output Enabled", false),
            // ... other params ...
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

void HP33120APluginAudioProcessor::prepareToPlay (double, int) {}
void HP33120APluginAudioProcessor::releaseResources() {}

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
                if (deviceCommandThread)
                    deviceCommandThread->queueFrequencyUpdate(freq);
                
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
bool HP33120APluginAudioProcessor::connectDevice(const std::string& resourceName) { return device.connect(resourceName); }
void HP33120APluginAudioProcessor::disconnectDevice() { device.disconnect(); }
std::string HP33120APluginAudioProcessor::getDeviceIDN() { return device.queryIDN(); }

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
        
        // Check if we have a pending frequency update
        if (hasPendingFreq.load())
        {
            double freq = pendingFreq.load();
            hasPendingFreq = false;
            
            // Send to device immediately - let the device handle the rate, not the computer
            // This is in background thread so it won't block audio, but we send as fast as MIDI comes in
            if (device.isConnected())
            {
                device.setFrequency(freq);
            }
        }
    }
}

void HP33120APluginAudioProcessor::DeviceCommandThread::queueFrequencyUpdate(double freq)
{
    // Atomically update the pending frequency
    pendingFreq = freq;
    hasPendingFreq = true;
    
    // Signal the thread to wake up and process it
    commandPending.signal();
}

void HP33120APluginAudioProcessor::DeviceCommandThread::stopThreadSafely()
{
    signalThreadShouldExit();
    commandPending.signal();
    stopThread(1000); // Wait up to 1 second for thread to finish
}