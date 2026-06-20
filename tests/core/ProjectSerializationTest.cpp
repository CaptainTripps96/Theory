#include "core/music_theory/CustomScaleBuilder.h"
#include "core/music_theory/ScaleLibrary.h"
#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/midi/MidiExporter.h"
#include "core/serialization/ProjectPackage.h"
#include "core/serialization/ProjectSchemaVersion.h"
#include "core/serialization/ProjectSerializer.h"
#include "core/sequencing/AccidentalVisibility.h"
#include "core/sequencing/NoteHarmonicInterpretation.h"
#include "core/sequencing/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core;
using namespace tsq::core::devices;
using namespace tsq::core::music_theory;
using namespace tsq::core::midi;
using namespace tsq::core::serialization;
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

std::filesystem::path uniquePackagePath()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("TheorySequencerSerializationTest-" + std::to_string (stamp) + ".tseq");
}

Project makeProjectWithData()
{
    Project project { "project-1", "Round Trip Song" };

    project.musicalStructure().addKeyCenterRegion (KeyCenterRegion { bars (1, 5), PitchClass::c() });
    project.musicalStructure().addKeyCenterRegion (KeyCenterRegion { bars (5, 9), PitchClass::g() });
    project.musicalStructure().addScaleModeRegion (ScaleModeRegion { bars (1, 5), "Major" });
    project.musicalStructure().addScaleModeRegion (ScaleModeRegion { bars (5, 9), "Mixolydian" });
    project.musicalStructure().addChordRegion (ChordRegion {
        bars (1, 3),
        PitchClass::c(),
        ChordQuality::majorSeventh,
        { PitchClass::c(), PitchClass::e(), PitchClass::g(), PitchClass::b() },
        "Cmaj7"
    });
    project.tempoMap().addNode (beat (16), Tempo { 140.0 });
    project.timeSignatureMap().addMarker (beat (16), TimeSignature { 3, 4 });
    project.rhythmSettings().setCurrentGridDivisionId ("eighthTriplet");
    project.rhythmSettings().setQuintupletsEnabled (true);

    Track track { "track-1", "Piano" };
    track.setInstrument (TrackInstrumentReference {
        "Round Trip Piano",
        "TheorySequencer",
        "VST3",
        "/Library/Audio/Plug-Ins/VST3/Round Trip Piano.vst3",
        "round-trip-piano",
        5678,
        0,
        true,
        0,
        2,
        "plugin-states/track-1.vststate"
    });

    MidiClip clip {
        "clip-1",
        "Verse",
        beat (0),
        beats (8),
        ClipLoop::enabled (beats (4))
    };

    const auto sourceContext = HarmonicContext { PitchClass::c(), "Major" };
    const auto scaleLibrary = ScaleLibrary::createBuiltInLibrary();
    const auto fSharpPitch = MidiPitch::fromValue (66);
    clip.addNote (MidiNote { "note-1", MidiPitch::middleC(), beat (0), beats (1), 100, NoteName::c() });
    clip.addNote (MidiNote {
        "note-2",
        fSharpPitch,
        beat (2),
        beats (1),
        96,
        NoteName::fSharp(),
        interpretNoteHarmonically (fSharpPitch, sourceContext, scaleLibrary)
    });
    clip.harmonicMetadata().addRegion (ClipHarmonicRegion { Region { beat (0), beat (4) }, HarmonicContext { PitchClass::c(), "Major" } });
    clip.harmonicMetadata().addRegion (ClipHarmonicRegion { Region { beat (4), beat (8) }, HarmonicContext { PitchClass::c(), "Mixolydian" } });

    track.addClip (std::move (clip));
    project.addTrack (std::move (track));

    project.addCustomScale (CustomScaleBuilder::build (
        ScaleMetadata { "Custom Triad", "Custom", { "user", "triad" }, "A minimal user scale." },
        { PitchClass::c(), PitchClass::e(), PitchClass::g() }));

    return project;
}

const PitchLaneVisibility* findLane (const std::vector<PitchLaneVisibility>& lanes, PitchClass pitchClass)
{
    for (const auto& lane : lanes)
        if (lane.pitchClass == pitchClass)
            return &lane;

    return nullptr;
}

PluginReference plugin (std::string name = "Device")
{
    return PluginReference {
        std::move (name),
        "TheorySequencer",
        "VST3",
        "/Library/Audio/Plug-Ins/VST3/Device.vst3",
        "device-vst3",
        1234,
        0,
        0,
        2
    };
}

DeviceSlot instrumentSlot()
{
    DeviceSlot slot { DeviceSlotId { "instrument" }, plugin ("Round Trip Instrument"), PluginKind::instrument };
    slot.setPluginStateFile ("plugin-states/instrument.vststate");
    return slot;
}

DeviceSlot effectSlot()
{
    DeviceSlot slot { DeviceSlotId { "chorus" }, plugin ("Round Trip Chorus"), PluginKind::audioEffect };
    slot.setBypassed (true);
    slot.setPluginStateFile ("plugin-states/chorus.vststate");
    return slot;
}

AudioSourceReference audioSource (std::string filePath = "assets/vocal.wav")
{
    return AudioSourceReference {
        "audio-source-1",
        std::move (filePath),
        "Vocal",
        true
    };
}

bool containsWarning (const std::vector<std::string>& warnings, const std::string& fragment)
{
    return std::any_of (warnings.begin(), warnings.end(), [&fragment] (const auto& warning) {
        return warning.find (fragment) != std::string::npos;
    });
}
}

TEST_CASE ("Empty project round trips through project serializer")
{
    const Project project { "empty-project", "Empty Song" };

    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (project));

    CHECK (loaded.id() == "empty-project");
    CHECK (loaded.name() == "Empty Song");
    CHECK (loaded.tracks().empty());
    CHECK (loaded.customScales().empty());
    CHECK (loaded.musicalStructure().defaultScaleDefinitionName() == "Major");
}

TEST_CASE ("Project with tracks clips notes and loops round trips")
{
    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (makeProjectWithData()));

    REQUIRE (loaded.tracks().size() == 1);
    const auto& track = loaded.tracks()[0];
    CHECK (track.id() == "track-1");
    CHECK (track.name() == "Piano");
    REQUIRE (track.instrument().has_value());
    CHECK (track.instrument()->pluginName == "Round Trip Piano");
    CHECK (track.instrument()->uniqueIdentifier == "round-trip-piano");
    CHECK (track.instrument()->fileOrIdentifier == "/Library/Audio/Plug-Ins/VST3/Round Trip Piano.vst3");
    CHECK (track.instrument()->pluginStateFile == "plugin-states/track-1.vststate");

    REQUIRE (track.clips().size() == 1);
    const auto& clip = track.clips()[0];
    CHECK (clip.id() == "clip-1");
    CHECK (clip.name() == "Verse");
    CHECK (clip.startInProject() == beat (0));
    CHECK (clip.length() == beats (8));
    CHECK (clip.sourceLength() == beats (4));
    CHECK (clip.loop().isEnabled());
    CHECK (clip.loop().loopDuration() == beats (4));

    REQUIRE (clip.notes().size() == 2);
    CHECK (clip.notes()[0].id() == "note-1");
    CHECK (clip.notes()[0].pitch() == MidiPitch::middleC());
    REQUIRE (clip.notes()[1].spelling().has_value());
    CHECK (clip.notes()[1].spelling()->toString() == "F#");
    REQUIRE (clip.notes()[1].harmonicInterpretation().has_value());
    const auto expectedSourceContext = HarmonicContext { PitchClass::c(), "Major" };
    CHECK (clip.notes()[1].harmonicInterpretation()->sourceContext == expectedSourceContext);
    CHECK (clip.notes()[1].harmonicInterpretation()->scaleDegreeIndex == 3);
    CHECK (clip.notes()[1].harmonicInterpretation()->alteration == 1);
}

TEST_CASE ("Musical structure lanes round trip")
{
    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (makeProjectWithData()));

    CHECK (loaded.musicalStructure().keyCenterRegions().size() == 2);
    CHECK (loaded.musicalStructure().scaleModeRegions().size() == 2);
    CHECK (loaded.musicalStructure().chordRegions().size() == 1);
    CHECK (loaded.musicalStructure().keyCenterAt (beat (16)) == PitchClass::g());
    CHECK (loaded.musicalStructure().scaleDefinitionNameAt (beat (16)) == "Mixolydian");
    CHECK (loaded.musicalStructure().chordRegions()[0].chordName() == "Cmaj7");
    CHECK (loaded.musicalStructure().chordRegions()[0].root() == PitchClass::c());
    CHECK (loaded.musicalStructure().chordRegions()[0].quality() == ChordQuality::majorSeventh);
    CHECK (loaded.musicalStructure().chordRegions()[0].chordTones() == std::vector<PitchClass> {
        PitchClass::c(),
        PitchClass::e(),
        PitchClass::g(),
        PitchClass::b()
    });
}

TEST_CASE ("Tempo and time signature lanes round trip")
{
    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (makeProjectWithData()));

    REQUIRE (loaded.tempoMap().nodes().size() == 2);
    CHECK (loaded.tempoMap().nodes()[0].position == beat (0));
    CHECK (loaded.tempoMap().nodes()[0].tempo.bpm() == 120.0);
    CHECK (loaded.tempoMap().nodes()[1].position == beat (16));
    CHECK (loaded.tempoMap().nodes()[1].tempo.bpm() == 140.0);

    REQUIRE (loaded.timeSignatureMap().markers().size() == 2);
    CHECK (loaded.timeSignatureMap().markers()[0].timeSignature.numerator() == 4);
    CHECK (loaded.timeSignatureMap().markers()[0].timeSignature.denominator() == 4);
    CHECK (loaded.timeSignatureMap().markers()[1].position == beat (16));
    CHECK (loaded.timeSignatureMap().markers()[1].timeSignature.numerator() == 3);
    CHECK (loaded.timeSignatureMap().markers()[1].timeSignature.denominator() == 4);
}

TEST_CASE ("Rhythm settings round trip")
{
    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (makeProjectWithData()));

    CHECK (loaded.rhythmSettings().currentGridDivisionId() == "eighthTriplet");
    CHECK (loaded.rhythmSettings().tripletsEnabled());
    CHECK (loaded.rhythmSettings().quintupletsEnabled());
    CHECK_FALSE (loaded.rhythmSettings().septupletsEnabled());
    CHECK_FALSE (loaded.rhythmSettings().nonupletsEnabled());
}

TEST_CASE ("Custom scales round trip")
{
    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (makeProjectWithData()));

    REQUIRE (loaded.customScales().size() == 1);
    CHECK (loaded.customScales()[0].name() == "Custom Triad");
    CHECK (loaded.customScales()[0].category() == "Custom");
    CHECK (loaded.customScales()[0].pitchClassOffsetsFromRoot() == std::vector<int> { 0, 4, 7 });
}

TEST_CASE ("Plugin state file reference round trips")
{
    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (makeProjectWithData()));

    REQUIRE (loaded.tracks().size() == 1);
    REQUIRE (loaded.tracks()[0].instrument().has_value());
    CHECK (loaded.tracks()[0].instrument()->pluginStateFile == "plugin-states/track-1.vststate");
}

TEST_CASE ("Mixer architecture state round trips through project serializer")
{
    Project project { "project-1", "Mixer Song" };
    project.addTrack (Track { "return-1", "Reverb", TrackType::returnTrack });
    project.addTrack (Track { "master-1", "Master", TrackType::master });

    Track midiTrack { "track-1", "Piano" };
    MixerStrip strip;
    strip.setVolumeDb (-9.0);
    strip.setPan (0.25);
    strip.setSoloed (true);
    strip.setMeterSourceId ("meter-track-1");
    strip.setColorArgb (0xff336699u);
    midiTrack.setMixerStrip (strip);

    DeviceChain chain;
    chain.appendSlot (instrumentSlot());
    chain.appendSlot (effectSlot());
    midiTrack.setDeviceChain (chain);

    TrackRouting routing;
    routing.addOrReplaceSend (ReturnSend { "return-1", 0.35 });
    midiTrack.setRouting (routing);

    AutomationCurve volumeCurve;
    volumeCurve.addPoint (AutomationPoint { beat (0), 0.25 });
    volumeCurve.addPoint (AutomationPoint { beat (4), 0.75, AutomationInterpolation::hold });
    midiTrack.setAutomationLane (AutomationLane { AutomationTarget::trackVolume ("track-1"), volumeCurve });

    AutomationCurve parameterCurve;
    parameterCurve.addPoint (AutomationPoint { beat (0), 0.1 });
    parameterCurve.addPoint (AutomationPoint { beat (2), 0.9 });
    midiTrack.setAutomationLane (AutomationLane {
        AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "chorus" }, "mix"),
        parameterCurve
    });

    project.addTrack (std::move (midiTrack));

    Track audioTrack { "audio-1", "Vocal", TrackType::audio };
    AudioClip audioClip { "audio-clip-1", "Lead Vocal", audioSource(), beat (0), beats (8), beats (1) };
    audioClip.setLoopEnabled (true);
    audioClip.setStretchToTempo (true);
    audioClip.setGainDb (-6.0);
    audioTrack.addAudioClip (audioClip);
    project.addTrack (std::move (audioTrack));

    const auto json = ProjectSerializer::toJson (project);
    CHECK (requireInt (requireField (json, "schemaVersion"), "schema version") == currentProjectSchemaVersion);
    const auto& audioAssets = requireField (json, "audioAssets");
    const auto& waveformCache = requireField (audioAssets, "waveformCache");
    const auto& waveformEntries = requireArray (requireField (waveformCache, "entries"), "waveform cache entries");
    REQUIRE (waveformEntries.size() == 1);
    CHECK (requireString (requireField (waveformEntries[0], "sourceId"), "waveform source ID") == "audio-source-1");
    CHECK (requireString (requireField (waveformEntries[0], "filePath"), "waveform source path") == "assets/vocal.wav");
    CHECK (requireString (requireField (waveformEntries[0], "cacheKey"), "waveform cache key") == "audio-source-1::assets/vocal.wav");

    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (project));

    REQUIRE (loaded.tracks().size() == 4);
    const auto* loadedMidiTrack = loaded.findTrackById ("track-1");
    REQUIRE (loadedMidiTrack != nullptr);
    CHECK (loadedMidiTrack->type() == TrackType::midi);
    CHECK (loadedMidiTrack->mixerStrip().volumeDb() == -9.0);
    CHECK (loadedMidiTrack->mixerStrip().pan() == 0.25);
    CHECK (loadedMidiTrack->mixerStrip().soloed());
    REQUIRE (loadedMidiTrack->mixerStrip().colorArgb().has_value());
    CHECK (*loadedMidiTrack->mixerStrip().colorArgb() == 0xff336699u);
    REQUIRE (loadedMidiTrack->deviceChain().slots().size() == 2);
    CHECK (loadedMidiTrack->deviceChain().slots()[0].kind() == PluginKind::instrument);
    CHECK (loadedMidiTrack->deviceChain().slots()[1].id() == DeviceSlotId { "chorus" });
    CHECK (loadedMidiTrack->deviceChain().slots()[1].bypassed());
    REQUIRE (loadedMidiTrack->routing().sends().size() == 1);
    CHECK (loadedMidiTrack->routing().sends()[0].targetReturnTrackId == "return-1");
    CHECK (loadedMidiTrack->routing().sends()[0].normalizedLevel == 0.35);
    REQUIRE (loadedMidiTrack->automationLanes().size() == 2);
    CHECK (loadedMidiTrack->findAutomationLane (AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "chorus" }, "mix")) != nullptr);

    const auto* loadedAudioTrack = loaded.findTrackById ("audio-1");
    REQUIRE (loadedAudioTrack != nullptr);
    CHECK (loadedAudioTrack->type() == TrackType::audio);
    REQUIRE (loadedAudioTrack->audioClips().size() == 1);
    CHECK (loadedAudioTrack->audioClips()[0].source().filePath == "assets/vocal.wav");
    CHECK (loadedAudioTrack->audioClips()[0].loopEnabled());
    CHECK (loadedAudioTrack->audioClips()[0].stretchToTempo());
    CHECK (loadedAudioTrack->audioClips()[0].gainDb() == -6.0);

    REQUIRE (loaded.masterTrack() != nullptr);
    CHECK (loaded.masterTrack()->routing().audioTo().kind == RouteEndpointKind::hardwareOutput);
}

TEST_CASE ("First-party device slots round trip through project serializer")
{
    Project project { "project-1", "Native Device Song" };
    Track track { "track-1", "Native Synth" };

    const auto& definition = simpleOscComplexDefinition();
    DeviceChain chain;
    auto slot = DeviceSlot {
        DeviceSlotId { "simple-osc-complex" },
        defaultFirstPartyDeviceState (definition),
        definition.kind
    };
    slot.setBypassed (true);
    chain.appendSlot (std::move (slot));
    track.setDeviceChain (std::move (chain));
    project.addTrack (std::move (track));

    const auto json = ProjectSerializer::toJson (project);
    const auto& tracks = requireArray (requireField (json, "tracks"), "tracks");
    REQUIRE (tracks.size() == 1);
    const auto& slots = requireArray (requireField (requireField (tracks[0], "deviceChain"), "slots"), "device slots");
    REQUIRE (slots.size() == 1);
    CHECK (requireString (requireField (slots[0], "deviceType"), "device type") == "firstParty");
    CHECK (requireString (requireField (requireField (slots[0], "firstPartyDevice"), "typeId"), "native device type")
           == simpleOscComplexTypeId());

    const auto loaded = ProjectSerializer::fromJson (json);
    const auto* loadedTrack = loaded.findTrackById ("track-1");
    REQUIRE (loadedTrack != nullptr);
    REQUIRE (loadedTrack->deviceChain().slots().size() == 1);

    const auto& loadedSlot = loadedTrack->deviceChain().slots()[0];
    CHECK (loadedSlot.id() == DeviceSlotId { "simple-osc-complex" });
    CHECK (loadedSlot.kind() == PluginKind::instrument);
    CHECK (loadedSlot.bypassed());
    CHECK (loadedSlot.isFirstPartyDevice());
    CHECK_FALSE (loadedSlot.isPluginDevice());
    REQUIRE (loadedSlot.firstPartyDevice().has_value());
    CHECK (loadedSlot.firstPartyDevice()->typeId == simpleOscComplexTypeId());
    CHECK (loadedSlot.firstPartyDevice()->patchVersion == definition.patchVersion);
    CHECK (loadedSlot.firstPartyDevice()->parameterValues.size() == definition.parameters.size());
}

TEST_CASE ("Expression state round trips through project serializer")
{
    Project project { "project-1", "Expression Song" };
    Track track { "track-1", "Lead" };
    MidiClip clip { "clip-1", "Motif", beat (0), beats (8) };
    clip.addNote (MidiNote { "note-1", MidiPitch::middleC(), beat (0), beats (1), 100, NoteName::c() });
    clip.addNote (MidiNote { "note-2", MidiPitch::fromValue (64), beat (1), beats (1), 100, NoteName::e() });

    auto expression = clip.expressionState();
    auto* volumeLane = expression.findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (volumeLane != nullptr);
    volumeLane->rename ("Clip Volume");
    ExpressionRoute volumeRoute {
        ExpressionRouteId { "route-volume" },
        ExpressionDestination::firstPartyParameter ("track-1", DeviceSlotId { "simple-osc-complex" }, "amp.level"),
        0.85,
        0.15
    };
    volumeRoute.setEnabled (false);
    volumeLane->addRoute (volumeRoute);

    PhraseEnvelopeClip envelope {
        ExpressionClipId { "env-1" },
        { "note-1", "note-2" },
        Region { beat (0), beat (4) },
        0.4,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0, ExpressionCurveShape::exponential }
    };
    envelope.setDecayStage (EnvelopeStage { EnvelopeStageType::decay, beats (1), 1.0, 0.6, ExpressionCurveShape::logarithmic });
    envelope.setReleaseStage (EnvelopeStage { EnvelopeStageType::release, beats (1), 0.6, 0.0 });
    envelope.setPeakLevel (0.9, volumeLane->polarity());
    envelope.setSustainLevel (0.6, volumeLane->polarity());
    envelope.setTailExtension (beats (1));
    volumeLane->addPhraseEnvelopeClip (envelope);

    auto* pitchLane = expression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (pitchLane != nullptr);
    PitchSlur slur { ExpressionClipId { "slur-1" }, "note-1", "note-2" };
    slur.setSlurTime (beats (1));
    slur.setCurveShape (ExpressionCurveShape::logarithmic);
    slur.setBlockId (ExpressionBlockId { "slur-block" });
    slur.setHasVoiceOverride (true);
    pitchLane->addPitchSlur (slur);

    VibratoExpression vibrato { ExpressionClipId { "vibrato-1" }, { "note-1", "note-2" }, Region { beat (0), beat (4) } };
    vibrato.setAttackTime (beats (1));
    vibrato.setReleaseTime (beats (1));
    vibrato.setAmplitudeSemitones (0.5);
    vibrato.setFrequencyDivisionId ("eighth");
    vibrato.setWaveShape (CyclicWaveShape::triangle);
    vibrato.setPhase (0.25);
    vibrato.setBlockId (ExpressionBlockId { "vib-block" });
    vibrato.setVoiceOverrides ({
        VibratoVoiceOverride { "note-2", 0.25, beats (1), beats (1), "sixteenth", CyclicWaveShape::sine, 0.5 }
    });
    pitchLane->addVibratoExpression (vibrato);

    ExpressionLane motionLane { ExpressionLaneId { "expr-motion" }, "Motion", ExpressionLanePolarity::bipolar };
    motionLane.setEnabled (false);
    motionLane.addRoute (ExpressionRoute {
        ExpressionRouteId { "route-midi-cc" },
        ExpressionDestination::midiCc ("track-1", 74),
        -0.5,
        0.5
    });
    CyclicExpressionClip cyclic { ExpressionClipId { "cyclic-1" }, { "note-1" }, Region { beat (4), beat (8) } };
    cyclic.setAttackTime (beats (1));
    cyclic.setReleaseTime (beats (1));
    cyclic.setMaxAmplitude (0.7);
    cyclic.setFrequencyDivisionId ("sixteenth");
    cyclic.setWaveShape (CyclicWaveShape::rampUp);
    cyclic.setBlendMode (CyclicBlendMode::multiplicative);
    cyclic.setWavePolarityMode (CyclicWavePolarityMode::halfWaveRectified);
    cyclic.setPhase (0.125);
    motionLane.addCyclicClip (cyclic);
    expression.addLane (motionLane);

    clip.setExpressionState (expression);
    track.addClip (std::move (clip));
    project.addTrack (std::move (track));

    const auto json = ProjectSerializer::toJson (project);
    const auto& tracks = requireArray (requireField (json, "tracks"), "tracks");
    REQUIRE (tracks.size() == 1);
    const auto& clips = requireArray (requireField (tracks[0], "clips"), "clips");
    REQUIRE (clips.size() == 1);
    const auto& expressionJson = requireField (clips[0], "expression");
    CHECK (requireArray (requireField (expressionJson, "lanes"), "expression lanes").size() == 3);

    const auto loaded = ProjectSerializer::fromJson (json);
    REQUIRE (loaded.tracks().size() == 1);
    REQUIRE (loaded.tracks()[0].clips().size() == 1);
    const auto& loadedExpression = loaded.tracks()[0].clips()[0].expressionState();

    const auto* loadedVolumeLane = loadedExpression.findLane (ExpressionState::defaultVolumeLaneId());
    REQUIRE (loadedVolumeLane != nullptr);
    CHECK (loadedVolumeLane->name() == "Clip Volume");
    REQUIRE (loadedVolumeLane->routes().size() == 1);
    CHECK_FALSE (loadedVolumeLane->routes()[0].enabled());
    CHECK (loadedVolumeLane->routes()[0].outputMin() == 0.85);
    CHECK (loadedVolumeLane->routes()[0].outputMax() == 0.15);
    CHECK (loadedVolumeLane->routes()[0].destination().parameterId == "amp.level");
    REQUIRE (loadedVolumeLane->phraseEnvelopeClips().size() == 1);
    const auto& loadedEnvelope = loadedVolumeLane->phraseEnvelopeClips()[0];
    CHECK (loadedEnvelope.id() == ExpressionClipId { "env-1" });
    REQUIRE (loadedEnvelope.decayStage().has_value());
    CHECK (loadedEnvelope.decayStage()->curveShape == ExpressionCurveShape::logarithmic);
    REQUIRE (loadedEnvelope.releaseStage().has_value());
    REQUIRE (loadedEnvelope.peakLevel().has_value());
    CHECK (*loadedEnvelope.peakLevel() == 0.9);
    REQUIRE (loadedEnvelope.tailExtension().has_value());
    CHECK (*loadedEnvelope.tailExtension() == beats (1));

    const auto* loadedPitchLane = loadedExpression.findLane (ExpressionState::defaultPitchLaneId());
    REQUIRE (loadedPitchLane != nullptr);
    REQUIRE (loadedPitchLane->pitchSlurs().size() == 1);
    CHECK (loadedPitchLane->pitchSlurs()[0].blockId()->value == "slur-block");
    CHECK (loadedPitchLane->pitchSlurs()[0].hasVoiceOverride());
    REQUIRE (loadedPitchLane->vibratoExpressions().size() == 1);
    CHECK (loadedPitchLane->vibratoExpressions()[0].frequencyDivisionId() == "eighth");
    CHECK (loadedPitchLane->vibratoExpressions()[0].waveShape() == CyclicWaveShape::triangle);
    REQUIRE (loadedPitchLane->vibratoExpressions()[0].blockId().has_value());
    CHECK (loadedPitchLane->vibratoExpressions()[0].blockId()->value == "vib-block");
    REQUIRE (loadedPitchLane->vibratoExpressions()[0].voiceOverrides().size() == 1);
    const auto& loadedVibratoOverride = loadedPitchLane->vibratoExpressions()[0].voiceOverrides()[0];
    CHECK (loadedVibratoOverride.noteId == "note-2");
    CHECK (loadedVibratoOverride.amplitudeSemitones == 0.25);
    CHECK (loadedVibratoOverride.attackTime == beats (1));
    CHECK (loadedVibratoOverride.releaseTime == beats (1));
    CHECK (loadedVibratoOverride.frequencyDivisionId == "sixteenth");
    CHECK (loadedVibratoOverride.waveShape == CyclicWaveShape::sine);
    CHECK (loadedVibratoOverride.phase == 0.5);

    const auto* loadedMotionLane = loadedExpression.findLane (ExpressionLaneId { "expr-motion" });
    REQUIRE (loadedMotionLane != nullptr);
    CHECK_FALSE (loadedMotionLane->enabled());
    CHECK (loadedMotionLane->polarity() == ExpressionLanePolarity::bipolar);
    REQUIRE (loadedMotionLane->routes().size() == 1);
    CHECK (loadedMotionLane->routes()[0].destination().midiCcNumber == 74);
    REQUIRE (loadedMotionLane->cyclicClips().size() == 1);
    CHECK (loadedMotionLane->cyclicClips()[0].blendMode() == CyclicBlendMode::multiplicative);
    CHECK (loadedMotionLane->cyclicClips()[0].wavePolarityMode() == CyclicWavePolarityMode::halfWaveRectified);
}

TEST_CASE ("Projects without expression JSON load clips with default expression lanes")
{
    auto json = ProjectSerializer::toJson (makeProjectWithData());
    auto& tracks = json.asObject().at ("tracks").asArray();
    REQUIRE (tracks.size() == 1);
    auto& clips = tracks[0].asObject().at ("clips").asArray();
    REQUIRE (clips.size() == 1);
    clips[0].asObject().erase ("expression");

    const auto loaded = ProjectSerializer::fromJson (std::move (json));

    REQUIRE (loaded.tracks().size() == 1);
    REQUIRE (loaded.tracks()[0].clips().size() == 1);
    const auto& expression = loaded.tracks()[0].clips()[0].expressionState();
    REQUIRE (expression.lanes().size() == 2);
    REQUIRE (expression.findLane (ExpressionState::defaultVolumeLaneId()) != nullptr);
    REQUIRE (expression.findLane (ExpressionState::defaultPitchLaneId()) != nullptr);
    CHECK (expression.findLane (ExpressionState::defaultVolumeLaneId())->name() == "Volume");
    CHECK (expression.findLane (ExpressionState::defaultPitchLaneId())->name() == "Pitch");
}

TEST_CASE ("Legacy schema v1 migrates track plugin references into device chains")
{
    auto legacyJson = ProjectSerializer::toJson (makeProjectWithData());
    legacyJson.asObject()["schemaVersion"] = JsonValue::number (1.0);
    legacyJson.asObject().erase ("audioAssets");

    for (auto& track : legacyJson.asObject().at ("tracks").asArray())
    {
        auto& trackObject = track.asObject();
        trackObject.erase ("type");
        trackObject.erase ("mixerStrip");
        trackObject.erase ("routing");
        trackObject.erase ("deviceChain");
        trackObject.erase ("audioClips");
        trackObject.erase ("automationLanes");
    }

    const auto loaded = ProjectSerializer::fromJson (std::move (legacyJson));

    REQUIRE (loaded.tracks().size() == 1);
    const auto& track = loaded.tracks()[0];
    CHECK (track.type() == TrackType::midi);
    REQUIRE (track.instrument().has_value());
    CHECK (track.instrument()->uniqueIdentifier == "round-trip-piano");
    REQUIRE (track.deviceChain().slots().size() == 1);
    CHECK (track.deviceChain().slots()[0].id() == DeviceSlotId { "instrument" });
    CHECK (track.deviceChain().slots()[0].kind() == PluginKind::instrument);
    CHECK (track.deviceChain().slots()[0].pluginStateFile() == "plugin-states/track-1.vststate");

    const auto migratedJson = ProjectSerializer::toJson (loaded);
    CHECK (requireInt (requireField (migratedJson, "schemaVersion"), "schema version") == currentProjectSchemaVersion);
}

TEST_CASE ("Project serializer surfaces recoverable route and automation warnings")
{
    Project project { "project-1", "Warning Song" };
    Track track { "track-1", "Piano" };

    TrackRouting routing;
    routing.setAudioTo (RouteEndpoint::track ("missing-track"));
    track.setRouting (routing);

    AutomationCurve curve;
    curve.addPoint (AutomationPoint { beat (0), 0.5 });
    track.setAutomationLane (AutomationLane {
        AutomationTarget::pluginParameter ("track-1", DeviceSlotId { "missing-device" }, "cutoff"),
        curve
    });

    project.addTrack (std::move (track));

    const auto result = ProjectSerializer::deserializeWithWarnings (ProjectSerializer::serialize (project));
    CHECK (result.project.findTrackById ("track-1") != nullptr);
    CHECK (containsWarning (result.warnings, "Routing warning"));
    CHECK (containsWarning (result.warnings, "missing device slot"));
}

TEST_CASE ("Project package load surfaces missing asset and device-state warnings")
{
    const auto packagePath = uniquePackagePath();
    std::filesystem::remove_all (packagePath);
    std::filesystem::create_directories (packagePath);

    Project project { "project-1", "Missing Assets" };
    Track midiTrack { "track-1", "Piano" };
    DeviceChain chain;
    chain.appendSlot (instrumentSlot());
    midiTrack.setDeviceChain (chain);
    project.addTrack (std::move (midiTrack));

    Track audioTrack { "audio-1", "Audio", TrackType::audio };
    audioTrack.addAudioClip (AudioClip { "audio-clip-1", "Missing Audio", audioSource ("assets/missing.wav"), beat (0), beats (4) });
    project.addTrack (std::move (audioTrack));

    {
        std::ofstream stream { ProjectPackage::projectJsonPath (packagePath) };
        stream << ProjectSerializer::serialize (project);
    }

    const auto result = ProjectPackage::loadWithWarnings (packagePath);
    CHECK (result.project.findTrackById ("track-1") != nullptr);
    CHECK (containsWarning (result.warnings, "Missing plugin state file"));
    CHECK (containsWarning (result.warnings, "Missing audio source"));

    std::filesystem::remove_all (packagePath);
}

TEST_CASE ("Missing plugin reference is preserved during serialization")
{
    auto project = makeProjectWithData();
    auto* track = project.findTrackById ("track-1");
    REQUIRE (track != nullptr);
    REQUIRE (track->instrument().has_value());

    auto instrument = *track->instrument();
    instrument.fileOrIdentifier = "/missing/Not Installed.vst3";
    instrument.uniqueIdentifier = "missing-plugin";
    instrument.pluginStateFile = "plugin-states/missing.vststate";
    track->setInstrument (instrument);

    const auto loaded = ProjectSerializer::deserialize (ProjectSerializer::serialize (project));

    REQUIRE (loaded.tracks().size() == 1);
    REQUIRE (loaded.tracks()[0].instrument().has_value());
    CHECK (loaded.tracks()[0].instrument()->fileOrIdentifier == "/missing/Not Installed.vst3");
    CHECK (loaded.tracks()[0].instrument()->uniqueIdentifier == "missing-plugin");
    CHECK (loaded.tracks()[0].instrument()->pluginStateFile == "plugin-states/missing.vststate");
    CHECK (loaded.tracks()[0].clips().size() == 1);
}

TEST_CASE ("Invalid schema version returns a clear error")
{
    auto json = ProjectSerializer::toJson (Project { "project-1", "Song" });
    json.asObject()["schemaVersion"] = JsonValue::number (999.0);

    try
    {
        static_cast<void> (ProjectSerializer::fromJson (json));
        FAIL ("Expected invalid schema version to throw");
    }
    catch (const std::exception& exception)
    {
        CHECK (std::string { exception.what() }.find ("Unsupported project schema version") != std::string::npos);
    }
}

TEST_CASE ("Missing required field returns a clear error")
{
    auto json = ProjectSerializer::toJson (Project { "project-1", "Song" });
    json.asObject().erase ("tracks");

    try
    {
        static_cast<void> (ProjectSerializer::fromJson (json));
        FAIL ("Expected missing field to throw");
    }
    catch (const std::exception& exception)
    {
        CHECK (std::string { exception.what() }.find ("Missing required field 'tracks'") != std::string::npos);
    }
}

TEST_CASE ("Project package creates expected folder structure and loads")
{
    const auto packagePath = uniquePackagePath();
    std::filesystem::remove_all (packagePath);

    ProjectPackage::save (makeProjectWithData(), packagePath);

    CHECK (std::filesystem::is_directory (packagePath));
    CHECK (std::filesystem::is_regular_file (packagePath / "project.json"));
    CHECK (std::filesystem::is_directory (packagePath / "plugin-states"));
    CHECK (std::filesystem::is_regular_file (packagePath / "plugin-states" / "track-1.vststate"));
    CHECK (std::filesystem::is_directory (packagePath / "assets"));
    CHECK (std::filesystem::is_directory (packagePath / "exports"));
    CHECK (std::filesystem::is_directory (packagePath / "waveform-cache"));

    const auto loaded = ProjectPackage::load (packagePath);
    CHECK (loaded.name() == "Round Trip Song");
    REQUIRE (loaded.tracks().size() == 1);
    CHECK (loaded.tracks()[0].clips().size() == 1);

    std::filesystem::remove_all (packagePath);
}

TEST_CASE ("MVP project package preserves clip data accidental visibility and MIDI export")
{
    const auto packagePath = uniquePackagePath();
    std::filesystem::remove_all (packagePath);

    ProjectPackage::save (makeProjectWithData(), packagePath);
    const auto loaded = ProjectPackage::load (packagePath);

    REQUIRE (loaded.tracks().size() == 1);
    const auto& track = loaded.tracks()[0];
    REQUIRE (track.instrument().has_value());
    CHECK (track.instrument()->uniqueIdentifier == "round-trip-piano");

    REQUIRE (track.clips().size() == 1);
    const auto& clip = track.clips()[0];
    REQUIRE (clip.findNoteById ("note-1") != nullptr);
    REQUIRE (clip.findNoteById ("note-2") != nullptr);
    REQUIRE (clip.findNoteById ("note-2")->spelling().has_value());
    CHECK (clip.findNoteById ("note-2")->spelling()->toString() == "F#");
    CHECK (loaded.musicalStructure().keyCenterRegions().size() == 2);
    CHECK (loaded.musicalStructure().scaleModeRegions().size() == 2);

    const ScaleLibrary scales;
    const auto lanes = visibleLanesForClip (clip, Region { beat (0), beat (4) }, scales, false);
    const auto* fSharp = findLane (lanes, PitchClass::fSharp());
    REQUIRE (fSharp != nullptr);
    CHECK (fSharp->status == PitchLaneStatus::usedAccidental);

    MidiExportOptions exportOptions;
    exportOptions.tempo = loaded.tempoMap().tempoAt (clip.startInProject());
    exportOptions.timeSignature = loaded.timeSignatureMap().timeSignatureAt (clip.startInProject());

    const auto exportPath = packagePath / "exports" / "mvp-clip.mid";
    const auto exportResult = MidiExporter::tryExportClipToFile (clip, exportPath, exportOptions);
    CHECK (exportResult.succeeded());
    CHECK (std::filesystem::is_regular_file (exportPath));
    CHECK (std::filesystem::file_size (exportPath) > 0);

    std::filesystem::remove_all (packagePath);
}

TEST_CASE ("Invalid project package JSON reports a clear load error")
{
    const auto packagePath = uniquePackagePath();
    std::filesystem::remove_all (packagePath);
    std::filesystem::create_directories (packagePath);

    {
        std::ofstream stream { packagePath / "project.json" };
        stream << "{ invalid json";
    }

    try
    {
        static_cast<void> (ProjectPackage::load (packagePath));
        FAIL ("Expected invalid project package JSON to throw");
    }
    catch (const std::exception& exception)
    {
        CHECK_FALSE (std::string { exception.what() }.empty());
        CHECK (std::string { exception.what() }.find ("JSON") != std::string::npos);
    }

    std::filesystem::remove_all (packagePath);
}
