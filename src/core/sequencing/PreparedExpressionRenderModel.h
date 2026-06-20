#pragma once

#include "core/sequencing/ExpressionDestinationRegistry.h"
#include "core/sequencing/ExpressionEvaluation.h"
#include "core/sequencing/Project.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tsq::core::sequencing
{
using PreparedExpressionFingerprint = std::uint64_t;

struct PreparedExpressionValueSegment
{
    time::TickPosition start {};
    time::TickPosition end {};
    double startValue = 0.0;
    double endValue = 0.0;
};

struct PreparedExpressionRouteRenderData
{
    ExpressionRouteId routeId;
    ExpressionDestination destination;
    std::string destinationStableId;
    double outputMin = 0.0;
    double outputMax = 1.0;
    ExpressionRouteSmoothingPolicy smoothingPolicy = ExpressionRouteSmoothingPolicy::none;
    bool available = false;
    std::vector<PreparedExpressionValueSegment> outputSegments;
    PreparedExpressionFingerprint fingerprint = 0;
};

struct PreparedPitchSlurEvent
{
    ExpressionClipId id;
    std::string sourceNoteId;
    std::string destinationNoteId;
    time::TickDuration slurTime {};
    ExpressionCurveShape curveShape = ExpressionCurveShape::linear;
    bool legatoNoRetrigger = true;
    std::optional<ExpressionBlockId> blockId;
    bool hasVoiceOverride = false;
};

struct PreparedVibratoVoiceOverride
{
    std::string noteId;
    double amplitudeSemitones = 0.0;
    time::TickDuration attackTime {};
    time::TickDuration releaseTime {};
    std::string frequencyDivisionId;
    CyclicWaveShape waveShape = CyclicWaveShape::sine;
    double phase = 0.0;
};

struct PreparedVibratoEvent
{
    ExpressionClipId id;
    std::vector<std::string> sourceNoteIds;
    Region phraseRegion { time::TickPosition {}, time::TickPosition {} };
    time::TickDuration attackTime {};
    time::TickDuration releaseTime {};
    double amplitudeSemitones = 0.0;
    std::string frequencyDivisionId;
    CyclicWaveShape waveShape = CyclicWaveShape::sine;
    double phase = 0.0;
    std::optional<ExpressionBlockId> blockId;
    std::vector<PreparedVibratoVoiceOverride> voiceOverrides;
};

struct PreparedExpressionLaneRenderData
{
    ExpressionLaneId laneId;
    std::string name;
    ExpressionLanePolarity polarity = ExpressionLanePolarity::unipolar;
    bool enabled = true;
    std::vector<ExpressionSegment> laneSegments;
    std::vector<PreparedExpressionRouteRenderData> routes;
    std::vector<PreparedPitchSlurEvent> pitchSlurs;
    std::vector<PreparedVibratoEvent> vibratoEvents;
    PreparedExpressionFingerprint fingerprint = 0;
};

struct PreparedExpressionClipRenderData
{
    std::string trackId;
    std::string clipId;
    time::TickPosition clipStartInProject {};
    Region localRegion { time::TickPosition {}, time::TickPosition {} };
    std::vector<PreparedExpressionLaneRenderData> lanes;
    PreparedExpressionFingerprint fingerprint = 0;
};

struct PreparedExpressionRenderModel
{
    std::vector<PreparedExpressionClipRenderData> clips;
    PreparedExpressionFingerprint fingerprint = 0;
};

PreparedExpressionClipRenderData prepareExpressionClipRenderData (const Project& project,
                                                                  const Track& track,
                                                                  const MidiClip& clip,
                                                                  time::TickDuration segmentStep,
                                                                  const ExpressionEvaluationContext& context);

PreparedExpressionRenderModel prepareExpressionRenderModel (const Project& project,
                                                            time::TickDuration segmentStep,
                                                            ExpressionEvaluationContext context = {});

PreparedExpressionFingerprint preparedExpressionFingerprint (const PreparedExpressionRenderModel& model) noexcept;
}
