#pragma once

#include "engine/plugins/PluginDescription.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>

namespace tsq::ui
{
struct BrowserPluginDragPayload
{
    std::string stableId;
    std::string displayName;
    bool isInstrument = false;
    bool isAudioEffect = false;
};

struct BrowserProjectFileDragPayload
{
    std::string kind;
    std::string relativePath;
    std::string absolutePath;
    std::string displayName;
};

juce::var makePluginDragPayload (const engine::plugins::PluginDescription& plugin);
juce::var makeProjectFileDragPayload (const BrowserProjectFileDragPayload& file);

std::optional<BrowserPluginDragPayload> pluginDragPayloadFromVar (const juce::var& payload);
std::optional<BrowserProjectFileDragPayload> projectFileDragPayloadFromVar (const juce::var& payload);
}
