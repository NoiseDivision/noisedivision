cmake_minimum_required(VERSION 3.22)

# macOS: one "universal" build that runs natively on Apple Silicon and Intel Macs,
# for macOS 10.13 and newer. (Ignored on Windows.) Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
project(SPECTRAKILL VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Windows: build the Microsoft C++ runtime into the plugin, so it runs on PCs
# without Visual Studio or the VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SPECTRAKILL
    COMPANY_NAME "Noise Division"                    # shown as the vendor in FL Studio and other hosts
    COMPANY_COPYRIGHT "Copyright 2026 Noise Division"
    BUNDLE_ID com.noisedivision.spectrakill
    PLUGIN_MANUFACTURER_CODE Nsdv                    # the same for all Noise Division plugins
    PLUGIN_CODE Spkl
    FORMATS VST3 AU Standalone                       # AU is built on macOS only (Logic, GarageBand)
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Dynamics                      # "Fx" = effect
    PRODUCT_NAME "SPECTRAKILL")

# Logo and fonts are compiled into the plugin.
# Oswald and Montserrat: SIL Open Font License (Assets/*-OFL.txt). Special Elite: Apache 2.0 (Assets/SpecialElite-LICENSE.txt).
juce_add_binary_data(SpectraKillAssets SOURCES
    Assets/spectrakill_logo.png
    Assets/Oswald-Medium.ttf
    Assets/Oswald-SemiBold.ttf
    Assets/Oswald-Bold.ttf
    Assets/SpecialElite-Regular.ttf
    Assets/Montserrat-Regular.ttf
    Assets/Montserrat-SemiBold.ttf)

target_sources(SPECTRAKILL PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SPECTRAKILL PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SPECTRAKILL
    PRIVATE
        SpectraKillAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
