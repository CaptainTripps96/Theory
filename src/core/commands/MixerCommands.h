#pragma once

#include "core/commands/Command.h"
#include "core/sequencing/Automation.h"
#include "core/sequencing/DeviceChain.h"
#include "core/sequencing/MixerStrip.h"
#include "core/sequencing/Routing.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace tsq::core::commands
{
class SetTrackMixerStripCommand final : public Command
{
public:
    SetTrackMixerStripCommand (std::string trackId, sequencing::MixerStrip mixerStrip);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::mixer; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::MixerStrip mixerStrip_;
    sequencing::MixerStrip previousMixerStrip_;
};

class SetTrackRoutingCommand final : public Command
{
public:
    SetTrackRoutingCommand (std::string trackId, sequencing::TrackRouting routing);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::routing; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::TrackRouting routing_;
    sequencing::TrackRouting previousRouting_;
};

class SetTrackDeviceChainCommand final : public Command
{
public:
    SetTrackDeviceChainCommand (std::string trackId, sequencing::DeviceChain deviceChain);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::DeviceChain deviceChain_;
    sequencing::DeviceChain previousDeviceChain_;
};

class AddTrackDeviceCommand final : public Command
{
public:
    AddTrackDeviceCommand (std::string trackId, sequencing::DeviceSlot slot, std::optional<std::size_t> insertIndex = std::nullopt);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::DeviceSlot slot_;
    std::optional<std::size_t> insertIndex_;
};

class RemoveTrackDeviceCommand final : public Command
{
public:
    RemoveTrackDeviceCommand (std::string trackId, sequencing::DeviceSlotId slotId);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::DeviceSlotId slotId_;
    std::optional<sequencing::DeviceSlot> removedSlot_;
    std::optional<std::size_t> removedIndex_;
    std::vector<sequencing::AutomationLane> removedAutomationLanes_;
};

class ReplaceTrackDeviceCommand final : public Command
{
public:
    ReplaceTrackDeviceCommand (std::string trackId, sequencing::DeviceSlot replacement);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::DeviceSlot replacement_;
    std::optional<sequencing::DeviceSlot> previousSlot_;
    std::vector<sequencing::AutomationLane> removedAutomationLanes_;
};

class MoveTrackDeviceCommand final : public Command
{
public:
    MoveTrackDeviceCommand (std::string trackId, sequencing::DeviceSlotId slotId, std::size_t targetIndex);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::DeviceSlotId slotId_;
    std::size_t targetIndex_ = 0;
    std::optional<std::size_t> previousIndex_;
};

class SetTrackDeviceBypassCommand final : public Command
{
public:
    SetTrackDeviceBypassCommand (std::string trackId, sequencing::DeviceSlotId slotId, bool bypassed);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::DeviceSlotId slotId_;
    bool bypassed_ = false;
    std::optional<bool> previousBypassed_;
};

class SetFirstPartyDeviceParameterCommand final : public Command
{
public:
    SetFirstPartyDeviceParameterCommand (std::string trackId,
                                         sequencing::DeviceSlotId slotId,
                                         std::string parameterId,
                                         double normalizedValue);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::deviceChain; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::DeviceSlotId slotId_;
    std::string parameterId_;
    double normalizedValue_ = 0.0;
    std::optional<double> previousNormalizedValue_;
    bool previousParameterExisted_ = false;
};

class SetTrackAutomationLaneCommand final : public Command
{
public:
    SetTrackAutomationLaneCommand (std::string trackId, sequencing::AutomationLane lane);

    std::string name() const override;
    PlaybackSyncCategory playbackSyncCategory() const noexcept override { return PlaybackSyncCategory::automation; }
    CommandResult execute (ProjectCommandContext& context) override;
    CommandResult undo (ProjectCommandContext& context) override;

private:
    std::string trackId_;
    sequencing::AutomationLane lane_;
    std::optional<sequencing::AutomationLane> previousLane_;
};
}
