#pragma once

#include "core/sequencing/DeviceChain.h"
#include "engine/devices/NativeEffectProcessors.h"

#include <tracktion_engine/tracktion_engine.h>

#include <string>
#include <string_view>

namespace tsq::engine::devices
{
class FirstPartyEffectTracktionPlugin final : public tracktion::engine::Plugin
{
public:
    explicit FirstPartyEffectTracktionPlugin (tracktion::engine::PluginCreationInfo info);
    ~FirstPartyEffectTracktionPlugin() override;

    static const char* getPluginName();
    static const char* xmlTypeName;
    static bool supportsTypeId (std::string_view typeId) noexcept;
    static juce::ValueTree createState (const core::sequencing::FirstPartyDeviceState& deviceState);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName (int suggestedLength) override;
    juce::String getSelectableDescription() override;

    int getNumOutputChannelsGivenInputs (int numInputChannels) override;
    void getChannelNames (juce::StringArray* ins, juce::StringArray* outs) override;
    bool takesMidiInput() override;
    bool takesAudioInput() override;
    bool isSynth() override;
    bool producesAudioWhenNoAudioInput() override;
    bool noTail() override;
    double getTailLength() const override;

    void initialise (const tracktion::engine::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer (const tracktion::engine::PluginRenderContext& context) override;

    void setFirstPartyDeviceState (const core::sequencing::FirstPartyDeviceState& deviceState);

private:
    enum class EffectType
    {
        unsupported,
        phaser,
        reverb,
        tape
    };

    NativePhaserProcessor phaser_;
    NativeReverbProcessor reverb_;
    NativeTapeProcessor tape_;
    std::string typeId_;
    EffectType effectType_ = EffectType::unsupported;
    double currentSampleRate_ = 44100.0;
    int maxBlockSize_ = 4096;

    void refreshParametersFromState();
    static EffectType effectTypeForTypeId (std::string_view typeId) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FirstPartyEffectTracktionPlugin)
};

void registerFirstPartyEffectTracktionPlugin (tracktion::engine::PluginManager& pluginManager);
}
