cmake_minimum_required(VERSION 3.15)

set(CONSOLE_SOURCES
    Source/main.cpp
    Source/DemoProcessor.cpp
    Source/DemoProcessor.h
    Source/DemoDsp.cpp
    Source/DemoDsp.h
    Source/TestSignal.h
)

add_executable(ConsoleSample
    ${CONSOLE_SOURCES}
)

target_link_libraries(ConsoleSample PRIVATE AudioDebugCore)

set_target_properties(ConsoleSample PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(MSVC)
    target_compile_options(ConsoleSample PRIVATE /W4)
else()
    target_compile_options(ConsoleSample PRIVATE -Wall -Wextra -Wpedantic)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES
    ${CONSOLE_SOURCES}
)
