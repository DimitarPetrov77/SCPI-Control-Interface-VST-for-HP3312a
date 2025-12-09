#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "PluginProcessor.h"
#include "Parameters.h"

//==============================================================================
/**
*/
class HP33120APluginAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                            public juce::Timer,
                                            public juce::Button::Listener,
                                            public juce::ComboBox::Listener,
                                            public juce::Slider::Listener,
                                            public juce::FileDragAndDropTarget
{
public:
    HP33120APluginAudioProcessorEditor (HP33120APluginAudioProcessor&);
    ~HP33120APluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    
    // Button listener
    void buttonClicked(juce::Button* button) override;
    
    // ComboBox listener
    void comboBoxChanged(juce::ComboBox* comboBox) override;
    
    // Slider listener
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragStarted(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;

private:
    HP33120APluginAudioProcessor& audioProcessor;
    
    // Connection UI
    juce::TextButton connectButton;
    juce::TextButton disconnectButton;
    juce::Label gpibAddressLabel;
    juce::TextEditor gpibAddressEditor;
    juce::Label idnLabel;
    
    // Basic Settings
    juce::GroupComponent basicGroup;
    juce::Label waveformLabel;
    juce::ComboBox waveformCombo;
    juce::Slider frequencySlider;
    juce::Label frequencyLabel;
    juce::Slider amplitudeSlider;
    juce::Label amplitudeLabel;
    juce::Slider offsetSlider;
    juce::Label offsetLabel;
    juce::Slider phaseSlider;
    juce::Label phaseLabel;
    juce::Slider dutyCycleSlider;
    juce::Label dutyCycleLabel;
    juce::ToggleButton outputToggle;
    
    // AM/FM Settings
    juce::GroupComponent amfmGroup;
    juce::ToggleButton amEnabledToggle;
    juce::Label amDepthLabel;
    juce::Slider amDepthSlider;
    juce::Label amSourceLabel;
    juce::ComboBox amSourceCombo;
    juce::Label amIntWaveformLabel;
    juce::ComboBox amIntWaveformCombo;
    juce::Label amIntFreqLabel;
    juce::Slider amIntFreqSlider;
    
    juce::ToggleButton fmEnabledToggle;
    juce::Label fmDevLabel;
    juce::Slider fmDevSlider;
    juce::Label fmSourceLabel;
    juce::ComboBox fmSourceCombo;
    juce::Label fmIntWaveformLabel;
    juce::ComboBox fmIntWaveformCombo;
    juce::Label fmIntFreqLabel;
    juce::Slider fmIntFreqSlider;
    
    // FSK Settings
    juce::GroupComponent fskGroup;
    juce::ToggleButton fskEnabledToggle;
    juce::Label fskFreqLabel;
    juce::Slider fskFreqSlider;
    juce::Label fskSourceLabel;
    juce::ComboBox fskSourceCombo;
    juce::Label fskRateLabel;
    juce::Slider fskRateSlider;
    
    // Advanced Settings (Sweep/Burst/Sync/ARB)
    juce::GroupComponent advGroup;
    juce::ToggleButton sweepEnabledToggle;
    juce::Slider sweepStartSlider;
    juce::Label sweepStartLabel;
    juce::Slider sweepStopSlider;
    juce::Label sweepStopLabel;
    juce::Slider sweepTimeSlider;
    juce::Label sweepTimeLabel;
    
    juce::ToggleButton burstEnabledToggle;
    juce::Slider burstCyclesSlider;
    juce::Label burstCyclesLabel;
    juce::Slider burstPhaseSlider;
    juce::Label burstPhaseLabel;
    juce::Slider burstIntPeriodSlider;
    juce::Label burstIntPeriodLabel;
    juce::Label burstSourceLabel;
    juce::ComboBox burstSourceCombo;
    
    juce::ToggleButton syncEnabledToggle;
    juce::Label syncPhaseLabel;
    juce::Slider syncPhaseSlider;
    
    // ARB Slot UI (4 slots)
    struct ARBSlotUI
    {
        juce::TextEditor nameEditor;
        juce::Label pointsLabel;
        juce::Slider pointsSlider;
        juce::TextButton loadButton;
        juce::TextButton uploadButton;
        juce::TextButton deleteButton;
        juce::Label statusLabel;
        juce::Label fileNameLabel;  // Show loaded file name
        
        juce::Rectangle<int> bounds;  // Store bounds for drag-and-drop detection
        int slotIndex = 0;  // 0-3
    };
    
    ARBSlotUI arbSlotUIs[4];
    
    juce::Label triggerSourceLabel;
    juce::ComboBox triggerSourceCombo;
    
    // Status
    juce::TextEditor statusBox;
    juce::Label midiStatusLabel;
    juce::int64 lastMidiActivityTime = 0;
    static constexpr int MAX_STATUS_MESSAGES = 30;  // Keep only last 30 messages
    juce::StringArray statusMessages;  // Circular buffer for status messages
    
    // Parameter attachments
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    
    void updateDeviceParameters();
    void updateSingleParameter(juce::Slider* slider);  // Must be public or accessible
    void appendStatus(const juce::String& message);
    
    // ARB slot management
    void loadAudioFileToSlot(int slotIndex);
    void uploadSlotToDevice(int slotIndex);
    void deleteARBFromDevice(int slotIndex);
    
    // Drag-and-drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    
    bool isUpdatingParameters = false;  // Prevent recursive updates
    juce::int64 lastUpdateTime = 0;  // Throttle updates
    static constexpr int MIN_UPDATE_INTERVAL_MS = 50;  // Minimum time between updates
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HP33120APluginAudioProcessorEditor)
};

