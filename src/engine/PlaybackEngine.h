#pragma once

#include "core/sequencing/Project.h"
#include "engine/EngineTypes.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::engine
{
namespace plugins
{
struct PluginDescription;
}

class PlaybackEngine
{
public:
    virtual ~PlaybackEngine() = default;

    // Control-thread API. Implementations must prepare playback data before
    // handing it to the realtime audio callback owned by the backend engine.
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual std::vector<std::string> getAvailableAudioDevices() const = 0;
    virtual std::vector<AudioOutputDevice> getAvailableOutputDevices() const = 0;
    virtual AudioDeviceSettings getAudioDeviceSettings() const = 0;
    virtual bool setOutputDevice (const AudioOutputDevice& outputDevice) = 0;
    virtual std::string createAudioDeviceSettingsXml() const = 0;
    virtual bool restoreAudioDeviceSettingsXml (const std::string& settingsXml) = 0;
    virtual PlaybackEngineStatus getCurrentStatus() const = 0;
    virtual MeterSnapshot getMeterSnapshot() = 0;
    virtual void setProjectPluginStateDirectory (std::filesystem::path packagePath) = 0;
    virtual std::vector<TrackPluginState> captureTrackPluginStates() = 0;
    virtual void observeLivePluginParameterState() = 0;
    virtual void restoreObservedPluginParameterStateSoon() = 0;
    virtual std::vector<std::string> debugPluginParameterStateLines (std::string_view label) const = 0;
    virtual bool syncProject (const core::sequencing::Project& project) = 0;
    virtual bool startPlayback() = 0;
    virtual void stopPlayback() = 0;
    virtual bool isPlaying() const = 0;
    virtual core::time::TickPosition getPlayheadPosition() const = 0;
    virtual bool setPlayheadPosition (core::time::TickPosition position) = 0;
    virtual bool returnToStart() = 0;
    virtual bool setLoopEnabled (bool enabled) = 0;
    virtual bool isLoopEnabled() const = 0;
    virtual bool loadTestInstrument (const plugins::PluginDescription& plugin) = 0;
    virtual bool playTestPhrase() = 0;
    virtual void stopTestPhrase() = 0;
    virtual bool openLoadedPluginEditor() = 0;
    virtual bool openTrackPluginEditor (const std::string& trackId, const std::string& slotId) = 0;
    virtual bool setTrackPluginBypassed (const std::string& trackId, const std::string& slotId, bool bypassed) = 0;
    virtual TestInstrumentStatus getTestInstrumentStatus() const = 0;
};
}
