#pragma once

#include "core/devices/FirstPartyDeviceRegistry.h"
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

struct BrowserFirstPartyDeviceDragPayload
{
    std::string typeId;
    std::string displayName;
    core::sequencing::PluginKind kind = core::sequencing::PluginKind::unknown;
};

juce::var makePluginDragPayload (const engine::plugins::PluginDescription& plugin);
juce::var makeProjectFileDragPayload (const BrowserProjectFileDragPayload& file);
juce::var makeFirstPartyDeviceDragPayload (const core::devices::FirstPartyDeviceDefinition& device);

std::optional<BrowserPluginDragPayload> pluginDragPayloadFromVar (const juce::var& payload);
std::optional<BrowserProjectFileDragPayload> projectFileDragPayloadFromVar (const juce::var& payload);
std::optional<BrowserFirstPartyDeviceDragPayload> firstPartyDeviceDragPayloadFromVar (const juce::var& payload);
}
