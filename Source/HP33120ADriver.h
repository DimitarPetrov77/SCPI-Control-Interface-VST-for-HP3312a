#pragma once

#include <juce_core/juce_core.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

// VISA type definitions
typedef unsigned short ViUInt16;
typedef unsigned long ViUInt32;
typedef long ViStatus;
typedef unsigned long ViAccessMode;
typedef void* ViObject;
typedef void* ViSession;
typedef char* ViRsrc;
typedef char* ViString;
typedef unsigned char* ViBuf;

// VISA constants
const ViUInt32 VI_NULL = 0;
const ViStatus VI_SUCCESS = 0;
const ViUInt16 VI_FLUSH_ON_WRITE = 0x0002;

class HP33120ADriver
{
public:
    HP33120ADriver();
    ~HP33120ADriver();
    
    // Logging callback - set this to receive raw device responses
    std::function<void(const std::string&)> logCallback;
    
    // Enable/disable verbose logging (logs every command response)
    // Set to false to only log errors or use logVerbose() for specific commands
    bool verboseLogging = false;
    
    // Connection
    bool connect(const std::string& resourceName = "GPIB0::10::INSTR");
    void disconnect();
    bool isConnected() const { return connected; }
    std::string getLastError() const { return lastError; }
    
    // Query device
    std::string queryIDN();
    std::string queryError();
    
    // --- NEW: Atomic Apply Command (Per Manual Page 138) ---
    // This is the critical fix for MIDI notes. It sends Freq/Amp/Offset in one shot.
    void applyWaveform(const std::string& shape, double freq, double amp, double offset);

    // Basic SCPI commands
    void setWaveform(const std::string& waveform);
    void setUserWaveform(const std::string& name);  // Select specific ARB waveform by name AND change to USER shape
    void selectUserWaveform(const std::string& name);  // Just select which ARB is active (doesn't change shape)
    void setFrequency(double freqHz);
    void setAmplitude(double ampVpp);
    void setOffset(double offsetV);
    void setPhase(double phaseDeg);
    void setDutyCycle(double duty);
    void setOutputEnabled(bool enabled);
    
    // AM Modulation
    void setAMEnabled(bool enabled);
    void setAMDepth(double depth);
    void setAMSource(const std::string& source);
    void setAMInternalWaveform(const std::string& waveform);
    void setAMInternalFrequency(double freqHz);
    
    // FM Modulation
    void setFMEnabled(bool enabled);
    void setFMDeviation(double devHz);
    void setFMSource(const std::string& source);
    void setFMInternalWaveform(const std::string& waveform);
    void setFMInternalFrequency(double freqHz);
    
    // FSK
    void setFSKEnabled(bool enabled);
    void setFSKFrequency(double freqHz);
    void setFSKSource(const std::string& source);
    void setFSKInternalRate(double rateHz);
    
    // Sweep
    void setSweepEnabled(bool enabled);
    void setSweepStartFreq(double freqHz);
    void setSweepStopFreq(double freqHz);
    void setSweepTime(double timeS);
    
    // Burst
    void setBurstEnabled(bool enabled);
    void setBurstCycles(int cycles);
    void setBurstPhase(double phaseDeg);
    void setBurstInternalPeriod(double periodS);
    void setBurstSource(const std::string& source);
    
    // Sync
    void setSyncEnabled(bool enabled);
    void setSyncPhase(double phaseDeg);
    
    // Trigger
    void setTriggerSource(const std::string& source);
    
    // ARB Operations
    void downloadARBWaveform(const std::string& name, const std::vector<float>& data, int maxPoints = 16000);
    std::vector<float> queryARBWaveform(const std::string& name);  // Returns empty if not supported
    bool deleteARBWaveform(const std::string& name);  // Try SCPI DELETE, return success
    std::vector<std::string> listARBNames();  // Query device for ARB names (if supported)
    std::vector<std::string> queryWaveformCatalog();  // Query DATA:CATalog? to get all available waveforms
    
    // Live updates for LFO
    void updateFrequencyLive(double freqHz);
    void updateAmplitudeLive(double ampVpp);
    void updateDutyCycleLive(double duty);
    void updateAMDepthLive(double depth);
    void updateFMDevLive(double devHz);
    
    // Base parameters for LFO
    double baseFreq = 1000.0;
    double baseAmp = 1.0;
    double baseOffset = 0.0;
    double baseDuty = 50.0;
    double baseAMDepth = 50.0;
    double baseFMDev = 100.0;
    
private:
    void write(const std::string& cmd);
    void writeFast(const std::string& cmd);  // Fast write without error checking - for real-time slider updates
    std::string query(const std::string& cmd);
    
    // Mutex for thread safety (UI vs MIDI)
    mutable std::recursive_mutex driverMutex;
    
    bool connected = false;
    void* rm = nullptr;  // VISA Resource Manager
    void* session = nullptr;  // VISA Session
    std::string resourceName;
    std::string lastError;
    
    // VISA function pointers
    typedef ViStatus (*ViOpenDefaultRM)(ViSession*);
    typedef ViStatus (*ViOpen)(ViSession, ViRsrc, ViAccessMode, ViUInt32, ViSession*);
    typedef ViStatus (*ViClose)(ViObject);
    typedef ViStatus (*ViWrite)(ViSession, ViBuf, ViUInt32, ViUInt32*);
    typedef ViStatus (*ViRead)(ViSession, ViBuf, ViUInt32, ViUInt32*);
    typedef int (*ViPrintf)(ViSession, ViString, ...);
    typedef int (*ViScanf)(ViSession, ViString, ...);
    typedef ViStatus (*ViSetAttribute)(ViObject, ViUInt32, ViUInt32);
    typedef ViStatus (*ViFlush)(ViSession, ViUInt16);
    
    void* visaLib = nullptr;
    ViOpenDefaultRM viOpenDefaultRM = nullptr;
    ViOpen viOpen = nullptr;
    ViClose viClose = nullptr;
    ViWrite viWrite = nullptr;
    ViRead viRead = nullptr;
    ViPrintf viPrintf = nullptr;
    ViScanf viScanf = nullptr;
    ViSetAttribute viSetAttribute = nullptr;
    ViFlush viFlush = nullptr;
    
    bool loadVISALibrary();
    void unloadVISALibrary();
};