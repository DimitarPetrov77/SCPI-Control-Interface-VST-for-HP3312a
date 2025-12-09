#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "HP33120ADriver.h"
#include <string>
#include <cmath>

class LFOEngine : public juce::Timer
{
public:
    LFOEngine(HP33120ADriver* device);
    ~LFOEngine();
    
    void setEnabled(bool enabled);
    void setWaveform(const std::string& waveform);
    void setRate(double rateHz);
    void setDepth(double depth);
    void setTargetParam(const std::string& param);
    
    bool isEnabled() const { return enabled; }
    std::string getWaveform() const { return lfoWaveform; }
    double getRate() const { return lfoRate; }
    double getDepth() const { return lfoDepth; }
    std::string getTargetParam() const { return targetParam; }
    
private:
    void timerCallback() override;
    double waveformValue(const std::string& shape, double phase);
    
    HP33120ADriver* device;
    bool enabled = false;
    
    std::string lfoWaveform = "SINE";
    double lfoRate = 1.0;
    double lfoDepth = 0.1;
    std::string targetParam = "FREQUENCY";
    
    double phase = 0.0;
    static constexpr int updateIntervalMs = 50;  // 20 Hz update rate
};

