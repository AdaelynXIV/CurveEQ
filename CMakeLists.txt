cmake_minimum_required(VERSION 3.22)
project(CurveEQ VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(CurveEQ
    COMPANY_NAME "CurveAudio"
    PLUGIN_MANUFACTURER_CODE Crve
    PLUGIN_CODE Ceq1
    FORMATS VST3
    PRODUCT_NAME "Curve EQ"
    VST3_CATEGORIES Fx EQ
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(CurveEQ PRIVATE
    Source/Dsp.h
    Source/PluginProcessor.h Source/PluginProcessor.cpp
    Source/PluginEditor.h Source/PluginEditor.cpp)

target_compile_definitions(CurveEQ PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(CurveEQ
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(CURVEEQ_BUILD_TESTS "Build headless DSP tests" OFF)
if(CURVEEQ_BUILD_TESTS)
    add_executable(EqTest Tests/main.cpp)
    target_include_directories(EqTest PRIVATE Source)
    target_link_libraries(EqTest PRIVATE CurveEQ juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()

if(CURVEEQ_BUILD_TESTS)
    add_executable(EqSnapshot Tests/snapshot.cpp)
    target_include_directories(EqSnapshot PRIVATE Source)
    target_link_libraries(EqSnapshot PRIVATE CurveEQ juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
