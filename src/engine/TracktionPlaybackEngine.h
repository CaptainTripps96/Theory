#pragma once

#include "engine/PlaybackEngine.h"

#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tsq::engine
{
struct PluginParameterDebugValue
{
    int index = -1;
    std::string parameterId;
    std::string name;
    float value = 0.0f;
    float defaultValue = 0.0f;
};

class TracktionPlaybackEngine final : public PlaybackEngine
{
public:
    TracktionPlaybackEngine();
    ~TracktionPlaybackEngine() override;

    bool initialize() override;
    void shutdown() override;
    std::vector<std::string> getAvailableAudioDevices() const override;
    std::vector<AudioOutputDevice> getAvailableOutputDevices() const override;
    AudioDeviceSettings getAudioDeviceSettings() const override;
    bool setOutputDevice (const AudioOutputDevice& outputDevice) override;
    std::string createAudioDeviceSettingsXml() const override;
    bool restoreAudioDeviceSettingsXml (const std::string& settingsXml) override;
    PlaybackEngineStatus getCurrentStatus() const override;
    MeterSnapshot getMeterSnapshot() override;
    void setProjectPluginStateDirectory (std::filesystem::path packagePath) override;
    std::vector<TrackPluginState> captureTrackPluginStates() override;
    void observeLivePluginParameterState() override;
    void restoreObservedPluginParameterStateSoon() override;
    std::vector<std::string> debugPluginParameterStateLines (std::string_view label) const override;
    bool syncProject (const core::sequencing::Project& project) override;
    bool startPlayback() override;
    void stopPlayback() override;
    bool isPlaying() const override;
    core::time::TickPosition getPlayheadPosition() const override;
    bool setPlayheadPosition (core::time::TickPosition position) override;
    bool returnToStart() override;
    bool setLoopEnabled (bool enabled) override;
    bool isLoopEnabled() const override;
    bool loadTestInstrument (const plugins::PluginDescription& plugin) override;
    bool playTestPhrase() override;
    void stopTestPhrase() override;
    bool openLoadedPluginEditor() override;
    bool openTrackPluginEditor (const std::string& trackId, const std::string& slotId) override;
    bool setTrackPluginBypassed (const std::string& trackId, const std::string& slotId, bool bypassed) override;
    TestInstrumentStatus getTestInstrumentStatus() const override;
    std::vector<PluginParameterDebugValue> debugLoadedPluginParameters() const;
    bool debugSetLoadedPluginParameterValue (int parameterIndex, float normalizedValue);
    std::optional<float> debugTrackVolumeDb (const std::string& trackId) const;
    std::optional<float> debugTrackPan (const std::string& trackId) const;
    std::size_t debugTrackVolumeAutomationPointCount (const std::string& trackId) const;
    std::optional<float> debugSendGainDb (const std::string& trackId, const std::string& returnTrackId) const;
    std::vector<std::uint64_t> debugNativeSimpleOscExpressionNoteOnEventIds (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscExpressionSlurEventCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscLegatoSlurEventCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscActiveVoiceCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscMidiNoteOnCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscRenderCallbackCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscRenderCallbackWithMidiCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscRenderCallbackPlayingCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscExpressionSlurFallbackCount (const std::string& trackId) const;
    float debugNativeSimpleOscMaxOutputPeak (const std::string& trackId) const;
    float debugNativeSimpleOscLastOutputPeak (const std::string& trackId) const;
    std::vector<std::size_t> debugNativeSimpleOscEventCounters (const std::string& trackId) const;
    std::pair<double, double> debugNativeSimpleOscLastRenderTimeRange (const std::string& trackId) const;
    std::size_t debugTracktionMidiNoteCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscExpressionModulationStreamCount (const std::string& trackId) const;
    std::size_t debugNativeSimpleOscPatchStateRefreshCount (const std::string& trackId) const;
    bool debugChaseNativeSimpleOscAtPlayhead (const std::string& trackId);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
