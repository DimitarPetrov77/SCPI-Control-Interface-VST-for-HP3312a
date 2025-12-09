#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <functional>
#include "HP33120ADriver.h"

class ARBManager
{
public:
    struct ARBSlot
    {
        juce::String name;
        std::vector<float> originalAudioData;  // Store original for re-resampling
        int targetPointCount = 1024;
        bool hasData = false;
        bool uploadedToDevice = false;
        bool isUploading = false;  // Track upload in progress
        juce::CriticalSection lock;
    };
    
    // Callback type for async upload completion: (slotIndex, success, message)
    using UploadCallback = std::function<void(int, bool, const juce::String&)>;
    
    ARBManager(HP33120ADriver& driver);
    ~ARBManager();
    
    // Audio file loading (WAV/MP3)
    bool loadAudioFile(int slotIndex, const juce::File& file, juce::AudioFormatManager& formatManager);
    
    // Point count control
    void setSlotPointCount(int slotIndex, int pointCount);
    
    // Resampling with anti-aliasing
    std::vector<float> resampleWithAntiAliasing(const std::vector<float>& input, int targetPoints);
    
    // Device operations
    bool uploadSlotToDevice(int slotIndex);  // Synchronous (blocks)
    void uploadSlotToDeviceAsync(int slotIndex, UploadCallback callback = nullptr);  // Asynchronous (non-blocking)
    bool deleteARBFromDevice(const juce::String& name);
    void syncFromDevice();  // Read ARBs from device on plugin load
    
    // Check if upload is in progress
    bool isUploading(int slotIndex) const;
    
    // Accessors
    ARBSlot& getSlot(int index) { return slots[index]; }
    const ARBSlot& getSlot(int index) const { return slots[index]; }
    
private:
    HP33120ADriver& device;
    ARBSlot slots[4];
    
    // Background thread for async uploads
    class UploadThread;
    std::unique_ptr<UploadThread> uploadThread;
    
    // Anti-aliasing filter
    void applyLowPassFilter(std::vector<float>& data, double cutoffRatio);
    void normalize(std::vector<float>& data);
};

