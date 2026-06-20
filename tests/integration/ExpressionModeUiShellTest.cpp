#include "app/AppServices.h"
#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "ui/PianoRollComponent.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>
#include <vector>

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

void addClipWithNote (app::AppServices& services)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    core::sequencing::MidiClip clip { "clip-1", "Expression Clip", beat (0), beats (4) };
    clip.addNote (core::sequencing::MidiNote {
        "note-1",
        core::music_theory::MidiPitch::fromValue (60),
        beat (0),
        beats (1),
        100
    });
    track->addClip (clip);
}

void addClipWithSlurPair (app::AppServices& services)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    core::sequencing::MidiClip clip { "slur-pair", "Slur Pair", beat (0), beats (4) };
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
        beat (1),
        beats (1),
        100
    });
    track->addClip (clip);
}

void addClipWithSlurRun (app::AppServices& services)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    core::sequencing::MidiClip clip { "slur-run", "Slur Run", beat (0), beats (4) };
    const int pitches[] { 60, 62, 64 };
    for (auto index = 0; index < 3; ++index)
    {
        clip.addNote (core::sequencing::MidiNote {
            "run-note-" + std::to_string (index + 1),
            core::music_theory::MidiPitch::fromValue (pitches[index]),
            beat (index),
            beats (1),
            100
        });
    }
    track->addClip (clip);
}

void addClipWithSlurChords (app::AppServices& services)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    core::sequencing::MidiClip clip { "slur-chords", "Slur Chords", beat (0), beats (4) };
    const int sourcePitches[] { 60, 64, 67 };
    const int destinationPitches[] { 62, 65, 69 };
    for (auto index = 0; index < 3; ++index)
    {
        clip.addNote (core::sequencing::MidiNote {
            "source-" + std::to_string (index + 1),
            core::music_theory::MidiPitch::fromValue (sourcePitches[index]),
            beat (0),
            beats (1),
            100
        });
        clip.addNote (core::sequencing::MidiNote {
            "destination-" + std::to_string (index + 1),
            core::music_theory::MidiPitch::fromValue (destinationPitches[index]),
            beat (1),
            beats (1),
            100
        });
    }
    track->addClip (clip);
}

void addClipWithThreeSlurChords (app::AppServices& services)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    core::sequencing::MidiClip clip { "slur-three-chords", "Slur Three Chords", beat (0), beats (6) };
    const int chordPitches[][3] {
        { 60, 64, 67 },
        { 62, 65, 69 },
        { 64, 67, 71 }
    };

    for (auto chordIndex = 0; chordIndex < 3; ++chordIndex)
        for (auto voiceIndex = 0; voiceIndex < 3; ++voiceIndex)
            clip.addNote (core::sequencing::MidiNote {
                "chord-" + std::to_string (chordIndex + 1) + "-voice-" + std::to_string (voiceIndex + 1),
                core::music_theory::MidiPitch::fromValue (chordPitches[chordIndex][voiceIndex]),
                beat (chordIndex),
                beats (1),
                100
            });

    track->addClip (clip);
}

void addSimpleOscClipWithReleaseTail (app::AppServices& services)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    auto state = core::devices::defaultFirstPartyDeviceState (core::devices::simpleOscComplexDefinition());
    for (auto& parameter : state.parameterValues)
        if (parameter.parameterId == "amp.release")
            parameter.normalizedValue = 0.05;

    core::sequencing::DeviceChain chain;
    chain.appendSlot (core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { "simple-osc-complex" },
        state,
        core::sequencing::PluginKind::instrument
    });
    track->setDeviceChain (chain);

    core::sequencing::MidiClip clip { "release-clip", "Release Clip", beat (0), beats (8) };
    clip.addNote (core::sequencing::MidiNote {
        "note-1",
        core::music_theory::MidiPitch::fromValue (60),
        beat (1),
        beats (1),
        100
    });
    track->addClip (clip);
}

void addExpressionOverlayClip (app::AppServices& services,
                               std::string clipId,
                               int phraseEnvelopeCount,
                               bool denseCyclic)
{
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    core::sequencing::MidiClip clip { clipId, "Expression Overlay Clip", beat (0), beats (16) };
    for (auto index = 0; index < 32; ++index)
    {
        clip.addNote (core::sequencing::MidiNote {
            "note-" + std::to_string (index + 1),
            core::music_theory::MidiPitch::fromValue (48 + (index % 24)),
            core::time::TickPosition::fromTicks (index * (core::time::ticksPerQuarterNote / 2)),
            core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2),
            100
        });
    }

    auto expression = clip.expressionState();
    auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);

    for (auto index = 0; index < phraseEnvelopeCount; ++index)
    {
        const auto start = beat (index);
        const auto end = beat (index + 1);
        core::sequencing::PhraseEnvelopeClip envelope {
            core::sequencing::ExpressionClipId { "env-" + std::to_string (index + 1) },
            { "note-" + std::to_string (index + 1) },
            core::sequencing::Region { start, end },
            0.0,
            core::sequencing::EnvelopeStage {
                core::sequencing::EnvelopeStageType::attack,
                core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 4),
                0.0,
                1.0
            }
        };
        envelope.setDecayStage (core::sequencing::EnvelopeStage {
            core::sequencing::EnvelopeStageType::decay,
            core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 4),
            1.0,
            0.45
        });
        envelope.setSustainLevel (0.45, core::sequencing::ExpressionLanePolarity::unipolar);
        envelope.setReleaseStage (core::sequencing::EnvelopeStage {
            core::sequencing::EnvelopeStageType::release,
            core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 4),
            0.45,
            0.0
        });
        lane->addPhraseEnvelopeClip (envelope);
    }

    if (denseCyclic)
    {
        core::sequencing::CyclicExpressionClip cyclic {
            core::sequencing::ExpressionClipId { "cyclic-dense" },
            { "note-1" },
            core::sequencing::Region { beat (0), beat (16) }
        };
        cyclic.setMaxAmplitude (0.45);
        cyclic.setFrequencyDivisionId ("sixteenth");
        cyclic.setWaveShape (core::sequencing::CyclicWaveShape::sine);
        cyclic.setWavePolarityMode (core::sequencing::CyclicWavePolarityMode::positiveOscillator);
        lane->addCyclicClip (cyclic);
    }

    clip.setExpressionState (expression);
    track->addClip (clip);
}

void paintPianoRoll (ui::PianoRollComponent& pianoRoll, const std::string& label)
{
    juce::Image image { juce::Image::ARGB, 1280, 720, true };
    juce::Graphics graphics { image };
    core::diagnostics::ScopedPerformanceTimer timer { label };
    pianoRoll.paintEntireComponent (graphics, true);
}
}

TEST_CASE ("Expression Mode UI shell shows lanes and does not mutate notes when toggled", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");

    const auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    const auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    REQUIRE (clip->notes().size() == 1);

    const auto laneIds = pianoRoll.expressionLaneIds();
    REQUIRE (laneIds.size() == 2);
    CHECK (laneIds[0] == "expr-volume");
    CHECK (laneIds[1] == "expr-pitch");
    REQUIRE (pianoRoll.selectedExpressionLaneId().has_value());
    CHECK (*pianoRoll.selectedExpressionLaneId() == "expr-volume");

    pianoRoll.setExpressionModeEnabled (true);
    CHECK (pianoRoll.expressionModeEnabled());
    CHECK (clip->notes().size() == 1);

    juce::Image image { juce::Image::ARGB, 1280, 720, true };
    juce::Graphics graphics { image };
    pianoRoll.paintEntireComponent (graphics, true);
    CHECK (clip->notes().size() == 1);

    pianoRoll.setExpressionModeEnabled (false);
    CHECK_FALSE (pianoRoll.expressionModeEnabled());
    CHECK (clip->notes().size() == 1);
}

TEST_CASE ("Expression Mode UI shell creates and renames lanes through undoable commands", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugCreateExpressionLane());
    auto laneIds = pianoRoll.expressionLaneIds();
    REQUIRE (laneIds.size() == 3);
    REQUIRE (pianoRoll.selectedExpressionLaneId().has_value());
    CHECK (*pianoRoll.selectedExpressionLaneId() == laneIds.back());

    REQUIRE (pianoRoll.debugRenameSelectedExpressionLane ("Gesture"));
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    const auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionLaneId { laneIds.back() });
    REQUIRE (lane != nullptr);
    CHECK (lane->name() == "Gesture");

    REQUIRE (services.commandStack().undo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionLaneId { laneIds.back() });
    REQUIRE (lane != nullptr);
    CHECK (lane->name() != "Gesture");

    REQUIRE (services.commandStack().undo().succeeded());
    laneIds = pianoRoll.expressionLaneIds();
    CHECK (laneIds.size() == 2);
}

TEST_CASE ("Expression Mode overlay paints empty phrase and dense cyclic lanes", "[integration][expression][ui][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    {
        app::AppServices services;
        addExpressionOverlayClip (services, "empty-expression", 0, false);
        ui::PianoRollComponent pianoRoll { services };
        pianoRoll.setBounds (0, 0, 1280, 720);
        pianoRoll.openClip ("track-1", "empty-expression");
        paintPianoRoll (pianoRoll, "ExpressionModeOverlayPerf::paint no-expression-mode empty");
        pianoRoll.setExpressionModeEnabled (true);
        paintPianoRoll (pianoRoll, "ExpressionModeOverlayPerf::paint expression-mode empty");
        CHECK (pianoRoll.expressionModeEnabled());
    }

    {
        app::AppServices services;
        addExpressionOverlayClip (services, "phrase-expression", 10, false);
        ui::PianoRollComponent pianoRoll { services };
        pianoRoll.setBounds (0, 0, 1280, 720);
        pianoRoll.openClip ("track-1", "phrase-expression");
        pianoRoll.setExpressionModeEnabled (true);
        paintPianoRoll (pianoRoll, "ExpressionModeOverlayPerf::paint phrase envelopes");
        CHECK (pianoRoll.expressionLaneIds().size() == 2);
    }

    {
        app::AppServices services;
        addExpressionOverlayClip (services, "dense-cyclic-expression", 0, true);
        ui::PianoRollComponent pianoRoll { services };
        pianoRoll.setBounds (0, 0, 1280, 720);
        pianoRoll.openClip ("track-1", "dense-cyclic-expression");
        pianoRoll.setExpressionModeEnabled (true);
        paintPianoRoll (pianoRoll, "ExpressionModeOverlayPerf::paint dense cyclic cold");
        paintPianoRoll (pianoRoll, "ExpressionModeOverlayPerf::paint dense cyclic warm");
        CHECK (pianoRoll.expressionModeEnabled());
    }
}

TEST_CASE ("Expression Mode release ghosts are opt-in and selectable by marquee", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addSimpleOscClipWithReleaseTail (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "release-clip");
    pianoRoll.setExpressionModeEnabled (true);

    CHECK_FALSE (pianoRoll.expressionReleaseModeEnabled());
    CHECK_FALSE (pianoRoll.debugEmulateMarqueeSelectFirstReleaseGhost());
    CHECK_FALSE (pianoRoll.hasSelectedNotes());

    pianoRoll.setExpressionReleaseModeEnabled (true);
    CHECK (pianoRoll.expressionReleaseModeEnabled());
    CHECK (pianoRoll.debugEmulateMarqueeSelectFirstReleaseGhost());
    CHECK (pianoRoll.hasSelectedNotes());
}

TEST_CASE ("Expression Mode phrase envelope keyboard edits are undoable and notes do not pitch edit", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* note = clip->findNoteById ("note-1");
    REQUIRE (note != nullptr);
    const auto originalPitch = note->pitch();
    const auto originalStart = note->startInClip();

    CHECK (pianoRoll.debugExpressionKeyPress ('a', 0));
    CHECK (clip->expressionState()
               .findLane (core::sequencing::ExpressionState::defaultVolumeLaneId())
               ->phraseEnvelopeClips()
               .empty());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->routes().size() == 1);
    CHECK (lane->routes().front().destination().kind == core::sequencing::ExpressionDestinationKind::trackVolume);
    REQUIRE (lane->phraseEnvelopeClips().size() == 1);
    CHECK (lane->phraseEnvelopeClips().front().attackStage().duration == core::time::sixteenthNoteDuration());
    CHECK (lane->phraseEnvelopeClips().front().decayStage().has_value());
    CHECK (lane->phraseEnvelopeClips().front().releaseStage().has_value());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('d', juce::KeyPress::rightKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 1);
    CHECK (lane->phraseEnvelopeClips().front().decayStage().has_value());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('c', juce::KeyPress::upKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 1);
    REQUIRE (lane->phraseEnvelopeClips().front().decayStage().has_value());
    CHECK (lane->phraseEnvelopeClips().front().decayStage()->curveShape == core::sequencing::ExpressionCurveShape::logarithmic);

    CHECK (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::upKey));
    note = clip->findNoteById ("note-1");
    REQUIRE (note != nullptr);
    CHECK (note->pitch() == originalPitch);
    CHECK (note->startInClip() == originalStart);

    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::deleteKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->phraseEnvelopeClips().empty());

    REQUIRE (services.commandStack().undo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->phraseEnvelopeClips().size() == 1);
}

TEST_CASE ("Expression Mode phrase envelope can be created from decay and release shortcuts", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    {
        app::AppServices services;
        addClipWithNote (services);

        ui::PianoRollComponent pianoRoll { services };
        pianoRoll.setBounds (0, 0, 1280, 720);
        pianoRoll.openClip ("track-1", "clip-1");
        pianoRoll.setExpressionModeEnabled (true);

        REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
        REQUIRE (pianoRoll.debugExpressionKeyPress ('d', juce::KeyPress::downKey));

        auto* track = services.project().findTrackById ("track-1");
        REQUIRE (track != nullptr);
        auto* clip = track->findClipById ("clip-1");
        REQUIRE (clip != nullptr);
        auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
        REQUIRE (lane != nullptr);
        REQUIRE (lane->phraseEnvelopeClips().size() == 1);
        const auto& envelope = lane->phraseEnvelopeClips().front();
        REQUIRE (envelope.decayStage().has_value());
        REQUIRE (envelope.peakLevel().has_value());
        REQUIRE (envelope.sustainLevel().has_value());
        CHECK (*envelope.peakLevel() == Catch::Approx (0.65));
        CHECK (*envelope.sustainLevel() == Catch::Approx (0.45));
    }

    {
        app::AppServices services;
        addClipWithNote (services);

        ui::PianoRollComponent pianoRoll { services };
        pianoRoll.setBounds (0, 0, 1280, 720);
        pianoRoll.openClip ("track-1", "clip-1");
        pianoRoll.setExpressionModeEnabled (true);

        REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
        REQUIRE (pianoRoll.debugExpressionKeyPress ('r', juce::KeyPress::rightKey));

        auto* track = services.project().findTrackById ("track-1");
        REQUIRE (track != nullptr);
        auto* clip = track->findClipById ("clip-1");
        REQUIRE (clip != nullptr);
        auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
        REQUIRE (lane != nullptr);
        REQUIRE (lane->phraseEnvelopeClips().size() == 1);
        REQUIRE (lane->phraseEnvelopeClips().front().releaseStage().has_value());
        CHECK (lane->phraseEnvelopeClips().front().releaseStage()->duration == core::time::sixteenthNoteDuration());
    }
}

TEST_CASE ("Expression Mode phrase envelope creation follows the current note selection", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithSlurRun (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-run");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-1" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("slur-run");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 1);
    CHECK (lane->phraseEnvelopeClips().front().sourceNoteIds() == std::vector<std::string> { "run-note-1" });

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-2" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));

    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 2);

    const auto firstPhrase = std::find_if (lane->phraseEnvelopeClips().begin(),
                                           lane->phraseEnvelopeClips().end(),
                                           [] (const auto& envelope)
                                           {
                                               return envelope.sourceNoteIds() == std::vector<std::string> { "run-note-1" };
                                           });
    const auto secondPhrase = std::find_if (lane->phraseEnvelopeClips().begin(),
                                            lane->phraseEnvelopeClips().end(),
                                            [] (const auto& envelope)
                                            {
                                                return envelope.sourceNoteIds() == std::vector<std::string> { "run-note-2" };
                                            });
    CHECK (firstPhrase != lane->phraseEnvelopeClips().end());
    CHECK (secondPhrase != lane->phraseEnvelopeClips().end());
}

TEST_CASE ("Expression Mode note selection restores existing phrase and cyclic controls", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithSlurRun (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-run");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-1" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey, true));

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-2" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey, true));

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("slur-run");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 2);
    REQUIRE (lane->cyclicClips().size() == 2);

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-1" }));
    CHECK (pianoRoll.debugPhraseEnvelopeControlsVisible());
    CHECK_FALSE (pianoRoll.debugCyclicExpressionControlsVisible());

    REQUIRE (pianoRoll.debugShowCyclicExpressionControls());
    CHECK (pianoRoll.debugCyclicExpressionControlsVisible());
    CHECK_FALSE (pianoRoll.debugPhraseEnvelopeControlsVisible());

    REQUIRE (pianoRoll.debugShowPhraseEnvelopeControls());
    CHECK (pianoRoll.debugPhraseEnvelopeControlsVisible());
    CHECK_FALSE (pianoRoll.debugCyclicExpressionControlsVisible());
}

TEST_CASE ("Expression Mode allows the same notes to drive phrases in different lanes", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "note-1" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* volumeLane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    REQUIRE (volumeLane->phraseEnvelopeClips().size() == 1);
    CHECK (volumeLane->phraseEnvelopeClips().front().sourceNoteIds() == std::vector<std::string> { "note-1" });

    REQUIRE (pianoRoll.debugCreateExpressionLane());
    REQUIRE (pianoRoll.selectedExpressionLaneId().has_value());
    const auto secondLaneId = *pianoRoll.selectedExpressionLaneId();
    CHECK_FALSE (pianoRoll.debugPhraseEnvelopeControlsVisible());

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "note-1" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));

    volumeLane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    auto* secondLane = clip->expressionState().findLane (core::sequencing::ExpressionLaneId { secondLaneId });
    REQUIRE (volumeLane != nullptr);
    REQUIRE (secondLane != nullptr);
    REQUIRE (volumeLane->phraseEnvelopeClips().size() == 1);
    REQUIRE (secondLane->phraseEnvelopeClips().size() == 1);
    CHECK (secondLane->phraseEnvelopeClips().front().sourceNoteIds() == std::vector<std::string> { "note-1" });
    CHECK (pianoRoll.debugPhraseEnvelopeControlsVisible());
}

TEST_CASE ("Expression Mode expanded phrase envelope controls select and commit one slider gesture", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey));
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Add Phrase Envelope");
    REQUIRE (pianoRoll.debugSelectFirstPhraseEnvelope());
    pianoRoll.resized();

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 1);
    CHECK (pianoRoll.debugPhraseEnvelopeControlsVisible());
    CHECK (pianoRoll.debugPhraseEnvelopeDecayControlsVisible());
    CHECK (pianoRoll.debugPhraseEnvelopeReleaseControlsVisible());

    const auto undoDepthBeforeGesture = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugSetPhraseEnvelopeAttackStartGesture ({ 0.20, 0.35, 0.50 }));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeGesture + 1);

    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 1);
    CHECK (lane->phraseEnvelopeClips().front().attackStage().startLevel == Catch::Approx (0.50));

    REQUIRE (services.commandStack().undo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->phraseEnvelopeClips().size() == 1);
    CHECK (lane->phraseEnvelopeClips().front().attackStage().startLevel == Catch::Approx (0.0));
}

TEST_CASE ("Expression Mode routing UI adds edits inverts and removes routes", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    CHECK (pianoRoll.debugExpressionRouteCount() == 0);
    REQUIRE (pianoRoll.debugAddFirstAvailableExpressionRoute());
    CHECK (pianoRoll.debugExpressionRouteCount() == 1);
    CHECK (pianoRoll.debugExpressionRouteControlsVisible());

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->routes().size() == 1);
    CHECK (lane->routes().front().destination().kind == core::sequencing::ExpressionDestinationKind::trackVolume);

    REQUIRE (pianoRoll.debugSetFirstExpressionRouteRange (0.85, 0.15));
    CHECK (pianoRoll.debugExpressionRouteControlsVisible());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->routes().size() == 1);
    CHECK (lane->routes().front().outputMin() == Catch::Approx (0.85));
    CHECK (lane->routes().front().outputMax() == Catch::Approx (0.15));
    CHECK (lane->routes().front().mapLaneValue (0.0, lane->polarity()) == Catch::Approx (0.85));
    CHECK (lane->routes().front().mapLaneValue (1.0, lane->polarity()) == Catch::Approx (0.15));

    REQUIRE (pianoRoll.debugToggleFirstExpressionRouteEnabled());
    CHECK (pianoRoll.debugExpressionRouteControlsVisible());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->routes().size() == 1);
    CHECK_FALSE (lane->routes().front().enabled());

    REQUIRE (services.commandStack().undo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->routes().size() == 1);
    CHECK (lane->routes().front().enabled());

    REQUIRE (pianoRoll.debugRemoveFirstExpressionRoute());
    CHECK (pianoRoll.debugExpressionRouteCount() == 0);
}

TEST_CASE ("Expression Mode routing UI labels playback export stored and unavailable routes", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugAddExpressionRouteByStableId (
        core::sequencing::ExpressionDestination::trackVolume ("track-1").stableId()));
    REQUIRE (pianoRoll.debugAddExpressionRouteByStableId (
        core::sequencing::ExpressionDestination::midiCc ("track-1", 74).stableId()));
    REQUIRE (pianoRoll.debugAddExpressionRouteByStableId (
        core::sequencing::ExpressionDestination::pitchBend ("track-1").stableId()));

    REQUIRE (services.addExpressionRoute (
        "track-1",
        "clip-1",
        core::sequencing::ExpressionState::defaultVolumeLaneId(),
        core::sequencing::ExpressionRoute {
            core::sequencing::ExpressionRouteId { "route-missing" },
            core::sequencing::ExpressionDestination::midiCc ("missing-track", 11),
            0.0,
            127.0
        }));

    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    const auto labels = pianoRoll.debugExpressionRouteSupportLabels();
    REQUIRE (labels.size() == 4);
    CHECK (std::find (labels.begin(), labels.end(), "Playback") != labels.end());
    CHECK (std::find (labels.begin(), labels.end(), "Export only") != labels.end());
    CHECK (std::find (labels.begin(), labels.end(), "Stored only") != labels.end());
    CHECK (std::find (labels.begin(), labels.end(), "Unavailable") != labels.end());
}

TEST_CASE ("Expression Mode routing UI can target Simple Osc Complex parameters", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addSimpleOscClipWithReleaseTail (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "release-clip");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugAddSimpleOscExpressionRoute ("wavefolder.amount"));

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("release-clip");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->routes().size() == 1);
    CHECK (lane->routes().front().destination().kind == core::sequencing::ExpressionDestinationKind::firstPartyParameter);
    CHECK (lane->routes().front().destination().parameterId == "wavefolder.amount");
}

TEST_CASE ("Expression Mode cyclic keyboard edits create shape and delete cyclic clips", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    CHECK (pianoRoll.debugCyclicExpressionCount() == 0);

    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey, true));
    CHECK (pianoRoll.debugCyclicExpressionCount() == 1);
    CHECK (pianoRoll.debugCyclicExpressionControlsVisible());
    CHECK (pianoRoll.debugCyclicExpressionWaveformCount() == 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Add Cyclic Expression");

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->routes().size() == 1);
    CHECK (lane->routes().front().destination().kind == core::sequencing::ExpressionDestinationKind::trackVolume);
    REQUIRE (lane->cyclicClips().size() == 1);
    CHECK (lane->cyclicClips().front().attackTime() == core::time::sixteenthNoteDuration());
    CHECK (lane->cyclicClips().front().maxAmplitude() == Catch::Approx (0.25));
    CHECK (lane->cyclicClips().front().releaseTime() == core::time::sixteenthNoteDuration());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('d', juce::KeyPress::upKey, true));
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Replace Cyclic Expression");
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->cyclicClips().size() == 1);
    CHECK (lane->cyclicClips().front().maxAmplitude() == Catch::Approx (0.30));

    REQUIRE (pianoRoll.debugExpressionKeyPress ('r', juce::KeyPress::leftKey, true));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->cyclicClips().size() == 1);
    CHECK (lane->cyclicClips().front().releaseTime() == core::time::eighthNoteDuration());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('f', juce::KeyPress::rightKey, true));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->cyclicClips().size() == 1);
    CHECK_FALSE (lane->cyclicClips().front().frequencyDivisionId().empty());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('c', juce::KeyPress::upKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->cyclicClips().size() == 1);
    CHECK (lane->cyclicClips().front().waveShape() == core::sequencing::CyclicWaveShape::triangle);

    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::deleteKey));
    CHECK (pianoRoll.debugCyclicExpressionCount() == 0);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Remove Cyclic Expression");

    REQUIRE (services.commandStack().undo().succeeded());
    REQUIRE (services.commandStack().nextRedoName().has_value());
    CHECK (*services.commandStack().nextRedoName() == "Remove Cyclic Expression");
    CHECK (pianoRoll.debugCyclicExpressionCount() == 1);
}

TEST_CASE ("Expression Mode cyclic keyboard edits honor shifted prefixes after shift is released", "[integration][expression][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', 0, true));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::rightKey));

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->phraseEnvelopeClips().empty());
    REQUIRE (lane->routes().size() == 1);
    CHECK (lane->routes().front().destination().kind == core::sequencing::ExpressionDestinationKind::trackVolume);
    REQUIRE (lane->cyclicClips().size() == 1);
    CHECK (pianoRoll.debugCyclicExpressionControlsVisible());
    CHECK (pianoRoll.debugCyclicExpressionWaveformCount() == 1);
    CHECK (lane->cyclicClips().front().attackTime() == core::time::sixteenthNoteDuration());
    CHECK (lane->cyclicClips().front().maxAmplitude() == Catch::Approx (0.25));
    CHECK (lane->cyclicClips().front().releaseTime() == core::time::sixteenthNoteDuration());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('d', 0, true));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::upKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->cyclicClips().size() == 1);
    CHECK (lane->cyclicClips().front().maxAmplitude() == Catch::Approx (0.30));
}

TEST_CASE ("Expression Mode S creates and deletes a pitch slur between two selected notes", "[integration][expression][ui][slur]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithSlurPair (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-pair");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    const auto undoDepthBeforeCreate = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeCreate + 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Add Pitch Slurs");
    CHECK (pianoRoll.debugPitchSlurCount() == 1);

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("slur-pair");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->pitchSlurs().size() == 1);
    CHECK (lane->pitchSlurs().front().sourceNoteId() == "note-1");
    CHECK (lane->pitchSlurs().front().destinationNoteId() == "note-2");
    CHECK (lane->pitchSlurs().front().legatoNoRetrigger());
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == 0);

    const auto zeroTimePoints = pianoRoll.debugFirstPitchSlurTrajectoryPoints();
    REQUIRE (zeroTimePoints.size() == 4);
    CHECK (zeroTimePoints[1].x > zeroTimePoints[0].x);
    CHECK (zeroTimePoints[1].y == Catch::Approx (zeroTimePoints[0].y));
    CHECK (zeroTimePoints[2].x == Catch::Approx (zeroTimePoints[1].x));
    CHECK (zeroTimePoints[2].y == Catch::Approx (zeroTimePoints[3].y));
    CHECK (zeroTimePoints[3].x > zeroTimePoints[2].x);

    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::rightKey));
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == core::time::sixteenthNoteDuration().ticks());

    const auto glidePoints = pianoRoll.debugFirstPitchSlurTrajectoryPoints();
    REQUIRE (glidePoints.size() > 4);
    CHECK (glidePoints[0].y == Catch::Approx (zeroTimePoints[0].y));
    CHECK (glidePoints[1].x == Catch::Approx (zeroTimePoints[1].x));
    CHECK (glidePoints[1].y == Catch::Approx (zeroTimePoints[0].y));
    CHECK (glidePoints[2].x > glidePoints[1].x);
    CHECK (glidePoints.back().y == Catch::Approx (zeroTimePoints.back().y));

    const auto undoDepthBeforeDelete = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::deleteKey));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeDelete + 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Remove Pitch Slur");
    CHECK (pianoRoll.debugPitchSlurCount() == 0);

    REQUIRE (services.commandStack().undo().succeeded());
    REQUIRE (services.commandStack().nextRedoName().has_value());
    CHECK (*services.commandStack().nextRedoName() == "Remove Pitch Slur");
    CHECK (pianoRoll.debugPitchSlurCount() == 1);
}

TEST_CASE ("Expression Mode S can repeatedly create and reselect pitch slurs from marquee-style note selections", "[integration][expression][ui][slur]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithSlurRun (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-run");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-1", "run-note-2" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (pianoRoll.debugPitchSlurCount() == 1);
    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::rightKey));
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == core::time::sixteenthNoteDuration().ticks());

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-2", "run-note-3" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (pianoRoll.debugPitchSlurCount() == 2);
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == 0);

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("slur-run");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->pitchSlurs().size() == 2);

    const auto slurTimeForPair = [&] (const std::string& source, const std::string& destination)
        -> std::optional<core::time::TickDuration>
    {
        const auto match = std::find_if (lane->pitchSlurs().begin(), lane->pitchSlurs().end(), [&] (const auto& slur)
        {
            return slur.sourceNoteId() == source && slur.destinationNoteId() == destination;
        });
        if (match == lane->pitchSlurs().end())
            return std::nullopt;
        return match->slurTime();
    };

    REQUIRE (slurTimeForPair ("run-note-1", "run-note-2").has_value());
    CHECK (*slurTimeForPair ("run-note-1", "run-note-2") == core::time::sixteenthNoteDuration());
    REQUIRE (slurTimeForPair ("run-note-2", "run-note-3").has_value());
    CHECK (*slurTimeForPair ("run-note-2", "run-note-3") == core::time::TickDuration {});

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "run-note-1", "run-note-2" }));
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == core::time::sixteenthNoteDuration().ticks());
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (pianoRoll.debugPitchSlurCount() == 2);
    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::rightKey));

    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (slurTimeForPair ("run-note-1", "run-note-2").has_value());
    CHECK (*slurTimeForPair ("run-note-1", "run-note-2")
           == core::time::TickDuration::fromTicks (core::time::sixteenthNoteDuration().ticks() * 2));
    REQUIRE (slurTimeForPair ("run-note-2", "run-note-3").has_value());
    CHECK (*slurTimeForPair ("run-note-2", "run-note-3") == core::time::TickDuration {});
}

TEST_CASE ("Expression Mode slurs stay inactive while another lane is selected", "[integration][expression][ui][slur]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithSlurPair (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-pair");
    pianoRoll.setExpressionModeEnabled (true);
    REQUIRE (pianoRoll.debugSelectPitchExpressionLane());

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "note-1", "note-2" }));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (pianoRoll.debugPitchSlurCount() == 1);
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == 0);

    REQUIRE (pianoRoll.debugSelectExpressionLane ("expr-volume"));
    REQUIRE (pianoRoll.selectedExpressionLaneId().has_value());
    CHECK (*pianoRoll.selectedExpressionLaneId() == "expr-volume");

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "note-1", "note-2" }));
    REQUIRE (pianoRoll.selectedExpressionLaneId().has_value());
    CHECK (*pianoRoll.selectedExpressionLaneId() == "expr-volume");

    CHECK (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::rightKey));
    CHECK (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::deleteKey));
    CHECK (pianoRoll.debugPitchSlurCount() == 1);

    REQUIRE (pianoRoll.debugSelectPitchExpressionLane());
    REQUIRE (pianoRoll.debugSelectNoteIds ({ "note-1", "note-2" }));
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == 0);
}

TEST_CASE ("Expression Mode S creates register paired chord slur blocks with shared edits and voice overrides", "[integration][expression][ui][slur]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithSlurChords (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-chords");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    const auto undoDepthBeforeCreate = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeCreate + 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Add Pitch Slurs");
    CHECK (pianoRoll.debugPitchSlurCount() == 3);

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("slur-chords");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->pitchSlurs().size() == 3);
    REQUIRE (lane->pitchSlurs().front().blockId().has_value());
    const auto blockId = *lane->pitchSlurs().front().blockId();
    for (const auto& slur : lane->pitchSlurs())
    {
        CHECK (slur.blockId().has_value());
        CHECK (*slur.blockId() == blockId);
        CHECK_FALSE (slur.hasVoiceOverride());
    }

    const auto undoDepthBeforeSharedEdit = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', juce::KeyPress::rightKey));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeSharedEdit + 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Replace Pitch Slurs");
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    for (const auto& slur : lane->pitchSlurs())
        CHECK (slur.slurTime() == core::time::sixteenthNoteDuration());

    REQUIRE (services.commandStack().undo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    for (const auto& slur : lane->pitchSlurs())
        CHECK (slur.slurTime() == core::time::TickDuration {});

    REQUIRE (services.commandStack().redo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    for (const auto& slur : lane->pitchSlurs())
        CHECK (slur.slurTime() == core::time::sixteenthNoteDuration());

    const auto slurForPair = [&] (const std::string& source, const std::string& destination)
        -> const core::sequencing::PitchSlur*
    {
        const auto* currentLane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
        if (currentLane == nullptr)
            return nullptr;

        const auto match = std::find_if (currentLane->pitchSlurs().begin(),
                                         currentLane->pitchSlurs().end(),
                                         [&] (const auto& slur)
                                         {
                                             return slur.sourceNoteId() == source && slur.destinationNoteId() == destination;
                                         });
        return match == currentLane->pitchSlurs().end() ? nullptr : &*match;
    };

    REQUIRE (pianoRoll.debugSelectNoteIds ({ "source-1", "destination-1" }));
    REQUIRE (pianoRoll.debugSelectedPitchSlurTimeTicks().has_value());
    CHECK (*pianoRoll.debugSelectedPitchSlurTimeTicks() == core::time::sixteenthNoteDuration().ticks());
    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::rightKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->pitchSlurs().size() == 3);
    const auto* editedVoice = slurForPair ("source-1", "destination-1");
    const auto* untouchedVoice2 = slurForPair ("source-2", "destination-2");
    const auto* untouchedVoice3 = slurForPair ("source-3", "destination-3");
    REQUIRE (editedVoice != nullptr);
    REQUIRE (untouchedVoice2 != nullptr);
    REQUIRE (untouchedVoice3 != nullptr);
    CHECK (editedVoice->hasVoiceOverride());
    CHECK (editedVoice->slurTime() == core::time::TickDuration::fromTicks (core::time::sixteenthNoteDuration().ticks() * 2));
    CHECK (untouchedVoice2->slurTime() == core::time::sixteenthNoteDuration());
    CHECK (untouchedVoice3->slurTime() == core::time::sixteenthNoteDuration());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('c', juce::KeyPress::upKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    editedVoice = slurForPair ("source-1", "destination-1");
    untouchedVoice2 = slurForPair ("source-2", "destination-2");
    untouchedVoice3 = slurForPair ("source-3", "destination-3");
    REQUIRE (editedVoice != nullptr);
    REQUIRE (untouchedVoice2 != nullptr);
    REQUIRE (untouchedVoice3 != nullptr);
    CHECK (editedVoice->curveShape() == core::sequencing::ExpressionCurveShape::logarithmic);
    CHECK (untouchedVoice2->curveShape() == core::sequencing::ExpressionCurveShape::linear);
    CHECK (untouchedVoice3->curveShape() == core::sequencing::ExpressionCurveShape::linear);
}

TEST_CASE ("Expression Mode S creates adjacent slur blocks across selected chord runs", "[integration][expression][ui][slur]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithThreeSlurChords (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-three-chords");
    pianoRoll.setExpressionModeEnabled (true);

    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (pianoRoll.debugPitchSlurCount() == 6);

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("slur-three-chords");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->pitchSlurs().size() == 6);

    auto firstBlockCount = 0;
    auto secondBlockCount = 0;
    for (const auto& slur : lane->pitchSlurs())
    {
        CHECK (slur.legatoNoRetrigger());
        REQUIRE (slur.blockId().has_value());

        if (slur.sourceNoteId().find ("chord-1-") == 0 && slur.destinationNoteId().find ("chord-2-") == 0)
            ++firstBlockCount;
        if (slur.sourceNoteId().find ("chord-2-") == 0 && slur.destinationNoteId().find ("chord-3-") == 0)
            ++secondBlockCount;
    }

    CHECK (firstBlockCount == 3);
    CHECK (secondBlockCount == 3);
}

TEST_CASE ("Expression Mode pitch lane shifted attack creates vibrato", "[integration][expression][ui][vibrato]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);
    REQUIRE (pianoRoll.debugSelectPitchExpressionLane());
    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey, true));
    CHECK (pianoRoll.debugVibratoExpressionCount() == 1);
    CHECK (pianoRoll.debugCyclicExpressionControlsVisible());
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Add Vibrato Expression");

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->cyclicClips().empty());
    REQUIRE (lane->vibratoExpressions().size() == 1);
    CHECK (lane->vibratoExpressions().front().attackTime() == core::time::sixteenthNoteDuration());
    CHECK (lane->vibratoExpressions().front().releaseTime() == core::time::sixteenthNoteDuration());
    CHECK (lane->vibratoExpressions().front().amplitudeSemitones() == Catch::Approx (0.05));
    CHECK_FALSE (lane->vibratoExpressions().front().frequencyDivisionId().empty());
}

TEST_CASE ("Expression Mode pitch lane vibrato keyboard edits create shape and delete vibrato", "[integration][expression][ui][vibrato]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithNote (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "clip-1");
    pianoRoll.setExpressionModeEnabled (true);
    REQUIRE (pianoRoll.debugSelectPitchExpressionLane());
    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());

    const auto undoDepthBeforeCreate = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('d', juce::KeyPress::upKey, true));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeCreate + 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Add Vibrato Expression");
    CHECK (pianoRoll.debugVibratoExpressionCount() == 1);

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("clip-1");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->vibratoExpressions().size() == 1);
    CHECK (lane->vibratoExpressions().front().amplitudeSemitones() == Catch::Approx (0.05));

    const auto undoDepthBeforeAmplitudeEdit = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('d', juce::KeyPress::upKey, true));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeAmplitudeEdit + 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Replace Vibrato Expression");
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->vibratoExpressions().front().amplitudeSemitones() == Catch::Approx (0.10));

    REQUIRE (services.commandStack().undo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->vibratoExpressions().front().amplitudeSemitones() == Catch::Approx (0.05));

    REQUIRE (services.commandStack().redo().succeeded());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->vibratoExpressions().front().amplitudeSemitones() == Catch::Approx (0.10));

    REQUIRE (pianoRoll.debugExpressionKeyPress ('a', juce::KeyPress::rightKey, true));
    REQUIRE (pianoRoll.debugExpressionKeyPress ('r', juce::KeyPress::leftKey, true));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->vibratoExpressions().front().attackTime() == core::time::sixteenthNoteDuration());
    CHECK (lane->vibratoExpressions().front().releaseTime() == core::time::eighthNoteDuration());

    const auto previousDivision = lane->vibratoExpressions().front().frequencyDivisionId();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('f', juce::KeyPress::rightKey, true));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    CHECK_FALSE (lane->vibratoExpressions().front().frequencyDivisionId().empty());
    CHECK (lane->vibratoExpressions().front().frequencyDivisionId() != previousDivision);

    REQUIRE (pianoRoll.debugExpressionKeyPress ('c', juce::KeyPress::upKey));
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    CHECK (lane->vibratoExpressions().front().waveShape() == core::sequencing::CyclicWaveShape::triangle);

    const auto undoDepthBeforeDelete = services.commandStack().undoDepth();
    REQUIRE (pianoRoll.debugExpressionKeyPress ('\0', juce::KeyPress::deleteKey));
    CHECK (services.commandStack().undoDepth() == undoDepthBeforeDelete + 1);
    REQUIRE (services.commandStack().nextUndoName().has_value());
    CHECK (*services.commandStack().nextUndoName() == "Remove Vibrato Expression");
    CHECK (pianoRoll.debugVibratoExpressionCount() == 0);

    REQUIRE (services.commandStack().undo().succeeded());
    REQUIRE (services.commandStack().nextRedoName().has_value());
    CHECK (*services.commandStack().nextRedoName() == "Remove Vibrato Expression");
    CHECK (pianoRoll.debugVibratoExpressionCount() == 1);
}

TEST_CASE ("Expression Mode pitch lane vibrato shares phase and supports per voice overrides", "[integration][expression][ui][vibrato]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    addClipWithSlurChords (services);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "slur-chords");
    pianoRoll.setExpressionModeEnabled (true);
    REQUIRE (pianoRoll.debugSelectPitchExpressionLane());
    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());

    REQUIRE (pianoRoll.debugExpressionKeyPress ('d', juce::KeyPress::upKey, true));
    CHECK (pianoRoll.debugVibratoExpressionCount() == 1);

    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);
    auto* clip = track->findClipById ("slur-chords");
    REQUIRE (clip != nullptr);
    auto* lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->vibratoExpressions().size() == 1);
    const auto& vibrato = lane->vibratoExpressions().front();
    CHECK (vibrato.sourceNoteIds().size() == 6);
    CHECK (vibrato.phase() == Catch::Approx (0.0));
    CHECK (vibrato.blockId().has_value());
    CHECK (vibrato.voiceOverrides().empty());

    REQUIRE (pianoRoll.debugApplyFirstVibratoVoiceOverride());
    lane = clip->expressionState().findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (lane != nullptr);
    REQUIRE (lane->vibratoExpressions().front().voiceOverrides().size() == 1);
    CHECK (lane->vibratoExpressions().front().voiceOverrides().front().phase == Catch::Approx (0.25));
}
