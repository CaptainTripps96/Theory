#include "core/sequencing/ExpressionPhraseEditing.h"

#include "core/sequencing/ExpressionEvaluation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>

namespace tsq::core::sequencing
{
namespace
{
constexpr double levelStep = 0.05;
constexpr double defaultSustainBelowPeak = 0.15;

double clampLevel (double value, ExpressionLanePolarity polarity) noexcept
{
    return std::clamp (value,
                       expressionLaneMinimumValue (polarity),
                       expressionLaneMaximumValue (polarity));
}

time::TickDuration positiveGrid (time::TickDuration gridDuration) noexcept
{
    return time::TickDuration::fromTicks (std::max<std::int64_t> (1, gridDuration.ticks()));
}

time::TickDuration clippedGridForPhrase (time::TickDuration gridDuration, const Region& region) noexcept
{
    return time::TickDuration::fromTicks (std::min (positiveGrid (gridDuration).ticks(), region.duration().ticks()));
}

time::TickDuration stageDuration (const std::optional<EnvelopeStage>& stage) noexcept
{
    return stage.has_value() ? stage->duration : time::TickDuration {};
}

time::TickDuration totalStageDuration (const PhraseEnvelopeClip& envelope) noexcept
{
    return envelope.attackStage().duration + stageDuration (envelope.decayStage()) + stageDuration (envelope.releaseStage());
}

time::TickDuration totalVibratoSettingsDuration (const VibratoExpression& vibrato) noexcept
{
    return vibrato.attackTime() + vibrato.releaseTime();
}

bool canGrow (const PhraseEnvelopeClip& envelope, time::TickDuration gridDuration) noexcept
{
    return totalStageDuration (envelope) + positiveGrid (gridDuration) <= envelope.phraseRegion().duration();
}

double sustainOrStoredLevel (const PhraseEnvelopeClip& envelope) noexcept
{
    return envelope.sustainLevel().value_or (envelope.storedLevel());
}

void setAttackEnd (PhraseEnvelopeClip& envelope, double endLevel, ExpressionLanePolarity polarity)
{
    auto attack = envelope.attackStage();
    attack.endLevel = clampLevel (endLevel, polarity);
    envelope.setAttackStage (attack);
}

EnvelopeStage makeReleaseStage (time::TickDuration duration, double startLevel, double endLevel)
{
    return EnvelopeStage { EnvelopeStageType::release, duration, startLevel, endLevel };
}

bool growAttack (PhraseEnvelopeClip& envelope, time::TickDuration gridDuration)
{
    if (! canGrow (envelope, gridDuration))
        return false;

    auto attack = envelope.attackStage();
    attack.duration = attack.duration + positiveGrid (gridDuration);
    envelope.setAttackStage (attack);
    return true;
}

bool shrinkAttack (PhraseEnvelopeClip& envelope, time::TickDuration gridDuration)
{
    auto attack = envelope.attackStage();
    const auto grid = positiveGrid (gridDuration);
    const auto newTicks = std::max<std::int64_t> (0, attack.duration.ticks() - grid.ticks());
    if (newTicks == attack.duration.ticks())
        return false;

    attack.duration = time::TickDuration::fromTicks (newTicks);
    envelope.setAttackStage (attack);
    return true;
}

bool ensureDecay (PhraseEnvelopeClip& envelope, time::TickDuration gridDuration, ExpressionLanePolarity polarity)
{
    if (envelope.decayStage().has_value())
        return false;

    if (! canGrow (envelope, gridDuration))
        return false;

    const auto peak = defaultPhraseEnvelopePeakLevel (polarity);
    const auto sustain = clampLevel (peak - defaultSustainBelowPeak, polarity);
    envelope.setPeakLevel (peak, polarity);
    envelope.setSustainLevel (sustain, polarity);
    setAttackEnd (envelope, peak, polarity);

    envelope.setDecayStage (EnvelopeStage { EnvelopeStageType::decay, positiveGrid (gridDuration), peak, sustain });
    return true;
}

bool growDecay (PhraseEnvelopeClip& envelope, time::TickDuration gridDuration, ExpressionLanePolarity polarity)
{
    if (! envelope.decayStage().has_value())
        return ensureDecay (envelope, gridDuration, polarity);

    if (! canGrow (envelope, gridDuration))
        return false;

    auto decay = *envelope.decayStage();
    decay.duration = decay.duration + positiveGrid (gridDuration);
    envelope.setDecayStage (decay);
    return true;
}

bool shrinkDecay (PhraseEnvelopeClip& envelope, time::TickDuration gridDuration)
{
    if (! envelope.decayStage().has_value())
        return false;

    auto decay = *envelope.decayStage();
    const auto grid = positiveGrid (gridDuration);
    if (decay.duration <= grid)
    {
        envelope.setDecayStage (std::nullopt);
        envelope.setPeakLevel (std::nullopt, ExpressionLanePolarity::unipolar);
        envelope.setSustainLevel (std::nullopt, ExpressionLanePolarity::unipolar);
        auto attack = envelope.attackStage();
        attack.endLevel = envelope.storedLevel();
        envelope.setAttackStage (attack);
        return true;
    }

    decay.duration = decay.duration - grid;
    envelope.setDecayStage (decay);
    return true;
}

bool growRelease (PhraseEnvelopeClip& envelope, time::TickDuration gridDuration, ExpressionLanePolarity polarity)
{
    if (! envelope.releaseStage().has_value())
    {
        if (! canGrow (envelope, gridDuration))
            return false;

        envelope.setReleaseStage (makeReleaseStage (positiveGrid (gridDuration),
                                                    sustainOrStoredLevel (envelope),
                                                    defaultPhraseEnvelopeReleaseEndLevel (polarity)));
        return true;
    }

    if (! canGrow (envelope, gridDuration))
        return false;

    auto release = *envelope.releaseStage();
    release.duration = release.duration + positiveGrid (gridDuration);
    release.startLevel = sustainOrStoredLevel (envelope);
    envelope.setReleaseStage (release);
    return true;
}

bool shrinkRelease (PhraseEnvelopeClip& envelope, time::TickDuration gridDuration)
{
    if (! envelope.releaseStage().has_value())
        return false;

    auto release = *envelope.releaseStage();
    const auto grid = positiveGrid (gridDuration);
    if (release.duration <= grid)
    {
        envelope.setReleaseStage (std::nullopt);
        return true;
    }

    release.duration = release.duration - grid;
    envelope.setReleaseStage (release);
    return true;
}

bool adjustAttackStart (PhraseEnvelopeClip& envelope, double delta, ExpressionLanePolarity polarity)
{
    auto attack = envelope.attackStage();
    const auto next = clampLevel (attack.startLevel + delta, polarity);
    if (std::abs (next - attack.startLevel) < 0.000001)
        return false;

    attack.startLevel = next;
    envelope.setAttackStage (attack);
    return true;
}

bool adjustSustain (PhraseEnvelopeClip& envelope, double delta, ExpressionLanePolarity polarity)
{
    if (! envelope.decayStage().has_value())
        return false;

    const auto current = envelope.sustainLevel().value_or (envelope.decayStage()->endLevel);
    const auto next = clampLevel (current + delta, polarity);
    if (std::abs (next - current) < 0.000001)
        return false;

    envelope.setSustainLevel (next, polarity);
    auto decay = *envelope.decayStage();
    decay.endLevel = next;
    envelope.setDecayStage (decay);
    if (envelope.releaseStage().has_value())
    {
        auto release = *envelope.releaseStage();
        release.startLevel = next;
        envelope.setReleaseStage (release);
    }
    return true;
}

bool adjustReleaseEnd (PhraseEnvelopeClip& envelope, double delta, ExpressionLanePolarity polarity)
{
    if (! envelope.releaseStage().has_value())
        return false;

    auto release = *envelope.releaseStage();
    const auto next = clampLevel (release.endLevel + delta, polarity);
    if (std::abs (next - release.endLevel) < 0.000001)
        return false;

    release.endLevel = next;
    envelope.setReleaseStage (release);
    return true;
}

bool adjustPeak (PhraseEnvelopeClip& envelope, double delta, ExpressionLanePolarity polarity)
{
    if (! envelope.decayStage().has_value() || ! envelope.peakLevel().has_value())
        return false;

    const auto next = clampLevel (*envelope.peakLevel() + delta, polarity);
    if (std::abs (next - *envelope.peakLevel()) < 0.000001)
        return false;

    envelope.setPeakLevel (next, polarity);
    setAttackEnd (envelope, next, polarity);
    auto decay = *envelope.decayStage();
    decay.startLevel = next;
    envelope.setDecayStage (decay);
    return true;
}

bool cycleCurve (PhraseEnvelopeClip& envelope, PhraseEnvelopeActiveSegment activeSegment, bool forward)
{
    switch (activeSegment)
    {
        case PhraseEnvelopeActiveSegment::decay:
            if (envelope.decayStage().has_value())
            {
                auto decay = *envelope.decayStage();
                decay.curveShape = nextPhraseEnvelopeCurveShape (decay.curveShape, forward);
                envelope.setDecayStage (decay);
                return true;
            }
            break;
        case PhraseEnvelopeActiveSegment::release:
            if (envelope.releaseStage().has_value())
            {
                auto release = *envelope.releaseStage();
                release.curveShape = nextPhraseEnvelopeCurveShape (release.curveShape, forward);
                envelope.setReleaseStage (release);
                return true;
            }
            break;
        case PhraseEnvelopeActiveSegment::attack:
            break;
    }

    auto attack = envelope.attackStage();
    attack.curveShape = nextPhraseEnvelopeCurveShape (attack.curveShape, forward);
    envelope.setAttackStage (attack);
    return true;
}
}

ExpressionLanePreset createExpressionLanePreset (const ExpressionLane& lane)
{
    return ExpressionLanePreset {
        lane.name(),
        lane.polarity(),
        lane.enabled(),
        lane.routes()
    };
}

ExpressionLane createExpressionLaneFromPreset (ExpressionLaneId laneId, const ExpressionLanePreset& preset)
{
    auto lane = ExpressionLane { std::move (laneId), preset.name, preset.polarity };
    lane.setEnabled (preset.enabled);
    for (const auto& route : preset.routes)
        lane.addRoute (route);
    return lane;
}

ExpressionLane duplicateExpressionLaneRouting (ExpressionLaneId laneId, const ExpressionLane& sourceLane)
{
    return createExpressionLaneFromPreset (std::move (laneId), createExpressionLanePreset (sourceLane));
}

std::optional<Region> phraseRegionForSelectedNotes (const MidiClip& clip,
                                                    const std::vector<std::string>& selectedNoteIds,
                                                    const std::vector<ReleaseGhostNote>& releaseGhosts)
{
    if (selectedNoteIds.empty())
        return std::nullopt;

    auto selected = std::set<std::string> { selectedNoteIds.begin(), selectedNoteIds.end() };
    auto startTicks = std::optional<std::int64_t> {};
    auto endTicks = std::optional<std::int64_t> {};

    for (const auto& note : clip.notes())
    {
        if (! selected.contains (note.id()))
            continue;

        startTicks = startTicks.has_value() ? std::min (*startTicks, note.startInClip().ticks()) : note.startInClip().ticks();
        endTicks = endTicks.has_value() ? std::max (*endTicks, note.endInClip().ticks()) : note.endInClip().ticks();
    }

    if (! startTicks.has_value() || ! endTicks.has_value())
        return std::nullopt;

    for (const auto& ghost : releaseGhosts)
    {
        if (selected.contains (ghost.noteId))
            endTicks = std::max (*endTicks, ghost.phraseRegion.end().ticks());
    }

    if (*endTicks <= *startTicks)
        return std::nullopt;

    return Region { time::TickPosition::fromTicks (*startTicks), time::TickPosition::fromTicks (*endTicks) };
}

double defaultPhraseEnvelopeStartLevel (ExpressionLanePolarity polarity) noexcept
{
    return expressionLaneMinimumValue (polarity);
}

double defaultPhraseEnvelopeReleaseEndLevel (ExpressionLanePolarity polarity) noexcept
{
    return expressionLaneMinimumValue (polarity);
}

double defaultPhraseEnvelopePeakLevel (ExpressionLanePolarity polarity) noexcept
{
    return polarity == ExpressionLanePolarity::bipolar ? 0.35 : 0.65;
}

std::optional<PhraseEnvelopeClip> createPhraseEnvelopeForSelection (ExpressionClipId id,
                                                                    const MidiClip& clip,
                                                                    std::vector<std::string> selectedNoteIds,
                                                                    time::TickDuration gridDuration,
                                                                    double storedLevel,
                                                                    ExpressionLanePolarity polarity,
                                                                    const std::vector<ReleaseGhostNote>& releaseGhosts)
{
    auto region = phraseRegionForSelectedNotes (clip, selectedNoteIds, releaseGhosts);
    if (! region.has_value())
        return std::nullopt;

    const auto attackDuration = clippedGridForPhrase (gridDuration, *region);
    if (attackDuration.ticks() <= 0)
        return std::nullopt;

    const auto clampedStoredLevel = clampLevel (storedLevel, polarity);
    return PhraseEnvelopeClip {
        std::move (id),
        std::move (selectedNoteIds),
        *region,
        clampedStoredLevel,
        EnvelopeStage {
            EnvelopeStageType::attack,
            attackDuration,
            defaultPhraseEnvelopeStartLevel (polarity),
            clampedStoredLevel
        }
    };
}

bool editPhraseEnvelope (PhraseEnvelopeClip& envelope,
                         PhraseEnvelopeEditKey editKey,
                         PhraseEnvelopeEditDirection direction,
                         time::TickDuration gridDuration,
                         ExpressionLanePolarity polarity,
                         PhraseEnvelopeActiveSegment& activeSegment)
{
    const auto levelDelta = direction == PhraseEnvelopeEditDirection::up
        ? levelStep
        : (direction == PhraseEnvelopeEditDirection::down ? -levelStep : 0.0);

    switch (editKey)
    {
        case PhraseEnvelopeEditKey::attack:
            activeSegment = PhraseEnvelopeActiveSegment::attack;
            if (direction == PhraseEnvelopeEditDirection::right)
                return growAttack (envelope, gridDuration);
            if (direction == PhraseEnvelopeEditDirection::left)
                return shrinkAttack (envelope, gridDuration);
            if (direction == PhraseEnvelopeEditDirection::up || direction == PhraseEnvelopeEditDirection::down)
                return adjustAttackStart (envelope, levelDelta, polarity);
            return false;

        case PhraseEnvelopeEditKey::decay:
            activeSegment = PhraseEnvelopeActiveSegment::decay;
            if (direction == PhraseEnvelopeEditDirection::right)
                return growDecay (envelope, gridDuration, polarity);
            if (direction == PhraseEnvelopeEditDirection::left)
            {
                if (! envelope.decayStage().has_value())
                    return ensureDecay (envelope, gridDuration, polarity);

                return shrinkDecay (envelope, gridDuration);
            }
            if (direction == PhraseEnvelopeEditDirection::up || direction == PhraseEnvelopeEditDirection::down)
            {
                if (! envelope.decayStage().has_value() && ! ensureDecay (envelope, gridDuration, polarity))
                    return false;

                return adjustSustain (envelope, levelDelta, polarity);
            }
            return false;

        case PhraseEnvelopeEditKey::release:
            activeSegment = PhraseEnvelopeActiveSegment::release;
            if (direction == PhraseEnvelopeEditDirection::left)
                return growRelease (envelope, gridDuration, polarity);
            if (direction == PhraseEnvelopeEditDirection::right)
            {
                if (! envelope.releaseStage().has_value())
                    return growRelease (envelope, gridDuration, polarity);

                return shrinkRelease (envelope, gridDuration);
            }
            if (direction == PhraseEnvelopeEditDirection::up || direction == PhraseEnvelopeEditDirection::down)
                return adjustReleaseEnd (envelope, levelDelta, polarity);
            return false;

        case PhraseEnvelopeEditKey::force:
            if (direction == PhraseEnvelopeEditDirection::up || direction == PhraseEnvelopeEditDirection::down)
                return adjustPeak (envelope, levelDelta, polarity);
            return false;

        case PhraseEnvelopeEditKey::curve:
            if (direction == PhraseEnvelopeEditDirection::up || direction == PhraseEnvelopeEditDirection::down)
                return cycleCurve (envelope, activeSegment, direction == PhraseEnvelopeEditDirection::up);
            return false;
    }

    return false;
}

bool copyPhraseEnvelopeSettings (const PhraseEnvelopeClip& source,
                                 PhraseEnvelopeClip& target,
                                 ExpressionLanePolarity polarity)
{
    if (totalStageDuration (source) > target.phraseRegion().duration())
        return false;

    auto edited = target;
    edited.setStoredLevel (source.storedLevel(), polarity);
    edited.setAttackStage (source.attackStage());
    edited.setDecayStage (source.decayStage());
    edited.setReleaseStage (source.releaseStage());
    edited.setPeakLevel (source.peakLevel(), polarity);
    edited.setSustainLevel (source.sustainLevel(), polarity);
    edited.setTailExtension (source.tailExtension());
    target = std::move (edited);
    return true;
}

void copyPitchSlurSettings (const PitchSlur& source, PitchSlur& target)
{
    target.setSlurTime (source.slurTime());
    target.setCurveShape (source.curveShape());
    target.setLegatoNoRetrigger (source.legatoNoRetrigger());
    target.setHasVoiceOverride (source.hasVoiceOverride());
}

bool copyVibratoSettings (const VibratoExpression& source, VibratoExpression& target)
{
    if (totalVibratoSettingsDuration (source) > target.phraseRegion().duration())
        return false;

    auto edited = target;
    edited.setAttackTime (source.attackTime());
    edited.setReleaseTime (source.releaseTime());
    edited.setAmplitudeSemitones (source.amplitudeSemitones());
    edited.setFrequencyDivisionId (source.frequencyDivisionId());
    edited.setWaveShape (source.waveShape());
    edited.setPhase (source.phase());
    target = std::move (edited);
    return true;
}

ExpressionCurveShape nextPhraseEnvelopeCurveShape (ExpressionCurveShape shape, bool forward) noexcept
{
    if (forward)
    {
        switch (shape)
        {
            case ExpressionCurveShape::linear: return ExpressionCurveShape::logarithmic;
            case ExpressionCurveShape::logarithmic: return ExpressionCurveShape::exponential;
            case ExpressionCurveShape::exponential: return ExpressionCurveShape::linear;
        }
    }

    switch (shape)
    {
        case ExpressionCurveShape::linear: return ExpressionCurveShape::exponential;
        case ExpressionCurveShape::logarithmic: return ExpressionCurveShape::linear;
        case ExpressionCurveShape::exponential: return ExpressionCurveShape::logarithmic;
    }

    return ExpressionCurveShape::linear;
}
}
