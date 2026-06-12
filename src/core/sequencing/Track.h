#pragma once

#include "core/sequencing/AudioClip.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/MixerStrip.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/Routing.h"
#include "core/sequencing/TrackInstrumentReference.h"
#include "core/sequencing/TrackType.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
class Track
{
public:
    Track (std::string id, std::string name);
    Track (std::string id, std::string name, TrackType type);

    const std::string& id() const noexcept;
    const std::string& name() const noexcept;
    void rename (std::string name);
    TrackType type() const noexcept;
    const std::optional<TrackInstrumentReference>& instrument() const noexcept;
    void setInstrument (TrackInstrumentReference instrument);
    void clearInstrument() noexcept;
    const MixerStrip& mixerStrip() const noexcept;
    void setMixerStrip (MixerStrip mixerStrip);
    const TrackRouting& routing() const noexcept;
    void setRouting (TrackRouting routing);
    const DeviceChain& deviceChain() const noexcept;
    void setDeviceChain (DeviceChain deviceChain);

    void addClip (MidiClip clip);
    MidiClip removeClipById (const std::string& clipId);
    void moveClip (const std::string& clipId, time::TickPosition startInProject);
    void resizeClip (const std::string& clipId, time::TickDuration length);
    void replaceClip (const std::string& clipId, MidiClip replacement);
    const std::vector<MidiClip>& clips() const noexcept;
    MidiClip* findClipById (const std::string& id) noexcept;
    const MidiClip* findClipById (const std::string& id) const noexcept;

    void addAudioClip (AudioClip clip);
    AudioClip removeAudioClipById (const std::string& clipId);
    void moveAudioClip (const std::string& clipId, time::TickPosition startInProject);
    void resizeAudioClip (const std::string& clipId, time::TickDuration length);
    void replaceAudioClip (const std::string& clipId, AudioClip replacement);
    const std::vector<AudioClip>& audioClips() const noexcept;
    AudioClip* findAudioClipById (const std::string& id) noexcept;
    const AudioClip* findAudioClipById (const std::string& id) const noexcept;

    const std::vector<AutomationLane>& automationLanes() const noexcept;
    const AutomationLane* findAutomationLane (const AutomationTarget& target) const noexcept;
    AutomationLane* findAutomationLane (const AutomationTarget& target) noexcept;
    void setAutomationLane (AutomationLane lane);
    AutomationLane removeAutomationLane (const AutomationTarget& target);

private:
    std::string id_;
    std::string name_;
    TrackType type_ = TrackType::midi;
    std::optional<TrackInstrumentReference> instrument_;
    MixerStrip mixerStrip_;
    TrackRouting routing_;
    DeviceChain deviceChain_;
    std::vector<MidiClip> clips_;
    std::vector<AudioClip> audioClips_;
    std::vector<AutomationLane> automationLanes_;
};
}
