#pragma once

#include <string>
#include <vector>

namespace tsq::engine::plugins
{
enum class PluginCapability
{
    instrument,
    audioEffect,
    ambiguous,
    unsupported
};

struct PluginParameterDescription
{
    int index = -1;
    std::string stableId;
    std::string name;
    std::string label;
    std::string units;
    double defaultValueNormalized = 0.0;
    double minimumValue = 0.0;
    double maximumValue = 1.0;
    bool automatable = true;
    bool discrete = false;
};

struct PluginDescription
{
    std::string name;
    std::string manufacturer;
    std::string format;
    std::string category;
    std::string version;
    std::string fileOrIdentifier;
    std::string uniqueIdentifier;
    int uniqueId = 0;
    int deprecatedUid = 0;
    std::string stableIdentifier;
    bool isInstrument = false;
    bool isAudioEffect = false;
    bool hasAmbiguousCapabilities = false;
    bool unsupported = false;
    int numInputChannels = 0;
    int numOutputChannels = 0;
    bool parametersScanned = false;
    std::string parameterDiscoveryStatus;
    std::vector<PluginParameterDescription> parameters;
};

PluginCapability classifyPlugin (const PluginDescription& plugin) noexcept;
std::string pluginCapabilityId (PluginCapability capability);
std::string pluginCapabilityDisplayName (PluginCapability capability);
std::string stablePluginIdentifier (const PluginDescription& plugin);
void normalizePluginMetadata (PluginDescription& plugin);
}
