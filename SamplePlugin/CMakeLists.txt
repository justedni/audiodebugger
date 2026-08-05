cmake_minimum_required(VERSION 3.15)

project(SamplePlugin VERSION 1.0.0)

set(SAMPLE_PLUGIN_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIG>)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${SAMPLE_PLUGIN_BIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${SAMPLE_PLUGIN_BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${SAMPLE_PLUGIN_BIN_DIR})

juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME "Vincent Dortel"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE vdor
    PLUGIN_CODE demo
    FORMATS VST3
    NEEDS_WEB_BROWSER FALSE
    PRODUCT_NAME SamplePlugin)

juce_add_binary_data(SamplePluginData
    SOURCES
        Resource/StStrings_64.wav)

set_target_properties(SamplePluginData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(${PROJECT_NAME}
    PRIVATE
        Source/SamplePluginEditor.cpp
        Source/SamplePluginEditor.h
        Source/SamplePluginProcessor.cpp
        Source/SamplePluginProcessor.h
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        JUCE_VST3_CAN_REPLACE_VST2=0)


target_link_libraries(${PROJECT_NAME}
    PRIVATE
        AudioDebugJuce
        SamplePluginData
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    LIBRARY_OUTPUT_DIRECTORY ${SAMPLE_PLUGIN_BIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${SAMPLE_PLUGIN_BIN_DIR}
)
