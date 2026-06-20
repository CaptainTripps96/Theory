#include "core/serialization/ProjectSerializer.h"

#include "core/serialization/ProjectMigration.h"
#include "core/serialization/ProjectSchemaVersion.h"
#include "core/time/GridDivision.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
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
    offsets.reserve (scale.pitchClassOffsetsFromRoot().size());
    for (const auto offset : scale.pitchClassOffsetsFromRoot())
        offsets.push_back (JsonValue::number (static_cast<double> (offset)));

    JsonValue::Array degrees;
    degrees.reserve (scale.degreeMapping().size());
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
    const auto& offsetValues = requireArray (requireField (value, "pitchClassOffsetsFromRoot"), "scale pitch-class offsets");
    offsets.reserve (offsetValues.size());
    for (const auto& offset : offsetValues)
        offsets.push_back (requireInt (offset, "scale pitch-class offset"));

    std::vector<ScaleDegree> degrees;
    const auto& degreeValues = requireArray (requireField (value, "degreeMapping"), "scale degree mapping");
    degrees.reserve (degreeValues.size());
    for (const auto& degree : degreeValues)
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
    result.reserve (pitchClasses.size());
    for (const auto pitchClass : pitchClasses)
        result.push_back (JsonValue::number (static_cast<double> (pitchClass.semitonesFromC())));

    return JsonValue::array (std::move (result));
}

std::vector<PitchClass> pitchClassArrayFromJson (const JsonValue& value, const std::string& description)
{
    std::vector<PitchClass> result;
    const auto& values = requireArray (value, description);
    result.reserve (values.size());
    for (const auto& pitchClass : values)
        result.push_back (PitchClass::fromSemitonesFromC (requireInt (pitchClass, description + " pitch class")));

    return result;
}

JsonValue clipHarmonicMapToJson (const ClipHarmonicMap& map)
{
    JsonValue::Array regions;
    regions.reserve (map.regions().size());
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
    nodes.reserve (map.nodes().size());
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
    markers.reserve (map.markers().size());
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

std::string expressionPolarityToString (ExpressionLanePolarity polarity)
{
    switch (polarity)
    {
        case ExpressionLanePolarity::unipolar: return "unipolar";
        case ExpressionLanePolarity::bipolar: return "bipolar";
    }

    return "unipolar";
}

ExpressionLanePolarity expressionPolarityFromString (const std::string& polarity)
{
    if (polarity == "unipolar") return ExpressionLanePolarity::unipolar;
    if (polarity == "bipolar") return ExpressionLanePolarity::bipolar;

    throw std::runtime_error ("Unknown expression lane polarity '" + polarity + "'");
}

std::string expressionCurveShapeToString (ExpressionCurveShape shape)
{
    switch (shape)
    {
        case ExpressionCurveShape::linear: return "linear";
        case ExpressionCurveShape::logarithmic: return "logarithmic";
        case ExpressionCurveShape::exponential: return "exponential";
    }

    return "linear";
}

ExpressionCurveShape expressionCurveShapeFromString (const std::string& shape)
{
    if (shape == "linear") return ExpressionCurveShape::linear;
    if (shape == "logarithmic") return ExpressionCurveShape::logarithmic;
    if (shape == "exponential") return ExpressionCurveShape::exponential;

    throw std::runtime_error ("Unknown expression curve shape '" + shape + "'");
}

std::string expressionDestinationKindToString (ExpressionDestinationKind kind)
{
    switch (kind)
    {
        case ExpressionDestinationKind::trackVolume: return "trackVolume";
        case ExpressionDestinationKind::trackPan: return "trackPan";
        case ExpressionDestinationKind::pitch: return "pitch";
        case ExpressionDestinationKind::pitchBend: return "pitchBend";
        case ExpressionDestinationKind::firstPartyParameter: return "firstPartyParameter";
        case ExpressionDestinationKind::pluginParameter: return "pluginParameter";
        case ExpressionDestinationKind::midiCc: return "midiCc";
        case ExpressionDestinationKind::sendLevel: return "sendLevel";
    }

    return "trackVolume";
}

ExpressionDestinationKind expressionDestinationKindFromString (const std::string& kind)
{
    if (kind == "trackVolume") return ExpressionDestinationKind::trackVolume;
    if (kind == "trackPan") return ExpressionDestinationKind::trackPan;
    if (kind == "pitch") return ExpressionDestinationKind::pitch;
    if (kind == "pitchBend") return ExpressionDestinationKind::pitchBend;
    if (kind == "firstPartyParameter") return ExpressionDestinationKind::firstPartyParameter;
    if (kind == "pluginParameter") return ExpressionDestinationKind::pluginParameter;
    if (kind == "midiCc") return ExpressionDestinationKind::midiCc;
    if (kind == "sendLevel") return ExpressionDestinationKind::sendLevel;

    throw std::runtime_error ("Unknown expression destination kind '" + kind + "'");
}

JsonValue expressionDestinationToJson (const ExpressionDestination& destination)
{
    return JsonValue::object ({
        { "kind", JsonValue::string (expressionDestinationKindToString (destination.kind)) },
        { "trackId", JsonValue::string (destination.trackId) },
        { "sendTargetTrackId", JsonValue::string (destination.sendTargetTrackId) },
        { "deviceSlotId", JsonValue::string (destination.deviceSlotId.value) },
        { "parameterId", JsonValue::string (destination.parameterId) },
        { "midiCcNumber", JsonValue::number (static_cast<double> (destination.midiCcNumber)) },
    });
}

ExpressionDestination expressionDestinationFromJson (const JsonValue& value)
{
    const auto kind = expressionDestinationKindFromString (
        requireString (requireField (value, "kind"), "expression destination kind"));
    auto trackId = requireString (requireField (value, "trackId"), "expression destination track ID");
    const auto deviceSlotIdFromJson = [&value] (const std::string& description) {
        const auto slotId = requireString (requireField (value, "deviceSlotId"), description);
        return slotId.empty() ? DeviceSlotId {} : DeviceSlotId { slotId };
    };

    switch (kind)
    {
        case ExpressionDestinationKind::trackVolume:
            return ExpressionDestination::trackVolume (std::move (trackId));
        case ExpressionDestinationKind::trackPan:
            return ExpressionDestination::trackPan (std::move (trackId));
        case ExpressionDestinationKind::pitch:
            return ExpressionDestination::pitch (
                std::move (trackId),
                deviceSlotIdFromJson ("expression destination device slot ID"));
        case ExpressionDestinationKind::pitchBend:
            return ExpressionDestination::pitchBend (std::move (trackId));
        case ExpressionDestinationKind::firstPartyParameter:
            return ExpressionDestination::firstPartyParameter (
                std::move (trackId),
                DeviceSlotId { requireString (requireField (value, "deviceSlotId"), "expression destination device slot ID") },
                requireString (requireField (value, "parameterId"), "expression destination parameter ID"));
        case ExpressionDestinationKind::pluginParameter:
            return ExpressionDestination::pluginParameter (
                std::move (trackId),
                DeviceSlotId { requireString (requireField (value, "deviceSlotId"), "expression destination device slot ID") },
                requireString (requireField (value, "parameterId"), "expression destination parameter ID"));
        case ExpressionDestinationKind::midiCc:
            return ExpressionDestination::midiCc (
                std::move (trackId),
                requireInt (requireField (value, "midiCcNumber"), "expression MIDI CC number"));
        case ExpressionDestinationKind::sendLevel:
            return ExpressionDestination::sendLevel (
                std::move (trackId),
                requireString (requireField (value, "sendTargetTrackId"), "expression send target track ID"));
    }

    throw std::runtime_error ("Unknown expression destination kind");
}

JsonValue expressionRouteToJson (const ExpressionRoute& route)
{
    return JsonValue::object ({
        { "id", JsonValue::string (route.id().value) },
        { "destination", expressionDestinationToJson (route.destination()) },
        { "outputMin", JsonValue::number (route.outputMin()) },
        { "outputMax", JsonValue::number (route.outputMax()) },
        { "enabled", JsonValue::boolean (route.enabled()) },
    });
}

ExpressionRoute expressionRouteFromJson (const JsonValue& value)
{
    ExpressionRoute route {
        ExpressionRouteId { requireString (requireField (value, "id"), "expression route ID") },
        expressionDestinationFromJson (requireField (value, "destination")),
        requireField (value, "outputMin").asNumber(),
        requireField (value, "outputMax").asNumber()
    };
    route.setEnabled (requireBool (requireField (value, "enabled"), "expression route enabled flag"));
    return route;
}

std::string envelopeStageTypeToString (EnvelopeStageType type)
{
    switch (type)
    {
        case EnvelopeStageType::attack: return "attack";
        case EnvelopeStageType::decay: return "decay";
        case EnvelopeStageType::release: return "release";
    }

    return "attack";
}

EnvelopeStageType envelopeStageTypeFromString (const std::string& type)
{
    if (type == "attack") return EnvelopeStageType::attack;
    if (type == "decay") return EnvelopeStageType::decay;
    if (type == "release") return EnvelopeStageType::release;

    throw std::runtime_error ("Unknown envelope stage type '" + type + "'");
}

JsonValue envelopeStageToJson (const EnvelopeStage& stage)
{
    return JsonValue::object ({
        { "type", JsonValue::string (envelopeStageTypeToString (stage.stageType)) },
        { "durationTicks", numberFromTicks (stage.duration.ticks()) },
        { "startLevel", JsonValue::number (stage.startLevel) },
        { "endLevel", JsonValue::number (stage.endLevel) },
        { "curveShape", JsonValue::string (expressionCurveShapeToString (stage.curveShape)) },
    });
}

EnvelopeStage envelopeStageFromJson (const JsonValue& value)
{
    return EnvelopeStage {
        envelopeStageTypeFromString (requireString (requireField (value, "type"), "envelope stage type")),
        tickDurationFromJson (requireField (value, "durationTicks"), "envelope stage duration"),
        requireField (value, "startLevel").asNumber(),
        requireField (value, "endLevel").asNumber(),
        expressionCurveShapeFromString (requireString (requireField (value, "curveShape"), "envelope curve shape"))
    };
}

JsonValue optionalDoubleToJson (const std::optional<double>& value)
{
    return value.has_value() ? JsonValue::number (*value) : JsonValue::null();
}

std::optional<double> optionalDoubleFromJson (const JsonValue& value)
{
    if (value.isNull())
        return std::nullopt;

    return value.asNumber();
}

JsonValue optionalDurationToJson (const std::optional<TickDuration>& value)
{
    return value.has_value() ? numberFromTicks (value->ticks()) : JsonValue::null();
}

std::optional<TickDuration> optionalDurationFromJson (const JsonValue& value, const std::string& description)
{
    if (value.isNull())
        return std::nullopt;

    return tickDurationFromJson (value, description);
}

JsonValue optionalBlockIdToJson (const std::optional<ExpressionBlockId>& value)
{
    return value.has_value() ? JsonValue::string (value->value) : JsonValue::null();
}

std::optional<ExpressionBlockId> optionalBlockIdFromJson (const JsonValue& value)
{
    if (value.isNull())
        return std::nullopt;

    return ExpressionBlockId { requireString (value, "expression block ID") };
}

JsonValue phraseEnvelopeToJson (const PhraseEnvelopeClip& clip)
{
    return JsonValue::object ({
        { "id", JsonValue::string (clip.id().value) },
        { "sourceNoteIds", JsonValue::array (stringArrayToJson (clip.sourceNoteIds())) },
        { "phraseRegion", regionToJson (clip.phraseRegion()) },
        { "storedLevel", JsonValue::number (clip.storedLevel()) },
        { "attackStage", envelopeStageToJson (clip.attackStage()) },
        { "decayStage", clip.decayStage().has_value() ? envelopeStageToJson (*clip.decayStage()) : JsonValue::null() },
        { "releaseStage", clip.releaseStage().has_value() ? envelopeStageToJson (*clip.releaseStage()) : JsonValue::null() },
        { "peakLevel", optionalDoubleToJson (clip.peakLevel()) },
        { "sustainLevel", optionalDoubleToJson (clip.sustainLevel()) },
        { "tailExtensionTicks", optionalDurationToJson (clip.tailExtension()) },
    });
}

PhraseEnvelopeClip phraseEnvelopeFromJson (const JsonValue& value, ExpressionLanePolarity polarity)
{
    PhraseEnvelopeClip clip {
        ExpressionClipId { requireString (requireField (value, "id"), "phrase envelope ID") },
        stringArrayFromJson (requireField (value, "sourceNoteIds"), "phrase envelope source note IDs"),
        regionFromJson (requireField (value, "phraseRegion"), "phrase envelope region"),
        requireField (value, "storedLevel").asNumber(),
        envelopeStageFromJson (requireField (value, "attackStage"))
    };

    if (const auto& decayStage = requireField (value, "decayStage"); ! decayStage.isNull())
        clip.setDecayStage (envelopeStageFromJson (decayStage));
    if (const auto& releaseStage = requireField (value, "releaseStage"); ! releaseStage.isNull())
        clip.setReleaseStage (envelopeStageFromJson (releaseStage));
    clip.setStoredLevel (requireField (value, "storedLevel").asNumber(), polarity);
    clip.setPeakLevel (optionalDoubleFromJson (requireField (value, "peakLevel")), polarity);
    clip.setSustainLevel (optionalDoubleFromJson (requireField (value, "sustainLevel")), polarity);
    clip.setTailExtension (optionalDurationFromJson (requireField (value, "tailExtensionTicks"), "phrase envelope tail extension"));
    return clip;
}

std::string cyclicWaveShapeToString (CyclicWaveShape shape)
{
    switch (shape)
    {
        case CyclicWaveShape::sine: return "sine";
        case CyclicWaveShape::triangle: return "triangle";
        case CyclicWaveShape::rampUp: return "rampUp";
        case CyclicWaveShape::rampDown: return "rampDown";
        case CyclicWaveShape::square: return "square";
    }

    return "sine";
}

CyclicWaveShape cyclicWaveShapeFromString (const std::string& shape)
{
    if (shape == "sine") return CyclicWaveShape::sine;
    if (shape == "triangle") return CyclicWaveShape::triangle;
    if (shape == "rampUp") return CyclicWaveShape::rampUp;
    if (shape == "rampDown") return CyclicWaveShape::rampDown;
    if (shape == "square") return CyclicWaveShape::square;

    throw std::runtime_error ("Unknown cyclic wave shape '" + shape + "'");
}

std::string cyclicBlendModeToString (CyclicBlendMode mode)
{
    switch (mode)
    {
        case CyclicBlendMode::additive: return "additive";
        case CyclicBlendMode::multiplicative: return "multiplicative";
    }

    return "additive";
}

CyclicBlendMode cyclicBlendModeFromString (const std::string& mode)
{
    if (mode == "additive") return CyclicBlendMode::additive;
    if (mode == "multiplicative") return CyclicBlendMode::multiplicative;

    throw std::runtime_error ("Unknown cyclic blend mode '" + mode + "'");
}

std::string cyclicWavePolarityModeToString (CyclicWavePolarityMode mode)
{
    switch (mode)
    {
        case CyclicWavePolarityMode::positiveOscillator: return "positiveOscillator";
        case CyclicWavePolarityMode::halfWaveRectified: return "halfWaveRectified";
    }

    return "positiveOscillator";
}

CyclicWavePolarityMode cyclicWavePolarityModeFromString (const std::string& mode)
{
    if (mode == "positiveOscillator") return CyclicWavePolarityMode::positiveOscillator;
    if (mode == "halfWaveRectified") return CyclicWavePolarityMode::halfWaveRectified;

    throw std::runtime_error ("Unknown cyclic wave polarity mode '" + mode + "'");
}

JsonValue cyclicExpressionClipToJson (const CyclicExpressionClip& clip)
{
    return JsonValue::object ({
        { "id", JsonValue::string (clip.id().value) },
        { "sourceNoteIds", JsonValue::array (stringArrayToJson (clip.sourceNoteIds())) },
        { "phraseRegion", regionToJson (clip.phraseRegion()) },
        { "attackTicks", numberFromTicks (clip.attackTime().ticks()) },
        { "releaseTicks", numberFromTicks (clip.releaseTime().ticks()) },
        { "maxAmplitude", JsonValue::number (clip.maxAmplitude()) },
        { "frequencyDivisionId", JsonValue::string (clip.frequencyDivisionId()) },
        { "waveShape", JsonValue::string (cyclicWaveShapeToString (clip.waveShape())) },
        { "blendMode", JsonValue::string (cyclicBlendModeToString (clip.blendMode())) },
        { "wavePolarityMode", JsonValue::string (cyclicWavePolarityModeToString (clip.wavePolarityMode())) },
        { "phase", JsonValue::number (clip.phase()) },
    });
}

CyclicExpressionClip cyclicExpressionClipFromJson (const JsonValue& value)
{
    CyclicExpressionClip clip {
        ExpressionClipId { requireString (requireField (value, "id"), "cyclic expression clip ID") },
        stringArrayFromJson (requireField (value, "sourceNoteIds"), "cyclic expression source note IDs"),
        regionFromJson (requireField (value, "phraseRegion"), "cyclic expression region")
    };

    clip.setAttackTime (tickDurationFromJson (requireField (value, "attackTicks"), "cyclic expression attack"));
    clip.setReleaseTime (tickDurationFromJson (requireField (value, "releaseTicks"), "cyclic expression release"));
    clip.setMaxAmplitude (requireField (value, "maxAmplitude").asNumber());
    clip.setFrequencyDivisionId (requireString (requireField (value, "frequencyDivisionId"), "cyclic expression frequency division ID"));
    clip.setWaveShape (cyclicWaveShapeFromString (requireString (requireField (value, "waveShape"), "cyclic expression wave shape")));
    clip.setBlendMode (cyclicBlendModeFromString (requireString (requireField (value, "blendMode"), "cyclic expression blend mode")));
    clip.setWavePolarityMode (cyclicWavePolarityModeFromString (requireString (requireField (value, "wavePolarityMode"), "cyclic expression wave polarity mode")));
    clip.setPhase (requireField (value, "phase").asNumber());
    return clip;
}

JsonValue pitchSlurToJson (const PitchSlur& slur)
{
    return JsonValue::object ({
        { "id", JsonValue::string (slur.id().value) },
        { "sourceNoteId", JsonValue::string (slur.sourceNoteId()) },
        { "destinationNoteId", JsonValue::string (slur.destinationNoteId()) },
        { "slurTimeTicks", numberFromTicks (slur.slurTime().ticks()) },
        { "curveShape", JsonValue::string (expressionCurveShapeToString (slur.curveShape())) },
        { "legatoNoRetrigger", JsonValue::boolean (slur.legatoNoRetrigger()) },
        { "blockId", optionalBlockIdToJson (slur.blockId()) },
        { "hasVoiceOverride", JsonValue::boolean (slur.hasVoiceOverride()) },
    });
}

PitchSlur pitchSlurFromJson (const JsonValue& value)
{
    PitchSlur slur {
        ExpressionClipId { requireString (requireField (value, "id"), "pitch slur ID") },
        requireString (requireField (value, "sourceNoteId"), "pitch slur source note ID"),
        requireString (requireField (value, "destinationNoteId"), "pitch slur destination note ID")
    };
    slur.setSlurTime (tickDurationFromJson (requireField (value, "slurTimeTicks"), "pitch slur time"));
    slur.setCurveShape (expressionCurveShapeFromString (requireString (requireField (value, "curveShape"), "pitch slur curve shape")));
    slur.setLegatoNoRetrigger (requireBool (requireField (value, "legatoNoRetrigger"), "pitch slur legato flag"));
    slur.setBlockId (optionalBlockIdFromJson (requireField (value, "blockId")));
    slur.setHasVoiceOverride (requireBool (requireField (value, "hasVoiceOverride"), "pitch slur voice override flag"));
    return slur;
}

JsonValue vibratoVoiceOverrideToJson (const VibratoVoiceOverride& override)
{
    return JsonValue::object ({
        { "noteId", JsonValue::string (override.noteId) },
        { "amplitudeSemitones", JsonValue::number (override.amplitudeSemitones) },
        { "attackTicks", numberFromTicks (override.attackTime.ticks()) },
        { "releaseTicks", numberFromTicks (override.releaseTime.ticks()) },
        { "frequencyDivisionId", JsonValue::string (override.frequencyDivisionId) },
        { "waveShape", JsonValue::string (cyclicWaveShapeToString (override.waveShape)) },
        { "phase", JsonValue::number (override.phase) },
    });
}

VibratoVoiceOverride vibratoVoiceOverrideFromJson (const JsonValue& value)
{
    return VibratoVoiceOverride {
        requireString (requireField (value, "noteId"), "vibrato voice override note ID"),
        requireField (value, "amplitudeSemitones").asNumber(),
        tickDurationFromJson (requireField (value, "attackTicks"), "vibrato voice override attack"),
        tickDurationFromJson (requireField (value, "releaseTicks"), "vibrato voice override release"),
        requireString (requireField (value, "frequencyDivisionId"), "vibrato voice override frequency division ID"),
        cyclicWaveShapeFromString (requireString (requireField (value, "waveShape"), "vibrato voice override wave shape")),
        requireField (value, "phase").asNumber()
    };
}

JsonValue vibratoExpressionToJson (const VibratoExpression& vibrato)
{
    JsonValue::Array overrides;
    overrides.reserve (vibrato.voiceOverrides().size());
    for (const auto& override : vibrato.voiceOverrides())
        overrides.push_back (vibratoVoiceOverrideToJson (override));

    return JsonValue::object ({
        { "id", JsonValue::string (vibrato.id().value) },
        { "sourceNoteIds", JsonValue::array (stringArrayToJson (vibrato.sourceNoteIds())) },
        { "phraseRegion", regionToJson (vibrato.phraseRegion()) },
        { "attackTicks", numberFromTicks (vibrato.attackTime().ticks()) },
        { "releaseTicks", numberFromTicks (vibrato.releaseTime().ticks()) },
        { "amplitudeSemitones", JsonValue::number (vibrato.amplitudeSemitones()) },
        { "frequencyDivisionId", JsonValue::string (vibrato.frequencyDivisionId()) },
        { "waveShape", JsonValue::string (cyclicWaveShapeToString (vibrato.waveShape())) },
        { "phase", JsonValue::number (vibrato.phase()) },
        { "blockId", optionalBlockIdToJson (vibrato.blockId()) },
        { "voiceOverrides", JsonValue::array (std::move (overrides)) },
    });
}

VibratoExpression vibratoExpressionFromJson (const JsonValue& value)
{
    VibratoExpression vibrato {
        ExpressionClipId { requireString (requireField (value, "id"), "vibrato expression ID") },
        stringArrayFromJson (requireField (value, "sourceNoteIds"), "vibrato expression source note IDs"),
        regionFromJson (requireField (value, "phraseRegion"), "vibrato expression region")
    };
    vibrato.setAttackTime (tickDurationFromJson (requireField (value, "attackTicks"), "vibrato expression attack"));
    vibrato.setReleaseTime (tickDurationFromJson (requireField (value, "releaseTicks"), "vibrato expression release"));
    vibrato.setAmplitudeSemitones (requireField (value, "amplitudeSemitones").asNumber());
    vibrato.setFrequencyDivisionId (requireString (requireField (value, "frequencyDivisionId"), "vibrato frequency division ID"));
    vibrato.setWaveShape (cyclicWaveShapeFromString (requireString (requireField (value, "waveShape"), "vibrato wave shape")));
    vibrato.setPhase (requireField (value, "phase").asNumber());
    vibrato.setBlockId (optionalBlockIdFromJson (requireField (value, "blockId")));

    std::vector<VibratoVoiceOverride> overrides;
    const auto& overrideValues = requireArray (requireField (value, "voiceOverrides"), "vibrato voice overrides");
    overrides.reserve (overrideValues.size());
    for (const auto& override : overrideValues)
        overrides.push_back (vibratoVoiceOverrideFromJson (override));
    vibrato.setVoiceOverrides (std::move (overrides));
    return vibrato;
}

JsonValue expressionLaneToJson (const ExpressionLane& lane)
{
    JsonValue::Array routes;
    routes.reserve (lane.routes().size());
    for (const auto& route : lane.routes())
        routes.push_back (expressionRouteToJson (route));

    JsonValue::Array phraseEnvelopes;
    phraseEnvelopes.reserve (lane.phraseEnvelopeClips().size());
    for (const auto& clip : lane.phraseEnvelopeClips())
        phraseEnvelopes.push_back (phraseEnvelopeToJson (clip));

    JsonValue::Array cyclic;
    cyclic.reserve (lane.cyclicClips().size());
    for (const auto& clip : lane.cyclicClips())
        cyclic.push_back (cyclicExpressionClipToJson (clip));

    JsonValue::Array pitchSlurs;
    pitchSlurs.reserve (lane.pitchSlurs().size());
    for (const auto& slur : lane.pitchSlurs())
        pitchSlurs.push_back (pitchSlurToJson (slur));

    JsonValue::Array vibrato;
    vibrato.reserve (lane.vibratoExpressions().size());
    for (const auto& expression : lane.vibratoExpressions())
        vibrato.push_back (vibratoExpressionToJson (expression));

    return JsonValue::object ({
        { "id", JsonValue::string (lane.id().value) },
        { "name", JsonValue::string (lane.name()) },
        { "polarity", JsonValue::string (expressionPolarityToString (lane.polarity())) },
        { "enabled", JsonValue::boolean (lane.enabled()) },
        { "routes", JsonValue::array (std::move (routes)) },
        { "phraseEnvelopes", JsonValue::array (std::move (phraseEnvelopes)) },
        { "cyclic", JsonValue::array (std::move (cyclic)) },
        { "pitchSlurs", JsonValue::array (std::move (pitchSlurs)) },
        { "vibrato", JsonValue::array (std::move (vibrato)) },
    });
}

ExpressionLane expressionLaneFromJson (const JsonValue& value)
{
    ExpressionLane lane {
        ExpressionLaneId { requireString (requireField (value, "id"), "expression lane ID") },
        requireString (requireField (value, "name"), "expression lane name"),
        expressionPolarityFromString (requireString (requireField (value, "polarity"), "expression lane polarity"))
    };
    lane.setEnabled (requireBool (requireField (value, "enabled"), "expression lane enabled flag"));

    for (const auto& route : requireArray (requireField (value, "routes"), "expression routes"))
        lane.addRoute (expressionRouteFromJson (route));
    for (const auto& envelope : requireArray (requireField (value, "phraseEnvelopes"), "phrase envelopes"))
        lane.addPhraseEnvelopeClip (phraseEnvelopeFromJson (envelope, lane.polarity()));
    for (const auto& clip : requireArray (requireField (value, "cyclic"), "cyclic expression clips"))
        lane.addCyclicClip (cyclicExpressionClipFromJson (clip));
    for (const auto& slur : requireArray (requireField (value, "pitchSlurs"), "pitch slurs"))
        lane.addPitchSlur (pitchSlurFromJson (slur));
    for (const auto& vibrato : requireArray (requireField (value, "vibrato"), "vibrato expressions"))
        lane.addVibratoExpression (vibratoExpressionFromJson (vibrato));

    return lane;
}

void replaceDefaultExpressionLane (ExpressionState& state, ExpressionLane lane)
{
    auto* existingLane = state.findLane (lane.id());
    if (existingLane == nullptr)
        throw std::runtime_error ("Expression state is missing a default lane");

    existingLane->rename (lane.name());
    existingLane->setPolarity (lane.polarity());
    existingLane->setEnabled (lane.enabled());
    for (const auto& route : lane.routes())
        existingLane->addRoute (route);
    for (const auto& clip : lane.phraseEnvelopeClips())
        existingLane->addPhraseEnvelopeClip (clip);
    for (const auto& clip : lane.cyclicClips())
        existingLane->addCyclicClip (clip);
    for (const auto& slur : lane.pitchSlurs())
        existingLane->addPitchSlur (slur);
    for (const auto& vibrato : lane.vibratoExpressions())
        existingLane->addVibratoExpression (vibrato);
}

JsonValue expressionStateToJson (const ExpressionState& state)
{
    JsonValue::Array lanes;
    lanes.reserve (state.lanes().size());
    for (const auto& lane : state.lanes())
        lanes.push_back (expressionLaneToJson (lane));

    return JsonValue::object ({
        { "lanes", JsonValue::array (std::move (lanes)) },
    });
}

ExpressionState expressionStateFromJson (const JsonValue& value)
{
    ExpressionState state;
    std::set<std::string> seenLaneIds;

    for (const auto& laneJson : requireArray (requireField (value, "lanes"), "expression lanes"))
    {
        auto lane = expressionLaneFromJson (laneJson);
        if (! seenLaneIds.insert (lane.id().value).second)
            throw std::runtime_error ("Expression state contains duplicate lane ID '" + lane.id().value + "'");

        if (lane.id() == ExpressionState::defaultVolumeLaneId() || lane.id() == ExpressionState::defaultPitchLaneId())
            replaceDefaultExpressionLane (state, std::move (lane));
        else
            state.addLane (std::move (lane));
    }

    return state;
}

JsonValue midiClipToJson (const MidiClip& clip)
{
    JsonValue::Array notes;
    notes.reserve (clip.notes().size());
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
        { "expression", expressionStateToJson (clip.expressionState()) },
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

    const auto& object = requireObject (value, "MIDI clip");
    if (const auto expression = object.find ("expression"); expression != object.end())
        clip.setExpressionState (expressionStateFromJson (expression->second));

    const auto& notes = requireArray (requireField (value, "notes"), "clip notes");
    if (! notes.empty())
        clip.reserveNotes (notes.size() + std::max<std::size_t> (8, notes.size() / 8));

    std::vector<MidiNote> clipNotes;
    clipNotes.reserve (notes.size());
    for (const auto& note : notes)
        clipNotes.push_back (midiNoteFromJson (note));

    clip.addNotes (std::move (clipNotes));

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

JsonValue firstPartyDeviceStateToJson (const FirstPartyDeviceState& state)
{
    JsonValue::Object parameters;
    for (const auto& parameter : state.parameterValues)
        parameters.emplace (parameter.parameterId, JsonValue::number (parameter.normalizedValue));

    return JsonValue::object ({
        { "typeId", JsonValue::string (state.typeId) },
        { "patchVersion", JsonValue::number (static_cast<double> (state.patchVersion)) },
        { "parameters", JsonValue::object (std::move (parameters)) },
    });
}

FirstPartyDeviceState firstPartyDeviceStateFromJson (const JsonValue& value)
{
    std::vector<FirstPartyDeviceParameterValue> parameterValues;
    const auto& parameters = requireObject (requireField (value, "parameters"), "first-party device parameters");
    parameterValues.reserve (parameters.size());
    for (const auto& [parameterId, parameterValue] : parameters)
    {
        parameterValues.push_back (FirstPartyDeviceParameterValue {
            parameterId,
            parameterValue.asNumber()
        });
    }

    return FirstPartyDeviceState {
        requireString (requireField (value, "typeId"), "first-party device type ID"),
        requireInt (requireField (value, "patchVersion"), "first-party device patch version"),
        std::move (parameterValues)
    };
}

std::optional<TrackInstrumentReference> legacyInstrumentForSerialization (const Track& track)
{
    if (track.instrument().has_value())
        return *track.instrument();

    for (const auto& slot : track.deviceChain().slots())
        if (slot.kind() == PluginKind::instrument && slot.isPluginDevice())
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
    sends.reserve (routing.sends().size());
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
    auto object = JsonValue::Object {
        { "id", JsonValue::string (slot.id().value) },
        { "kind", JsonValue::string (pluginKindId (slot.kind())) },
        { "bypassed", JsonValue::boolean (slot.bypassed()) },
        { "pluginStateFile", JsonValue::string (slot.pluginStateFile()) },
    };

    if (slot.isFirstPartyDevice())
    {
        object.emplace ("deviceType", JsonValue::string ("firstParty"));
        object.emplace ("firstPartyDevice", firstPartyDeviceStateToJson (*slot.firstPartyDevice()));
    }
    else
    {
        object.emplace ("deviceType", JsonValue::string ("plugin"));
        object.emplace ("plugin", pluginReferenceToJson (slot.plugin()));
    }

    return JsonValue::object (std::move (object));
}

DeviceSlot deviceSlotFromJson (const JsonValue& value)
{
    const auto& object = requireObject (value, "device slot");
    const auto deviceType = [&]() {
        const auto type = object.find ("deviceType");
        return type == object.end() ? std::string { "plugin" } : requireString (type->second, "device type");
    }();

    const auto slotId = DeviceSlotId { requireString (requireField (value, "id"), "device slot ID") };
    const auto kind = pluginKindFromId (requireString (requireField (value, "kind"), "plugin kind"));

    DeviceSlot slot = deviceType == "firstParty"
        ? DeviceSlot { slotId, firstPartyDeviceStateFromJson (requireField (value, "firstPartyDevice")), kind }
        : DeviceSlot { slotId, pluginReferenceFromJson (requireField (value, "plugin")), kind };

    slot.setBypassed (requireBool (requireField (value, "bypassed"), "device bypass state"));
    slot.setPluginStateFile (requireString (requireField (value, "pluginStateFile"), "device plugin state file"));
    return slot;
}

JsonValue deviceChainToJson (const DeviceChain& chain)
{
    JsonValue::Array slots;
    slots.reserve (chain.slots().size());
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
    points.reserve (curve.points().size());
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
    clips.reserve (track.clips().size());
    for (const auto& clip : track.clips())
        clips.push_back (midiClipToJson (clip));

    JsonValue::Array audioClips;
    audioClips.reserve (track.audioClips().size());
    for (const auto& clip : track.audioClips())
        audioClips.push_back (audioClipToJson (clip));

    JsonValue::Array automationLanes;
    automationLanes.reserve (track.automationLanes().size());
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
            if (slot.kind() == PluginKind::instrument && slot.isPluginDevice())
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
    keyCenters.reserve (structure.keyCenterRegions().size());
    for (const auto& region : structure.keyCenterRegions())
        keyCenters.push_back (keyCenterRegionToJson (region));

    JsonValue::Array scaleModes;
    scaleModes.reserve (structure.scaleModeRegions().size());
    for (const auto& region : structure.scaleModeRegions())
        scaleModes.push_back (scaleModeRegionToJson (region));

    JsonValue::Array chords;
    chords.reserve (structure.chordRegions().size());
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
    waveformEntries.reserve (project.tracks().size());
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
    tracks.reserve (project.tracks().size());
    for (const auto& track : project.tracks())
        tracks.push_back (trackToJson (track));

    JsonValue::Array customScales;
    customScales.reserve (project.customScales().size());
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
