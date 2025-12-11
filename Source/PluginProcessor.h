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
        
        // Basic parameters
        void queueFrequencyUpdate(double freq);
        void queueAmplitudeUpdate(double amp);
        void queueOffsetUpdate(double offset);
        void queuePhaseUpdate(double phase);
        void queueDutyCycleUpdate(double duty);
        void queueWaveformUpdate(int waveformIndex);
        void queueOutputUpdate(bool enabled);
        
        // AM parameters
        void queueAMEnabledUpdate(bool enabled);
        void queueAMDepthUpdate(double depth);
        void queueAMSourceUpdate(int sourceIndex);
        void queueAMIntWaveformUpdate(int waveformIndex);
        void queueAMIntFreqUpdate(double freq);
        
        // FM parameters
        void queueFMEnabledUpdate(bool enabled);
        void queueFMDeviationUpdate(double deviation);
        void queueFMSourceUpdate(int sourceIndex);
        void queueFMIntWaveformUpdate(int waveformIndex);
        void queueFMIntFreqUpdate(double freq);
        
        // FSK parameters
        void queueFSKEnabledUpdate(bool enabled);
        void queueFSKFrequencyUpdate(double freq);
        void queueFSKSourceUpdate(int sourceIndex);
        void queueFSKRateUpdate(double rate);
        
        // Sweep parameters
        void queueSweepEnabledUpdate(bool enabled);
        void queueSweepStartUpdate(double freq);
        void queueSweepStopUpdate(double freq);
        void queueSweepTimeUpdate(double time);
        
        // Burst parameters
        void queueBurstEnabledUpdate(bool enabled);
        void queueBurstCyclesUpdate(int cycles);
        void queueBurstPhaseUpdate(double phase);
        void queueBurstIntPeriodUpdate(double period);
        void queueBurstSourceUpdate(int sourceIndex);
        
        // Sync parameters
        void queueSyncEnabledUpdate(bool enabled);
        void queueSyncPhaseUpdate(double phase);
        
        // Trigger parameters
        void queueTriggerSourceUpdate(int sourceIndex);
        
        void stopThreadSafely();
        
    private:
        HP33120ADriver& device;
        juce::WaitableEvent commandPending;
        
        // Basic parameters
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
        std::atomic<int> pendingWaveform{0};
        std::atomic<bool> hasPendingWaveform{false};
        std::atomic<bool> pendingOutput{false};
        std::atomic<bool> hasPendingOutput{false};
        
        // AM parameters
        std::atomic<bool> pendingAMEnabled{false};
        std::atomic<bool> hasPendingAMEnabled{false};
        std::atomic<double> pendingAMDepth{50.0};
        std::atomic<bool> hasPendingAMDepth{false};
        std::atomic<int> pendingAMSource{0};
        std::atomic<bool> hasPendingAMSource{false};
        std::atomic<int> pendingAMIntWaveform{0};
        std::atomic<bool> hasPendingAMIntWaveform{false};
        std::atomic<double> pendingAMIntFreq{100.0};
        std::atomic<bool> hasPendingAMIntFreq{false};
        
        // FM parameters
        std::atomic<bool> pendingFMEnabled{false};
        std::atomic<bool> hasPendingFMEnabled{false};
        std::atomic<double> pendingFMDeviation{100.0};
        std::atomic<bool> hasPendingFMDeviation{false};
        std::atomic<int> pendingFMSource{0};
        std::atomic<bool> hasPendingFMSource{false};
        std::atomic<int> pendingFMIntWaveform{0};
        std::atomic<bool> hasPendingFMIntWaveform{false};
        std::atomic<double> pendingFMIntFreq{10.0};
        std::atomic<bool> hasPendingFMIntFreq{false};
        
        // FSK parameters
        std::atomic<bool> pendingFSKEnabled{false};
        std::atomic<bool> hasPendingFSKEnabled{false};
        std::atomic<double> pendingFSKFrequency{100.0};
        std::atomic<bool> hasPendingFSKFrequency{false};
        std::atomic<int> pendingFSKSource{0};
        std::atomic<bool> hasPendingFSKSource{false};
        std::atomic<double> pendingFSKRate{10.0};
        std::atomic<bool> hasPendingFSKRate{false};
        
        // Sweep parameters
        std::atomic<bool> pendingSweepEnabled{false};
        std::atomic<bool> hasPendingSweepEnabled{false};
        std::atomic<double> pendingSweepStart{100.0};
        std::atomic<bool> hasPendingSweepStart{false};
        std::atomic<double> pendingSweepStop{10000.0};
        std::atomic<bool> hasPendingSweepStop{false};
        std::atomic<double> pendingSweepTime{1.0};
        std::atomic<bool> hasPendingSweepTime{false};
        
        // Burst parameters
        std::atomic<bool> pendingBurstEnabled{false};
        std::atomic<bool> hasPendingBurstEnabled{false};
        std::atomic<int> pendingBurstCycles{1};
        std::atomic<bool> hasPendingBurstCycles{false};
        std::atomic<double> pendingBurstPhase{0.0};
        std::atomic<bool> hasPendingBurstPhase{false};
        std::atomic<double> pendingBurstIntPeriod{0.1};
        std::atomic<bool> hasPendingBurstIntPeriod{false};
        std::atomic<int> pendingBurstSource{0};
        std::atomic<bool> hasPendingBurstSource{false};
        
        // Sync parameters
        std::atomic<bool> pendingSyncEnabled{false};
        std::atomic<bool> hasPendingSyncEnabled{false};
        std::atomic<double> pendingSyncPhase{0.0};
        std::atomic<bool> hasPendingSyncPhase{false};
        
        // Trigger parameters
        std::atomic<int> pendingTriggerSource{0};
        std::atomic<bool> hasPendingTriggerSource{false};
        
        // Periodic error checking - check every 500ms to catch errors from writeFast()
        juce::int64 lastErrorCheck{0};
        static constexpr int ERROR_CHECK_INTERVAL_MS = 500;
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
        
        // Throttle timestamps for all parameters
        juce::int64 lastFreqUpdate = 0;
        juce::int64 lastAmpUpdate = 0;
        juce::int64 lastOffsetUpdate = 0;
        juce::int64 lastPhaseUpdate = 0;
        juce::int64 lastDutyUpdate = 0;
        juce::int64 lastWaveformUpdate = 0;
        juce::int64 lastOutputUpdate = 0;
        
        // AM
        juce::int64 lastAMEnabledUpdate = 0;
        juce::int64 lastAMDepthUpdate = 0;
        juce::int64 lastAMSourceUpdate = 0;
        juce::int64 lastAMIntWaveformUpdate = 0;
        juce::int64 lastAMIntFreqUpdate = 0;
        
        // FM
        juce::int64 lastFMEnabledUpdate = 0;
        juce::int64 lastFMDeviationUpdate = 0;
        juce::int64 lastFMSourceUpdate = 0;
        juce::int64 lastFMIntWaveformUpdate = 0;
        juce::int64 lastFMIntFreqUpdate = 0;
        
        // FSK
        juce::int64 lastFSKEnabledUpdate = 0;
        juce::int64 lastFSKFrequencyUpdate = 0;
        juce::int64 lastFSKSourceUpdate = 0;
        juce::int64 lastFSKRateUpdate = 0;
        
        // Sweep
        juce::int64 lastSweepEnabledUpdate = 0;
        juce::int64 lastSweepStartUpdate = 0;
        juce::int64 lastSweepStopUpdate = 0;
        juce::int64 lastSweepTimeUpdate = 0;
        
        // Burst
        juce::int64 lastBurstEnabledUpdate = 0;
        juce::int64 lastBurstCyclesUpdate = 0;
        juce::int64 lastBurstPhaseUpdate = 0;
        juce::int64 lastBurstIntPeriodUpdate = 0;
        juce::int64 lastBurstSourceUpdate = 0;
        
        // Sync
        juce::int64 lastSyncEnabledUpdate = 0;
        juce::int64 lastSyncPhaseUpdate = 0;
        
        // Trigger
        juce::int64 lastTriggerSourceUpdate = 0;
        
        static constexpr int UPDATE_INTERVAL_MS = 20; // 50 Hz max update rate for smooth operation
    };
    
    std::unique_ptr<ParameterListener> parameterListener;
    
    std::unique_ptr<DeviceCommandThread> deviceCommandThread;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HP33120APluginAudioProcessor)
};

