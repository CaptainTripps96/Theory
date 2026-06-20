#pragma once

#include "core/sequencing/Expression.h"
#include "core/time/ProjectRhythmSettings.h"

#include <optional>
#include <vector>

namespace tsq::core::sequencing
{
struct ExpressionEvaluationContext
{
    time::ProjectRhythmSettings rhythmSettings;
    bool normalizeOverflow = true;
};

struct ExpressionSample
{
    time::TickPosition position {};
    double value = 0.0;
};

struct ExpressionSegment
{
    time::TickPosition start {};
    time::TickPosition end {};
    double startValue = 0.0;
    double endValue = 0.0;
};

double expressionLaneMinimumValue (ExpressionLanePolarity polarity) noexcept;
double expressionLaneMaximumValue (ExpressionLanePolarity polarity) noexcept;
double expressionLaneNeutralValue (ExpressionLanePolarity polarity) noexcept;

double evaluateExpressionCurve (ExpressionCurveShape shape, double alpha) noexcept;
double evaluateEnvelopeStage (const EnvelopeStage& stage, time::TickDuration offset) noexcept;
std::optional<double> evaluatePhraseEnvelopeClipAt (const PhraseEnvelopeClip& clip,
                                                    time::TickPosition position,
                                                    ExpressionLanePolarity polarity) noexcept;
std::optional<double> evaluateCyclicExpressionClipAt (const CyclicExpressionClip& clip,
                                                      time::TickPosition position,
                                                      const ExpressionEvaluationContext& context) noexcept;
double evaluateExpressionLaneAt (const ExpressionLane& lane,
                                 time::TickPosition position,
                                 const ExpressionEvaluationContext& context = {});

void sampleExpressionLane (const ExpressionLane& lane,
                           Region region,
                           time::TickDuration step,
                           const ExpressionEvaluationContext& context,
                           std::vector<ExpressionSample>& output);

void buildExpressionSegments (const ExpressionLane& lane,
                              Region region,
                              time::TickDuration step,
                              const ExpressionEvaluationContext& context,
                              std::vector<ExpressionSegment>& output);
}
