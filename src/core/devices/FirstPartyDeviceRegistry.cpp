#include "core/devices/FirstPartyDeviceRegistry.h"

#include <algorithm>

namespace tsq::core::devices
{
namespace
{
FirstPartyParameterDefinition continuousParameter (std::string id,
                                                   std::string name,
                                                   double defaultNormalizedValue,
                                                   double minimumValue = 0.0,
                                                   double maximumValue = 1.0,
                                                   std::string units = {},
                                                   bool expressionTarget = true)
{
    return FirstPartyParameterDefinition {
        std::move (id),
        std::move (name),
        defaultNormalizedValue,
        minimumValue,
        maximumValue,
        std::move (units),
        FirstPartyParameterValueType::continuous,
        true,
        expressionTarget
    };
}

FirstPartyParameterDefinition discreteParameter (std::string id,
                                                 std::string name,
                                                 double defaultNormalizedValue,
                                                 double minimumValue,
                                                 double maximumValue,
                                                 std::string units = {},
                                                 bool expressionTarget = true)
{
    return FirstPartyParameterDefinition {
        std::move (id),
        std::move (name),
        defaultNormalizedValue,
        minimumValue,
        maximumValue,
        std::move (units),
        FirstPartyParameterValueType::discrete,
        true,
        expressionTarget
    };
}

double normalizedDefault (double plainValue, double minimumValue, double maximumValue) noexcept
{
    if (maximumValue <= minimumValue)
        return 0.0;

    return std::clamp ((plainValue - minimumValue) / (maximumValue - minimumValue), 0.0, 1.0);
}
}

const char* simpleOscComplexTypeId() noexcept
{
    return "tsq.device.instrument.simple-osc-complex";
}

const char* nativePhaserTypeId() noexcept
{
    return "tsq.device.audio-effect.phaser";
}

const char* nativeReverbTypeId() noexcept
{
    return "tsq.device.audio-effect.reverb";
}

const char* nativeTapeSimulatorTypeId() noexcept
{
    return "tsq.device.audio-effect.tape-simulator";
}

const FirstPartyDeviceDefinition& simpleOscComplexDefinition()
{
    static const FirstPartyDeviceDefinition definition {
        simpleOscComplexTypeId(),
        "Simple Osc Complex",
        "TheorySequencer",
        "Polyphonic triangle phase-mod oscillator into a multi-stage wavefolder and amp envelope.",
        sequencing::PluginKind::instrument,
        1,
        {
            continuousParameter ("pitch", "Pitch", 0.5, -48.0, 48.0, "st"),
            continuousParameter ("amp.level", "Amp Level", 0.75),
            continuousParameter ("osc.pm.amount", "PM Amount", 0.0),
            discreteParameter ("osc.mod.ratio", "Mod Ratio", 0.25, 0.0, 7.0, "ratio"),
            continuousParameter ("wavefolder.amount", "Fold Amount", 0.0),
            discreteParameter ("wavefolder.stages", "Fold Stages", 0.25, 1.0, 5.0, "stages"),
            continuousParameter ("amp.attack", "Attack", 0.0, 0.0, 10.0, "s"),
            continuousParameter ("amp.decay", "Decay", 0.18, 0.001, 10.0, "s"),
            continuousParameter ("amp.sustain", "Sustain", 0.75),
            continuousParameter ("amp.release", "Release", 0.0, 0.0, 20.0, "s")
        }
    };

    return definition;
}

const FirstPartyDeviceDefinition& nativePhaserDefinition()
{
    static const FirstPartyDeviceDefinition definition {
        nativePhaserTypeId(),
        "Phaser",
        "TheorySequencer",
        "Four-stage script-style phaser based on the Synthesizer VST effect.",
        sequencing::PluginKind::audioEffect,
        1,
        {
            continuousParameter ("phaser.amount", "Amount", 1.0, 0.0, 1.0, {}, false),
            continuousParameter ("phaser.speed", "Speed", normalizedDefault (0.5, 0.1, 10.0), 0.1, 10.0, "Hz", false)
        }
    };

    return definition;
}

const FirstPartyDeviceDefinition& nativeReverbDefinition()
{
    static const FirstPartyDeviceDefinition definition {
        nativeReverbTypeId(),
        "Reverb",
        "TheorySequencer",
        "Spring-flavored stereo reverb inspired by the Synthesizer VST spring effect.",
        sequencing::PluginKind::audioEffect,
        1,
        {
            continuousParameter ("reverb.mix", "Dry/Wet", 0.25, 0.0, 1.0, {}, false),
            continuousParameter ("reverb.decay", "Decay", normalizedDefault (0.65, 0.05, 1.0), 0.05, 1.0, {}, false)
        }
    };

    return definition;
}

const FirstPartyDeviceDefinition& nativeTapeSimulatorDefinition()
{
    static const FirstPartyDeviceDefinition definition {
        nativeTapeSimulatorTypeId(),
        "Tape Simulator",
        "TheorySequencer",
        "Saturation, wow/flutter, wear, hiss, and wet/dry mix from the Synthesizer VST tape effect.",
        sequencing::PluginKind::audioEffect,
        1,
        {
            continuousParameter ("tape.drive", "Drive", 0.25, 0.0, 1.0, {}, false),
            continuousParameter ("tape.instability", "Instability", 0.0, 0.0, 1.0, {}, false),
            continuousParameter ("tape.wear", "Wear", 0.0, 0.0, 1.0, {}, false),
            continuousParameter ("tape.noise", "Noise", 0.0, 0.0, 1.0, {}, false),
            continuousParameter ("tape.mix", "Mix", 0.45, 0.0, 1.0, {}, false)
        }
    };

    return definition;
}

std::vector<FirstPartyDeviceDefinition> firstPartyDeviceDefinitions()
{
    return {
        simpleOscComplexDefinition(),
        nativePhaserDefinition(),
        nativeReverbDefinition(),
        nativeTapeSimulatorDefinition()
    };
}

const FirstPartyDeviceDefinition* findFirstPartyDeviceDefinition (const std::string& typeId)
{
    if (typeId == simpleOscComplexTypeId())
        return &simpleOscComplexDefinition();
    if (typeId == nativePhaserTypeId())
        return &nativePhaserDefinition();
    if (typeId == nativeReverbTypeId())
        return &nativeReverbDefinition();
    if (typeId == nativeTapeSimulatorTypeId())
        return &nativeTapeSimulatorDefinition();

    return nullptr;
}

const FirstPartyParameterDefinition* findFirstPartyParameterDefinition (const FirstPartyDeviceDefinition& definition,
                                                                        const std::string& parameterId)
{
    const auto match = std::find_if (definition.parameters.begin(), definition.parameters.end(), [&parameterId] (const auto& parameter)
    {
        return parameter.id == parameterId;
    });

    return match == definition.parameters.end() ? nullptr : &*match;
}

std::vector<FirstPartyParameterDefinition> expressionTargetParameters (const FirstPartyDeviceDefinition& definition)
{
    std::vector<FirstPartyParameterDefinition> result;
    result.reserve (definition.parameters.size());

    for (const auto& parameter : definition.parameters)
        if (parameter.expressionTarget)
            result.push_back (parameter);

    return result;
}

sequencing::FirstPartyDeviceState defaultFirstPartyDeviceState (const FirstPartyDeviceDefinition& definition)
{
    std::vector<sequencing::FirstPartyDeviceParameterValue> values;
    values.reserve (definition.parameters.size());

    for (const auto& parameter : definition.parameters)
    {
        values.push_back (sequencing::FirstPartyDeviceParameterValue {
            parameter.id,
            parameter.defaultNormalizedValue
        });
    }

    return sequencing::FirstPartyDeviceState {
        definition.typeId,
        definition.patchVersion,
        std::move (values)
    };
}
}
