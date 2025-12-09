#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "HP33120ADriver.h"
#include "Parameters.h"

//==============================================================================
/**
*/
class HP33120APluginAudioProcessor  : public juce::AudioProcessor
{
    // Forward declaration for nested class (needed for getDeviceCommandThread)
    class DeviceCommandThread;
    
public:
    //==============================================================================
    HP33120APluginAudioProcessor();
    ~HP33120APluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    class Editor;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    //==============================================================================
    // Device connection
    bool connectDevice(const std::string& resourceName = "GPIB0::10::INSTR");
    void disconnectDevice();
    bool isDeviceConnected() const { return device.isConnected(); }
    std::string getDeviceIDN();
    
    // Device control methods
    void updateParameter(const juce::String& paramID, float value);
    
    // Device access
    HP33120ADriver& getDevice() { return device; }
    DeviceCommandThread* getDeviceCommandThread();
    
    // MIDI note to frequency conversion
    static double midiNoteToFrequency(int noteNumber);
    static double velocityToAmplitude(int velocity);
    
    // MIDI status callback (for UI updates)
    std::function<void(const juce::String&)> midiStatusCallback;
    
    // MIDI keyboard state (public for UI access)
    juce::MidiKeyboardState keyboardState;

    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;
    
    HP33120ADriver device;
    
    // ARB Management
    std::unique_ptr<class ARBManager> arbManager;
    std::unique_ptr<juce::AudioFormatManager> audioFormatManager;
    
private:
    
    // Parameter value references
    std::atomic<float>* waveformParam = nullptr;
    std::atomic<float>* frequencyParam = nullptr;
    std::atomic<float>* amplitudeParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* phaseParam = nullptr;
    std::atomic<float>* dutyCycleParam = nullptr;
    std::atomic<float>* outputEnabledParam = nullptr;
    
    std::atomic<float>* amEnabledParam = nullptr;
    std::atomic<float>* amDepthParam = nullptr;
    std::atomic<float>* amSourceParam = nullptr;
    std::atomic<float>* amIntWaveformParam = nullptr;
    std::atomic<float>* amIntFreqParam = nullptr;
    
    std::atomic<float>* fmEnabledParam = nullptr;
    std::atomic<float>* fmDeviationParam = nullptr;
    std::atomic<float>* fmSourceParam = nullptr;
    std::atomic<float>* fmIntWaveformParam = nullptr;
    std::atomic<float>* fmIntFreqParam = nullptr;
    
    std::atomic<float>* fskEnabledParam = nullptr;
    std::atomic<float>* fskFreqParam = nullptr;
    std::atomic<float>* fskSourceParam = nullptr;
    std::atomic<float>* fskRateParam = nullptr;
    
    std::atomic<float>* sweepEnabledParam = nullptr;
    std::atomic<float>* sweepStartParam = nullptr;
    std::atomic<float>* sweepStopParam = nullptr;
    std::atomic<float>* sweepTimeParam = nullptr;
    
    std::atomic<float>* burstEnabledParam = nullptr;
    std::atomic<float>* burstCyclesParam = nullptr;
    std::atomic<float>* burstPhaseParam = nullptr;
    std::atomic<float>* burstIntPeriodParam = nullptr;
    std::atomic<float>* burstSourceParam = nullptr;
    
    std::atomic<float>* syncEnabledParam = nullptr;
    std::atomic<float>* syncPhaseParam = nullptr;
    
    std::atomic<float>* triggerSourceParam = nullptr;
    
    // MIDI handling
    void handleMIDI(const juce::MidiBuffer& midiMessages);
    
    // Parameter update flags
    bool needsParameterUpdate = false;
    
    // Background thread for non-blocking device communication
    class DeviceCommandThread : public juce::Thread
    {
    public:
        DeviceCommandThread(HP33120ADriver& dev) : Thread("DeviceCommandThread"), device(dev) {}
        void run() override;
        void queueFrequencyUpdate(double freq);
        void queueAmplitudeUpdate(double amp);
        void queueOffsetUpdate(double offset);
        void queuePhaseUpdate(double phase);
        void queueDutyCycleUpdate(double duty);
        void stopThreadSafely();
        
    private:
        HP33120ADriver& device;
        juce::WaitableEvent commandPending;
        std::atomic<double> pendingFreq{0.0};
        std::atomic<bool> hasPendingFreq{false};
        std::atomic<double> pendingAmp{0.0};
        std::atomic<bool> hasPendingAmp{false};
        std::atomic<double> pendingOffset{0.0};
        std::atomic<bool> hasPendingOffset{false};
        std::atomic<double> pendingPhase{0.0};
        std::atomic<bool> hasPendingPhase{false};
        std::atomic<double> pendingDuty{0.0};
        std::atomic<bool> hasPendingDuty{false};
    };
    
    // Parameter listener to handle automation/LFO changes
    // Throttles updates to ~20 Hz (50ms) like the Python version for smooth operation
    class ParameterListener : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        ParameterListener(HP33120APluginAudioProcessor& p) : processor(p) {}
        void parameterChanged(const juce::String& parameterID, float newValue) override;
    private:
        HP33120APluginAudioProcessor& processor;
        juce::int64 lastFreqUpdate = 0;
        juce::int64 lastAmpUpdate = 0;
        juce::int64 lastOffsetUpdate = 0;
        juce::int64 lastPhaseUpdate = 0;
        juce::int64 lastDutyUpdate = 0;
        static constexpr int UPDATE_INTERVAL_MS = 50; // 20 Hz max update rate (matches Python version)
    };
    
    std::unique_ptr<ParameterListener> parameterListener;
    
    std::unique_ptr<DeviceCommandThread> deviceCommandThread;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HP33120APluginAudioProcessor)
};

