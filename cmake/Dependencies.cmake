set(TSQ_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps" CACHE PATH "Directory containing pinned third-party dependencies")
set(TSQ_JUCE_DIR "${TSQ_DEPS_DIR}/JUCE" CACHE PATH "Local JUCE checkout")
set(TSQ_TRACKTION_ENGINE_DIR "${TSQ_DEPS_DIR}/tracktion_engine" CACHE PATH "Local Tracktion Engine checkout")
set(TSQ_CATCH2_DIR "${TSQ_DEPS_DIR}/Catch2" CACHE PATH "Local Catch2 checkout")

set(TSQ_MISSING_DEPENDENCIES "")

set(TSQ_REQUIRED_DEPENDENCY_DIRS "")
set(TSQ_EXPECTED_DEPENDENCY_LAYOUT "")
if(TSQ_BUILD_APP)
    list(APPEND TSQ_REQUIRED_DEPENDENCY_DIRS
        "${TSQ_JUCE_DIR}"
        "${TSQ_TRACKTION_ENGINE_DIR}"
    )
    list(APPEND TSQ_EXPECTED_DEPENDENCY_LAYOUT
        "deps/JUCE"
        "deps/tracktion_engine"
    )
endif()

if(TSQ_BUILD_TESTS)
    list(APPEND TSQ_REQUIRED_DEPENDENCY_DIRS "${TSQ_CATCH2_DIR}")
    list(APPEND TSQ_EXPECTED_DEPENDENCY_LAYOUT "deps/Catch2")
endif()

foreach(required_dir IN LISTS TSQ_REQUIRED_DEPENDENCY_DIRS)
    if(NOT IS_DIRECTORY "${required_dir}")
        list(APPEND TSQ_MISSING_DEPENDENCIES "${required_dir}")
    endif()
endforeach()

if(TSQ_MISSING_DEPENDENCIES)
    string(JOIN "\n  " TSQ_MISSING_DEPENDENCIES_TEXT ${TSQ_MISSING_DEPENDENCIES})
    string(JOIN "\n  " TSQ_EXPECTED_DEPENDENCY_LAYOUT_TEXT ${TSQ_EXPECTED_DEPENDENCY_LAYOUT})
    message(FATAL_ERROR
        "TheorySequencer requires local dependency checkouts, but these directories are missing:\n"
        "  ${TSQ_MISSING_DEPENDENCIES_TEXT}\n\n"
        "Expected layout for enabled build options:\n"
        "  ${TSQ_EXPECTED_DEPENDENCY_LAYOUT_TEXT}\n\n"
        "Install pinned/local dependency checkouts at those paths. "
        "This project does not fetch floating dependency branches automatically."
    )
endif()

if(TSQ_BUILD_APP AND NOT EXISTS "${TSQ_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE was found at '${TSQ_JUCE_DIR}', but no CMakeLists.txt exists there. "
        "Expected a JUCE checkout that supports add_subdirectory()."
    )
endif()

if(TSQ_BUILD_APP AND NOT EXISTS "${TSQ_TRACKTION_ENGINE_DIR}/modules/CMakeLists.txt")
    message(FATAL_ERROR
        "Tracktion Engine was found at '${TSQ_TRACKTION_ENGINE_DIR}', but no modules/CMakeLists.txt exists there. "
        "Expected a Tracktion Engine checkout with CMake module support."
    )
endif()

if(TSQ_BUILD_TESTS AND NOT EXISTS "${TSQ_CATCH2_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Catch2 was found at '${TSQ_CATCH2_DIR}', but no CMakeLists.txt exists there. "
        "Expected a Catch2 checkout that supports add_subdirectory()."
    )
endif()

if(TSQ_BUILD_APP)
    add_subdirectory("${TSQ_JUCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/JUCE")
    add_subdirectory("${TSQ_TRACKTION_ENGINE_DIR}/modules" "${CMAKE_BINARY_DIR}/_deps/tracktion_engine_modules")
endif()

if(TSQ_BUILD_TESTS)
    add_subdirectory("${TSQ_CATCH2_DIR}" "${CMAKE_BINARY_DIR}/_deps/Catch2")

    if(NOT TARGET Catch2::Catch2WithMain)
        message(FATAL_ERROR
            "Catch2 at '${TSQ_CATCH2_DIR}' did not provide target Catch2::Catch2WithMain. "
            "Use a Catch2 v3 checkout or update the test target wiring intentionally."
        )
    endif()

    set(TSQ_CATCH2_MAIN_TARGET Catch2::Catch2WithMain)
endif()
