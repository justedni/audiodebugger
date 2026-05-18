cmake_minimum_required(VERSION 3.15)

project(AudioDebuggerApp VERSION 1.0.0)

juce_add_gui_app(${PROJECT_NAME}
    COMPANY_NAME "Vincent Dortel"
    PRODUCT_NAME "${PROJECT_NAME}"
    BUNDLE_ID "com.vincentdortel.audiodebugger")

target_sources(${PROJECT_NAME} PRIVATE
    Source/Main.cpp
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:${PROJECT_NAME},JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:${PROJECT_NAME},JUCE_VERSION>"
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../Library/source
        ${CMAKE_CURRENT_SOURCE_DIR}/../Library/include
)

set(PLATFORM_DIR "x64")

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        AudioDebuggerLib
        juce::juce_core
        juce::juce_audio_basics
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
)
