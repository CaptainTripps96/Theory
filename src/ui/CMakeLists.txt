add_library(tsq_ui STATIC
    AudioSettingsComponent.cpp
    BrowserDragPayload.cpp
    BrowserPanelComponent.cpp
    DeviceChainComponent.cpp
    DetailEditorComponent.cpp
    DiagnosticsComponent.cpp
    HarmonicHeaderComponent.cpp
    InspectorComponent.cpp
    MainComponent.cpp
    MainWindow.cpp
    ModuleInfo.cpp
    PianoRollComponent.cpp
    PluginBrowserComponent.cpp
    ScalePaletteComponent.cpp
    StatusBarComponent.cpp
    TimelineComponent.cpp
    TrackListComponent.cpp
    TransportComponent.cpp
    WaveformCache.cpp
)
add_library(tsq::ui ALIAS tsq_ui)

target_include_directories(tsq_ui
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(tsq_ui
    PUBLIC
        tsq::core
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
    PRIVATE
        juce::juce_audio_utils
)

tsq_apply_common_compiler_options(tsq_ui)
tsq_apply_warnings(tsq_ui)
tsq_apply_sanitizers(tsq_ui)
