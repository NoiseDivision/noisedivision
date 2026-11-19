cmake_minimum_required(VERSION 3.22)

# macOS: one "universal" build that runs natively on Apple Silicon and Intel Macs,
# for macOS 10.13 and newer. (Ignored on Windows.) Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS supported")
project(MASTERFUSE VERSION 1.0.0)

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

juce_add_plugin(MASTERFUSE
    COMPANY_NAME "Noise Division"                    # shown as the vendor in FL Studio and other hosts
    COMPANY_COPYRIGHT "Copyright 2026 Noise Division"
    BUNDLE_ID com.noisedivision.masterfuse
    PLUGIN_MANUFACTURER_CODE Nsdv                    # 4 letters, first one upper case; the same for all Noise Division plugins
    PLUGIN_CODE Tknb                                 # kept from The Knob, so existing projects still find the plugin
    FORMATS VST3 AU Standalone                       # AU is built on macOS only (Logic, GarageBand)
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Mastering Dynamics                # "Fx" = effect (not a synth/instrument)
    PRODUCT_NAME "MASTERFUSE")

# Artwork and fonts are compiled into the plugin.
# The images are rendered from Artwork/mock.html (see Artwork/art.js).
# Chakra Petch and Montserrat are licensed under the SIL Open Font License (see Assets/*-OFL.txt).
juce_add_binary_data(MasterFuseAssets SOURCES
    Assets/background.png
    Assets/knob.png
    Assets/knob_small.png
    Assets/fuse_on.png
    Assets/fuse_off.png
    Assets/ChakraPetch-SemiBold.ttf
    Assets/Montserrat.ttf)

target_sources(MASTERFUSE PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(MASTERFUSE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MASTERFUSE
    PRIVATE
        MasterFuseAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
