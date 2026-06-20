#pragma once

#include "core/sequencing/Expression.h"
#include "core/sequencing/ExpressionReleaseGhosts.h"
#include "core/sequencing/MidiClip.h"
#include "core/time/Tick.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
enum class PhraseEnvelopeEditKey
{
    attack,
    decay,
    release,
    force,
    curve
};

enum class PhraseEnvelopeEditDirection
{
    left,
    right,
    up,
    down
};

enum class PhraseEnvelopeActiveSegment
{
    attack,
    decay,
    release
};

struct ExpressionLanePreset
{
    std::string name;
    ExpressionLanePolarity polarity = ExpressionLanePolarity::unipolar;
    bool enabled = true;
    std::vector<ExpressionRoute> routes;
};

ExpressionLanePreset createExpressionLanePreset (const ExpressionLane& lane);
ExpressionLane createExpressionLaneFromPreset (ExpressionLaneId laneId, const ExpressionLanePreset& preset);
ExpressionLane duplicateExpressionLaneRouting (ExpressionLaneId laneId, const ExpressionLane& sourceLane);

std::optional<Region> phraseRegionForSelectedNotes (const MidiClip& clip,
                                                    const std::vector<std::string>& selectedNoteIds,
                                                    const std::vector<ReleaseGhostNote>& releaseGhosts = {});

double defaultPhraseEnvelopeStartLevel (ExpressionLanePolarity polarity) noexcept;
double defaultPhraseEnvelopeReleaseEndLevel (ExpressionLanePolarity polarity) noexcept;
double defaultPhraseEnvelopePeakLevel (ExpressionLanePolarity polarity) noexcept;

std::optional<PhraseEnvelopeClip> createPhraseEnvelopeForSelection (ExpressionClipId id,
                                                                    const MidiClip& clip,
                                                                    std::vector<std::string> selectedNoteIds,
                                                                    time::TickDuration gridDuration,
                                                                    double storedLevel,
                                                                    ExpressionLanePolarity polarity,
                                                                    const std::vector<ReleaseGhostNote>& releaseGhosts = {});

bool editPhraseEnvelope (PhraseEnvelopeClip& envelope,
                         PhraseEnvelopeEditKey editKey,
                         PhraseEnvelopeEditDirection direction,
                         time::TickDuration gridDuration,
                         ExpressionLanePolarity polarity,
                         PhraseEnvelopeActiveSegment& activeSegment);

bool copyPhraseEnvelopeSettings (const PhraseEnvelopeClip& source,
                                 PhraseEnvelopeClip& target,
                                 ExpressionLanePolarity polarity);

void copyPitchSlurSettings (const PitchSlur& source, PitchSlur& target);
bool copyVibratoSettings (const VibratoExpression& source, VibratoExpression& target);

ExpressionCurveShape nextPhraseEnvelopeCurveShape (ExpressionCurveShape shape, bool forward) noexcept;
}
