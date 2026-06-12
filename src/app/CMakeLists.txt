juce_add_gui_app(tsq_app
    BUNDLE_ID "com.theorysequencer.app"
    COMPANY_NAME "TheorySequencer"
    PRODUCT_NAME "TheorySequencer"
)
add_executable(tsq::app ALIAS tsq_app)

target_sources(tsq_app
    PRIVATE
        AppSettings.cpp
        AppSettingsService.cpp
        AppServices.cpp
        Main.cpp
        MidiInputRecordingService.cpp
        TheorySequencerApplication.cpp
        ModuleInfo.cpp
)

target_include_directories(tsq_app
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(tsq_app
    PRIVATE
        tsq::core
        tsq::engine
        tsq::ui
        juce::juce_audio_devices
        juce::juce_core
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(tsq_app
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:tsq_app,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:tsq_app,JUCE_VERSION>"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

tsq_apply_common_compiler_options(tsq_app)
tsq_apply_warnings(tsq_app)
tsq_apply_sanitizers(tsq_app)
