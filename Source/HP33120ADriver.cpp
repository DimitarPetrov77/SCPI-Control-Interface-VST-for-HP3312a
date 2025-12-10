#include "HP33120ADriver.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

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
        // Set timeout to 500ms - short enough for UI responsiveness, long enough for most commands
        // If device is slow or unresponsive, commands will fail quickly rather than blocking the UI
        viSetAttribute((ViObject)sessionObj, 0x3FFF001A, 500); // 500ms timeout
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
    
    try
    {
        std::string cmdWithNewline = cmd + "\n";
        ViSession sess = (ViSession)session;
        ViStatus status = viPrintf(sess, "%s", cmdWithNewline.c_str());
        
        if (status != VI_SUCCESS)
        {
            lastError = "Write failed: " + cmd;
            if (logCallback)
                logCallback("[ERROR] Command failed: " + cmd + " (status: " + std::to_string(status) + ")");
            return;
        }
        
        if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
        
        // ALWAYS check for device errors (not just in verbose mode)
        // This is important for user feedback - they need to know when commands fail
        if (viPrintf && viRead)
        {
            // Query SYST:ERR? to check if device had any problems with the command
            status = viPrintf(sess, "SYST:ERR?\n");
            if (status == VI_SUCCESS && viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
            
            char buffer[256] = {0};
            ViUInt32 retCount = 0;
            status = viRead(sess, (ViBuf)buffer, sizeof(buffer) - 1, &retCount);
            
            if (status == VI_SUCCESS && retCount > 0)
            {
                buffer[retCount] = '\0';
                std::string response(buffer);
                
                // Remove trailing whitespace
                while (!response.empty() && (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
                    response.pop_back();
                
                // Check if there's an actual error (not "+0,No error")
                bool hasError = !response.empty() && response[0] != '+' && response.find("No error") == std::string::npos;
                
                // Also check for error codes like "-222,Data out of range"
                if (response.find("-") == 0 || (response.find(",") != std::string::npos && response[0] == '-'))
                {
                    hasError = true;
                }
                
                if (hasError && logCallback)
                {
                    // Log the error with the command that caused it
                    logCallback("[DEVICE ERROR] " + cmd + " -> " + response);
                    lastError = response;
                }
                else if (verboseLogging && logCallback)
                {
                    // In verbose mode, log all commands even if no error
                    logCallback(cmd + " -> " + response);
                }
            }
            else if (status == (ViStatus)0xBFFF0015)  // VI_ERROR_TMO - timeout
            {
                // Timeout on error query - device might be busy or command was slow
                // Don't log this as an error, just continue
                if (verboseLogging && logCallback)
                {
                    logCallback(cmd + " -> [no response - device may be processing]");
                }
            }
            else if (logCallback && verboseLogging)
            {
                logCallback(cmd + " -> [read failed: " + std::to_string(status) + "]");
            }
        }
    }
    catch (const std::exception& e)
    {
        lastError = std::string("Exception: ") + e.what();
        if (logCallback)
            logCallback("[EXCEPTION] " + cmd + " -> " + e.what());
    }
    catch (...)
    {
        lastError = "Unknown error";
        if (logCallback)
            logCallback("[ERROR] " + cmd + " -> Unknown exception");
    }
}

// Fast write without error checking - for real-time slider updates
// This is ~50-100ms faster than write() because it skips SYST:ERR? query
void HP33120ADriver::writeFast(const std::string& cmd)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    if (!connected || !viPrintf) return;
    
    try
    {
        std::string cmdWithNewline = cmd + "\n";
        ViSession sess = (ViSession)session;
        ViStatus status = viPrintf(sess, "%s", cmdWithNewline.c_str());
        
        if (status != VI_SUCCESS)
        {
            lastError = "Write failed: " + cmd;
            // Don't log errors for fast writes - they're fire-and-forget
            return;
        }
        
        if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
        // NO error query - that's what makes this fast!
    }
    catch (...)
    {
        // Silently ignore exceptions in fast mode
    }
}

std::string HP33120ADriver::query(const std::string& cmd)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    if (!connected || !viPrintf || !viRead) return "";
    
    try
    {
        std::string cmdWithNewline = cmd + "\n";
        ViSession sess = (ViSession)session;
        
        ViStatus writeStatus = viPrintf(sess, "%s", cmdWithNewline.c_str());
        if (writeStatus != VI_SUCCESS)
        {
            lastError = "Query write failed: " + cmd;
            return "";
        }
        
        if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
        
        // No delay needed - the VISA read will use the configured timeout
        
        char buffer[1024] = {0};
        ViUInt32 retCount = 0;
        
        ViStatus status = viRead(sess, (ViBuf)buffer, sizeof(buffer) - 1, &retCount);
        
        if (status != VI_SUCCESS) 
        {
            // VI_ERROR_TMO (0xBFFF0015) is timeout - common for unsupported queries
            if (status != (ViStatus)0xBFFF0015)
            {
                lastError = "Query read failed: " + cmd + " (status: " + std::to_string(status) + ")";
                if (logCallback)
                    logCallback("[QUERY ERROR] " + cmd + " -> Read failed (" + std::to_string(status) + ")");
            }
            return "";
        }
        
        if (retCount > 0) buffer[retCount] = '\0';
        std::string result(buffer);
        
        // Remove trailing whitespace
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
            result.pop_back();
        
        return result;
    }
    catch (const std::exception& e)
    {
        lastError = std::string("Query exception: ") + e.what();
        if (logCallback)
            logCallback("[EXCEPTION] Query " + cmd + " -> " + e.what());
        return "";
    }
    catch (...)
    {
        lastError = "Query unknown error";
        if (logCallback)
            logCallback("[ERROR] Query " + cmd + " -> Unknown exception");
        return "";
    }
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

void HP33120ADriver::setUserWaveform(const std::string& name)
{
    // Select a specific ARB waveform by name
    // First set shape to USER, then select the specific waveform
    write("FUNCtion:SHAPe USER");
    write("FUNCtion:USER " + name);
}

void HP33120ADriver::selectUserWaveform(const std::string& name)
{
    // Just select which ARB is active without changing the main waveform shape
    // This is useful when using ARBs for AM/FM modulation while keeping a different carrier
    write("FUNCtion:USER " + name);
}

void HP33120ADriver::setFrequency(double freqHz)
{
    baseFreq = freqHz;
    // Use juce::String for locale-safe formatting (guarantees dots, not commas)
    juce::String cmd = "FREQ " + juce::String(freqHz, 6);
    writeFast(cmd.toStdString());  // Fast for instant slider response
}

void HP33120ADriver::setAmplitude(double ampVpp)
{
    baseAmp = ampVpp;
    // Use juce::String for locale-safe formatting (guarantees dots, not commas)
    juce::String cmd = "VOLT " + juce::String(ampVpp, 6);
    writeFast(cmd.toStdString());  // Fast for instant slider response
}

void HP33120ADriver::setOffset(double offsetV)
{
    baseOffset = offsetV;
    // Use juce::String for locale-safe formatting (guarantees dots, not commas)
    juce::String cmd = "VOLT:OFFS " + juce::String(offsetV, 6);
    writeFast(cmd.toStdString());  // Fast for instant slider response
}

void HP33120ADriver::setPhase(double phaseDeg)
{
    if (phaseDeg >= 360.0) phaseDeg = 359.999;
    juce::String cmd = "PHAS " + juce::String(phaseDeg, 3);
    writeFast(cmd.toStdString());  // Fast for instant slider response
}

void HP33120ADriver::setDutyCycle(double duty)
{
    baseDuty = duty;
    juce::String cmd = "FUNC:SQU:DCYC " + juce::String(duty, 6);
    writeFast(cmd.toStdString());  // Fast for instant slider response
}

void HP33120ADriver::setOutputEnabled(bool enabled)
{
    write(enabled ? "OUTP ON" : "OUTP OFF");
}

// AM - use writeFast for slider-controlled values, write for toggles/combos
void HP33120ADriver::setAMEnabled(bool enabled) { write(enabled ? "AM:STAT ON" : "AM:STAT OFF"); }
void HP33120ADriver::setAMDepth(double depth) { writeFast("AM:DEPT " + std::to_string(depth)); }
void HP33120ADriver::setAMSource(const std::string& source) { write("AM:SOUR " + source); }
void HP33120ADriver::setAMInternalWaveform(const std::string& waveform) { write("AM:INT:FUNC " + waveform); }
void HP33120ADriver::setAMInternalFrequency(double freqHz) { writeFast("AM:INT:FREQ " + std::to_string(freqHz)); }

// FM - use writeFast for slider-controlled values, write for toggles/combos
void HP33120ADriver::setFMEnabled(bool enabled) { write(enabled ? "FM:STAT ON" : "FM:STAT OFF"); }
void HP33120ADriver::setFMDeviation(double devHz) { writeFast("FM:DEV " + std::to_string(devHz)); }
void HP33120ADriver::setFMSource(const std::string& source) { write("FM:SOUR " + source); }
void HP33120ADriver::setFMInternalWaveform(const std::string& waveform) { write("FM:INT:FUNC " + waveform); }
void HP33120ADriver::setFMInternalFrequency(double freqHz) { writeFast("FM:INT:FREQ " + std::to_string(freqHz)); }

// FSK - use writeFast for slider-controlled values, write for toggles/combos
void HP33120ADriver::setFSKEnabled(bool enabled) { write(enabled ? "FSK:STAT ON" : "FSK:STAT OFF"); }
void HP33120ADriver::setFSKFrequency(double freqHz) { writeFast("FSK:FREQ " + std::to_string(freqHz)); }
void HP33120ADriver::setFSKSource(const std::string& source) { write("FSK:SOUR " + source); }
void HP33120ADriver::setFSKInternalRate(double rateHz) { writeFast("FSK:INT:RATE " + std::to_string(rateHz)); }

// Sweep - use writeFast for slider-controlled values, write for toggles
void HP33120ADriver::setSweepEnabled(bool enabled) { write(enabled ? "SWE:STAT ON" : "SWE:STAT OFF"); }
void HP33120ADriver::setSweepStartFreq(double freqHz) { writeFast("FREQ:STAR " + std::to_string(freqHz)); }
void HP33120ADriver::setSweepStopFreq(double freqHz) { writeFast("FREQ:STOP " + std::to_string(freqHz)); }
void HP33120ADriver::setSweepTime(double timeS) { writeFast("SWE:TIME " + std::to_string(timeS)); }

// Burst - use writeFast for slider-controlled values, write for toggles/combos
void HP33120ADriver::setBurstEnabled(bool enabled) { write(enabled ? "BM:STAT ON" : "BM:STAT OFF"); }
void HP33120ADriver::setBurstCycles(int cycles) { writeFast("BM:NCYC " + std::to_string(cycles)); }
void HP33120ADriver::setBurstPhase(double phaseDeg) { writeFast("BM:PHAS " + std::to_string(phaseDeg)); }
void HP33120ADriver::setBurstInternalPeriod(double periodS) { writeFast("BM:INT:RATE " + std::to_string(1.0/periodS)); }
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

void HP33120ADriver::downloadARBWaveform(const std::string& name, const std::vector<float>& data, int maxPoints)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    if (!connected || data.empty()) return;
    
    // Validate point count (ARBManager should have already resampled, but validate anyway)
    if (maxPoints < 8 || maxPoints > 16000)
    {
        if (logCallback)
            logCallback("ARB point count out of range: " + std::to_string(maxPoints) + " (must be 8-16000)");
        return;
    }
    
    // ARBManager should have already resampled and normalized the data
    // Just validate the data is in correct range and correct size
    std::vector<float> finalData = data;
    
    // If data size doesn't match maxPoints, log warning but proceed (ARBManager should handle this)
    if ((int)finalData.size() != maxPoints)
    {
        if (logCallback)
            logCallback("Warning: ARB data size (" + std::to_string(finalData.size()) + 
                       ") doesn't match target (" + std::to_string(maxPoints) + ")");
        // Truncate or pad if needed (shouldn't happen if ARBManager works correctly)
        if (finalData.size() > (size_t)maxPoints)
            finalData.resize(maxPoints);
        else
        {
            // Pad with zeros (shouldn't happen, but handle gracefully)
            finalData.resize(maxPoints, 0.0f);
        }
    }
    
    // Final validation: ensure data is in [-1, +1] range (ARBManager should have normalized, but double-check)
    bool needsNormalization = false;
    float maxVal = 0.0f;
    for (const auto& val : finalData)
    {
        float absVal = std::abs(val);
        if (absVal > 1.0f) needsNormalization = true;
        maxVal = std::max(maxVal, absVal);
    }
    
    if (needsNormalization && maxVal > 0.0f)
    {
        // Normalize if out of range (shouldn't happen if ARBManager works correctly)
        for (auto& val : finalData)
            val /= maxVal;
    }
    
    // Clamp to [-1, +1] to handle any floating point precision issues
    for (auto& val : finalData)
        val = std::max(-1.0f, std::min(1.0f, val));
    
    // Build SCPI command: DATA VOLATILE, val1,val2,...,valN
    // According to HP33120A manual, we must:
    // 1. Upload to VOLATILE memory using DATA VOLATILE
    // 2. Copy to non-volatile memory with DATA:COPY <name>, VOLATILE
    // 3. Select with FUNCtion:USER <name>
    // 4. Set shape to USER with FUNCtion:SHAPe USER
    // Use std::ostringstream for efficient string building, then convert to juce::String
    // This is much faster than concatenating juce::String objects
    std::ostringstream oss;
    oss.imbue(std::locale("C"));  // Force C locale to ensure dot decimal separator
    oss << std::fixed << std::setprecision(6);
    oss << "DATA VOLATILE";
    
    // Build data string efficiently using ostringstream
    // For very large datasets, yield periodically to keep system responsive
    const size_t yieldInterval = 1000;  // Yield every 1000 points
    for (size_t i = 0; i < finalData.size(); ++i)
    {
        oss << "," << finalData[i];
        
        // Yield periodically during string building for very large datasets
        if (i > 0 && (i % yieldInterval == 0) && finalData.size() > 5000)
        {
            std::this_thread::yield();
        }
    }
    
    juce::String cmd = juce::String(oss.str());
    
    // For very large commands, log a warning but proceed
    if (cmd.length() > 100000 && logCallback)
    {
        logCallback("Warning: Large ARB command (" + std::to_string(cmd.length()) + " chars) - upload may take time");
    }
    
    // Debug: Log first part of command to verify format
    if (logCallback && cmd.length() > 0)
    {
        juce::String firstPart = cmd.substring(0, juce::jmin(100, (int)cmd.length()));
        logCallback(("ARB command start: " + firstPart + "...").toStdString());
    }
    
    // Send the ARB data command directly (bypass normal write() to avoid error query on huge command)
    // This prevents crashes from querying errors on very large commands
    // Note: driverMutex is already locked at function start
    if (!connected || !viPrintf)
    {
        lastError = "Device not connected";
        if (logCallback)
            logCallback("ARB upload failed: Device not connected");
        return;
    }
    
    // Clear last error before starting
    lastError.clear();
    
    try
    {
        std::string cmdStr = cmd.toStdString();
        ViSession sess = (ViSession)session;
        
        // Temporarily increase timeout for large ARB uploads
        // Large waveform data (8000+ points, 100KB+ of command text) needs more time
        if (viSetAttribute)
        {
            viSetAttribute((ViObject)sess, 0x3FFF001A, 10000); // 10 second timeout for ARB upload
        }
        
        // Step 1: Upload to VOLATILE memory
        // Use viPrintf like the Python code does - it handles large strings correctly
        ViStatus status = viPrintf(sess, "%s\n", cmdStr.c_str());
        
        // Restore normal timeout immediately after write
        if (viSetAttribute)
        {
            viSetAttribute((ViObject)sess, 0x3FFF001A, 500); // Restore 500ms timeout
        }
        
        if (status != VI_SUCCESS)
        {
            lastError = "VISA write error " + std::to_string(status);
            if (logCallback)
                logCallback("ARB upload failed: VISA write error " + std::to_string(status));
            return;
        }
        
        if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
        
        // For large ARB uploads, give device more time to process
        // The device needs time to parse and store the large command
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Query error to check if upload was successful
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string error = queryError();
        
        if (error.find("No error") != std::string::npos || error.find("+0") != std::string::npos)
        {
            if (logCallback)
                logCallback("DATA VOLATILE -> [Uploaded " + std::to_string(finalData.size()) + " points]");
        }
        else
        {
            lastError = "DATA VOLATILE: " + error;
            if (logCallback)
                logCallback("DATA VOLATILE -> " + error);
            return;  // Don't proceed if upload failed
        }
        
        // Step 2: Copy from VOLATILE to non-volatile memory with the specified name
        // Strategy: According to manual, copying to an existing name overwrites it (no error).
        // The +781 error only occurs when all 4 slots are full AND the name doesn't exist.
        // Correct sequence: Try copy first, if it fails with +781, delete only the target name
        // (or one other waveform if target doesn't exist), then re-upload VOLATILE if needed.
        
        // First check available memory slots
        std::string freeSlots = query("DATA:NVOLatile:FREE?");
        if (logCallback)
            logCallback("DATA:NVOLatile:FREE? -> " + freeSlots);
        
        // Check if target name already exists in catalog (will be overwritten if so)
        std::string currentCatalog = query("DATA:NVOLatile:CATalog?");
        if (logCallback)
            logCallback("DATA:NVOLatile:CATalog? -> " + currentCatalog);
        
        std::string upperName = name;
        std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
        bool targetExists = (currentCatalog.find("\"" + upperName + "\"") != std::string::npos);
        
        // If memory is full (0 free slots) and target doesn't exist, we need to delete something first
        if (freeSlots.find("0") != std::string::npos && !targetExists)
        {
            if (logCallback)
                logCallback("Warning: No free memory slots and '" + name + "' doesn't exist. Need to delete an existing waveform first.");
            
            // Parse existing user waveforms from catalog
            std::vector<std::string> userWaveforms;
            std::string builtinNames[] = {"SINC", "NEG_RAMP", "EXP_RISE", "EXP_FALL", "CARDIAC"};
            size_t pos = 0;
            while ((pos = currentCatalog.find('"', pos)) != std::string::npos)
            {
                size_t endPos = currentCatalog.find('"', pos + 1);
                if (endPos != std::string::npos)
                {
                    std::string wfName = currentCatalog.substr(pos + 1, endPos - pos - 1);
                    bool isBuiltin = false;
                    for (const auto& builtin : builtinNames)
                    {
                        if (wfName == builtin) { isBuiltin = true; break; }
                    }
                    if (!isBuiltin)
                        userWaveforms.push_back(wfName);
                    pos = endPos + 1;
                }
                else break;
            }
            
            if (!userWaveforms.empty())
            {
                // Delete the first user waveform to make room
                std::string wfToDelete = userWaveforms[0];
                if (logCallback)
                    logCallback("Deleting '" + wfToDelete + "' to free memory slot...");
                
                // Switch to built-in waveform first to avoid "can't delete active" error
                write("FUNCtion:SHAPe SIN");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                queryError();  // Clear
                
                write("DATA:DELete " + wfToDelete);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::string delError = queryError();
                if (logCallback)
                    logCallback("DATA:DELete " + wfToDelete + " -> " + delError);
            }
        }
        
        // Now try to copy
        std::string copyCmd = "DATA:COPY " + name + ",VOLATILE";
        write(copyCmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // Give more time
        error = queryError();
        
        if (logCallback)
            logCallback("DATA:COPY " + name + " error check: " + error);
        
        // Check if copy succeeded by verifying the waveform appears in catalog
        // This is more reliable than just checking the error response
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string verifyCatalog = query("DATA:NVOLatile:CATalog?");
        bool copyVerified = (verifyCatalog.find("\"" + upperName + "\"") != std::string::npos);
        
        if (logCallback)
            logCallback("DATA:NVOLatile:CATalog? (verify) -> " + verifyCatalog);
        
        // Check if copy succeeded
        bool hasErrorCode = (error.find("+781") != std::string::npos || 
                            error.find("+785") != std::string::npos ||
                            error.find("+787") != std::string::npos ||
                            error.find("+786") != std::string::npos ||
                            error.find("+782") != std::string::npos ||
                            error.find("+783") != std::string::npos ||
                            error.find("+780") != std::string::npos);
        
        // Use catalog verification as the primary success check
        bool copySucceeded = copyVerified && !hasErrorCode;
        
        bool useVolatile = false;
        bool needReupload = false;
        
        if (!copySucceeded)
        {
            // Copy failed - check the reason
            if (error.find("+781") != std::string::npos)
            {
                // Memory full - need to free space by deleting the target name or another waveform
                if (logCallback)
                    logCallback("Memory full (copy failed with +781). Freeing memory slot for '" + name + "'...");
                
                // Get catalog to see what waveforms exist
                std::string catalog = query("DATA:CATalog?");
                std::string upperName = name;
                std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
                
                bool targetNameExists = (catalog.find(upperName) != std::string::npos || 
                                        catalog.find("\"" + upperName + "\"") != std::string::npos);
                
                // Switch to built-in waveform to allow deletion
                write("FUNCtion:SIN");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                queryError();  // Clear error
                
                // Delete the target name first (if it exists, this frees its slot)
                if (targetNameExists)
                {
                    if (logCallback)
                        logCallback("Deleting existing waveform '" + name + "' to free its slot...");
                    
                    std::string deleteCmd = "DATA:DELete " + name;
                    write(deleteCmd);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    std::string delError = queryError();
                    
                    if (logCallback)
                        logCallback("DATA:DELete " + name + " -> " + delError);
                    
                    if (delError.find("+787") != std::string::npos)
                    {
                        // Still active - try switching again
                        if (logCallback)
                            logCallback("Waveform still active. Switching to SINE again...");
                        
                        write("FUNCtion:SIN");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        queryError();
                        
                        write(deleteCmd);
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        delError = queryError();
                        
                        if (logCallback)
                            logCallback("DATA:DELete " + name + " (retry) -> " + delError);
                    }
                }
                else
                {
                    // Target name doesn't exist - need to delete another waveform to free space
                    // Parse catalog to find a user waveform to delete (not built-in, not VOLATILE)
                    if (logCallback)
                        logCallback("Target name doesn't exist. Finding another waveform to delete...");
                    
                    // Extract user waveform names from catalog
                    // Catalog format: "SINC","NEG_RAMP","EXP_RISE","EXP_FALL","CARDIAC","VOLATILE","ARB_1","ARB_2"
                    std::vector<std::string> userWaveforms;
                    std::string builtinNames[] = {"SINC", "NEG_RAMP", "EXP_RISE", "EXP_FALL", "CARDIAC"};
                    
                    // Simple parsing: find quoted strings that aren't built-in names
                    size_t pos = 0;
                    while ((pos = catalog.find('"', pos)) != std::string::npos)
                    {
                        size_t endPos = catalog.find('"', pos + 1);
                        if (endPos != std::string::npos)
                        {
                            std::string wfName = catalog.substr(pos + 1, endPos - pos - 1);
                            bool isBuiltin = false;
                            for (const auto& builtin : builtinNames)
                            {
                                if (wfName == builtin)
                                {
                                    isBuiltin = true;
                                    break;
                                }
                            }
                            if (!isBuiltin && wfName != "VOLATILE")
                            {
                                userWaveforms.push_back(wfName);
                            }
                            pos = endPos + 1;
                        }
                        else
                        {
                            break;
                        }
                    }
                    
                    if (!userWaveforms.empty())
                    {
                        // Delete the first user waveform found (preferably not the target name)
                        std::string wfToDelete = userWaveforms[0];
                        if (logCallback)
                            logCallback("Deleting waveform '" + wfToDelete + "' to free memory slot...");
                        
                        std::string deleteCmd = "DATA:DELete " + wfToDelete;
                        write(deleteCmd);
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        std::string delError = queryError();
                        
                        if (logCallback)
                            logCallback("DATA:DELete " + wfToDelete + " -> " + delError);
                        
                        if (delError.find("+787") != std::string::npos)
                        {
                            // Still active - try switching again
                            write("FUNCtion:SIN");
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            queryError();
                            
                            write(deleteCmd);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            delError = queryError();
                            
                            if (logCallback)
                                logCallback("DATA:DELete " + wfToDelete + " (retry) -> " + delError);
                        }
                    }
                    else
                    {
                        // No user waveforms found - this shouldn't happen if memory is full
                        // But if it does, we'll need to delete VOLATILE and re-upload
                        if (logCallback)
                            logCallback("Warning: No user waveforms found in catalog, but memory is full.");
                        needReupload = true;  // Will re-upload VOLATILE after copy attempt
                    }
                }
                
                // Check if VOLATILE was deleted (it shouldn't be, but check just in case)
                // If we deleted the target name and it was using VOLATILE, we need to re-upload
                // Actually, we only delete non-volatile waveforms, so VOLATILE should still be there
                // But let's verify by trying the copy - if it fails with +780, we'll re-upload
            }
            else if (error.find("+780") != std::string::npos)
            {
                // VOLATILE not loaded - re-upload
                if (logCallback)
                    logCallback("VOLATILE memory not found. Re-uploading...");
                needReupload = true;
            }
            else
            {
                // Other error - log and potentially fall back to VOLATILE
                if (logCallback)
                {
                    logCallback("DATA:COPY " + name + " -> " + error);
                    if (error.find("+785") != std::string::npos)
                    {
                        logCallback("Waveform doesn't exist. Using VOLATILE memory.");
                        useVolatile = true;
                    }
                    else
                    {
                        logCallback("Error: Failed to copy waveform to device.");
                        return;  // Don't proceed if copy failed for other reasons
                    }
                }
            }
        }
        
        // Re-upload to VOLATILE if needed (only if +780 error occurred)
        if (needReupload)
        {
            // Re-send the VOLATILE upload command
            status = viPrintf(sess, "%s\n", cmdStr.c_str());
            
            if (status != VI_SUCCESS)
            {
                if (logCallback)
                    logCallback("ARB re-upload failed: VISA write error " + std::to_string(status));
                return;
            }
            
            if (viFlush) viFlush(sess, VI_FLUSH_ON_WRITE);
            
            // Give device time to process
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            error = queryError();
            
            if (logCallback)
            {
                if (error.find("No error") != std::string::npos || error.find("+0") != std::string::npos)
                {
                    logCallback("DATA VOLATILE (re-upload) -> [Uploaded " + std::to_string(finalData.size()) + " points]");
                }
                else
                {
                    logCallback("DATA VOLATILE (re-upload) -> " + error);
                    return;  // Don't proceed if re-upload failed
                }
            }
        }
        
        // If we deleted a waveform or re-uploaded, try copying again
        if (!copySucceeded && (error.find("+781") != std::string::npos || needReupload))
        {
            copyCmd = "DATA:COPY " + name + ",VOLATILE";
            write(copyCmd);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            error = queryError();
            
            if (logCallback)
                logCallback("DATA:COPY " + name + " (retry) error check: " + error);
            
            // Verify via catalog
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            verifyCatalog = query("DATA:NVOLatile:CATalog?");
            copyVerified = (verifyCatalog.find("\"" + upperName + "\"") != std::string::npos);
            
            if (logCallback)
                logCallback("DATA:NVOLatile:CATalog? (retry verify) -> " + verifyCatalog);
            
            hasErrorCode = (error.find("+781") != std::string::npos || 
                            error.find("+785") != std::string::npos ||
                            error.find("+787") != std::string::npos ||
                            error.find("+786") != std::string::npos ||
                            error.find("+782") != std::string::npos ||
                            error.find("+783") != std::string::npos ||
                            error.find("+780") != std::string::npos);
            
            copySucceeded = copyVerified && !hasErrorCode;
        }
        
        // Log final copy result
        if (logCallback)
        {
            if (copySucceeded)
            {
                logCallback("DATA:COPY " + name + " -> [Copied to non-volatile memory]");
            }
            else
            {
                logCallback("DATA:COPY " + name + " -> " + error);
                if (error.find("+781") != std::string::npos)
                {
                    logCallback("Error: Memory still full after cleanup. Using VOLATILE memory.");
                    logCallback("Note: VOLATILE memory is lost on power cycle.");
                    useVolatile = true;  // Fall back to VOLATILE
                }
                else if (error.find("+780") != std::string::npos)
                {
                    logCallback("Error: VOLATILE memory lost. Using VOLATILE directly.");
                    useVolatile = true;  // Fall back to VOLATILE
                }
                else
                {
                    logCallback("Error: Failed to copy waveform to device.");
                    return;  // Don't proceed if copy failed for other reasons
                }
            }
        }
        
        // Step 3: Select the waveform
        // If we're using VOLATILE, select VOLATILE instead of the named waveform
        if (useVolatile)
        {
            write("FUNCtion:USER VOLATILE");
        }
        else
        {
            write("FUNCtion:USER " + name);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        error = queryError();
        
        if (logCallback)
        {
            if (error.find("No error") != std::string::npos || error.find("+0") != std::string::npos)
            {
                if (useVolatile)
                {
                    logCallback("FUNCtion:USER VOLATILE -> [Selected (using volatile memory)]");
                }
                else
                {
                    logCallback("FUNCtion:USER " + name + " -> [Selected]");
                }
            }
            else
            {
                logCallback("FUNCtion:USER " + (useVolatile ? std::string("VOLATILE") : name) + " -> " + error);
            }
        }
        
        // Step 4: Set shape to USER (optional but recommended)
        write("FUNCtion:SHAPe USER");
    }
    catch (const std::exception& e)
    {
        if (logCallback)
            logCallback("ARB upload exception: " + std::string(e.what()));
    }
    catch (...)
    {
        if (logCallback)
            logCallback("ARB upload failed: Unknown error");
    }
}

std::vector<float> HP33120ADriver::queryARBWaveform(const std::string& /*name*/)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    std::vector<float> result;
    
    if (!connected) return result;
    
    // HP33120A does not support querying ARB waveform data via SCPI
    // This command is not in the manual and would cause read errors
    // Return empty - ARB data must be tracked manually in the plugin
    
    return result;
}

bool HP33120ADriver::deleteARBWaveform(const std::string& /*name*/)
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    if (!connected) return false;
    
    // HP33120A doesn't have a DELETE command for ARBs
    // The device has 4 memory slots, and uploading with the same name overwrites
    // However, to be safe, we can try to clear it by uploading zeros or a minimal waveform
    // But actually, the device will just overwrite when we upload a new ARB with the same name
    // So we don't need to delete - just upload will overwrite
    
    // Return true to indicate "handled" (we'll overwrite on next upload)
    return true;
}

std::vector<std::string> HP33120ADriver::listARBNames()
{
    // Use the catalog query to get ARB names
    return queryWaveformCatalog();
}

std::vector<std::string> HP33120ADriver::queryWaveformCatalog()
{
    std::lock_guard<std::recursive_mutex> lock(driverMutex);
    std::vector<std::string> result;
    
    if (!connected) return result;
    
    // Query DATA:CATalog? to get all available waveforms
    // Response format: "SINC","NEG_RAMP","EXP_RISE","EXP_FALL","CARDIAC","VOLATILE","ARB_1","ARB_2"
    std::string catalog = query("DATA:CATalog?");
    
    // Also query DATA:NVOLatile:CATalog? for non-volatile user waveforms
    std::string nvCatalog = query("DATA:NVOLatile:CATalog?");
    
    // Log raw responses for debugging
    if (logCallback)
    {
        logCallback("DATA:CATalog? -> " + (catalog.empty() ? "(empty)" : catalog));
        if (!nvCatalog.empty())
            logCallback("DATA:NVOLatile:CATalog? -> " + nvCatalog);
    }
    
    if (catalog.empty()) return result;
    
    // Parse quoted strings from the response
    // Format: "NAME1","NAME2","NAME3"
    auto parseQuotedStrings = [](const std::string& str, std::vector<std::string>& out) {
        size_t pos = 0;
        while (pos < str.length())
        {
            // Find opening quote
            size_t startQuote = str.find('"', pos);
            if (startQuote == std::string::npos) break;
            
            // Find closing quote
            size_t endQuote = str.find('"', startQuote + 1);
            if (endQuote == std::string::npos) break;
            
            // Extract waveform name (without quotes)
            std::string waveformName = str.substr(startQuote + 1, endQuote - startQuote - 1);
            if (!waveformName.empty())
            {
                out.push_back(waveformName);
            }
            
            // Move past this quoted string and any comma/whitespace
            pos = endQuote + 1;
            while (pos < str.length() && (str[pos] == ',' || str[pos] == ' ' || str[pos] == '\n' || str[pos] == '\r'))
                pos++;
        }
    };
    
    parseQuotedStrings(catalog, result);
    
    // Also parse non-volatile catalog and add any waveforms not already in result
    if (!nvCatalog.empty())
    {
        std::vector<std::string> nvResult;
        parseQuotedStrings(nvCatalog, nvResult);
        for (const auto& wf : nvResult)
        {
            bool found = false;
            for (const auto& existing : result)
            {
                if (existing == wf)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                result.push_back(wf);
            }
        }
    }
    
    return result;
}

// Live Updates (LFO)
void HP33120ADriver::updateFrequencyLive(double freqHz) { setFrequency(freqHz); }
void HP33120ADriver::updateAmplitudeLive(double ampVpp) { setAmplitude(ampVpp); }
void HP33120ADriver::updateDutyCycleLive(double duty) { setDutyCycle(duty); }
void HP33120ADriver::updateAMDepthLive(double depth) { setAMDepth(depth); }
void HP33120ADriver::updateFMDevLive(double devHz) { setFMDeviation(devHz); }