cmake_minimum_required(VERSION 3.22)

project(HP33120A_VST VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE
add_subdirectory(JUCE)

# Plugin configuration - build both VST3 and Standalone
juce_add_plugin(HP33120A_VST
    COMPANY_NAME "YourCompany"
    PLUGIN_MANUFACTURER_CODE YcCm
    PLUGIN_CODE Hp33
    FORMATS VST3 Standalone
    PRODUCT_NAME "HP33120A Function Generator"
    DESCRIPTION "VST3 plugin and standalone app for controlling HP33120A Function Generator"
    VERSION 1.0.0
    NEEDS_MIDI_INPUT TRUE
    IS_SYNTH TRUE
    IS_RESIZABLE YES
    COPY_PLUGIN_AFTER_BUILD YES
)

# Link required JUCE modules
target_link_libraries(HP33120A_VST PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_data_structures
)

# Source files
target_sources(HP33120A_VST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/HP33120ADriver.cpp
    Source/HP33120ADriver.h
    Source/ARBManager.cpp
    Source/ARBManager.h
    Source/Parameters.h
)

# VISA library configuration
if(WIN32)
    # Windows - try to find VISA
    find_path(VISA_INCLUDE_DIR
        NAMES visa.h
        PATHS
            "C:/Program Files/IVI Foundation/VISA/Win64/Include"
            "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/Include"
            "C:/Program Files/National Instruments/VISA/Win64/Include"
            "C:/Program Files (x86)/National Instruments/VISA/WinNT/Include"
    )
    
    # For x64 builds, prefer visa64, for x86 builds use visa32
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        find_library(VISA_LIBRARY
            NAMES visa64
            PATHS
                "C:/Program Files/IVI Foundation/VISA/Win64/Lib/msc"
                "C:/Program Files/National Instruments/VISA/Win64/Lib/msc"
            NO_DEFAULT_PATH
        )
        if(NOT VISA_LIBRARY)
            find_library(VISA_LIBRARY
                NAMES visa64
                PATHS
                    "C:/Program Files/IVI Foundation/VISA/Win64/Lib/msc"
                    "C:/Program Files/National Instruments/VISA/Win64/Lib/msc"
            )
        endif()
    else()
        # 32-bit build
        find_library(VISA_LIBRARY
            NAMES visa32
            PATHS
                "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/Lib/msc"
                "C:/Program Files (x86)/National Instruments/VISA/WinNT/Lib/msc"
            NO_DEFAULT_PATH
        )
        if(NOT VISA_LIBRARY)
            find_library(VISA_LIBRARY
                NAMES visa32
                PATHS
                    "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/Lib/msc"
                    "C:/Program Files (x86)/National Instruments/VISA/WinNT/Lib/msc"
            )
        endif()
    endif()
    
    if(VISA_INCLUDE_DIR AND VISA_LIBRARY)
        target_include_directories(HP33120A_VST PRIVATE ${VISA_INCLUDE_DIR})
        target_link_libraries(HP33120A_VST PRIVATE ${VISA_LIBRARY})
        message(STATUS "Found VISA: ${VISA_LIBRARY}")
    else()
        message(WARNING "VISA library not found. Plugin will compile but VISA functions may not work.")
        message(STATUS "Please install NI-VISA or IVI-VISA and set VISA_INCLUDE_DIR and VISA_LIBRARY")
    endif()
    
elseif(APPLE)
    # macOS
    find_path(VISA_INCLUDE_DIR
        NAMES visa.h
        PATHS
            /usr/local/include
            /opt/local/include
            /usr/include
    )
    
    find_library(VISA_LIBRARY
        NAMES visa
        PATHS
            /usr/local/lib
            /opt/local/lib
            /usr/lib
    )
    
    if(VISA_INCLUDE_DIR AND VISA_LIBRARY)
        target_include_directories(HP33120A_VST PRIVATE ${VISA_INCLUDE_DIR})
        target_link_libraries(HP33120A_VST PRIVATE ${VISA_LIBRARY})
        message(STATUS "Found VISA: ${VISA_LIBRARY}")
    else()
        message(WARNING "VISA library not found. Install via: brew install libvisa")
    endif()
    
else()
    # Linux
    find_path(VISA_INCLUDE_DIR
        NAMES visa.h
        PATHS
            /usr/include
            /usr/local/include
    )
    
    find_library(VISA_LIBRARY
        NAMES visa
        PATHS
            /usr/lib
            /usr/local/lib
            /usr/lib/x86_64-linux-gnu
    )
    
    if(VISA_INCLUDE_DIR AND VISA_LIBRARY)
        target_include_directories(HP33120A_VST PRIVATE ${VISA_INCLUDE_DIR})
        target_link_libraries(HP33120A_VST PRIVATE ${VISA_LIBRARY})
        message(STATUS "Found VISA: ${VISA_LIBRARY}")
    else()
        message(WARNING "VISA library not found. Install via: sudo apt-get install libvisa-dev")
    endif()
endif()

# Compiler-specific settings
if(MSVC)
    target_compile_options(HP33120A_VST PRIVATE /W4)
else()
    target_compile_options(HP33120A_VST PRIVATE -Wall -Wextra -Wpedantic)
endif()

