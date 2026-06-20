#include "core/sequencing/PitchExpressionEvaluation.h"

#include "core/sequencing/ExpressionEvaluation.h"
#include "core/time/GridDivision.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
constexpr auto pi = 3.141592653589793238462643383279502884;

struct RankedNote
{
    std::string id;
    int pitch = 0;
    time::TickPosition start {};
};

const MidiNote& requireNote (const MidiClip& clip, const std::string& noteId)
{
    const auto* note = clip.findNoteById (noteId);
    if (note == nullptr)
        throw std::invalid_argument ("Pitch expression note reference is missing");

    return *note;
}

std::vector<RankedNote> rankedNotes (const MidiClip& clip, const std::vector<std::string>& noteIds)
{
    if (noteIds.empty())
        throw std::invalid_argument ("Pitch expression note pairing requires selected notes");

    std::vector<RankedNote> result;
    result.reserve (noteIds.size());
    for (const auto& noteId : noteIds)
    {
        const auto& note = requireNote (clip, noteId);
        result.push_back (RankedNote { note.id(), note.pitch().value(), note.startInClip() });
    }

    std::sort (result.begin(), result.end(), [] (const auto& lhs, const auto& rhs)
    {
        if (lhs.pitch != rhs.pitch)
            return lhs.pitch < rhs.pitch;
        if (lhs.start != rhs.start)
            return lhs.start < rhs.start;
        return lhs.id < rhs.id;
    });

    return result;
}

void applySettingsToSlur (PitchSlur& slur, const PitchSlurBlockSettings& settings, bool markOverride)
{
    slur.setSlurTime (settings.slurTime);
    slur.setCurveShape (settings.curveShape);
    slur.setLegatoNoRetrigger (settings.legatoNoRetrigger);
    if (settings.blockId.has_value())
        slur.setBlockId (settings.blockId);
    slur.setHasVoiceOverride (markOverride);
}

double clampFinite (double value, double minimum, double maximum) noexcept
{
    return std::clamp (std::isfinite (value) ? value : 0.0, minimum, maximum);
}

double positiveModulo (double value, double modulus) noexcept
{
    if (modulus <= 0.0)
        return 0.0;

    auto result = std::fmod (value, modulus);
    if (result < 0.0)
        result += modulus;
    return result;
}

double bipolarWave (CyclicWaveShape shape, double phase) noexcept
{
    const auto normalizedPhase = positiveModulo (phase, 1.0);

    switch (shape)
    {
        case CyclicWaveShape::sine:
            return std::sin (normalizedPhase * 2.0 * pi);

        case CyclicWaveShape::triangle:
            return 1.0 - (4.0 * std::abs (normalizedPhase - 0.5));

        case CyclicWaveShape::rampUp:
            return (normalizedPhase * 2.0) - 1.0;

        case CyclicWaveShape::rampDown:
            return 1.0 - (normalizedPhase * 2.0);

        case CyclicWaveShape::square:
            return normalizedPhase < 0.5 ? 1.0 : -1.0;
    }

    return 0.0;
}

time::TickDuration frequencyDurationFor (std::string_view divisionId, const time::ProjectRhythmSettings& rhythmSettings) noexcept
{
    const auto definition = time::gridDivisionDefinitionFor (divisionId, rhythmSettings);
    if (definition.tickDuration.ticks() <= 0)
        return time::sixteenthNoteDuration();

    return definition.tickDuration;
}

double fadeForRegion (const Region& region,
                      time::TickPosition position,
                      time::TickDuration attackTime,
                      time::TickDuration releaseTime) noexcept
{
    auto attack = 1.0;
    if (attackTime.ticks() > 0)
    {
        const auto elapsed = static_cast<double> ((position - region.start()).ticks());
        attack = clampFinite (elapsed / static_cast<double> (attackTime.ticks()), 0.0, 1.0);
    }

    auto release = 1.0;
    if (releaseTime.ticks() > 0)
    {
        const auto releaseStart = region.end() - releaseTime;
        if (position >= releaseStart)
        {
            const auto elapsed = static_cast<double> ((position - releaseStart).ticks());
            release = 1.0 - clampFinite (elapsed / static_cast<double> (releaseTime.ticks()), 0.0, 1.0);
        }
    }

    return std::min (attack, release);
}

std::optional<VibratoVoiceOverride> overrideForNote (const VibratoExpression& vibrato, const std::string& noteId)
{
    const auto match = std::find_if (vibrato.voiceOverrides().begin(),
                                     vibrato.voiceOverrides().end(),
                                     [&noteId] (const auto& candidate)
                                     {
                                         return candidate.noteId == noteId;
                                     });

    if (match == vibrato.voiceOverrides().end())
        return std::nullopt;

    return *match;
}

double evaluateVibratoAt (const VibratoExpression& vibrato,
                          const std::string& noteId,
                          time::TickPosition position,
                          const time::ProjectRhythmSettings& rhythmSettings) noexcept
{
    if (! vibrato.phraseRegion().contains (position))
        return 0.0;

    if (std::find (vibrato.sourceNoteIds().begin(), vibrato.sourceNoteIds().end(), noteId) == vibrato.sourceNoteIds().end())
        return 0.0;

    auto amplitudeSemitones = vibrato.amplitudeSemitones();
    auto attackTime = vibrato.attackTime();
    auto releaseTime = vibrato.releaseTime();
    auto frequencyDivisionId = vibrato.frequencyDivisionId();
    auto waveShape = vibrato.waveShape();
    auto phase = vibrato.phase();

    if (const auto voiceOverride = overrideForNote (vibrato, noteId))
    {
        amplitudeSemitones = voiceOverride->amplitudeSemitones;
        attackTime = voiceOverride->attackTime;
        releaseTime = voiceOverride->releaseTime;
        frequencyDivisionId = voiceOverride->frequencyDivisionId;
        waveShape = voiceOverride->waveShape;
        phase = voiceOverride->phase;
    }

    const auto duration = frequencyDurationFor (frequencyDivisionId, rhythmSettings);
    const auto elapsedTicks = static_cast<double> ((position - vibrato.phraseRegion().start()).ticks());
    const auto cycleTicks = static_cast<double> (duration.ticks());
    const auto normalizedPhase = (cycleTicks <= 0.0 ? 0.0 : elapsedTicks / cycleTicks) + phase;
    return bipolarWave (waveShape, normalizedPhase)
        * std::max (0.0, amplitudeSemitones)
        * fadeForRegion (vibrato.phraseRegion(), position, attackTime, releaseTime);
}

std::optional<const PitchSlur*> activeSlurForVoice (const ExpressionLane& pitchLane,
                                                    const std::string& voiceNoteId)
{
    for (const auto& slur : pitchLane.pitchSlurs())
        if (slur.sourceNoteId() == voiceNoteId)
            return &slur;

    return std::nullopt;
}
}

std::vector<PitchSlurNotePair> pairNotesByRegister (const MidiClip& clip,
                                                    const std::vector<std::string>& sourceNoteIds,
                                                    const std::vector<std::string>& destinationNoteIds)
{
    if (sourceNoteIds.size() != destinationNoteIds.size())
        throw std::invalid_argument ("Pitch slur source and destination selections must contain the same number of notes");

    const auto sources = rankedNotes (clip, sourceNoteIds);
    const auto destinations = rankedNotes (clip, destinationNoteIds);

    std::vector<PitchSlurNotePair> pairs;
    pairs.reserve (sources.size());
    for (auto index = std::size_t {}; index < sources.size(); ++index)
    {
        if (sources[index].id == destinations[index].id)
            throw std::invalid_argument ("Pitch slur source and destination notes must differ");

        pairs.push_back (PitchSlurNotePair { sources[index].id, destinations[index].id });
    }

    return pairs;
}

std::vector<PitchSlur> createLegatoPitchSlurBlock (const MidiClip& clip,
                                                   const std::vector<std::string>& sourceNoteIds,
                                                   const std::vector<std::string>& destinationNoteIds,
                                                   const ExpressionBlockId& blockId,
                                                   std::string idPrefix)
{
    if (blockId.empty())
        throw std::invalid_argument ("Pitch slur block ID must not be empty");
    if (idPrefix.empty())
        idPrefix = "slur";

    const auto pairs = pairNotesByRegister (clip, sourceNoteIds, destinationNoteIds);
    std::vector<PitchSlur> slurs;
    slurs.reserve (pairs.size());

    for (auto index = std::size_t {}; index < pairs.size(); ++index)
    {
        PitchSlur slur {
            ExpressionClipId { idPrefix + "-" + std::to_string (index + 1) },
            pairs[index].sourceNoteId,
            pairs[index].destinationNoteId
        };
        slur.setSlurTime (time::TickDuration {});
        slur.setLegatoNoRetrigger (true);
        slur.setBlockId (blockId);
        slurs.push_back (std::move (slur));
    }

    return slurs;
}

void applySharedSlurBlockSettings (ExpressionLane& pitchLane,
                                   const PitchSlurBlockSettings& settings)
{
    if (! settings.blockId.has_value() || settings.blockId->empty())
        throw std::invalid_argument ("Shared slur block edits require a block ID");

    std::vector<ExpressionClipId> slurIds;
    slurIds.reserve (pitchLane.pitchSlurs().size());
    for (const auto& slur : pitchLane.pitchSlurs())
        if (slur.blockId() == settings.blockId && ! slur.hasVoiceOverride())
            slurIds.push_back (slur.id());

    for (const auto& slurId : slurIds)
    {
        auto* slur = pitchLane.findPitchSlur (slurId);
        if (slur == nullptr)
            continue;

        applySettingsToSlur (*slur, settings, false);
    }
}

void applySlurVoiceOverride (ExpressionLane& pitchLane,
                             const ExpressionClipId& slurId,
                             const PitchSlurBlockSettings& settings)
{
    auto* slur = pitchLane.findPitchSlur (slurId);
    if (slur == nullptr)
        throw std::invalid_argument ("Pitch slur voice override target is missing");

    applySettingsToSlur (*slur, settings, true);
}

PitchVoiceTrajectorySample evaluatePitchVoiceTrajectoryAt (const MidiClip& clip,
                                                           const ExpressionLane& pitchLane,
                                                           const std::string& voiceNoteId,
                                                           time::TickPosition position,
                                                           const time::ProjectRhythmSettings& rhythmSettings)
{
    const auto& sourceNote = requireNote (clip, voiceNoteId);

    PitchVoiceTrajectorySample sample;
    sample.voiceNoteId = voiceNoteId;
    sample.position = position;
    sample.baseSemitones = static_cast<double> (sourceNote.pitch().value());

    if (const auto activeSlur = activeSlurForVoice (pitchLane, voiceNoteId))
    {
        const auto& slur = **activeSlur;
        const auto& destinationNote = requireNote (clip, slur.destinationNoteId());
        if (position >= destinationNote.startInClip())
        {
            const auto pitchDelta = static_cast<double> (destinationNote.pitch().value() - sourceNote.pitch().value());
            auto alpha = 1.0;
            if (slur.slurTime().ticks() > 0)
            {
                const auto elapsed = static_cast<double> ((position - destinationNote.startInClip()).ticks());
                alpha = clampFinite (elapsed / static_cast<double> (slur.slurTime().ticks()), 0.0, 1.0);
            }

            sample.slurOffsetSemitones = pitchDelta * evaluateExpressionCurve (slur.curveShape(), alpha);
        }
    }

    for (const auto& vibrato : pitchLane.vibratoExpressions())
        sample.vibratoOffsetSemitones += evaluateVibratoAt (vibrato, voiceNoteId, position, rhythmSettings);

    sample.finalSemitones = sample.baseSemitones + sample.slurOffsetSemitones + sample.vibratoOffsetSemitones;
    return sample;
}
}
