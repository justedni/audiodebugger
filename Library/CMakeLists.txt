cmake_minimum_required(VERSION 3.15)

project(AudioDebuggerLib VERSION 1.0.0)

set(CORE_INTERFACE
    include/core/AudioDebug.h
    include/core/DebugProcessor.h
)

set(JUCE_INTERFACE
    include/juce/AudioDebuggerUI.h
    include/juce/CustomSlider.h
)

set(CORE_SOURCE
    source/core/AudioDebug.cpp
    source/core/DebugProcessor.cpp
    source/core/FileWriter.cpp
    source/core/FileWriter.h
    source/core/ReportFile.cpp
    source/core/ReportFile.h
)

set(JUCE_SOURCE
    source/juce/AudioDebuggerUI.cpp
    source/juce/ChannelComponent.cpp
    source/juce/ChannelComponent.h
    source/juce/ControlPanel.cpp
    source/juce/ControlPanel.h
    source/juce/CustomSlider.cpp
    source/juce/DebuggerSpawner.cpp
    source/juce/DiffDetector.cpp
    source/juce/DiffDetector.h
    source/juce/JuceDebugProcessor.cpp
    source/juce/MarkerStore.cpp
    source/juce/MarkerStore.h
    source/juce/MipmapBuilder.cpp
    source/juce/MipmapBuilder.h
    source/juce/Mipmaps.cpp
    source/juce/Mipmaps.h
    source/juce/OptionPanel.cpp
    source/juce/OptionPanel.h
    source/juce/OverlayComponent.cpp
    source/juce/OverlayComponent.h
    source/juce/RingBuffer.h
    source/juce/WaveformOverview.cpp
    source/juce/WaveformOverview.h
)

set(ALL_SOURCES
    ${CORE_INTERFACE}
    ${JUCE_INTERFACE}
    ${CORE_SOURCE}
    ${JUCE_SOURCE}
)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    ${ALL_SOURCES}
)

target_compile_definitions(${PROJECT_NAME}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES)


source_group("interface" FILES ${CORE_INTERFACE} ${JUCE_INTERFACE})
source_group("source" FILES ${CORE_SOURCE} ${JUCE_SOURCE})
