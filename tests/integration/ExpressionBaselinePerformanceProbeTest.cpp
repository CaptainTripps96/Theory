#include "app/AppServices.h"
#include "core/commands/AddClipCommand.h"
#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/diagnostics/PerformanceTrace.h"
#include "core/music_theory/MidiPitch.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Region.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/Tick.h"
#include "engine/TracktionPlaybackEngine.h"
#include "ui/PianoRollComponent.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace tsq;

void pumpMessagesFor (int milliseconds)
{
    auto* manager = juce::MessageManager::getInstance();
    REQUIRE (manager != nullptr);
    manager->runDispatchLoopUntil (milliseconds);
}

core::time::TickDuration beats (std::int64_t value)
{
    return core::time::TickDuration::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::time::TickPosition sixteenth (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote / 4);
}

core::time::TickDuration eighthDuration()
{
    return core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2);
}

core::time::TickPosition beatPosition (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::sequencing::MidiClip denseExpressionBaselineClip (std::string clipId,
                                                        std::size_t noteCount,
                                                        std::int64_t lengthBeats = 64)
{
    core::sequencing::MidiClip clip {
        std::move (clipId),
        "Expression Baseline Clip",
        core::time::TickPosition {},
        beats (lengthBeats)
    };

    const auto usableSixteenthSlots = (lengthBeats * 4) - 2;
    for (std::size_t index = 0; index < noteCount; ++index)
    {
        const auto startSlot = static_cast<std::int64_t> (index % static_cast<std::size_t> (usableSixteenthSlots));
        const auto pitch = 42 + static_cast<int> ((index * 7) % 44);
        clip.addNote (core::sequencing::MidiNote {
            "expr-baseline-note-" + std::to_string (index + 1),
            core::music_theory::MidiPitch::fromValue (pitch),
            sixteenth (startSlot),
            eighthDuration(),
            100
        });
    }

    return clip;
}

core::sequencing::Project simpleOscProjectWithClip (core::sequencing::MidiClip clip)
{
    core::sequencing::Project project { "expression-simple-osc", "Expression Simple Osc" };
    core::sequencing::Track track { "track-1", "Simple Osc Track", core::sequencing::TrackType::midi };

    core::sequencing::DeviceChain chain;
    chain.appendSlot (core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { "simple-osc-complex" },
        core::devices::defaultFirstPartyDeviceState (core::devices::simpleOscComplexDefinition()),
        core::sequencing::PluginKind::instrument
    });
    track.setDeviceChain (std::move (chain));
    track.addClip (std::move (clip));
    project.addTrack (std::move (track));
    return project;
}

void paintAndSelectPianoRollBaseline (std::size_t noteCount)
{
    app::AppServices services;
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    const auto clipId = "expr-baseline-ui-" + std::to_string (noteCount);
    track->addClip (denseExpressionBaselineClip (clipId, noteCount));

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1440, 760);
    pianoRoll.openClip ("track-1", clipId);

    juce::Image image { juce::Image::ARGB, 1440, 760, true };
    juce::Graphics graphics { image };

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "ExpressionBaselinePerfProbe::piano-roll paint cold notes=" + std::to_string (noteCount)
        };
        pianoRoll.paintEntireComponent (graphics, true);
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "ExpressionBaselinePerfProbe::piano-roll paint warm notes=" + std::to_string (noteCount)
        };
        pianoRoll.paintEntireComponent (graphics, true);
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "ExpressionBaselinePerfProbe::piano-roll select-all notes=" + std::to_string (noteCount)
        };
        REQUIRE (pianoRoll.selectAllNotes());
    }

    {
        core::diagnostics::ScopedPerformanceTimer timer {
            "ExpressionBaselinePerfProbe::piano-roll marquee-visible notes=" + std::to_string (noteCount)
        };
        REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    }

    CHECK (pianoRoll.hasOpenClip());
    CHECK (pianoRoll.hasSelectedNotes());
}

core::sequencing::Project simpleOscSyncProject (std::size_t noteCount)
{
    return simpleOscProjectWithClip (denseExpressionBaselineClip ("expr-baseline-simple-osc-clip", noteCount));
}

core::sequencing::MidiClip simpleOscClipWithFirstPartyExpressionRoute()
{
    core::sequencing::MidiClip clip {
        "prepared-simple-osc-expression-clip",
        "Prepared Simple Osc Expression",
        core::time::TickPosition {},
        beats (4)
    };
    clip.addNote (core::sequencing::MidiNote {
        "expr-note-1",
        core::music_theory::MidiPitch::middleC(),
        core::time::TickPosition {},
        beats (2),
        100
    });

    core::sequencing::ExpressionState expression;
    core::sequencing::ExpressionLane lane {
        core::sequencing::ExpressionLaneId { "expr-fold" },
        "Fold",
        core::sequencing::ExpressionLanePolarity::unipolar
    };
    lane.addRoute (core::sequencing::ExpressionRoute {
        core::sequencing::ExpressionRouteId { "route-fold" },
        core::sequencing::ExpressionDestination::firstPartyParameter (
            "track-1",
            core::sequencing::DeviceSlotId { "simple-osc-complex" },
            "wavefolder.amount"),
        0.10,
        0.90
    });
    lane.addPhraseEnvelopeClip (core::sequencing::PhraseEnvelopeClip {
        core::sequencing::ExpressionClipId { "env-fold" },
        { "expr-note-1" },
        core::sequencing::Region { beatPosition (0), beatPosition (2) },
        0.0,
        core::sequencing::EnvelopeStage {
            core::sequencing::EnvelopeStageType::attack,
            beats (2),
            0.0,
            1.0
        }
    });
    expression.addLane (std::move (lane));
    clip.setExpressionState (std::move (expression));
    return clip;
}

core::sequencing::MidiClip simpleOscChordClipWithVolumeExpression()
{
    core::sequencing::MidiClip clip {
        "simple-osc-volume-expression-chords",
        "Simple Osc Volume Expression Chords",
        core::time::TickPosition {},
        beats (4)
    };

    const int chordPitches[][3] {
        { 60, 64, 67 },
        { 62, 65, 69 },
        { 64, 67, 71 }
    };
    std::vector<std::string> noteIds;
    for (auto chordIndex = 0; chordIndex < 3; ++chordIndex)
        for (auto voiceIndex = 0; voiceIndex < 3; ++voiceIndex)
        {
            auto noteId = "volume-chord-" + std::to_string (chordIndex + 1) + "-voice-" + std::to_string (voiceIndex + 1);
            noteIds.push_back (noteId);
            clip.addNote (core::sequencing::MidiNote {
                std::move (noteId),
                core::music_theory::MidiPitch::fromValue (chordPitches[chordIndex][voiceIndex]),
                beatPosition (chordIndex),
                beats (1),
                104
            });
        }

    auto expression = clip.expressionState();
    auto* volumeLane = expression.findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    volumeLane->addRoute (core::sequencing::ExpressionRoute {
        core::sequencing::ExpressionRouteId { "route-volume-expression-chords" },
        core::sequencing::ExpressionDestination::trackVolume ("track-1"),
        0.0,
        1.0
    });

    core::sequencing::PhraseEnvelopeClip envelope {
        core::sequencing::ExpressionClipId { "env-volume-expression-chords" },
        noteIds,
        core::sequencing::Region { beatPosition (0), beatPosition (3) },
        0.55,
        core::sequencing::EnvelopeStage {
            core::sequencing::EnvelopeStageType::attack,
            beats (1),
            0.10,
            0.82
        }
    };
    envelope.setDecayStage (core::sequencing::EnvelopeStage {
        core::sequencing::EnvelopeStageType::decay,
        beats (1),
        0.82,
        0.70
    });
    envelope.setPeakLevel (0.82, volumeLane->polarity());
    envelope.setSustainLevel (0.70, volumeLane->polarity());
    envelope.setReleaseStage (core::sequencing::EnvelopeStage {
        core::sequencing::EnvelopeStageType::release,
        beats (1),
        0.70,
        0.20
    });
    volumeLane->addPhraseEnvelopeClip (std::move (envelope));
    clip.setExpressionState (std::move (expression));
    return clip;
}

core::sequencing::MidiClip simpleOscSustainedClipWithRisingVolumeExpression()
{
    core::sequencing::MidiClip clip {
        "simple-osc-rising-volume-expression",
        "Simple Osc Rising Volume Expression",
        core::time::TickPosition {},
        beats (4)
    };

    clip.addNote (core::sequencing::MidiNote {
        "rising-volume-note",
        core::music_theory::MidiPitch::middleC(),
        core::time::TickPosition {},
        beats (4),
        110
    });

    auto expression = clip.expressionState();
    auto* volumeLane = expression.findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    volumeLane->addRoute (core::sequencing::ExpressionRoute {
        core::sequencing::ExpressionRouteId { "route-rising-volume" },
        core::sequencing::ExpressionDestination::trackVolume ("track-1"),
        0.0,
        1.0
    });
    volumeLane->addPhraseEnvelopeClip (core::sequencing::PhraseEnvelopeClip {
        core::sequencing::ExpressionClipId { "env-rising-volume" },
        { "rising-volume-note" },
        core::sequencing::Region { core::time::TickPosition {}, core::time::TickPosition {} + beats (4) },
        0.0,
        core::sequencing::EnvelopeStage {
            core::sequencing::EnvelopeStageType::attack,
            beats (4),
            0.0,
            1.0
        }
    });
    clip.setExpressionState (std::move (expression));
    return clip;
}

core::sequencing::MidiClip adjacentChordSlurChainClip (std::string clipId)
{
    core::sequencing::MidiClip clip {
        std::move (clipId),
        "Simple Osc Slur Chain",
        core::time::TickPosition {},
        beats (6)
    };

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
                beatPosition (chordIndex),
                beats (1),
                100
            });

    auto expression = clip.expressionState();
    auto* pitchLane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    for (auto voiceIndex = 0; voiceIndex < 3; ++voiceIndex)
    {
        core::sequencing::PitchSlur first {
            core::sequencing::ExpressionClipId { "slur-1-" + std::to_string (voiceIndex + 1) },
            "chord-1-voice-" + std::to_string (voiceIndex + 1),
            "chord-2-voice-" + std::to_string (voiceIndex + 1)
        };
        first.setLegatoNoRetrigger (true);
        first.setBlockId (core::sequencing::ExpressionBlockId { "slur-block-1" });
        pitchLane->addPitchSlur (first);

        core::sequencing::PitchSlur second {
            core::sequencing::ExpressionClipId { "slur-2-" + std::to_string (voiceIndex + 1) },
            "chord-2-voice-" + std::to_string (voiceIndex + 1),
            "chord-3-voice-" + std::to_string (voiceIndex + 1)
        };
        second.setLegatoNoRetrigger (true);
        second.setBlockId (core::sequencing::ExpressionBlockId { "slur-block-2" });
        pitchLane->addPitchSlur (second);
    }
    clip.setExpressionState (expression);
    return clip;
}
}

TEST_CASE ("Expression Mode baseline probes current piano-roll editing costs", "[integration][expression][baseline][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    for (const auto noteCount : std::array<std::size_t, 3> { 64, 512, 2048 })
        DYNAMIC_SECTION ("note count " << noteCount)
        {
            paintAndSelectPianoRollBaseline (noteCount);
        }
}

TEST_CASE ("Expression Mode baseline probes native synth playback sync", "[integration][expression][baseline][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    for (const auto noteCount : std::array<std::size_t, 3> { 64, 512, 2048 })
        DYNAMIC_SECTION ("note count " << noteCount)
        {
            const auto project = simpleOscSyncProject (noteCount);
            core::diagnostics::ScopedPerformanceTimer timer {
                "ExpressionBaselinePerfProbe::simple-osc sync notes=" + std::to_string (noteCount)
            };
            REQUIRE (engine.syncProject (project));
            CHECK (engine.getCurrentStatus().message.find ("Project synced") != std::string::npos);
        }
}

TEST_CASE ("Native Simple Osc expression note IDs are distinct per loop repetition", "[integration][expression][simple-osc]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    core::sequencing::MidiClip clip {
        "looped-simple-osc-clip",
        "Looped Simple Osc",
        core::time::TickPosition {},
        beats (4),
        core::sequencing::ClipLoop::enabled (beats (2))
    };
    clip.addNote (core::sequencing::MidiNote {
        "loop-note",
        core::music_theory::MidiPitch::middleC(),
        core::time::TickPosition {},
        beats (1),
        100
    });
    auto expression = clip.expressionState();
    auto* pitchLane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    core::sequencing::VibratoExpression vibrato {
        core::sequencing::ExpressionClipId { "loop-vibrato" },
        { "loop-note" },
        core::sequencing::Region { core::time::TickPosition {}, core::time::TickPosition {} + beats (1) }
    };
    vibrato.setAmplitudeSemitones (0.05);
    pitchLane->addVibratoExpression (vibrato);
    clip.setExpressionState (expression);

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());
    REQUIRE (engine.syncProject (simpleOscProjectWithClip (std::move (clip))));

    const auto noteOnIds = engine.debugNativeSimpleOscExpressionNoteOnEventIds ("track-1");
    REQUIRE (noteOnIds.size() == 2);

    const auto uniqueNoteOnIds = std::set<std::uint64_t> { noteOnIds.begin(), noteOnIds.end() };
    CHECK (uniqueNoteOnIds.size() == 2);
}

TEST_CASE ("Native Simple Osc schedules and chases adjacent chord slur chains", "[integration][expression][simple-osc][slur]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    core::sequencing::MidiClip clip {
        "simple-osc-slur-chain",
        "Simple Osc Slur Chain",
        core::time::TickPosition {},
        beats (6)
    };

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
                beatPosition (chordIndex),
                beats (1),
                100
            });

    auto expression = clip.expressionState();
    auto* pitchLane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    for (auto voiceIndex = 0; voiceIndex < 3; ++voiceIndex)
    {
        core::sequencing::PitchSlur first {
            core::sequencing::ExpressionClipId { "slur-1-" + std::to_string (voiceIndex + 1) },
            "chord-1-voice-" + std::to_string (voiceIndex + 1),
            "chord-2-voice-" + std::to_string (voiceIndex + 1)
        };
        first.setLegatoNoRetrigger (true);
        first.setBlockId (core::sequencing::ExpressionBlockId { "slur-block-1" });
        pitchLane->addPitchSlur (first);

        core::sequencing::PitchSlur second {
            core::sequencing::ExpressionClipId { "slur-2-" + std::to_string (voiceIndex + 1) },
            "chord-2-voice-" + std::to_string (voiceIndex + 1),
            "chord-3-voice-" + std::to_string (voiceIndex + 1)
        };
        second.setLegatoNoRetrigger (true);
        second.setBlockId (core::sequencing::ExpressionBlockId { "slur-block-2" });
        pitchLane->addPitchSlur (second);
    }
    clip.setExpressionState (expression);

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());
    REQUIRE (engine.syncProject (simpleOscProjectWithClip (std::move (clip))));

    CHECK (engine.debugNativeSimpleOscExpressionSlurEventCount ("track-1") == 6);

    const auto thirdChordMiddle = beatPosition (2) + core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2);
    REQUIRE (engine.setPlayheadPosition (thirdChordMiddle));
    REQUIRE (engine.debugChaseNativeSimpleOscAtPlayhead ("track-1"));
    CHECK (engine.debugNativeSimpleOscActiveVoiceCount ("track-1") == 3);
}

TEST_CASE ("Piano roll created chord slur runs sync to Native Simple Osc playback", "[integration][expression][simple-osc][slur][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    auto* track = services.project().findTrackById ("track-1");
    REQUIRE (track != nullptr);

    core::sequencing::DeviceChain chain;
    chain.appendSlot (core::sequencing::DeviceSlot {
        core::sequencing::DeviceSlotId { "simple-osc-complex" },
        core::devices::defaultFirstPartyDeviceState (core::devices::simpleOscComplexDefinition()),
        core::sequencing::PluginKind::instrument
    });
    track->setDeviceChain (std::move (chain));

    core::sequencing::MidiClip clip {
        "ui-simple-osc-slur-chain",
        "UI Simple Osc Slur Chain",
        core::time::TickPosition {},
        beats (6)
    };

    const int chordPitches[][3] {
        { 60, 64, 67 },
        { 62, 65, 69 },
        { 64, 67, 71 }
    };

    for (auto chordIndex = 0; chordIndex < 3; ++chordIndex)
        for (auto voiceIndex = 0; voiceIndex < 3; ++voiceIndex)
            clip.addNote (core::sequencing::MidiNote {
                "ui-chord-" + std::to_string (chordIndex + 1) + "-voice-" + std::to_string (voiceIndex + 1),
                core::music_theory::MidiPitch::fromValue (chordPitches[chordIndex][voiceIndex]),
                beatPosition (chordIndex),
                beats (1),
                100
            });
    track->addClip (clip);

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "ui-simple-osc-slur-chain");
    pianoRoll.setExpressionModeEnabled (true);
    REQUIRE (pianoRoll.debugEmulateMarqueeSelectAllVisibleNotes());
    REQUIRE (pianoRoll.debugExpressionKeyPress ('s', 0));
    CHECK (pianoRoll.debugPitchSlurCount() == 6);

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());
    REQUIRE (engine.syncProject (services.project()));

    CHECK (engine.debugNativeSimpleOscExpressionSlurEventCount ("track-1") == 6);

    const auto thirdChordMiddle = beatPosition (2) + core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 2);
    REQUIRE (engine.setPlayheadPosition (thirdChordMiddle));
    REQUIRE (engine.debugChaseNativeSimpleOscAtPlayhead ("track-1"));
    CHECK (engine.debugNativeSimpleOscActiveVoiceCount ("track-1") == 3);
}

TEST_CASE ("Native Simple Osc expression playback can chase an active note at the playhead", "[integration][expression][simple-osc]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    core::sequencing::MidiClip clip {
        "chased-simple-osc-clip",
        "Chased Simple Osc",
        core::time::TickPosition {},
        beats (4)
    };
    clip.addNote (core::sequencing::MidiNote {
        "long-note",
        core::music_theory::MidiPitch::middleC(),
        core::time::TickPosition {},
        beats (4),
        100
    });
    auto expression = clip.expressionState();
    auto* pitchLane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    core::sequencing::VibratoExpression vibrato {
        core::sequencing::ExpressionClipId { "chase-vibrato" },
        { "long-note" },
        core::sequencing::Region { core::time::TickPosition {}, core::time::TickPosition {} + beats (4) }
    };
    vibrato.setAmplitudeSemitones (0.05);
    pitchLane->addVibratoExpression (vibrato);
    clip.setExpressionState (expression);

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());
    REQUIRE (engine.syncProject (simpleOscProjectWithClip (std::move (clip))));
    REQUIRE (engine.setPlayheadPosition (core::time::TickPosition {} + beats (2)));

    CHECK (engine.debugNativeSimpleOscActiveVoiceCount ("track-1") == 0);
    REQUIRE (engine.debugChaseNativeSimpleOscAtPlayhead ("track-1"));
    CHECK (engine.debugNativeSimpleOscActiveVoiceCount ("track-1") == 1);
}

TEST_CASE ("Native Simple Osc normal MIDI clip is scheduled and chaseable without expression", "[integration][expression][simple-osc][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    core::sequencing::MidiClip clip {
        "plain-simple-osc-clip",
        "Plain Simple Osc Clip",
        core::time::TickPosition {},
        beats (4)
    };
    clip.addNote (core::sequencing::MidiNote {
        "plain-note",
        core::music_theory::MidiPitch::middleC(),
        core::time::TickPosition {},
        beats (4),
        100
    });

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());
    REQUIRE (engine.syncProject (simpleOscProjectWithClip (std::move (clip))));

    const auto noteOnIds = engine.debugNativeSimpleOscExpressionNoteOnEventIds ("track-1");
    REQUIRE (noteOnIds.size() == 1);
    CHECK (engine.debugTracktionMidiNoteCount ("track-1") == 0);
    CHECK (engine.debugNativeSimpleOscActiveVoiceCount ("track-1") == 0);

    REQUIRE (engine.setPlayheadPosition (core::time::TickPosition {} + beats (2)));
    REQUIRE (engine.debugChaseNativeSimpleOscAtPlayhead ("track-1"));
    CHECK (engine.debugNativeSimpleOscActiveVoiceCount ("track-1") == 1);
}

TEST_CASE ("Native Simple Osc expression routes are prepared before playback", "[integration][expression][simple-osc]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());
    REQUIRE (engine.syncProject (simpleOscProjectWithClip (simpleOscClipWithFirstPartyExpressionRoute())));

    CHECK (engine.debugNativeSimpleOscExpressionModulationStreamCount ("track-1") == 1);

    const auto refreshCountAfterSync = engine.debugNativeSimpleOscPatchStateRefreshCount ("track-1");
    REQUIRE (engine.returnToStart());
    REQUIRE (engine.startPlayback());
    juce::Thread::sleep (50);
    engine.stopPlayback();

    CHECK (engine.debugNativeSimpleOscPatchStateRefreshCount ("track-1") <= refreshCountAfterSync + 1);
}

TEST_CASE ("Native Simple Osc chord volume expression remains audible during playback", "[integration][expression][simple-osc][playback][mixer][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", core::devices::simpleOscComplexTypeId(), 0));
    const auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> (
            "track-1",
            simpleOscChordClipWithVolumeExpression()));
    REQUIRE (addClipResult.succeeded());
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    auto* engine = dynamic_cast<engine::TracktionPlaybackEngine*> (&services.playbackEngine());
    REQUIRE (engine != nullptr);
    REQUIRE (engine->debugNativeSimpleOscExpressionNoteOnEventIds ("track-1").size() == 9);
    REQUIRE (engine->debugNativeSimpleOscExpressionModulationStreamCount ("track-1") >= 1);

    REQUIRE (services.returnPlaybackToStart());
    CHECK (engine->debugNativeSimpleOscLastOutputPeak ("track-1") == 0.0f);

    REQUIRE (services.startProjectPlayback());
    pumpMessagesFor (700);

    CHECK (engine->debugNativeSimpleOscActiveVoiceCount ("track-1") >= 3);
    CHECK (engine->debugNativeSimpleOscLastOutputPeak ("track-1") > 0.0001f);

    services.stopProjectPlayback();
}

TEST_CASE ("Native Simple Osc volume expression shapes output level during playback", "[integration][expression][simple-osc][playback][mixer][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", core::devices::simpleOscComplexTypeId(), 0));
    const auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> (
            "track-1",
            simpleOscSustainedClipWithRisingVolumeExpression()));
    REQUIRE (addClipResult.succeeded());
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    auto* engine = dynamic_cast<engine::TracktionPlaybackEngine*> (&services.playbackEngine());
    REQUIRE (engine != nullptr);
    REQUIRE (engine->debugNativeSimpleOscExpressionModulationStreamCount ("track-1") >= 1);

    REQUIRE (services.returnPlaybackToStart());
    REQUIRE (services.startProjectPlayback());
    pumpMessagesFor (260);
    const auto earlyPeak = engine->debugNativeSimpleOscLastOutputPeak ("track-1");
    pumpMessagesFor (1200);
    const auto laterPeak = engine->debugNativeSimpleOscLastOutputPeak ("track-1");
    services.stopProjectPlayback();

    CHECK (earlyPeak > 0.0001f);
    CHECK (laterPeak > 0.02f);
    CHECK (laterPeak > earlyPeak * 2.0f);
}

TEST_CASE ("Piano roll single click updates playback playhead", "[integration][expression][simple-osc][ui][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    core::sequencing::MidiClip clip {
        "playhead-click-clip",
        "Playhead Click Clip",
        beatPosition (2),
        beats (4)
    };
    const auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> ("track-1", clip));
    REQUIRE (addClipResult.succeeded());

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "playhead-click-clip");

    REQUIRE (services.setPlaybackPlayheadPosition (beatPosition (1)));
    CHECK (services.playbackPlayheadPosition().ticks() == beatPosition (1).ticks());
    REQUIRE (services.setPlaybackPlayheadPosition (core::time::TickPosition {}));
    CHECK (services.playbackPlayheadPosition() == core::time::TickPosition {});
    REQUIRE (pianoRoll.debugEmulateSingleClickAtFirstEditableCell());
    const auto expectedClickTick = beatPosition (2) + core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 4);
    CHECK (services.playbackPlayheadPosition().ticks() == expectedClickTick.ticks());
}

TEST_CASE ("Native Simple Osc schedules notes added through piano roll after an empty clip sync", "[integration][expression][simple-osc][ui][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", core::devices::simpleOscComplexTypeId(), 0));

    core::sequencing::MidiClip clip {
        "ui-created-simple-osc-note",
        "UI Created Simple Osc Note",
        core::time::TickPosition {},
        beats (4)
    };
    const auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> ("track-1", clip));
    REQUIRE (addClipResult.succeeded());
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    auto* engine = dynamic_cast<engine::TracktionPlaybackEngine*> (&services.playbackEngine());
    REQUIRE (engine != nullptr);
    CHECK (engine->debugNativeSimpleOscExpressionNoteOnEventIds ("track-1").empty());

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "ui-created-simple-osc-note");
    REQUIRE (pianoRoll.debugEmulateDoubleClickAtFirstEditableCell());
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    const auto noteOnIds = engine->debugNativeSimpleOscExpressionNoteOnEventIds ("track-1");
    REQUIRE (noteOnIds.size() == 1);
    CHECK (engine->debugTracktionMidiNoteCount ("track-1") == 0);

    REQUIRE (services.setPlaybackPlayheadPosition (core::time::TickPosition {} + beats (1)));
    REQUIRE (engine->debugChaseNativeSimpleOscAtPlayhead ("track-1"));
    CHECK (engine->debugNativeSimpleOscActiveVoiceCount ("track-1") == 1);
}

TEST_CASE ("Native Simple Osc UI-created notes wake and render during playback", "[integration][expression][simple-osc][playback][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", core::devices::simpleOscComplexTypeId(), 0));

    core::sequencing::MidiClip clip {
        "ui-rendered-simple-osc-note",
        "UI Rendered Simple Osc Note",
        core::time::TickPosition {},
        beats (4)
    };
    const auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> ("track-1", clip));
    REQUIRE (addClipResult.succeeded());

    ui::PianoRollComponent pianoRoll { services };
    pianoRoll.setBounds (0, 0, 1280, 720);
    pianoRoll.openClip ("track-1", "ui-rendered-simple-osc-note");
    REQUIRE (pianoRoll.debugEmulateDoubleClickAtFirstEditableCell());
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    auto* engine = dynamic_cast<engine::TracktionPlaybackEngine*> (&services.playbackEngine());
    REQUIRE (engine != nullptr);
    REQUIRE (engine->debugNativeSimpleOscExpressionNoteOnEventIds ("track-1").size() == 1);

    juce::Thread::sleep (80);
    CHECK (engine->debugNativeSimpleOscMaxOutputPeak ("track-1") == 0.0f);

    const auto renderCallbacksBefore = engine->debugNativeSimpleOscRenderCallbackCount ("track-1");
    const auto playingCallbacksBefore = engine->debugNativeSimpleOscRenderCallbackPlayingCount ("track-1");
    REQUIRE (services.returnPlaybackToStart());
    REQUIRE (services.startProjectPlayback());
    juce::Thread::sleep (180);

    CHECK (engine->debugNativeSimpleOscRenderCallbackCount ("track-1") > renderCallbacksBefore);
    CHECK (engine->debugNativeSimpleOscRenderCallbackPlayingCount ("track-1") > playingCallbacksBefore);
    CHECK (engine->debugNativeSimpleOscMaxOutputPeak ("track-1") > 0.0001f);

    services.stopProjectPlayback();
}

TEST_CASE ("Native Simple Osc long MIDI note sustains during playback without expression", "[integration][expression][simple-osc][playback][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", core::devices::simpleOscComplexTypeId(), 0));

    core::sequencing::MidiClip clip {
        "simple-osc-long-plain-note",
        "Simple Osc Long Plain Note",
        core::time::TickPosition {},
        beats (4)
    };
    clip.addNote (core::sequencing::MidiNote {
        "long-plain-note",
        core::music_theory::MidiPitch::middleC(),
        core::time::TickPosition {},
        beats (4),
        110
    });

    const auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> ("track-1", clip));
    REQUIRE (addClipResult.succeeded());
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    auto* engine = dynamic_cast<engine::TracktionPlaybackEngine*> (&services.playbackEngine());
    REQUIRE (engine != nullptr);
    REQUIRE (engine->debugNativeSimpleOscExpressionNoteOnEventIds ("track-1").size() == 1);

    REQUIRE (services.returnPlaybackToStart());
    REQUIRE (services.startProjectPlayback());
    pumpMessagesFor (700);

    const auto renderRange = engine->debugNativeSimpleOscLastRenderTimeRange ("track-1");
    const auto eventCounters = engine->debugNativeSimpleOscEventCounters ("track-1");
    INFO ("playhead ticks=" << engine->getPlayheadPosition().ticks()
                            << " renderStart=" << renderRange.first
                            << " renderEnd=" << renderRange.second
                            << " activeVoices=" << engine->debugNativeSimpleOscActiveVoiceCount ("track-1")
                            << " lastPeak=" << engine->debugNativeSimpleOscLastOutputPeak ("track-1")
                            << " maxPeak=" << engine->debugNativeSimpleOscMaxOutputPeak ("track-1")
                            << " eventCounters="
                            << (eventCounters.size() > 0 ? eventCounters[0] : 0) << ","
                            << (eventCounters.size() > 1 ? eventCounters[1] : 0) << ","
                            << (eventCounters.size() > 2 ? eventCounters[2] : 0) << ","
                            << (eventCounters.size() > 3 ? eventCounters[3] : 0) << ","
                            << (eventCounters.size() > 4 ? eventCounters[4] : 0) << ","
                            << (eventCounters.size() > 5 ? eventCounters[5] : 0));
    CHECK (engine->debugNativeSimpleOscActiveVoiceCount ("track-1") >= 1);
    CHECK (engine->debugNativeSimpleOscLastOutputPeak ("track-1") > 0.0001f);
    CHECK (engine->debugNativeSimpleOscMaxOutputPeak ("track-1") > 0.0001f);

    services.stopProjectPlayback();
}

TEST_CASE ("Native Simple Osc slur chains stay active during playback", "[integration][expression][simple-osc][playback][slur][regression]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    app::AppServices services;
    REQUIRE (services.insertFirstPartyDeviceToTrack ("track-1", core::devices::simpleOscComplexTypeId(), 0));
    const auto addClipResult = services.commandStack().execute (
        std::make_unique<core::commands::AddClipCommand> (
            "track-1",
            adjacentChordSlurChainClip ("playback-simple-osc-slur-chain")));
    REQUIRE (addClipResult.succeeded());
    REQUIRE (services.syncPlaybackProjectIfNeeded());

    auto* engine = dynamic_cast<engine::TracktionPlaybackEngine*> (&services.playbackEngine());
    REQUIRE (engine != nullptr);
    REQUIRE (engine->debugNativeSimpleOscExpressionSlurEventCount ("track-1") == 6);
    CHECK (engine->debugNativeSimpleOscLegatoSlurEventCount ("track-1") == 6);
    CHECK (engine->debugNativeSimpleOscExpressionNoteOnEventIds ("track-1").size() == 3);
    CHECK (engine->debugTracktionMidiNoteCount ("track-1") == 0);
    CHECK (engine->debugNativeSimpleOscMidiNoteOnCount ("track-1") == 0);

    const auto renderCallbacksBefore = engine->debugNativeSimpleOscRenderCallbackCount ("track-1");
    REQUIRE (services.returnPlaybackToStart());
    REQUIRE (services.startProjectPlayback());
    juce::Thread::sleep (1300);

    CHECK (engine->debugNativeSimpleOscRenderCallbackCount ("track-1") > renderCallbacksBefore);
    CHECK (engine->debugNativeSimpleOscExpressionSlurFallbackCount ("track-1") == 0);
    CHECK (engine->debugNativeSimpleOscActiveVoiceCount ("track-1") == 3);

    services.stopProjectPlayback();
}
