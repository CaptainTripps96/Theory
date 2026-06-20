#pragma once

#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiClip.h"

#include <cstddef>

namespace tsq::core::sequencing
{
struct ExpressionReferenceCleanupResult
{
    std::size_t removedPhraseEnvelopeCount = 0;
    std::size_t removedCyclicExpressionCount = 0;
    std::size_t removedPitchSlurCount = 0;
    std::size_t removedVibratoExpressionCount = 0;
    std::size_t removedVibratoVoiceOverrideCount = 0;

    bool changed() const noexcept;
};

ExpressionReferenceCleanupResult removeExpressionReferencesToMissingNotes (MidiClip& clip);
}
