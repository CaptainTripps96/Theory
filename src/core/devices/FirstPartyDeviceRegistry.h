#pragma once

#include "core/sequencing/DeviceChain.h"

#include <string>
#include <vector>

namespace tsq::core::devices
{
enum class FirstPartyParameterValueType
{
    continuous,
    discrete
};

struct FirstPartyParameterDefinition
{
    std::string id;
    std::string name;
    double defaultNormalizedValue = 0.0;
    double minimumValue = 0.0;
    double maximumValue = 1.0;
    std::string units;
    FirstPartyParameterValueType valueType = FirstPartyParameterValueType::continuous;
    bool automatable = true;
    bool expressionTarget = true;
};

struct FirstPartyDeviceDefinition
{
    std::string typeId;
    std::string name;
    std::string manufacturer;
    std::string shortDescription;
    sequencing::PluginKind kind = sequencing::PluginKind::unknown;
    int patchVersion = 1;
    std::vector<FirstPartyParameterDefinition> parameters;
};

const char* simpleOscComplexTypeId() noexcept;
const char* nativePhaserTypeId() noexcept;
const char* nativeReverbTypeId() noexcept;
const char* nativeTapeSimulatorTypeId() noexcept;
const FirstPartyDeviceDefinition& simpleOscComplexDefinition();
const FirstPartyDeviceDefinition& nativePhaserDefinition();
const FirstPartyDeviceDefinition& nativeReverbDefinition();
const FirstPartyDeviceDefinition& nativeTapeSimulatorDefinition();
std::vector<FirstPartyDeviceDefinition> firstPartyDeviceDefinitions();
const FirstPartyDeviceDefinition* findFirstPartyDeviceDefinition (const std::string& typeId);
const FirstPartyParameterDefinition* findFirstPartyParameterDefinition (const FirstPartyDeviceDefinition& definition,
                                                                        const std::string& parameterId);
std::vector<FirstPartyParameterDefinition> expressionTargetParameters (const FirstPartyDeviceDefinition& definition);
sequencing::FirstPartyDeviceState defaultFirstPartyDeviceState (const FirstPartyDeviceDefinition& definition);
}
