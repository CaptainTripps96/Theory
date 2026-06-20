add_library(tsq_engine STATIC
    devices/FirstPartyEffectTracktionPlugin.cpp
    devices/NativeEffectProcessors.cpp
    devices/SimpleOscComplexTracktionPlugin.cpp
    plugins/PluginDescription.cpp
    plugins/PluginRegistry.cpp
    plugins/PluginScanService.cpp
    TracktionPlaybackEngine.cpp
    ModuleInfo.cpp
)
add_library(tsq::engine ALIAS tsq_engine)

target_include_directories(tsq_engine
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(tsq_engine
    PUBLIC
        tsq::core
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
)

target_compile_definitions(tsq_engine
    PRIVATE
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
)

tsq_apply_common_compiler_options(tsq_engine)
tsq_apply_warnings(tsq_engine)
tsq_apply_sanitizers(tsq_engine)
