cmake_minimum_required(VERSION 3.15)
project(AudioDebugger)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
        add_subdirectory(../JUCE ${CMAKE_CURRENT_BINARY_DIR}/JUCE)
    else()
        add_subdirectory(JUCE)
    endif()
endif()

add_subdirectory(Library)
add_subdirectory(Standalone)
add_subdirectory(SamplePlugin)
