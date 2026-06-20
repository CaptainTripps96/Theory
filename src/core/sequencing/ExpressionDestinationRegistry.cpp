#include "core/sequencing/ExpressionDestinationRegistry.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
std::string trackLabel (const Track* track, const std::string& fallback)
{
    if (track == nullptr)
        return fallback.empty() ? "Missing Track" : fallback;

    return track->name().empty() ? track->id() : track->name();
}

std::string deviceLabel (const DeviceSlot* slot)
{
    if (slot == nullptr)
        return "Missing Device";

    if (slot->isFirstPartyDevice() && slot->firstPartyDevice().has_value())
    {
        if (const auto* definition = devices::findFirstPartyDeviceDefinition (slot->firstPartyDevice()->typeId))
            return definition->name;

        return slot->firstPartyDevice()->typeId;
    }

    if (! slot->plugin().pluginName.empty())
        return slot->plugin().pluginName;

    return slot->id().value;
}

const Track* findTrack (const Project& project, const std::string& trackId)
{
    return project.findTrackById (trackId);
}

const Track* findReturnTrack (const Project& project, const std::string& trackId)
{
    const auto* track = project.findTrackById (trackId);
    return track != nullptr && track->type() == TrackType::returnTrack ? track : nullptr;
}

const DeviceSlot* findSlot (const Project& project, const ExpressionDestination& destination)
{
    const auto* track = findTrack (project, destination.trackId);
    return track == nullptr ? nullptr : track->deviceChain().findSlot (destination.deviceSlotId);
}

ExpressionDestinationMetadata baseMetadata (const Project& project,
                                            const ExpressionDestination& destination,
                                            std::string displayName,
                                            double outputMin,
                                            double outputMax,
                                            std::string units = {},
                                            bool discrete = false)
{
    const auto* track = findTrack (project, destination.trackId);
    const auto support = expressionDestinationRuntimeSupport (destination);
    const auto available = expressionDestinationIsAvailable (project, destination);
    return ExpressionDestinationMetadata {
        destination,
        destination.stableId(),
        std::move (displayName),
        trackLabel (track, destination.trackId),
        outputMin,
        outputMax,
        std::move (units),
        discrete,
        available,
        available,
        available && support.playbackMapped,
        available && support.plainMidiExportMapped,
        available ? support.statusLabel : std::string { "Unavailable" },
        available ? support.detailText : std::string { "This expression route points to something that is missing or invalid." }
    };
}

std::string midiCcDisplayName (int ccNumber)
{
    switch (ccNumber)
    {
        case 1: return "MIDI CC 1 Mod Wheel";
        case 11: return "MIDI CC 11 Expression";
        case 74: return "MIDI CC 74 Brightness";
        default: return "MIDI CC " + std::to_string (ccNumber);
    }
}

void appendTrackDestinations (const Project& project,
                              const Track& track,
                              std::vector<ExpressionDestinationMetadata>& output)
{
    output.push_back (expressionDestinationMetadata (project, ExpressionDestination::trackVolume (track.id())));
    output.push_back (expressionDestinationMetadata (project, ExpressionDestination::trackPan (track.id())));
    output.push_back (expressionDestinationMetadata (project, ExpressionDestination::pitchBend (track.id())));
    output.push_back (expressionDestinationMetadata (project, ExpressionDestination::pitch (track.id())));
    output.push_back (expressionDestinationMetadata (project, ExpressionDestination::midiCc (track.id(), 1)));
    output.push_back (expressionDestinationMetadata (project, ExpressionDestination::midiCc (track.id(), 11)));
    output.push_back (expressionDestinationMetadata (project, ExpressionDestination::midiCc (track.id(), 74)));
}

void appendSendDestinations (const Project& project,
                             const Track& track,
                             std::vector<ExpressionDestinationMetadata>& output)
{
    for (const auto& candidate : project.tracks())
    {
        if (candidate.type() != TrackType::returnTrack)
            continue;

        output.push_back (expressionDestinationMetadata (project, ExpressionDestination::sendLevel (track.id(), candidate.id())));
    }
}

void appendDeviceDestinations (const Project& project,
                               const Track& track,
                               std::vector<ExpressionDestinationMetadata>& output)
{
    for (const auto& slot : track.deviceChain().slots())
    {
        if (slot.isFirstPartyDevice() && slot.firstPartyDevice().has_value())
        {
            const auto* definition = devices::findFirstPartyDeviceDefinition (slot.firstPartyDevice()->typeId);
            if (definition == nullptr)
                continue;

            for (const auto& parameter : devices::expressionTargetParameters (*definition))
            {
                output.push_back (expressionDestinationMetadata (
                    project,
                    ExpressionDestination::firstPartyParameter (track.id(), slot.id(), parameter.id)));
            }
        }
    }
}
}

std::string expressionDestinationKindId (ExpressionDestinationKind kind)
{
    switch (kind)
    {
        case ExpressionDestinationKind::trackVolume: return "trackVolume";
        case ExpressionDestinationKind::trackPan: return "trackPan";
        case ExpressionDestinationKind::pitch: return "pitch";
        case ExpressionDestinationKind::pitchBend: return "pitchBend";
        case ExpressionDestinationKind::firstPartyParameter: return "firstPartyParameter";
        case ExpressionDestinationKind::pluginParameter: return "pluginParameter";
        case ExpressionDestinationKind::midiCc: return "midiCc";
        case ExpressionDestinationKind::sendLevel: return "sendLevel";
    }

    return "trackVolume";
}

ExpressionDestinationKind expressionDestinationKindFromId (const std::string& id)
{
    if (id == "trackVolume") return ExpressionDestinationKind::trackVolume;
    if (id == "trackPan") return ExpressionDestinationKind::trackPan;
    if (id == "pitch") return ExpressionDestinationKind::pitch;
    if (id == "pitchBend") return ExpressionDestinationKind::pitchBend;
    if (id == "firstPartyParameter") return ExpressionDestinationKind::firstPartyParameter;
    if (id == "pluginParameter") return ExpressionDestinationKind::pluginParameter;
    if (id == "midiCc") return ExpressionDestinationKind::midiCc;
    if (id == "sendLevel") return ExpressionDestinationKind::sendLevel;

    throw std::invalid_argument ("Unknown expression destination kind '" + id + "'");
}

double mapExpressionRouteValue (const ExpressionRoute& route,
                                double laneValue,
                                ExpressionLanePolarity polarity,
                                ExpressionRouteMappingCurve) noexcept
{
    return route.mapLaneValue (laneValue, polarity);
}

ExpressionRouteSmoothingPolicy defaultExpressionRouteSmoothingPolicy (const ExpressionDestination&) noexcept
{
    return ExpressionRouteSmoothingPolicy::none;
}

ExpressionDestinationRuntimeSupport expressionDestinationRuntimeSupport (const ExpressionDestination& destination)
{
    switch (destination.kind)
    {
        case ExpressionDestinationKind::trackVolume:
        case ExpressionDestinationKind::trackPan:
        case ExpressionDestinationKind::sendLevel:
            return {
                true,
                false,
                "Playback",
                "Mapped to Tracktion mixer playback. Plain MIDI export stores the semantic route in the project but does not bake it into the MIDI file."
            };

        case ExpressionDestinationKind::firstPartyParameter:
            return {
                true,
                false,
                "Playback",
                "Mapped to first-party device playback. Plain MIDI export stores the semantic route in the project but does not bake it into the MIDI file."
            };

        case ExpressionDestinationKind::midiCc:
            return {
                false,
                true,
                "Export only",
                "Stored with the project and can be baked into plain MIDI CC export when expression MIDI CC export is enabled. Live playback is not mapped yet."
            };

        case ExpressionDestinationKind::pitch:
        case ExpressionDestinationKind::pitchBend:
            return {
                false,
                false,
                "Stored only",
                "Stored with the project, but generic expression pitch routes are not playback- or MIDI-export-mapped yet. Use Pitch lane slurs and vibrato for first-party pitch expression."
            };

        case ExpressionDestinationKind::pluginParameter:
            return {
                false,
                false,
                "Stored only",
                "Stored with the project, but third-party plugin parameter expression playback is not mapped yet."
            };
    }

    return { false, false, "Stored only", "Stored with the project." };
}

const devices::FirstPartyParameterDefinition* findFirstPartyExpressionTarget (const DeviceSlot& slot,
                                                                              const std::string& parameterId)
{
    if (! slot.isFirstPartyDevice() || ! slot.firstPartyDevice().has_value())
        return nullptr;

    const auto* definition = devices::findFirstPartyDeviceDefinition (slot.firstPartyDevice()->typeId);
    if (definition == nullptr)
        return nullptr;

    const auto* parameter = devices::findFirstPartyParameterDefinition (*definition, parameterId);
    return parameter != nullptr && parameter->expressionTarget ? parameter : nullptr;
}

bool expressionDestinationIsAvailable (const Project& project, const ExpressionDestination& destination)
{
    if (! destination.isValid())
        return false;

    const auto* track = findTrack (project, destination.trackId);
    if (track == nullptr)
        return false;

    switch (destination.kind)
    {
        case ExpressionDestinationKind::trackVolume:
        case ExpressionDestinationKind::trackPan:
        case ExpressionDestinationKind::pitchBend:
            return true;

        case ExpressionDestinationKind::pitch:
            return destination.deviceSlotId.empty() || track->deviceChain().findSlot (destination.deviceSlotId) != nullptr;

        case ExpressionDestinationKind::firstPartyParameter:
        {
            const auto* slot = track->deviceChain().findSlot (destination.deviceSlotId);
            return slot != nullptr && findFirstPartyExpressionTarget (*slot, destination.parameterId) != nullptr;
        }

        case ExpressionDestinationKind::pluginParameter:
        {
            const auto* slot = track->deviceChain().findSlot (destination.deviceSlotId);
            return slot != nullptr && slot->isPluginDevice();
        }

        case ExpressionDestinationKind::midiCc:
            return destination.midiCcNumber >= 0 && destination.midiCcNumber <= 127;

        case ExpressionDestinationKind::sendLevel:
            return findReturnTrack (project, destination.sendTargetTrackId) != nullptr;
    }

    return false;
}

ExpressionDestinationMetadata expressionDestinationMetadata (const Project& project, const ExpressionDestination& destination)
{
    switch (destination.kind)
    {
        case ExpressionDestinationKind::trackVolume:
            return baseMetadata (project, destination, "Track Volume", 0.0, 1.0);

        case ExpressionDestinationKind::trackPan:
            return baseMetadata (project, destination, "Track Pan", -1.0, 1.0);

        case ExpressionDestinationKind::pitch:
            return baseMetadata (project, destination, "Pitch", -48.0, 48.0, "st");

        case ExpressionDestinationKind::pitchBend:
            return baseMetadata (project, destination, "Pitch Bend", -1.0, 1.0);

        case ExpressionDestinationKind::midiCc:
            return baseMetadata (project,
                                 destination,
                                 midiCcDisplayName (destination.midiCcNumber),
                                 0.0,
                                 127.0,
                                 {},
                                 true);

        case ExpressionDestinationKind::sendLevel:
        {
            auto metadata = baseMetadata (project, destination, "Send Level", 0.0, 1.0);
            metadata.detailText = trackLabel (findTrack (project, destination.trackId), destination.trackId)
                + " -> "
                + trackLabel (findReturnTrack (project, destination.sendTargetTrackId), destination.sendTargetTrackId);
            return metadata;
        }

        case ExpressionDestinationKind::pluginParameter:
        {
            auto metadata = baseMetadata (project, destination, destination.parameterId, 0.0, 1.0);
            metadata.detailText = deviceLabel (findSlot (project, destination));
            return metadata;
        }

        case ExpressionDestinationKind::firstPartyParameter:
        {
            auto metadata = baseMetadata (project, destination, destination.parameterId, 0.0, 1.0);
            const auto* slot = findSlot (project, destination);
            metadata.detailText = deviceLabel (slot);
            if (slot == nullptr)
                return metadata;

            if (const auto* parameter = findFirstPartyExpressionTarget (*slot, destination.parameterId))
            {
                metadata.displayName = parameter->name;
                metadata.defaultOutputMin = parameter->minimumValue;
                metadata.defaultOutputMax = parameter->maximumValue;
                metadata.units = parameter->units;
                metadata.discrete = parameter->valueType == devices::FirstPartyParameterValueType::discrete;
                metadata.expressionTarget = true;
            }
            return metadata;
        }
    }

    return baseMetadata (project, destination, destination.stableId(), 0.0, 1.0);
}

std::vector<ExpressionDestinationMetadata> expressionDestinationMetadataForTrack (const Project& project, const Track& track)
{
    std::vector<ExpressionDestinationMetadata> result;
    result.reserve (8 + track.deviceChain().slots().size() * 8);

    appendTrackDestinations (project, track, result);
    appendSendDestinations (project, track, result);
    appendDeviceDestinations (project, track, result);

    return result;
}
}
