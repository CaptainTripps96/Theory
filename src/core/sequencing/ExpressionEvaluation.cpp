#include "core/sequencing/ExpressionEvaluation.h"

#include "core/time/GridDivision.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace tsq::core::sequencing
{
namespace
{
constexpr auto pi = 3.141592653589793238462643383279502884;

double finiteOrZero (double value) noexcept
{
    return std::isfinite (value) ? value : 0.0;
}

double clampAlpha (double alpha) noexcept
{
    return std::clamp (finiteOrZero (alpha), 0.0, 1.0);
}

double interpolate (double start, double end, double alpha) noexcept
{
    return start + ((end - start) * alpha);
}

double clampForPolarity (double value, ExpressionLanePolarity polarity) noexcept
{
    return std::clamp (finiteOrZero (value),
                       expressionLaneMinimumValue (polarity),
                       expressionLaneMaximumValue (polarity));
}

time::TickPosition positionFromTicks (std::int64_t ticks) noexcept
{
    return time::TickPosition::fromTicks (ticks);
}

time::TickDuration frequencyDurationFor (const CyclicExpressionClip& clip, const ExpressionEvaluationContext& context) noexcept
{
    const auto definition = time::gridDivisionDefinitionFor (clip.frequencyDivisionId(), context.rhythmSettings);
    if (definition.tickDuration.ticks() <= 0)
        return time::sixteenthNoteDuration();

    return definition.tickDuration;
}

double releaseFade (const Region& region, time::TickPosition position, time::TickDuration releaseTime) noexcept
{
    if (releaseTime.ticks() <= 0)
        return 1.0;

    const auto releaseStart = region.end() - releaseTime;
    if (position < releaseStart)
        return 1.0;

    const auto elapsed = static_cast<double> ((position - releaseStart).ticks());
    const auto duration = static_cast<double> (releaseTime.ticks());
    return 1.0 - clampAlpha (elapsed / duration);
}

double attackFade (const Region& region, time::TickPosition position, time::TickDuration attackTime) noexcept
{
    if (attackTime.ticks() <= 0)
        return 1.0;

    const auto elapsed = static_cast<double> ((position - region.start()).ticks());
    const auto duration = static_cast<double> (attackTime.ticks());
    return clampAlpha (elapsed / duration);
}

double cyclicFade (const CyclicExpressionClip& clip, time::TickPosition position) noexcept
{
    return std::min (attackFade (clip.phraseRegion(), position, clip.attackTime()),
                     releaseFade (clip.phraseRegion(), position, clip.releaseTime()));
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

double cyclicWaveValue (const CyclicExpressionClip& clip, double phase) noexcept
{
    const auto bipolar = bipolarWave (clip.waveShape(), phase);
    switch (clip.wavePolarityMode())
    {
        case CyclicWavePolarityMode::positiveOscillator:
            return (bipolar + 1.0) * 0.5;

        case CyclicWavePolarityMode::halfWaveRectified:
            return std::max (0.0, bipolar);
    }

    return 0.0;
}

double phraseEnvelopeSustainValue (const PhraseEnvelopeClip& clip) noexcept
{
    if (clip.sustainLevel().has_value())
        return *clip.sustainLevel();
    if (clip.decayStage().has_value())
        return clip.decayStage()->endLevel;

    return clip.attackStage().endLevel;
}

double normalizeToBounds (double value, double minValue, double maxValue, double observedMin, double observedMax) noexcept
{
    if (observedMin >= minValue && observedMax <= maxValue)
        return value;

    if (observedMax <= observedMin)
        return std::clamp (value, minValue, maxValue);

    return minValue + (((value - observedMin) / (observedMax - observedMin)) * (maxValue - minValue));
}

void normalizeSamplesIfNeeded (ExpressionLanePolarity polarity, std::vector<ExpressionSample>& samples)
{
    if (samples.empty())
        return;

    auto observedMin = samples.front().value;
    auto observedMax = samples.front().value;
    for (const auto& sample : samples)
    {
        observedMin = std::min (observedMin, sample.value);
        observedMax = std::max (observedMax, sample.value);
    }

    const auto minValue = expressionLaneMinimumValue (polarity);
    const auto maxValue = expressionLaneMaximumValue (polarity);
    if (observedMin >= minValue && observedMax <= maxValue)
        return;

    for (auto& sample : samples)
        sample.value = normalizeToBounds (sample.value, minValue, maxValue, observedMin, observedMax);
}
}

double expressionLaneMinimumValue (ExpressionLanePolarity polarity) noexcept
{
    return polarity == ExpressionLanePolarity::bipolar ? -1.0 : 0.0;
}

double expressionLaneMaximumValue (ExpressionLanePolarity) noexcept
{
    return 1.0;
}

double expressionLaneNeutralValue (ExpressionLanePolarity) noexcept
{
    return 0.0;
}

double evaluateExpressionCurve (ExpressionCurveShape shape, double alpha) noexcept
{
    const auto clamped = clampAlpha (alpha);

    switch (shape)
    {
        case ExpressionCurveShape::linear:
            return clamped;

        case ExpressionCurveShape::logarithmic:
            return std::sqrt (clamped);

        case ExpressionCurveShape::exponential:
            return clamped * clamped;
    }

    return clamped;
}

double evaluateEnvelopeStage (const EnvelopeStage& stage, time::TickDuration offset) noexcept
{
    if (stage.duration.ticks() <= 0)
        return stage.endLevel;

    const auto alpha = static_cast<double> (std::clamp<std::int64_t> (offset.ticks(), 0, stage.duration.ticks()))
        / static_cast<double> (stage.duration.ticks());
    return interpolate (stage.startLevel, stage.endLevel, evaluateExpressionCurve (stage.curveShape, alpha));
}

std::optional<double> evaluatePhraseEnvelopeClipAt (const PhraseEnvelopeClip& clip,
                                                    time::TickPosition position,
                                                    ExpressionLanePolarity polarity) noexcept
{
    const auto& region = clip.phraseRegion();
    const auto tailEnd = clip.tailExtension().has_value() ? region.end() + *clip.tailExtension() : region.end();
    if (position < region.start() || position >= tailEnd)
        return std::nullopt;

    if (! region.contains (position))
        return clampForPolarity (clip.storedLevel(), polarity);

    const auto releaseDuration = clip.releaseStage().has_value() ? clip.releaseStage()->duration : time::TickDuration {};
    const auto releaseStart = region.end() - releaseDuration;
    if (clip.releaseStage().has_value() && position >= releaseStart)
        return clampForPolarity (evaluateEnvelopeStage (*clip.releaseStage(), position - releaseStart), polarity);

    const auto attackEnd = region.start() + clip.attackStage().duration;
    if (position < attackEnd)
        return clampForPolarity (evaluateEnvelopeStage (clip.attackStage(), position - region.start()), polarity);

    auto decayEnd = attackEnd;
    if (clip.decayStage().has_value())
    {
        decayEnd = attackEnd + clip.decayStage()->duration;
        if (position < decayEnd && position < releaseStart)
            return clampForPolarity (evaluateEnvelopeStage (*clip.decayStage(), position - attackEnd), polarity);
    }

    return clampForPolarity (phraseEnvelopeSustainValue (clip), polarity);
}

std::optional<double> evaluateCyclicExpressionClipAt (const CyclicExpressionClip& clip,
                                                      time::TickPosition position,
                                                      const ExpressionEvaluationContext& context) noexcept
{
    if (! clip.phraseRegion().contains (position))
        return std::nullopt;

    const auto frequencyDuration = frequencyDurationFor (clip, context);
    const auto elapsedTicks = static_cast<double> ((position - clip.phraseRegion().start()).ticks());
    const auto cycleTicks = static_cast<double> (frequencyDuration.ticks());
    const auto phase = (cycleTicks <= 0.0 ? 0.0 : elapsedTicks / cycleTicks) + clip.phase();
    return cyclicWaveValue (clip, phase) * clip.maxAmplitude() * cyclicFade (clip, position);
}

double evaluateExpressionLaneAt (const ExpressionLane& lane,
                                 time::TickPosition position,
                                 const ExpressionEvaluationContext& context)
{
    if (! lane.enabled())
        return expressionLaneNeutralValue (lane.polarity());

    auto value = expressionLaneNeutralValue (lane.polarity());
    for (const auto& clip : lane.phraseEnvelopeClips())
    {
        if (const auto phraseValue = evaluatePhraseEnvelopeClipAt (clip, position, lane.polarity()))
            value = *phraseValue;
    }

    for (const auto& clip : lane.cyclicClips())
    {
        const auto cyclicValue = evaluateCyclicExpressionClipAt (clip, position, context);
        if (! cyclicValue.has_value())
            continue;

        if (clip.blendMode() == CyclicBlendMode::multiplicative)
            value *= *cyclicValue;
        else
            value += *cyclicValue;
    }

    return context.normalizeOverflow ? clampForPolarity (value, lane.polarity()) : finiteOrZero (value);
}

void sampleExpressionLane (const ExpressionLane& lane,
                           Region region,
                           time::TickDuration step,
                           const ExpressionEvaluationContext& context,
                           std::vector<ExpressionSample>& output)
{
    if (step.ticks() <= 0)
        throw std::invalid_argument ("Expression sampling step must be positive");

    output.clear();
    if (region.duration().ticks() <= 0)
        return;

    for (auto tick = region.start().ticks(); tick < region.end().ticks(); tick += step.ticks())
    {
        const auto position = positionFromTicks (tick);
        output.push_back (ExpressionSample {
            position,
            evaluateExpressionLaneAt (lane, position, ExpressionEvaluationContext { context.rhythmSettings, false })
        });
    }

    if (output.empty() || output.back().position != region.end())
    {
        output.push_back (ExpressionSample {
            region.end(),
            evaluateExpressionLaneAt (lane, region.end(), ExpressionEvaluationContext { context.rhythmSettings, false })
        });
    }

    if (context.normalizeOverflow)
        normalizeSamplesIfNeeded (lane.polarity(), output);
}

void buildExpressionSegments (const ExpressionLane& lane,
                              Region region,
                              time::TickDuration step,
                              const ExpressionEvaluationContext& context,
                              std::vector<ExpressionSegment>& output)
{
    std::vector<ExpressionSample> samples;
    sampleExpressionLane (lane, region, step, context, samples);

    output.clear();
    if (samples.size() < 2)
        return;

    output.reserve (samples.size() - 1);
    for (auto index = std::size_t { 1 }; index < samples.size(); ++index)
    {
        if (samples[index - 1].position == samples[index].position)
            continue;

        output.push_back (ExpressionSegment {
            samples[index - 1].position,
            samples[index].position,
            samples[index - 1].value,
            samples[index].value
        });
    }
}
}
