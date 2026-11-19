cmake_minimum_required(VERSION 3.22)

# macOS: one "universal" build that runs natively on Apple Silicon and Intel Macs,
# for macOS 10.13 and newer. (Ignored on Windows.) Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
project(PeakSlayer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Windows: build the Microsoft C++ runtime into the plugin, so it runs on PCs
# that don't have Visual Studio or the VC++ Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PeakSlayer
    COMPANY_NAME "Noise Division"                    # shown as the vendor in FL Studio and other hosts
    COMPANY_COPYRIGHT "Copyright 2026 Noise Division"
    BUNDLE_ID com.noisedivision.peakslayer
    PLUGIN_MANUFACTURER_CODE Nsdv                    # 4 letters, first one upper case; the same for all Noise Division plugins
    PLUGIN_CODE Pksl
    FORMATS VST3 AU Standalone                       # AU is built on macOS only (Logic, GarageBand)
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Dynamics Distortion                # "Fx" = effect (not a synth/instrument)
    PRODUCT_NAME "PEAKSLAYER")

# The logo is compiled into the plugin, so the .vst3 needs no extra files.
juce_add_binary_data(PeakSlayerAssets SOURCES
    Assets/peakslayer_logo.png)

target_sources(PeakSlayer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PeakSlayer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PeakSlayer
    PRIVATE
        PeakSlayerAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
