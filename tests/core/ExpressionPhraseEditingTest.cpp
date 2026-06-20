#include "core/commands/CommandStack.h"
#include "core/commands/ExpressionCommands.h"
#include "core/commands/ProjectCommandContext.h"
#include "core/sequencing/ExpressionPhraseEditing.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdint>
#include <memory>

namespace
{
using namespace tsq;

core::time::TickPosition beat (int beatIndex)
{
    return core::time::TickPosition::fromTicks (static_cast<std::int64_t> (beatIndex) * core::time::ticksPerQuarterNote);
}

core::time::TickDuration beats (int beatCount)
{
    return core::time::TickDuration::fromTicks (static_cast<std::int64_t> (beatCount) * core::time::ticksPerQuarterNote);
}

core::sequencing::MidiClip clipWithNotes()
{
    core::sequencing::MidiClip clip { "clip-1", "Phrase Clip", beat (0), beats (4) };
    clip.addNote (core::sequencing::MidiNote {
        "note-1",
        core::music_theory::MidiPitch::fromValue (60),
        beat (0),
        beats (1),
        100
    });
    clip.addNote (core::sequencing::MidiNote {
        "note-2",
        core::music_theory::MidiPitch::fromValue (64),
        beat (2),
        beats (1),
        100
    });
    return clip;
}

core::sequencing::Project projectWithClip (core::sequencing::MidiClip clip)
{
    core::sequencing::Project project { "project-1", "Project 1" };
    core::sequencing::Track track { "track-1", "Track 1" };
    track.addClip (clip);
    project.addTrack (track);
    return project;
}
}

TEST_CASE ("Phrase envelope editing creates attack envelopes from selected notes", "[expression][phrase]")
{
    const auto clip = clipWithNotes();
    auto envelope = core::sequencing::createPhraseEnvelopeForSelection (
        core::sequencing::ExpressionClipId { "env-1" },
        clip,
        { "note-1", "note-2" },
        beats (1),
        0.52,
        core::sequencing::ExpressionLanePolarity::unipolar);

    REQUIRE (envelope.has_value());
    CHECK (envelope->phraseRegion().start() == beat (0));
    CHECK (envelope->phraseRegion().end() == beat (3));
    CHECK (envelope->storedLevel() == Catch::Approx (0.52));
    CHECK (envelope->attackStage().duration == beats (1));
    CHECK (envelope->attackStage().startLevel == Catch::Approx (0.0));
    CHECK (envelope->attackStage().endLevel == Catch::Approx (0.52));

    const auto bipolar = core::sequencing::createPhraseEnvelopeForSelection (
        core::sequencing::ExpressionClipId { "env-2" },
        clip,
        { "note-1" },
        beats (1),
        0.1,
        core::sequencing::ExpressionLanePolarity::bipolar);
    REQUIRE (bipolar.has_value());
    CHECK (bipolar->attackStage().startLevel == Catch::Approx (-1.0));
}

TEST_CASE ("Phrase envelope keyboard edits enforce timing and level rules", "[expression][phrase]")
{
    const auto clip = clipWithNotes();
    auto envelope = core::sequencing::createPhraseEnvelopeForSelection (
        core::sequencing::ExpressionClipId { "env-1" },
        clip,
        { "note-1", "note-2" },
        beats (1),
        0.4,
        core::sequencing::ExpressionLanePolarity::unipolar);
    REQUIRE (envelope.has_value());

    auto activeSegment = core::sequencing::PhraseEnvelopeActiveSegment::attack;
    CHECK_FALSE (core::sequencing::editPhraseEnvelope (*envelope,
                                                       core::sequencing::PhraseEnvelopeEditKey::attack,
                                                       core::sequencing::PhraseEnvelopeEditDirection::right,
                                                       beats (3),
                                                       core::sequencing::ExpressionLanePolarity::unipolar,
                                                       activeSegment));
    CHECK (envelope->attackStage().duration == beats (1));

    CHECK (core::sequencing::editPhraseEnvelope (*envelope,
                                                 core::sequencing::PhraseEnvelopeEditKey::attack,
                                                 core::sequencing::PhraseEnvelopeEditDirection::left,
                                                 beats (1),
                                                 core::sequencing::ExpressionLanePolarity::unipolar,
                                                 activeSegment));
    CHECK (envelope->attackStage().duration == core::time::TickDuration {});

    CHECK (core::sequencing::editPhraseEnvelope (*envelope,
                                                 core::sequencing::PhraseEnvelopeEditKey::decay,
                                                 core::sequencing::PhraseEnvelopeEditDirection::right,
                                                 beats (1),
                                                 core::sequencing::ExpressionLanePolarity::unipolar,
                                                 activeSegment));
    REQUIRE (envelope->decayStage().has_value());
    REQUIRE (envelope->peakLevel().has_value());
    REQUIRE (envelope->sustainLevel().has_value());
    CHECK (*envelope->peakLevel() == Catch::Approx (0.65));
    CHECK (*envelope->sustainLevel() == Catch::Approx (0.50));

    CHECK (core::sequencing::editPhraseEnvelope (*envelope,
                                                 core::sequencing::PhraseEnvelopeEditKey::decay,
                                                 core::sequencing::PhraseEnvelopeEditDirection::down,
                                                 beats (1),
                                                 core::sequencing::ExpressionLanePolarity::unipolar,
                                                 activeSegment));
    CHECK (*envelope->sustainLevel() == Catch::Approx (0.45));

    CHECK (core::sequencing::editPhraseEnvelope (*envelope,
                                                 core::sequencing::PhraseEnvelopeEditKey::force,
                                                 core::sequencing::PhraseEnvelopeEditDirection::up,
                                                 beats (1),
                                                 core::sequencing::ExpressionLanePolarity::unipolar,
                                                 activeSegment));
    CHECK (*envelope->peakLevel() == Catch::Approx (0.70));

    CHECK (core::sequencing::editPhraseEnvelope (*envelope,
                                                 core::sequencing::PhraseEnvelopeEditKey::release,
                                                 core::sequencing::PhraseEnvelopeEditDirection::left,
                                                 beats (1),
                                                 core::sequencing::ExpressionLanePolarity::unipolar,
                                                 activeSegment));
    REQUIRE (envelope->releaseStage().has_value());
    CHECK (envelope->releaseStage()->endLevel == Catch::Approx (0.0));

    CHECK (core::sequencing::editPhraseEnvelope (*envelope,
                                                 core::sequencing::PhraseEnvelopeEditKey::curve,
                                                 core::sequencing::PhraseEnvelopeEditDirection::up,
                                                 beats (1),
                                                 core::sequencing::ExpressionLanePolarity::unipolar,
                                                 activeSegment));
    CHECK (envelope->releaseStage()->curveShape == core::sequencing::ExpressionCurveShape::logarithmic);
}

TEST_CASE ("Phrase envelope edits create visible decay and release stages from first gesture", "[expression][phrase]")
{
    const auto clip = clipWithNotes();

    auto decayLeftEnvelope = core::sequencing::createPhraseEnvelopeForSelection (
        core::sequencing::ExpressionClipId { "env-decay-left" },
        clip,
        { "note-1", "note-2" },
        beats (1),
        0.4,
        core::sequencing::ExpressionLanePolarity::unipolar);
    REQUIRE (decayLeftEnvelope.has_value());

    auto activeSegment = core::sequencing::PhraseEnvelopeActiveSegment::attack;
    REQUIRE (core::sequencing::editPhraseEnvelope (*decayLeftEnvelope,
                                                   core::sequencing::PhraseEnvelopeEditKey::decay,
                                                   core::sequencing::PhraseEnvelopeEditDirection::left,
                                                   beats (1),
                                                   core::sequencing::ExpressionLanePolarity::unipolar,
                                                   activeSegment));
    REQUIRE (decayLeftEnvelope->decayStage().has_value());
    CHECK (activeSegment == core::sequencing::PhraseEnvelopeActiveSegment::decay);
    CHECK (decayLeftEnvelope->decayStage()->duration == beats (1));
    CHECK (decayLeftEnvelope->decayStage()->startLevel == Catch::Approx (0.65));
    CHECK (decayLeftEnvelope->decayStage()->endLevel == Catch::Approx (0.50));

    auto decayDownEnvelope = core::sequencing::createPhraseEnvelopeForSelection (
        core::sequencing::ExpressionClipId { "env-decay-down" },
        clip,
        { "note-1", "note-2" },
        beats (1),
        0.4,
        core::sequencing::ExpressionLanePolarity::unipolar);
    REQUIRE (decayDownEnvelope.has_value());

    activeSegment = core::sequencing::PhraseEnvelopeActiveSegment::attack;
    REQUIRE (core::sequencing::editPhraseEnvelope (*decayDownEnvelope,
                                                   core::sequencing::PhraseEnvelopeEditKey::decay,
                                                   core::sequencing::PhraseEnvelopeEditDirection::down,
                                                   beats (1),
                                                   core::sequencing::ExpressionLanePolarity::unipolar,
                                                   activeSegment));
    REQUIRE (decayDownEnvelope->decayStage().has_value());
    CHECK (decayDownEnvelope->decayStage()->endLevel == Catch::Approx (0.45));

    auto releaseRightEnvelope = core::sequencing::createPhraseEnvelopeForSelection (
        core::sequencing::ExpressionClipId { "env-release-right" },
        clip,
        { "note-1", "note-2" },
        beats (1),
        0.4,
        core::sequencing::ExpressionLanePolarity::unipolar);
    REQUIRE (releaseRightEnvelope.has_value());

    activeSegment = core::sequencing::PhraseEnvelopeActiveSegment::attack;
    REQUIRE (core::sequencing::editPhraseEnvelope (*releaseRightEnvelope,
                                                   core::sequencing::PhraseEnvelopeEditKey::release,
                                                   core::sequencing::PhraseEnvelopeEditDirection::right,
                                                   beats (1),
                                                   core::sequencing::ExpressionLanePolarity::unipolar,
                                                   activeSegment));
    REQUIRE (releaseRightEnvelope->releaseStage().has_value());
    CHECK (activeSegment == core::sequencing::PhraseEnvelopeActiveSegment::release);
    CHECK (releaseRightEnvelope->releaseStage()->duration == beats (1));
    CHECK (releaseRightEnvelope->releaseStage()->startLevel == Catch::Approx (0.4));
    CHECK (releaseRightEnvelope->releaseStage()->endLevel == Catch::Approx (0.0));
}

TEST_CASE ("Phrase envelope edits can be committed and undone through expression state commands", "[expression][phrase][commands]")
{
    auto clip = clipWithNotes();
    auto expression = clip.expressionState();
    auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);

    auto envelope = core::sequencing::createPhraseEnvelopeForSelection (
        core::sequencing::ExpressionClipId { "env-1" },
        clip,
        { "note-1" },
        beats (1),
        0.25,
        lane->polarity());
    REQUIRE (envelope.has_value());
    lane->addPhraseEnvelopeClip (*envelope);
    clip.setExpressionState (expression);

    auto project = projectWithClip (clip);
    core::commands::ProjectCommandContext context { project };
    core::commands::CommandStack stack { context };

    auto editedExpression = expression;
    auto* editedLane = editedExpression.findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (editedLane != nullptr);
    auto editedEnvelope = editedLane->removePhraseEnvelopeClip (core::sequencing::ExpressionClipId { "env-1" });
    auto activeSegment = core::sequencing::PhraseEnvelopeActiveSegment::attack;
    REQUIRE (core::sequencing::editPhraseEnvelope (editedEnvelope,
                                                   core::sequencing::PhraseEnvelopeEditKey::attack,
                                                   core::sequencing::PhraseEnvelopeEditDirection::left,
                                                   beats (1),
                                                   editedLane->polarity(),
                                                   activeSegment));
    editedLane->addPhraseEnvelopeClip (editedEnvelope);

    REQUIRE (stack.execute (std::make_unique<core::commands::SetClipExpressionStateCommand> (
        "track-1",
        "clip-1",
        editedExpression)).succeeded());

    auto* storedTrack = project.findTrackById ("track-1");
    REQUIRE (storedTrack != nullptr);
    auto* storedClip = storedTrack->findClipById ("clip-1");
    REQUIRE (storedClip != nullptr);
    auto* storedLane = storedClip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (storedLane != nullptr);
    REQUIRE (storedLane->phraseEnvelopeClips().size() == 1);
    CHECK (storedLane->phraseEnvelopeClips().front().attackStage().duration == core::time::TickDuration {});

    REQUIRE (stack.undo().succeeded());
    storedLane = storedClip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (storedLane != nullptr);
    REQUIRE (storedLane->phraseEnvelopeClips().size() == 1);
    CHECK (storedLane->phraseEnvelopeClips().front().attackStage().duration == beats (1));
}

TEST_CASE ("Expression lane presets copy routing without note-bound phrase objects", "[expression][phrase][preset]")
{
    auto lane = core::sequencing::ExpressionLane {
        core::sequencing::ExpressionLaneId { "expr-energy" },
        "Energy",
        core::sequencing::ExpressionLanePolarity::bipolar
    };
    lane.setEnabled (false);
    lane.addRoute (core::sequencing::ExpressionRoute {
        core::sequencing::ExpressionRouteId { "route-1" },
        core::sequencing::ExpressionDestination::trackPan ("track-1"),
        -0.5,
        0.8
    });
    lane.addRoute (core::sequencing::ExpressionRoute {
        core::sequencing::ExpressionRouteId { "route-2" },
        core::sequencing::ExpressionDestination::midiCc ("track-1", 74),
        0.2,
        0.9
    });

    auto envelope = core::sequencing::PhraseEnvelopeClip {
        core::sequencing::ExpressionClipId { "env-1" },
        { "note-1" },
        core::sequencing::Region { beat (0), beat (2) },
        0.3,
        core::sequencing::EnvelopeStage { core::sequencing::EnvelopeStageType::attack, beats (1), 0.0, 0.3 }
    };
    lane.addPhraseEnvelopeClip (envelope);

    auto cyclic = core::sequencing::CyclicExpressionClip {
        core::sequencing::ExpressionClipId { "cyclic-1" },
        { "note-1" },
        core::sequencing::Region { beat (0), beat (2) }
    };
    cyclic.setMaxAmplitude (0.7);
    lane.addCyclicClip (cyclic);

    auto slur = core::sequencing::PitchSlur {
        core::sequencing::ExpressionClipId { "slur-1" },
        "note-1",
        "note-2"
    };
    lane.addPitchSlur (slur);

    auto vibrato = core::sequencing::VibratoExpression {
        core::sequencing::ExpressionClipId { "vibrato-1" },
        { "note-1" },
        core::sequencing::Region { beat (0), beat (2) }
    };
    lane.addVibratoExpression (vibrato);

    const auto preset = core::sequencing::createExpressionLanePreset (lane);
    CHECK (preset.name == "Energy");
    CHECK (preset.polarity == core::sequencing::ExpressionLanePolarity::bipolar);
    CHECK_FALSE (preset.enabled);
    REQUIRE (preset.routes.size() == 2);
    CHECK (preset.routes[0].destination() == core::sequencing::ExpressionDestination::trackPan ("track-1"));

    auto duplicated = core::sequencing::createExpressionLaneFromPreset (
        core::sequencing::ExpressionLaneId { "expr-energy-copy" },
        preset);
    CHECK (duplicated.id() == core::sequencing::ExpressionLaneId { "expr-energy-copy" });
    CHECK (duplicated.name() == "Energy");
    CHECK (duplicated.polarity() == core::sequencing::ExpressionLanePolarity::bipolar);
    CHECK_FALSE (duplicated.enabled());
    CHECK (duplicated.routes().size() == 2);
    CHECK (duplicated.phraseEnvelopeClips().empty());
    CHECK (duplicated.cyclicClips().empty());
    CHECK (duplicated.pitchSlurs().empty());
    CHECK (duplicated.vibratoExpressions().empty());
}

TEST_CASE ("Expression settings copy preserves target phrase bindings and timing constraints", "[expression][phrase][copy]")
{
    auto source = core::sequencing::PhraseEnvelopeClip {
        core::sequencing::ExpressionClipId { "env-source" },
        { "note-source" },
        core::sequencing::Region { beat (0), beat (4) },
        0.4,
        core::sequencing::EnvelopeStage { core::sequencing::EnvelopeStageType::attack, beats (1), 0.0, 0.4 }
    };
    source.setDecayStage (core::sequencing::EnvelopeStage {
        core::sequencing::EnvelopeStageType::decay,
        beats (1),
        0.8,
        0.5
    });
    source.setReleaseStage (core::sequencing::EnvelopeStage {
        core::sequencing::EnvelopeStageType::release,
        beats (1),
        0.5,
        0.1
    });
    source.setPeakLevel (0.8, core::sequencing::ExpressionLanePolarity::unipolar);
    source.setSustainLevel (0.5, core::sequencing::ExpressionLanePolarity::unipolar);
    source.setTailExtension (beats (1));

    auto target = core::sequencing::PhraseEnvelopeClip {
        core::sequencing::ExpressionClipId { "env-target" },
        { "note-target-a", "note-target-b" },
        core::sequencing::Region { beat (8), beat (12) },
        0.2,
        core::sequencing::EnvelopeStage { core::sequencing::EnvelopeStageType::attack, beats (1), 0.0, 0.2 }
    };

    REQUIRE (core::sequencing::copyPhraseEnvelopeSettings (
        source,
        target,
        core::sequencing::ExpressionLanePolarity::unipolar));
    CHECK (target.id() == core::sequencing::ExpressionClipId { "env-target" });
    CHECK (target.sourceNoteIds() == std::vector<std::string> { "note-target-a", "note-target-b" });
    CHECK (target.phraseRegion().start() == beat (8));
    CHECK (target.phraseRegion().end() == beat (12));
    CHECK (target.storedLevel() == Catch::Approx (0.4));
    CHECK (target.attackStage().duration == beats (1));
    REQUIRE (target.decayStage().has_value());
    CHECK (target.decayStage()->endLevel == Catch::Approx (0.5));
    REQUIRE (target.releaseStage().has_value());
    CHECK (target.releaseStage()->endLevel == Catch::Approx (0.1));
    REQUIRE (target.peakLevel().has_value());
    CHECK (*target.peakLevel() == Catch::Approx (0.8));

    auto shortTarget = core::sequencing::PhraseEnvelopeClip {
        core::sequencing::ExpressionClipId { "env-short" },
        { "note-short" },
        core::sequencing::Region { beat (16), beat (18) },
        0.1,
        core::sequencing::EnvelopeStage { core::sequencing::EnvelopeStageType::attack, beats (1), 0.0, 0.1 }
    };
    CHECK_FALSE (core::sequencing::copyPhraseEnvelopeSettings (
        source,
        shortTarget,
        core::sequencing::ExpressionLanePolarity::unipolar));
    CHECK (shortTarget.id() == core::sequencing::ExpressionClipId { "env-short" });
    CHECK (shortTarget.sourceNoteIds() == std::vector<std::string> { "note-short" });
    CHECK_FALSE (shortTarget.decayStage().has_value());
    CHECK_FALSE (shortTarget.releaseStage().has_value());
}

TEST_CASE ("Pitch expression settings copy preserves target notes", "[expression][pitch][copy]")
{
    auto sourceSlur = core::sequencing::PitchSlur {
        core::sequencing::ExpressionClipId { "slur-source" },
        "source-a",
        "source-b"
    };
    sourceSlur.setSlurTime (beats (2));
    sourceSlur.setCurveShape (core::sequencing::ExpressionCurveShape::exponential);
    sourceSlur.setLegatoNoRetrigger (false);
    sourceSlur.setHasVoiceOverride (true);

    auto targetSlur = core::sequencing::PitchSlur {
        core::sequencing::ExpressionClipId { "slur-target" },
        "target-a",
        "target-b"
    };
    core::sequencing::copyPitchSlurSettings (sourceSlur, targetSlur);
    CHECK (targetSlur.id() == core::sequencing::ExpressionClipId { "slur-target" });
    CHECK (targetSlur.sourceNoteId() == "target-a");
    CHECK (targetSlur.destinationNoteId() == "target-b");
    CHECK (targetSlur.slurTime() == beats (2));
    CHECK (targetSlur.curveShape() == core::sequencing::ExpressionCurveShape::exponential);
    CHECK_FALSE (targetSlur.legatoNoRetrigger());
    CHECK (targetSlur.hasVoiceOverride());

    auto sourceVibrato = core::sequencing::VibratoExpression {
        core::sequencing::ExpressionClipId { "vibrato-source" },
        { "source-a", "source-b" },
        core::sequencing::Region { beat (0), beat (4) }
    };
    sourceVibrato.setAttackTime (beats (1));
    sourceVibrato.setReleaseTime (beats (1));
    sourceVibrato.setAmplitudeSemitones (0.4);
    sourceVibrato.setFrequencyDivisionId ("eighth");
    sourceVibrato.setWaveShape (core::sequencing::CyclicWaveShape::triangle);
    sourceVibrato.setPhase (0.25);
    sourceVibrato.setVoiceOverrides ({ core::sequencing::VibratoVoiceOverride { "source-a", 0.9, beats (1), {}, "quarter" } });

    auto targetVibrato = core::sequencing::VibratoExpression {
        core::sequencing::ExpressionClipId { "vibrato-target" },
        { "target-a" },
        core::sequencing::Region { beat (8), beat (11) }
    };
    REQUIRE (core::sequencing::copyVibratoSettings (sourceVibrato, targetVibrato));
    CHECK (targetVibrato.id() == core::sequencing::ExpressionClipId { "vibrato-target" });
    CHECK (targetVibrato.sourceNoteIds() == std::vector<std::string> { "target-a" });
    CHECK (targetVibrato.phraseRegion().start() == beat (8));
    CHECK (targetVibrato.attackTime() == beats (1));
    CHECK (targetVibrato.releaseTime() == beats (1));
    CHECK (targetVibrato.amplitudeSemitones() == Catch::Approx (0.4));
    CHECK (targetVibrato.frequencyDivisionId() == "eighth");
    CHECK (targetVibrato.waveShape() == core::sequencing::CyclicWaveShape::triangle);
    CHECK (targetVibrato.phase() == Catch::Approx (0.25));
    CHECK (targetVibrato.voiceOverrides().empty());

    auto shortVibrato = core::sequencing::VibratoExpression {
        core::sequencing::ExpressionClipId { "vibrato-short" },
        { "short-note" },
        core::sequencing::Region { beat (12), beat (13) }
    };
    CHECK_FALSE (core::sequencing::copyVibratoSettings (sourceVibrato, shortVibrato));
    CHECK (shortVibrato.id() == core::sequencing::ExpressionClipId { "vibrato-short" });
    CHECK (shortVibrato.sourceNoteIds() == std::vector<std::string> { "short-note" });
    CHECK (shortVibrato.attackTime() == core::time::TickDuration {});
    CHECK (shortVibrato.releaseTime() == core::time::TickDuration {});
}
