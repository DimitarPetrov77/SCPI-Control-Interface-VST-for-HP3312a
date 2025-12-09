#include "ARBManager.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// Background thread for async ARB uploads
class ARBManager::UploadThread : public juce::Thread
{
public:
    UploadThread(ARBManager& manager, HP33120ADriver& driver)
        : Thread("ARBUploadThread"), arbManager(manager), device(driver)
    {
        startThread();
    }
    
    ~UploadThread() override
    {
        stopThread(5000);  // Wait up to 5 seconds for thread to stop
    }
    
    void queueUpload(int slotIndex, ARBManager::UploadCallback callback)
    {
        juce::ScopedLock sl(queueLock);
        uploadQueue.push({slotIndex, callback});
        notify();  // Wake up thread
    }
    
    void run() override
    {
        while (!threadShouldExit())
        {
            // Wait for work or timeout
            wait(100);
            
            // Process queued uploads
            UploadTask task;
            {
                juce::ScopedLock sl(queueLock);
                if (!uploadQueue.empty())
                {
                    task = uploadQueue.front();
                    uploadQueue.pop();
                }
                else
                {
                    continue;  // No work, continue waiting
                }
            }
            
            // Perform the upload
            bool success = false;
            juce::String message;
            
            try
            {
                // Check device connection on background thread (not UI thread)
                if (!device.isConnected())
                {
                    message = "Device not connected";
                    if (task.callback)
                    {
                        juce::MessageManager::getInstance()->callAsync([task, message]()
                        {
                            task.callback(task.slotIndex, false, message);
                        });
                    }
                    continue;
                }
                
                auto& slot = arbManager.getSlot(task.slotIndex);
                {
                    juce::ScopedLock sl(slot.lock);
                    slot.isUploading = true;
                }
                
                // Resample to target point count
                // Yield control periodically during resampling to keep system responsive
                std::vector<float> resampled = arbManager.resampleWithAntiAliasing(
                    slot.originalAudioData,
                    slot.targetPointCount
                );
                
                // Yield after resampling (in case it was CPU-intensive)
                Thread::yield();
                
                if (!resampled.empty())
                {
                    // Upload to device (this will take time, but it's on background thread)
                    device.downloadARBWaveform(
                        slot.name.toStdString(),
                        resampled,
                        slot.targetPointCount
                    );
                    
                    // Yield after upload
                    Thread::yield();
                    
                    {
                        juce::ScopedLock sl(slot.lock);
                        slot.uploadedToDevice = true;
                        slot.isUploading = false;
                    }
                    
                    success = true;
                    message = "Uploaded " + juce::String(resampled.size()) + " points";
                }
                else
                {
                    {
                        juce::ScopedLock sl(slot.lock);
                        slot.isUploading = false;
                    }
                    message = "Resampling failed";
                }
            }
            catch (const std::exception& e)
            {
                auto& slot = arbManager.getSlot(task.slotIndex);
                {
                    juce::ScopedLock sl(slot.lock);
                    slot.isUploading = false;
                }
                message = "Exception: " + juce::String(e.what());
            }
            catch (...)
            {
                auto& slot = arbManager.getSlot(task.slotIndex);
                {
                    juce::ScopedLock sl(slot.lock);
                    slot.isUploading = false;
                }
                message = "Unknown error";
            }
            
            // Call callback on message thread
            if (task.callback)
            {
                juce::MessageManager::getInstance()->callAsync([task, success, message]()
                {
                    task.callback(task.slotIndex, success, message);
                });
            }
        }
    }
    
private:
    ARBManager& arbManager;
    HP33120ADriver& device;
    
    struct UploadTask
    {
        int slotIndex;
        ARBManager::UploadCallback callback;
    };
    
    juce::CriticalSection queueLock;
    std::queue<UploadTask> uploadQueue;
};

ARBManager::ARBManager(HP33120ADriver& driver)
    : device(driver), uploadThread(std::make_unique<UploadThread>(*this, driver))
{
    // Initialize default slot names
    slots[0].name = "MYARB";
    slots[1].name = "USER";
    slots[2].name = "VOLATILE";
    slots[3].name = "CUSTOM";
}

ARBManager::~ARBManager()
{
    uploadThread.reset();  // Stop thread in destructor
}

bool ARBManager::loadAudioFile(int slotIndex, const juce::File& file, juce::AudioFormatManager& formatManager)
{
    if (slotIndex < 0 || slotIndex >= 4) return false;
    
    const juce::ScopedLock sl(slots[slotIndex].lock);
    
    // Create reader for the file
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (!reader)
    {
        return false;
    }
    
    // Read audio data
    juce::AudioBuffer<float> audioBuffer((int)reader->numChannels, (int)reader->lengthInSamples);
    reader->read(&audioBuffer, 0, (int)reader->lengthInSamples, 0, true, true);
    
    // Convert to mono and store
    slots[slotIndex].originalAudioData.clear();
    slots[slotIndex].originalAudioData.reserve(audioBuffer.getNumSamples());
    
    for (int i = 0; i < audioBuffer.getNumSamples(); ++i)
    {
        float sample = 0.0f;
        if (audioBuffer.getNumChannels() >= 2)
        {
            // Mix stereo to mono
            sample = (audioBuffer.getSample(0, i) + audioBuffer.getSample(1, i)) * 0.5f;
        }
        else if (audioBuffer.getNumChannels() >= 1)
        {
            sample = audioBuffer.getSample(0, i);
        }
        slots[slotIndex].originalAudioData.push_back(sample);
    }
    
    slots[slotIndex].hasData = !slots[slotIndex].originalAudioData.empty();
    slots[slotIndex].uploadedToDevice = false;  // Reset upload status when new file loaded
    
    return slots[slotIndex].hasData;
}

void ARBManager::setSlotPointCount(int slotIndex, int pointCount)
{
    if (slotIndex < 0 || slotIndex >= 4) return;
    if (pointCount < 8 || pointCount > 16000) return;
    
    const juce::ScopedLock sl(slots[slotIndex].lock);
    
    bool wasUploaded = slots[slotIndex].uploadedToDevice;
    slots[slotIndex].targetPointCount = pointCount;
    
    // If slot was uploaded, re-upload with new point count
    if (wasUploaded && slots[slotIndex].hasData)
    {
        uploadSlotToDevice(slotIndex);
    }
}

void ARBManager::applyLowPassFilter(std::vector<float>& data, double cutoffRatio)
{
    if (data.empty()) return;
    
    // IIR biquad low-pass filter coefficients
    // Based on JUCE's ResamplingAudioSource::createLowPass()
    const double n = 1.0 / std::tan(juce::MathConstants<double>::pi * juce::jmax(0.001, cutoffRatio));
    const double nSquared = n * n;
    const double c1 = 1.0 / (1.0 + juce::MathConstants<double>::sqrt2 * n + nSquared);
    
    // Filter coefficients (normalized)
    const double a = 1.0 / 1.0;  // a0 = 1.0
    const double b0 = c1 * a;
    const double b1 = c1 * 2.0 * a;
    const double b2 = c1 * a;
    const double a1 = c1 * 2.0 * (1.0 - nSquared) * a;
    const double a2 = c1 * (1.0 - juce::MathConstants<double>::sqrt2 * n + nSquared) * a;
    
    // Apply filter (direct form II)
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (auto& sample : data)
    {
        double in = sample;
        double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        
        // Denormal protection
        if (std::abs(out) < 1.0e-8) out = 0.0;
        
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = out;
        sample = (float)out;
    }
}

void ARBManager::normalize(std::vector<float>& data)
{
    if (data.empty()) return;
    
    // Find maximum absolute value
    float maxVal = 0.0f;
    for (const auto& val : data)
    {
        maxVal = std::max(maxVal, std::abs(val));
    }
    
    if (maxVal > 1.0f)
    {
        // Scale down if exceeds range
        for (auto& val : data)
        {
            val /= maxVal;
        }
    }
    else
    {
        // Clamp to range (in case of any floating point issues)
        for (auto& val : data)
        {
            val = std::max(-1.0f, std::min(1.0f, val));
        }
    }
}

std::vector<float> ARBManager::resampleWithAntiAliasing(const std::vector<float>& input, int targetPoints)
{
    if (input.empty() || targetPoints < 8) return {};
    
    std::vector<float> output;
    
    if (input.size() == targetPoints)
    {
        output = input;
    }
    else if (input.size() > targetPoints)
    {
        // Downsampling: apply anti-aliasing filter first
        std::vector<float> filtered = input;
        double ratio = (double)targetPoints / input.size();
        double cutoffRatio = 0.5 * ratio;  // Nyquist for target rate
        applyLowPassFilter(filtered, cutoffRatio);
        
        // Linear interpolation resampling
        output.resize(targetPoints);
        if (targetPoints > 1)
        {
            double step = (double)(filtered.size() - 1) / (targetPoints - 1);
            for (int i = 0; i < targetPoints; ++i)
            {
                double srcIndex = i * step;
                int idx0 = (int)srcIndex;
                int idx1 = std::min(idx0 + 1, (int)filtered.size() - 1);
                double frac = srcIndex - idx0;
                output[i] = (float)(filtered[idx0] * (1.0 - frac) + filtered[idx1] * frac);
            }
        }
        else
        {
            output[0] = filtered[0];
        }
    }
    else
    {
        // Upsampling: just interpolate (no filter needed)
        output.resize(targetPoints);
        if (input.size() > 1)
        {
            double step = (double)(input.size() - 1) / (targetPoints - 1);
            for (int i = 0; i < targetPoints; ++i)
            {
                double srcIndex = i * step;
                int idx0 = (int)srcIndex;
                int idx1 = std::min(idx0 + 1, (int)input.size() - 1);
                double frac = srcIndex - idx0;
                output[i] = (float)(input[idx0] * (1.0 - frac) + input[idx1] * frac);
            }
        }
        else
        {
            // Single sample - replicate
            for (int i = 0; i < targetPoints; ++i)
            {
                output[i] = input[0];
            }
        }
    }
    
    // Normalize to [-1, +1]
    normalize(output);
    
    return output;
}

bool ARBManager::uploadSlotToDevice(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= 4) return false;
    if (!device.isConnected()) return false;
    
    try
    {
        const juce::ScopedLock sl(slots[slotIndex].lock);
        
        if (!slots[slotIndex].hasData) return false;
        
        // Resample to target point count
        std::vector<float> resampled = resampleWithAntiAliasing(
            slots[slotIndex].originalAudioData,
            slots[slotIndex].targetPointCount
        );
        
        if (resampled.empty())
        {
            return false;
        }
        
        // HP33120A has 4 ARB memory slots
        // Uploading with the same name will overwrite the existing ARB
        // No need to explicitly delete - the device handles overwriting automatically
        
        // Upload to device (this will overwrite any existing ARB with the same name)
        device.downloadARBWaveform(
            slots[slotIndex].name.toStdString(),
            resampled,
            slots[slotIndex].targetPointCount
        );
        
        slots[slotIndex].uploadedToDevice = true;
        return true;
    }
    catch (const std::exception& e)
    {
        // Error already logged by device driver
        return false;
    }
    catch (...)
    {
        // Unknown error - device driver should have logged it
        return false;
    }
}

void ARBManager::uploadSlotToDeviceAsync(int slotIndex, UploadCallback callback)
{
    if (slotIndex < 0 || slotIndex >= 4)
    {
        if (callback)
        {
            // Call callback asynchronously to avoid blocking UI thread
            juce::MessageManager::getInstance()->callAsync([callback, slotIndex]()
            {
                callback(slotIndex, false, "Invalid slot index");
            });
        }
        return;
    }
    
    // Check slot state quickly (this is just reading local data, no driver access)
    {
        const juce::ScopedLock sl(slots[slotIndex].lock);
        if (!slots[slotIndex].hasData)
        {
            if (callback)
            {
                juce::MessageManager::getInstance()->callAsync([callback, slotIndex]()
                {
                    callback(slotIndex, false, "No data in slot");
                });
            }
            return;
        }
        
        if (slots[slotIndex].isUploading)
        {
            if (callback)
            {
                juce::MessageManager::getInstance()->callAsync([callback, slotIndex]()
                {
                    callback(slotIndex, false, "Upload already in progress");
                });
            }
            return;
        }
    }
    
    // Queue upload on background thread (device.isConnected() will be checked there)
    if (uploadThread)
    {
        uploadThread->queueUpload(slotIndex, callback);
    }
    else
    {
        if (callback)
        {
            juce::MessageManager::getInstance()->callAsync([callback, slotIndex]()
            {
                callback(slotIndex, false, "Upload thread not available");
            });
        }
    }
}

bool ARBManager::isUploading(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex >= 4) return false;
    const juce::ScopedLock sl(slots[slotIndex].lock);
    return slots[slotIndex].isUploading;
}

bool ARBManager::deleteARBFromDevice(const juce::String& name)
{
    if (!device.isConnected()) return false;
    return device.deleteARBWaveform(name.toStdString());
}

void ARBManager::syncFromDevice()
{
    if (!device.isConnected()) return;
    
    // HP33120A does not support querying ARB names or data via SCPI
    // We cannot determine which ARBs are currently in device memory
    // Reset all slots to "not uploaded" state - user must upload manually
    // This prevents crashes from unsupported query commands
    
    for (int i = 0; i < 4; ++i)
    {
        const juce::ScopedLock sl(slots[i].lock);
        slots[i].uploadedToDevice = false;  // Reset - assume not uploaded until user uploads
    }
}

