#include "core/sequencing/Track.h"

#include "core/sequencing/ClipOverlap.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
Track::Track (std::string id, std::string name)
    : Track (std::move (id), std::move (name), TrackType::midi)
{
}

Track::Track (std::string id, std::string name, TrackType type)
    : id_ (std::move (id)),
      name_ (std::move (name)),
      type_ (type)
{
    if (id_.empty())
        throw std::invalid_argument ("Track requires a non-empty ID");

    if (name_.empty())
        throw std::invalid_argument ("Track requires a non-empty name");

    if (type_ == TrackType::master)
        routing_.setAudioTo (RouteEndpoint::hardwareOutput ("Main Output"));
}

const std::string& Track::id() const noexcept
{
    return id_;
}

const std::string& Track::name() const noexcept
{
    return name_;
}

void Track::rename (std::string name)
{
    if (name.empty())
        throw std::invalid_argument ("Track requires a non-empty name");

    name_ = std::move (name);
}

TrackType Track::type() const noexcept
{
    return type_;
}

const std::optional<TrackInstrumentReference>& Track::instrument() const noexcept
{
    return instrument_;
}

void Track::setInstrument (TrackInstrumentReference instrument)
{
    if (! trackTypeCanHostInstrument (type_))
        throw std::invalid_argument ("Only MIDI tracks can host an instrument");

    if (! instrument.isValid())
        throw std::invalid_argument ("Track instrument reference requires a plugin identifier or file path");

    instrument_ = std::move (instrument);
}

void Track::clearInstrument() noexcept
{
    instrument_.reset();
}

const MixerStrip& Track::mixerStrip() const noexcept
{
    return mixerStrip_;
}

void Track::setMixerStrip (MixerStrip mixerStrip)
{
    mixerStrip_ = mixerStrip;
}

const TrackRouting& Track::routing() const noexcept
{
    return routing_;
}

void Track::setRouting (TrackRouting routing)
{
    routing_ = std::move (routing);
}

const DeviceChain& Track::deviceChain() const noexcept
{
    return deviceChain_;
}

void Track::setDeviceChain (DeviceChain deviceChain)
{
    const auto errors = validateDeviceChainForTrackType (type_, deviceChain);
    if (! errors.empty())
        throw std::invalid_argument (errors.front());

    deviceChain_ = std::move (deviceChain);
}

void Track::addClip (MidiClip clip)
{
    if (! trackTypeCanOwnMidiClips (type_))
        throw std::invalid_argument ("Only MIDI tracks can contain MIDI clips");

    const auto duplicateId = std::any_of (clips_.begin(), clips_.end(), [&clip] (const auto& existingClip) {
        return existingClip.id() == clip.id();
    });

    if (duplicateId)
        throw std::invalid_argument ("Track already contains a clip with this ID");

    const auto overlapsExistingClip = std::any_of (clips_.begin(), clips_.end(), [&clip] (const auto& existingClip) {
        return clipsOverlap (existingClip, clip);
    });

    if (overlapsExistingClip)
        throw std::invalid_argument ("MIDI clips on the same track must not overlap");

    clips_.push_back (std::move (clip));
    std::stable_sort (clips_.begin(), clips_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInProject() < rhs.startInProject();
    });
}

MidiClip Track::removeClipById (const std::string& clipId)
{
    const auto match = std::find_if (clips_.begin(), clips_.end(), [&clipId] (const auto& clip) {
        return clip.id() == clipId;
    });

    if (match == clips_.end())
        throw std::invalid_argument ("Track does not contain a clip with this ID");

    auto removedClip = *match;
    clips_.erase (match);
    return removedClip;
}

void Track::moveClip (const std::string& clipId, time::TickPosition startInProject)
{
    auto* clip = findClipById (clipId);
    if (clip == nullptr)
        throw std::invalid_argument ("Track does not contain a clip with this ID");

    const auto movedClip = clip->withStartInProject (startInProject);
    const auto overlapsExistingClip = std::any_of (clips_.begin(), clips_.end(), [&movedClip, &clipId] (const auto& existingClip) {
        return existingClip.id() != clipId && clipsOverlap (existingClip, movedClip);
    });

    if (overlapsExistingClip)
        throw std::invalid_argument ("MIDI clips on the same track must not overlap");

    *clip = movedClip;
    std::stable_sort (clips_.begin(), clips_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInProject() < rhs.startInProject();
    });
}

void Track::resizeClip (const std::string& clipId, time::TickDuration length)
{
    auto* clip = findClipById (clipId);
    if (clip == nullptr)
        throw std::invalid_argument ("Track does not contain a clip with this ID");

    replaceClip (clipId, clip->withLength (length));
}

void Track::replaceClip (const std::string& clipId, MidiClip replacement)
{
    if (replacement.id() != clipId)
        throw std::invalid_argument ("Replacement clip ID must match the clip being replaced");

    auto* clip = findClipById (clipId);
    if (clip == nullptr)
        throw std::invalid_argument ("Track does not contain a clip with this ID");

    const auto overlapsExistingClip = std::any_of (clips_.begin(), clips_.end(), [&replacement, &clipId] (const auto& existingClip) {
        return existingClip.id() != clipId && clipsOverlap (existingClip, replacement);
    });

    if (overlapsExistingClip)
        throw std::invalid_argument ("MIDI clips on the same track must not overlap");

    *clip = std::move (replacement);
    std::stable_sort (clips_.begin(), clips_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInProject() < rhs.startInProject();
    });
}

const std::vector<MidiClip>& Track::clips() const noexcept
{
    return clips_;
}

MidiClip* Track::findClipById (const std::string& id) noexcept
{
    const auto match = std::find_if (clips_.begin(), clips_.end(), [&id] (const auto& clip) {
        return clip.id() == id;
    });

    if (match == clips_.end())
        return nullptr;

    return &*match;
}

const MidiClip* Track::findClipById (const std::string& id) const noexcept
{
    const auto match = std::find_if (clips_.begin(), clips_.end(), [&id] (const auto& clip) {
        return clip.id() == id;
    });

    if (match == clips_.end())
        return nullptr;

    return &*match;
}

void Track::addAudioClip (AudioClip clip)
{
    if (! trackTypeCanOwnAudioClips (type_))
        throw std::invalid_argument ("Only audio tracks can contain audio clips");

    const auto duplicateId = std::any_of (audioClips_.begin(), audioClips_.end(), [&clip] (const auto& existingClip) {
        return existingClip.id() == clip.id();
    });

    if (duplicateId)
        throw std::invalid_argument ("Track already contains an audio clip with this ID");

    const auto overlapsExistingClip = std::any_of (audioClips_.begin(), audioClips_.end(), [&clip] (const auto& existingClip) {
        return audioClipsOverlap (existingClip, clip);
    });

    if (overlapsExistingClip)
        throw std::invalid_argument ("Audio clips on the same track must not overlap");

    audioClips_.push_back (std::move (clip));
    std::stable_sort (audioClips_.begin(), audioClips_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInProject() < rhs.startInProject();
    });
}

AudioClip Track::removeAudioClipById (const std::string& clipId)
{
    const auto match = std::find_if (audioClips_.begin(), audioClips_.end(), [&clipId] (const auto& clip) {
        return clip.id() == clipId;
    });

    if (match == audioClips_.end())
        throw std::invalid_argument ("Track does not contain an audio clip with this ID");

    auto removedClip = *match;
    audioClips_.erase (match);
    return removedClip;
}

void Track::moveAudioClip (const std::string& clipId, time::TickPosition startInProject)
{
    auto* clip = findAudioClipById (clipId);
    if (clip == nullptr)
        throw std::invalid_argument ("Track does not contain an audio clip with this ID");

    replaceAudioClip (clipId, clip->withStartInProject (startInProject));
}

void Track::resizeAudioClip (const std::string& clipId, time::TickDuration length)
{
    auto* clip = findAudioClipById (clipId);
    if (clip == nullptr)
        throw std::invalid_argument ("Track does not contain an audio clip with this ID");

    replaceAudioClip (clipId, clip->withLength (length));
}

void Track::replaceAudioClip (const std::string& clipId, AudioClip replacement)
{
    if (replacement.id() != clipId)
        throw std::invalid_argument ("Replacement audio clip ID must match the clip being replaced");

    auto* clip = findAudioClipById (clipId);
    if (clip == nullptr)
        throw std::invalid_argument ("Track does not contain an audio clip with this ID");

    const auto overlapsExistingClip = std::any_of (audioClips_.begin(), audioClips_.end(), [&replacement, &clipId] (const auto& existingClip) {
        return existingClip.id() != clipId && audioClipsOverlap (existingClip, replacement);
    });

    if (overlapsExistingClip)
        throw std::invalid_argument ("Audio clips on the same track must not overlap");

    *clip = std::move (replacement);
    std::stable_sort (audioClips_.begin(), audioClips_.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.startInProject() < rhs.startInProject();
    });
}

const std::vector<AudioClip>& Track::audioClips() const noexcept
{
    return audioClips_;
}

AudioClip* Track::findAudioClipById (const std::string& id) noexcept
{
    const auto match = std::find_if (audioClips_.begin(), audioClips_.end(), [&id] (const auto& clip) {
        return clip.id() == id;
    });

    return match == audioClips_.end() ? nullptr : &*match;
}

const AudioClip* Track::findAudioClipById (const std::string& id) const noexcept
{
    const auto match = std::find_if (audioClips_.begin(), audioClips_.end(), [&id] (const auto& clip) {
        return clip.id() == id;
    });

    return match == audioClips_.end() ? nullptr : &*match;
}

const std::vector<AutomationLane>& Track::automationLanes() const noexcept
{
    return automationLanes_;
}

const AutomationLane* Track::findAutomationLane (const AutomationTarget& target) const noexcept
{
    const auto match = std::find_if (automationLanes_.begin(), automationLanes_.end(), [&target] (const auto& lane) {
        return lane.target() == target;
    });

    return match == automationLanes_.end() ? nullptr : &*match;
}

AutomationLane* Track::findAutomationLane (const AutomationTarget& target) noexcept
{
    const auto match = std::find_if (automationLanes_.begin(), automationLanes_.end(), [&target] (const auto& lane) {
        return lane.target() == target;
    });

    return match == automationLanes_.end() ? nullptr : &*match;
}

void Track::setAutomationLane (AutomationLane lane)
{
    const auto match = std::find_if (automationLanes_.begin(), automationLanes_.end(), [&lane] (const auto& existing) {
        return existing.target() == lane.target();
    });

    if (match == automationLanes_.end())
        automationLanes_.push_back (std::move (lane));
    else
        *match = std::move (lane);
}

AutomationLane Track::removeAutomationLane (const AutomationTarget& target)
{
    const auto match = std::find_if (automationLanes_.begin(), automationLanes_.end(), [&target] (const auto& lane) {
        return lane.target() == target;
    });

    if (match == automationLanes_.end())
        throw std::invalid_argument ("Track does not contain automation for this target");

    auto removed = *match;
    automationLanes_.erase (match);
    return removed;
}
}
