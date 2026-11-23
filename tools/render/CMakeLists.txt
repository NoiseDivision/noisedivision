# Builds one command-line renderer per plugin, from the plugins' own sources.
#   cmake -B build -DCMAKE_BUILD_TYPE=Release [-DJUCE_DIR=/path/to/JUCE]
#   cmake --build build -j
cmake_minimum_required(VERSION 3.22)
project(NDRender VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4)
  FetchContent_MakeAvailable(JUCE)
endif()
set(P ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins)

# JUCE itself is compiled once and shared by all six renderers
add_library(ndjuce STATIC)
target_link_libraries(ndjuce PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
target_compile_definitions(ndjuce PUBLIC JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
target_include_directories(ndjuce INTERFACE $<TARGET_PROPERTY:ndjuce,INCLUDE_DIRECTORIES>)
target_compile_definitions(ndjuce INTERFACE $<TARGET_PROPERTY:ndjuce,COMPILE_DEFINITIONS>)

function(nd_renderer name dir cls presets curveprop)
  file(GLOB assets ${P}/${dir}/Assets/*.png ${P}/${dir}/Assets/*.ttf)
  juce_add_binary_data(${name}_data SOURCES ${assets})
  add_executable(${name}_render render.cpp ${P}/${dir}/Source/PluginProcessor.cpp ${P}/${dir}/Source/PluginEditor.cpp)
  target_include_directories(${name}_render PRIVATE ${P}/${dir}/Source)
  target_compile_definitions(${name}_render PRIVATE PROC_HEADER="PluginProcessor.h" PROC_CLASS=${cls} HAS_PRESETS=${presets})
  target_link_libraries(${name}_render PRIVATE ${name}_data ndjuce)
  if(curveprop)
    target_compile_definitions(${name}_render PRIVATE CURVE_PROP="${curveprop}")
  endif()
  set_target_properties(${name}_render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endfunction()

nd_renderer(peakslayer  PeakSlayer  PeakSlayerProcessor  1 "")
nd_renderer(krush       KRUSH       KrushProcessor       1 "")
nd_renderer(masterfuse  MASTERFUSE  MasterFuseProcessor  0 "")
nd_renderer(spectrakill SPECTRAKILL SpectraKillProcessor 1 db)
nd_renderer(dark        DARK        DarkProcessor        0 "")
nd_renderer(shrapnel    SHRAPNEL    ShrapnelProcessor    1 amount)
nd_renderer(recoil      RECOIL      RecoilProcessor      1 "")
