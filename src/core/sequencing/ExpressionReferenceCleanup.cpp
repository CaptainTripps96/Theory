#include "core/sequencing/ExpressionReferenceCleanup.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tsq::core::sequencing
{
namespace
{
std::unordered_set<std::string> noteIdsForClip (const MidiClip& clip)
{
    std::unordered_set<std::string> ids;
    ids.reserve (clip.notes().size());
    for (const auto& note : clip.notes())
        ids.insert (note.id());
    return ids;
}

bool containsNoteId (const std::unordered_set<std::string>& validNoteIds, const std::string& noteId)
{
    return validNoteIds.find (noteId) != validNoteIds.end();
}

bool allNotesExist (const std::unordered_set<std::string>& validNoteIds, const std::vector<std::string>& noteIds)
{
    return std::all_of (noteIds.begin(), noteIds.end(), [&validNoteIds] (const auto& noteId)
    {
        return containsNoteId (validNoteIds, noteId);
    });
}

std::vector<VibratoVoiceOverride> validVibratoVoiceOverrides (
    const std::unordered_set<std::string>& validNoteIds,
    const std::vector<VibratoVoiceOverride>& overrides,
    std::size_t& removedCount)
{
    std::vector<VibratoVoiceOverride> kept;
    kept.reserve (overrides.size());

    for (const auto& override : overrides)
    {
        if (containsNoteId (validNoteIds, override.noteId))
            kept.push_back (override);
        else
            ++removedCount;
    }

    return kept;
}
}

bool ExpressionReferenceCleanupResult::changed() const noexcept
{
    return removedPhraseEnvelopeCount > 0
        || removedCyclicExpressionCount > 0
        || removedPitchSlurCount > 0
        || removedVibratoExpressionCount > 0
        || removedVibratoVoiceOverrideCount > 0;
}

ExpressionReferenceCleanupResult removeExpressionReferencesToMissingNotes (MidiClip& clip)
{
    const auto validNoteIds = noteIdsForClip (clip);
    auto expression = clip.expressionState();
    ExpressionReferenceCleanupResult result;

    for (const auto& laneSnapshot : expression.lanes())
    {
        auto* lane = expression.findLane (laneSnapshot.id());
        if (lane == nullptr)
            continue;

        std::vector<ExpressionClipId> phraseEnvelopeIdsToRemove;
        for (const auto& envelope : lane->phraseEnvelopeClips())
            if (! allNotesExist (validNoteIds, envelope.sourceNoteIds()))
                phraseEnvelopeIdsToRemove.push_back (envelope.id());

        for (const auto& envelopeId : phraseEnvelopeIdsToRemove)
        {
            lane->removePhraseEnvelopeClip (envelopeId);
            ++result.removedPhraseEnvelopeCount;
        }

        std::vector<ExpressionClipId> cyclicIdsToRemove;
        for (const auto& cyclic : lane->cyclicClips())
            if (! allNotesExist (validNoteIds, cyclic.sourceNoteIds()))
                cyclicIdsToRemove.push_back (cyclic.id());

        for (const auto& cyclicId : cyclicIdsToRemove)
        {
            lane->removeCyclicClip (cyclicId);
            ++result.removedCyclicExpressionCount;
        }

        std::vector<ExpressionClipId> slurIdsToRemove;
        for (const auto& slur : lane->pitchSlurs())
            if (! containsNoteId (validNoteIds, slur.sourceNoteId())
                || ! containsNoteId (validNoteIds, slur.destinationNoteId()))
                slurIdsToRemove.push_back (slur.id());

        for (const auto& slurId : slurIdsToRemove)
        {
            lane->removePitchSlur (slurId);
            ++result.removedPitchSlurCount;
        }

        std::vector<ExpressionClipId> vibratoIdsToRemove;
        for (const auto& vibrato : lane->vibratoExpressions())
        {
            if (! allNotesExist (validNoteIds, vibrato.sourceNoteIds()))
            {
                vibratoIdsToRemove.push_back (vibrato.id());
                continue;
            }

            auto keptOverrides = validVibratoVoiceOverrides (
                validNoteIds,
                vibrato.voiceOverrides(),
                result.removedVibratoVoiceOverrideCount);

            if (keptOverrides.size() != vibrato.voiceOverrides().size())
                if (auto* mutableVibrato = lane->findVibratoExpression (vibrato.id()))
                    mutableVibrato->setVoiceOverrides (std::move (keptOverrides));
        }

        for (const auto& vibratoId : vibratoIdsToRemove)
        {
            lane->removeVibratoExpression (vibratoId);
            ++result.removedVibratoExpressionCount;
        }
    }

    if (result.changed())
        clip.setExpressionState (std::move (expression));

    return result;
}
}
