#include "core/sequencing/PreparedExpressionRenderModel.h"

#include "core/diagnostics/PerformanceTrace.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
constexpr auto fnvOffsetBasis = PreparedExpressionFingerprint { 14695981039346656037ull };
constexpr auto fnvPrime = PreparedExpressionFingerprint { 1099511628211ull };

void hashByte (PreparedExpressionFingerprint& hash, unsigned char byte) noexcept
{
    hash ^= static_cast<PreparedExpressionFingerprint> (byte);
    hash *= fnvPrime;
}

void hashBytes (PreparedExpressionFingerprint& hash, const void* data, std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*> (data);
    for (auto index = std::size_t {}; index < size; ++index)
        hashByte (hash, bytes[index]);
}

void hashString (PreparedExpressionFingerprint& hash, const std::string& value) noexcept
{
    hashBytes (hash, value.data(), value.size());
    hashByte (hash, 0xffu);
}

void hashBool (PreparedExpressionFingerprint& hash, bool value) noexcept
{
    hashByte (hash, value ? 1u : 0u);
}

void hashInt64 (PreparedExpressionFingerprint& hash, std::int64_t value) noexcept
{
    hashBytes (hash, &value, sizeof (value));
}

void hashInt (PreparedExpressionFingerprint& hash, int value) noexcept
{
    hashBytes (hash, &value, sizeof (value));
}

void hashDouble (PreparedExpressionFingerprint& hash, double value) noexcept
{
    if (! std::isfinite (value))
        value = 0.0;

    if (value == 0.0)
        value = 0.0;

    hashBytes (hash, &value, sizeof (value));
}

template <typename Enum>
void hashEnum (PreparedExpressionFingerprint& hash, Enum value) noexcept
{
    hashInt (hash, static_cast<int> (value));
}

void hashPosition (PreparedExpressionFingerprint& hash, time::TickPosition position) noexcept
{
    hashInt64 (hash, position.ticks());
}

void hashDuration (PreparedExpressionFingerprint& hash, time::TickDuration duration) noexcept
{
    hashInt64 (hash, duration.ticks());
}

void hashRegion (PreparedExpressionFingerprint& hash, const Region& region) noexcept
{
    hashPosition (hash, region.start());
    hashPosition (hash, region.end());
}

void hashOptionalBlockId (PreparedExpressionFingerprint& hash, const std::optional<ExpressionBlockId>& blockId) noexcept
{
    hashBool (hash, blockId.has_value());
    if (blockId.has_value())
        hashString (hash, blockId->value);
}

PreparedExpressionFingerprint hashDestination (const ExpressionDestination& destination) noexcept
{
    auto hash = fnvOffsetBasis;
    hashEnum (hash, destination.kind);
    hashString (hash, destination.trackId);
    hashString (hash, destination.sendTargetTrackId);
    hashString (hash, destination.deviceSlotId.value);
    hashString (hash, destination.parameterId);
    hashInt (hash, destination.midiCcNumber);
    return hash;
}

PreparedExpressionFingerprint combineHashes (const std::vector<PreparedExpressionFingerprint>& hashes) noexcept
{
    auto hash = fnvOffsetBasis;
    for (const auto value : hashes)
        hashBytes (hash, &value, sizeof (value));
    return hash;
}

PreparedExpressionFingerprint hashSegment (const ExpressionSegment& segment) noexcept
{
    auto hash = fnvOffsetBasis;
    hashPosition (hash, segment.start);
    hashPosition (hash, segment.end);
    hashDouble (hash, segment.startValue);
    hashDouble (hash, segment.endValue);
    return hash;
}

PreparedExpressionFingerprint hashPreparedSegment (const PreparedExpressionValueSegment& segment) noexcept
{
    auto hash = fnvOffsetBasis;
    hashPosition (hash, segment.start);
    hashPosition (hash, segment.end);
    hashDouble (hash, segment.startValue);
    hashDouble (hash, segment.endValue);
    return hash;
}

PreparedExpressionFingerprint hashPitchSlur (const PreparedPitchSlurEvent& event) noexcept
{
    auto hash = fnvOffsetBasis;
    hashString (hash, event.id.value);
    hashString (hash, event.sourceNoteId);
    hashString (hash, event.destinationNoteId);
    hashDuration (hash, event.slurTime);
    hashEnum (hash, event.curveShape);
    hashBool (hash, event.legatoNoRetrigger);
    hashOptionalBlockId (hash, event.blockId);
    hashBool (hash, event.hasVoiceOverride);
    return hash;
}

PreparedExpressionFingerprint hashVibratoVoiceOverride (const PreparedVibratoVoiceOverride& override) noexcept
{
    auto hash = fnvOffsetBasis;
    hashString (hash, override.noteId);
    hashDouble (hash, override.amplitudeSemitones);
    hashDuration (hash, override.attackTime);
    hashDuration (hash, override.releaseTime);
    hashString (hash, override.frequencyDivisionId);
    hashEnum (hash, override.waveShape);
    hashDouble (hash, override.phase);
    return hash;
}

PreparedExpressionFingerprint hashVibrato (const PreparedVibratoEvent& event) noexcept
{
    auto hash = fnvOffsetBasis;
    hashString (hash, event.id.value);
    for (const auto& noteId : event.sourceNoteIds)
        hashString (hash, noteId);
    hashRegion (hash, event.phraseRegion);
    hashDuration (hash, event.attackTime);
    hashDuration (hash, event.releaseTime);
    hashDouble (hash, event.amplitudeSemitones);
    hashString (hash, event.frequencyDivisionId);
    hashEnum (hash, event.waveShape);
    hashDouble (hash, event.phase);
    hashOptionalBlockId (hash, event.blockId);
    for (const auto& override : event.voiceOverrides)
    {
        const auto overrideHash = hashVibratoVoiceOverride (override);
        hashBytes (hash, &overrideHash, sizeof (overrideHash));
    }
    return hash;
}

PreparedExpressionFingerprint hashRoute (const PreparedExpressionRouteRenderData& route) noexcept
{
    auto hash = fnvOffsetBasis;
    hashString (hash, route.routeId.value);
    const auto destinationHash = hashDestination (route.destination);
    hashBytes (hash, &destinationHash, sizeof (destinationHash));
    hashString (hash, route.destinationStableId);
    hashDouble (hash, route.outputMin);
    hashDouble (hash, route.outputMax);
    hashEnum (hash, route.smoothingPolicy);
    hashBool (hash, route.available);
    for (const auto& segment : route.outputSegments)
    {
        const auto segmentHash = hashPreparedSegment (segment);
        hashBytes (hash, &segmentHash, sizeof (segmentHash));
    }
    return hash;
}

PreparedExpressionFingerprint hashLane (const PreparedExpressionLaneRenderData& lane) noexcept
{
    auto hash = fnvOffsetBasis;
    hashString (hash, lane.laneId.value);
    hashString (hash, lane.name);
    hashEnum (hash, lane.polarity);
    hashBool (hash, lane.enabled);
    for (const auto& segment : lane.laneSegments)
    {
        const auto segmentHash = hashSegment (segment);
        hashBytes (hash, &segmentHash, sizeof (segmentHash));
    }
    for (const auto& route : lane.routes)
        hashBytes (hash, &route.fingerprint, sizeof (route.fingerprint));
    for (const auto& slur : lane.pitchSlurs)
    {
        const auto slurHash = hashPitchSlur (slur);
        hashBytes (hash, &slurHash, sizeof (slurHash));
    }
    for (const auto& vibrato : lane.vibratoEvents)
    {
        const auto vibratoHash = hashVibrato (vibrato);
        hashBytes (hash, &vibratoHash, sizeof (vibratoHash));
    }
    return hash;
}

PreparedExpressionFingerprint hashClip (const PreparedExpressionClipRenderData& clip) noexcept
{
    auto hash = fnvOffsetBasis;
    hashString (hash, clip.trackId);
    hashString (hash, clip.clipId);
    hashPosition (hash, clip.clipStartInProject);
    hashRegion (hash, clip.localRegion);
    for (const auto& lane : clip.lanes)
        hashBytes (hash, &lane.fingerprint, sizeof (lane.fingerprint));
    return hash;
}

PreparedExpressionValueSegment mappedRouteSegment (const ExpressionRoute& route,
                                                   const ExpressionSegment& segment,
                                                   ExpressionLanePolarity polarity) noexcept
{
    return PreparedExpressionValueSegment {
        segment.start,
        segment.end,
        mapExpressionRouteValue (route, segment.startValue, polarity),
        mapExpressionRouteValue (route, segment.endValue, polarity)
    };
}

PreparedPitchSlurEvent preparePitchSlur (const PitchSlur& slur)
{
    return PreparedPitchSlurEvent {
        slur.id(),
        slur.sourceNoteId(),
        slur.destinationNoteId(),
        slur.slurTime(),
        slur.curveShape(),
        slur.legatoNoRetrigger(),
        slur.blockId(),
        slur.hasVoiceOverride()
    };
}

PreparedVibratoVoiceOverride prepareVibratoVoiceOverride (const VibratoVoiceOverride& override)
{
    return PreparedVibratoVoiceOverride {
        override.noteId,
        override.amplitudeSemitones,
        override.attackTime,
        override.releaseTime,
        override.frequencyDivisionId,
        override.waveShape,
        override.phase
    };
}

PreparedVibratoEvent prepareVibrato (const VibratoExpression& vibrato)
{
    std::vector<PreparedVibratoVoiceOverride> voiceOverrides;
    voiceOverrides.reserve (vibrato.voiceOverrides().size());
    for (const auto& override : vibrato.voiceOverrides())
        voiceOverrides.push_back (prepareVibratoVoiceOverride (override));

    return PreparedVibratoEvent {
        vibrato.id(),
        vibrato.sourceNoteIds(),
        vibrato.phraseRegion(),
        vibrato.attackTime(),
        vibrato.releaseTime(),
        vibrato.amplitudeSemitones(),
        vibrato.frequencyDivisionId(),
        vibrato.waveShape(),
        vibrato.phase(),
        vibrato.blockId(),
        std::move (voiceOverrides)
    };
}
}

PreparedExpressionClipRenderData prepareExpressionClipRenderData (const Project& project,
                                                                  const Track& track,
                                                                  const MidiClip& clip,
                                                                  time::TickDuration segmentStep,
                                                                  const ExpressionEvaluationContext& context)
{
    diagnostics::ScopedPerformanceTimer timer { "PreparedExpressionRenderModel::prepareExpressionClipRenderData" };
    if (segmentStep.ticks() <= 0)
        throw std::invalid_argument ("Prepared expression segment step must be positive");

    PreparedExpressionClipRenderData prepared;
    prepared.trackId = track.id();
    prepared.clipId = clip.id();
    prepared.clipStartInProject = clip.startInProject();
    prepared.localRegion = Region { time::TickPosition {}, time::TickPosition::fromTicks (clip.length().ticks()) };

    prepared.lanes.reserve (clip.expressionState().lanes().size());
    for (const auto& lane : clip.expressionState().lanes())
    {
        PreparedExpressionLaneRenderData preparedLane;
        preparedLane.laneId = lane.id();
        preparedLane.name = lane.name();
        preparedLane.polarity = lane.polarity();
        preparedLane.enabled = lane.enabled();

        buildExpressionSegments (lane, prepared.localRegion, segmentStep, context, preparedLane.laneSegments);

        preparedLane.routes.reserve (lane.routes().size());
        for (const auto& route : lane.routes())
        {
            if (! route.enabled())
                continue;

            const auto metadata = expressionDestinationMetadata (project, route.destination());
            PreparedExpressionRouteRenderData preparedRoute;
            preparedRoute.routeId = route.id();
            preparedRoute.destination = route.destination();
            preparedRoute.destinationStableId = metadata.stableId;
            preparedRoute.outputMin = route.outputMin();
            preparedRoute.outputMax = route.outputMax();
            preparedRoute.smoothingPolicy = defaultExpressionRouteSmoothingPolicy (route.destination());
            preparedRoute.available = metadata.available;
            preparedRoute.outputSegments.reserve (preparedLane.laneSegments.size());

            if (preparedLane.enabled && preparedRoute.available)
            {
                for (const auto& segment : preparedLane.laneSegments)
                    preparedRoute.outputSegments.push_back (mappedRouteSegment (route, segment, lane.polarity()));
            }

            preparedRoute.fingerprint = hashRoute (preparedRoute);
            preparedLane.routes.push_back (std::move (preparedRoute));
        }

        preparedLane.pitchSlurs.reserve (lane.pitchSlurs().size());
        for (const auto& slur : lane.pitchSlurs())
            preparedLane.pitchSlurs.push_back (preparePitchSlur (slur));

        preparedLane.vibratoEvents.reserve (lane.vibratoExpressions().size());
        for (const auto& vibrato : lane.vibratoExpressions())
            preparedLane.vibratoEvents.push_back (prepareVibrato (vibrato));

        preparedLane.fingerprint = hashLane (preparedLane);
        prepared.lanes.push_back (std::move (preparedLane));
    }

    prepared.fingerprint = hashClip (prepared);
    return prepared;
}

PreparedExpressionRenderModel prepareExpressionRenderModel (const Project& project,
                                                            time::TickDuration segmentStep,
                                                            ExpressionEvaluationContext context)
{
    diagnostics::ScopedPerformanceTimer timer { "PreparedExpressionRenderModel::prepareExpressionRenderModel" };
    context.rhythmSettings = project.rhythmSettings();

    PreparedExpressionRenderModel model;
    for (const auto& track : project.tracks())
    {
        model.clips.reserve (model.clips.size() + track.clips().size());
        for (const auto& clip : track.clips())
            model.clips.push_back (prepareExpressionClipRenderData (project, track, clip, segmentStep, context));
    }

    std::vector<PreparedExpressionFingerprint> clipHashes;
    clipHashes.reserve (model.clips.size());
    for (const auto& clip : model.clips)
        clipHashes.push_back (clip.fingerprint);
    model.fingerprint = combineHashes (clipHashes);
    return model;
}

PreparedExpressionFingerprint preparedExpressionFingerprint (const PreparedExpressionRenderModel& model) noexcept
{
    return model.fingerprint;
}
}
