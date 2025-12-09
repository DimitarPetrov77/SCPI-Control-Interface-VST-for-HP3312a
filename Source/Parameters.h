#pragma once

namespace Parameters
{
    // Connection
    constexpr const char* GPIB_ADDRESS = "GPIB Address";
    
    // Basic Settings
    constexpr const char* WAVEFORM = "Waveform";
    constexpr const char* FREQUENCY = "Frequency";
    constexpr const char* AMPLITUDE = "Amplitude";
    constexpr const char* OFFSET = "Offset";
    constexpr const char* PHASE = "Phase";
    constexpr const char* DUTY_CYCLE = "Duty Cycle";
    constexpr const char* OUTPUT_ENABLED = "Output Enabled";
    
    // AM Settings
    constexpr const char* AM_ENABLED = "AM Enabled";
    constexpr const char* AM_DEPTH = "AM Depth";
    constexpr const char* AM_SOURCE = "AM Source";
    constexpr const char* AM_INT_WAVEFORM = "AM Int Waveform";
    constexpr const char* AM_INT_FREQ = "AM Int Frequency";
    
    // FM Settings
    constexpr const char* FM_ENABLED = "FM Enabled";
    constexpr const char* FM_DEVIATION = "FM Deviation";
    constexpr const char* FM_SOURCE = "FM Source";
    constexpr const char* FM_INT_WAVEFORM = "FM Int Waveform";
    constexpr const char* FM_INT_FREQ = "FM Int Frequency";
    
    // FSK Settings
    constexpr const char* FSK_ENABLED = "FSK Enabled";
    constexpr const char* FSK_FREQUENCY = "FSK Frequency";
    constexpr const char* FSK_SOURCE = "FSK Source";
    constexpr const char* FSK_RATE = "FSK Rate";
    
    // Sweep Settings
    constexpr const char* SWEEP_ENABLED = "Sweep Enabled";
    constexpr const char* SWEEP_START = "Sweep Start";
    constexpr const char* SWEEP_STOP = "Sweep Stop";
    constexpr const char* SWEEP_TIME = "Sweep Time";
    
    // Burst Settings
    constexpr const char* BURST_ENABLED = "Burst Enabled";
    constexpr const char* BURST_CYCLES = "Burst Cycles";
    constexpr const char* BURST_PHASE = "Burst Phase";
    constexpr const char* BURST_INT_PERIOD = "Burst Int Period";
    constexpr const char* BURST_SOURCE = "Burst Source";
    
    // Sync Settings
    constexpr const char* SYNC_ENABLED = "Sync Enabled";
    constexpr const char* SYNC_PHASE = "Sync Phase";
    
    // Trigger Settings
    constexpr const char* TRIGGER_SOURCE = "Trigger Source";
    
    // ARB Slot Parameters (4 slots)
    constexpr const char* ARB_SLOT1_NAME = "ARB Slot 1 Name";
    constexpr const char* ARB_SLOT1_POINTS = "ARB Slot 1 Points";
    constexpr const char* ARB_SLOT2_NAME = "ARB Slot 2 Name";
    constexpr const char* ARB_SLOT2_POINTS = "ARB Slot 2 Points";
    constexpr const char* ARB_SLOT3_NAME = "ARB Slot 3 Name";
    constexpr const char* ARB_SLOT3_POINTS = "ARB Slot 3 Points";
    constexpr const char* ARB_SLOT4_NAME = "ARB Slot 4 Name";
    constexpr const char* ARB_SLOT4_POINTS = "ARB Slot 4 Points";
}

