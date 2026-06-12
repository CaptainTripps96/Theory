#include "core/serialization/ProjectSerializer.h"

#include "core/serialization/ProjectMigration.h"
#include "core/serialization/ProjectSchemaVersion.h"
#include "core/time/GridDivision.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace tsq::core::serialization
{
namespace
{
using namespace music_theory;
using namespace sequencing;
using namespace time;

JsonValue numberFromTicks (std::int64_t ticks)
{
    return JsonValue::number (static_cast<double> (ticks));
}

JsonValue decibelsToJson (double decibels)
{
    if (MixerStrip::isSilenceDb (decibels))
        return JsonValue::string ("-inf");

    return JsonValue::number (decibels);
}

double decibelsFromJson (const JsonValue& value, const std::string& description)
{
    if (value.isString())
    {
        if (value.asString() == "-inf")
            return MixerStrip::silenceDb();

        throw std::runtime_error ("Unknown " + description + " value '" + value.asString() + "'");
    }

    return value.asNumber();
}

TickPosition tickPositionFromJson (const JsonValue& value, const std::string& description)
{
    return TickPosition::fromTicks (requireInt64 (value, description));
}

TickDuration tickDurationFromJson (const JsonValue& value, const std::string& description)
{
    return TickDuration::fromTicks (requireInt64 (value, description));
}

JsonValue regionToJson (const Region& region)
{
    return JsonValue::object ({
        { "startTick", numberFromTicks (region.start().ticks()) },
        { "endTick", numberFromTicks (region.end().ticks()) },
    });
}

Region regionFromJson (const JsonValue& value, const std::string& description)
{
    return Region {
        tickPositionFromJson (requireField (value, "startTick"), description + ".startTick"),
        tickPositionFromJson (requireField (value, "endTick"), description + ".endTick")
    };
}

std::string spellingPreferenceToString (SpellingPreference preference)
{
    switch (preference)
    {
        case SpellingPreference::preferSharps: return "preferSharps";
        case SpellingPreference::preferFlats: return "preferFlats";
    }

    return "preferSharps";
}

SpellingPreference spellingPreferenceFromJson (const JsonValue& value)
{
    const auto preference = requireString (value, "preferredSpelling");
    if (preference == "preferSharps")
        return SpellingPreference::preferSharps;
    if (preference == "preferFlats")
        return SpellingPreference::preferFlats;

    throw std::runtime_error ("Unknown spelling preference '" + preference + "'");
}

JsonValue scaleDegreeToJson (ScaleDegree degree)
{
    return JsonValue::object ({
        { "degree", JsonValue::number (static_cast<double> (degree.degree())) },
        { "alteration", JsonValue::number (static_cast<double> (degree.alteration())) },
    });
}

ScaleDegree scaleDegreeFromJson (const JsonValue& value)
{
    return ScaleDegree {
        requireInt (requireField (value, "degree"), "scale degree"),
        requireInt (requireField (value, "alteration"), "scale degree alteration")
    };
}

JsonValue scaleDefinitionToJson (const ScaleDefinition& scale)
{
    JsonValue::Array offsets;
    for (const auto offset : scale.pitchClassOffsetsFromRoot())
        offsets.push_back (JsonValue::number (static_cast<double> (offset)));

    JsonValue::Array degrees;
    for (const auto degree : scale.degreeMapping())
        degrees.push_back (scaleDegreeToJson (degree));

    return JsonValue::object ({
        { "name", JsonValue::string (scale.name()) },
        { "category", JsonValue::string (scale.category()) },
        { "tags", JsonValue::array (stringArrayToJson (scale.tags())) },
        { "description", JsonValue::string (scale.description()) },
        { "pitchClassOffsetsFromRoot", JsonValue::array (std::move (offsets)) },
        { "degreeMapping", JsonValue::array (std::move (degrees)) },
        { "preferredSpelling", JsonValue::string (spellingPreferenceToString (scale.preferredSpelling())) },
    });
}

ScaleDefinition scaleDefinitionFromJson (const JsonValue& value)
{
    ScaleMetadata metadata {
        requireString (requireField (value, "name"), "scale name"),
        requireString (requireField (value, "category"), "scale category"),
        stringArrayFromJson (requireField (value, "tags"), "scale tags"),
        requireString (requireField (value, "description"), "scale description")
    };

    std::vector<int> offsets;
    for (const auto& offset : requireArray (requireField (value, "pitchClassOffsetsFromRoot"), "scale pitch-class offsets"))
        offsets.push_back (requireInt (offset, "scale pitch-class offset"));

    std::vector<ScaleDegree> degrees;
    for (const auto& degree : requireArray (requireField (value, "degreeMapping"), "scale degree mapping"))
        degrees.push_back (scaleDegreeFromJson (degree));

    return ScaleDefinition {
        std::move (metadata),
        std::move (offsets),
        std::move (degrees),
        spellingPreferenceFromJson (requireField (value, "preferredSpelling"))
    };
}

std::string letterToString (LetterName letter)
{
    switch (letter)
    {
        case LetterName::c: return "c";
        case LetterName::d: return "d";
        case LetterName::e: return "e";
        case LetterName::f: return "f";
        case LetterName::g: return "g";
        case LetterName::a: return "a";
        case LetterName::b: return "b";
    }

    return "c";
}

LetterName letterFromString (const std::string& letter)
{
    if (letter == "c") return LetterName::c;
    if (letter == "d") return LetterName::d;
    if (letter == "e") return LetterName::e;
    if (letter == "f") return LetterName::f;
    if (letter == "g") return LetterName::g;
    if (letter == "a") return LetterName::a;
    if (letter == "b") return LetterName::b;

    throw std::runtime_error ("Unknown note letter '" + letter + "'");
}

JsonValue noteNameToJson (NoteName noteName)
{
    return JsonValue::object ({
        { "letter", JsonValue::string (letterToString (noteName.letter())) },
        { "accidental", JsonValue::number (static_cast<double> (noteName.accidental().semitoneOffset())) },
    });
}

NoteName noteNameFromJson (const JsonValue& value)
{
    return NoteName {
        letterFromString (requireString (requireField (value, "letter"), "note letter")),
        Accidental { requireInt (requireField (value, "accidental"), "note accidental") }
    };
}

JsonValue harmonicContextToJson (const HarmonicContext& context)
{
    return JsonValue::object ({
        { "keyCenterPitchClass", JsonValue::number (static_cast<double> (context.keyCenter().semitonesFromC())) },
        { "scaleDefinitionName", JsonValue::string (context.scaleDefinitionName()) },
    });
}

HarmonicContext harmonicContextFromJson (const JsonValue& value)
{
    return HarmonicContext {
        PitchClass::fromSemitonesFromC (requireInt (requireField (value, "keyCenterPitchClass"), "key center pitch class")),
        requireString (requireField (value, "scaleDefinitionName"), "scale definition name")
    };
}

JsonValue noteHarmonicInterpretationToJson (const NoteHarmonicInterpretation& interpretation)
{
    return JsonValue::object ({
        { "sourceContext", harmonicContextToJson (interpretation.sourceContext) },
        { "scaleDegreeIndex", JsonValue::number (static_cast<double> (interpretation.scaleDegreeIndex)) },
        { "alteration", JsonValue::number (static_cast<double> (interpretation.alteration)) },
    });
}

NoteHarmonicInterpretation noteHarmonicInterpretationFromJson (const JsonValue& value)
{
    const auto scaleDegreeIndex = requireInt (requireField (value, "scaleDegreeIndex"), "note harmonic interpretation scale degree index");
    if (scaleDegreeIndex < 0)
        throw std::runtime_error ("Note harmonic interpretation scale degree index must not be negative");

    return NoteHarmonicInterpretation {
        harmonicContextFromJson (requireField (value, "sourceContext")),
        static_cast<std::size_t> (scaleDegreeIndex),
        requireInt (requireField (value, "alteration"), "note harmonic interpretation alteration")
    };
}

std::string chordQualityToString (ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::major: return "major";
        case ChordQuality::minor: return "minor";
        case ChordQuality::diminished: return "diminished";
        case ChordQuality::augmented: return "augmented";
        case ChordQuality::suspendedSecond: return "suspendedSecond";
        case ChordQuality::suspendedFourth: return "suspendedFourth";
        case ChordQuality::power: return "power";
        case ChordQuality::majorSeventh: return "majorSeventh";
        case ChordQuality::dominantSeventh: return "dominantSeventh";
        case ChordQuality::minorSeventh: return "minorSeventh";
        case ChordQuality::halfDiminishedSeventh: return "halfDiminishedSeventh";
        case ChordQuality::diminishedSeventh: return "diminishedSeventh";
        case ChordQuality::minorMajorSeventh: return "minorMajorSeventh";
        case ChordQuality::addNine: return "addNine";
    }

    return "major";
}

ChordQuality chordQualityFromString (const std::string& quality)
{
    if (quality == "major") return ChordQuality::major;
    if (quality == "minor") return ChordQuality::minor;
    if (quality == "diminished") return ChordQuality::diminished;
    if (quality == "augmented") return ChordQuality::augmented;
    if (quality == "suspendedSecond") return ChordQuality::suspendedSecond;
    if (quality == "suspendedFourth") return ChordQuality::suspendedFourth;
    if (quality == "power") return ChordQuality::power;
    if (quality == "majorSeventh") return ChordQuality::majorSeventh;
    if (quality == "dominantSeventh") return ChordQuality::dominantSeventh;
    if (quality == "minorSeventh") return ChordQuality::minorSeventh;
    if (quality == "halfDiminishedSeventh") return ChordQuality::halfDiminishedSeventh;
    if (quality == "diminishedSeventh") return ChordQuality::diminishedSeventh;
    if (quality == "minorMajorSeventh") return ChordQuality::minorMajorSeventh;
    if (quality == "addNine") return ChordQuality::addNine;

    throw std::runtime_error ("Unknown chord quality '" + quality + "'");
}

JsonValue pitchClassArrayToJson (const std::vector<PitchClass>& pitchClasses)
{
    JsonValue::Array result;
    for (const auto pitchClass : pitchClasses)
        result.push_back (JsonValue::number (static_cast<double> (pitchClass.semitonesFromC())));

    return JsonValue::array (std::move (result));
}

std::vector<PitchClass> pitchClassArrayFromJson (const JsonValue& value, const std::string& description)
{
    std::vector<PitchClass> result;
    for (const auto& pitchClass : requireArray (value, description))
        result.push_back (PitchClass::fromSemitonesFromC (requireInt (pitchClass, description + " pitch class")));

    return result;
}

JsonValue clipHarmonicMapToJson (const ClipHarmonicMap& map)
{
    JsonValue::Array regions;
    for (const auto& region : map.regions())
    {
        regions.push_back (JsonValue::object ({
            { "region", regionToJson (region.region) },
            { "originalContext", harmonicContextToJson (region.originalContext) },
        }));
    }

    return JsonValue::object ({
        { "defaultContext", harmonicContextToJson (map.defaultContext()) },
        { "regions", JsonValue::array (std::move (regions)) },
    });
}

ClipHarmonicMap clipHarmonicMapFromJson (const JsonValue& value)
{
    ClipHarmonicMap map { harmonicContextFromJson (requireField (value, "defaultContext")) };

    for (const auto& region : requireArray (requireField (value, "regions"), "clip harmonic metadata regions"))
    {
        map.addRegion (ClipHarmonicRegion {
            regionFromJson (requireField (region, "region"), "clip harmonic metadata region"),
            harmonicContextFromJson (requireField (region, "originalContext"))
        });
    }

    return map;
}

JsonValue midiNoteToJson (const MidiNote& note)
{
    auto object = JsonValue::Object {
        { "id", JsonValue::string (note.id()) },
        { "pitch", JsonValue::number (static_cast<double> (note.pitch().value())) },
        { "startTick", numberFromTicks (note.startInClip().ticks()) },
        { "durationTicks", numberFromTicks (note.duration().ticks()) },
        { "velocity", JsonValue::number (static_cast<double> (note.velocity())) },
    };

    object.emplace ("spelling", note.spelling().has_value() ? noteNameToJson (*note.spelling()) : JsonValue::null());
    object.emplace ("harmonicInterpretation",
                    note.harmonicInterpretation().has_value()
                        ? noteHarmonicInterpretationToJson (*note.harmonicInterpretation())
                        : JsonValue::null());
    return JsonValue::object (std::move (object));
}

MidiNote midiNoteFromJson (const JsonValue& value)
{
    const auto& object = requireObject (value, "MIDI note");

    std::optional<NoteName> spelling;
    const auto& spellingJson = requireField (value, "spelling");
    if (! spellingJson.isNull())
        spelling = noteNameFromJson (spellingJson);

    std::optional<NoteHarmonicInterpretation> harmonicInterpretation;
    if (const auto interpretation = object.find ("harmonicInterpretation");
        interpretation != object.end() && ! interpretation->second.isNull())
    {
        harmonicInterpretation = noteHarmonicInterpretationFromJson (interpretation->second);
    }

    return MidiNote {
        requireString (requireField (value, "id"), "note ID"),
        MidiPitch::fromValue (requireInt (requireField (value, "pitch"), "MIDI pitch")),
        tickPositionFromJson (requireField (value, "startTick"), "note start tick"),
        tickDurationFromJson (requireField (value, "durationTicks"), "note duration ticks"),
        requireInt (requireField (value, "velocity"), "note velocity"),
        spelling,
        harmonicInterpretation
    };
}

JsonValue tempoMapToJson (const TempoMap& map)
{
    JsonValue::Array nodes;
    for (const auto& node : map.nodes())
    {
        nodes.push_back (JsonValue::object ({
            { "positionTick", numberFromTicks (node.position.ticks()) },
            { "bpm", JsonValue::number (node.tempo.bpm()) },
        }));
    }

    return JsonValue::object ({
        { "nodes", JsonValue::array (std::move (nodes)) },
    });
}

TempoMap tempoMapFromJson (const JsonValue& value)
{
    TempoMap map;
    for (const auto& node : requireArray (requireField (value, "nodes"), "tempo nodes"))
    {
        map.addNode (
            tickPositionFromJson (requireField (node, "positionTick"), "tempo node position tick"),
            Tempo { requireField (node, "bpm").asNumber() });
    }

    return map;
}

JsonValue timeSignatureMapToJson (const TimeSignatureMap& map)
{
    JsonValue::Array markers;
    for (const auto& marker : map.markers())
    {
        markers.push_back (JsonValue::object ({
            { "positionTick", numberFromTicks (marker.position.ticks()) },
            { "numerator", JsonValue::number (static_cast<double> (marker.timeSignature.numerator())) },
            { "denominator", JsonValue::number (static_cast<double> (marker.timeSignature.denominator())) },
        }));
    }

    return JsonValue::object ({
        { "markers", JsonValue::array (std::move (markers)) },
    });
}

TimeSignatureMap timeSignatureMapFromJson (const JsonValue& value)
{
    TimeSignatureMap map;
    for (const auto& marker : requireArray (requireField (value, "markers"), "time signature markers"))
    {
        map.addMarker (
            tickPositionFromJson (requireField (marker, "positionTick"), "time signature marker position tick"),
            TimeSignature {
                requireInt (requireField (marker, "numerator"), "time signature numerator"),
                requireInt (requireField (marker, "denominator"), "time signature denominator")
            });
    }

    return map;
}

JsonValue rhythmSettingsToJson (const ProjectRhythmSettings& settings)
{
    return JsonValue::object ({
        { "currentGridDivisionId", JsonValue::string (settings.currentGridDivisionId()) },
        { "tuplets", JsonValue::object ({
            { "tripletsEnabled", JsonValue::boolean (settings.tripletsEnabled()) },
            { "quintupletsEnabled", JsonValue::boolean (settings.quintupletsEnabled()) },
            { "septupletsEnabled", JsonValue::boolean (settings.septupletsEnabled()) },
            { "nonupletsEnabled", JsonValue::boolean (settings.nonupletsEnabled()) },
        }) },
    });
}

ProjectRhythmSettings rhythmSettingsFromJson (const JsonValue& value)
{
    ProjectRhythmSettings settings;
    const auto& object = requireObject (value, "rhythmic settings");

    if (const auto grid = object.find ("currentGridDivisionId"); grid != object.end())
        settings.setCurrentGridDivisionId (requireString (grid->second, "current grid division ID"));

    if (const auto tuplets = object.find ("tuplets"); tuplets != object.end())
    {
        const auto& tupletObject = requireObject (tuplets->second, "rhythmic settings tuplets");
        if (const auto enabled = tupletObject.find ("tripletsEnabled"); enabled != tupletObject.end())
            settings.setTripletsEnabled (requireBool (enabled->second, "triplets enabled"));
        if (const auto enabled = tupletObject.find ("quintupletsEnabled"); enabled != tupletObject.end())
            settings.setQuintupletsEnabled (requireBool (enabled->second, "quintuplets enabled"));
        if (const auto enabled = tupletObject.find ("septupletsEnabled"); enabled != tupletObject.end())
            settings.setSeptupletsEnabled (requireBool (enabled->second, "septuplets enabled"));
        if (const auto enabled = tupletObject.find ("nonupletsEnabled"); enabled != tupletObject.end())
            settings.setNonupletsEnabled (requireBool (enabled->second, "nonuplets enabled"));
    }
    else if (const auto legacyTuplets = object.find ("tupletsEnabled"); legacyTuplets != object.end())
    {
        settings.setTripletsEnabled (requireBool (legacyTuplets->second, "legacy tuplets enabled"));
    }

    const auto currentGrid = gridDivisionDefinitionFor (settings.currentGridDivisionId(), settings);
    settings.setCurrentGridDivisionId (currentGrid.id);
    return settings;
}

JsonValue clipLoopToJson (const ClipLoop& loop)
{
    return JsonValue::object ({
        { "enabled", JsonValue::boolean (loop.isEnabled()) },
        { "durationTicks", numberFromTicks (loop.loopDuration().ticks()) },
    });
}

ClipLoop clipLoopFromJson (const JsonValue& value)
{
    const auto enabled = requireBool (requireField (value, "enabled"), "clip loop enabled");
    const auto duration = tickDurationFromJson (requireField (value, "durationTicks"), "clip loop duration ticks");
    return enabled ? ClipLoop::enabled (duration) : ClipLoop::disabled();
}

JsonValue midiClipToJson (const MidiClip& clip)
{
    JsonValue::Array notes;
    for (const auto& note : clip.notes())
        notes.push_back (midiNoteToJson (note));

    return JsonValue::object ({
        { "id", JsonValue::string (clip.id()) },
        { "name", JsonValue::string (clip.name()) },
        { "startTick", numberFromTicks (clip.startInProject().ticks()) },
        { "lengthTicks", numberFromTicks (clip.length().ticks()) },
        { "loop", clipLoopToJson (clip.loop()) },
        { "notes", JsonValue::array (std::move (notes)) },
        { "harmonicMetadata", clipHarmonicMapToJson (clip.harmonicMetadata()) },
    });
}

MidiClip midiClipFromJson (const JsonValue& value)
{
    MidiClip clip {
        requireString (requireField (value, "id"), "clip ID"),
        requireString (requireField (value, "name"), "clip name"),
        tickPositionFromJson (requireField (value, "startTick"), "clip start tick"),
        tickDurationFromJson (requireField (value, "lengthTicks"), "clip length ticks"),
        clipLoopFromJson (requireField (value, "loop"))
    };

    clip.setHarmonicMetadata (clipHarmonicMapFromJson (requireField (value, "harmonicMetadata")));

    for (const auto& note : requireArray (requireField (value, "notes"), "clip notes"))
        clip.addNote (midiNoteFromJson (note));

    return clip;
}

JsonValue trackInstrumentToJson (const TrackInstrumentReference& instrument)
{
    return JsonValue::object ({
        { "pluginName", JsonValue::string (instrument.pluginName) },
        { "manufacturer", JsonValue::string (instrument.manufacturer) },
        { "format", JsonValue::string (instrument.format) },
        { "fileOrIdentifier", JsonValue::string (instrument.fileOrIdentifier) },
        { "uniqueIdentifier", JsonValue::string (instrument.uniqueIdentifier) },
        { "uniqueId", JsonValue::number (static_cast<double> (instrument.uniqueId)) },
        { "deprecatedUid", JsonValue::number (static_cast<double> (instrument.deprecatedUid)) },
        { "isInstrument", JsonValue::boolean (instrument.isInstrument) },
        { "numInputChannels", JsonValue::number (static_cast<double> (instrument.numInputChannels)) },
        { "numOutputChannels", JsonValue::number (static_cast<double> (instrument.numOutputChannels)) },
        { "pluginStateFile", JsonValue::string (instrument.pluginStateFile) },
    });
}

TrackInstrumentReference trackInstrumentFromJson (const JsonValue& value)
{
    const auto& object = requireObject (value, "track instrument reference");
    auto reference = TrackInstrumentReference {
        requireString (requireField (value, "pluginName"), "track instrument plugin name"),
        requireString (requireField (value, "manufacturer"), "track instrument manufacturer"),
        requireString (requireField (value, "format"), "track instrument format"),
        requireString (requireField (value, "fileOrIdentifier"), "track instrument file or identifier"),
        requireString (requireField (value, "uniqueIdentifier"), "track instrument unique identifier"),
        requireInt (requireField (value, "uniqueId"), "track instrument unique ID"),
        requireInt (requireField (value, "deprecatedUid"), "track instrument deprecated UID"),
        requireBool (requireField (value, "isInstrument"), "track instrument marker"),
        requireInt (requireField (value, "numInputChannels"), "track instrument input channel count"),
        requireInt (requireField (value, "numOutputChannels"), "track instrument output channel count"),
        {}
    };

    if (const auto stateFile = object.find ("pluginStateFile"); stateFile != object.end())
        reference.pluginStateFile = requireString (stateFile->second, "track instrument plugin state file");

    return reference;
}

JsonValue pluginReferenceToJson (const PluginReference& plugin)
{
    return JsonValue::object ({
        { "pluginName", JsonValue::string (plugin.pluginName) },
        { "manufacturer", JsonValue::string (plugin.manufacturer) },
        { "format", JsonValue::string (plugin.format) },
        { "fileOrIdentifier", JsonValue::string (plugin.fileOrIdentifier) },
        { "uniqueIdentifier", JsonValue::string (plugin.uniqueIdentifier) },
        { "uniqueId", JsonValue::number (static_cast<double> (plugin.uniqueId)) },
        { "deprecatedUid", JsonValue::number (static_cast<double> (plugin.deprecatedUid)) },
        { "numInputChannels", JsonValue::number (static_cast<double> (plugin.numInputChannels)) },
        { "numOutputChannels", JsonValue::number (static_cast<double> (plugin.numOutputChannels)) },
    });
}

PluginReference pluginReferenceFromJson (const JsonValue& value)
{
    return PluginReference {
        requireString (requireField (value, "pluginName"), "plugin name"),
        requireString (requireField (value, "manufacturer"), "plugin manufacturer"),
        requireString (requireField (value, "format"), "plugin format"),
        requireString (requireField (value, "fileOrIdentifier"), "plugin file or identifier"),
        requireString (requireField (value, "uniqueIdentifier"), "plugin unique identifier"),
        requireInt (requireField (value, "uniqueId"), "plugin unique ID"),
        requireInt (requireField (value, "deprecatedUid"), "plugin deprecated UID"),
        requireInt (requireField (value, "numInputChannels"), "plugin input channel count"),
        requireInt (requireField (value, "numOutputChannels"), "plugin output channel count")
    };
}

std::optional<TrackInstrumentReference> legacyInstrumentForSerialization (const Track& track)
{
    if (track.instrument().has_value())
        return *track.instrument();

    for (const auto& slot : track.deviceChain().slots())
        if (slot.kind() == PluginKind::instrument)
            return slot.plugin().toTrackInstrumentReference (slot.pluginStateFile());

    return std::nullopt;
}

DeviceChain deviceChainForSerialization (const Track& track)
{
    auto chain = track.deviceChain();
    if (! chain.empty() || ! track.instrument().has_value())
        return chain;

    DeviceSlot instrumentSlot {
        DeviceSlotId { "instrument" },
        PluginReference::fromTrackInstrumentReference (*track.instrument()),
        PluginKind::instrument
    };
    instrumentSlot.setPluginStateFile (track.instrument()->pluginStateFile);
    chain.appendSlot (std::move (instrumentSlot));
    return chain;
}

JsonValue colorToJson (const std::optional<std::uint32_t>& color)
{
    if (! color.has_value())
        return JsonValue::null();

    return JsonValue::number (static_cast<double> (*color));
}

std::optional<std::uint32_t> colorFromJson (const JsonValue& value)
{
    if (value.isNull())
        return std::nullopt;

    const auto color = requireInt64 (value, "track color ARGB");
    if (color < 0 || color > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error ("Track color ARGB is out of range");

    return static_cast<std::uint32_t> (color);
}

JsonValue mixerStripToJson (const MixerStrip& strip)
{
    return JsonValue::object ({
        { "volumeDb", decibelsToJson (strip.volumeDb()) },
        { "linearGain", JsonValue::number (strip.linearGain()) },
        { "pan", JsonValue::number (strip.pan()) },
        { "active", JsonValue::boolean (strip.active()) },
        { "muted", JsonValue::boolean (strip.muted()) },
        { "soloed", JsonValue::boolean (strip.soloed()) },
        { "meterSourceId", JsonValue::string (strip.meterSourceId()) },
        { "colorArgb", colorToJson (strip.colorArgb()) },
    });
}

MixerStrip mixerStripFromJson (const JsonValue& value)
{
    MixerStrip strip;
    strip.setVolumeDb (decibelsFromJson (requireField (value, "volumeDb"), "mixer volume dB"));
    strip.setPan (requireField (value, "pan").asNumber());
    strip.setActive (requireBool (requireField (value, "active"), "track activator"));
    strip.setSoloed (requireBool (requireField (value, "soloed"), "track solo"));
    strip.setMeterSourceId (requireString (requireField (value, "meterSourceId"), "meter source ID"));
    strip.setColorArgb (colorFromJson (requireField (value, "colorArgb")));
    return strip;
}

std::string routeEndpointKindToString (RouteEndpointKind kind)
{
    switch (kind)
    {
        case RouteEndpointKind::none: return "none";
        case RouteEndpointKind::track: return "track";
        case RouteEndpointKind::returnTrack: return "return";
        case RouteEndpointKind::master: return "master";
        case RouteEndpointKind::hardwareOutput: return "hardwareOutput";
        case RouteEndpointKind::sidechain: return "sidechain";
    }

    return "none";
}

RouteEndpointKind routeEndpointKindFromString (const std::string& kind)
{
    if (kind == "none") return RouteEndpointKind::none;
    if (kind == "track") return RouteEndpointKind::track;
    if (kind == "return") return RouteEndpointKind::returnTrack;
    if (kind == "master") return RouteEndpointKind::master;
    if (kind == "hardwareOutput") return RouteEndpointKind::hardwareOutput;
    if (kind == "sidechain") return RouteEndpointKind::sidechain;

    throw std::runtime_error ("Unknown route endpoint kind '" + kind + "'");
}

JsonValue routeEndpointToJson (const RouteEndpoint& endpoint)
{
    return JsonValue::object ({
        { "kind", JsonValue::string (routeEndpointKindToString (endpoint.kind)) },
        { "id", JsonValue::string (endpoint.id) },
        { "label", JsonValue::string (endpoint.label) },
    });
}

RouteEndpoint routeEndpointFromJson (const JsonValue& value)
{
    return RouteEndpoint {
        routeEndpointKindFromString (requireString (requireField (value, "kind"), "route endpoint kind")),
        requireString (requireField (value, "id"), "route endpoint ID"),
        requireString (requireField (value, "label"), "route endpoint label")
    };
}

JsonValue returnSendToJson (const ReturnSend& send)
{
    return JsonValue::object ({
        { "targetReturnTrackId", JsonValue::string (send.targetReturnTrackId) },
        { "normalizedLevel", JsonValue::number (send.normalizedLevel) },
    });
}

ReturnSend returnSendFromJson (const JsonValue& value)
{
    return ReturnSend {
        requireString (requireField (value, "targetReturnTrackId"), "send target return track ID"),
        requireField (value, "normalizedLevel").asNumber()
    };
}

JsonValue trackRoutingToJson (const TrackRouting& routing)
{
    JsonValue::Array sends;
    for (const auto& send : routing.sends())
        sends.push_back (returnSendToJson (send));

    return JsonValue::object ({
        { "audioFrom", routeEndpointToJson (routing.audioFrom()) },
        { "audioTo", routeEndpointToJson (routing.audioTo()) },
        { "midiFrom", routeEndpointToJson (routing.midiFrom()) },
        { "midiTo", routeEndpointToJson (routing.midiTo()) },
        { "sends", JsonValue::array (std::move (sends)) },
    });
}

TrackRouting trackRoutingFromJson (const JsonValue& value)
{
    TrackRouting routing;
    routing.setAudioFrom (routeEndpointFromJson (requireField (value, "audioFrom")));
    routing.setAudioTo (routeEndpointFromJson (requireField (value, "audioTo")));
    routing.setMidiFrom (routeEndpointFromJson (requireField (value, "midiFrom")));
    routing.setMidiTo (routeEndpointFromJson (requireField (value, "midiTo")));

    for (const auto& send : requireArray (requireField (value, "sends"), "return sends"))
        routing.addOrReplaceSend (returnSendFromJson (send));

    return routing;
}

JsonValue deviceSlotToJson (const DeviceSlot& slot)
{
    return JsonValue::object ({
        { "id", JsonValue::string (slot.id().value) },
        { "plugin", pluginReferenceToJson (slot.plugin()) },
        { "kind", JsonValue::string (pluginKindId (slot.kind())) },
        { "bypassed", JsonValue::boolean (slot.bypassed()) },
        { "pluginStateFile", JsonValue::string (slot.pluginStateFile()) },
    });
}

DeviceSlot deviceSlotFromJson (const JsonValue& value)
{
    DeviceSlot slot {
        DeviceSlotId { requireString (requireField (value, "id"), "device slot ID") },
        pluginReferenceFromJson (requireField (value, "plugin")),
        pluginKindFromId (requireString (requireField (value, "kind"), "plugin kind"))
    };
    slot.setBypassed (requireBool (requireField (value, "bypassed"), "device bypass state"));
    slot.setPluginStateFile (requireString (requireField (value, "pluginStateFile"), "device plugin state file"));
    return slot;
}

JsonValue deviceChainToJson (const DeviceChain& chain)
{
    JsonValue::Array slots;
    for (const auto& slot : chain.slots())
        slots.push_back (deviceSlotToJson (slot));

    return JsonValue::object ({
        { "slots", JsonValue::array (std::move (slots)) },
    });
}

DeviceChain deviceChainFromJson (const JsonValue& value)
{
    DeviceChain chain;
    for (const auto& slot : requireArray (requireField (value, "slots"), "device slots"))
        chain.appendSlot (deviceSlotFromJson (slot));

    return chain;
}

JsonValue audioSourceToJson (const AudioSourceReference& source)
{
    return JsonValue::object ({
        { "sourceId", JsonValue::string (source.sourceId) },
        { "filePath", JsonValue::string (source.filePath) },
        { "displayName", JsonValue::string (source.displayName) },
        { "embeddedInProject", JsonValue::boolean (source.embeddedInProject) },
    });
}

AudioSourceReference audioSourceFromJson (const JsonValue& value)
{
    return AudioSourceReference {
        requireString (requireField (value, "sourceId"), "audio source ID"),
        requireString (requireField (value, "filePath"), "audio source file path"),
        requireString (requireField (value, "displayName"), "audio source display name"),
        requireBool (requireField (value, "embeddedInProject"), "audio source embedded flag")
    };
}

JsonValue audioClipToJson (const AudioClip& clip)
{
    return JsonValue::object ({
        { "id", JsonValue::string (clip.id()) },
        { "name", JsonValue::string (clip.name()) },
        { "source", audioSourceToJson (clip.source()) },
        { "startTick", numberFromTicks (clip.startInProject().ticks()) },
        { "lengthTicks", numberFromTicks (clip.length().ticks()) },
        { "sourceOffsetTicks", numberFromTicks (clip.sourceOffset().ticks()) },
        { "loopEnabled", JsonValue::boolean (clip.loopEnabled()) },
        { "stretchToTempo", JsonValue::boolean (clip.stretchToTempo()) },
        { "gainDb", decibelsToJson (clip.gainDb()) },
    });
}

AudioClip audioClipFromJson (const JsonValue& value)
{
    AudioClip clip {
        requireString (requireField (value, "id"), "audio clip ID"),
        requireString (requireField (value, "name"), "audio clip name"),
        audioSourceFromJson (requireField (value, "source")),
        tickPositionFromJson (requireField (value, "startTick"), "audio clip start tick"),
        tickDurationFromJson (requireField (value, "lengthTicks"), "audio clip length ticks"),
        tickDurationFromJson (requireField (value, "sourceOffsetTicks"), "audio clip source offset ticks")
    };
    clip.setLoopEnabled (requireBool (requireField (value, "loopEnabled"), "audio clip loop enabled"));
    clip.setStretchToTempo (requireBool (requireField (value, "stretchToTempo"), "audio clip stretch-to-tempo flag"));
    clip.setGainDb (decibelsFromJson (requireField (value, "gainDb"), "audio clip gain dB"));
    return clip;
}

std::string automationTargetKindToString (AutomationTargetKind kind)
{
    switch (kind)
    {
        case AutomationTargetKind::trackVolume: return "trackVolume";
        case AutomationTargetKind::trackPan: return "trackPan";
        case AutomationTargetKind::trackMute: return "trackMute";
        case AutomationTargetKind::sendLevel: return "sendLevel";
        case AutomationTargetKind::deviceBypass: return "deviceBypass";
        case AutomationTargetKind::pluginParameter: return "pluginParameter";
    }

    return "trackVolume";
}

AutomationTargetKind automationTargetKindFromString (const std::string& kind)
{
    if (kind == "trackVolume") return AutomationTargetKind::trackVolume;
    if (kind == "trackPan") return AutomationTargetKind::trackPan;
    if (kind == "trackMute") return AutomationTargetKind::trackMute;
    if (kind == "sendLevel") return AutomationTargetKind::sendLevel;
    if (kind == "deviceBypass") return AutomationTargetKind::deviceBypass;
    if (kind == "pluginParameter") return AutomationTargetKind::pluginParameter;

    throw std::runtime_error ("Unknown automation target kind '" + kind + "'");
}

JsonValue automationTargetToJson (const AutomationTarget& target)
{
    return JsonValue::object ({
        { "kind", JsonValue::string (automationTargetKindToString (target.kind)) },
        { "trackId", JsonValue::string (target.trackId) },
        { "sendTargetTrackId", JsonValue::string (target.sendTargetTrackId) },
        { "deviceSlotId", JsonValue::string (target.deviceSlotId.value) },
        { "pluginParameterId", JsonValue::string (target.pluginParameterId) },
    });
}

AutomationTarget automationTargetFromJson (const JsonValue& value)
{
    const auto kind = automationTargetKindFromString (requireString (requireField (value, "kind"), "automation target kind"));
    auto trackId = requireString (requireField (value, "trackId"), "automation target track ID");

    switch (kind)
    {
        case AutomationTargetKind::trackVolume: return AutomationTarget::trackVolume (std::move (trackId));
        case AutomationTargetKind::trackPan: return AutomationTarget::trackPan (std::move (trackId));
        case AutomationTargetKind::trackMute: return AutomationTarget::trackMute (std::move (trackId));
        case AutomationTargetKind::sendLevel:
            return AutomationTarget::sendLevel (
                std::move (trackId),
                requireString (requireField (value, "sendTargetTrackId"), "automation send target return track ID"));
        case AutomationTargetKind::deviceBypass:
            return AutomationTarget::deviceBypass (
                std::move (trackId),
                DeviceSlotId { requireString (requireField (value, "deviceSlotId"), "automation device slot ID") });
        case AutomationTargetKind::pluginParameter:
            return AutomationTarget::pluginParameter (
                std::move (trackId),
                DeviceSlotId { requireString (requireField (value, "deviceSlotId"), "automation device slot ID") },
                requireString (requireField (value, "pluginParameterId"), "automation plugin parameter ID"));
    }

    throw std::runtime_error ("Unknown automation target kind");
}

std::string automationInterpolationToString (AutomationInterpolation interpolation)
{
    switch (interpolation)
    {
        case AutomationInterpolation::hold: return "hold";
        case AutomationInterpolation::linear: return "linear";
    }

    return "linear";
}

AutomationInterpolation automationInterpolationFromString (const std::string& interpolation)
{
    if (interpolation == "hold") return AutomationInterpolation::hold;
    if (interpolation == "linear") return AutomationInterpolation::linear;

    throw std::runtime_error ("Unknown automation interpolation '" + interpolation + "'");
}

JsonValue automationPointToJson (const AutomationPoint& point)
{
    return JsonValue::object ({
        { "positionTick", numberFromTicks (point.position.ticks()) },
        { "normalizedValue", JsonValue::number (point.normalizedValue) },
        { "interpolationToNext", JsonValue::string (automationInterpolationToString (point.interpolationToNext)) },
    });
}

AutomationPoint automationPointFromJson (const JsonValue& value)
{
    return AutomationPoint {
        tickPositionFromJson (requireField (value, "positionTick"), "automation point position tick"),
        requireField (value, "normalizedValue").asNumber(),
        automationInterpolationFromString (requireString (requireField (value, "interpolationToNext"), "automation interpolation"))
    };
}

JsonValue automationCurveToJson (const AutomationCurve& curve)
{
    JsonValue::Array points;
    for (const auto& point : curve.points())
        points.push_back (automationPointToJson (point));

    return JsonValue::object ({
        { "points", JsonValue::array (std::move (points)) },
    });
}

AutomationCurve automationCurveFromJson (const JsonValue& value)
{
    AutomationCurve curve;
    for (const auto& point : requireArray (requireField (value, "points"), "automation points"))
        curve.addPoint (automationPointFromJson (point));

    return curve;
}

JsonValue automationLaneToJson (const AutomationLane& lane)
{
    return JsonValue::object ({
        { "target", automationTargetToJson (lane.target()) },
        { "visible", JsonValue::boolean (lane.visible()) },
        { "curve", automationCurveToJson (lane.curve()) },
    });
}

AutomationLane automationLaneFromJson (const JsonValue& value)
{
    AutomationLane lane {
        automationTargetFromJson (requireField (value, "target")),
        automationCurveFromJson (requireField (value, "curve"))
    };
    lane.setVisible (requireBool (requireField (value, "visible"), "automation lane visible flag"));
    return lane;
}

JsonValue trackToJson (const Track& track)
{
    JsonValue::Array clips;
    for (const auto& clip : track.clips())
        clips.push_back (midiClipToJson (clip));

    JsonValue::Array audioClips;
    for (const auto& clip : track.audioClips())
        audioClips.push_back (audioClipToJson (clip));

    JsonValue::Array automationLanes;
    for (const auto& lane : track.automationLanes())
        automationLanes.push_back (automationLaneToJson (lane));

    const auto legacyInstrument = legacyInstrumentForSerialization (track);

    return JsonValue::object ({
        { "id", JsonValue::string (track.id()) },
        { "name", JsonValue::string (track.name()) },
        { "type", JsonValue::string (trackTypeId (track.type())) },
        { "mixerStrip", mixerStripToJson (track.mixerStrip()) },
        { "routing", trackRoutingToJson (track.routing()) },
        { "deviceChain", deviceChainToJson (deviceChainForSerialization (track)) },
        { "clips", JsonValue::array (std::move (clips)) },
        { "audioClips", JsonValue::array (std::move (audioClips)) },
        { "automationLanes", JsonValue::array (std::move (automationLanes)) },
        { "pluginReference", legacyInstrument.has_value() ? trackInstrumentToJson (*legacyInstrument) : JsonValue::null() },
    });
}

Track trackFromJson (const JsonValue& value)
{
    Track track {
        requireString (requireField (value, "id"), "track ID"),
        requireString (requireField (value, "name"), "track name"),
        trackTypeFromId (requireString (requireField (value, "type"), "track type"))
    };

    track.setMixerStrip (mixerStripFromJson (requireField (value, "mixerStrip")));
    track.setRouting (trackRoutingFromJson (requireField (value, "routing")));
    const auto chain = deviceChainFromJson (requireField (value, "deviceChain"));
    track.setDeviceChain (chain);

    const auto& pluginReference = requireField (value, "pluginReference");
    if (! pluginReference.isNull() && trackTypeCanHostInstrument (track.type()))
        track.setInstrument (trackInstrumentFromJson (pluginReference));
    else if (trackTypeCanHostInstrument (track.type()))
    {
        for (const auto& slot : chain.slots())
        {
            if (slot.kind() == PluginKind::instrument)
            {
                track.setInstrument (slot.plugin().toTrackInstrumentReference (slot.pluginStateFile()));
                break;
            }
        }
    }

    for (const auto& clip : requireArray (requireField (value, "clips"), "track clips"))
        track.addClip (midiClipFromJson (clip));

    for (const auto& clip : requireArray (requireField (value, "audioClips"), "track audio clips"))
        track.addAudioClip (audioClipFromJson (clip));

    for (const auto& lane : requireArray (requireField (value, "automationLanes"), "track automation lanes"))
        track.setAutomationLane (automationLaneFromJson (lane));

    return track;
}

JsonValue keyCenterRegionToJson (const KeyCenterRegion& region)
{
    return JsonValue::object ({
        { "region", regionToJson (region.region()) },
        { "pitchClass", JsonValue::number (static_cast<double> (region.pitchClass().semitonesFromC())) },
    });
}

KeyCenterRegion keyCenterRegionFromJson (const JsonValue& value)
{
    return KeyCenterRegion {
        regionFromJson (requireField (value, "region"), "key center region"),
        PitchClass::fromSemitonesFromC (requireInt (requireField (value, "pitchClass"), "key center pitch class"))
    };
}

JsonValue scaleModeRegionToJson (const ScaleModeRegion& region)
{
    return JsonValue::object ({
        { "region", regionToJson (region.region()) },
        { "scaleDefinitionName", JsonValue::string (region.scaleDefinitionName()) },
    });
}

ScaleModeRegion scaleModeRegionFromJson (const JsonValue& value)
{
    return ScaleModeRegion {
        regionFromJson (requireField (value, "region"), "scale/mode region"),
        requireString (requireField (value, "scaleDefinitionName"), "scale/mode region scale")
    };
}

JsonValue chordRegionToJson (const ChordRegion& region)
{
    return JsonValue::object ({
        { "region", regionToJson (region.region()) },
        { "rootPitchClass", JsonValue::number (static_cast<double> (region.root().semitonesFromC())) },
        { "quality", JsonValue::string (chordQualityToString (region.quality())) },
        { "chordTones", pitchClassArrayToJson (region.chordTones()) },
        { "chordName", JsonValue::string (region.chordName()) },
    });
}

ChordRegion chordRegionFromJson (const JsonValue& value)
{
    return ChordRegion {
        regionFromJson (requireField (value, "region"), "chord region"),
        PitchClass::fromSemitonesFromC (requireInt (requireField (value, "rootPitchClass"), "chord region root pitch class")),
        chordQualityFromString (requireString (requireField (value, "quality"), "chord region quality")),
        pitchClassArrayFromJson (requireField (value, "chordTones"), "chord region chord tones"),
        requireString (requireField (value, "chordName"), "chord name")
    };
}

JsonValue musicalStructureToJson (const MusicalStructure& structure)
{
    JsonValue::Array keyCenters;
    for (const auto& region : structure.keyCenterRegions())
        keyCenters.push_back (keyCenterRegionToJson (region));

    JsonValue::Array scaleModes;
    for (const auto& region : structure.scaleModeRegions())
        scaleModes.push_back (scaleModeRegionToJson (region));

    JsonValue::Array chords;
    for (const auto& region : structure.chordRegions())
        chords.push_back (chordRegionToJson (region));

    return JsonValue::object ({
        { "defaultKeyCenterPitchClass", JsonValue::number (static_cast<double> (structure.defaultKeyCenter().semitonesFromC())) },
        { "defaultScaleDefinitionName", JsonValue::string (structure.defaultScaleDefinitionName()) },
        { "keyCenterRegions", JsonValue::array (std::move (keyCenters)) },
        { "scaleModeRegions", JsonValue::array (std::move (scaleModes)) },
        { "chordRegions", JsonValue::array (std::move (chords)) },
    });
}

MusicalStructure musicalStructureFromJson (const JsonValue& value)
{
    MusicalStructure structure {
        PitchClass::fromSemitonesFromC (requireInt (requireField (value, "defaultKeyCenterPitchClass"), "default key center pitch class")),
        requireString (requireField (value, "defaultScaleDefinitionName"), "default scale definition name")
    };

    for (const auto& region : requireArray (requireField (value, "keyCenterRegions"), "key center regions"))
        structure.addKeyCenterRegion (keyCenterRegionFromJson (region));

    for (const auto& region : requireArray (requireField (value, "scaleModeRegions"), "scale/mode regions"))
        structure.addScaleModeRegion (scaleModeRegionFromJson (region));

    for (const auto& region : requireArray (requireField (value, "chordRegions"), "chord regions"))
        structure.addChordRegion (chordRegionFromJson (region));

    return structure;
}

std::string waveformCacheKeyForSource (const AudioSourceReference& source)
{
    return source.sourceId + "::" + source.filePath;
}

JsonValue audioAssetsToJson (const Project& project)
{
    JsonValue::Array waveformEntries;
    std::set<std::string> seenSourceIds;
    for (const auto& track : project.tracks())
    {
        for (const auto& clip : track.audioClips())
        {
            const auto& source = clip.source();
            if (! seenSourceIds.insert (source.sourceId).second)
                continue;

            waveformEntries.push_back (JsonValue::object ({
                { "sourceId", JsonValue::string (source.sourceId) },
                { "filePath", JsonValue::string (source.filePath) },
                { "displayName", JsonValue::string (source.displayName) },
                { "embeddedInProject", JsonValue::boolean (source.embeddedInProject) },
                { "cacheKey", JsonValue::string (waveformCacheKeyForSource (source)) },
            }));
        }
    }

    return JsonValue::object ({
        { "policy", JsonValue::string ("referenceOnly") },
        { "waveformCache", JsonValue::object ({
            { "directory", JsonValue::string ("waveform-cache") },
            { "entries", JsonValue::array (std::move (waveformEntries)) },
        }) },
    });
}

const Track* findReturnTrack (const Project& project, const std::string& trackId)
{
    const auto* track = project.findTrackById (trackId);
    return track != nullptr && track->type() == TrackType::returnTrack ? track : nullptr;
}

std::vector<std::string> loadWarningsForProject (const Project& project)
{
    std::vector<std::string> warnings;

    for (const auto& error : validateProjectRouting (project).errors)
        warnings.push_back ("Routing warning: " + error);

    for (const auto& track : project.tracks())
    {
        for (const auto& lane : track.automationLanes())
        {
            const auto& target = lane.target();
            const auto* targetTrack = project.findTrackById (target.trackId);
            if (targetTrack == nullptr)
            {
                warnings.push_back ("Automation target '" + target.stableId() + "' references a missing track");
                continue;
            }

            if (target.trackId != track.id())
                warnings.push_back ("Automation lane on track '" + track.name() + "' targets a different track");

            if (target.kind == AutomationTargetKind::sendLevel && findReturnTrack (project, target.sendTargetTrackId) == nullptr)
                warnings.push_back ("Automation target '" + target.stableId() + "' references a missing return track");

            if ((target.kind == AutomationTargetKind::deviceBypass || target.kind == AutomationTargetKind::pluginParameter)
                && targetTrack->deviceChain().findSlot (target.deviceSlotId) == nullptr)
            {
                warnings.push_back ("Automation target '" + target.stableId() + "' references a missing device slot");
            }
        }
    }

    return warnings;
}
}

JsonValue ProjectSerializer::toJson (const Project& project)
{
    JsonValue::Array tracks;
    for (const auto& track : project.tracks())
        tracks.push_back (trackToJson (track));

    JsonValue::Array customScales;
    for (const auto& scale : project.customScales())
        customScales.push_back (scaleDefinitionToJson (scale));

    return JsonValue::object ({
        { "schemaVersion", JsonValue::number (static_cast<double> (currentProjectSchemaVersion)) },
        { "project", JsonValue::object ({
            { "id", JsonValue::string (project.id()) },
            { "name", JsonValue::string (project.name()) },
        }) },
        { "tracks", JsonValue::array (std::move (tracks)) },
        { "musicalStructure", musicalStructureToJson (project.musicalStructure()) },
        { "tempoMap", tempoMapToJson (project.tempoMap()) },
        { "timeSignatureMap", timeSignatureMapToJson (project.timeSignatureMap()) },
        { "customScales", JsonValue::array (std::move (customScales)) },
        { "rhythmicSettings", rhythmSettingsToJson (project.rhythmSettings()) },
        { "audioAssets", audioAssetsToJson (project) },
        { "pluginReferences", JsonValue::array() },
    });
}

Project ProjectSerializer::fromJson (JsonValue projectJson)
{
    auto result = fromJsonWithWarnings (std::move (projectJson));
    return std::move (result.project);
}

ProjectLoadResult ProjectSerializer::fromJsonWithWarnings (JsonValue projectJson)
{
    projectJson = ProjectMigration::migrateToCurrent (std::move (projectJson));

    requireField (projectJson, "rhythmicSettings");
    requireField (projectJson, "pluginReferences");
    requireField (projectJson, "audioAssets");

    const auto& metadata = requireField (projectJson, "project");
    Project project {
        requireString (requireField (metadata, "id"), "project ID"),
        requireString (requireField (metadata, "name"), "project name")
    };

    project.musicalStructure() = musicalStructureFromJson (requireField (projectJson, "musicalStructure"));

    const auto& projectObject = requireObject (projectJson, "project document");
    if (const auto tempoMap = projectObject.find ("tempoMap"); tempoMap != projectObject.end())
        project.tempoMap() = tempoMapFromJson (tempoMap->second);

    if (const auto timeSignatureMap = projectObject.find ("timeSignatureMap"); timeSignatureMap != projectObject.end())
        project.timeSignatureMap() = timeSignatureMapFromJson (timeSignatureMap->second);

    if (const auto rhythmicSettings = projectObject.find ("rhythmicSettings"); rhythmicSettings != projectObject.end())
        project.rhythmSettings() = rhythmSettingsFromJson (rhythmicSettings->second);

    for (const auto& scale : requireArray (requireField (projectJson, "customScales"), "custom scales"))
        project.addCustomScale (scaleDefinitionFromJson (scale));

    for (const auto& track : requireArray (requireField (projectJson, "tracks"), "tracks"))
        project.addTrack (trackFromJson (track));

    auto warnings = loadWarningsForProject (project);
    return ProjectLoadResult { std::move (project), std::move (warnings) };
}

std::string ProjectSerializer::serialize (const Project& project)
{
    return writeJson (toJson (project));
}

Project ProjectSerializer::deserialize (std::string_view jsonText)
{
    return fromJson (parseJson (jsonText));
}

ProjectLoadResult ProjectSerializer::deserializeWithWarnings (std::string_view jsonText)
{
    return fromJsonWithWarnings (parseJson (jsonText));
}
}
