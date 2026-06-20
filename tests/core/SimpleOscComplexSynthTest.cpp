#include "core/devices/FirstPartyDeviceRegistry.h"
#include "core/devices/SimpleOscComplexSynth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace
{
using namespace tsq::core::devices;
using namespace tsq::core::sequencing;

FirstPartyDeviceState simpleOscStateWith (std::initializer_list<FirstPartyDeviceParameterValue> values)
{
    auto state = defaultFirstPartyDeviceState (simpleOscComplexDefinition());
    for (const auto& value : values)
    {
        const auto existing = std::find_if (state.parameterValues.begin(), state.parameterValues.end(), [&value] (const auto& candidate)
        {
            return candidate.parameterId == value.parameterId;
        });

        if (existing == state.parameterValues.end())
            state.parameterValues.push_back (value);
        else
            existing->normalizedValue = value.normalizedValue;
    }

    return state;
}

double absolutePeak (const std::vector<float>& samples)
{
    return std::accumulate (samples.begin(), samples.end(), 0.0, [] (double peak, float sample)
    {
        return std::max (peak, std::abs (static_cast<double> (sample)));
    });
}

bool allFinite (const std::vector<float>& samples)
{
    return std::all_of (samples.begin(), samples.end(), [] (float sample)
    {
        return std::isfinite (sample);
    });
}

double absolutePeakForRange (const std::vector<float>& samples, std::size_t start, std::size_t length)
{
    if (start >= samples.size())
        return 0.0;

    const auto end = std::min (samples.size(), start + length);
    return std::accumulate (samples.begin() + static_cast<std::ptrdiff_t> (start),
                            samples.begin() + static_cast<std::ptrdiff_t> (end),
                            0.0,
                            [] (double peak, float sample)
                            {
                                return std::max (peak, std::abs (static_cast<double> (sample)));
                            });
}
}

TEST_CASE ("Simple Osc Complex default patch is a plain sustained oscillator", "[devices][simple-osc-complex]")
{
    const auto state = defaultFirstPartyDeviceState (simpleOscComplexDefinition());
    const auto patch = simpleOscComplexPatchFromState (state);

    CHECK (patch.pmAmount == Catch::Approx (0.0));
    CHECK (patch.wavefolderAmount == Catch::Approx (0.0));
    CHECK (patch.attackSeconds == Catch::Approx (0.0));
    CHECK (patch.releaseSeconds == Catch::Approx (0.0));

    SimpleOscComplexSynth synth { 4 };
    synth.prepare (48000.0);
    synth.setPatch (patch);
    synth.noteOn (60, 0.9f);

    std::vector<float> left (4096, 0.0f);
    std::vector<float> right (4096, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (synth.activeVoiceCount() == 1);
    CHECK (allFinite (left));
    CHECK (allFinite (right));
    CHECK (absolutePeakForRange (left, 0, 512) > 0.001);
    CHECK (absolutePeakForRange (left, 2048, 512) > 0.001);
    CHECK (absolutePeakForRange (left, 3584, 512) > 0.001);

    synth.noteOff (60);
    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);
    synth.render (left.data(), right.data(), 64);
    CHECK (synth.activeVoiceCount() == 0);
}

TEST_CASE ("Simple Osc Complex maps first-party state to DSP patch", "[devices][simple-osc-complex]")
{
    const auto state = simpleOscStateWith ({
        { "pitch", 1.0 },
        { "amp.level", 0.5 },
        { "osc.pm.amount", 0.75 },
        { "osc.mod.ratio", 1.0 },
        { "wavefolder.amount", 0.25 },
        { "wavefolder.stages", 1.0 },
        { "amp.attack", 0.0 },
        { "amp.decay", 0.5 },
        { "amp.sustain", 0.25 },
        { "amp.release", 0.0 }
    });

    const auto patch = simpleOscComplexPatchFromState (state);
    CHECK (patch.pitchSemitones == Catch::Approx (48.0));
    CHECK (patch.ampLevel == Catch::Approx (0.5));
    CHECK (patch.pmAmount == Catch::Approx (0.75));
    CHECK (patch.modRatioIndex == 7);
    CHECK (patch.wavefolderAmount == Catch::Approx (0.25));
    CHECK (patch.wavefolderStages == 5);
    CHECK (patch.attackSeconds == Catch::Approx (0.0));
    CHECK (patch.decaySeconds == Catch::Approx (5.0005));
    CHECK (patch.sustainLevel == Catch::Approx (0.25));
    CHECK (patch.releaseSeconds == Catch::Approx (0.0));
}

TEST_CASE ("Simple Osc Complex renders finite stereo audio and releases voices", "[devices][simple-osc-complex]")
{
    auto state = simpleOscStateWith ({
        { "amp.attack", 0.0 },
        { "amp.decay", 0.0 },
        { "amp.sustain", 1.0 },
        { "amp.release", 0.0 },
        { "amp.level", 1.0 },
        { "osc.pm.amount", 0.6 },
        { "wavefolder.amount", 0.7 },
        { "wavefolder.stages", 0.75 }
    });

    SimpleOscComplexSynth synth { 4 };
    synth.prepare (48000.0);
    synth.setPatch (simpleOscComplexPatchFromState (state));
    synth.noteOn (60, 1.0f);

    std::vector<float> left (512, 0.0f);
    std::vector<float> right (512, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (synth.activeVoiceCount() == 1);
    CHECK (allFinite (left));
    CHECK (allFinite (right));
    CHECK (absolutePeak (left) > 0.001);
    CHECK (absolutePeak (right) > 0.001);
    CHECK (absolutePeak (left) <= 1.0);
    CHECK (absolutePeak (right) <= 1.0);

    synth.noteOff (60);
    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (allFinite (left));
    CHECK (allFinite (right));
    CHECK (synth.activeVoiceCount() == 0);
}

TEST_CASE ("Simple Osc Complex steals voices when polyphony is exceeded", "[devices][simple-osc-complex]")
{
    SimpleOscComplexSynth synth { 2 };
    synth.prepare (44100.0);
    synth.noteOn (60, 0.8f);
    synth.noteOn (64, 0.8f);
    synth.noteOn (67, 0.8f);

    CHECK (synth.activeVoiceCount() == 2);

    std::vector<float> left (256, 0.0f);
    std::vector<float> right (256, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (allFinite (left));
    CHECK (absolutePeak (left) > 0.001);
}

TEST_CASE ("Simple Osc Complex renders deterministically for identical input", "[devices][simple-osc-complex]")
{
    const auto patch = simpleOscComplexPatchFromState (simpleOscStateWith ({
        { "amp.attack", 0.04 },
        { "amp.decay", 0.20 },
        { "amp.sustain", 0.68 },
        { "amp.release", 0.10 },
        { "amp.level", 0.72 },
        { "osc.pm.amount", 0.48 },
        { "osc.mod.ratio", 0.50 },
        { "wavefolder.amount", 0.56 },
        { "wavefolder.stages", 0.50 }
    }));

    const auto render = [&patch]
    {
        SimpleOscComplexSynth synth { 8 };
        synth.prepare (48000.0);
        synth.setPatch (patch);
        synth.noteOn (60, 0.9f);
        synth.noteOn (67, 0.65f);

        std::vector<float> left (2048, 0.0f);
        std::vector<float> right (2048, 0.0f);
        synth.render (left.data(), right.data(), 768);
        synth.noteOff (60);
        synth.render (left.data() + 768, right.data() + 768, 640);
        synth.noteOff (67);
        synth.render (left.data() + 1408, right.data() + 1408, 640);

        return std::pair { std::move (left), std::move (right) };
    };

    const auto first = render();
    const auto second = render();

    REQUIRE (first.first.size() == second.first.size());
    REQUIRE (first.second.size() == second.second.size());
    CHECK (allFinite (first.first));
    CHECK (allFinite (first.second));
    CHECK (absolutePeak (first.first) > 0.001);
    CHECK (absolutePeak (first.second) > 0.001);
    CHECK (absolutePeak (first.first) <= 1.0);
    CHECK (absolutePeak (first.second) <= 1.0);

    for (std::size_t index = 0; index < first.first.size(); ++index)
    {
        CHECK (first.first[index] == Catch::Approx (second.first[index]).margin (0.0));
        CHECK (first.second[index] == Catch::Approx (second.second[index]).margin (0.0));
    }
}

TEST_CASE ("Simple Osc Complex tracks note IDs through note on and note off", "[devices][simple-osc-complex][expression]")
{
    SimpleOscComplexSynth synth { 4 };
    synth.prepare (48000.0);
    synth.noteOn (101, 60, 0.8f);
    synth.noteOn (202, 64, 0.8f);

    CHECK (synth.activeVoiceCount() == 2);
    CHECK (synth.activeVoiceCountForNoteId (101) == 1);
    CHECK (synth.activeVoiceCountForNoteId (202) == 1);

    synth.noteOff (SimpleOscComplexNoteId { 101 });
    CHECK (synth.activeVoiceCountForNoteId (101) == 1);
    CHECK (synth.activeVoiceCountForNoteId (202) == 1);

    std::vector<float> left (1024, 0.0f);
    std::vector<float> right (1024, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));
    CHECK (allFinite (left));
    CHECK (allFinite (right));
}

TEST_CASE ("Simple Osc Complex releases every active voice that shares a defensive note ID", "[devices][simple-osc-complex][expression]")
{
    auto state = simpleOscStateWith ({
        { "amp.attack", 0.0 },
        { "amp.decay", 0.0 },
        { "amp.sustain", 1.0 },
        { "amp.release", 0.0 },
        { "amp.level", 0.8 }
    });

    SimpleOscComplexSynth synth { 4 };
    synth.prepare (48000.0);
    synth.setPatch (simpleOscComplexPatchFromState (state));
    synth.noteOn (101, 60, 0.8f);
    synth.noteOn (101, 64, 0.8f);

    CHECK (synth.activeVoiceCountForNoteId (101) == 2);
    synth.noteOff (SimpleOscComplexNoteId { 101 });

    std::vector<float> left (128, 0.0f);
    std::vector<float> right (128, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (synth.activeVoiceCountForNoteId (101) == 0);
}

TEST_CASE ("Simple Osc Complex legato slur preserves envelope continuity", "[devices][simple-osc-complex][expression]")
{
    auto state = simpleOscStateWith ({
        { "amp.attack", 0.0 },
        { "amp.decay", 0.0 },
        { "amp.sustain", 1.0 },
        { "amp.release", 0.5 },
        { "amp.level", 0.8 }
    });

    SimpleOscComplexSynth synth { 4 };
    synth.prepare (48000.0);
    synth.setPatch (simpleOscComplexPatchFromState (state));
    synth.noteOn (101, 60, 0.9f);

    std::vector<float> left (256, 0.0f);
    std::vector<float> right (256, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));
    const auto before = synth.debugEnvelopeLevelForNoteId (101);

    REQUIRE (synth.startLegatoSlur (101, 202, 64, 0.9f, 0.1, tsq::core::sequencing::ExpressionCurveShape::linear, true));
    CHECK (synth.activeVoiceCountForNoteId (101) == 0);
    CHECK (synth.activeVoiceCountForNoteId (202) == 1);
    CHECK (synth.activeVoiceCount() == 1);
    CHECK (synth.debugEnvelopeLevelForNoteId (202) == Catch::Approx (before));
}

TEST_CASE ("Simple Osc Complex ignores repeated legato slurs once destination is active", "[devices][simple-osc-complex][expression]")
{
    SimpleOscComplexSynth synth { 4 };
    synth.prepare (48000.0);
    synth.noteOn (101, 60, 0.8f);

    CHECK (synth.startLegatoSlur (101,
                                  202,
                                  64,
                                  0.8f,
                                  0.0,
                                  tsq::core::sequencing::ExpressionCurveShape::linear,
                                  true));
    CHECK (synth.startLegatoSlur (101,
                                  202,
                                  64,
                                  0.8f,
                                  0.0,
                                  tsq::core::sequencing::ExpressionCurveShape::linear,
                                  true));

    CHECK (synth.activeVoiceCountForNoteId (101) == 0);
    CHECK (synth.activeVoiceCountForNoteId (202) == 1);
    CHECK (synth.activeVoiceCount() == 1);
}

TEST_CASE ("Simple Osc Complex slur pitch reaches destination", "[devices][simple-osc-complex][expression]")
{
    SimpleOscComplexSynth synth { 4 };
    synth.prepare (1000.0);
    synth.noteOn (101, 60, 0.9f);
    REQUIRE (synth.startLegatoSlur (101, 202, 64, 0.9f, 0.010, tsq::core::sequencing::ExpressionCurveShape::linear, true));

    std::vector<float> left (12, 0.0f);
    std::vector<float> right (12, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (synth.debugCurrentPitchSemitonesForNoteId (202) == Catch::Approx (64.0).margin (0.000001));
    CHECK (allFinite (left));
    CHECK (allFinite (right));
}

TEST_CASE ("Simple Osc Complex legato slur release is owned by destination note", "[devices][simple-osc-complex][expression]")
{
    auto state = simpleOscStateWith ({
        { "amp.attack", 0.0 },
        { "amp.decay", 0.0 },
        { "amp.sustain", 1.0 },
        { "amp.release", 0.0 },
        { "amp.level", 0.8 }
    });

    SimpleOscComplexSynth synth { 4 };
    synth.prepare (48000.0);
    synth.setPatch (simpleOscComplexPatchFromState (state));
    synth.noteOn (101, 60, 0.9f);

    std::vector<float> left (256, 0.0f);
    std::vector<float> right (256, 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    REQUIRE (synth.startLegatoSlur (101, 202, 64, 0.9f, 0.0, tsq::core::sequencing::ExpressionCurveShape::linear, true));
    synth.noteOff (SimpleOscComplexNoteId { 101 });

    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (synth.activeVoiceCountForNoteId (202) == 1);
    CHECK (synth.debugEnvelopeLevelForNoteId (202) > 0.5);
    CHECK (absolutePeak (left) > 0.001);

    synth.noteOff (SimpleOscComplexNoteId { 202 });
    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);
    synth.render (left.data(), right.data(), static_cast<int> (left.size()));

    CHECK (synth.debugEnvelopeLevelForNoteId (202) < 0.01);
}

TEST_CASE ("Simple Osc Complex voice stealing is deterministic and prefers release voices", "[devices][simple-osc-complex][expression]")
{
    auto state = simpleOscStateWith ({
        { "amp.release", 1.0 },
        { "amp.sustain", 1.0 },
        { "amp.level", 0.8 }
    });

    SimpleOscComplexSynth synth { 2 };
    synth.prepare (48000.0);
    synth.setPatch (simpleOscComplexPatchFromState (state));
    synth.noteOn (101, 60, 0.8f);
    synth.noteOn (202, 64, 0.8f);
    synth.noteOff (101);
    synth.noteOn (303, 67, 0.8f);

    CHECK (synth.activeVoiceCountForNoteId (101) == 0);
    CHECK (synth.activeVoiceCountForNoteId (202) == 1);
    CHECK (synth.activeVoiceCountForNoteId (303) == 1);
}

TEST_CASE ("Simple Osc Complex can slur from a release voice", "[devices][simple-osc-complex][expression]")
{
    auto state = simpleOscStateWith ({
        { "amp.release", 1.0 },
        { "amp.sustain", 1.0 },
        { "amp.level", 0.8 }
    });

    SimpleOscComplexSynth synth { 2 };
    synth.prepare (48000.0);
    synth.setPatch (simpleOscComplexPatchFromState (state));
    synth.noteOn (101, 60, 0.8f);
    synth.noteOff (101);

    REQUIRE (synth.activeVoiceCountForNoteId (101) == 1);
    REQUIRE (synth.startLegatoSlur (101, 202, 65, 0.8f, 0.02, tsq::core::sequencing::ExpressionCurveShape::linear, true));
    CHECK (synth.activeVoiceCountForNoteId (101) == 0);
    CHECK (synth.activeVoiceCountForNoteId (202) == 1);
}

TEST_CASE ("Simple Osc Complex remains finite under rapid expression note events", "[devices][simple-osc-complex][expression]")
{
    auto state = simpleOscStateWith ({
        { "amp.attack", 0.0 },
        { "amp.decay", 0.0 },
        { "amp.sustain", 1.0 },
        { "amp.release", 0.1 },
        { "amp.level", 1.0 },
        { "osc.pm.amount", 1.0 },
        { "wavefolder.amount", 1.0 }
    });

    SimpleOscComplexSynth synth { 8 };
    synth.prepare (48000.0);
    synth.setPatch (simpleOscComplexPatchFromState (state));

    std::vector<float> left (16, 0.0f);
    std::vector<float> right (16, 0.0f);

    for (auto index = 0; index < 64; ++index)
    {
        const auto sourceId = static_cast<SimpleOscComplexNoteId> (1000 + index);
        const auto destinationId = static_cast<SimpleOscComplexNoteId> (2000 + index);
        synth.noteOn (sourceId, 48 + (index % 36), 0.8f);
        synth.startLegatoSlur (sourceId,
                               destinationId,
                               52 + (index % 36),
                               0.8f,
                               0.005,
                               tsq::core::sequencing::ExpressionCurveShape::exponential,
                               true);
        synth.setVoicePitchOffset (destinationId, (index % 5) * 0.1);
        if (index % 3 == 0)
            synth.noteOff (destinationId);
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        synth.render (left.data(), right.data(), 16);
        CHECK (allFinite (left));
        CHECK (allFinite (right));
        CHECK (absolutePeak (left) <= 1.0);
        CHECK (absolutePeak (right) <= 1.0);
    }
}
