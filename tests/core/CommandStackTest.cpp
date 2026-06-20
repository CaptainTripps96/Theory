#include "core/commands/AddClipCommand.h"
#include "core/commands/AddChordRegionCommand.h"
#include "core/commands/AddCustomScaleCommand.h"
#include "core/commands/AddNoteCommand.h"
#include "core/commands/ArpeggiateSelectionCommand.h"
#include "core/commands/AddScaleModeRegionCommand.h"
#include "core/commands/AddTempoNodeCommand.h"
#include "core/commands/AddTimeSignatureMarkerCommand.h"
#include "core/commands/ChordStackingCommand.h"
#include "core/commands/CommandStack.h"
#include "core/commands/DeleteChordRegionCommand.h"
#include "core/commands/DeleteClipCommand.h"
#include "core/commands/DeleteKeyCenterRegionCommand.h"
#include "core/commands/DeleteNoteCommand.h"
#include "core/commands/DeleteScaleModeRegionCommand.h"
#include "core/commands/AssignTrackInstrumentCommand.h"
#include "core/commands/ExpressionCommands.h"
#include "core/commands/GlobalizeChordProgressionCommand.h"
#include "core/commands/MoveClipCommand.h"
#include "core/commands/ReplaceChordRegionCommand.h"
#include "core/commands/MoveNoteCommand.h"
#include "core/commands/ReplaceKeyCenterRegionCommand.h"
#include "core/commands/ReplaceScaleModeRegionCommand.h"
#include "core/commands/ResizeClipCommand.h"
#include "core/commands/ResizeNoteCommand.h"
#include "core/commands/SeparateNotesToClipCommand.h"
#include "core/commands/SetProjectRhythmSettingsCommand.h"
#include "core/commands/TransposeClipCommand.h"
#include "core/music_theory/CustomScaleBuilder.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/sequencing/Arpeggiator.h"
#include "core/sequencing/NoteHarmonicInterpretation.h"
#include "core/sequencing/PitchExpressionEvaluation.h"
#include "core/sequencing/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core;
using namespace tsq::core::commands;
using namespace tsq::core::music_theory;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickDuration beats (int count)
{
    return TickDuration::fromTicks (static_cast<std::int64_t> (count) * ticksPerQuarterNote);
}

TickPosition beat (int zeroBasedBeat)
{
    return TickPosition::fromTicks (static_cast<std::int64_t> (zeroBasedBeat) * ticksPerQuarterNote);
}

Region bars (int firstBar, int onePastLastBar)
{
    return Region { beat ((firstBar - 1) * 4), beat ((onePastLastBar - 1) * 4) };
}

MidiClip clip (std::string id, int startBeat, int lengthBeats)
{
    return MidiClip { std::move (id), "Clip", beat (startBeat), beats (lengthBeats) };
}

AudioSourceReference audioSource (std::string path = "audio/test.wav")
{
    return AudioSourceReference { "audio-source-1", std::move (path), "Test Audio", false };
}

AudioClip audioClip (std::string id, int startBeat, int lengthBeats)
{
    return AudioClip { std::move (id), "Audio Clip", audioSource(), beat (startBeat), beats (lengthBeats) };
}

MidiNote note (std::string id, int startBeat = 0)
{
    return MidiNote { std::move (id), MidiPitch::middleC(), beat (startBeat), beats (1), 100, NoteName::c() };
}

MidiNote pitchedNote (std::string id, int midiPitch, NoteName spelling, int startBeat = 0)
{
    return MidiNote {
        std::move (id),
        MidiPitch::fromValue (midiPitch),
        beat (startBeat),
        beats (1),
        100,
        spelling
    };
}

Project projectWithTrackAndClip()
{
    Project project { "project-1", "Song" };
    Track track { "track-1", "Piano" };
    track.addClip (clip ("clip-1", 0, 4));
    project.addTrack (std::move (track));
    return project;
}

MidiClip* findClip (Project& project, const std::string& clipId)
{
    auto* track = project.findTrackById ("track-1");
    if (track == nullptr)
        return nullptr;

    return track->findClipById (clipId);
}

AudioClip* findAudioClip (Project& project, const std::string& trackId, const std::string& clipId)
{
    auto* track = project.findTrackById (trackId);
    if (track == nullptr)
        return nullptr;

    return track->findAudioClipById (clipId);
}

std::vector<int> notePitches (const MidiClip& clip)
{
    std::vector<int> result;
    for (const auto& clipNote : clip.notes())
        result.push_back (clipNote.pitch().value());

    return result;
}

std::vector<std::int64_t> noteStartTicks (const MidiClip& clip)
{
    std::vector<std::int64_t> result;
    for (const auto& clipNote : clip.notes())
        result.push_back (clipNote.startInClip().ticks());

    return result;
}

std::vector<std::int64_t> noteDurationTicks (const MidiClip& clip)
{
    std::vector<std::int64_t> result;
    for (const auto& clipNote : clip.notes())
        result.push_back (clipNote.duration().ticks());

    return result;
}

std::vector<int> sortedNotePitches (const MidiClip& clip)
{
    auto result = notePitches (clip);
    std::sort (result.begin(), result.end());
    return result;
}

TrackInstrumentReference testInstrument()
{
    return TrackInstrumentReference {
        "Test Piano",
        "TheorySequencer",
        "VST3",
        "/Library/Audio/Plug-Ins/VST3/Test Piano.vst3",
        "test-piano-vst3",
        1234,
        0,
        true,
        0,
        2,
        {}
    };
}

void addCMajorSeventhBlock (MidiClip& clip)
{
    clip.addNote (MidiNote { "note-1", MidiPitch::fromValue (60), beat (0), beats (4), 100, NoteName::c() });
    clip.addNote (MidiNote { "note-2", MidiPitch::fromValue (64), beat (0), beats (4), 100, NoteName::e() });
    clip.addNote (MidiNote { "note-3", MidiPitch::fromValue (67), beat (0), beats (4), 100, NoteName::g() });
    clip.addNote (MidiNote { "note-4", MidiPitch::fromValue (71), beat (0), beats (4), 100, NoteName::b() });
}
}

TEST_CASE ("Add note command supports undo and redo")
{
    auto project = projectWithTrackAndClip();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    const auto executeResult = stack.execute (std::make_unique<AddNoteCommand> ("track-1", "clip-1", note ("note-1")));

    REQUIRE (executeResult.succeeded());
    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (findClip (project, "clip-1")->notes().size() == 1);

    REQUIRE (stack.undo().succeeded());
    CHECK (findClip (project, "clip-1")->notes().empty());

    REQUIRE (stack.redo().succeeded());
    CHECK (findClip (project, "clip-1")->notes().size() == 1);
}

TEST_CASE ("Move note command supports undo and redo")
{
    auto project = projectWithTrackAndClip();
    findClip (project, "clip-1")->addNote (note ("note-1"));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<MoveNoteCommand> ("track-1", "clip-1", "note-1", beat (2))).succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (2));

    REQUIRE (stack.undo().succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (0));

    REQUIRE (stack.redo().succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (2));
}

TEST_CASE ("Move note command can move pitch with undo and redo")
{
    auto project = projectWithTrackAndClip();
    findClip (project, "clip-1")->addNote (note ("note-1"));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<MoveNoteCommand> ("track-1", "clip-1", "note-1", beat (1), MidiPitch::fromValue (67))).succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (1));
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->pitch() == MidiPitch::fromValue (67));

    REQUIRE (stack.undo().succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (0));
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->pitch() == MidiPitch::middleC());

    REQUIRE (stack.redo().succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (1));
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->pitch() == MidiPitch::fromValue (67));
}

TEST_CASE ("Resize note command can resize either edge with undo and redo")
{
    auto project = projectWithTrackAndClip();
    findClip (project, "clip-1")->addNote (note ("note-1", 1));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ResizeNoteCommand> ("track-1", "clip-1", "note-1", beat (0), beats (2))).succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (0));
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->duration() == beats (2));

    REQUIRE (stack.undo().succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (1));
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->duration() == beats (1));

    REQUIRE (stack.redo().succeeded());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->startInClip() == beat (0));
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->duration() == beats (2));
}

TEST_CASE ("Delete note command supports undo and redo")
{
    auto project = projectWithTrackAndClip();
    findClip (project, "clip-1")->addNote (note ("note-1"));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<DeleteNoteCommand> ("track-1", "clip-1", "note-1")).succeeded());
    CHECK (findClip (project, "clip-1")->notes().empty());

    REQUIRE (stack.undo().succeeded());
    REQUIRE (findClip (project, "clip-1")->findNoteById ("note-1") != nullptr);
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->pitch() == MidiPitch::middleC());

    REQUIRE (stack.redo().succeeded());
    CHECK (findClip (project, "clip-1")->notes().empty());
}

TEST_CASE ("Delete note command removes and restores expression references")
{
    auto project = projectWithTrackAndClip();
    auto* clip = findClip (project, "clip-1");
    REQUIRE (clip != nullptr);
    clip->addNote (note ("note-1", 0));
    clip->addNote (note ("note-2", 1));

    auto expression = clip->expressionState();
    auto* volumeLane = expression.findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    volumeLane->addPhraseEnvelopeClip (PhraseEnvelopeClip {
        ExpressionClipId { "env-1" },
        { "note-1" },
        Region { beat (0), beat (1) },
        0.25,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    });

    auto* pitchLane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    pitchLane->addPitchSlur (PitchSlur { ExpressionClipId { "slur-1" }, "note-1", "note-2" });

    auto removedVibrato = VibratoExpression {
        ExpressionClipId { "vibrato-removed" },
        { "note-1" },
        Region { beat (0), beat (2) }
    };
    removedVibrato.setAmplitudeSemitones (0.5);
    pitchLane->addVibratoExpression (removedVibrato);

    auto repairedVibrato = VibratoExpression {
        ExpressionClipId { "vibrato-repaired" },
        { "note-2" },
        Region { beat (0), beat (2) }
    };
    repairedVibrato.setAmplitudeSemitones (0.5);
    repairedVibrato.setVoiceOverrides ({
        VibratoVoiceOverride { "note-1", 0.25, beats (0), beats (0), "sixteenth", CyclicWaveShape::sine, 0.0 }
    });
    pitchLane->addVibratoExpression (repairedVibrato);
    clip->setExpressionState (expression);

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<DeleteNoteCommand> ("track-1", "clip-1", "note-1")).succeeded());
    clip = findClip (project, "clip-1");
    REQUIRE (clip != nullptr);
    CHECK (clip->findNoteById ("note-1") == nullptr);

    volumeLane = clip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    CHECK (volumeLane->phraseEnvelopeClips().empty());

    pitchLane = clip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    CHECK (pitchLane->pitchSlurs().empty());
    REQUIRE (pitchLane->vibratoExpressions().size() == 1);
    CHECK (pitchLane->vibratoExpressions().front().id() == ExpressionClipId { "vibrato-repaired" });
    CHECK (pitchLane->vibratoExpressions().front().voiceOverrides().empty());
    CHECK_NOTHROW (evaluatePitchVoiceTrajectoryAt (*clip, *pitchLane, "note-2", beat (1)));

    REQUIRE (stack.undo().succeeded());
    clip = findClip (project, "clip-1");
    REQUIRE (clip != nullptr);
    REQUIRE (clip->findNoteById ("note-1") != nullptr);

    volumeLane = clip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    REQUIRE (volumeLane->phraseEnvelopeClips().size() == 1);

    pitchLane = clip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    REQUIRE (pitchLane->pitchSlurs().size() == 1);
    REQUIRE (pitchLane->vibratoExpressions().size() == 2);
    const auto* restoredRepairedVibrato = pitchLane->findVibratoExpression (ExpressionClipId { "vibrato-repaired" });
    REQUIRE (restoredRepairedVibrato != nullptr);
    REQUIRE (restoredRepairedVibrato->voiceOverrides().size() == 1);

    REQUIRE (stack.redo().succeeded());
    clip = findClip (project, "clip-1");
    REQUIRE (clip != nullptr);
    pitchLane = clip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    CHECK (pitchLane->pitchSlurs().empty());
    REQUIRE (pitchLane->vibratoExpressions().size() == 1);
    CHECK (pitchLane->vibratoExpressions().front().voiceOverrides().empty());
}

TEST_CASE ("Add clip command supports undo and redo")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddClipCommand> ("track-1", clip ("clip-1", 0, 4))).succeeded());
    CHECK (findClip (project, "clip-1") != nullptr);

    REQUIRE (stack.undo().succeeded());
    CHECK (findClip (project, "clip-1") == nullptr);

    REQUIRE (stack.redo().succeeded());
    CHECK (findClip (project, "clip-1") != nullptr);
}

TEST_CASE ("Audio clip commands support undo and redo")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "audio-1", "Audio 1", TrackType::audio });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddClipCommand> ("audio-1", audioClip ("audio-clip-1", 0, 4))).succeeded());
    REQUIRE (findAudioClip (project, "audio-1", "audio-clip-1") != nullptr);

    REQUIRE (stack.execute (std::make_unique<MoveClipCommand> ("audio-1", "audio-clip-1", beat (4))).succeeded());
    REQUIRE (findAudioClip (project, "audio-1", "audio-clip-1") != nullptr);
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1")->startInProject() == beat (4));

    REQUIRE (stack.execute (std::make_unique<ResizeClipCommand> ("audio-1", "audio-clip-1", beats (8))).succeeded());
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1")->length() == beats (8));

    REQUIRE (stack.execute (std::make_unique<DeleteClipCommand> ("audio-1", "audio-clip-1")).succeeded());
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1") == nullptr);

    REQUIRE (stack.undo().succeeded());
    REQUIRE (findAudioClip (project, "audio-1", "audio-clip-1") != nullptr);
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1")->length() == beats (8));

    REQUIRE (stack.undo().succeeded());
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1")->length() == beats (4));

    REQUIRE (stack.undo().succeeded());
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1")->startInProject() == beat (0));

    REQUIRE (stack.undo().succeeded());
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1") == nullptr);

    REQUIRE (stack.redo().succeeded());
    CHECK (findAudioClip (project, "audio-1", "audio-clip-1") != nullptr);
}

TEST_CASE ("Project rhythm settings command supports undo and redo")
{
    Project project { "project-1", "Song" };
    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto settings = project.rhythmSettings();
    settings.setCurrentGridDivisionId ("eighthTriplet");
    settings.setQuintupletsEnabled (true);

    REQUIRE (stack.execute (std::make_unique<SetProjectRhythmSettingsCommand> (settings)).succeeded());
    CHECK (project.rhythmSettings().currentGridDivisionId() == "eighthTriplet");
    CHECK (project.rhythmSettings().quintupletsEnabled());

    REQUIRE (stack.undo().succeeded());
    CHECK (project.rhythmSettings().currentGridDivisionId() == "sixteenth");
    CHECK_FALSE (project.rhythmSettings().quintupletsEnabled());

    REQUIRE (stack.redo().succeeded());
    CHECK (project.rhythmSettings().currentGridDivisionId() == "eighthTriplet");
    CHECK (project.rhythmSettings().quintupletsEnabled());
}

TEST_CASE ("Add clip command snapshots harmonic metadata across the clip duration")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });
    project.musicalStructure().addScaleModeRegion (ScaleModeRegion { bars (5, 9), "Natural Minor" });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddClipCommand> ("track-1", clip ("clip-1", 0, 32))).succeeded());

    const auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    const auto& regions = storedClip->harmonicMetadata().regions();
    REQUIRE (regions.size() == 2);
    CHECK (regions[0].region.start() == beat (0));
    CHECK (regions[0].region.end() == beat (16));
    CHECK (regions[0].originalContext.keyCenter() == PitchClass::c());
    CHECK (regions[0].originalContext.scaleDefinitionName() == "Major");
    CHECK (regions[1].region.start() == beat (16));
    CHECK (regions[1].region.end() == beat (32));
    CHECK (regions[1].originalContext.keyCenter() == PitchClass::c());
    CHECK (regions[1].originalContext.scaleDefinitionName() == "Natural Minor");
}

TEST_CASE ("Move clip command fails when it would overlap another same-track clip")
{
    Project project { "project-1", "Song" };
    Track track { "track-1", "Piano" };
    track.addClip (clip ("clip-1", 0, 4));
    track.addClip (clip ("clip-2", 4, 4));
    project.addTrack (std::move (track));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    const auto result = stack.execute (std::make_unique<MoveClipCommand> ("track-1", "clip-2", beat (2)));

    CHECK (result.failed());
    CHECK_FALSE (result.error().empty());
    CHECK (findClip (project, "clip-2")->startInProject() == beat (4));
    CHECK_FALSE (stack.canUndo());
}

TEST_CASE ("Move MIDI clip to track command moves a clip between MIDI tracks")
{
    Project project { "project-1", "Song" };
    Track sourceTrack { "track-1", "Piano" };
    sourceTrack.addClip (clip ("clip-1", 0, 4));
    project.addTrack (std::move (sourceTrack));
    project.addTrack (Track { "track-2", "Strings", TrackType::midi });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<MoveMidiClipToTrackCommand> ("track-1", "track-2", "clip-1", beat (8))).succeeded());

    REQUIRE (project.findTrackById ("track-1") != nullptr);
    REQUIRE (project.findTrackById ("track-2") != nullptr);
    CHECK (project.findTrackById ("track-1")->findClipById ("clip-1") == nullptr);
    REQUIRE (project.findTrackById ("track-2")->findClipById ("clip-1") != nullptr);
    CHECK (project.findTrackById ("track-2")->findClipById ("clip-1")->startInProject() == beat (8));

    REQUIRE (stack.undo().succeeded());
    REQUIRE (project.findTrackById ("track-1")->findClipById ("clip-1") != nullptr);
    CHECK (project.findTrackById ("track-1")->findClipById ("clip-1")->startInProject() == beat (0));
    CHECK (project.findTrackById ("track-2")->findClipById ("clip-1") == nullptr);
}

TEST_CASE ("Separate notes to clip command places the new clip to the right without overlap")
{
    Project project { "project-1", "Song" };
    Track track { "track-1", "Piano" };
    auto sourceClip = clip ("clip-1", 0, 4);
    sourceClip.addNote (note ("note-1", 0));
    sourceClip.addNote (note ("note-2", 1));
    track.addClip (std::move (sourceClip));
    track.addClip (clip ("clip-occupied", 4, 4));
    project.addTrack (std::move (track));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<SeparateNotesToClipCommand> (
        "track-1",
        "clip-1",
        "clip-2",
        std::vector<std::string> { "note-2" })).succeeded());

    auto* storedTrack = project.findTrackById ("track-1");
    REQUIRE (storedTrack != nullptr);
    auto* original = storedTrack->findClipById ("clip-1");
    auto* separated = storedTrack->findClipById ("clip-2");
    REQUIRE (original != nullptr);
    REQUIRE (separated != nullptr);

    CHECK (original->length() == beats (4));
    CHECK (original->findNoteById ("note-1") != nullptr);
    CHECK (original->findNoteById ("note-2") == nullptr);
    CHECK (separated->startInProject() == beat (8));
    CHECK (separated->length() == beats (4));
    REQUIRE (separated->findNoteById ("note-2") != nullptr);
    CHECK (separated->findNoteById ("note-2")->startInClip() == beat (1));

    REQUIRE (stack.undo().succeeded());
    original = storedTrack->findClipById ("clip-1");
    REQUIRE (original != nullptr);
    CHECK (original->findNoteById ("note-1") != nullptr);
    CHECK (original->findNoteById ("note-2") != nullptr);
    CHECK (storedTrack->findClipById ("clip-2") == nullptr);
}

TEST_CASE ("Move audio clip command fails when it would overlap another same-track audio clip")
{
    Project project { "project-1", "Song" };
    Track track { "audio-1", "Audio 1", TrackType::audio };
    track.addAudioClip (audioClip ("audio-clip-1", 0, 4));
    track.addAudioClip (audioClip ("audio-clip-2", 4, 4));
    project.addTrack (std::move (track));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    const auto result = stack.execute (std::make_unique<MoveClipCommand> ("audio-1", "audio-clip-2", beat (2)));

    CHECK (result.failed());
    CHECK_FALSE (result.error().empty());
    CHECK (findAudioClip (project, "audio-1", "audio-clip-2")->startInProject() == beat (4));
    CHECK_FALSE (stack.canUndo());
}

TEST_CASE ("Command stack reports invalid null command")
{
    auto project = projectWithTrackAndClip();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto result = stack.execute (nullptr);

    CHECK (result.failed());
    CHECK (result.error() == "Cannot execute a null command");
    CHECK_FALSE (stack.canUndo());
}

TEST_CASE ("Expression lane and route commands support undo redo and dirty category")
{
    auto project = projectWithTrackAndClip();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    std::vector<PlaybackSyncCategory> categories;
    stack.setChangeCallback ([&] (PlaybackSyncCategory category) {
        categories.push_back (category);
    });

    REQUIRE (stack.execute (std::make_unique<CreateExpressionLaneCommand> (
        "track-1",
        "clip-1",
        ExpressionLane { ExpressionLaneId { "expr-motion" }, "Motion", ExpressionLanePolarity::unipolar })).succeeded());

    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    auto* lane = storedClip->expressionState().findLane (ExpressionLaneId { "expr-motion" });
    REQUIRE (lane != nullptr);
    CHECK (lane->name() == "Motion");
    REQUIRE_FALSE (categories.empty());
    CHECK (categories.back() == PlaybackSyncCategory::expression);

    REQUIRE (stack.execute (std::make_unique<RenameExpressionLaneCommand> (
        "track-1",
        "clip-1",
        ExpressionLaneId { "expr-motion" },
        "Pressure")).succeeded());
    CHECK (storedClip->expressionState().findLane (ExpressionLaneId { "expr-motion" })->name() == "Pressure");

    REQUIRE (stack.execute (std::make_unique<SetExpressionLanePolarityCommand> (
        "track-1",
        "clip-1",
        ExpressionLaneId { "expr-motion" },
        ExpressionLanePolarity::bipolar)).succeeded());
    CHECK (storedClip->expressionState().findLane (ExpressionLaneId { "expr-motion" })->polarity() == ExpressionLanePolarity::bipolar);

    REQUIRE (stack.execute (std::make_unique<AddExpressionRouteCommand> (
        "track-1",
        "clip-1",
        ExpressionLaneId { "expr-motion" },
        ExpressionRoute {
            ExpressionRouteId { "route-1" },
            ExpressionDestination::midiCc ("track-1", 74),
            -1.0,
            1.0
        })).succeeded());
    REQUIRE (storedClip->expressionState().findLane (ExpressionLaneId { "expr-motion" })->routes().size() == 1);

    REQUIRE (stack.undo().succeeded());
    CHECK (storedClip->expressionState().findLane (ExpressionLaneId { "expr-motion" })->routes().empty());

    REQUIRE (stack.undo().succeeded());
    CHECK (storedClip->expressionState().findLane (ExpressionLaneId { "expr-motion" })->polarity() == ExpressionLanePolarity::unipolar);

    REQUIRE (stack.redo().succeeded());
    CHECK (storedClip->expressionState().findLane (ExpressionLaneId { "expr-motion" })->polarity() == ExpressionLanePolarity::bipolar);
}

TEST_CASE ("Expression object commands add remove and restore clip expression data")
{
    auto project = projectWithTrackAndClip();
    ProjectCommandContext context { project };
    CommandStack stack { context };
    CHECK_FALSE (stack.nextUndoName().has_value());
    CHECK_FALSE (stack.nextRedoName().has_value());

    auto envelope = PhraseEnvelopeClip {
        ExpressionClipId { "env-1" },
        { "note-1" },
        Region { beat (0), beat (2) },
        0.5,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    };

    REQUIRE (stack.execute (std::make_unique<AddPhraseEnvelopeClipCommand> (
        "track-1",
        "clip-1",
        ExpressionState::defaultVolumeLaneId(),
        envelope)).succeeded());
    REQUIRE (stack.nextUndoName().has_value());
    CHECK (*stack.nextUndoName() == "Add Phrase Envelope");
    CHECK_FALSE (stack.nextRedoName().has_value());

    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    auto* volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    REQUIRE (volumeLane->phraseEnvelopeClips().size() == 1);

    REQUIRE (stack.execute (std::make_unique<AddPitchSlurCommand> (
        "track-1",
        "clip-1",
        ExpressionState::defaultPitchLaneId(),
        PitchSlur { ExpressionClipId { "slur-1" }, "note-1", "note-2" })).succeeded());
    REQUIRE (stack.nextUndoName().has_value());
    CHECK (*stack.nextUndoName() == "Add Pitch Slur");

    auto* pitchLane = storedClip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    REQUIRE (pitchLane->pitchSlurs().size() == 1);

    auto vibrato = VibratoExpression { ExpressionClipId { "vibrato-1" }, { "note-1" }, Region { beat (0), beat (2) } };
    vibrato.setAmplitudeSemitones (0.25);
    REQUIRE (stack.execute (std::make_unique<AddVibratoExpressionCommand> (
        "track-1",
        "clip-1",
        ExpressionState::defaultPitchLaneId(),
        vibrato)).succeeded());
    REQUIRE (stack.nextUndoName().has_value());
    CHECK (*stack.nextUndoName() == "Add Vibrato Expression");
    pitchLane = storedClip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    REQUIRE (pitchLane->vibratoExpressions().size() == 1);

    auto cyclic = CyclicExpressionClip { ExpressionClipId { "cyclic-1" }, { "note-1" }, Region { beat (0), beat (2) } };
    cyclic.setMaxAmplitude (0.5);
    REQUIRE (stack.execute (std::make_unique<AddCyclicExpressionClipCommand> (
        "track-1",
        "clip-1",
        ExpressionState::defaultVolumeLaneId(),
        cyclic)).succeeded());
    REQUIRE (stack.nextUndoName().has_value());
    CHECK (*stack.nextUndoName() == "Add Cyclic Expression");
    volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    REQUIRE (volumeLane->cyclicClips().size() == 1);

    REQUIRE (stack.execute (std::make_unique<RemoveCyclicExpressionClipCommand> (
        "track-1",
        "clip-1",
        ExpressionState::defaultVolumeLaneId(),
        ExpressionClipId { "cyclic-1" })).succeeded());
    REQUIRE (stack.nextUndoName().has_value());
    CHECK (*stack.nextUndoName() == "Remove Cyclic Expression");
    volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    CHECK (volumeLane->cyclicClips().empty());

    REQUIRE (stack.undo().succeeded());
    REQUIRE (stack.nextRedoName().has_value());
    CHECK (*stack.nextRedoName() == "Remove Cyclic Expression");
    REQUIRE (stack.nextUndoName().has_value());
    CHECK (*stack.nextUndoName() == "Add Cyclic Expression");
    volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    REQUIRE (volumeLane->cyclicClips().size() == 1);
    CHECK (volumeLane->cyclicClips()[0].id() == ExpressionClipId { "cyclic-1" });
}

TEST_CASE ("Expression commands fail without mutating when the lane is missing")
{
    auto project = projectWithTrackAndClip();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    const auto before = findClip (project, "clip-1")->expressionState().lanes().size();
    const auto result = stack.execute (std::make_unique<AddExpressionRouteCommand> (
        "track-1",
        "clip-1",
        ExpressionLaneId { "missing-lane" },
        ExpressionRoute {
            ExpressionRouteId { "route-1" },
            ExpressionDestination::midiCc ("track-1", 74),
            0.0,
            1.0
        }));

    CHECK (result.failed());
    CHECK_FALSE (stack.canUndo());
    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (findClip (project, "clip-1")->expressionState().lanes().size() == before);
    CHECK (findClip (project, "clip-1")->expressionState().findLane (ExpressionLaneId { "missing-lane" }) == nullptr);
}

TEST_CASE ("Assign track instrument command supports undo and redo")
{
    Project project { "project-1", "Song" };
    project.addTrack (Track { "track-1", "Piano" });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AssignTrackInstrumentCommand> ("track-1", testInstrument())).succeeded());
    REQUIRE (project.findTrackById ("track-1") != nullptr);
    REQUIRE (project.findTrackById ("track-1")->instrument().has_value());
    CHECK (project.findTrackById ("track-1")->instrument()->pluginName == "Test Piano");

    REQUIRE (stack.undo().succeeded());
    CHECK_FALSE (project.findTrackById ("track-1")->instrument().has_value());

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.findTrackById ("track-1")->instrument().has_value());
    CHECK (project.findTrackById ("track-1")->instrument()->uniqueIdentifier == "test-piano-vst3");
}

TEST_CASE ("Move clip command does not alter MIDI note pitches")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 1));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 2));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<MoveClipCommand> ("track-1", "clip-1", beat (8))).succeeded());
    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64, 67 });
}

TEST_CASE ("Chromatic transpose clip command maps C major triad to D major")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 1));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 2));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ChromaticTransposeClipCommand> (
        "track-1",
        "clip-1",
        HarmonicContext { PitchClass::d(), "Major" })).succeeded());

    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 62, 66, 69 });
}

TEST_CASE ("Scale-degree transpose clip command maps C major triad to D dorian")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 1));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 2));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ScaleDegreeTransposeClipCommand> (
        "track-1",
        "clip-1",
        HarmonicContext { PitchClass::d(), "Dorian" })).succeeded());

    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 62, 65, 69 });
}

TEST_CASE ("Scale-degree transpose preserves a raised fourth accidental")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 66, NoteName::fSharp(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ScaleDegreeTransposeClipCommand> (
        "track-1",
        "clip-1",
        HarmonicContext { PitchClass::d(), "Major" })).succeeded());

    const auto* transposedClip = findClip (project, "clip-1");
    REQUIRE (transposedClip != nullptr);
    REQUIRE (transposedClip->notes().size() == 1);
    CHECK (transposedClip->notes()[0].pitch() == MidiPitch::fromValue (68));
    REQUIRE (transposedClip->notes()[0].spelling().has_value());
    CHECK (*transposedClip->notes()[0].spelling() == NoteName { LetterName::g, Accidental::sharp() });
}

TEST_CASE ("Scale-degree transpose selected notes uses each note position harmonic context")
{
    auto project = projectWithTrackAndClip();
    project.musicalStructure().addKeyCenterRegion (KeyCenterRegion { bars (1, 3), PitchClass::c() });
    project.musicalStructure().addKeyCenterRegion (KeyCenterRegion { bars (3, 5), PitchClass::b() });
    project.musicalStructure().addScaleModeRegion (ScaleModeRegion { bars (1, 3), "Major" });
    project.musicalStructure().addScaleModeRegion (ScaleModeRegion { bars (3, 5), "Whole Tone" });

    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    track->replaceClip ("clip-1", clip ("clip-1", 0, 16));

    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);

    const auto sourceContext = HarmonicContext { PitchClass::c(), "Major" };
    const auto scaleLibrary = ScaleLibrary::createBuiltInLibrary();
    const auto interpretedNote = [&sourceContext, &scaleLibrary] (
                                     std::string id,
                                     int midiPitch,
                                     NoteName spelling,
                                     int startBeat)
    {
        const auto pitch = MidiPitch::fromValue (midiPitch);
        return MidiNote {
            std::move (id),
            pitch,
            beat (startBeat),
            beats (1),
            100,
            spelling,
            interpretNoteHarmonically (pitch, sourceContext, scaleLibrary)
        };
    };

    storedClip->addNote (interpretedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (interpretedNote ("note-2", 62, NoteName::d(), 1));
    storedClip->addNote (interpretedNote ("note-3", 64, NoteName::e(), 2));
    storedClip->addNote (interpretedNote ("note-4", 60, NoteName::c(), 8));
    storedClip->addNote (interpretedNote ("note-5", 62, NoteName::d(), 9));
    storedClip->addNote (interpretedNote ("note-6", 64, NoteName::e(), 10));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ScaleDegreeTransposeSelectedNotesCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-4", "note-5", "note-6" })).succeeded());

    const auto* transposedClip = findClip (project, "clip-1");
    REQUIRE (transposedClip != nullptr);
    CHECK (notePitches (*transposedClip) == std::vector<int> { 60, 62, 64, 59, 61, 63 });

    const auto* unselectedNote = transposedClip->findNoteById ("note-1");
    REQUIRE (unselectedNote != nullptr);
    REQUIRE (unselectedNote->harmonicInterpretation().has_value());
    CHECK (unselectedNote->harmonicInterpretation()->sourceContext == sourceContext);

    const auto* selectedNote = transposedClip->findNoteById ("note-4");
    REQUIRE (selectedNote != nullptr);
    REQUIRE (selectedNote->harmonicInterpretation().has_value());
    const auto targetContext = HarmonicContext { PitchClass::b(), "Whole Tone" };
    CHECK (selectedNote->harmonicInterpretation()->sourceContext == targetContext);

    REQUIRE (stack.undo().succeeded());
    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 62, 64, 60, 62, 64 });
}

TEST_CASE ("Stack diatonic third command builds a C major seventh")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    std::vector<std::string> selection { "note-1" };
    {
        auto command = std::make_unique<StackDiatonicThirdCommand> ("track-1", "clip-1", selection);
        auto* commandPtr = command.get();
        REQUIRE (stack.execute (std::move (command)).succeeded());
        selection = commandPtr->resultingSelectionNoteIds();
    }
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64 });

    {
        auto command = std::make_unique<StackDiatonicThirdCommand> ("track-1", "clip-1", selection);
        auto* commandPtr = command.get();
        REQUIRE (stack.execute (std::move (command)).succeeded());
        selection = commandPtr->resultingSelectionNoteIds();
    }
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64, 67 });

    {
        auto command = std::make_unique<StackDiatonicThirdCommand> ("track-1", "clip-1", selection);
        auto* commandPtr = command.get();
        REQUIRE (stack.execute (std::move (command)).succeeded());
        selection = commandPtr->resultingSelectionNoteIds();
    }
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64, 67, 71 });
}

TEST_CASE ("Stack diatonic third command maps D in C major to F")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 62, NoteName::d(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<StackDiatonicThirdCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1" })).succeeded());

    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 62, 65 });
}

TEST_CASE ("Stack diatonic third command uses active Mixolydian context")
{
    auto project = projectWithTrackAndClip();
    project.musicalStructure().addScaleModeRegion (ScaleModeRegion { bars (1, 5), "Mixolydian" });
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    std::vector<std::string> selection { "note-1" };
    for (auto count = 0; count < 3; ++count)
    {
        auto command = std::make_unique<StackDiatonicThirdCommand> ("track-1", "clip-1", selection);
        auto* commandPtr = command.get();
        REQUIRE (stack.execute (std::move (command)).succeeded());
        selection = commandPtr->resultingSelectionNoteIds();
    }

    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64, 67, 70 });
    REQUIRE (findClip (project, "clip-1")->notes().back().spelling().has_value());
    CHECK (*findClip (project, "clip-1")->notes().back().spelling() == NoteName::bFlat());
}

TEST_CASE ("Stack diatonic third command does not add an extra octave in non-C keys")
{
    auto project = projectWithTrackAndClip();
    project.musicalStructure().addKeyCenterRegion (KeyCenterRegion { bars (1, 5), PitchClass::g() });
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 64, NoteName::e(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<StackDiatonicThirdCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1" })).succeeded());

    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 64, 67 });
}

TEST_CASE ("Stack diatonic third command supports undo and redo")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<StackDiatonicThirdCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1" })).succeeded());
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64 });

    REQUIRE (stack.undo().succeeded());
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60 });

    REQUIRE (stack.redo().succeeded());
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64 });
}

TEST_CASE ("Remove highest chord tone command removes and restores top note")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto command = std::make_unique<RemoveHighestChordToneCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3" });
    auto* commandPtr = command.get();
    REQUIRE (stack.execute (std::move (command)).succeeded());

    CHECK (commandPtr->resultingSelectionNoteIds() == std::vector<std::string> { "note-1", "note-2" });
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64 });

    REQUIRE (stack.undo().succeeded());
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64, 67 });
}

TEST_CASE ("Remove highest chord tone command cleans and restores expression references")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));

    auto expression = storedClip->expressionState();
    auto* volumeLane = expression.findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    volumeLane->addPhraseEnvelopeClip (PhraseEnvelopeClip {
        ExpressionClipId { "env-top" },
        { "note-3" },
        Region { beat (0), beat (1) },
        0.25,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    });
    volumeLane->addCyclicClip (CyclicExpressionClip {
        ExpressionClipId { "cyclic-top" },
        { "note-3" },
        Region { beat (0), beat (1) }
    });

    auto* pitchLane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    pitchLane->addPitchSlur (PitchSlur { ExpressionClipId { "slur-top" }, "note-2", "note-3" });
    auto repairedVibrato = VibratoExpression {
        ExpressionClipId { "vibrato-repaired" },
        { "note-1" },
        Region { beat (0), beat (2) }
    };
    repairedVibrato.setVoiceOverrides ({
        VibratoVoiceOverride { "note-3", 0.25, beats (0), beats (0), "sixteenth", CyclicWaveShape::sine, 0.0 }
    });
    pitchLane->addVibratoExpression (repairedVibrato);
    storedClip->setExpressionState (expression);

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<RemoveHighestChordToneCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3" })).succeeded());

    storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    CHECK (storedClip->findNoteById ("note-3") == nullptr);

    volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    CHECK (volumeLane->phraseEnvelopeClips().empty());
    CHECK (volumeLane->cyclicClips().empty());

    pitchLane = storedClip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    CHECK (pitchLane->pitchSlurs().empty());
    REQUIRE (pitchLane->vibratoExpressions().size() == 1);
    CHECK (pitchLane->vibratoExpressions().front().voiceOverrides().empty());
    CHECK_NOTHROW (evaluatePitchVoiceTrajectoryAt (*storedClip, *pitchLane, "note-1", beat (1)));

    REQUIRE (stack.undo().succeeded());
    storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    REQUIRE (storedClip->findNoteById ("note-3") != nullptr);

    volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    REQUIRE (volumeLane->phraseEnvelopeClips().size() == 1);
    REQUIRE (volumeLane->cyclicClips().size() == 1);

    pitchLane = storedClip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    REQUIRE (pitchLane->pitchSlurs().size() == 1);
    REQUIRE (pitchLane->vibratoExpressions().size() == 1);
    REQUIRE (pitchLane->vibratoExpressions().front().voiceOverrides().size() == 1);
}

TEST_CASE ("Invert chord command rotates a C major seventh upward")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));
    storedClip->addNote (pitchedNote ("note-4", 71, NoteName::b(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<InvertChordCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4" },
        InvertChordCommand::Direction::upward)).succeeded());

    CHECK (sortedNotePitches (*findClip (project, "clip-1")) == std::vector<int> { 64, 67, 71, 72 });
}

TEST_CASE ("Invert chord command rotates downward back to root position")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 72, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));
    storedClip->addNote (pitchedNote ("note-4", 71, NoteName::b(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<InvertChordCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4" },
        InvertChordCommand::Direction::downward)).succeeded());

    CHECK (sortedNotePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64, 67, 71 });
}

TEST_CASE ("Invert chord command moves a single selected note by octave")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<InvertChordCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1" },
        InvertChordCommand::Direction::upward)).succeeded());
    REQUIRE (findClip (project, "clip-1")->findNoteById ("note-1") != nullptr);
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->pitch() == MidiPitch::fromValue (72));

    REQUIRE (stack.execute (std::make_unique<InvertChordCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1" },
        InvertChordCommand::Direction::downward)).succeeded());
    REQUIRE (findClip (project, "clip-1")->findNoteById ("note-1") != nullptr);
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1")->pitch() == MidiPitch::fromValue (60));
}

TEST_CASE ("Invert chord command supports undo and redo")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));
    storedClip->addNote (pitchedNote ("note-4", 71, NoteName::b(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<InvertChordCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4" },
        InvertChordCommand::Direction::upward)).succeeded());
    CHECK (sortedNotePitches (*findClip (project, "clip-1")) == std::vector<int> { 64, 67, 71, 72 });

    REQUIRE (stack.undo().succeeded());
    CHECK (sortedNotePitches (*findClip (project, "clip-1")) == std::vector<int> { 60, 64, 67, 71 });

    REQUIRE (stack.redo().succeeded());
    CHECK (sortedNotePitches (*findClip (project, "clip-1")) == std::vector<int> { 64, 67, 71, 72 });
}

TEST_CASE ("Arpeggiate selection command turns C major seventh block upward")
{
    auto project = projectWithTrackAndClip();
    addCMajorSeventhBlock (*findClip (project, "clip-1"));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ArpeggiateSelectionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4" },
        "quarter",
        ArpeggioPattern::up)).succeeded());

    const auto* clip = findClip (project, "clip-1");
    REQUIRE (clip != nullptr);
    CHECK (notePitches (*clip) == std::vector<int> { 60, 64, 67, 71 });
    CHECK (noteStartTicks (*clip) == std::vector<std::int64_t> { 0, 960, 1920, 2880 });
    CHECK (noteDurationTicks (*clip) == std::vector<std::int64_t> { 960, 960, 960, 960 });
    REQUIRE (clip->notes()[1].spelling().has_value());
    CHECK (*clip->notes()[1].spelling() == NoteName::e());
}

TEST_CASE ("Arpeggiate selection command supports down pattern")
{
    auto project = projectWithTrackAndClip();
    addCMajorSeventhBlock (*findClip (project, "clip-1"));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ArpeggiateSelectionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4" },
        "quarter",
        ArpeggioPattern::down)).succeeded());

    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (notePitches (*findClip (project, "clip-1")) == std::vector<int> { 71, 67, 64, 60 });
}

TEST_CASE ("Arpeggiate selection command preserves separate chord spans")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);

    storedClip->addNote (MidiNote { "note-1", MidiPitch::fromValue (60), beat (0), beats (2), 100, NoteName::c() });
    storedClip->addNote (MidiNote { "note-2", MidiPitch::fromValue (64), beat (0), beats (2), 100, NoteName::e() });
    storedClip->addNote (MidiNote { "note-3", MidiPitch::fromValue (67), beat (0), beats (2), 100, NoteName::g() });
    storedClip->addNote (MidiNote { "note-4", MidiPitch::fromValue (65), beat (2), beats (2), 100, NoteName::f() });
    storedClip->addNote (MidiNote { "note-5", MidiPitch::fromValue (69), beat (2), beats (2), 100, NoteName::a() });
    storedClip->addNote (MidiNote { "note-6", MidiPitch::fromValue (72), beat (2), beats (2), 100, NoteName::c() });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ArpeggiateSelectionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4", "note-5", "note-6" },
        "quarter",
        ArpeggioPattern::up)).succeeded());

    const auto* arpeggiatedClip = findClip (project, "clip-1");
    REQUIRE (arpeggiatedClip != nullptr);
    CHECK (notePitches (*arpeggiatedClip) == std::vector<int> { 60, 64, 65, 69 });
    CHECK (noteStartTicks (*arpeggiatedClip) == std::vector<std::int64_t> { 0, 960, 1920, 2880 });
    CHECK (noteDurationTicks (*arpeggiatedClip) == std::vector<std::int64_t> { 960, 960, 960, 960 });
}

TEST_CASE ("Arpeggio subdivision stepping moves shorter and longer through enabled grids")
{
    ProjectRhythmSettings settings;

    const auto shorter = shorterArpeggioSubdivisionId ("quarter", settings);
    const auto longer = longerArpeggioSubdivisionId ("sixteenth", settings);
    const auto shorterDefinition = gridDivisionDefinitionFor (shorter, settings);
    const auto longerDefinition = gridDivisionDefinitionFor (longer, settings);

    CHECK (shorterDefinition.tickDuration.ticks() < duration (GridDivision::quarter).ticks());
    CHECK (longerDefinition.tickDuration.ticks() > duration (GridDivision::sixteenth).ticks());
}

TEST_CASE ("Disabled quintuplet arpeggio subdivision is unavailable")
{
    ProjectRhythmSettings settings;
    settings.setQuintupletsEnabled (false);

    const auto subdivisions = availableArpeggioSubdivisions (settings);
    const auto hasQuintuplet = std::any_of (subdivisions.begin(), subdivisions.end(), [] (const auto& subdivision) {
        return subdivision.id == "sixteenthQuintuplet";
    });

    CHECK_FALSE (hasQuintuplet);
}

TEST_CASE ("Enabled quintuplet arpeggio subdivision is available")
{
    ProjectRhythmSettings settings;
    settings.setQuintupletsEnabled (true);

    const auto subdivisions = availableArpeggioSubdivisions (settings);
    const auto hasQuintuplet = std::any_of (subdivisions.begin(), subdivisions.end(), [] (const auto& subdivision) {
        return subdivision.id == "sixteenthQuintuplet";
    });

    CHECK (hasQuintuplet);
}

TEST_CASE ("Arpeggiate selection command supports undo and redo")
{
    auto project = projectWithTrackAndClip();
    addCMajorSeventhBlock (*findClip (project, "clip-1"));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ArpeggiateSelectionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4" },
        "quarter",
        ArpeggioPattern::up)).succeeded());
    CHECK (noteStartTicks (*findClip (project, "clip-1")) == std::vector<std::int64_t> { 0, 960, 1920, 2880 });

    REQUIRE (stack.undo().succeeded());
    CHECK (noteStartTicks (*findClip (project, "clip-1")) == std::vector<std::int64_t> { 0, 0, 0, 0 });
    CHECK (noteDurationTicks (*findClip (project, "clip-1")) == std::vector<std::int64_t> { 3840, 3840, 3840, 3840 });

    REQUIRE (stack.redo().succeeded());
    CHECK (noteStartTicks (*findClip (project, "clip-1")) == std::vector<std::int64_t> { 0, 960, 1920, 2880 });
}

TEST_CASE ("Arpeggiate selection command cleans and restores expression references")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (MidiNote { "note-1", MidiPitch::fromValue (60), beat (0), beats (2), 100, NoteName::c() });
    storedClip->addNote (MidiNote { "note-2", MidiPitch::fromValue (64), beat (0), beats (2), 100, NoteName::e() });
    storedClip->addNote (MidiNote { "note-3", MidiPitch::fromValue (67), beat (0), beats (2), 100, NoteName::g() });

    auto expression = storedClip->expressionState();
    auto* volumeLane = expression.findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    volumeLane->addPhraseEnvelopeClip (PhraseEnvelopeClip {
        ExpressionClipId { "env-third" },
        { "note-3" },
        Region { beat (0), beat (1) },
        0.25,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    });

    auto* pitchLane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    pitchLane->addPitchSlur (PitchSlur { ExpressionClipId { "slur-third" }, "note-2", "note-3" });
    auto repairedVibrato = VibratoExpression {
        ExpressionClipId { "vibrato-repaired" },
        { "note-1" },
        Region { beat (0), beat (2) }
    };
    repairedVibrato.setVoiceOverrides ({
        VibratoVoiceOverride { "note-3", 0.25, beats (0), beats (0), "sixteenth", CyclicWaveShape::sine, 0.0 }
    });
    pitchLane->addVibratoExpression (repairedVibrato);
    storedClip->setExpressionState (expression);

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ArpeggiateSelectionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3" },
        "quarter",
        ArpeggioPattern::up)).succeeded());

    storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    CHECK (storedClip->findNoteById ("note-3") == nullptr);

    volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    CHECK (volumeLane->phraseEnvelopeClips().empty());

    pitchLane = storedClip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    CHECK (pitchLane->pitchSlurs().empty());
    REQUIRE (pitchLane->vibratoExpressions().size() == 1);
    CHECK (pitchLane->vibratoExpressions().front().voiceOverrides().empty());
    CHECK_NOTHROW (evaluatePitchVoiceTrajectoryAt (*storedClip, *pitchLane, "note-1", beat (1)));

    REQUIRE (stack.undo().succeeded());
    storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    REQUIRE (storedClip->findNoteById ("note-3") != nullptr);

    volumeLane = storedClip->expressionState().findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    REQUIRE (volumeLane->phraseEnvelopeClips().size() == 1);

    pitchLane = storedClip->expressionState().findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    REQUIRE (pitchLane->pitchSlurs().size() == 1);
    REQUIRE (pitchLane->vibratoExpressions().size() == 1);
    REQUIRE (pitchLane->vibratoExpressions().front().voiceOverrides().size() == 1);
}

TEST_CASE ("Globalize chord progression command creates a C major region")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<GlobalizeChordProgressionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3" })).succeeded());

    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    const auto& region = project.musicalStructure().chordRegions()[0];
    CHECK (region.chordName() == "C");
    CHECK (region.root() == PitchClass::c());
    CHECK (region.quality() == ChordQuality::major);
    CHECK (region.start() == beat (0));
    CHECK (region.end() == beat (1));
}

TEST_CASE ("Globalize chord progression command normalizes inversions")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-2", 67, NoteName::g(), 0));
    storedClip->addNote (pitchedNote ("note-3", 72, NoteName::c(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<GlobalizeChordProgressionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3" })).succeeded());

    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    const auto& region = project.musicalStructure().chordRegions()[0];
    CHECK (region.chordName() == "C");
    CHECK (region.root() == PitchClass::c());
    CHECK (region.chordTones() == std::vector<PitchClass> { PitchClass::c(), PitchClass::e(), PitchClass::g() });
}

TEST_CASE ("Globalize chord progression command recognizes suspended fourth chords")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 65, NoteName::f(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<GlobalizeChordProgressionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3" })).succeeded());

    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    CHECK (project.musicalStructure().chordRegions()[0].chordName() == "Csus4");
}

TEST_CASE ("Globalize chord progression command creates multiple regions")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));
    storedClip->addNote (pitchedNote ("note-4", 65, NoteName::f(), 2));
    storedClip->addNote (pitchedNote ("note-5", 69, NoteName::a(), 2));
    storedClip->addNote (pitchedNote ("note-6", 72, NoteName::c(), 2));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<GlobalizeChordProgressionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3", "note-4", "note-5", "note-6" })).succeeded());

    REQUIRE (project.musicalStructure().chordRegions().size() == 2);
    CHECK (project.musicalStructure().chordRegions()[0].chordName() == "C");
    CHECK (project.musicalStructure().chordRegions()[0].start() == beat (0));
    CHECK (project.musicalStructure().chordRegions()[0].end() == beat (2));
    CHECK (project.musicalStructure().chordRegions()[1].chordName() == "F");
    CHECK (project.musicalStructure().chordRegions()[1].start() == beat (2));
    CHECK (project.musicalStructure().chordRegions()[1].end() == beat (3));
}

TEST_CASE ("Globalize chord progression command supports undo and redo")
{
    auto project = projectWithTrackAndClip();
    auto* storedClip = findClip (project, "clip-1");
    REQUIRE (storedClip != nullptr);
    storedClip->addNote (pitchedNote ("note-1", 60, NoteName::c(), 0));
    storedClip->addNote (pitchedNote ("note-2", 64, NoteName::e(), 0));
    storedClip->addNote (pitchedNote ("note-3", 67, NoteName::g(), 0));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<GlobalizeChordProgressionCommand> (
        "track-1",
        "clip-1",
        std::vector<std::string> { "note-1", "note-2", "note-3" })).succeeded());
    REQUIRE (project.musicalStructure().chordRegions().size() == 1);

    REQUIRE (stack.undo().succeeded());
    CHECK (project.musicalStructure().chordRegions().empty());

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    CHECK (project.musicalStructure().chordRegions()[0].chordName() == "C");
}

TEST_CASE ("Chord region replace and delete commands support undo and redo")
{
    Project project { "project-1", "Song" };
    const auto original = ChordRegion {
        Region { beat (0), beat (2) },
        PitchClass::c(),
        ChordQuality::major,
        { PitchClass::c(), PitchClass::e(), PitchClass::g() },
        "C"
    };
    const auto resized = ChordRegion {
        Region { beat (1), beat (3) },
        PitchClass::c(),
        ChordQuality::major,
        { PitchClass::c(), PitchClass::e(), PitchClass::g() },
        "C"
    };
    project.musicalStructure().addChordRegion (original);

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ReplaceChordRegionCommand> (original, resized)).succeeded());
    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    CHECK (project.musicalStructure().chordRegions()[0].start() == beat (1));
    CHECK (project.musicalStructure().chordRegions()[0].end() == beat (3));

    REQUIRE (stack.undo().succeeded());
    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    CHECK (project.musicalStructure().chordRegions()[0].start() == beat (0));
    CHECK (project.musicalStructure().chordRegions()[0].end() == beat (2));

    REQUIRE (stack.redo().succeeded());
    REQUIRE (stack.execute (std::make_unique<DeleteChordRegionCommand> (resized)).succeeded());
    CHECK (project.musicalStructure().chordRegions().empty());

    REQUIRE (stack.undo().succeeded());
    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    CHECK (project.musicalStructure().chordRegions()[0].chordName() == "C");
}

TEST_CASE ("Add chord region command supports undo and redo")
{
    Project project { "project-1", "Song" };
    const auto region = ChordRegion {
        Region { beat (0), beat (2) },
        PitchClass::c(),
        ChordQuality::major,
        { PitchClass::c(), PitchClass::e(), PitchClass::g() },
        "C"
    };

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddChordRegionCommand> (region)).succeeded());
    REQUIRE (project.musicalStructure().chordRegions().size() == 1);

    REQUIRE (stack.undo().succeeded());
    CHECK (project.musicalStructure().chordRegions().empty());

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.musicalStructure().chordRegions().size() == 1);
    CHECK (project.musicalStructure().chordRegions()[0].chordName() == "C");
}

TEST_CASE ("Resize clip command can extend an arrangement as a loop")
{
    auto project = projectWithTrackAndClip();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ResizeClipCommand> (
        "track-1",
        "clip-1",
        beats (12),
        ClipLoop::enabled (beats (4)))).succeeded());

    REQUIRE (findClip (project, "clip-1") != nullptr);
    CHECK (findClip (project, "clip-1")->length() == beats (12));
    CHECK (findClip (project, "clip-1")->sourceLength() == beats (4));
    CHECK (findClip (project, "clip-1")->loop().isEnabled());

    REQUIRE (stack.undo().succeeded());
    CHECK (findClip (project, "clip-1")->length() == beats (4));
    CHECK (findClip (project, "clip-1")->sourceLength() == beats (4));
    CHECK_FALSE (findClip (project, "clip-1")->loop().isEnabled());

    REQUIRE (stack.redo().succeeded());
    CHECK (findClip (project, "clip-1")->length() == beats (12));
    CHECK (findClip (project, "clip-1")->sourceLength() == beats (4));
    CHECK (findClip (project, "clip-1")->loop().isEnabled());
}

TEST_CASE ("Resize clip command shortens looped visible length while preserving source length")
{
    Project project { "project-1", "Song" };
    Track track { "track-1", "Piano" };
    track.addClip (MidiClip { "clip-1", "Clip", beat (0), beats (12), ClipLoop::enabled (beats (4)) });
    project.addTrack (std::move (track));

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ResizeClipCommand> ("track-1", "clip-1", beats (6))).succeeded());
    CHECK (findClip (project, "clip-1")->length() == beats (6));
    CHECK (findClip (project, "clip-1")->sourceLength() == beats (4));

    REQUIRE (stack.undo().succeeded());
    CHECK (findClip (project, "clip-1")->length() == beats (12));
    CHECK (findClip (project, "clip-1")->sourceLength() == beats (4));
}

TEST_CASE ("Add scale mode region command supports undo and redo")
{
    Project project { "project-1", "Song" };
    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto command = std::make_unique<AddScaleModeRegionCommand> (ScaleModeRegion { bars (5, 9), "Mixolydian" });

    REQUIRE (stack.execute (std::move (command)).succeeded());
    REQUIRE (project.musicalStructure().scaleModeRegions().size() == 1);
    CHECK (project.musicalStructure().scaleModeRegions()[0].scaleDefinitionName() == "Mixolydian");

    REQUIRE (stack.undo().succeeded());
    CHECK (project.musicalStructure().scaleModeRegions().empty());

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.musicalStructure().scaleModeRegions().size() == 1);
    CHECK (project.musicalStructure().scaleModeRegions()[0].scaleDefinitionName() == "Mixolydian");
}

TEST_CASE ("Delete key center and scale mode region commands support undo")
{
    Project project { "project-1", "Song" };
    const auto keyRegion = KeyCenterRegion { bars (1, 5), PitchClass::g() };
    const auto scaleRegion = ScaleModeRegion { bars (1, 5), "Mixolydian" };
    project.musicalStructure().addKeyCenterRegion (keyRegion);
    project.musicalStructure().addScaleModeRegion (scaleRegion);

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<DeleteKeyCenterRegionCommand> (keyRegion)).succeeded());
    CHECK (project.musicalStructure().keyCenterRegions().empty());

    REQUIRE (stack.undo().succeeded());
    REQUIRE (project.musicalStructure().keyCenterRegions().size() == 1);
    CHECK (project.musicalStructure().keyCenterRegions()[0].pitchClass() == PitchClass::g());

    REQUIRE (stack.execute (std::make_unique<DeleteScaleModeRegionCommand> (scaleRegion)).succeeded());
    CHECK (project.musicalStructure().scaleModeRegions().empty());

    REQUIRE (stack.undo().succeeded());
    REQUIRE (project.musicalStructure().scaleModeRegions().size() == 1);
    CHECK (project.musicalStructure().scaleModeRegions()[0].scaleDefinitionName() == "Mixolydian");
}

TEST_CASE ("Tempo node command supports undo and redo")
{
    Project project { "project-1", "Song" };
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddTempoNodeCommand> (beat (8), Tempo { 132.0 })).succeeded());
    REQUIRE (project.tempoMap().nodes().size() == 2);
    CHECK (project.tempoMap().tempoAt (beat (8)).bpm() == 132.0);

    REQUIRE (stack.undo().succeeded());
    REQUIRE (project.tempoMap().nodes().size() == 1);
    CHECK (project.tempoMap().tempoAt (beat (8)).bpm() == 120.0);

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.tempoMap().nodes().size() == 2);
    CHECK (project.tempoMap().tempoAt (beat (8)).bpm() == 132.0);
}

TEST_CASE ("Time signature marker command supports undo and redo")
{
    Project project { "project-1", "Song" };
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddTimeSignatureMarkerCommand> (beat (8), TimeSignature { 3, 4 })).succeeded());
    REQUIRE (project.timeSignatureMap().markers().size() == 2);
    CHECK (project.timeSignatureMap().timeSignatureAt (beat (8)).numerator() == 3);

    REQUIRE (stack.undo().succeeded());
    REQUIRE (project.timeSignatureMap().markers().size() == 1);
    CHECK (project.timeSignatureMap().timeSignatureAt (beat (8)).numerator() == 4);

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.timeSignatureMap().markers().size() == 2);
    CHECK (project.timeSignatureMap().timeSignatureAt (beat (8)).numerator() == 3);
}

TEST_CASE ("Key center and scale mode regions can be replaced through commands")
{
    Project project { "project-1", "Song" };
    project.musicalStructure().addKeyCenterRegion (KeyCenterRegion { bars (1, 5), PitchClass::c() });
    project.musicalStructure().addScaleModeRegion (ScaleModeRegion { bars (1, 5), "Major" });

    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<ReplaceKeyCenterRegionCommand> (
        KeyCenterRegion { bars (1, 5), PitchClass::c() },
        KeyCenterRegion { bars (3, 7), PitchClass::g() })).succeeded());
    CHECK (project.musicalStructure().keyCenterAt (beat (8)) == PitchClass::g());

    REQUIRE (stack.execute (std::make_unique<ReplaceScaleModeRegionCommand> (
        ScaleModeRegion { bars (1, 5), "Major" },
        ScaleModeRegion { bars (3, 7), "Mixolydian" })).succeeded());
    CHECK (project.musicalStructure().scaleDefinitionNameAt (beat (8)) == "Mixolydian");

    REQUIRE (stack.undo().succeeded());
    CHECK (project.musicalStructure().scaleDefinitionNameAt (beat (0)) == "Major");

    REQUIRE (stack.undo().succeeded());
    CHECK (project.musicalStructure().keyCenterAt (beat (0)) == PitchClass::c());
}

TEST_CASE ("Add custom scale command supports undo and redo")
{
    Project project { "project-1", "Song" };
    ProjectCommandContext context { project };
    CommandStack stack { context };

    auto scale = CustomScaleBuilder::build (
        ScaleMetadata { "User Hexatonic", "Custom", { "user" }, "Six selected notes." },
        { PitchClass::c(), PitchClass::d(), PitchClass::e(), PitchClass::fSharp(), PitchClass::gSharp(), PitchClass::aSharp() });

    REQUIRE (stack.execute (std::make_unique<AddCustomScaleCommand> (scale)).succeeded());
    REQUIRE (project.customScales().size() == 1);
    CHECK (project.customScales()[0].name() == "User Hexatonic");

    REQUIRE (stack.undo().succeeded());
    CHECK (project.customScales().empty());

    REQUIRE (stack.redo().succeeded());
    REQUIRE (project.customScales().size() == 1);
    CHECK (project.customScales()[0].pitchClassOffsetsFromRoot().size() == 6);
}

TEST_CASE ("Redo stack clears after executing a new command")
{
    auto project = projectWithTrackAndClip();
    ProjectCommandContext context { project };
    CommandStack stack { context };

    REQUIRE (stack.execute (std::make_unique<AddNoteCommand> ("track-1", "clip-1", note ("note-1"))).succeeded());
    REQUIRE (stack.undo().succeeded());
    REQUIRE (stack.canRedo());

    REQUIRE (stack.execute (std::make_unique<AddNoteCommand> ("track-1", "clip-1", note ("note-2", 1))).succeeded());

    CHECK_FALSE (stack.canRedo());
    CHECK (findClip (project, "clip-1")->findNoteById ("note-1") == nullptr);
    CHECK (findClip (project, "clip-1")->findNoteById ("note-2") != nullptr);
}
