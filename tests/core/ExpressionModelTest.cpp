#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiClip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
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

Region beatRegion (int startBeat, int endBeat)
{
    return Region { beat (startBeat), beat (endBeat) };
}

PhraseEnvelopeClip phraseEnvelope (std::string id, Region region)
{
    return PhraseEnvelopeClip {
        ExpressionClipId { std::move (id) },
        std::vector<std::string> { "note-1" },
        region,
        0.5,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    };
}
}

TEST_CASE ("Expression state creates default volume and pitch lanes")
{
    ExpressionState state;

    REQUIRE (state.lanes().size() == 2);
    REQUIRE (state.findLane (ExpressionState::defaultVolumeLaneId()) != nullptr);
    REQUIRE (state.findLane (ExpressionState::defaultPitchLaneId()) != nullptr);
    CHECK (state.findLane (ExpressionState::defaultVolumeLaneId())->name() == "Volume");
    CHECK (state.findLane (ExpressionState::defaultVolumeLaneId())->polarity() == ExpressionLanePolarity::unipolar);
    CHECK (state.findLane (ExpressionState::defaultPitchLaneId())->name() == "Pitch");
    CHECK (state.findLane (ExpressionState::defaultPitchLaneId())->polarity() == ExpressionLanePolarity::bipolar);
    CHECK_THROWS_AS (state.removeLane (ExpressionState::defaultVolumeLaneId()), std::invalid_argument);
    CHECK_THROWS_AS (state.removeLane (ExpressionState::defaultPitchLaneId()), std::invalid_argument);
}

TEST_CASE ("Expression lanes can be added renamed and removed")
{
    ExpressionState state;
    ExpressionLane lane { ExpressionLaneId { "expr-energy" }, "Energy", ExpressionLanePolarity::unipolar };

    state.addLane (lane);
    CHECK_THROWS_AS (state.addLane (lane), std::invalid_argument);

    auto* addedLane = state.findLane (ExpressionLaneId { "expr-energy" });
    REQUIRE (addedLane != nullptr);
    addedLane->rename ("Pressure");
    addedLane->setEnabled (false);
    addedLane->setPolarity (ExpressionLanePolarity::bipolar);

    CHECK (addedLane->name() == "Pressure");
    CHECK_FALSE (addedLane->enabled());
    CHECK (addedLane->polarity() == ExpressionLanePolarity::bipolar);

    const auto removed = state.removeLane (ExpressionLaneId { "expr-energy" });
    CHECK (removed.name() == "Pressure");
    CHECK (state.findLane (ExpressionLaneId { "expr-energy" }) == nullptr);
}

TEST_CASE ("Expression lanes clamp values by polarity")
{
    ExpressionLane unipolarLane { ExpressionLaneId { "expr-unipolar" }, "Unipolar", ExpressionLanePolarity::unipolar };
    ExpressionLane bipolarLane { ExpressionLaneId { "expr-bipolar" }, "Bipolar", ExpressionLanePolarity::bipolar };

    CHECK (unipolarLane.clampValue (-0.25) == Catch::Approx (0.0));
    CHECK (unipolarLane.clampValue (1.25) == Catch::Approx (1.0));
    CHECK (bipolarLane.clampValue (-2.0) == Catch::Approx (-1.0));
    CHECK (bipolarLane.clampValue (2.0) == Catch::Approx (1.0));
}

TEST_CASE ("Expression routes map lane values to destination ranges")
{
    ExpressionLane lane { ExpressionLaneId { "expr-lane" }, "Lane", ExpressionLanePolarity::unipolar };
    ExpressionRoute route {
        ExpressionRouteId { "route-1" },
        ExpressionDestination::firstPartyParameter ("track-1", DeviceSlotId { "device-1" }, "osc.pm.amount"),
        0.8,
        0.2
    };

    CHECK (route.destination().stableId() == "first-party-parameter:track-1:device-1:osc.pm.amount");
    CHECK (route.mapLaneValue (0.0, ExpressionLanePolarity::unipolar) == Catch::Approx (0.8));
    CHECK (route.mapLaneValue (1.0, ExpressionLanePolarity::unipolar) == Catch::Approx (0.2));
    CHECK (route.mapLaneValue (-1.0, ExpressionLanePolarity::bipolar) == Catch::Approx (0.8));
    CHECK (route.mapLaneValue (1.0, ExpressionLanePolarity::bipolar) == Catch::Approx (0.2));

    lane.addRoute (route);
    CHECK_THROWS_AS (lane.addRoute (route), std::invalid_argument);
    CHECK (lane.removeRoute (ExpressionRouteId { "route-1" }).id() == ExpressionRouteId { "route-1" });
    CHECK_THROWS_AS (
        ExpressionRoute (ExpressionRouteId { "bad-route" }, ExpressionDestination::midiCc ("track-1", 128), 0.0, 1.0),
        std::invalid_argument);
}

TEST_CASE ("Phrase envelope clips validate stage timing")
{
    auto clip = phraseEnvelope ("env-1", beatRegion (0, 4));
    clip.setStoredLevel (2.0, ExpressionLanePolarity::unipolar);
    clip.setPeakLevel (-2.0, ExpressionLanePolarity::bipolar);
    clip.setSustainLevel (2.0, ExpressionLanePolarity::bipolar);
    clip.setDecayStage (EnvelopeStage { EnvelopeStageType::decay, beats (1), 1.0, 0.5 });
    clip.setReleaseStage (EnvelopeStage { EnvelopeStageType::release, beats (1), 0.5, 0.0 });
    clip.setTailExtension (beats (1));

    CHECK (clip.storedLevel() == Catch::Approx (1.0));
    REQUIRE (clip.peakLevel().has_value());
    CHECK (*clip.peakLevel() == Catch::Approx (-1.0));
    REQUIRE (clip.sustainLevel().has_value());
    CHECK (*clip.sustainLevel() == Catch::Approx (1.0));
    REQUIRE (clip.decayStage().has_value());
    REQUIRE (clip.releaseStage().has_value());
    CHECK_THROWS_AS (clip.setReleaseStage (EnvelopeStage { EnvelopeStageType::release, beats (3), 0.5, 0.0 }), std::invalid_argument);
    CHECK (clip.releaseStage()->duration == beats (1));
    CHECK_THROWS_AS (clip.setAttackStage (EnvelopeStage { EnvelopeStageType::decay, beats (1), 0.0, 1.0 }), std::invalid_argument);
    CHECK_THROWS_AS (
        PhraseEnvelopeClip (ExpressionClipId { "too-long" },
                            { "note-1" },
                            beatRegion (0, 1),
                            0.5,
                            EnvelopeStage { EnvelopeStageType::attack, beats (2), 0.0, 1.0 }),
        std::invalid_argument);
}

TEST_CASE ("Cyclic expression clips reject overlapping regions on the same lane")
{
    ExpressionLane lane { ExpressionLaneId { "expr-vibrato" }, "Vibrato", ExpressionLanePolarity::unipolar };

    CyclicExpressionClip first { ExpressionClipId { "cyclic-1" }, { "note-1" }, beatRegion (0, 2) };
    first.setAttackTime (beats (1));
    first.setMaxAmplitude (2.0);
    first.setFrequencyDivisionId ("eighth");

    CHECK (first.maxAmplitude() == Catch::Approx (1.0));

    lane.addCyclicClip (first);
    CHECK_THROWS_AS (
        lane.addCyclicClip (CyclicExpressionClip { ExpressionClipId { "cyclic-overlap" }, { "note-2" }, beatRegion (1, 3) }),
        std::invalid_argument);
    lane.addCyclicClip (CyclicExpressionClip { ExpressionClipId { "cyclic-adjacent" }, { "note-3" }, beatRegion (2, 4) });

    REQUIRE (lane.cyclicClips().size() == 2);
    CHECK_THROWS_AS (first.setReleaseTime (beats (2)), std::invalid_argument);
}

TEST_CASE ("Pitch slurs and vibrato expressions validate musical references")
{
    PitchSlur slur { ExpressionClipId { "slur-1" }, "note-1", "note-2" };
    slur.setSlurTime (beats (1));
    slur.setCurveShape (ExpressionCurveShape::exponential);
    slur.setBlockId (ExpressionBlockId { "block-1" });
    slur.setHasVoiceOverride (true);

    CHECK (slur.legatoNoRetrigger());
    CHECK (slur.slurTime() == beats (1));
    CHECK (slur.blockId()->value == "block-1");
    CHECK (slur.hasVoiceOverride());
    CHECK_THROWS_AS (PitchSlur (ExpressionClipId { "bad-slur" }, "note-1", "note-1"), std::invalid_argument);

    VibratoExpression vibrato { ExpressionClipId { "vibrato-1" }, { "note-1", "note-2" }, beatRegion (0, 4) };
    vibrato.setAttackTime (beats (1));
    vibrato.setReleaseTime (beats (1));
    vibrato.setAmplitudeSemitones (0.75);
    vibrato.setFrequencyDivisionId ("sixteenth-triplet");
    vibrato.setBlockId (ExpressionBlockId { "vib-block" });
    vibrato.setVoiceOverrides ({
        VibratoVoiceOverride { "note-2", 0.25, beats (1), beats (1), "eighth", CyclicWaveShape::triangle, 0.25 }
    });

    CHECK (vibrato.amplitudeSemitones() == Catch::Approx (0.75));
    CHECK (vibrato.frequencyDivisionId() == "sixteenth-triplet");
    REQUIRE (vibrato.voiceOverrides().size() == 1);
    CHECK (vibrato.voiceOverrides()[0].noteId == "note-2");
    CHECK_THROWS_AS (vibrato.setReleaseTime (beats (4)), std::invalid_argument);
    CHECK_THROWS_AS (
        vibrato.setVoiceOverrides ({
            VibratoVoiceOverride { "note-2", 0.25, {}, {}, "eighth", CyclicWaveShape::triangle, 0.0 },
            VibratoVoiceOverride { "note-2", 0.5, {}, {}, "sixteenth", CyclicWaveShape::sine, 0.0 }
        }),
        std::invalid_argument);
}

TEST_CASE ("MIDI clips own expression state alongside harmonic metadata")
{
    MidiClip clip { "clip-1", "Phrase", beat (0), beats (4) };
    REQUIRE (clip.expressionState().findLane (ExpressionState::defaultVolumeLaneId()) != nullptr);

    ExpressionState expressionState;
    expressionState.addLane (ExpressionLane { ExpressionLaneId { "expr-energy" }, "Energy", ExpressionLanePolarity::unipolar });
    clip.setExpressionState (expressionState);

    REQUIRE (clip.expressionState().findLane (ExpressionLaneId { "expr-energy" }) != nullptr);

    const auto movedClip = clip.withStartInProject (beat (8));
    REQUIRE (movedClip.expressionState().findLane (ExpressionLaneId { "expr-energy" }) != nullptr);
    CHECK (movedClip.expressionState().findLane (ExpressionLaneId { "expr-energy" })->name() == "Energy");
}
