#include "engine/devices/FirstPartyEffectTracktionPlugin.h"

#include "core/devices/FirstPartyDeviceRegistry.h"

#include <algorithm>
#include <cmath>

namespace tsq::engine::devices
{
namespace te = tracktion::engine;

namespace
{
constexpr auto patchTypeProperty = "tsqFirstPartyTypeId";
constexpr auto patchVersionProperty = "tsqFirstPartyPatchVersion";

std::string statePropertyForParameter (const std::string& parameterId)
{
    return "tsqParam:" + parameterId;
}

double clampNormalized (double value) noexcept
{
    return std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0);
}

double actualValueForParameter (const core::devices::FirstPartyParameterDefinition& parameter,
                                double normalizedValue) noexcept
{
    const auto actual = parameter.minimumValue
        + (clampNormalized (normalizedValue) * (parameter.maximumValue - parameter.minimumValue));
    if (parameter.valueType == core::devices::FirstPartyParameterValueType::discrete)
        return std::round (actual);

    return actual;
}

double propertyOrDefault (const juce::ValueTree& state,
                          const core::devices::FirstPartyParameterDefinition& parameter)
{
    const auto propertyName = juce::Identifier { statePropertyForParameter (parameter.id) };
    if (! state.hasProperty (propertyName))
        return parameter.defaultNormalizedValue;

    return clampNormalized (static_cast<double> (state.getProperty (propertyName)));
}

double normalizedValueOrDefault (const core::sequencing::FirstPartyDeviceState& deviceState,
                                 const core::devices::FirstPartyParameterDefinition& parameter)
{
    const auto match = std::find_if (deviceState.parameterValues.begin(),
                                     deviceState.parameterValues.end(),
                                     [&parameter] (const auto& value)
                                     {
                                         return value.parameterId == parameter.id;
                                     });

    return match == deviceState.parameterValues.end()
        ? parameter.defaultNormalizedValue
        : clampNormalized (match->normalizedValue);
}

double actualParameterValueOrDefault (const core::sequencing::FirstPartyDeviceState& deviceState,
                                      const char* parameterId,
                                      double fallback)
{
    const auto* definition = core::devices::findFirstPartyDeviceDefinition (deviceState.typeId);
    if (definition == nullptr)
        return fallback;

    const auto* parameter = core::devices::findFirstPartyParameterDefinition (*definition, parameterId);
    if (parameter == nullptr)
        return fallback;

    return actualValueForParameter (*parameter, normalizedValueOrDefault (deviceState, *parameter));
}

core::sequencing::FirstPartyDeviceState deviceStateFromValueTree (const juce::ValueTree& state)
{
    auto typeId = state.getProperty (patchTypeProperty).toString().toStdString();
    const auto* definition = core::devices::findFirstPartyDeviceDefinition (typeId);
    if (definition == nullptr)
        return {};

    std::vector<core::sequencing::FirstPartyDeviceParameterValue> values;
    values.reserve (definition->parameters.size());
    for (const auto& parameter : definition->parameters)
    {
        values.push_back (core::sequencing::FirstPartyDeviceParameterValue {
            parameter.id,
            propertyOrDefault (state, parameter)
        });
    }

    return core::sequencing::FirstPartyDeviceState {
        definition->typeId,
        static_cast<int> (state.getProperty (patchVersionProperty, definition->patchVersion)),
        std::move (values)
    };
}
}

const char* FirstPartyEffectTracktionPlugin::xmlTypeName = "tsq_first_party_effect";

const char* FirstPartyEffectTracktionPlugin::getPluginName()
{
    return "TheorySequencer Native Effect";
}

FirstPartyEffectTracktionPlugin::FirstPartyEffectTracktionPlugin (te::PluginCreationInfo info)
    : Plugin (info)
{
}

FirstPartyEffectTracktionPlugin::~FirstPartyEffectTracktionPlugin()
{
    notifyListenersOfDeletion();
}

FirstPartyEffectTracktionPlugin::EffectType FirstPartyEffectTracktionPlugin::effectTypeForTypeId (std::string_view typeId) noexcept
{
    if (typeId == core::devices::nativePhaserTypeId())
        return EffectType::phaser;
    if (typeId == core::devices::nativeReverbTypeId())
        return EffectType::reverb;
    if (typeId == core::devices::nativeTapeSimulatorTypeId())
        return EffectType::tape;

    return EffectType::unsupported;
}

bool FirstPartyEffectTracktionPlugin::supportsTypeId (std::string_view typeId) noexcept
{
    return effectTypeForTypeId (typeId) != EffectType::unsupported;
}

juce::ValueTree FirstPartyEffectTracktionPlugin::createState (const core::sequencing::FirstPartyDeviceState& deviceState)
{
    juce::ValueTree state { te::IDs::PLUGIN };
    state.setProperty (te::IDs::type, xmlTypeName, nullptr);
    state.setProperty (patchTypeProperty, juce::String::fromUTF8 (deviceState.typeId.c_str()), nullptr);
    state.setProperty (patchVersionProperty, deviceState.patchVersion, nullptr);

    for (const auto& parameter : deviceState.parameterValues)
        if (! parameter.parameterId.empty())
            state.setProperty (juce::Identifier { statePropertyForParameter (parameter.parameterId) },
                               clampNormalized (parameter.normalizedValue),
                               nullptr);

    return state;
}

juce::String FirstPartyEffectTracktionPlugin::getName() const
{
    if (const auto* definition = core::devices::findFirstPartyDeviceDefinition (typeId_))
        return juce::String::fromUTF8 (definition->name.c_str());

    return getPluginName();
}

juce::String FirstPartyEffectTracktionPlugin::getPluginType()
{
    return xmlTypeName;
}

juce::String FirstPartyEffectTracktionPlugin::getShortName (int)
{
    switch (effectType_)
    {
        case EffectType::phaser: return "PHSR";
        case EffectType::reverb: return "RVRB";
        case EffectType::tape: return "TAPE";
        case EffectType::unsupported: break;
    }

    return "FX";
}

juce::String FirstPartyEffectTracktionPlugin::getSelectableDescription()
{
    if (const auto* definition = core::devices::findFirstPartyDeviceDefinition (typeId_))
        return juce::String::fromUTF8 (definition->shortDescription.c_str());

    return "TheorySequencer native audio effect";
}

int FirstPartyEffectTracktionPlugin::getNumOutputChannelsGivenInputs (int numInputChannels)
{
    return std::max (2, numInputChannels);
}

void FirstPartyEffectTracktionPlugin::getChannelNames (juce::StringArray* ins, juce::StringArray* outs)
{
    if (ins != nullptr)
    {
        ins->clear();
        ins->add ("Left");
        ins->add ("Right");
    }

    if (outs != nullptr)
    {
        outs->clear();
        outs->add ("Left");
        outs->add ("Right");
    }
}

bool FirstPartyEffectTracktionPlugin::takesMidiInput()
{
    return false;
}

bool FirstPartyEffectTracktionPlugin::takesAudioInput()
{
    return true;
}

bool FirstPartyEffectTracktionPlugin::isSynth()
{
    return false;
}

bool FirstPartyEffectTracktionPlugin::producesAudioWhenNoAudioInput()
{
    return false;
}

bool FirstPartyEffectTracktionPlugin::noTail()
{
    return effectType_ == EffectType::phaser;
}

double FirstPartyEffectTracktionPlugin::getTailLength() const
{
    switch (effectType_)
    {
        case EffectType::reverb: return 3.5;
        case EffectType::tape: return 0.1;
        case EffectType::phaser:
        case EffectType::unsupported: break;
    }

    return 0.0;
}

void FirstPartyEffectTracktionPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    currentSampleRate_ = info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
    phaser_.prepare (currentSampleRate_, maxBlockSize_);
    reverb_.prepare (currentSampleRate_, maxBlockSize_);
    tape_.prepare (currentSampleRate_, maxBlockSize_);
    refreshParametersFromState();
}

void FirstPartyEffectTracktionPlugin::deinitialise()
{
    reset();
}

void FirstPartyEffectTracktionPlugin::reset()
{
    phaser_.reset();
    reverb_.reset();
    tape_.reset();
}

void FirstPartyEffectTracktionPlugin::applyToBuffer (const te::PluginRenderContext& context)
{
    if (context.destBuffer == nullptr || context.bufferNumSamples <= 0)
        return;

    SCOPED_REALTIME_CHECK

    auto& buffer = *context.destBuffer;
    switch (effectType_)
    {
        case EffectType::phaser:
            phaser_.processBlock (buffer, context.bufferStartSample, context.bufferNumSamples);
            break;
        case EffectType::reverb:
            reverb_.processBlock (buffer, context.bufferStartSample, context.bufferNumSamples);
            break;
        case EffectType::tape:
            tape_.processBlock (buffer, context.bufferStartSample, context.bufferNumSamples);
            break;
        case EffectType::unsupported:
            break;
    }
}

void FirstPartyEffectTracktionPlugin::setFirstPartyDeviceState (const core::sequencing::FirstPartyDeviceState& deviceState)
{
    if (! supportsTypeId (deviceState.typeId))
        return;

    const auto* definition = core::devices::findFirstPartyDeviceDefinition (deviceState.typeId);
    if (definition == nullptr)
        return;

    state.setProperty (patchTypeProperty, juce::String::fromUTF8 (deviceState.typeId.c_str()), nullptr);
    state.setProperty (patchVersionProperty, deviceState.patchVersion, nullptr);

    for (const auto& parameter : definition->parameters)
    {
        state.setProperty (juce::Identifier { statePropertyForParameter (parameter.id) },
                           normalizedValueOrDefault (deviceState, parameter),
                           nullptr);
    }

    refreshParametersFromState();
}

void FirstPartyEffectTracktionPlugin::refreshParametersFromState()
{
    const auto deviceState = deviceStateFromValueTree (state);
    if (! deviceState.isValid() || ! supportsTypeId (deviceState.typeId))
    {
        typeId_.clear();
        effectType_ = EffectType::unsupported;
        return;
    }

    typeId_ = deviceState.typeId;
    effectType_ = effectTypeForTypeId (typeId_);

    switch (effectType_)
    {
        case EffectType::phaser:
            phaser_.setParameters (NativePhaserParameters {
                static_cast<float> (actualParameterValueOrDefault (deviceState, "phaser.amount", 1.0)),
                static_cast<float> (actualParameterValueOrDefault (deviceState, "phaser.speed", 0.5))
            });
            break;

        case EffectType::reverb:
            reverb_.setParameters (NativeReverbParameters {
                static_cast<float> (actualParameterValueOrDefault (deviceState, "reverb.mix", 0.25)),
                static_cast<float> (actualParameterValueOrDefault (deviceState, "reverb.decay", 0.65))
            });
            break;

        case EffectType::tape:
            tape_.setParameters (NativeTapeParameters {
                static_cast<float> (actualParameterValueOrDefault (deviceState, "tape.drive", 0.25)),
                static_cast<float> (actualParameterValueOrDefault (deviceState, "tape.instability", 0.0)),
                static_cast<float> (actualParameterValueOrDefault (deviceState, "tape.wear", 0.0)),
                static_cast<float> (actualParameterValueOrDefault (deviceState, "tape.noise", 0.0)),
                static_cast<float> (actualParameterValueOrDefault (deviceState, "tape.mix", 0.45))
            });
            break;

        case EffectType::unsupported:
            break;
    }
}

void registerFirstPartyEffectTracktionPlugin (te::PluginManager& pluginManager)
{
    pluginManager.createBuiltInType<FirstPartyEffectTracktionPlugin>();
}
}
