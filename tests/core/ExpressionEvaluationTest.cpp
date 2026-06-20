#include "core/sequencing/ExpressionEvaluation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
using namespace tsq::core::sequencing;
using namespace tsq::core::time;

TickDuration ticks (std::int64_t value)
{
    return TickDuration::fromTicks (value);
}

TickDuration beats (int count)
{
    return TickDuration::fromTicks (static_cast<std::int64_t> (count) * ticksPerQuarterNote);
}

TickPosition tickPosition (std::int64_t value)
{
    return TickPosition::fromTicks (value);
}

TickPosition beat (int zeroBasedBeat)
{
    return tickPosition (static_cast<std::int64_t> (zeroBasedBeat) * ticksPerQuarterNote);
}

Region beatRegion (int startBeat, int endBeat)
{
    return Region { beat (startBeat), beat (endBeat) };
}

PhraseEnvelopeClip envelope (double storedLevel = 0.0)
{
    return PhraseEnvelopeClip {
        ExpressionClipId { "env-1" },
        { "note-1" },
        beatRegion (0, 4),
        storedLevel,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 }
    };
}
}

TEST_CASE ("Expression curve shapes evaluate deterministic alpha curves")
{
    CHECK (evaluateExpressionCurve (ExpressionCurveShape::linear, 0.25) == Catch::Approx (0.25));
    CHECK (evaluateExpressionCurve (ExpressionCurveShape::logarithmic, 0.25) == Catch::Approx (0.5));
    CHECK (evaluateExpressionCurve (ExpressionCurveShape::exponential, 0.5) == Catch::Approx (0.25));
    CHECK (evaluateEnvelopeStage (
               EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.0, 1.0 },
               ticks (ticksPerQuarterNote / 2)) == Catch::Approx (0.5));
}

TEST_CASE ("Phrase envelope evaluator supports attack only and stored tail behavior")
{
    auto clip = envelope (0.25);
    clip.setTailExtension (beats (1));

    REQUIRE (evaluatePhraseEnvelopeClipAt (clip, tickPosition (ticksPerQuarterNote / 2), ExpressionLanePolarity::unipolar).has_value());
    CHECK (*evaluatePhraseEnvelopeClipAt (clip, tickPosition (ticksPerQuarterNote / 2), ExpressionLanePolarity::unipolar)
           == Catch::Approx (0.5));
    CHECK (*evaluatePhraseEnvelopeClipAt (clip, beat (3), ExpressionLanePolarity::unipolar) == Catch::Approx (1.0));
    CHECK (*evaluatePhraseEnvelopeClipAt (clip, beat (4), ExpressionLanePolarity::unipolar) == Catch::Approx (0.25));
    CHECK_FALSE (evaluatePhraseEnvelopeClipAt (clip, beat (5), ExpressionLanePolarity::unipolar).has_value());
}

TEST_CASE ("Phrase envelope evaluator supports attack release and full ADSR")
{
    auto attackRelease = envelope();
    attackRelease.setReleaseStage (EnvelopeStage { EnvelopeStageType::release, beats (1), 1.0, 0.0 });

    CHECK (*evaluatePhraseEnvelopeClipAt (attackRelease, beat (0), ExpressionLanePolarity::unipolar) == Catch::Approx (0.0));
    CHECK (*evaluatePhraseEnvelopeClipAt (attackRelease, tickPosition ((3 * ticksPerQuarterNote) + (ticksPerQuarterNote / 2)), ExpressionLanePolarity::unipolar)
           == Catch::Approx (0.5));

    auto adsr = envelope();
    adsr.setDecayStage (EnvelopeStage { EnvelopeStageType::decay, beats (1), 1.0, 0.4 });
    adsr.setSustainLevel (0.4, ExpressionLanePolarity::unipolar);
    adsr.setReleaseStage (EnvelopeStage { EnvelopeStageType::release, beats (1), 0.4, 0.0 });

    CHECK (*evaluatePhraseEnvelopeClipAt (adsr, tickPosition (ticksPerQuarterNote + (ticksPerQuarterNote / 2)), ExpressionLanePolarity::unipolar)
           == Catch::Approx (0.7));
    CHECK (*evaluatePhraseEnvelopeClipAt (adsr, tickPosition ((2 * ticksPerQuarterNote) + (ticksPerQuarterNote / 2)), ExpressionLanePolarity::unipolar)
           == Catch::Approx (0.4));
    CHECK (*evaluatePhraseEnvelopeClipAt (adsr, tickPosition ((3 * ticksPerQuarterNote) + (ticksPerQuarterNote / 2)), ExpressionLanePolarity::unipolar)
           == Catch::Approx (0.2));
}

TEST_CASE ("Cyclic evaluator supports oscillator polarity modes")
{
    ExpressionEvaluationContext context;
    CyclicExpressionClip positive { ExpressionClipId { "cyclic-positive" }, { "note-1" }, beatRegion (0, 2) };
    positive.setMaxAmplitude (1.0);
    positive.setFrequencyDivisionId ("quarter");
    positive.setWaveShape (CyclicWaveShape::sine);
    positive.setWavePolarityMode (CyclicWavePolarityMode::positiveOscillator);
    positive.setPhase (0.5);

    CyclicExpressionClip rectified { ExpressionClipId { "cyclic-rectified" }, { "note-1" }, beatRegion (0, 2) };
    rectified.setMaxAmplitude (1.0);
    rectified.setFrequencyDivisionId ("quarter");
    rectified.setWaveShape (CyclicWaveShape::sine);
    rectified.setWavePolarityMode (CyclicWavePolarityMode::halfWaveRectified);
    rectified.setPhase (0.5);

    CHECK (*evaluateCyclicExpressionClipAt (positive, beat (0), context) == Catch::Approx (0.5));
    CHECK (*evaluateCyclicExpressionClipAt (rectified, beat (0), context) == Catch::Approx (0.0).margin (0.000000001));
}

TEST_CASE ("Expression lane evaluator combines phrase and cyclic clips")
{
    ExpressionEvaluationContext context;
    ExpressionLane additive { ExpressionLaneId { "expr-add" }, "Add", ExpressionLanePolarity::unipolar };
    auto base = envelope();
    base.setAttackStage (EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.5, 0.5 });
    additive.addPhraseEnvelopeClip (base);

    CyclicExpressionClip cyclic { ExpressionClipId { "cyclic-1" }, { "note-1" }, beatRegion (0, 4) };
    cyclic.setMaxAmplitude (0.5);
    cyclic.setFrequencyDivisionId ("quarter");
    cyclic.setWaveShape (CyclicWaveShape::square);
    cyclic.setWavePolarityMode (CyclicWavePolarityMode::positiveOscillator);
    additive.addCyclicClip (cyclic);

    CHECK (evaluateExpressionLaneAt (additive, beat (0), ExpressionEvaluationContext { context.rhythmSettings, false })
           == Catch::Approx (1.0));

    ExpressionLane multiplicative { ExpressionLaneId { "expr-mul" }, "Multiply", ExpressionLanePolarity::unipolar };
    multiplicative.addPhraseEnvelopeClip (base);
    cyclic.setBlendMode (CyclicBlendMode::multiplicative);
    multiplicative.addCyclicClip (cyclic);

    CHECK (evaluateExpressionLaneAt (multiplicative, beat (0), context) == Catch::Approx (0.25));
}

TEST_CASE ("Expression lane evaluator clamps scalar values and samples overflow spans")
{
    ExpressionEvaluationContext context;
    ExpressionLane lane { ExpressionLaneId { "expr-overflow" }, "Overflow", ExpressionLanePolarity::unipolar };
    auto base = envelope();
    base.setAttackStage (EnvelopeStage { EnvelopeStageType::attack, beats (1), 0.75, 0.75 });
    lane.addPhraseEnvelopeClip (base);

    CyclicExpressionClip cyclic { ExpressionClipId { "cyclic-1" }, { "note-1" }, beatRegion (0, 4) };
    cyclic.setMaxAmplitude (0.5);
    cyclic.setFrequencyDivisionId ("quarter");
    cyclic.setWaveShape (CyclicWaveShape::square);
    cyclic.setWavePolarityMode (CyclicWavePolarityMode::positiveOscillator);
    lane.addCyclicClip (cyclic);

    CHECK (evaluateExpressionLaneAt (lane, beat (0), context) == Catch::Approx (1.0));
    CHECK (evaluateExpressionLaneAt (lane, beat (0), ExpressionEvaluationContext { context.rhythmSettings, false }) == Catch::Approx (1.25));

    std::vector<ExpressionSample> samples;
    sampleExpressionLane (lane, beatRegion (0, 2), ticks (ticksPerQuarterNote / 2), context, samples);

    REQUIRE_FALSE (samples.empty());
    for (const auto& sample : samples)
    {
        CHECK (sample.value >= 0.0);
        CHECK (sample.value <= 1.0);
    }
}

TEST_CASE ("Expression lane evaluator supports bipolar lanes and sampled segments")
{
    ExpressionEvaluationContext context;
    ExpressionLane lane { ExpressionLaneId { "expr-bipolar" }, "Bipolar", ExpressionLanePolarity::bipolar };
    PhraseEnvelopeClip clip {
        ExpressionClipId { "env-1" },
        { "note-1" },
        beatRegion (0, 2),
        0.0,
        EnvelopeStage { EnvelopeStageType::attack, beats (1), -1.0, 1.0 }
    };
    lane.addPhraseEnvelopeClip (clip);

    CHECK (evaluateExpressionLaneAt (lane, beat (0), context) == Catch::Approx (-1.0));
    CHECK (evaluateExpressionLaneAt (lane, tickPosition (ticksPerQuarterNote / 2), context) == Catch::Approx (0.0));
    CHECK (evaluateExpressionLaneAt (lane, beat (1), context) == Catch::Approx (1.0));

    std::vector<ExpressionSample> samples;
    sampleExpressionLane (lane, beatRegion (0, 2), beats (1), context, samples);
    REQUIRE (samples.size() == 3);
    CHECK (samples[0].position == beat (0));
    CHECK (samples[1].position == beat (1));
    CHECK (samples[2].position == beat (2));

    std::vector<ExpressionSegment> segments;
    buildExpressionSegments (lane, beatRegion (0, 2), beats (1), context, segments);
    REQUIRE (segments.size() == 2);
    CHECK (segments[0].start == beat (0));
    CHECK (segments[0].end == beat (1));
    CHECK (segments[0].startValue == Catch::Approx (-1.0));
    CHECK (segments[0].endValue == Catch::Approx (1.0));
}

TEST_CASE ("Expression evaluation performance probe covers dense viewport sampling", "[performance][expression]")
{
    ExpressionEvaluationContext context;
    std::vector<ExpressionLane> lanes;
    lanes.reserve (8);

    for (auto laneIndex = 0; laneIndex < 8; ++laneIndex)
    {
        ExpressionLane lane {
            ExpressionLaneId { "expr-lane-" + std::to_string (laneIndex) },
            "Lane",
            ExpressionLanePolarity::unipolar
        };

        for (auto clipIndex = 0; clipIndex < 100; ++clipIndex)
        {
            CyclicExpressionClip cyclic {
                ExpressionClipId { "cyclic-" + std::to_string (laneIndex) + "-" + std::to_string (clipIndex) },
                { "note-" + std::to_string (clipIndex) },
                Region { beat (clipIndex), beat (clipIndex + 1) }
            };
            cyclic.setFrequencyDivisionId ("sixteenth");
            cyclic.setMaxAmplitude (0.25);
            cyclic.setWaveShape (clipIndex % 2 == 0 ? CyclicWaveShape::sine : CyclicWaveShape::triangle);
            lane.addCyclicClip (cyclic);
        }

        lanes.push_back (std::move (lane));
    }

    std::vector<ExpressionSample> samples;
    const auto started = std::chrono::steady_clock::now();
    auto totalSamples = std::size_t {};
    for (auto repeat = 0; repeat < 4; ++repeat)
    {
        for (const auto& lane : lanes)
        {
            sampleExpressionLane (lane, beatRegion (0, 100), ticks (ticksPerQuarterNote / 4), context, samples);
            totalSamples += samples.size();
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - started);

    INFO ("dense expression sampling elapsed ms=" << elapsed.count());
    CHECK (totalSamples == 4u * 8u * 401u);
}
