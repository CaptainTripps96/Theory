#include "engine/plugins/PluginDescription.h"

#include <cctype>

namespace tsq::engine::plugins
{
namespace
{
std::string sanitizedIdentifierPart (const std::string& text)
{
    std::string result;
    result.reserve (text.size());

    for (const auto character : text)
    {
        const auto unsignedCharacter = static_cast<unsigned char> (character);
        if (std::isalnum (unsignedCharacter) || character == '-' || character == '_' || character == '.')
            result.push_back (character);
        else if (! result.empty() && result.back() != '-')
            result.push_back ('-');
    }

    while (! result.empty() && result.back() == '-')
        result.pop_back();

    return result.empty() ? "plugin" : result;
}
}

PluginCapability classifyPlugin (const PluginDescription& plugin) noexcept
{
    if (plugin.unsupported)
        return PluginCapability::unsupported;

    if (plugin.hasAmbiguousCapabilities)
        return PluginCapability::ambiguous;

    if (plugin.isInstrument)
        return PluginCapability::instrument;

    if (plugin.isAudioEffect)
        return PluginCapability::audioEffect;

    return PluginCapability::unsupported;
}

std::string pluginCapabilityId (PluginCapability capability)
{
    switch (capability)
    {
        case PluginCapability::instrument: return "instrument";
        case PluginCapability::audioEffect: return "audioEffect";
        case PluginCapability::ambiguous: return "ambiguous";
        case PluginCapability::unsupported: return "unsupported";
    }

    return "unsupported";
}

std::string pluginCapabilityDisplayName (PluginCapability capability)
{
    switch (capability)
    {
        case PluginCapability::instrument: return "Instrument";
        case PluginCapability::audioEffect: return "Audio Effect";
        case PluginCapability::ambiguous: return "Ambiguous";
        case PluginCapability::unsupported: return "Unsupported";
    }

    return "Unsupported";
}

std::string stablePluginIdentifier (const PluginDescription& plugin)
{
    if (! plugin.stableIdentifier.empty())
        return plugin.stableIdentifier;

    if (! plugin.uniqueIdentifier.empty())
        return plugin.uniqueIdentifier;

    if (! plugin.format.empty() && plugin.uniqueId != 0)
        return plugin.format + ":" + std::to_string (plugin.uniqueId);

    if (! plugin.fileOrIdentifier.empty())
        return plugin.fileOrIdentifier;

    return sanitizedIdentifierPart (plugin.manufacturer) + ":" + sanitizedIdentifierPart (plugin.name);
}

void normalizePluginMetadata (PluginDescription& plugin)
{
    if (plugin.isInstrument)
    {
        plugin.isAudioEffect = plugin.isAudioEffect || plugin.numInputChannels > 0;
        plugin.hasAmbiguousCapabilities = plugin.hasAmbiguousCapabilities || plugin.isAudioEffect;
    }
    else if (! plugin.isAudioEffect)
    {
        plugin.isAudioEffect = plugin.numInputChannels > 0 || plugin.numOutputChannels > 0;
    }

    plugin.unsupported = plugin.unsupported || (! plugin.isInstrument && ! plugin.isAudioEffect);
    plugin.stableIdentifier = stablePluginIdentifier (plugin);

    if (plugin.parameterDiscoveryStatus.empty())
        plugin.parameterDiscoveryStatus = plugin.parametersScanned ? "complete" : "deferred";

    for (std::size_t index = 0; index < plugin.parameters.size(); ++index)
    {
        auto& parameter = plugin.parameters[index];
        if (parameter.index < 0)
            parameter.index = static_cast<int> (index);
        if (parameter.stableId.empty())
            parameter.stableId = std::to_string (parameter.index);
        if (parameter.name.empty())
            parameter.name = parameter.stableId;
    }
}
}
