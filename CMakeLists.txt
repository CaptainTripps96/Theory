cmake_minimum_required(VERSION 3.22)

project(TheorySequencer
    VERSION 0.1.0
    DESCRIPTION "Theory-aware MIDI sequencer and future DAW"
    LANGUAGES C CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TSQ_BUILD_TESTS "Build TheorySequencer tests" ON)
option(TSQ_BUILD_APP "Build the JUCE desktop app, UI, engine, and plugin/audio targets" ON)
option(TSQ_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer where supported" OFF)
option(TSQ_WARNINGS_AS_ERRORS "Treat TheorySequencer warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)
include(Warnings)
include(Sanitizers)
include(Dependencies)

add_subdirectory(src)

include(CTest)
if(TSQ_BUILD_TESTS AND BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
