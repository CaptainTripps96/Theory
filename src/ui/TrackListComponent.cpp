#include "ui/TrackListComponent.h"

#include "app/AppServices.h"
#include "core/commands/MixerCommands.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/MixerMath.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "engine/PlaybackEngine.h"
#include "engine/plugins/PluginRegistry.h"
#include "ui/BrowserDragPayload.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace tsq::ui
{
namespace
{
const auto surfaceColour = juce::Colour { 0xff151a22 };
const auto rowColour = juce::Colour { 0xff202733 };
const auto rowInactiveColour = juce::Colour { 0xff1b2029 };
const auto selectedRowColour = juce::Colour { 0xff243349 };
const auto outlineColour = juce::Colour { 0xff303945 };
const auto textColour = juce::Colour { 0xffedf2f7 };
const auto mutedTextColour = juce::Colour { 0xff9aa7b7 };
const auto recordColour = juce::Colour { 0xffd95d5d };
const auto soloColour = juce::Colour { 0xffe3b45d };
const auto meterColour = juce::Colour { 0xff6fd0a8 };
const auto overloadColour = juce::Colour { 0xffe36f6f };
const auto dropPreviewColour = juce::Colour { 0xff5bbad5 };

constexpr int rulerHeight = 32;
constexpr int structureHeight = 166;
constexpr int trackTopPadding = 12;
constexpr int rowHeight = 86;
constexpr int automationLaneHeight = 42;
constexpr int automationLaneGap = 4;
constexpr int rowGap = 8;
constexpr int horizontalPadding = 8;

constexpr int insertMidiTrackMenuId = 1;
constexpr int insertAudioTrackMenuId = 2;
constexpr int insertReturnTrackMenuId = 3;
constexpr int insertMasterTrackMenuId = 4;

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

int visibleAutomationLaneCount (const core::sequencing::Track& track)
{
    return static_cast<int> (std::count_if (track.automationLanes().begin(),
                                            track.automationLanes().end(),
                                            [] (const auto& lane) { return lane.visible(); }));
}

int trackHeightFor (const core::sequencing::Track& track)
{
    const auto laneCount = visibleAutomationLaneCount (track);
    return rowHeight + (laneCount * (automationLaneHeight + automationLaneGap));
}

std::string trackTypeLabel (core::sequencing::TrackType type)
{
    switch (type)
    {
        case core::sequencing::TrackType::midi: return "MIDI";
        case core::sequencing::TrackType::audio: return "Audio";
        case core::sequencing::TrackType::returnTrack: return "Return";
        case core::sequencing::TrackType::master: return "Master";
    }

    return "Track";
}

juce::String instrumentSummary (const core::sequencing::Track& track)
{
    if (track.instrument().has_value())
        return toJuceString (track.instrument()->pluginName);

    if (! track.deviceChain().empty())
        return juce::String { static_cast<int> (track.deviceChain().slots().size()) } + " devices";

    return juce::String { "No instrument" };
}

juce::String panText (double pan)
{
    if (std::abs (pan) < 0.005)
        return "C";

    return juce::String { pan < 0.0 ? "L" : "R" } + juce::String { juce::roundToInt (std::abs (pan) * 100.0) };
}

std::string endpointLabel (const core::sequencing::Project& project,
                           const core::sequencing::RouteEndpoint& endpoint)
{
    using core::sequencing::RouteEndpointKind;

    switch (endpoint.kind)
    {
        case RouteEndpointKind::none: return "None";
        case RouteEndpointKind::master: return "Master";
        case RouteEndpointKind::hardwareOutput: return endpoint.id.empty() ? "Main Out" : endpoint.id;
        case RouteEndpointKind::sidechain: return endpoint.id.empty() ? "Sidechain" : endpoint.id;
        case RouteEndpointKind::track:
        case RouteEndpointKind::returnTrack:
            if (const auto* track = project.findTrackById (endpoint.id))
                return track->name();
            return endpoint.id.empty() ? "Missing" : endpoint.id;
    }

    return "None";
}

bool routingWouldRemainValid (const core::sequencing::Project& project,
                              const std::string& trackId,
                              const core::sequencing::TrackRouting& routing)
{
    auto copy = project;
    auto* track = copy.findTrackById (trackId);
    if (track == nullptr)
        return false;

    track->setRouting (routing);
    return core::sequencing::validateProjectRouting (copy).valid();
}

core::sequencing::RouteEndpoint endpointForTrack (const core::sequencing::Track& track)
{
    if (track.type() == core::sequencing::TrackType::returnTrack)
        return core::sequencing::RouteEndpoint::returnTrack (track.id());

    return core::sequencing::RouteEndpoint::track (track.id());
}

struct RouteChoice
{
    juce::String label;
    core::sequencing::RouteEndpoint endpoint;
};

struct SendTarget
{
    std::string returnTrackId;
    juce::String label;
};

double sendLevelFor (const core::sequencing::TrackRouting& routing, const std::string& returnTrackId)
{
    const auto& sends = routing.sends();
    const auto match = std::find_if (sends.begin(), sends.end(), [&returnTrackId] (const auto& send) {
        return send.targetReturnTrackId == returnTrackId;
    });

    return match == sends.end() ? 0.0 : match->normalizedLevel;
}

bool hasSendTo (const core::sequencing::TrackRouting& routing, const std::string& returnTrackId)
{
    const auto& sends = routing.sends();
    return std::any_of (sends.begin(), sends.end(), [&returnTrackId] (const auto& send) {
        return send.targetReturnTrackId == returnTrackId;
    });
}

void addRouteChoiceIfValid (std::vector<RouteChoice>& choices,
                            const core::sequencing::Project& project,
                            const std::string& trackId,
                            const core::sequencing::RouteEndpoint& endpoint,
                            const std::function<void (core::sequencing::TrackRouting&, core::sequencing::RouteEndpoint)>& setter)
{
    const auto* track = project.findTrackById (trackId);
    if (track == nullptr)
        return;

    auto routing = track->routing();
    setter (routing, endpoint);
    if (! routingWouldRemainValid (project, trackId, routing))
        return;

    choices.push_back (RouteChoice { toJuceString (endpointLabel (project, endpoint)), endpoint });
}

bool containsEndpoint (const std::vector<RouteChoice>& choices, const core::sequencing::RouteEndpoint& endpoint)
{
    return std::any_of (choices.begin(), choices.end(), [&endpoint] (const auto& choice) {
        return choice.endpoint == endpoint;
    });
}

std::vector<SendTarget> sendTargetsFor (const core::sequencing::Project& project, const core::sequencing::Track& track)
{
    if (track.type() == core::sequencing::TrackType::master)
        return {};

    std::vector<SendTarget> targets;
    for (const auto& candidate : project.tracks())
    {
        if (candidate.type() != core::sequencing::TrackType::returnTrack || candidate.id() == track.id())
            continue;

        auto routing = track.routing();
        routing.addOrReplaceSend (core::sequencing::ReturnSend { candidate.id(), std::max (0.001, sendLevelFor (track.routing(), candidate.id())) });
        if (! routingWouldRemainValid (project, track.id(), routing))
            continue;

        targets.push_back (SendTarget { candidate.id(), toJuceString (candidate.name()) });
    }

    return targets;
}

std::vector<RouteChoice> routeChoicesFor (const core::sequencing::Project& project,
                                          const core::sequencing::Track& track,
                                          const char* routeId)
{
    using namespace core::sequencing;

    std::vector<RouteChoice> choices;
    const auto add = [&] (const RouteEndpoint& endpoint, auto setter)
    {
        addRouteChoiceIfValid (choices, project, track.id(), endpoint, setter);
    };

    const auto setAudioTo = [] (TrackRouting& routing, RouteEndpoint endpoint) { routing.setAudioTo (std::move (endpoint)); };
    const auto setAudioFrom = [] (TrackRouting& routing, RouteEndpoint endpoint) { routing.setAudioFrom (std::move (endpoint)); };
    const auto setMidiTo = [] (TrackRouting& routing, RouteEndpoint endpoint) { routing.setMidiTo (std::move (endpoint)); };
    const auto setMidiFrom = [] (TrackRouting& routing, RouteEndpoint endpoint) { routing.setMidiFrom (std::move (endpoint)); };

    const auto route = std::string_view { routeId };
    if (route == "audioTo")
    {
        if (track.type() == TrackType::master)
        {
            add (RouteEndpoint::hardwareOutput ("Main Output"), setAudioTo);
            return choices;
        }

        add (RouteEndpoint::none(), setAudioTo);
        add (RouteEndpoint::master(), setAudioTo);
        add (RouteEndpoint::hardwareOutput ("Main Output"), setAudioTo);

        for (const auto& candidate : project.tracks())
        {
            if (candidate.id() == track.id() || candidate.type() == TrackType::master)
                continue;

            add (endpointForTrack (candidate), setAudioTo);
        }
    }
    else if (route == "audioFrom")
    {
        add (RouteEndpoint::none(), setAudioFrom);
        if (track.type() == TrackType::audio || track.type() == TrackType::returnTrack)
        {
            for (const auto& candidate : project.tracks())
            {
                if (candidate.id() == track.id() || candidate.type() == TrackType::master)
                    continue;

                add (endpointForTrack (candidate), setAudioFrom);
            }
        }
    }
    else if (route == "midiFrom")
    {
        add (RouteEndpoint::none(), setMidiFrom);
        if (track.type() == TrackType::midi)
        {
            for (const auto& candidate : project.tracks())
            {
                if (candidate.id() == track.id() || candidate.type() != TrackType::midi)
                    continue;

                add (RouteEndpoint::track (candidate.id()), setMidiFrom);
            }
        }
    }
    else if (route == "midiTo")
    {
        add (RouteEndpoint::none(), setMidiTo);
        if (track.type() == TrackType::midi)
        {
            for (const auto& candidate : project.tracks())
            {
                if (candidate.id() == track.id() || candidate.type() != TrackType::midi)
                    continue;

                add (RouteEndpoint::track (candidate.id()), setMidiTo);
            }
        }
    }

    return choices;
}

void hashCombine (std::size_t& seed, std::size_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

void hashString (std::size_t& seed, const std::string& value)
{
    hashCombine (seed, std::hash<std::string> {} (value));
}

void hashBool (std::size_t& seed, bool value) noexcept
{
    hashCombine (seed, value ? 1u : 0u);
}

void hashDouble (std::size_t& seed, double value)
{
    hashCombine (seed, std::hash<double> {} (value));
}

void hashEndpoint (std::size_t& seed, const core::sequencing::RouteEndpoint& endpoint)
{
    hashCombine (seed, static_cast<std::size_t> (endpoint.kind));
    hashString (seed, endpoint.id);
    hashString (seed, endpoint.label);
}

void hashRouting (std::size_t& seed, const core::sequencing::TrackRouting& routing)
{
    hashEndpoint (seed, routing.audioFrom());
    hashEndpoint (seed, routing.audioTo());
    hashEndpoint (seed, routing.midiFrom());
    hashEndpoint (seed, routing.midiTo());
    hashCombine (seed, routing.sends().size());
    for (const auto& send : routing.sends())
    {
        hashString (seed, send.targetReturnTrackId);
        hashDouble (seed, send.normalizedLevel);
    }
}

void hashMixerStrip (std::size_t& seed, const core::sequencing::MixerStrip& mixerStrip)
{
    hashDouble (seed, mixerStrip.volumeDb());
    hashDouble (seed, mixerStrip.pan());
    hashBool (seed, mixerStrip.active());
    hashBool (seed, mixerStrip.soloed());
    hashString (seed, mixerStrip.meterSourceId());
    hashBool (seed, mixerStrip.colorArgb().has_value());
    if (mixerStrip.colorArgb().has_value())
        hashCombine (seed, static_cast<std::size_t> (*mixerStrip.colorArgb()));
}

void hashInstrumentReference (std::size_t& seed, const core::sequencing::TrackInstrumentReference& instrument)
{
    hashString (seed, instrument.pluginName);
    hashString (seed, instrument.manufacturer);
    hashString (seed, instrument.format);
    hashString (seed, instrument.fileOrIdentifier);
    hashString (seed, instrument.uniqueIdentifier);
    hashCombine (seed, static_cast<std::size_t> (instrument.uniqueId));
    hashCombine (seed, static_cast<std::size_t> (instrument.deprecatedUid));
    hashBool (seed, instrument.isInstrument);
    hashCombine (seed, static_cast<std::size_t> (instrument.numInputChannels));
    hashCombine (seed, static_cast<std::size_t> (instrument.numOutputChannels));
    hashString (seed, instrument.pluginStateFile);
}

void hashPluginReference (std::size_t& seed, const core::sequencing::PluginReference& plugin)
{
    hashString (seed, plugin.pluginName);
    hashString (seed, plugin.manufacturer);
    hashString (seed, plugin.format);
    hashString (seed, plugin.fileOrIdentifier);
    hashString (seed, plugin.uniqueIdentifier);
    hashCombine (seed, static_cast<std::size_t> (plugin.uniqueId));
    hashCombine (seed, static_cast<std::size_t> (plugin.deprecatedUid));
    hashCombine (seed, static_cast<std::size_t> (plugin.numInputChannels));
    hashCombine (seed, static_cast<std::size_t> (plugin.numOutputChannels));
}

std::size_t trackHeaderFingerprint (const core::sequencing::Track& track, bool recordArmed)
{
    auto fingerprint = std::size_t {};
    hashString (fingerprint, track.id());
    hashString (fingerprint, track.name());
    hashCombine (fingerprint, static_cast<std::size_t> (track.type()));
    hashBool (fingerprint, track.instrument().has_value());
    if (track.instrument().has_value())
        hashInstrumentReference (fingerprint, *track.instrument());

    hashCombine (fingerprint, track.deviceChain().size());
    for (const auto& slot : track.deviceChain().slots())
    {
        hashString (fingerprint, slot.id().value);
        hashPluginReference (fingerprint, slot.plugin());
        hashCombine (fingerprint, static_cast<std::size_t> (slot.kind()));
        hashBool (fingerprint, slot.bypassed());
        hashString (fingerprint, slot.pluginStateFile());
    }

    hashMixerStrip (fingerprint, track.mixerStrip());
    hashRouting (fingerprint, track.routing());
    hashBool (fingerprint, recordArmed);
    return fingerprint;
}

std::size_t routingTopologyFingerprint (const core::sequencing::Project& project)
{
    auto fingerprint = std::size_t {};
    hashCombine (fingerprint, project.tracks().size());
    for (const auto& track : project.tracks())
    {
        hashString (fingerprint, track.id());
        hashString (fingerprint, track.name());
        hashCombine (fingerprint, static_cast<std::size_t> (track.type()));
        hashRouting (fingerprint, track.routing());
    }

    return fingerprint;
}

std::size_t rowLayoutFingerprintFor (const core::sequencing::Project& project)
{
    auto fingerprint = std::size_t {};
    hashCombine (fingerprint, project.tracks().size());
    for (const auto& track : project.tracks())
    {
        hashString (fingerprint, track.id());
        hashCombine (fingerprint, static_cast<std::size_t> (trackHeightFor (track)));
    }

    return fingerprint;
}

bool meterChannelsDiffer (const engine::MeterChannelSnapshot& lhs, const engine::MeterChannelSnapshot& rhs)
{
    constexpr auto meterRepaintThreshold = 0.002f;
    return std::abs (lhs.peakLinear - rhs.peakLinear) > meterRepaintThreshold
        || lhs.peakOverload != rhs.peakOverload
        || lhs.rmsAvailable != rhs.rmsAvailable
        || std::abs (lhs.rmsLinear - rhs.rmsLinear) > meterRepaintThreshold;
}

bool metersDiffer (const engine::MeterSourceSnapshot& lhs, const engine::MeterSourceSnapshot& rhs)
{
    if (lhs.sourceId != rhs.sourceId
        || lhs.trackId != rhs.trackId
        || lhs.displayName != rhs.displayName
        || lhs.master != rhs.master
        || lhs.returnTrack != rhs.returnTrack
        || lhs.active != rhs.active
        || lhs.channels.size() != rhs.channels.size())
        return true;

    for (std::size_t index = 0; index < lhs.channels.size(); ++index)
        if (meterChannelsDiffer (lhs.channels[index], rhs.channels[index]))
            return true;

    return false;
}
}

class TrackListComponent::TrackHeaderComponent final : public juce::Component
{
public:
    explicit TrackHeaderComponent (TrackListComponent& owner)
        : owner_ (owner)
    {
        setTitle ("Track Header");
        setDescription ("Mixer-style track header with meter, volume, pan, activator, solo, record arm, routing, and sends.");

        addAndMakeVisible (nameLabel_);
        nameLabel_.setTitle ("Track Name");
        nameLabel_.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
        nameLabel_.setColour (juce::Label::textColourId, textColour);
        nameLabel_.setJustificationType (juce::Justification::centredLeft);

        addAndMakeVisible (summaryLabel_);
        summaryLabel_.setTitle ("Track Summary");
        summaryLabel_.setFont (juce::FontOptions { 10.5f });
        summaryLabel_.setColour (juce::Label::textColourId, mutedTextColour);
        summaryLabel_.setJustificationType (juce::Justification::centredLeft);

        configureButton (activeButton_, "On", "Track Activator");
        activeButton_.onClick = [this] { commitActiveToggle(); };

        configureButton (soloButton_, "S", "Solo");
        soloButton_.onClick = [this] { commitSoloToggle(); };

        configureButton (armButton_, "ARM", "Record Arm");
        armButton_.onClick = [this] { commitArmToggle(); };

        configureSlider (volumeSlider_, core::sequencing::MixerStrip::minimumFiniteVolumeDb, core::sequencing::MixerStrip::maximumVolumeDb);
        volumeSlider_.setTooltip ("Volume");
        volumeSlider_.setTitle ("Track Volume");
        volumeSlider_.setDescription ("Adjusts track volume in decibels.");
        volumeSlider_.onDragStart = [this] { volumeDragActive_ = true; };
        volumeSlider_.onDragEnd = [this]
        {
            volumeDragActive_ = false;
            commitVolumeFromSlider();
        };
        volumeSlider_.onValueChange = [this]
        {
            if (updating_)
                return;

            volumeEditor_.setText (core::sequencing::formatDecibelText (volumeSlider_.getValue(), 1), juce::dontSendNotification);
            if (! volumeDragActive_)
                commitVolumeFromSlider();
        };
        addAndMakeVisible (volumeSlider_);

        volumeEditor_.setTooltip ("Volume dB");
        volumeEditor_.setTitle ("Track Volume dB");
        volumeEditor_.setDescription ("Type a track volume in dB, or -inf for silence.");
        volumeEditor_.setJustification (juce::Justification::centred);
        volumeEditor_.setSelectAllWhenFocused (true);
        volumeEditor_.setColour (juce::TextEditor::backgroundColourId, juce::Colour { 0xff11161d });
        volumeEditor_.setColour (juce::TextEditor::outlineColourId, outlineColour);
        volumeEditor_.setColour (juce::TextEditor::focusedOutlineColourId, dropPreviewColour);
        volumeEditor_.setColour (juce::TextEditor::textColourId, textColour);
        volumeEditor_.onReturnKey = [this] { commitVolumeFromText(); };
        volumeEditor_.onFocusLost = [this] { commitVolumeFromText(); };
        volumeEditor_.onEscapeKey = [this] { updateVolumeEditorFromModel(); };
        addAndMakeVisible (volumeEditor_);

        configureSlider (panSlider_, core::sequencing::MixerStrip::minimumPan, core::sequencing::MixerStrip::maximumPan);
        panSlider_.setTooltip ("Pan");
        panSlider_.setTitle ("Track Pan");
        panSlider_.setDescription ("Adjusts track pan from left to right.");
        panSlider_.onDragStart = [this] { panDragActive_ = true; };
        panSlider_.onDragEnd = [this]
        {
            panDragActive_ = false;
            commitPanFromSlider();
        };
        panSlider_.onValueChange = [this]
        {
            if (! updating_ && ! panDragActive_)
                commitPanFromSlider();
            repaint();
        };
        addAndMakeVisible (panSlider_);

        configureCombo (audioToSelector_, "Audio To");
        configureCombo (audioFromSelector_, "Audio From");
        configureCombo (midiToSelector_, "MIDI To");
        configureCombo (midiFromSelector_, "MIDI From");
    }

    void updateFromTrack (const core::sequencing::Track& track,
                          const engine::MeterSourceSnapshot& meter,
                          bool highlighted,
                          bool selected,
                          std::size_t projectRoutingFingerprint)
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TrackHeaderComponent::updateFromTrack" };

        const auto recordArmed = owner_.appServices_.isTrackRecordArmed (track.id());
        const auto nextTrackFingerprint = trackHeaderFingerprint (track, recordArmed);
        const auto modelChanged = trackId_ != track.id()
            || trackFingerprint_ != nextTrackFingerprint
            || routingTopologyFingerprint_ != projectRoutingFingerprint;
        const auto meterChanged = metersDiffer (meterSnapshot_, meter);
        const auto highlightChanged = dropHighlighted_ != highlighted;
        const auto selectedChanged = selected_ != selected;

        if (! modelChanged)
        {
            meterSnapshot_ = meter;
            dropHighlighted_ = highlighted;
            selected_ = selected;

            if (highlightChanged || selectedChanged)
                repaint();
            else if (meterChanged)
                repaint (meterBounds_.expanded (3));

            return;
        }

        trackId_ = track.id();
        currentMixerStrip_ = track.mixerStrip();
        currentRouting_ = track.routing();
        dropHighlighted_ = highlighted;
        selected_ = selected;
        meterSnapshot_ = meter;

        updating_ = true;
        const auto accessibleTrackName = toJuceString (track.name());
        setTitle (accessibleTrackName + " Track Header");
        setDescription (accessibleTrackName + " mixer controls and level meter.");
        nameLabel_.setText (toJuceString (track.name()), juce::dontSendNotification);
        nameLabel_.setTitle (accessibleTrackName + " Name");
        summaryLabel_.setText (toJuceString (trackTypeLabel (track.type())) + " - " + instrumentSummary (track), juce::dontSendNotification);
        summaryLabel_.setTitle (accessibleTrackName + " Summary");
        volumeSlider_.setTitle (accessibleTrackName + " Volume");
        volumeEditor_.setTitle (accessibleTrackName + " Volume dB");
        panSlider_.setTitle (accessibleTrackName + " Pan");
        activeButton_.setTitle (accessibleTrackName + " Activator");
        soloButton_.setTitle (accessibleTrackName + " Solo");
        armButton_.setTitle (accessibleTrackName + " Record Arm");
        audioFromSelector_.setTitle (accessibleTrackName + " Audio From");
        audioToSelector_.setTitle (accessibleTrackName + " Audio To");
        midiFromSelector_.setTitle (accessibleTrackName + " MIDI From");
        midiToSelector_.setTitle (accessibleTrackName + " MIDI To");

        activeButton_.setButtonText (track.mixerStrip().active() ? "On" : "Off");
        activeButton_.setToggleState (track.mixerStrip().active(), juce::dontSendNotification);
        activeButton_.setColour (juce::TextButton::buttonColourId,
                                 track.mixerStrip().active() ? dropPreviewColour.withAlpha (0.22f) : outlineColour);

        soloButton_.setToggleState (track.mixerStrip().soloed(), juce::dontSendNotification);
        soloButton_.setColour (juce::TextButton::buttonColourId,
                               track.mixerStrip().soloed() ? soloColour.withAlpha (0.34f) : outlineColour);

        armButton_.setEnabled (core::sequencing::trackTypeCanRecordMidi (track.type()));
        armButton_.setToggleState (recordArmed, juce::dontSendNotification);
        armButton_.setColour (juce::TextButton::buttonColourId,
                              recordArmed ? recordColour.withAlpha (0.50f) : outlineColour);

        volumeSlider_.setValue (core::sequencing::MixerStrip::isSilenceDb (track.mixerStrip().volumeDb())
                                    ? core::sequencing::MixerStrip::minimumFiniteVolumeDb
                                    : track.mixerStrip().volumeDb(),
                                juce::dontSendNotification);

        if (! volumeEditor_.hasKeyboardFocus (false))
            updateVolumeEditorFromModel();

        panSlider_.setValue (track.mixerStrip().pan(), juce::dontSendNotification);

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "TrackHeaderComponent::update routing-controls" };
            updateRouteSelector (audioToSelector_, audioToChoices_, routeChoicesFor (owner_.appServices_.project(), track, "audioTo"), track.routing().audioTo());
            updateRouteSelector (audioFromSelector_, audioFromChoices_, routeChoicesFor (owner_.appServices_.project(), track, "audioFrom"), track.routing().audioFrom());
            updateRouteSelector (midiToSelector_, midiToChoices_, routeChoicesFor (owner_.appServices_.project(), track, "midiTo"), track.routing().midiTo());
            updateRouteSelector (midiFromSelector_, midiFromChoices_, routeChoicesFor (owner_.appServices_.project(), track, "midiFrom"), track.routing().midiFrom());
        }

        {
            core::diagnostics::ScopedPerformanceTimer phaseTimer { "TrackHeaderComponent::update send-controls" };
            syncSendControls (sendTargetsFor (owner_.appServices_.project(), track));
        }

        for (auto& control : sendControls_)
            control->slider.setValue (sendLevelFor (track.routing(), control->returnTrackId), juce::dontSendNotification);

        trackFingerprint_ = nextTrackFingerprint;
        routingTopologyFingerprint_ = projectRoutingFingerprint;
        updating_ = false;
        repaint();
    }

    void paint (juce::Graphics& graphics) override
    {
        core::diagnostics::ScopedPerformanceTimer timer { "TrackHeaderComponent::paint" };

        const auto bounds = getLocalBounds().toFloat();
        const auto active = currentMixerStrip_.active();

        graphics.setColour (selected_ ? selectedRowColour : (active ? rowColour : rowInactiveColour));
        graphics.fillRoundedRectangle (bounds, 5.0f);

        graphics.setColour (dropHighlighted_ ? dropPreviewColour : outlineColour);
        graphics.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, dropHighlighted_ ? 2.0f : 1.0f);

        drawMeter (graphics);
    }

    void paintOverChildren (juce::Graphics& graphics) override
    {
        const auto panBounds = panSlider_.getBounds().reduced (2, 7);
        graphics.setColour (mutedTextColour.withAlpha (0.55f));
        graphics.drawVerticalLine (panBounds.getCentreX(),
                                   static_cast<float> (panBounds.getY()),
                                   static_cast<float> (panBounds.getBottom()));

        graphics.setColour (mutedTextColour);
        graphics.setFont (juce::FontOptions { 9.0f, juce::Font::bold });
        graphics.drawText (panText (panSlider_.getValue()), panBounds.withTrimmedTop (panBounds.getHeight() - 10), juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().withHeight (rowHeight).reduced (8, 6);
        auto top = bounds.removeFromTop (24);
        bounds.removeFromTop (4);
        auto routes = bounds.removeFromTop (22);
        bounds.removeFromTop (3);
        auto sends = bounds;

        auto info = top.removeFromLeft (96);
        nameLabel_.setBounds (info.removeFromTop (13).expanded (0, 1));
        summaryLabel_.setBounds (info);

        meterBounds_ = top.removeFromLeft (42).reduced (4, 3);
        top.removeFromLeft (4);

        volumeSlider_.setBounds (top.removeFromLeft (86));
        top.removeFromLeft (4);
        volumeEditor_.setBounds (top.removeFromLeft (58));
        top.removeFromLeft (5);
        panSlider_.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (5);
        activeButton_.setBounds (top.removeFromLeft (34));
        top.removeFromLeft (4);
        soloButton_.setBounds (top.removeFromLeft (28));
        top.removeFromLeft (4);
        armButton_.setBounds (top.removeFromLeft (40));

        routes.removeFromLeft (96);
        routes.removeFromLeft (46);
        const auto comboWidth = std::max (58, (routes.getWidth() - 18) / 4);
        audioFromSelector_.setBounds (routes.removeFromLeft (comboWidth));
        routes.removeFromLeft (6);
        audioToSelector_.setBounds (routes.removeFromLeft (comboWidth));
        routes.removeFromLeft (6);
        midiFromSelector_.setBounds (routes.removeFromLeft (comboWidth));
        routes.removeFromLeft (6);
        midiToSelector_.setBounds (routes);

        sends.removeFromLeft (96);
        sends.removeFromLeft (46);
        const auto count = static_cast<int> (sendControls_.size());
        const auto gap = 5;
        const auto slotWidth = count <= 0 ? 0 : std::max (48, (sends.getWidth() - (gap * (count - 1))) / count);
        for (int index = 0; index < count; ++index)
        {
            auto& control = *sendControls_[static_cast<std::size_t> (index)];
            if (sends.getWidth() < 42 || sends.isEmpty())
            {
                control.label.setVisible (false);
                control.slider.setVisible (false);
                continue;
            }

            control.label.setVisible (true);
            control.slider.setVisible (true);
            auto slot = sends.removeFromLeft (std::min (slotWidth, sends.getWidth()));
            const auto labelWidth = slot.getWidth() >= 78 ? 44 : 20;
            control.label.setBounds (slot.removeFromLeft (labelWidth));
            control.slider.setBounds (slot);

            if (index + 1 < count)
                sends.removeFromLeft (gap);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! trackId_.empty())
        {
            owner_.appServices_.setSelectedTrack (trackId_);
            owner_.refresh();
        }
    }

private:
    void configureButton (juce::TextButton& button, const juce::String& text, const juce::String& tooltip)
    {
        button.setButtonText (text);
        button.setTooltip (tooltip);
        button.setTitle (tooltip);
        button.setDescription (tooltip);
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, outlineColour);
        button.setColour (juce::TextButton::textColourOffId, textColour);
        button.setColour (juce::TextButton::textColourOnId, textColour);
        addAndMakeVisible (button);
    }

    void configureSlider (juce::Slider& slider, double minimum, double maximum)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (minimum, maximum, 0.01);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.setColour (juce::Slider::trackColourId, dropPreviewColour.withAlpha (0.45f));
        slider.setColour (juce::Slider::thumbColourId, textColour);
    }

    void configureCombo (juce::ComboBox& combo, const juce::String& tooltip)
    {
        combo.setTooltip (tooltip);
        combo.setTitle (tooltip);
        combo.setDescription (tooltip + " routing selector");
        combo.setColour (juce::ComboBox::backgroundColourId, juce::Colour { 0xff11161d });
        combo.setColour (juce::ComboBox::outlineColourId, outlineColour);
        combo.setColour (juce::ComboBox::textColourId, textColour);
        combo.onChange = [this, &combo]
        {
            if (! updating_)
                commitRouteSelector (combo);
        };
        addAndMakeVisible (combo);
    }

    struct SendControl
    {
        std::string returnTrackId;
        juce::Label label;
        juce::Slider slider;
        bool dragActive = false;
    };

    void configureSendControl (SendControl& control, const juce::String& labelText)
    {
        control.label.setText ("S " + labelText, juce::dontSendNotification);
        control.label.setTooltip ("Send to " + labelText);
        control.label.setTitle ("Send to " + labelText);
        control.label.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
        control.label.setColour (juce::Label::textColourId, mutedTextColour);
        control.label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (control.label);

        control.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        control.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        control.slider.setRange (0.0, 1.0, 0.001);
        control.slider.setDoubleClickReturnValue (true, 0.0);
        control.slider.setTooltip ("Send to " + labelText);
        control.slider.setTitle ("Send to " + labelText);
        control.slider.setDescription ("Adjusts send level to " + labelText);
        control.slider.setColour (juce::Slider::trackColourId, meterColour.withAlpha (0.48f));
        control.slider.setColour (juce::Slider::thumbColourId, textColour);

        auto* rawControl = &control;
        control.slider.onDragStart = [rawControl] { rawControl->dragActive = true; };
        control.slider.onDragEnd = [this, rawControl]
        {
            rawControl->dragActive = false;
            commitSendFromSlider (*rawControl);
        };
        control.slider.onValueChange = [this, rawControl]
        {
            if (! updating_ && ! rawControl->dragActive)
                commitSendFromSlider (*rawControl);
        };

        addAndMakeVisible (control.slider);
    }

    void syncSendControls (const std::vector<SendTarget>& targets)
    {
        auto matchesExisting = sendControls_.size() == targets.size();
        for (std::size_t index = 0; matchesExisting && index < targets.size(); ++index)
            matchesExisting = sendControls_[index]->returnTrackId == targets[index].returnTrackId;

        if (! matchesExisting)
        {
            for (auto& control : sendControls_)
            {
                removeChildComponent (&control->label);
                removeChildComponent (&control->slider);
            }

            sendControls_.clear();
            for (const auto& target : targets)
            {
                auto control = std::make_unique<SendControl>();
                control->returnTrackId = target.returnTrackId;
                configureSendControl (*control, target.label);
                sendControls_.push_back (std::move (control));
            }

            resized();
        }
        else
        {
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                sendControls_[index]->label.setText ("S " + targets[index].label, juce::dontSendNotification);
                sendControls_[index]->label.setTooltip ("Send to " + targets[index].label);
                sendControls_[index]->label.setTitle ("Send to " + targets[index].label);
                sendControls_[index]->slider.setTooltip ("Send to " + targets[index].label);
                sendControls_[index]->slider.setTitle ("Send to " + targets[index].label);
            }
        }
    }

    void updateRouteSelector (juce::ComboBox& combo,
                              std::vector<RouteChoice>& storage,
                              std::vector<RouteChoice> choices,
                              const core::sequencing::RouteEndpoint& current)
    {
        if (! containsEndpoint (choices, current))
            choices.insert (choices.begin(), RouteChoice { toJuceString (endpointLabel (owner_.appServices_.project(), current)), current });

        storage = std::move (choices);
        combo.clear (juce::dontSendNotification);
        for (int index = 0; index < static_cast<int> (storage.size()); ++index)
            combo.addItem (storage[static_cast<std::size_t> (index)].label, index + 1);

        const auto match = std::find_if (storage.begin(), storage.end(), [&current] (const auto& choice) {
            return choice.endpoint == current;
        });
        combo.setSelectedId (match == storage.end() ? 1 : static_cast<int> (std::distance (storage.begin(), match)) + 1,
                             juce::dontSendNotification);
        combo.setEnabled (storage.size() > 1);
    }

    void updateVolumeEditorFromModel()
    {
        volumeEditor_.setText (core::sequencing::formatDecibelText (currentMixerStrip_.volumeDb(), 1),
                               juce::dontSendNotification);
    }

    void commitVolumeFromSlider()
    {
        auto strip = currentMixerStrip_;
        strip.setVolumeDb (volumeSlider_.getValue());
        owner_.commitMixerStrip (trackId_, strip);
    }

    void commitVolumeFromText()
    {
        if (updating_ || trackId_.empty())
            return;

        const auto parsed = core::sequencing::parseDecibelText (volumeEditor_.getText().toStdString());
        if (! parsed.has_value())
        {
            owner_.appServices_.reportWarning ("Volume entry failed: enter a number of dB or -inf");
            updateVolumeEditorFromModel();
            return;
        }

        auto strip = currentMixerStrip_;
        strip.setVolumeDb (*parsed);
        owner_.commitMixerStrip (trackId_, strip);
    }

    void commitPanFromSlider()
    {
        auto strip = currentMixerStrip_;
        strip.setPan (panSlider_.getValue());
        owner_.commitMixerStrip (trackId_, strip);
    }

    void commitActiveToggle()
    {
        auto strip = currentMixerStrip_;
        strip.setActive (! strip.active());
        owner_.commitMixerStrip (trackId_, strip);
    }

    void commitSoloToggle()
    {
        auto strip = currentMixerStrip_;
        strip.setSoloed (! strip.soloed());
        owner_.commitMixerStrip (trackId_, strip);
    }

    void commitArmToggle()
    {
        if (owner_.appServices_.isTrackRecordArmed (trackId_))
            owner_.appServices_.clearRecordArmedTrack();
        else
            owner_.appServices_.setRecordArmedTrack (trackId_);

        owner_.refresh();
    }

    void commitRouteSelector (juce::ComboBox& combo)
    {
        auto routing = currentRouting_;

        if (&combo == &audioToSelector_)
            routing.setAudioTo (selectedEndpoint (audioToChoices_, combo));
        else if (&combo == &audioFromSelector_)
            routing.setAudioFrom (selectedEndpoint (audioFromChoices_, combo));
        else if (&combo == &midiToSelector_)
            routing.setMidiTo (selectedEndpoint (midiToChoices_, combo));
        else if (&combo == &midiFromSelector_)
            routing.setMidiFrom (selectedEndpoint (midiFromChoices_, combo));
        else
            return;

        owner_.commitRouting (trackId_, std::move (routing));
    }

    void commitSendFromSlider (SendControl& control)
    {
        if (trackId_.empty() || control.returnTrackId.empty())
            return;

        auto routing = currentRouting_;
        const auto level = std::clamp (control.slider.getValue(), 0.0, 1.0);
        if (level <= 0.0005)
        {
            if (! hasSendTo (routing, control.returnTrackId))
                return;

            routing.removeSend (control.returnTrackId);
        }
        else
        {
            routing.addOrReplaceSend (core::sequencing::ReturnSend { control.returnTrackId, level });
        }

        owner_.commitRouting (trackId_, std::move (routing));
    }

    core::sequencing::RouteEndpoint selectedEndpoint (const std::vector<RouteChoice>& choices, const juce::ComboBox& combo) const
    {
        const auto index = combo.getSelectedId() - 1;
        if (index < 0 || index >= static_cast<int> (choices.size()))
            return core::sequencing::RouteEndpoint::none();

        return choices[static_cast<std::size_t> (index)].endpoint;
    }

    void drawMeter (juce::Graphics& graphics)
    {
        if (meterBounds_.isEmpty())
            return;

        graphics.setColour (juce::Colour { 0xff10151c });
        graphics.fillRoundedRectangle (meterBounds_.toFloat(), 3.0f);
        graphics.setColour (outlineColour);
        graphics.drawRoundedRectangle (meterBounds_.toFloat().reduced (0.5f), 3.0f, 1.0f);

        auto peak = 0.0f;
        auto overloaded = false;
        for (const auto& channel : meterSnapshot_.channels)
        {
            peak = std::max (peak, channel.peakLinear);
            overloaded = overloaded || channel.peakOverload;
        }

        const auto meterWidth = juce::roundToInt (std::clamp (peak, 0.0f, 1.0f) * static_cast<float> (meterBounds_.getWidth()));
        if (meterWidth > 0)
        {
            auto fill = meterBounds_.withWidth (meterWidth).reduced (2, 3);
            graphics.setColour (overloaded ? overloadColour : meterColour);
            graphics.fillRoundedRectangle (fill.toFloat(), 2.0f);
        }
    }

    TrackListComponent& owner_;
    std::string trackId_;
    core::sequencing::MixerStrip currentMixerStrip_;
    core::sequencing::TrackRouting currentRouting_;
    engine::MeterSourceSnapshot meterSnapshot_;
    std::size_t trackFingerprint_ = 0;
    std::size_t routingTopologyFingerprint_ = 0;
    bool updating_ = false;
    bool volumeDragActive_ = false;
    bool panDragActive_ = false;
    bool dropHighlighted_ = false;
    bool selected_ = false;
    juce::Rectangle<int> meterBounds_;
    std::vector<RouteChoice> audioToChoices_;
    std::vector<RouteChoice> audioFromChoices_;
    std::vector<RouteChoice> midiToChoices_;
    std::vector<RouteChoice> midiFromChoices_;
    std::vector<std::unique_ptr<SendControl>> sendControls_;
    juce::Label nameLabel_;
    juce::Label summaryLabel_;
    juce::TextButton activeButton_;
    juce::TextButton soloButton_;
    juce::TextButton armButton_;
    juce::Slider volumeSlider_;
    juce::TextEditor volumeEditor_;
    juce::Slider panSlider_;
    juce::ComboBox audioToSelector_;
    juce::ComboBox audioFromSelector_;
    juce::ComboBox midiToSelector_;
    juce::ComboBox midiFromSelector_;
};

TrackListComponent::TrackListComponent (app::AppServices& appServices)
    : appServices_ (appServices)
{
    setTitle ("Track Headers");
    setDescription ("Mixer-style track headers with meters, routing, sends, volume, pan, activator, solo, and record-arm controls.");
    rebuildRowsIfNeeded();
}

TrackListComponent::~TrackListComponent() = default;

void TrackListComponent::paint (juce::Graphics& graphics)
{
    core::diagnostics::ScopedPerformanceTimer timer { "TrackListComponent::paint" };

    graphics.fillAll (surfaceColour);
    graphics.setColour (outlineColour);
    graphics.drawRect (getLocalBounds());

    auto bounds = getLocalBounds().reduced (12, 10);
    graphics.setColour (mutedTextColour);
    graphics.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
    graphics.drawText ("Tracks", bounds.removeFromTop (24), juce::Justification::centredLeft);

    if (appServices_.project().tracks().empty())
    {
        graphics.setFont (juce::FontOptions { 13.0f });
        graphics.drawText ("No tracks - right-click or drop plugins/files here",
                           getLocalBounds(),
                           juce::Justification::centred);
    }
}

void TrackListComponent::resized()
{
    layoutRows();
}

void TrackListComponent::mouseUp (const juce::MouseEvent&)
{
}

void TrackListComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() && isEmptyTrackCreationArea (event.getPosition()))
    {
        showInsertTrackMenu();
        return;
    }
}

void TrackListComponent::refresh()
{
    core::diagnostics::ScopedPerformanceTimer timer { "TrackListComponent::refresh" };

    auto rowsRebuilt = false;
    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TrackListComponent::refresh rebuildRowsIfNeeded" };
        rowsRebuilt = rebuildRowsIfNeeded();
    }

    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TrackListComponent::refresh getMeterSnapshot" };
        meterSnapshot_ = appServices_.playbackEngine().getMeterSnapshot();
    }

    std::unordered_map<std::string, const engine::MeterSourceSnapshot*> meterSourcesByTrack;
    meterSourcesByTrack.reserve (meterSnapshot_.sources.size());
    for (const auto& source : meterSnapshot_.sources)
        meterSourcesByTrack.emplace (source.trackId, &source);

    const auto& project = appServices_.project();
    const auto routingFingerprint = routingTopologyFingerprint (project);
    const auto& tracks = project.tracks();
    {
        core::diagnostics::ScopedPerformanceTimer phaseTimer { "TrackListComponent::refresh updateRows" };
        for (int index = 0; index < static_cast<int> (rowComponents_.size()) && index < static_cast<int> (tracks.size()); ++index)
        {
            const auto& track = tracks[static_cast<std::size_t> (index)];
            const auto meter = meterSourcesByTrack.find (track.id());
            rowComponents_[static_cast<std::size_t> (index)]->updateFromTrack (
                track,
                meter == meterSourcesByTrack.end() ? engine::MeterSourceSnapshot {} : *meter->second,
                index == pluginDropPreviewTrackIndex_,
                appServices_.selectedTrackId().has_value() && *appServices_.selectedTrackId() == track.id(),
                routingFingerprint);
        }
    }

    if (rowsRebuilt)
        repaint();
}

bool TrackListComponent::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (pluginDragPayloadFromVar (details.description).has_value())
        return trackIndexAt (details.localPosition) >= 0 || isEmptyTrackCreationArea (details.localPosition);

    if (projectFileDragPayloadFromVar (details.description).has_value())
        return isEmptyTrackCreationArea (details.localPosition);

    return false;
}

void TrackListComponent::itemDragMove (const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto payload = pluginDragPayloadFromVar (details.description);
    pluginDropPreviewTrackIndex_ = payload.has_value() && trackIndexAt (details.localPosition) >= 0 ? trackIndexAt (details.localPosition) : -1;
    refresh();
}

void TrackListComponent::itemDragExit (const juce::DragAndDropTarget::SourceDetails&)
{
    pluginDropPreviewTrackIndex_ = -1;
    refresh();
}

void TrackListComponent::itemDropped (const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto payload = pluginDragPayloadFromVar (details.description);
    const auto trackIndex = trackIndexAt (details.localPosition);
    pluginDropPreviewTrackIndex_ = -1;

    if (trackIndex < 0)
    {
        if (isEmptyTrackCreationArea (details.localPosition))
            createTrackFromPayload (details.description);

        refresh();
        return;
    }

    if (! payload.has_value() || trackIndex >= static_cast<int> (appServices_.project().tracks().size()))
    {
        refresh();
        return;
    }

    const auto plugin = appServices_.pluginRegistry().findByStableId (payload->stableId);
    if (! plugin.has_value())
    {
        appServices_.reportWarning ("Plugin drop failed: plugin was not found in the registry");
        refresh();
        return;
    }

    const auto& trackId = appServices_.project().tracks()[static_cast<std::size_t> (trackIndex)].id();
    if (plugin->isInstrument)
        appServices_.assignInstrumentToTrack (trackId, *plugin);
    else if (plugin->isAudioEffect)
        appServices_.addPluginDeviceToTrack (trackId, *plugin, core::sequencing::PluginKind::audioEffect);
    else
        appServices_.reportWarning ("Plugin drop failed: unsupported plugin type");

    refresh();
}

bool TrackListComponent::rebuildRowsIfNeeded()
{
    std::vector<std::string> trackIds;
    for (const auto& track : appServices_.project().tracks())
        trackIds.push_back (track.id());

    const auto nextLayoutFingerprint = rowLayoutFingerprintFor (appServices_.project());
    const auto trackIdsChanged = trackIds != rowTrackIds_;
    if (! trackIdsChanged && nextLayoutFingerprint == rowLayoutFingerprint_)
        return false;

    rowTrackIds_ = std::move (trackIds);
    rowLayoutFingerprint_ = nextLayoutFingerprint;

    if (trackIdsChanged)
    {
        rowComponents_.clear();
        removeAllChildren();

        for (std::size_t index = 0; index < rowTrackIds_.size(); ++index)
        {
            auto row = std::make_unique<TrackHeaderComponent> (*this);
            addAndMakeVisible (*row);
            rowComponents_.push_back (std::move (row));
        }
    }

    layoutRows();
    return true;
}

void TrackListComponent::layoutRows()
{
    const auto& tracks = appServices_.project().tracks();
    auto y = rulerHeight + structureHeight + trackTopPadding;
    for (int index = 0; index < static_cast<int> (rowComponents_.size()); ++index)
    {
        const auto height = index < static_cast<int> (tracks.size())
            ? trackHeightFor (tracks[static_cast<std::size_t> (index)])
            : rowHeight;
        rowComponents_[static_cast<std::size_t> (index)]->setBounds (
            horizontalPadding,
            y,
            std::max (0, getWidth() - (horizontalPadding * 2)),
            height);
        y += height + rowGap;
    }
}

void TrackListComponent::showInsertTrackMenu()
{
    juce::PopupMenu menu;
    menu.addItem (insertMidiTrackMenuId, "Insert MIDI Track");
    menu.addItem (insertAudioTrackMenuId, "Insert Audio Track");
    menu.addItem (insertReturnTrackMenuId, "Insert Return Track");
    menu.addItem (insertMasterTrackMenuId, "Insert Master Track", appServices_.project().masterTrack() == nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options {}.withTargetComponent (this),
                        [this] (int result)
                        {
                            switch (result)
                            {
                                case insertMidiTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::midi);
                                    break;
                                case insertAudioTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::audio);
                                    break;
                                case insertReturnTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::returnTrack);
                                    break;
                                case insertMasterTrackMenuId:
                                    appServices_.insertTrack (core::sequencing::TrackType::master);
                                    break;
                                default:
                                    break;
                            }

                            refresh();
                        });
}

bool TrackListComponent::isEmptyTrackCreationArea (juce::Point<int> position) const
{
    const auto trackCount = static_cast<int> (appServices_.project().tracks().size());
    if (trackCount == 0)
        return position.y >= rulerHeight + structureHeight + trackTopPadding;

    return position.y > trackRowForIndex (trackCount - 1).getBottom() + (rowGap / 2);
}

bool TrackListComponent::createTrackFromPayload (const juce::var& payload)
{
    if (const auto plugin = pluginDragPayloadFromVar (payload))
        return appServices_.createTrackFromPluginStableId (plugin->stableId);

    if (const auto file = projectFileDragPayloadFromVar (payload))
    {
        if (file->kind == "Audio")
            return appServices_.createAudioTrackFromFile (std::filesystem::path { file->absolutePath }, file->displayName);

        if (file->kind == "MIDI")
            return appServices_.createMidiTrackFromFile (std::filesystem::path { file->absolutePath }, file->displayName);

        appServices_.reportWarning ("Track creation failed: drag a scanned plugin row or supported audio/MIDI file");
        return false;
    }

    return false;
}

int TrackListComponent::trackIndexAt (juce::Point<int> position) const
{
    const auto& tracks = appServices_.project().tracks();
    auto y = rulerHeight + structureHeight + trackTopPadding;
    for (int index = 0; index < static_cast<int> (tracks.size()); ++index)
    {
        const auto height = trackHeightFor (tracks[static_cast<std::size_t> (index)]);
        const auto row = juce::Rectangle<int> {
            horizontalPadding,
            y,
            std::max (0, getWidth() - (horizontalPadding * 2)),
            height
        };
        if (row.contains (position))
            return index;

        y += height + rowGap;
    }

    return -1;
}

juce::Rectangle<int> TrackListComponent::trackRowForIndex (int index) const
{
    const auto& tracks = appServices_.project().tracks();
    auto y = rulerHeight + structureHeight + trackTopPadding;
    for (int previous = 0; previous < index && previous < static_cast<int> (tracks.size()); ++previous)
        y += trackHeightFor (tracks[static_cast<std::size_t> (previous)]) + rowGap;

    const auto height = index >= 0 && index < static_cast<int> (tracks.size())
        ? trackHeightFor (tracks[static_cast<std::size_t> (index)])
        : rowHeight;

    return juce::Rectangle<int> {
        horizontalPadding,
        y,
        std::max (0, getWidth() - (horizontalPadding * 2)),
        height
    };
}

void TrackListComponent::commitMixerStrip (const std::string& trackId, core::sequencing::MixerStrip mixerStrip)
{
    if (trackId.empty())
        return;

    const auto* track = appServices_.project().findTrackById (trackId);
    if (track == nullptr)
        return;

    if (track->mixerStrip() == mixerStrip)
        return;

    const auto result = appServices_.commandStack().execute (
        std::make_unique<core::commands::SetTrackMixerStripCommand> (trackId, mixerStrip));
    if (result.failed())
        appServices_.reportWarning ("Mixer edit failed: " + result.error());
    else
        appServices_.clearUserMessage();

    refresh();
}

void TrackListComponent::commitRouting (const std::string& trackId, core::sequencing::TrackRouting routing)
{
    if (trackId.empty())
        return;

    const auto result = appServices_.commandStack().execute (
        std::make_unique<core::commands::SetTrackRoutingCommand> (trackId, std::move (routing)));
    if (result.failed())
        appServices_.reportWarning ("Routing edit failed: " + result.error());
    else
        appServices_.clearUserMessage();

    refresh();
}
}
