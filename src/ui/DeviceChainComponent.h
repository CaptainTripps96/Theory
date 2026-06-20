#pragma once

#include "core/sequencing/DeviceChain.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace tsq::app
{
class AppServices;
}

namespace tsq::ui
{
struct BrowserPluginDragPayload;
struct BrowserFirstPartyDeviceDragPayload;

class DeviceChainComponent final : public juce::Component
{
public:
    explicit DeviceChainComponent (app::AppServices& appServices);
    ~DeviceChainComponent() override;

    void refresh();
    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    class ChainContentComponent;

    app::AppServices& appServices_;
    std::unique_ptr<ChainContentComponent> chainContent_;
    juce::Viewport chainViewport_;
    juce::Label trackLabel_;
    juce::Label flowLabel_;

    std::optional<core::sequencing::PluginKind> pluginDropKindForSelectedTrack (const BrowserPluginDragPayload& payload) const;
    std::optional<core::sequencing::PluginKind> firstPartyDeviceDropKindForSelectedTrack (const BrowserFirstPartyDeviceDragPayload& payload) const;
    bool insertPluginPayloadIntoSelectedTrack (const BrowserPluginDragPayload& payload, std::size_t insertIndex);
    bool insertFirstPartyDevicePayloadIntoSelectedTrack (const BrowserFirstPartyDeviceDragPayload& payload, std::size_t insertIndex);
    bool replaceDeviceWithPluginPayload (const std::string& trackId,
                                         const core::sequencing::DeviceSlotId& slotId,
                                         const BrowserPluginDragPayload& payload);
    bool moveDevice (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId, std::size_t targetIndex);
    bool removeDevice (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId);
    bool setDeviceBypassed (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId, bool bypassed);
    bool setFirstPartyDeviceParameter (const std::string& trackId,
                                       const core::sequencing::DeviceSlotId& slotId,
                                       const std::string& parameterId,
                                       double normalizedValue);
    bool openDeviceEditor (const std::string& trackId, const core::sequencing::DeviceSlotId& slotId);
    bool updateLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceChainComponent)
};
}
