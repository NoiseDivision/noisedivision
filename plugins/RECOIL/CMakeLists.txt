cmake_minimum_required(VERSION 3.22)

# macOS: one "universal" build that runs natively on Apple Silicon and Intel Macs,
# for macOS 10.13 and newer. (Ignored on Windows.) Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
project(RECOIL VERSION 1.1.0)

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

juce_add_plugin(RECOIL
    COMPANY_NAME "Noise Division"                    # shown as the vendor in FL Studio and other hosts
    COMPANY_COPYRIGHT "Copyright 2026 Noise Division"
    BUNDLE_ID com.noisedivision.recoil
    PLUGIN_MANUFACTURER_CODE Nsdv                    # the same for all Noise Division plugins
    PLUGIN_CODE Rcol
    FORMATS VST3 AU Standalone                       # AU is built on macOS only (Logic, GarageBand)
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE                            # MIDI notes can be the trigger
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Dynamics                      # "Fx" = effect
    PRODUCT_NAME "RECOIL")

# Logo and fonts are compiled into the plugin.
# Chakra Petch and Montserrat: SIL Open Font License (Assets/*-OFL.txt).
juce_add_binary_data(RecoilAssets SOURCES
    Assets/recoil_logo.png
    Assets/ChakraPetch-SemiBold.ttf
    Assets/ChakraPetch-Bold.ttf
    Assets/Montserrat-SemiBold.ttf)

target_sources(RECOIL PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(RECOIL PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)                       # MAP can read an MP3 bounce too

target_link_libraries(RECOIL
    PRIVATE
        RecoilAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
