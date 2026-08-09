cmake_minimum_required(VERSION 3.15)

# The JUCE layer is optional. A consumer that only wants capture
# can configure this project with -DAUDIODEBUG_BUILD_JUCE=OFF
option(AUDIODEBUG_BUILD_JUCE "Build the JUCE UI and helper layer" ON)


set(CORE_INTERFACE
    include/core/AudioDebug.h
    include/core/AudioDebugInterface.h
    include/core/DiffDetector.h
    include/core/RptFileFormat.h
    include/core/UnitTests.h
)

set(CORE_SOURCE
    source/core/AudioDebug.cpp
    source/core/AudioDebugger.cpp
    source/core/AudioDebugger.h
    source/core/DebuggerAccess.h
    source/core/DebugProcessor.cpp
    source/core/DiffDetector.cpp
    source/core/FileWriter.cpp
    source/core/FileWriter.h
    source/core/ReportFile.cpp
    source/core/ReportFile.h
    source/core/RingBuffer.h
    source/core/UnitTests.cpp
)

add_library(AudioDebugCore STATIC)

target_sources(AudioDebugCore PRIVATE ${CORE_INTERFACE} ${CORE_SOURCE})

target_include_directories(AudioDebugCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
)

set_target_properties(AudioDebugCore PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON)

source_group("interface" FILES ${CORE_INTERFACE})
source_group("source"    FILES ${CORE_SOURCE})


if(AUDIODEBUG_BUILD_JUCE)

    set(JUCE_INTERFACE
        include/juce/AudioDebuggerUI.h
        include/juce/ChannelComponent.h
        include/juce/CustomSlider.h
        include/juce/DebuggerSpawner.h
        include/juce/JuceDebugProcessor.h
    )

    set(JUCE_SOURCE
        source/juce/AudioDebuggerUI.cpp
        source/juce/ChannelComponent.cpp
        source/juce/ControlPanel.cpp
        source/juce/ControlPanel.h
        source/juce/CustomSlider.cpp
        source/juce/DebuggerSpawner.cpp
        source/juce/DiffTrackComponent.cpp
        source/juce/DiffTrackComponent.h
        source/juce/JuceDebugProcessor.cpp
        source/juce/MarkerStore.cpp
        source/juce/MarkerStore.h
        source/juce/MidiTrackComponent.cpp
        source/juce/MidiTrackComponent.h
        source/juce/MipmapBuilder.cpp
        source/juce/MipmapBuilder.h
        source/juce/Mipmaps.cpp
        source/juce/Mipmaps.h
        source/juce/OptionPanel.cpp
        source/juce/OptionPanel.h
        source/juce/OverlayComponent.cpp
        source/juce/OverlayComponent.h
        source/juce/WaveformOverview.cpp
        source/juce/WaveformOverview.h
    )

    add_library(AudioDebugJuce STATIC)

    target_sources(AudioDebugJuce PRIVATE ${JUCE_INTERFACE} ${JUCE_SOURCE})

    target_include_directories(AudioDebugJuce
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/source
    )

    target_link_libraries(AudioDebugJuce
        PUBLIC
            AudioDebugCore
            juce::juce_core
            juce::juce_data_structures
            juce::juce_events
            juce::juce_graphics
            juce::juce_gui_basics
    )

    target_compile_definitions(AudioDebugJuce
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_DISPLAY_SPLASH_SCREEN=0
            JUCE_REPORT_APP_USAGE=0
            JUCE_STRICT_REFCOUNTEDPOINTER=1
    )

    set_target_properties(AudioDebugJuce PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE ON)

    source_group("interface" FILES ${JUCE_INTERFACE})
    source_group("source"    FILES ${JUCE_SOURCE})

endif()
