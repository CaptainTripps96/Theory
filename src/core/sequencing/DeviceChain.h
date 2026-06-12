#pragma once

#include "core/sequencing/TrackInstrumentReference.h"
#include "core/sequencing/TrackType.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::sequencing
{
struct DeviceSlotId
{
    std::string value;

    DeviceSlotId() = default;
    explicit DeviceSlotId (std::string id);

    bool empty() const noexcept;
};

bool operator== (const DeviceSlotId& lhs, const DeviceSlotId& rhs) noexcept;
bool operator!= (const DeviceSlotId& lhs, const DeviceSlotId& rhs) noexcept;

enum class PluginKind
{
    unknown,
    instrument,
    audioEffect,
    midiEffect
};

std::string pluginKindId (PluginKind kind);
PluginKind pluginKindFromId (std::string_view id);

struct PluginReference
{
    std::string pluginName;
    std::string manufacturer;
    std::string format;
    std::string fileOrIdentifier;
    std::string uniqueIdentifier;
    int uniqueId = 0;
    int deprecatedUid = 0;
    int numInputChannels = 0;
    int numOutputChannels = 0;

    bool isValid() const noexcept;

    static PluginReference fromTrackInstrumentReference (const TrackInstrumentReference& instrument);
    TrackInstrumentReference toTrackInstrumentReference (std::string pluginStateFile = {}) const;
};

bool operator== (const PluginReference& lhs, const PluginReference& rhs) noexcept;
bool operator!= (const PluginReference& lhs, const PluginReference& rhs) noexcept;

class DeviceSlot
{
public:
    DeviceSlot (DeviceSlotId id, PluginReference plugin, PluginKind kind);

    const DeviceSlotId& id() const noexcept;
    const PluginReference& plugin() const noexcept;
    PluginKind kind() const noexcept;
    bool bypassed() const noexcept;
    void setBypassed (bool bypassed) noexcept;
    const std::string& pluginStateFile() const noexcept;
    void setPluginStateFile (std::string pluginStateFile);

private:
    DeviceSlotId id_;
    PluginReference plugin_;
    PluginKind kind_ = PluginKind::unknown;
    bool bypassed_ = false;
    std::string pluginStateFile_;
};

bool operator== (const DeviceSlot& lhs, const DeviceSlot& rhs) noexcept;
bool operator!= (const DeviceSlot& lhs, const DeviceSlot& rhs) noexcept;

class DeviceChain
{
public:
    const std::vector<DeviceSlot>& slots() const noexcept;
    bool empty() const noexcept;
    std::size_t size() const noexcept;

    DeviceSlot* findSlot (const DeviceSlotId& slotId) noexcept;
    const DeviceSlot* findSlot (const DeviceSlotId& slotId) const noexcept;
    std::optional<std::size_t> indexOf (const DeviceSlotId& slotId) const noexcept;

    void appendSlot (DeviceSlot slot);
    void insertSlot (std::size_t index, DeviceSlot slot);
    DeviceSlot removeSlot (const DeviceSlotId& slotId);
    void replaceSlot (const DeviceSlotId& slotId, DeviceSlot replacement);
    void moveSlot (const DeviceSlotId& slotId, std::size_t newIndex);
    void setSlotBypassed (const DeviceSlotId& slotId, bool bypassed);

    int instrumentSlotCount() const noexcept;
    bool hasSlotId (const DeviceSlotId& slotId) const noexcept;

private:
    std::vector<DeviceSlot> slots_;

    void requireUniqueSlotId (const DeviceSlotId& slotId) const;
};

std::vector<std::string> validateDeviceChainForTrackType (TrackType trackType, const DeviceChain& chain);
}
