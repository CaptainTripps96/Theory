add_executable(tsq_core_tests
    core/AccidentalVisibilityTest.cpp
    core/BorrowedChordAnalysisTest.cpp
    core/ChordRecognizerTest.cpp
    core/CommandStackTest.cpp
    core/CustomScaleBuilderTest.cpp
    core/HarmonicContextResolverTest.cpp
    core/HarmonicOverlayTest.cpp
    core/LoggerTest.cpp
    core/MidiExporterTest.cpp
    core/MidiImporterTest.cpp
    core/MixerCommandTest.cpp
    core/MixerModelTest.cpp
    core/PlaceholderTest.cpp
    core/PitchModelTest.cpp
    core/ProjectModelTest.cpp
    core/ProjectSerializationTest.cpp
    core/RecordingInputTransformTest.cpp
    core/ScaleLibraryTest.cpp
    core/TimeModelTest.cpp
)

target_link_libraries(tsq_core_tests
    PRIVATE
        tsq::core
        ${TSQ_CATCH2_MAIN_TARGET}
)

tsq_apply_common_compiler_options(tsq_core_tests)
tsq_apply_warnings(tsq_core_tests)
tsq_apply_sanitizers(tsq_core_tests)

add_test(NAME tsq_core_tests COMMAND tsq_core_tests)
set_tests_properties(tsq_core_tests PROPERTIES
    LABELS "unit;pure;headless"
)

if(TSQ_BUILD_APP)
    add_executable(tsq_engine_integration_tests
        integration/AppServicesProjectPackageTest.cpp
        integration/BrowserPerformanceProbeTest.cpp
        integration/MeteringIntegrationTest.cpp
        integration/PianoRollPerformanceProbeTest.cpp
        integration/PluginRegistryTest.cpp
        integration/TrackListPerformanceProbeTest.cpp
        integration/VstStateRegressionTest.cpp
        "${PROJECT_SOURCE_DIR}/src/app/AppSettings.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/AppSettingsService.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/AppServices.cpp"
        "${PROJECT_SOURCE_DIR}/src/app/MidiInputRecordingService.cpp"
    )

    target_include_directories(tsq_engine_integration_tests
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
    )

    target_link_libraries(tsq_engine_integration_tests
        PRIVATE
            tsq::core
            tsq::engine
            tsq::ui
            ${TSQ_CATCH2_MAIN_TARGET}
            juce::juce_audio_devices
            juce::juce_core
            juce::juce_events
            juce::juce_gui_basics
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
    )

    if(APPLE)
        add_executable(tsq_mac_mouse_event_tool
            integration/MacMouseEventTool.mm
        )

        target_compile_features(tsq_mac_mouse_event_tool
            PRIVATE
                cxx_std_20
        )

        target_link_libraries(tsq_mac_mouse_event_tool
            PRIVATE
                "-framework ApplicationServices"
        )

        add_dependencies(tsq_engine_integration_tests tsq_mac_mouse_event_tool)

        target_sources(tsq_engine_integration_tests
            PRIVATE
                integration/MacNativeEditorDriver.mm
        )

        target_link_libraries(tsq_engine_integration_tests
            PRIVATE
                "-framework AppKit"
        )
    endif()

    target_compile_definitions(tsq_engine_integration_tests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
    )

    tsq_apply_common_compiler_options(tsq_engine_integration_tests)
    tsq_apply_warnings(tsq_engine_integration_tests)
    tsq_apply_sanitizers(tsq_engine_integration_tests)

    add_test(NAME tsq_engine_integration_tests COMMAND tsq_engine_integration_tests)
    set_tests_properties(tsq_engine_integration_tests PROPERTIES
        LABELS "integration;engine;plugin;vst3"
    )
endif()
