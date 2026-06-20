#pragma once

#include "core/sequencing/Expression.h"
#include "core/sequencing/MidiClip.h"
#include "core/time/ProjectRhythmSettings.h"

#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
struct PitchSlurNotePair
{
    std::string sourceNoteId;
    std::string destinationNoteId;
};

struct PitchSlurBlockSettings
{
    std::optional<ExpressionBlockId> blockId;
    time::TickDuration slurTime {};
    ExpressionCurveShape curveShape = ExpressionCurveShape::linear;
    bool legatoNoRetrigger = true;
};

struct PitchVoiceTrajectorySample
{
    std::string voiceNoteId;
    time::TickPosition position {};
    double baseSemitones = 0.0;
    double slurOffsetSemitones = 0.0;
    double vibratoOffsetSemitones = 0.0;
    double finalSemitones = 0.0;
};

std::vector<PitchSlurNotePair> pairNotesByRegister (const MidiClip& clip,
                                                    const std::vector<std::string>& sourceNoteIds,
                                                    const std::vector<std::string>& destinationNoteIds);

std::vector<PitchSlur> createLegatoPitchSlurBlock (const MidiClip& clip,
                                                   const std::vector<std::string>& sourceNoteIds,
                                                   const std::vector<std::string>& destinationNoteIds,
                                                   const ExpressionBlockId& blockId,
                                                   std::string idPrefix = "slur");

void applySharedSlurBlockSettings (ExpressionLane& pitchLane,
                                   const PitchSlurBlockSettings& settings);

void applySlurVoiceOverride (ExpressionLane& pitchLane,
                             const ExpressionClipId& slurId,
                             const PitchSlurBlockSettings& settings);

PitchVoiceTrajectorySample evaluatePitchVoiceTrajectoryAt (const MidiClip& clip,
                                                           const ExpressionLane& pitchLane,
                                                           const std::string& voiceNoteId,
                                                           time::TickPosition position,
                                                           const time::ProjectRhythmSettings& rhythmSettings = {});
}
