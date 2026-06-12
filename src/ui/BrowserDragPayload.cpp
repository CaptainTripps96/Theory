#include "ui/BrowserDragPayload.h"

#include "engine/plugins/PluginDescription.h"

namespace tsq::ui
{
namespace
{
constexpr auto payloadTypeProperty = "tsqPayloadType";
constexpr auto pluginPayloadType = "plugin";
constexpr auto projectFilePayloadType = "projectFile";

std::string stringProperty (const juce::DynamicObject& object, const juce::Identifier& property)
{
    return object.getProperty (property).toString().toStdString();
}

juce::String toJuceString (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}
}

juce::var makePluginDragPayload (const engine::plugins::PluginDescription& plugin)
{
    juce::var payload { new juce::DynamicObject {} };
    auto* object = payload.getDynamicObject();

    object->setProperty (payloadTypeProperty, pluginPayloadType);
    object->setProperty ("stableId", toJuceString (engine::plugins::stablePluginIdentifier (plugin)));
    object->setProperty ("displayName", toJuceString (plugin.name.empty() ? plugin.fileOrIdentifier : plugin.name));
    object->setProperty ("isInstrument", plugin.isInstrument);
    object->setProperty ("isAudioEffect", plugin.isAudioEffect);

    return payload;
}

juce::var makeProjectFileDragPayload (const BrowserProjectFileDragPayload& file)
{
    juce::var payload { new juce::DynamicObject {} };
    auto* object = payload.getDynamicObject();

    object->setProperty (payloadTypeProperty, projectFilePayloadType);
    object->setProperty ("kind", toJuceString (file.kind));
    object->setProperty ("relativePath", toJuceString (file.relativePath));
    object->setProperty ("absolutePath", toJuceString (file.absolutePath));
    object->setProperty ("displayName", toJuceString (file.displayName));

    return payload;
}

std::optional<BrowserPluginDragPayload> pluginDragPayloadFromVar (const juce::var& payload)
{
    const auto* object = payload.getDynamicObject();
    if (object == nullptr || object->getProperty (payloadTypeProperty).toString() != pluginPayloadType)
        return std::nullopt;

    BrowserPluginDragPayload result;
    result.stableId = stringProperty (*object, "stableId");
    result.displayName = stringProperty (*object, "displayName");
    result.isInstrument = static_cast<bool> (object->getProperty ("isInstrument"));
    result.isAudioEffect = static_cast<bool> (object->getProperty ("isAudioEffect"));

    if (result.stableId.empty())
        return std::nullopt;

    return result;
}

std::optional<BrowserProjectFileDragPayload> projectFileDragPayloadFromVar (const juce::var& payload)
{
    const auto* object = payload.getDynamicObject();
    if (object == nullptr || object->getProperty (payloadTypeProperty).toString() != projectFilePayloadType)
        return std::nullopt;

    BrowserProjectFileDragPayload result;
    result.kind = stringProperty (*object, "kind");
    result.relativePath = stringProperty (*object, "relativePath");
    result.absolutePath = stringProperty (*object, "absolutePath");
    result.displayName = stringProperty (*object, "displayName");

    if (result.kind.empty() || result.relativePath.empty())
        return std::nullopt;

    return result;
}
}
