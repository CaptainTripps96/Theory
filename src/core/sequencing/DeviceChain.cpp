#include "core/sequencing/DeviceChain.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tsq::core::sequencing
{
DeviceSlotId::DeviceSlotId (std::string id)
    : value (std::move (id))
{
    if (value.empty())
        throw std::invalid_argument ("Device slot ID must not be empty");
}

bool DeviceSlotId::empty() const noexcept
{
    return value.empty();
}

bool operator== (const DeviceSlotId& lhs, const DeviceSlotId& rhs) noexcept
{
    return lhs.value == rhs.value;
}

bool operator!= (const DeviceSlotId& lhs, const DeviceSlotId& rhs) noexcept
{
    return ! (lhs == rhs);
}

std::string pluginKindId (PluginKind kind)
{
    switch (kind)
    {
        case PluginKind::unknown: return "unknown";
        case PluginKind::instrument: return "instrument";
        case PluginKind::audioEffect: return "audioEffect";
        case PluginKind::midiEffect: return "midiEffect";
    }

    return "unknown";
}

PluginKind pluginKindFromId (std::string_view id)
{
    if (id == "unknown") return PluginKind::unknown;
    if (id == "instrument") return PluginKind::instrument;
    if (id == "audioEffect") return PluginKind::audioEffect;
    if (id == "midiEffect") return PluginKind::midiEffect;

    throw std::invalid_argument ("Unknown plugin kind '" + std::string { id } + "'");
}

bool PluginReference::isValid() const noexcept
{
    return ! fileOrIdentifier.empty() || ! uniqueIdentifier.empty();
}

PluginReference PluginReference::fromTrackInstrumentReference (const TrackInstrumentReference& instrument)
{
    return PluginReference {
        instrument.pluginName,
        instrument.manufacturer,
        instrument.format,
        instrument.fileOrIdentifier,
        instrument.uniqueIdentifier,
        instrument.uniqueId,
        instrument.deprecatedUid,
        instrument.numInputChannels,
        instrument.numOutputChannels
    };
}

TrackInstrumentReference PluginReference::toTrackInstrumentReference (std::string pluginStateFile) const
{
    return TrackInstrumentReference {
        pluginName,
        manufacturer,
        format,
        fileOrIdentifier,
        uniqueIdentifier,
        uniqueId,
        deprecatedUid,
        true,
        numInputChannels,
        numOutputChannels,
        std::move (pluginStateFile)
    };
}

bool operator== (const PluginReference& lhs, const PluginReference& rhs) noexcept
{
    return lhs.pluginName == rhs.pluginName
        && lhs.manufacturer == rhs.manufacturer
        && lhs.format == rhs.format
        && lhs.fileOrIdentifier == rhs.fileOrIdentifier
        && lhs.uniqueIdentifier == rhs.uniqueIdentifier
        && lhs.uniqueId == rhs.uniqueId
        && lhs.deprecatedUid == rhs.deprecatedUid
        && lhs.numInputChannels == rhs.numInputChannels
        && lhs.numOutputChannels == rhs.numOutputChannels;
}

bool operator!= (const PluginReference& lhs, const PluginReference& rhs) noexcept
{
    return ! (lhs == rhs);
}

DeviceSlot::DeviceSlot (DeviceSlotId id, PluginReference plugin, PluginKind kind)
    : id_ (std::move (id)),
      plugin_ (std::move (plugin)),
      kind_ (kind)
{
    if (id_.empty())
        throw std::invalid_argument ("Device slot requires a stable ID");

    if (! plugin_.isValid())
        throw std::invalid_argument ("Device slot requires a valid plugin reference");
}

const DeviceSlotId& DeviceSlot::id() const noexcept
{
    return id_;
}

const PluginReference& DeviceSlot::plugin() const noexcept
{
    return plugin_;
}

PluginKind DeviceSlot::kind() const noexcept
{
    return kind_;
}

bool DeviceSlot::bypassed() const noexcept
{
    return bypassed_;
}

void DeviceSlot::setBypassed (bool bypassed) noexcept
{
    bypassed_ = bypassed;
}

const std::string& DeviceSlot::pluginStateFile() const noexcept
{
    return pluginStateFile_;
}

void DeviceSlot::setPluginStateFile (std::string pluginStateFile)
{
    pluginStateFile_ = std::move (pluginStateFile);
}

bool operator== (const DeviceSlot& lhs, const DeviceSlot& rhs) noexcept
{
    return lhs.id() == rhs.id()
        && lhs.plugin() == rhs.plugin()
        && lhs.kind() == rhs.kind()
        && lhs.bypassed() == rhs.bypassed()
        && lhs.pluginStateFile() == rhs.pluginStateFile();
}

bool operator!= (const DeviceSlot& lhs, const DeviceSlot& rhs) noexcept
{
    return ! (lhs == rhs);
}

const std::vector<DeviceSlot>& DeviceChain::slots() const noexcept
{
    return slots_;
}

bool DeviceChain::empty() const noexcept
{
    return slots_.empty();
}

std::size_t DeviceChain::size() const noexcept
{
    return slots_.size();
}

DeviceSlot* DeviceChain::findSlot (const DeviceSlotId& slotId) noexcept
{
    const auto match = std::find_if (slots_.begin(), slots_.end(), [&slotId] (const auto& slot) {
        return slot.id() == slotId;
    });

    return match == slots_.end() ? nullptr : &*match;
}

const DeviceSlot* DeviceChain::findSlot (const DeviceSlotId& slotId) const noexcept
{
    const auto match = std::find_if (slots_.begin(), slots_.end(), [&slotId] (const auto& slot) {
        return slot.id() == slotId;
    });

    return match == slots_.end() ? nullptr : &*match;
}

std::optional<std::size_t> DeviceChain::indexOf (const DeviceSlotId& slotId) const noexcept
{
    for (std::size_t index = 0; index < slots_.size(); ++index)
        if (slots_[index].id() == slotId)
            return index;

    return std::nullopt;
}

void DeviceChain::appendSlot (DeviceSlot slot)
{
    insertSlot (slots_.size(), std::move (slot));
}

void DeviceChain::insertSlot (std::size_t index, DeviceSlot slot)
{
    requireUniqueSlotId (slot.id());
    if (index > slots_.size())
        throw std::out_of_range ("Device insert index is out of range");

    slots_.insert (slots_.begin() + static_cast<std::ptrdiff_t> (index), std::move (slot));
}

DeviceSlot DeviceChain::removeSlot (const DeviceSlotId& slotId)
{
    const auto match = std::find_if (slots_.begin(), slots_.end(), [&slotId] (const auto& slot) {
        return slot.id() == slotId;
    });

    if (match == slots_.end())
        throw std::invalid_argument ("Device chain does not contain the requested slot");

    auto removed = *match;
    slots_.erase (match);
    return removed;
}

void DeviceChain::replaceSlot (const DeviceSlotId& slotId, DeviceSlot replacement)
{
    if (slotId != replacement.id())
        throw std::invalid_argument ("Replacement device slot ID must match");

    auto* slot = findSlot (slotId);
    if (slot == nullptr)
        throw std::invalid_argument ("Device chain does not contain the requested slot");

    *slot = std::move (replacement);
}

void DeviceChain::moveSlot (const DeviceSlotId& slotId, std::size_t newIndex)
{
    const auto currentIndex = indexOf (slotId);
    if (! currentIndex.has_value())
        throw std::invalid_argument ("Device chain does not contain the requested slot");

    if (newIndex >= slots_.size())
        throw std::out_of_range ("Device move index is out of range");

    if (*currentIndex == newIndex)
        return;

    auto slot = slots_[*currentIndex];
    slots_.erase (slots_.begin() + static_cast<std::ptrdiff_t> (*currentIndex));
    slots_.insert (slots_.begin() + static_cast<std::ptrdiff_t> (newIndex), std::move (slot));
}

void DeviceChain::setSlotBypassed (const DeviceSlotId& slotId, bool bypassed)
{
    auto* slot = findSlot (slotId);
    if (slot == nullptr)
        throw std::invalid_argument ("Device chain does not contain the requested slot");

    slot->setBypassed (bypassed);
}

int DeviceChain::instrumentSlotCount() const noexcept
{
    return static_cast<int> (std::count_if (slots_.begin(), slots_.end(), [] (const auto& slot) {
        return slot.kind() == PluginKind::instrument;
    }));
}

bool DeviceChain::hasSlotId (const DeviceSlotId& slotId) const noexcept
{
    return findSlot (slotId) != nullptr;
}

void DeviceChain::requireUniqueSlotId (const DeviceSlotId& slotId) const
{
    if (hasSlotId (slotId))
        throw std::invalid_argument ("Device chain already contains a slot with this ID");
}

std::vector<std::string> validateDeviceChainForTrackType (TrackType trackType, const DeviceChain& chain)
{
    std::vector<std::string> errors;
    auto instrumentSeen = false;

    for (const auto& slot : chain.slots())
    {
        if (slot.kind() == PluginKind::instrument)
        {
            if (! trackTypeCanHostInstrument (trackType))
                errors.push_back ("Instrument devices are only valid on MIDI tracks");

            if (instrumentSeen)
                errors.push_back ("A device chain may contain only one instrument");

            instrumentSeen = true;
            continue;
        }

        if (slot.kind() == PluginKind::midiEffect)
        {
            if (trackType != TrackType::midi)
                errors.push_back ("MIDI effects are only valid on MIDI tracks");

            if (instrumentSeen)
                errors.push_back ("MIDI effects must appear before the instrument");

            continue;
        }

        if (slot.kind() == PluginKind::audioEffect)
        {
            if (! trackTypeCanHostAudioEffects (trackType))
                errors.push_back ("Audio effects are not valid on this track type");

            if (trackType == TrackType::midi && ! instrumentSeen)
                errors.push_back ("Audio effects on MIDI tracks must appear after the instrument");
        }
    }

    return errors;
}
}
