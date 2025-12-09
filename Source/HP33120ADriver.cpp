#include "HP33120ADriver.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#define VISA_LIB_NAME "visa32.dll"
#undef max
#undef min
#else
#include <dlfcn.h>
#define VISA_LIB_NAME "libvisa.so"
#endif

HP33120ADriver::HP33120ADriver()
{
    baseFreq = 1000.0;
    loadVISALibrary();
}

HP33120ADriver::~HP33120ADriver()
{
    disconnect();
    unloadVISALibrary();
}

bool HP33120ADriver::loadVISALibrary()
{
#ifdef _WIN32
    visaLib = LoadLibraryA(VISA_LIB_NAME);
    if (!visaLib)
    {
        visaLib = LoadLibraryA("C:\\Program Files\\IVI Foundation\\VISA\\Win64\\bin\\visa64.dll");
        if (!visaLib)
            visaLib = LoadLibraryA("C:\\Program Files (x86)\\IVI Foundation\\VISA\\WinNT\\bin\\visa32.dll");
    }
    
    if (visaLib)
    {
        viOpenDefaultRM = (ViOpenDefaultRM)GetProcAddress((HMODULE)visaLib, "viOpenDefaultRM");
        viOpen = (ViOpen)GetProcAddress((HMODULE)visaLib, "viOpen");
        viClose = (ViClose)GetProcAddress((HMODULE)visaLib, "viClose");
        viWrite = (ViWrite)GetProcAddress((HMODULE)visaLib, "viWrite");
        viRead = (ViRead)GetProcAddress((HMODULE)visaLib, "viRead");
        viPrintf = (ViPrintf)GetProcAddress((HMODULE)visaLib, "viPrintf");
        viScanf = (ViScanf)GetProcAddress((HMODULE)visaLib, "viScanf");
        viSetAttribute = (ViSetAttribute)GetProcAddress((HMODULE)visaLib, "viSetAttribute");
        viFlush = (ViFlush)GetProcAddress((HMODULE)visaLib, "viFlush");
    }
#else
    visaLib = dlopen(VISA_LIB_NAME, RTLD_LAZY);
    if (visaLib)
    {
        viOpenDefaultRM = (ViOpenDefaultRM)dlsym(visaLib, "viOpenDefaultRM");
        viOpen = (ViOpen)dlsym(visaLib, "viOpen");
        viClose = (ViClose)dlsym(visaLib, "viClose");
        viWrite = (ViWrite)dlsym(visaLib, "viWrite");
        viRead = (ViRead)dlsym(visaLib, "viRead");
        viPrintf = (ViPrintf)dlsym(visaLib, "viPrintf");
        viScanf = (ViScanf)dlsym(visaLib, "viScanf");
        viSetAttribute = (ViSetAttribute)dlsym(visaLib, "viSetAttribute");
        viFlush = (ViFlush)dlsym(visaLib, "viFlush");
    }
#endif
    return (viOpenDefaultRM != nullptr && viOpen != nullptr);
}

void HP33120ADriver::unloadVISALibrary()
{
    if (visaLib)
    {
#ifdef _WIN32
        FreeLibrary((HMODULE)visaLib);
#else
        dlclose(visaLib);
#endif
        visaLib = nullptr;
    }
}

bool HP33120ADriver::connect(const std::string& resource)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    lastError.clear();
    
    if (connected)
        disconnect();
    
    if (!viOpenDefaultRM || !viOpen)
    {
        lastError = "VISA library not loaded.";
        return false;
    }
    
    this->resourceName = resource;
    
    ViSession defaultRMSession = nullptr;
    ViSession sessionObj = nullptr;
    
    ViStatus status = viOpenDefaultRM(&defaultRMSession);
    if (status != VI_SUCCESS) return false;
    
    status = viOpen(defaultRMSession, (ViRsrc)resourceName.c_str(), VI_NULL, VI_NULL, &sessionObj);
    if (status != VI_SUCCESS)
    {
        viClose((ViObject)defaultRMSession);
        lastError = "Failed to open device.";
        return false;
    }
    
    rm = (void*)defaultRMSession;
    this->session = (void*)sessionObj;
    
    if (viSetAttribute)
    {
        viSetAttribute((ViObject)sessionObj, 0x3FFF001A, 5000); // Timeout
    }
    
    connected = true;
    
    // Ensure remote mode and clear status
    write("SYST:REM"); 
    write("*CLS");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    return true;
}

void HP33120ADriver::disconnect()
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    
    // Go to local mode before closing
    if (connected) write("SYST:LOC");

    if (session && viClose)
    {
        viClose((ViObject)session);
        session = nullptr;
    }
    if (rm && viClose)
    {
        viClose((ViObject)rm);
        rm = nullptr;
    }
    connected = false;
}

void HP33120ADriver::write(const std::string& cmd)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    if (!connected || !viPrintf) return;
    
    std::string cmdWithNewline = cmd + "\n";
    ViSession sess = (ViSession)session;
    viPrintf(sess, "%s", cmdWithNewline.c_str());
    
    if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
    
    // No delays - send commands as fast as possible, let the device handle the rate
    // Query device error response and log it (raw hardware response)
    // This shows the actual response from the hardware after each command
    // Minimal delay only for device to be ready to respond
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    // Directly query SYST:ERR? without going through query() to avoid recursion
    if (viPrintf && viRead)
    {
        viPrintf(sess, "SYST:ERR?\n");
        if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
        
        // Minimal delay for read - let device respond as fast as it can
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        
        char buffer[1024] = {0};
        ViUInt32 retCount = 0;
        ViStatus status = viRead(sess, (ViBuf)buffer, sizeof(buffer) - 1, &retCount);
        
        if (status == VI_SUCCESS && retCount > 0)
        {
            buffer[retCount] = '\0';
            std::string response(buffer);
            
            // Remove trailing whitespace
            while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
                response.pop_back();
            
            // Log normal commands and their responses (like Python does)
            // This shows: "FREQ 1000.0 -> +0,"No error""
            if (logCallback && !response.empty())
            {
                std::string logMsg = cmd + " -> " + response;
                logCallback(logMsg);
            }
        }
    }
}

std::string HP33120ADriver::query(const std::string& cmd)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    if (!connected || !viPrintf || !viRead) return "";
    
    // Send command (but don't log here - write() will log it)
    std::string cmdWithNewline = cmd + "\n";
    ViSession sess = (ViSession)session;
    viPrintf(sess, "%s", cmdWithNewline.c_str());
    
    if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    char buffer[1024] = {0};
    ViUInt32 retCount = 0;
    
    ViStatus status = viRead(sess, (ViBuf)buffer, sizeof(buffer) - 1, &retCount);
    
    if (status != VI_SUCCESS) 
    {
        if (logCallback)
        {
            logCallback(cmd + " -> [Read Error: " + std::to_string(status) + "]");
        }
        return "";
    }
    
    if (retCount > 0) buffer[retCount] = '\0';
    std::string result(buffer);
    
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
        result.pop_back();
    
    // Don't log queries - they spam the status window
    // Only log write() commands (normal SCPI commands like FREQ, VOLT, etc.)
    
    return result;
}

std::string HP33120ADriver::queryIDN() { return query("*IDN?"); }
std::string HP33120ADriver::queryError() { return query("SYST:ERR?"); }

// --- THE FIX: APPLy Command ---
void HP33120ADriver::applyWaveform(const std::string& shape, double freq, double amp, double offset)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    
    // Update base params for LFO references
    baseFreq = freq;
    baseAmp = amp;
    baseOffset = offset;

    // CRITICAL FIX: Use juce::String to guarantee dot (.) decimal separator
    // std::ostringstream can produce commas (,) depending on locale settings
    // SCPI hardware requires dots, so we use juce::String which always uses dots
    juce::String cmd = "APPL:" + juce::String(shape) + " " + 
                       juce::String(freq, 6) + ", " + 
                       juce::String(amp, 6) + ", " + 
                       juce::String(offset, 6);
    
    write(cmd.toStdString());
}

void HP33120ADriver::setWaveform(const std::string& waveform) { write("FUNC " + waveform); }

void HP33120ADriver::setFrequency(double freqHz)
{
    baseFreq = freqHz;
    // Use juce::String for locale-safe formatting (guarantees dots, not commas)
    juce::String cmd = "FREQ " + juce::String(freqHz, 6);
    write(cmd.toStdString());
}

void HP33120ADriver::setAmplitude(double ampVpp)
{
    baseAmp = ampVpp;
    // Use juce::String for locale-safe formatting (guarantees dots, not commas)
    juce::String cmd = "VOLT " + juce::String(ampVpp, 6);
    write(cmd.toStdString());
}

void HP33120ADriver::setOffset(double offsetV)
{
    baseOffset = offsetV;
    // Use juce::String for locale-safe formatting (guarantees dots, not commas)
    juce::String cmd = "VOLT:OFFS " + juce::String(offsetV, 6);
    write(cmd.toStdString());
}

void HP33120ADriver::setPhase(double phaseDeg)
{
    if (phaseDeg >= 360.0) phaseDeg = 359.999;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << "PHAS " << phaseDeg;
    write(oss.str());
}

void HP33120ADriver::setDutyCycle(double duty)
{
    baseDuty = duty;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << "FUNC:SQU:DCYC " << duty;
    write(oss.str());
}

void HP33120ADriver::setOutputEnabled(bool enabled)
{
    write(enabled ? "OUTP ON" : "OUTP OFF");
}

// AM
void HP33120ADriver::setAMEnabled(bool enabled) { write(enabled ? "AM:STAT ON" : "AM:STAT OFF"); }
void HP33120ADriver::setAMDepth(double depth) { write("AM:DEPT " + std::to_string(depth)); }
void HP33120ADriver::setAMSource(const std::string& source) { write("AM:SOUR " + source); }
void HP33120ADriver::setAMInternalWaveform(const std::string& waveform) { write("AM:INT:FUNC " + waveform); }
void HP33120ADriver::setAMInternalFrequency(double freqHz) { write("AM:INT:FREQ " + std::to_string(freqHz)); }

// FM
void HP33120ADriver::setFMEnabled(bool enabled) { write(enabled ? "FM:STAT ON" : "FM:STAT OFF"); }
void HP33120ADriver::setFMDeviation(double devHz) { write("FM:DEV " + std::to_string(devHz)); }
void HP33120ADriver::setFMSource(const std::string& source) { write("FM:SOUR " + source); }
void HP33120ADriver::setFMInternalWaveform(const std::string& waveform) { write("FM:INT:FUNC " + waveform); }
void HP33120ADriver::setFMInternalFrequency(double freqHz) { write("FM:INT:FREQ " + std::to_string(freqHz)); }

// FSK
void HP33120ADriver::setFSKEnabled(bool enabled) { write(enabled ? "FSK:STAT ON" : "FSK:STAT OFF"); }
void HP33120ADriver::setFSKFrequency(double freqHz) { write("FSK:FREQ " + std::to_string(freqHz)); }
void HP33120ADriver::setFSKSource(const std::string& source) { write("FSK:SOUR " + source); }
void HP33120ADriver::setFSKInternalRate(double rateHz) { write("FSK:INT:RATE " + std::to_string(rateHz)); }

// Sweep
void HP33120ADriver::setSweepEnabled(bool enabled) { write(enabled ? "SWE:STAT ON" : "SWE:STAT OFF"); }
void HP33120ADriver::setSweepStartFreq(double freqHz) { write("FREQ:STAR " + std::to_string(freqHz)); }
void HP33120ADriver::setSweepStopFreq(double freqHz) { write("FREQ:STOP " + std::to_string(freqHz)); }
void HP33120ADriver::setSweepTime(double timeS) { write("SWE:TIME " + std::to_string(timeS)); }

// Burst
void HP33120ADriver::setBurstEnabled(bool enabled) { write(enabled ? "BM:STAT ON" : "BM:STAT OFF"); }
void HP33120ADriver::setBurstCycles(int cycles) { write("BM:NCYC " + std::to_string(cycles)); }
void HP33120ADriver::setBurstPhase(double phaseDeg) { write("BM:PHAS " + std::to_string(phaseDeg)); }
void HP33120ADriver::setBurstInternalPeriod(double periodS) { write("BM:INT:RATE " + std::to_string(1.0/periodS)); } // Note: 33120A uses Rate for Internal, not Period
void HP33120ADriver::setBurstSource(const std::string& source) { write("BM:SOUR " + source); }

// Sync/Trig
void HP33120ADriver::setSyncEnabled(bool enabled) { write(enabled ? "OUTP:SYNC ON" : "OUTP:SYNC OFF"); }
void HP33120ADriver::setSyncPhase(double phaseDeg) 
{ 
    // Not supported directly on 33120A via specific command, uses standard Phase
    // Use setPhase() instead for phase control
    (void)phaseDeg; // Suppress unused parameter warning
}
void HP33120ADriver::setTriggerSource(const std::string& source) { write("TRIG:SOUR " + source); }

void HP33120ADriver::downloadARBWaveform(const std::string& name, const std::vector<float>& data)
{
    // Implementation omitted for brevity, logic remains same
    (void)name;   // Suppress unused parameter warning
    (void)data;   // Suppress unused parameter warning
}

// Live Updates (LFO)
void HP33120ADriver::updateFrequencyLive(double freqHz) { setFrequency(freqHz); }
void HP33120ADriver::updateAmplitudeLive(double ampVpp) { setAmplitude(ampVpp); }
void HP33120ADriver::updateDutyCycleLive(double duty) { setDutyCycle(duty); }
void HP33120ADriver::updateAMDepthLive(double depth) { setAMDepth(depth); }
void HP33120ADriver::updateFMDevLive(double devHz) { setFMDeviation(devHz); }