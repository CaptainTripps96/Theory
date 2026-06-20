#include "core/sequencing/PreparedExpressionRenderModel.h"

#include "core/devices/FirstPartyDeviceRegistry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

namespace
{
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickDuration beats (int count)
{
    return TickDuration::fromTicks (static_cast<std::int64_t> (count) * ticksPerQuarterNote);
}

TickPosition beat (int count)
{
    return TickPosition::fromTicks (static_cast<std::int64_t> (count) * ticksPerQuarterNote);
}

Project preparedExpressionProject()
{
    Project project { "project-1", "Prepared Expression" };
    Track track { "track-1", "Lead" };

    DeviceChain chain;
    chain.appendSlot (DeviceSlot {
        DeviceSlotId { "simple-osc-complex" },
        tsq::core::devices::defaultFirstPartyDeviceState (tsq::core::devices::simpleOscComplexDefinition()),
        PluginKind::instrument
    });
    track.setDeviceChain (chain);

    MidiClip clip { "clip-1", "Motif", beat (0), beats (4) };
    ExpressionState expression;
    ExpressionLane lane { ExpressionLaneId { "expr-motion" }, "Motion", ExpressionLanePolarity::unipolar };
    lane.addRoute (ExpressionRoute {
        ExpressionRouteId { "route-pm" },
        ExpressionDestination::firstPartyParameter ("track-1", DeviceSlotId { "simple-osc-complex" }, "osc.pm.amount"),
        0.25,
        0.75
    });

    PhraseEnvelopeClip envelope {
        ExpressionClipId { "env-1" },
        { "note-1" },
        Region { beat (0), beat (2) },
        0.0,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    };
    lane.addPhraseEnvelopeClip (envelope);
    expression.addLane (lane);

    clip.setExpressionState (expression);
    track.addClip (clip);
    project.addTrack (track);
    return project;
}

PreparedExpressionRenderModel prepare (const Project& project)
{
    return prepareExpressionRenderModel (project, beats (1));
}

Project pitchSemanticMetadataProject()
{
    Project project { "project-1", "Pitch Semantics" };
    Track track { "track-1", "Lead" };
    MidiClip clip { "clip-1", "Motif", beat (0), beats (4) };

    ExpressionState expression;
    auto* lane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    PitchSlur slur { ExpressionClipId { "slur-1" }, "note-1", "note-2" };
    slur.setSlurTime (beats (1));
    slur.setBlockId (ExpressionBlockId { "slur-block" });
    slur.setHasVoiceOverride (true);
    lane->addPitchSlur (slur);

    VibratoExpression vibrato { ExpressionClipId { "vibrato-1" }, { "note-2" }, Region { beat (1), beat (3) } };
    vibrato.setAmplitudeSemitones (0.5);
    vibrato.setFrequencyDivisionId ("eighth");
    vibrato.setBlockId (ExpressionBlockId { "vibrato-block" });
    vibrato.setVoiceOverrides ({
        VibratoVoiceOverride {
            "note-2",
            0.25,
            beats (1),
            beats (1),
            "sixteenth",
            CyclicWaveShape::triangle,
            0.5
        }
    });
    lane->addVibratoExpression (vibrato);
    clip.setExpressionState (expression);
    track.addClip (clip);
    project.addTrack (track);
    return project;
}
}

TEST_CASE ("Prepared expression render model builds lane and route segment data")
{
    const auto project = preparedExpressionProject();
    const auto model = prepare (project);

    REQUIRE (model.clips.size() == 1);
    const auto& clip = model.clips.front();
    CHECK (clip.trackId == "track-1");
    CHECK (clip.clipId == "clip-1");
    CHECK (clip.localRegion.start() == beat (0));
    CHECK (clip.localRegion.end() == beat (4));

    REQUIRE (clip.lanes.size() == 3);
    const auto& lane = clip.lanes.back();
    CHECK (lane.laneId.value == "expr-motion");
    REQUIRE (lane.laneSegments.size() == 4);
    CHECK (lane.laneSegments[0].startValue == Catch::Approx (0.0));
    CHECK (lane.laneSegments[0].endValue == Catch::Approx (1.0));

    REQUIRE (lane.routes.size() == 1);
    const auto& route = lane.routes.front();
    CHECK (route.available);
    CHECK (route.destinationStableId == "first-party-parameter:track-1:simple-osc-complex:osc.pm.amount");
    CHECK (route.outputMin == Catch::Approx (0.25));
    CHECK (route.outputMax == Catch::Approx (0.75));
    REQUIRE (route.outputSegments.size() == lane.laneSegments.size());
    CHECK (route.outputSegments[0].startValue == Catch::Approx (0.25));
    CHECK (route.outputSegments[0].endValue == Catch::Approx (0.75));
    CHECK (route.fingerprint != 0);
    CHECK (lane.fingerprint != 0);
    CHECK (clip.fingerprint != 0);
    CHECK (preparedExpressionFingerprint (model) != 0);
}

TEST_CASE ("Prepared expression fingerprints are stable until render-relevant data changes")
{
    auto project = preparedExpressionProject();
    const auto first = prepare (project);
    const auto second = prepare (project);
    CHECK (preparedExpressionFingerprint (first) == preparedExpressionFingerprint (second));

    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto expression = clip->expressionState();
    auto* lane = expression.findLane (ExpressionLaneId { "expr-motion" });
    REQUIRE (lane != nullptr);
    auto* route = lane->findRoute (ExpressionRouteId { "route-pm" });
    REQUIRE (route != nullptr);
    route->setOutputRange (0.0, 1.0);
    clip->setExpressionState (expression);

    const auto changed = prepare (project);
    CHECK (preparedExpressionFingerprint (changed) != preparedExpressionFingerprint (first));
}

TEST_CASE ("Prepared expression captures pitch semantic event streams")
{
    const auto project = pitchSemanticMetadataProject();
    const auto model = prepare (project);
    REQUIRE (model.clips.size() == 1);
    REQUIRE (model.clips.front().lanes.size() == 2);

    const auto& preparedLane = model.clips.front().lanes.back();
    REQUIRE (preparedLane.pitchSlurs.size() == 1);
    CHECK (preparedLane.pitchSlurs.front().id.value == "slur-1");
    CHECK (preparedLane.pitchSlurs.front().slurTime == beats (1));
    CHECK (preparedLane.pitchSlurs.front().hasVoiceOverride);
    REQUIRE (preparedLane.pitchSlurs.front().blockId.has_value());
    CHECK (preparedLane.pitchSlurs.front().blockId->value == "slur-block");

    REQUIRE (preparedLane.vibratoEvents.size() == 1);
    const auto& preparedVibrato = preparedLane.vibratoEvents.front();
    CHECK (preparedVibrato.id.value == "vibrato-1");
    CHECK (preparedVibrato.amplitudeSemitones == Catch::Approx (0.5));
    CHECK (preparedVibrato.frequencyDivisionId == "eighth");
    REQUIRE (preparedVibrato.blockId.has_value());
    CHECK (preparedVibrato.blockId->value == "vibrato-block");
    REQUIRE (preparedVibrato.voiceOverrides.size() == 1);
    CHECK (preparedVibrato.voiceOverrides.front().noteId == "note-2");
    CHECK (preparedVibrato.voiceOverrides.front().amplitudeSemitones == Catch::Approx (0.25));
    CHECK (preparedVibrato.voiceOverrides.front().attackTime == beats (1));
    CHECK (preparedVibrato.voiceOverrides.front().releaseTime == beats (1));
    CHECK (preparedVibrato.voiceOverrides.front().frequencyDivisionId == "sixteenth");
    CHECK (preparedVibrato.voiceOverrides.front().waveShape == CyclicWaveShape::triangle);
    CHECK (preparedVibrato.voiceOverrides.front().phase == Catch::Approx (0.5));
}

TEST_CASE ("Prepared expression fingerprints include pitch semantic block and override metadata")
{
    auto project = pitchSemanticMetadataProject();
    const auto baseline = prepare (project);

    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);

    auto expression = clip->expressionState();
    auto* lane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    auto* slur = lane->findPitchSlur (ExpressionClipId { "slur-1" });
    REQUIRE (slur != nullptr);
    slur->setBlockId (ExpressionBlockId { "slur-block-edited" });
    clip->setExpressionState (expression);

    CHECK (preparedExpressionFingerprint (prepare (project)) != preparedExpressionFingerprint (baseline));

    project = pitchSemanticMetadataProject();
    track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    expression = clip->expressionState();
    lane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    auto* vibrato = lane->findVibratoExpression (ExpressionClipId { "vibrato-1" });
    REQUIRE (vibrato != nullptr);
    vibrato->setBlockId (ExpressionBlockId { "vibrato-block-edited" });
    clip->setExpressionState (expression);

    CHECK (preparedExpressionFingerprint (prepare (project)) != preparedExpressionFingerprint (baseline));

    project = pitchSemanticMetadataProject();
    track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    expression = clip->expressionState();
    lane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    vibrato = lane->findVibratoExpression (ExpressionClipId { "vibrato-1" });
    REQUIRE (vibrato != nullptr);
    vibrato->setVoiceOverrides ({
        VibratoVoiceOverride {
            "note-2",
            0.25,
            beats (2),
            beats (1),
            "sixteenth",
            CyclicWaveShape::triangle,
            0.5
        }
    });
    clip->setExpressionState (expression);

    CHECK (preparedExpressionFingerprint (prepare (project)) != preparedExpressionFingerprint (baseline));

    project = pitchSemanticMetadataProject();
    track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    expression = clip->expressionState();
    lane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    vibrato = lane->findVibratoExpression (ExpressionClipId { "vibrato-1" });
    REQUIRE (vibrato != nullptr);
    vibrato->setVoiceOverrides ({
        VibratoVoiceOverride {
            "note-2",
            0.75,
            beats (1),
            beats (1),
            "sixteenth",
            CyclicWaveShape::triangle,
            0.5
        }
    });
    clip->setExpressionState (expression);

    CHECK (preparedExpressionFingerprint (prepare (project)) != preparedExpressionFingerprint (baseline));
}

TEST_CASE ("Prepared expression keeps unavailable routes addressable without output streams")
{
    Project project { "project-1", "Unavailable Route" };
    Track track { "track-1", "Lead" };
    MidiClip clip { "clip-1", "Motif", beat (0), beats (2) };

    ExpressionState expression;
    ExpressionLane lane { ExpressionLaneId { "expr-motion" }, "Motion", ExpressionLanePolarity::unipolar };
    lane.addRoute (ExpressionRoute {
        ExpressionRouteId { "route-missing" },
        ExpressionDestination::firstPartyParameter ("track-1", DeviceSlotId { "missing" }, "osc.pm.amount"),
        0.0,
        1.0
    });
    lane.addPhraseEnvelopeClip (PhraseEnvelopeClip {
        ExpressionClipId { "env-1" },
        { "note-1" },
        Region { beat (0), beat (1) },
        0.0,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    });
    expression.addLane (lane);
    clip.setExpressionState (expression);
    track.addClip (clip);
    project.addTrack (track);

    const auto model = prepare (project);
    REQUIRE (model.clips.size() == 1);
    REQUIRE (model.clips.front().lanes.size() == 3);
    const auto& route = model.clips.front().lanes.back().routes.front();
    CHECK_FALSE (route.available);
    CHECK (route.outputSegments.empty());
    CHECK (route.destinationStableId == "first-party-parameter:track-1:missing:osc.pm.amount");
}

TEST_CASE ("Prepared expression does not render route streams for bypassed lanes")
{
    auto project = preparedExpressionProject();
    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);

    auto expression = clip->expressionState();
    auto* lane = expression.findLane (ExpressionLaneId { "expr-motion" });
    REQUIRE (lane != nullptr);
    lane->setEnabled (false);
    clip->setExpressionState (expression);

    const auto model = prepare (project);
    REQUIRE (model.clips.size() == 1);
    const auto& preparedLane = model.clips.front().lanes.back();
    CHECK_FALSE (preparedLane.enabled);
    REQUIRE (preparedLane.routes.size() == 1);
    CHECK (preparedLane.routes.front().available);
    CHECK (preparedLane.routes.front().outputSegments.empty());
}
