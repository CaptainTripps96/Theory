#include "core/diagnostics/PerformanceTrace.h"
#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/sequencing/AutomationPlayback.h"
#include "core/sequencing/Expression.h"
#include "core/sequencing/DeviceChain.h"
#include "core/music_theory/MidiPitch.h"
#include "core/sequencing/MixerMath.h"
#include "core/sequencing/MidiClip.h"
#include "core/sequencing/MidiNote.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"
#include "core/sequencing/TrackType.h"
#include "core/time/Tick.h"
#include "engine/TracktionPlaybackEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace
{
using namespace tsq;

core::time::TickPosition beat (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::time::TickDuration beats (std::int64_t value)
{
    return core::time::TickDuration::fromTicks (value * core::time::ticksPerQuarterNote);
}

core::time::TickPosition sixteenth (std::int64_t value)
{
    return core::time::TickPosition::fromTicks (value * core::time::ticksPerQuarterNote / 4);
}

core::time::TickDuration sixteenthDuration()
{
    return core::time::TickDuration::fromTicks (core::time::ticksPerQuarterNote / 4);
}

core::sequencing::MidiNote note (std::string id, int pitch, std::int64_t sixteenthStart)
{
    return core::sequencing::MidiNote {
        std::move (id),
        core::music_theory::MidiPitch::fromValue (pitch),
        sixteenth (sixteenthStart),
        sixteenthDuration(),
        96
    };
}

core::sequencing::MidiClip midiClipWithNotes (int trackIndex, int clipIndex, int notesPerClip)
{
    core::sequencing::MidiClip clip {
        "clip-" + std::to_string (trackIndex) + "-" + std::to_string (clipIndex),
        "Clip " + std::to_string (trackIndex) + "." + std::to_string (clipIndex),
        beat (clipIndex * 16),
        beats (16)
    };

    for (auto noteIndex = 0; noteIndex < notesPerClip; ++noteIndex)
    {
        clip.addNote (note ("note-" + std::to_string (trackIndex)
                            + "-" + std::to_string (clipIndex)
                            + "-" + std::to_string (noteIndex),
                            48 + (noteIndex % 36),
                            noteIndex % 64));
    }

    return clip;
}

core::sequencing::Project midiSyncProject (int trackCount, int clipsPerTrack, int notesPerClip)
{
    core::sequencing::Project project {
        "sync-probe-" + std::to_string (trackCount) + "-" + std::to_string (clipsPerTrack) + "-" + std::to_string (notesPerClip),
        "Sync Probe"
    };

    for (auto trackIndex = 0; trackIndex < trackCount; ++trackIndex)
    {
        core::sequencing::Track track {
            "track-" + std::to_string (trackIndex),
            "Track " + std::to_string (trackIndex + 1),
            core::sequencing::TrackType::midi
        };

        for (auto clipIndex = 0; clipIndex < clipsPerTrack; ++clipIndex)
            track.addClip (midiClipWithNotes (trackIndex, clipIndex, notesPerClip));

        project.addTrack (std::move (track));
    }

    return project;
}

void mutateMixerOnly (core::sequencing::Project& project)
{
    auto* track = project.findTrackById ("track-0");
    REQUIRE (track != nullptr);

    auto strip = track->mixerStrip();
    strip.setVolumeDb (-7.5);
    strip.setPan (0.25);
    track->setMixerStrip (strip);
}

void mutateOneNote (core::sequencing::Project& project)
{
    auto* track = project.findTrackById ("track-0");
    REQUIRE (track != nullptr);

    auto* clip = track->findClipById ("clip-0-0");
    REQUIRE (clip != nullptr);

    auto* existingNote = clip->findNoteById ("note-0-0-0");
    REQUIRE (existingNote != nullptr);

    clip->replaceNote (existingNote->id(), existingNote->withPitch (core::music_theory::MidiPitch::fromValue (72)));
}

void mutateFirstPartyExpressionRouteRange (core::sequencing::Project& project)
{
    auto* track = project.findTrackById ("track-0");
    REQUIRE (track != nullptr);

    auto* clip = track->findClipById ("clip-0-0");
    REQUIRE (clip != nullptr);

    auto expression = clip->expressionState();
    auto* lane = expression.findLane (core::sequencing::ExpressionLaneId { "expr-stress-2" });
    REQUIRE (lane != nullptr);

    auto* route = lane->findRoute (core::sequencing::ExpressionRouteId { "route-volume-2" });
    REQUIRE (route != nullptr);
    route->setOutputRange (0.2, 0.8);

    clip->setExpressionState (std::move (expression));
}

void mutateFirstPartyDeviceParameter (core::sequencing::Project& project,
                                      double normalizedValue)
{
    auto* track = project.findTrackById ("track-0");
    REQUIRE (track != nullptr);

    auto chain = track->deviceChain();
    const auto slotId = core::sequencing::DeviceSlotId { "simple-osc-complex" };
    const auto* slot = chain.findSlot (slotId);
    REQUIRE (slot != nullptr);
    REQUIRE (slot->firstPartyDevice().has_value());

    auto state = *slot->firstPartyDevice();
    const auto parameter = std::find_if (state.parameterValues.begin(),
                                         state.parameterValues.end(),
                                         [] (const auto& value)
                                         {
                                             return value.parameterId == "osc.pm.amount";
                                         });
    REQUIRE (parameter != state.parameterValues.end());
    parameter->normalizedValue = normalizedValue;

    core::sequencing::DeviceSlot replacement { slot->id(), std::move (state), slot->kind() };
    replacement.setBypassed (slot->bypassed());
    chain.replaceSlot (slotId, std::move (replacement));
    track->setDeviceChain (std::move (chain));
}

void syncWithProbe (engine::TracktionPlaybackEngine& engine,
                    const core::sequencing::Project& project,
                    const std::string& label)
{
    core::diagnostics::ScopedPerformanceTimer timer { "TracktionSyncPerfProbe::" + label };
    REQUIRE (engine.syncProject (project));
    CHECK (engine.getCurrentStatus().message.find ("Project synced") != std::string::npos);
    core::diagnostics::writePerformanceTrace (
        "TracktionSyncPerfProbe::status label=" + label + " message=" + engine.getCurrentStatus().message,
        0);
}

core::sequencing::MidiClip expressionRouteClip (core::sequencing::ExpressionDestination destination,
                                                double normalizedValue)
{
    auto clip = midiClipWithNotes (0, 0, 1);
    auto expression = clip.expressionState();
    auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);
    lane->addRoute (core::sequencing::ExpressionRoute {
        core::sequencing::ExpressionRouteId { "route-" + destination.stableId() },
        std::move (destination),
        normalizedValue,
        normalizedValue
    });
    clip.setExpressionState (expression);
    return clip;
}

core::sequencing::Project expressionMixerRouteProject (core::sequencing::ExpressionDestination destination,
                                                       double normalizedValue,
                                                       bool includeSend)
{
    core::sequencing::Project project { "expression-mixer-playback", "Expression Mixer Playback" };

    core::sequencing::Track track { "track-0", "Track 1", core::sequencing::TrackType::midi };
    if (includeSend)
    {
        auto routing = track.routing();
        routing.addOrReplaceSend (core::sequencing::ReturnSend { "return-1", 0.1 });
        track.setRouting (routing);
    }
    track.addClip (expressionRouteClip (std::move (destination), normalizedValue));
    project.addTrack (std::move (track));

    if (includeSend)
    {
        core::sequencing::Track returnTrack { "return-1", "Return 1", core::sequencing::TrackType::returnTrack };
        project.addTrack (std::move (returnTrack));
    }

    return project;
}

core::sequencing::Project expressionVolumePhraseEnvelopeProject()
{
    core::sequencing::Project project { "expression-volume-envelope-playback", "Expression Volume Envelope Playback" };

    core::sequencing::Track track { "track-0", "Track 1", core::sequencing::TrackType::midi };
    auto clip = midiClipWithNotes (0, 0, 1);
    auto expression = clip.expressionState();
    auto* lane = expression.findLane (core::sequencing::ExpressionState::defaultVolumeLaneId());
    REQUIRE (lane != nullptr);

    lane->addRoute (core::sequencing::ExpressionRoute {
        core::sequencing::ExpressionRouteId { "route-volume-envelope" },
        core::sequencing::ExpressionDestination::trackVolume ("track-0"),
        0.0,
        1.0
    });

    core::sequencing::PhraseEnvelopeClip envelope {
        core::sequencing::ExpressionClipId { "volume-envelope" },
        { "note-0-0-0" },
        core::sequencing::Region { beat (0), beat (4) },
        0.30,
        core::sequencing::EnvelopeStage {
            core::sequencing::EnvelopeStageType::attack,
            beats (1),
            0.10,
            0.70
        }
    };
    envelope.setDecayStage (core::sequencing::EnvelopeStage {
        core::sequencing::EnvelopeStageType::decay,
        beats (1),
        0.70,
        0.45
    });
    envelope.setPeakLevel (0.70, lane->polarity());
    envelope.setSustainLevel (0.45, lane->polarity());
    envelope.setReleaseStage (core::sequencing::EnvelopeStage {
        core::sequencing::EnvelopeStageType::release,
        beats (1),
        0.45,
        0.05
    });
    lane->addPhraseEnvelopeClip (envelope);

    clip.setExpressionState (expression);
    track.addClip (std::move (clip));
    project.addTrack (std::move (track));
    return project;
}

void addExpressionLaneStressData (core::sequencing::MidiClip& clip,
                                  int laneCount,
                                  int phraseEnvelopeCount,
                                  const std::string& trackId,
                                  const core::sequencing::DeviceSlotId& deviceSlotId)
{
    auto expression = clip.expressionState();

    for (auto laneIndex = 0; laneIndex < laneCount; ++laneIndex)
    {
        const auto laneId = laneIndex == 0
            ? core::sequencing::ExpressionState::defaultVolumeLaneId()
            : core::sequencing::ExpressionLaneId { "expr-stress-" + std::to_string (laneIndex) };

        if (expression.findLane (laneId) == nullptr)
        {
            expression.addLane (core::sequencing::ExpressionLane {
                laneId,
                "Stress " + std::to_string (laneIndex + 1),
                laneIndex % 2 == 0 ? core::sequencing::ExpressionLanePolarity::unipolar
                                   : core::sequencing::ExpressionLanePolarity::bipolar
            });
        }

        auto* lane = expression.findLane (laneId);
        REQUIRE (lane != nullptr);
        lane->addRoute (core::sequencing::ExpressionRoute {
            core::sequencing::ExpressionRouteId { "route-volume-" + std::to_string (laneIndex) },
            laneIndex % 3 == 0
                ? core::sequencing::ExpressionDestination::trackVolume (trackId)
                : (laneIndex % 3 == 1
                    ? core::sequencing::ExpressionDestination::trackPan (trackId)
                    : core::sequencing::ExpressionDestination::firstPartyParameter (
                        trackId,
                        deviceSlotId,
                        laneIndex % 2 == 0 ? "wavefolder.amount" : "modulator.amount")),
            lane->polarity() == core::sequencing::ExpressionLanePolarity::bipolar ? -0.5 : 0.0,
            1.0
        });

        for (auto envelopeIndex = 0; envelopeIndex < phraseEnvelopeCount; ++envelopeIndex)
        {
            const auto noteIndex = envelopeIndex % std::max (1, static_cast<int> (clip.notes().size()));
            const auto& sourceNote = clip.notes()[static_cast<std::size_t> (noteIndex)];
            const auto start = sourceNote.startInClip();
            const auto end = start + beats (1);
            core::sequencing::PhraseEnvelopeClip envelope {
                core::sequencing::ExpressionClipId {
                    "env-" + std::to_string (laneIndex) + "-" + std::to_string (envelopeIndex)
                },
                { sourceNote.id() },
                core::sequencing::Region { start, end },
                0.35,
                core::sequencing::EnvelopeStage {
                    core::sequencing::EnvelopeStageType::attack,
                    sixteenthDuration(),
                    lane->polarity() == core::sequencing::ExpressionLanePolarity::bipolar ? -1.0 : 0.0,
                    0.85
                }
            };
            envelope.setReleaseStage (core::sequencing::EnvelopeStage {
                core::sequencing::EnvelopeStageType::release,
                sixteenthDuration(),
                0.85,
                lane->polarity() == core::sequencing::ExpressionLanePolarity::bipolar ? -0.25 : 0.0
            });
            lane->addPhraseEnvelopeClip (envelope);
        }

        if (laneIndex % 2 == 0)
        {
            core::sequencing::CyclicExpressionClip cyclic {
                core::sequencing::ExpressionClipId { "cyclic-" + std::to_string (laneIndex) },
                { clip.notes().empty() ? "missing" : clip.notes().front().id() },
                core::sequencing::Region {
                    core::time::TickPosition {},
                    core::time::TickPosition::fromTicks (clip.length().ticks())
                }
            };
            cyclic.setMaxAmplitude (0.25);
            cyclic.setFrequencyDivisionId ("sixteenth");
            cyclic.setWaveShape (core::sequencing::CyclicWaveShape::sine);
            lane->addCyclicClip (cyclic);
        }
    }

    auto* pitchLane = expression.findLane (core::sequencing::ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    const auto slurPairs = std::min<int> (32, static_cast<int> (clip.notes().size()) / 2);
    for (auto slurIndex = 0; slurIndex < slurPairs; ++slurIndex)
    {
        const auto& sourceNote = clip.notes()[static_cast<std::size_t> (slurIndex * 2)];
        const auto& destinationNote = clip.notes()[static_cast<std::size_t> ((slurIndex * 2) + 1)];
        auto slur = core::sequencing::PitchSlur {
            core::sequencing::ExpressionClipId { "slur-" + std::to_string (slurIndex) },
            sourceNote.id(),
            destinationNote.id()
        };
        slur.setSlurTime (sixteenthDuration());
        pitchLane->addPitchSlur (slur);
    }

    clip.setExpressionState (expression);
}

core::sequencing::Project expressionStressProject (int noteCount, int laneCount)
{
    core::sequencing::Project project {
        "expression-stress-" + std::to_string (noteCount) + "-" + std::to_string (laneCount),
        "Expression Stress"
    };

    constexpr auto trackId = "track-0";
    const auto deviceSlotId = core::sequencing::DeviceSlotId { "simple-osc-complex" };

    core::sequencing::Track track { trackId, "Expression Stress Track", core::sequencing::TrackType::midi };
    core::sequencing::DeviceChain chain;
    chain.appendSlot (core::sequencing::DeviceSlot {
        deviceSlotId,
        core::devices::defaultFirstPartyDeviceState (core::devices::simpleOscComplexDefinition()),
        core::sequencing::PluginKind::instrument
    });
    track.setDeviceChain (std::move (chain));

    auto clip = midiClipWithNotes (0, 0, noteCount);
    addExpressionLaneStressData (clip,
                                 laneCount,
                                 std::max (8, noteCount / 16),
                                 trackId,
                                 deviceSlotId);
    track.addClip (std::move (clip));
    project.addTrack (std::move (track));
    return project;
}
}

TEST_CASE ("Tracktion project sync and materialization paths are performance probed", "[integration][sync][perf]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    struct Scenario
    {
        int tracks = 0;
        int clipsPerTrack = 0;
        int notesPerClip = 0;
    };

    for (const auto scenario : std::array<Scenario, 2> {
             Scenario { 4, 4, 64 },
             Scenario { 8, 8, 128 },
         })
    {
        auto project = midiSyncProject (scenario.tracks, scenario.clipsPerTrack, scenario.notesPerClip);
        const auto projectLabel = "tracks=" + std::to_string (scenario.tracks)
            + " clipsPerTrack=" + std::to_string (scenario.clipsPerTrack)
            + " notesPerClip=" + std::to_string (scenario.notesPerClip);

        syncWithProbe (engine, project, "full sync " + projectLabel);

        mutateMixerOnly (project);
        syncWithProbe (engine, project, "in-place mixer-only sync " + projectLabel);

        mutateOneNote (project);
        syncWithProbe (engine, project, "in-place note-change sync " + projectLabel);

        syncWithProbe (engine, project, "in-place unchanged sync " + projectLabel);
    }
}

TEST_CASE ("Expression Mode dense playback preparation is performance probed", "[integration][expression][perf][sync]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    struct Scenario
    {
        int noteCount = 0;
        int laneCount = 0;
    };

    for (const auto scenario : std::array<Scenario, 2> {
             Scenario { 100, 2 },
             Scenario { 1000, 8 },
         })
    {
        auto project = expressionStressProject (scenario.noteCount, scenario.laneCount);
        const auto label = "notes=" + std::to_string (scenario.noteCount)
            + " lanes=" + std::to_string (scenario.laneCount);

        syncWithProbe (engine, project, "expression dense full sync " + label);

        {
            core::diagnostics::ScopedPerformanceTimer timer {
                "ExpressionModeFullPerfProbe::return-to-zero " + label
            };
            REQUIRE (engine.returnToStart());
        }

        {
            core::diagnostics::ScopedPerformanceTimer timer {
                "ExpressionModeFullPerfProbe::playback-start " + label
            };
            REQUIRE (engine.startPlayback());
            engine.stopPlayback();
        }

        auto* track = project.findTrackById ("track-0");
        REQUIRE (track != nullptr);
        auto* clip = track->findClipById ("clip-0-0");
        REQUIRE (clip != nullptr);
        auto* noteToEdit = clip->findNoteById ("note-0-0-0");
        REQUIRE (noteToEdit != nullptr);
        clip->replaceNote (noteToEdit->id(), noteToEdit->withPitch (core::music_theory::MidiPitch::fromValue (72)));
        syncWithProbe (engine, project, "expression dense note edit sync " + label);
    }
}

TEST_CASE ("First-party Simple Osc expression edits sync in place", "[integration][expression][sync]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    auto project = expressionStressProject (32, 3);
    syncWithProbe (engine, project, "simple-osc expression initial full sync");
    CHECK (engine.getCurrentStatus().message.find ("full edit rebuild") != std::string::npos);
    CHECK (engine.debugNativeSimpleOscExpressionModulationStreamCount ("track-0") == 1);

    const auto refreshCountAfterFullSync = engine.debugNativeSimpleOscPatchStateRefreshCount ("track-0");
    mutateFirstPartyExpressionRouteRange (project);
    syncWithProbe (engine, project, "simple-osc expression route edit in-place sync");

    CHECK (engine.getCurrentStatus().message.find ("in place") != std::string::npos);
    CHECK (engine.debugNativeSimpleOscExpressionModulationStreamCount ("track-0") == 1);
    CHECK (engine.debugNativeSimpleOscPatchStateRefreshCount ("track-0") > refreshCountAfterFullSync);
}

TEST_CASE ("First-party Simple Osc parameter edits sync in place", "[integration][expression][sync]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    auto project = expressionStressProject (32, 3);
    syncWithProbe (engine, project, "simple-osc parameter initial full sync");
    CHECK (engine.getCurrentStatus().message.find ("full edit rebuild") != std::string::npos);

    const auto refreshCountAfterFullSync = engine.debugNativeSimpleOscPatchStateRefreshCount ("track-0");
    mutateFirstPartyDeviceParameter (project, 0.82);
    syncWithProbe (engine, project, "simple-osc parameter edit in-place sync");

    CHECK (engine.getCurrentStatus().message.find ("in place") != std::string::npos);
    CHECK (engine.debugNativeSimpleOscExpressionModulationStreamCount ("track-0") == 1);
    CHECK (engine.debugNativeSimpleOscPatchStateRefreshCount ("track-0") > refreshCountAfterFullSync);
}

TEST_CASE ("Expression routes apply to Tracktion mixer volume and pan playback", "[integration][expression][playback][mixer]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    constexpr auto volumeValue = 0.42;
    auto volumeProject = expressionMixerRouteProject (
        core::sequencing::ExpressionDestination::trackVolume ("track-0"),
        volumeValue,
        false);
    REQUIRE (engine.syncProject (volumeProject));
    CHECK (engine.debugTrackVolumeAutomationPointCount ("track-0") > 0);
    REQUIRE (engine.setPlayheadPosition (beat (1)));
    const auto volumeDb = engine.debugTrackVolumeDb ("track-0");
    REQUIRE (volumeDb.has_value());
    CHECK (*volumeDb == Catch::Approx (core::sequencing::volumeDbFromAutomationValue (volumeValue)).margin (0.05));

    constexpr auto panValue = 0.82;
    auto panProject = expressionMixerRouteProject (
        core::sequencing::ExpressionDestination::trackPan ("track-0"),
        panValue,
        false);
    REQUIRE (engine.syncProject (panProject));
    REQUIRE (engine.setPlayheadPosition (beat (1)));
    const auto pan = engine.debugTrackPan ("track-0");
    REQUIRE (pan.has_value());
    CHECK (*pan == Catch::Approx (core::sequencing::panFromAutomationValue (panValue)).margin (0.02));

    auto envelopeProject = expressionVolumePhraseEnvelopeProject();
    REQUIRE (engine.syncProject (envelopeProject));
    CHECK (engine.debugTrackVolumeAutomationPointCount ("track-0") > 8);
    REQUIRE (engine.setPlayheadPosition (beat (1)));
    const auto peakVolumeDb = engine.debugTrackVolumeDb ("track-0");
    REQUIRE (peakVolumeDb.has_value());
    CHECK (*peakVolumeDb == Catch::Approx (core::sequencing::volumeDbFromAutomationValue (0.70)).margin (0.08));
    REQUIRE (engine.setPlayheadPosition (beat (3)));
    const auto releaseVolumeDb = engine.debugTrackVolumeDb ("track-0");
    REQUIRE (releaseVolumeDb.has_value());
    CHECK (*releaseVolumeDb == Catch::Approx (core::sequencing::volumeDbFromAutomationValue (0.45)).margin (0.08));
}

TEST_CASE ("Expression routes apply to Tracktion send level playback", "[integration][expression][playback][mixer]")
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    engine::TracktionPlaybackEngine engine;
    REQUIRE (engine.initialize());

    constexpr auto sendValue = 0.73;
    auto project = expressionMixerRouteProject (
        core::sequencing::ExpressionDestination::sendLevel ("track-0", "return-1"),
        sendValue,
        true);

    REQUIRE (engine.syncProject (project));
    REQUIRE (engine.setPlayheadPosition (beat (1)));
    const auto sendGainDb = engine.debugSendGainDb ("track-0", "return-1");
    REQUIRE (sendGainDb.has_value());
    CHECK (*sendGainDb == Catch::Approx (core::sequencing::sendDecibelsFromNormalizedLevel (sendValue, -60.0)).margin (0.05));
}
