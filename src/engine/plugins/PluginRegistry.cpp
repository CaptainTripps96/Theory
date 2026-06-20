#include "engine/plugins/PluginRegistry.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>

namespace tsq::engine::plugins
{
namespace
{
constexpr auto rootTag = "TheorySequencerPluginRegistry";
constexpr auto pluginTag = "Plugin";
constexpr auto parametersTag = "Parameters";
constexpr auto parameterTag = "Parameter";
constexpr auto schemaVersion = 2;

std::string toStdString (const juce::String& text)
{
    return text.toStdString();
}

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}

bool comparePlugins (const PluginDescription& lhs, const PluginDescription& rhs)
{
    if (lhs.name != rhs.name)
        return lhs.name < rhs.name;

    if (lhs.manufacturer != rhs.manufacturer)
        return lhs.manufacturer < rhs.manufacturer;

    return lhs.fileOrIdentifier < rhs.fileOrIdentifier;
}

PluginParameterDescription parameterFromXml (const juce::XmlElement& parameterXml)
{
    PluginParameterDescription parameter;
    parameter.index = parameterXml.getIntAttribute ("index", -1);
    parameter.stableId = toStdString (parameterXml.getStringAttribute ("stableId"));
    parameter.name = toStdString (parameterXml.getStringAttribute ("name"));
    parameter.label = toStdString (parameterXml.getStringAttribute ("label"));
    parameter.units = toStdString (parameterXml.getStringAttribute ("units"));
    parameter.defaultValueNormalized = parameterXml.getDoubleAttribute ("defaultValueNormalized", 0.0);
    parameter.minimumValue = parameterXml.getDoubleAttribute ("minimumValue", 0.0);
    parameter.maximumValue = parameterXml.getDoubleAttribute ("maximumValue", 1.0);
    parameter.automatable = parameterXml.getBoolAttribute ("automatable", true);
    parameter.discrete = parameterXml.getBoolAttribute ("discrete", false);
    return parameter;
}

void writeParameterXml (juce::XmlElement& parent, const PluginParameterDescription& parameter)
{
    auto* parameterXml = parent.createNewChildElement (parameterTag);
    parameterXml->setAttribute ("index", parameter.index);
    parameterXml->setAttribute ("stableId", toJuceString (parameter.stableId));
    parameterXml->setAttribute ("name", toJuceString (parameter.name));
    parameterXml->setAttribute ("label", toJuceString (parameter.label));
    parameterXml->setAttribute ("units", toJuceString (parameter.units));
    parameterXml->setAttribute ("defaultValueNormalized", parameter.defaultValueNormalized);
    parameterXml->setAttribute ("minimumValue", parameter.minimumValue);
    parameterXml->setAttribute ("maximumValue", parameter.maximumValue);
    parameterXml->setAttribute ("automatable", parameter.automatable);
    parameterXml->setAttribute ("discrete", parameter.discrete);
}
}

PluginRegistry::PluginRegistry (std::string cacheFilePath)
    : cacheFilePath_ (std::move (cacheFilePath))
{
}

std::vector<PluginDescription> PluginRegistry::plugins() const
{
    std::lock_guard lock { mutex_ };
    return plugins_;
}

std::vector<PluginDescription> PluginRegistry::instruments() const
{
    std::lock_guard lock { mutex_ };
    std::vector<PluginDescription> result;
    result.reserve (plugins_.size());
    for (const auto& plugin : plugins_)
        if (plugin.isInstrument)
            result.push_back (plugin);

    return result;
}

std::vector<PluginDescription> PluginRegistry::audioEffects() const
{
    std::lock_guard lock { mutex_ };
    std::vector<PluginDescription> result;
    result.reserve (plugins_.size());
    for (const auto& plugin : plugins_)
        if (plugin.isAudioEffect)
            result.push_back (plugin);

    return result;
}

int PluginRegistry::pluginCount() const
{
    std::lock_guard lock { mutex_ };
    return static_cast<int> (plugins_.size());
}

std::uint64_t PluginRegistry::revision() const
{
    std::lock_guard lock { mutex_ };
    return revision_;
}

std::optional<PluginDescription> PluginRegistry::findByStableId (std::string_view stableId) const
{
    std::lock_guard lock { mutex_ };
    const auto match = stableIndexById_.find (std::string { stableId });
    if (match == stableIndexById_.end() || match->second >= plugins_.size())
        return std::nullopt;

    return plugins_[match->second];
}

void PluginRegistry::replaceAll (std::vector<PluginDescription> plugins)
{
    for (auto& plugin : plugins)
        normalizePluginMetadata (plugin);

    std::sort (plugins.begin(), plugins.end(), comparePlugins);

    std::unordered_map<std::string, std::size_t> stableIndexById;
    stableIndexById.reserve (plugins.size());
    for (std::size_t index = 0; index < plugins.size(); ++index)
        stableIndexById.emplace (stablePluginIdentifier (plugins[index]), index);

    std::lock_guard lock { mutex_ };
    plugins_ = std::move (plugins);
    stableIndexById_ = std::move (stableIndexById);
    ++revision_;
}

void PluginRegistry::clear()
{
    std::lock_guard lock { mutex_ };
    plugins_.clear();
    stableIndexById_.clear();
    ++revision_;
}

bool PluginRegistry::load()
{
    auto cacheFile = juce::File::createFileWithoutCheckingPath (toJuceString (cacheFilePath_));

    if (! cacheFile.existsAsFile())
        return true;

    auto xml = juce::parseXML (cacheFile);

    if (xml == nullptr || ! xml->hasTagName (rootTag))
        return false;

    const auto loadedSchemaVersion = xml->getIntAttribute ("schemaVersion", 0);
    if (loadedSchemaVersion <= 0 || loadedSchemaVersion > schemaVersion)
        return false;

    std::vector<PluginDescription> loadedPlugins;
    loadedPlugins.reserve (static_cast<std::size_t> (std::max (0, xml->getNumChildElements())));

    for (auto* pluginXml : xml->getChildIterator())
    {
        if (pluginXml == nullptr || ! pluginXml->hasTagName (pluginTag))
            continue;

        PluginDescription plugin;
        plugin.name = toStdString (pluginXml->getStringAttribute ("name"));
        plugin.manufacturer = toStdString (pluginXml->getStringAttribute ("manufacturer"));
        plugin.format = toStdString (pluginXml->getStringAttribute ("format"));
        plugin.category = toStdString (pluginXml->getStringAttribute ("category"));
        plugin.version = toStdString (pluginXml->getStringAttribute ("version"));
        plugin.fileOrIdentifier = toStdString (pluginXml->getStringAttribute ("fileOrIdentifier"));
        plugin.uniqueIdentifier = toStdString (pluginXml->getStringAttribute ("uniqueIdentifier"));
        plugin.uniqueId = pluginXml->getIntAttribute ("uniqueId", 0);
        plugin.deprecatedUid = pluginXml->getIntAttribute ("deprecatedUid", 0);
        plugin.stableIdentifier = toStdString (pluginXml->getStringAttribute ("stableIdentifier"));
        plugin.isInstrument = pluginXml->getBoolAttribute ("isInstrument", false);
        plugin.isAudioEffect = pluginXml->getBoolAttribute ("isAudioEffect", false);
        plugin.hasAmbiguousCapabilities = pluginXml->getBoolAttribute ("hasAmbiguousCapabilities", false);
        plugin.unsupported = pluginXml->getBoolAttribute ("unsupported", false);
        plugin.numInputChannels = pluginXml->getIntAttribute ("numInputChannels", 0);
        plugin.numOutputChannels = pluginXml->getIntAttribute ("numOutputChannels", 0);
        plugin.parametersScanned = pluginXml->getBoolAttribute ("parametersScanned", false);
        plugin.parameterDiscoveryStatus = toStdString (pluginXml->getStringAttribute ("parameterDiscoveryStatus"));

        if (auto* parametersXml = pluginXml->getChildByName (parametersTag))
        {
            plugin.parameters.reserve (static_cast<std::size_t> (std::max (0, parametersXml->getNumChildElements())));
            for (auto* parameterXml : parametersXml->getChildIterator())
                if (parameterXml != nullptr && parameterXml->hasTagName (parameterTag))
                    plugin.parameters.push_back (parameterFromXml (*parameterXml));
        }

        normalizePluginMetadata (plugin);

        if (! plugin.name.empty() || ! plugin.fileOrIdentifier.empty())
            loadedPlugins.push_back (std::move (plugin));
    }

    replaceAll (std::move (loadedPlugins));
    return true;
}

bool PluginRegistry::save() const
{
    auto cacheFile = juce::File::createFileWithoutCheckingPath (toJuceString (cacheFilePath_));

    if (! cacheFile.getParentDirectory().createDirectory())
        return false;

    juce::XmlElement root { rootTag };
    root.setAttribute ("schemaVersion", schemaVersion);

    const auto snapshot = plugins();

    for (const auto& plugin : snapshot)
    {
        auto* pluginXml = root.createNewChildElement (pluginTag);
        pluginXml->setAttribute ("name", toJuceString (plugin.name));
        pluginXml->setAttribute ("manufacturer", toJuceString (plugin.manufacturer));
        pluginXml->setAttribute ("format", toJuceString (plugin.format));
        pluginXml->setAttribute ("category", toJuceString (plugin.category));
        pluginXml->setAttribute ("version", toJuceString (plugin.version));
        pluginXml->setAttribute ("fileOrIdentifier", toJuceString (plugin.fileOrIdentifier));
        pluginXml->setAttribute ("uniqueIdentifier", toJuceString (plugin.uniqueIdentifier));
        pluginXml->setAttribute ("uniqueId", plugin.uniqueId);
        pluginXml->setAttribute ("deprecatedUid", plugin.deprecatedUid);
        pluginXml->setAttribute ("stableIdentifier", toJuceString (stablePluginIdentifier (plugin)));
        pluginXml->setAttribute ("isInstrument", plugin.isInstrument);
        pluginXml->setAttribute ("isAudioEffect", plugin.isAudioEffect);
        pluginXml->setAttribute ("hasAmbiguousCapabilities", plugin.hasAmbiguousCapabilities);
        pluginXml->setAttribute ("unsupported", plugin.unsupported);
        pluginXml->setAttribute ("numInputChannels", plugin.numInputChannels);
        pluginXml->setAttribute ("numOutputChannels", plugin.numOutputChannels);
        pluginXml->setAttribute ("capability", toJuceString (pluginCapabilityId (classifyPlugin (plugin))));
        pluginXml->setAttribute ("parametersScanned", plugin.parametersScanned);
        pluginXml->setAttribute ("parameterDiscoveryStatus", toJuceString (plugin.parameterDiscoveryStatus));

        auto* parametersXml = pluginXml->createNewChildElement (parametersTag);
        for (const auto& parameter : plugin.parameters)
            writeParameterXml (*parametersXml, parameter);
    }

    return root.writeTo (cacheFile);
}

const std::string& PluginRegistry::cacheFilePath() const noexcept
{
    return cacheFilePath_;
}
}
