#include "core/sequencing/PitchExpressionEvaluation.h"

#include "core/music_theory/MidiPitch.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core::music_theory;
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickDuration beats (std::int64_t count)
{
    return TickDuration::fromTicks (count * ticksPerQuarterNote);
}

TickDuration ticks (std::int64_t count)
{
    return TickDuration::fromTicks (count);
}

TickPosition beat (std::int64_t count)
{
    return TickPosition::fromTicks (count * ticksPerQuarterNote);
}

MidiNote note (std::string id, int pitch, std::int64_t startBeat, std::int64_t lengthBeats = 1)
{
    return MidiNote {
        std::move (id),
        MidiPitch::fromValue (pitch),
        beat (startBeat),
        beats (lengthBeats),
        100
    };
}

MidiClip chordSlurClip()
{
    MidiClip clip { "clip-1", "Chord Slur", beat (0), beats (8) };
    clip.addNote (note ("src-high", 67, 0, 2));
    clip.addNote (note ("src-low", 60, 0, 2));
    clip.addNote (note ("src-mid", 64, 0, 2));
    clip.addNote (note ("dst-mid", 65, 2, 2));
    clip.addNote (note ("dst-low", 62, 2, 2));
    clip.addNote (note ("dst-high", 69, 2, 2));
    return clip;
}
}

TEST_CASE ("Pitch expression pairs chord slurs by register")
{
    const auto clip = chordSlurClip();
    const auto pairs = pairNotesByRegister (
        clip,
        { "src-high", "src-low", "src-mid" },
        { "dst-mid", "dst-low", "dst-high" });

    REQUIRE (pairs.size() == 3);
    CHECK (pairs[0].sourceNoteId == "src-low");
    CHECK (pairs[0].destinationNoteId == "dst-low");
    CHECK (pairs[1].sourceNoteId == "src-mid");
    CHECK (pairs[1].destinationNoteId == "dst-mid");
    CHECK (pairs[2].sourceNoteId == "src-high");
    CHECK (pairs[2].destinationNoteId == "dst-high");
}

TEST_CASE ("Pitch expression creates zero-time legato slur blocks")
{
    const auto clip = chordSlurClip();
    const auto slurs = createLegatoPitchSlurBlock (
        clip,
        { "src-low", "src-mid", "src-high" },
        { "dst-low", "dst-mid", "dst-high" },
        ExpressionBlockId { "block-1" },
        "block-slur");

    REQUIRE (slurs.size() == 3);
    CHECK (slurs[0].id().value == "block-slur-1");
    CHECK (slurs[0].slurTime() == TickDuration {});
    CHECK (slurs[0].legatoNoRetrigger());
    REQUIRE (slurs[0].blockId().has_value());
    CHECK (slurs[0].blockId()->value == "block-1");
    CHECK_FALSE (slurs[0].hasVoiceOverride());
}

TEST_CASE ("Pitch expression shared block edits skip diverged voice overrides")
{
    const auto clip = chordSlurClip();
    auto slurs = createLegatoPitchSlurBlock (
        clip,
        { "src-low", "src-mid", "src-high" },
        { "dst-low", "dst-mid", "dst-high" },
        ExpressionBlockId { "block-1" },
        "slur");

    ExpressionLane pitchLane { ExpressionState::defaultPitchLaneId(), "Pitch", ExpressionLanePolarity::bipolar };
    for (auto& slur : slurs)
        pitchLane.addPitchSlur (slur);

    applySharedSlurBlockSettings (pitchLane, PitchSlurBlockSettings {
        ExpressionBlockId { "block-1" },
        beats (1),
        ExpressionCurveShape::logarithmic,
        true
    });

    REQUIRE (pitchLane.findPitchSlur (ExpressionClipId { "slur-1" }) != nullptr);
    CHECK (pitchLane.findPitchSlur (ExpressionClipId { "slur-1" })->slurTime() == beats (1));

    applySlurVoiceOverride (pitchLane,
                            ExpressionClipId { "slur-2" },
                            PitchSlurBlockSettings {
                                ExpressionBlockId { "block-1" },
                                beats (2),
                                ExpressionCurveShape::exponential,
                                false
                            });

    applySharedSlurBlockSettings (pitchLane, PitchSlurBlockSettings {
        ExpressionBlockId { "block-1" },
        beats (3),
        ExpressionCurveShape::linear,
        true
    });

    CHECK (pitchLane.findPitchSlur (ExpressionClipId { "slur-1" })->slurTime() == beats (3));
    CHECK (pitchLane.findPitchSlur (ExpressionClipId { "slur-2" })->slurTime() == beats (2));
    CHECK (pitchLane.findPitchSlur (ExpressionClipId { "slur-2" })->curveShape() == ExpressionCurveShape::exponential);
    CHECK (pitchLane.findPitchSlur (ExpressionClipId { "slur-2" })->hasVoiceOverride());
    CHECK_FALSE (pitchLane.findPitchSlur (ExpressionClipId { "slur-2" })->legatoNoRetrigger());
}

TEST_CASE ("Pitch expression slur trajectory follows curve shape")
{
    MidiClip clip { "clip-1", "Slur", beat (0), beats (4) };
    clip.addNote (note ("src", 60, 0, 4));
    clip.addNote (note ("dst", 64, 1, 3));

    ExpressionLane pitchLane { ExpressionState::defaultPitchLaneId(), "Pitch", ExpressionLanePolarity::bipolar };
    PitchSlur slur { ExpressionClipId { "slur-1" }, "src", "dst" };
    slur.setSlurTime (beats (2));
    slur.setCurveShape (ExpressionCurveShape::linear);
    pitchLane.addPitchSlur (slur);

    CHECK (evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "src", beat (0)).finalSemitones == Catch::Approx (60.0));
    CHECK (evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "src", beat (1)).finalSemitones == Catch::Approx (60.0));
    CHECK (evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "src", beat (2)).finalSemitones == Catch::Approx (62.0));
    CHECK (evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "src", beat (3)).finalSemitones == Catch::Approx (64.0));
}

TEST_CASE ("Pitch expression vibrato phase is phrase-continuous across notes")
{
    MidiClip clip { "clip-1", "Vibrato", beat (0), beats (4) };
    clip.addNote (note ("n1", 60, 0, 2));
    clip.addNote (note ("n2", 62, 2, 2));

    ExpressionLane pitchLane { ExpressionState::defaultPitchLaneId(), "Pitch", ExpressionLanePolarity::bipolar };
    VibratoExpression vibrato { ExpressionClipId { "vib-1" }, { "n1", "n2" }, Region { beat (0), beat (4) } };
    vibrato.setAmplitudeSemitones (1.0);
    vibrato.setFrequencyDivisionId ("quarter");
    vibrato.setWaveShape (CyclicWaveShape::sine);
    pitchLane.addVibratoExpression (vibrato);

    const auto n1 = evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "n1", beat (1) + ticks (ticksPerQuarterNote / 4));
    const auto n2 = evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "n2", beat (2) + ticks (ticksPerQuarterNote / 4));

    CHECK (n1.vibratoOffsetSemitones == Catch::Approx (1.0).margin (0.0000001));
    CHECK (n2.vibratoOffsetSemitones == Catch::Approx (1.0).margin (0.0000001));
}

TEST_CASE ("Pitch expression vibrato layers on top of slur trajectories")
{
    MidiClip clip { "clip-1", "Slur Vibrato", beat (0), beats (4) };
    clip.addNote (note ("src", 60, 0, 4));
    clip.addNote (note ("dst", 64, 1, 3));

    ExpressionLane pitchLane { ExpressionState::defaultPitchLaneId(), "Pitch", ExpressionLanePolarity::bipolar };
    PitchSlur slur { ExpressionClipId { "slur-1" }, "src", "dst" };
    slur.setSlurTime (beats (2));
    pitchLane.addPitchSlur (slur);

    VibratoExpression vibrato { ExpressionClipId { "vib-1" }, { "src" }, Region { beat (0), beat (4) } };
    vibrato.setAmplitudeSemitones (0.5);
    vibrato.setFrequencyDivisionId ("quarter");
    vibrato.setPhase (0.25);
    pitchLane.addVibratoExpression (vibrato);

    const auto sample = evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "src", beat (2));
    CHECK (sample.baseSemitones == Catch::Approx (60.0));
    CHECK (sample.slurOffsetSemitones == Catch::Approx (2.0));
    CHECK (sample.vibratoOffsetSemitones == Catch::Approx (0.5).margin (0.0000001));
    CHECK (sample.finalSemitones == Catch::Approx (62.5).margin (0.0000001));
}

TEST_CASE ("Pitch expression voice overrides make polyphonic trajectories independent")
{
    const auto clip = chordSlurClip();
    auto slurs = createLegatoPitchSlurBlock (
        clip,
        { "src-low", "src-mid" },
        { "dst-low", "dst-mid" },
        ExpressionBlockId { "block-1" },
        "slur");

    ExpressionLane pitchLane { ExpressionState::defaultPitchLaneId(), "Pitch", ExpressionLanePolarity::bipolar };
    for (auto& slur : slurs)
    {
        slur.setSlurTime (beats (2));
        pitchLane.addPitchSlur (slur);
    }

    applySlurVoiceOverride (pitchLane,
                            ExpressionClipId { "slur-2" },
                            PitchSlurBlockSettings {
                                ExpressionBlockId { "block-1" },
                                beats (1),
                                ExpressionCurveShape::linear,
                                true
                            });

    const auto low = evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "src-low", beat (3));
    const auto mid = evaluatePitchVoiceTrajectoryAt (clip, pitchLane, "src-mid", beat (3));

    CHECK (low.finalSemitones == Catch::Approx (61.0));
    CHECK (mid.finalSemitones == Catch::Approx (65.0));
}
