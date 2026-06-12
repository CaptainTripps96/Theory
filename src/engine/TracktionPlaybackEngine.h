#pragma once

#include "engine/PlaybackEngine.h"

#include <memory>
#include <string>
#include <string_view>
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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
