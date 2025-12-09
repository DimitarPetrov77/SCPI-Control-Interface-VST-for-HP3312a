#include "LFOEngine.h"

LFOEngine::LFOEngine(HP33120ADriver* device)
    : device(device)
{
    startTimer(updateIntervalMs);
}

LFOEngine::~LFOEngine()
{
    stopTimer();
}

void LFOEngine::setEnabled(bool en)
{
    enabled = en;
}

void LFOEngine::setWaveform(const std::string& waveform)
{
    lfoWaveform = waveform;
}

void LFOEngine::setRate(double rateHz)
{
    lfoRate = rateHz;
}

void LFOEngine::setDepth(double depth)
{
    lfoDepth = depth;
}

void LFOEngine::setTargetParam(const std::string& param)
{
    targetParam = param;
}

void LFOEngine::timerCallback()
{
    if (!enabled || !device || !device->isConnected())
        return;
    
    double dt = updateIntervalMs / 1000.0;
    constexpr double twoPi = 2.0 * 3.14159265358979323846;
    phase += twoPi * lfoRate * dt;
    
    if (phase >= twoPi)
        phase -= twoPi;
    
    double val = waveformValue(lfoWaveform, phase);
    double offsetVal = val * lfoDepth;
    
    try
    {
        if (targetParam == "FREQUENCY")
        {
            double freq = device->baseFreq * (1.0 + offsetVal);
            device->updateFrequencyLive(freq);
        }
        else if (targetParam == "AMPLITUDE")
        {
            double amp = device->baseAmp * (1.0 + offsetVal);
            device->updateAmplitudeLive(amp);
        }
        else if (targetParam == "DUTY")
        {
            double duty = device->baseDuty + offsetVal * 20.0;
            device->updateDutyCycleLive(duty);
        }
        else if (targetParam == "AM_DEPTH")
        {
            double depth = device->baseAMDepth + offsetVal * 30.0;
            device->updateAMDepthLive(depth);
        }
        else if (targetParam == "FM_DEV")
        {
            double dev = device->baseFMDev + offsetVal * 500.0;
            device->updateFMDevLive(dev);
        }
    }
    catch (...)
    {
        // Silently handle errors
    }
}

double LFOEngine::waveformValue(const std::string& shape, double phaseValue)
{
    constexpr double twoPi = 2.0 * 3.14159265358979323846;
    
    if (shape == "SINE")
    {
        return std::sin(phaseValue);
    }
    else if (shape == "TRI")
    {
        double frac = std::fmod(phaseValue / twoPi, 1.0);
        if (frac < 0.25)
            return frac * 4.0;
        else if (frac < 0.75)
            return 2.0 - frac * 4.0;
        else
            return frac * 4.0 - 4.0;
    }
    else if (shape == "SQUARE")
    {
        return std::sin(phase) >= 0.0 ? 1.0 : -1.0;
    }
    else if (shape == "RAMP")
    {
        double frac = std::fmod(phaseValue / twoPi, 1.0);
        return frac * 2.0 - 1.0;
    }
    
    return 0.0;
}

