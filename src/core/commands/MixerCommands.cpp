#include "core/commands/MixerCommands.h"

#include "core/commands/ProjectCommandContext.h"
#include "core/sequencing/Project.h"
#include "core/sequencing/Track.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace tsq::core::commands
{
namespace
{
CommandResult failureFromException (const std::exception& exception)
{
    return CommandResult::failure (exception.what());
}

sequencing::Track& requireTrack (sequencing::Project& project, const std::string& trackId)
{
    auto* track = project.findTrackById (trackId);
    if (track == nullptr)
        throw std::invalid_argument ("Project does not contain the requested track");

    return *track;
}

CommandResult validateRouting (const sequencing::Project& project)
{
    const auto validation = sequencing::validateProjectRouting (project);
    if (validation.valid())
        return CommandResult::success();

    return CommandResult::failure (validation.summary());
}

CommandResult validateDeviceChain (const sequencing::Track& track, const sequencing::DeviceChain& chain)
{
    const auto errors = sequencing::validateDeviceChainForTrackType (track.type(), chain);
    if (errors.empty())
        return CommandResult::success();

    return CommandResult::failure (errors.front());
}

std::vector<sequencing::AutomationLane> removeAutomationLanesForDevice (sequencing::Track& track,
                                                                        const sequencing::DeviceSlotId& slotId,
                                                                        bool includeDeviceBypass)
{
    std::vector<sequencing::AutomationTarget> targetsToRemove;
    for (const auto& lane : track.automationLanes())
    {
        const auto& target = lane.target();
        if (target.deviceSlotId != slotId)
            continue;

        if (target.kind == sequencing::AutomationTargetKind::pluginParameter
            || (includeDeviceBypass && target.kind == sequencing::AutomationTargetKind::deviceBypass))
        {
            targetsToRemove.push_back (target);
        }
    }

    std::vector<sequencing::AutomationLane> removed;
    removed.reserve (targetsToRemove.size());
    for (const auto& target : targetsToRemove)
        removed.push_back (track.removeAutomationLane (target));

    return removed;
}

void restoreAutomationLanes (sequencing::Track& track, const std::vector<sequencing::AutomationLane>& lanes)
{
    for (const auto& lane : lanes)
        track.setAutomationLane (lane);
}
}

SetTrackMixerStripCommand::SetTrackMixerStripCommand (std::string trackId, sequencing::MixerStrip mixerStrip)
    : trackId_ (std::move (trackId)),
      mixerStrip_ (mixerStrip)
{
}

std::string SetTrackMixerStripCommand::name() const
{
    return "Set Track Mixer Strip";
}

CommandResult SetTrackMixerStripCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        previousMixerStrip_ = track.mixerStrip();
        track.setMixerStrip (mixerStrip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult SetTrackMixerStripCommand::undo (ProjectCommandContext& context)
{
    try
    {
        requireTrack (context.project(), trackId_).setMixerStrip (previousMixerStrip_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

SetTrackRoutingCommand::SetTrackRoutingCommand (std::string trackId, sequencing::TrackRouting routing)
    : trackId_ (std::move (trackId)),
      routing_ (std::move (routing))
{
}

std::string SetTrackRoutingCommand::name() const
{
    return "Set Track Routing";
}

CommandResult SetTrackRoutingCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        previousRouting_ = track.routing();
        track.setRouting (routing_);

        if (const auto validation = validateRouting (context.project()); validation.failed())
        {
            track.setRouting (previousRouting_);
            return validation;
        }

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult SetTrackRoutingCommand::undo (ProjectCommandContext& context)
{
    try
    {
        requireTrack (context.project(), trackId_).setRouting (previousRouting_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

SetTrackDeviceChainCommand::SetTrackDeviceChainCommand (std::string trackId, sequencing::DeviceChain deviceChain)
    : trackId_ (std::move (trackId)),
      deviceChain_ (std::move (deviceChain))
{
}

std::string SetTrackDeviceChainCommand::name() const
{
    return "Set Track Device Chain";
}

CommandResult SetTrackDeviceChainCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        previousDeviceChain_ = track.deviceChain();

        if (const auto validation = validateDeviceChain (track, deviceChain_); validation.failed())
            return validation;

        track.setDeviceChain (deviceChain_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult SetTrackDeviceChainCommand::undo (ProjectCommandContext& context)
{
    try
    {
        requireTrack (context.project(), trackId_).setDeviceChain (previousDeviceChain_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

AddTrackDeviceCommand::AddTrackDeviceCommand (std::string trackId, sequencing::DeviceSlot slot, std::optional<std::size_t> insertIndex)
    : trackId_ (std::move (trackId)),
      slot_ (std::move (slot)),
      insertIndex_ (insertIndex)
{
}

std::string AddTrackDeviceCommand::name() const
{
    return "Add Track Device";
}

CommandResult AddTrackDeviceCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        if (insertIndex_.has_value())
            chain.insertSlot (*insertIndex_, slot_);
        else
            chain.appendSlot (slot_);

        if (const auto validation = validateDeviceChain (track, chain); validation.failed())
            return validation;

        track.setDeviceChain (std::move (chain));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult AddTrackDeviceCommand::undo (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        chain.removeSlot (slot_.id());
        track.setDeviceChain (std::move (chain));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

RemoveTrackDeviceCommand::RemoveTrackDeviceCommand (std::string trackId, sequencing::DeviceSlotId slotId)
    : trackId_ (std::move (trackId)),
      slotId_ (std::move (slotId))
{
}

std::string RemoveTrackDeviceCommand::name() const
{
    return "Remove Track Device";
}

CommandResult RemoveTrackDeviceCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        removedAutomationLanes_.clear();
        auto chain = track.deviceChain();
        removedIndex_ = chain.indexOf (slotId_);
        removedSlot_ = chain.removeSlot (slotId_);
        track.setDeviceChain (std::move (chain));
        removedAutomationLanes_ = removeAutomationLanesForDevice (track, slotId_, true);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult RemoveTrackDeviceCommand::undo (ProjectCommandContext& context)
{
    try
    {
        if (! removedSlot_.has_value() || ! removedIndex_.has_value())
            return CommandResult::failure ("No removed device to restore");

        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        chain.insertSlot (*removedIndex_, *removedSlot_);
        track.setDeviceChain (std::move (chain));
        restoreAutomationLanes (track, removedAutomationLanes_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

ReplaceTrackDeviceCommand::ReplaceTrackDeviceCommand (std::string trackId, sequencing::DeviceSlot replacement)
    : trackId_ (std::move (trackId)),
      replacement_ (std::move (replacement))
{
}

std::string ReplaceTrackDeviceCommand::name() const
{
    return "Replace Track Device";
}

CommandResult ReplaceTrackDeviceCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        removedAutomationLanes_.clear();
        auto chain = track.deviceChain();
        const auto* previous = chain.findSlot (replacement_.id());
        if (previous == nullptr)
            return CommandResult::failure ("Device chain does not contain the requested slot");

        previousSlot_ = *previous;
        chain.replaceSlot (replacement_.id(), replacement_);

        if (const auto validation = validateDeviceChain (track, chain); validation.failed())
            return validation;

        track.setDeviceChain (std::move (chain));
        removedAutomationLanes_ = removeAutomationLanesForDevice (track, replacement_.id(), false);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult ReplaceTrackDeviceCommand::undo (ProjectCommandContext& context)
{
    try
    {
        if (! previousSlot_.has_value())
            return CommandResult::failure ("No previous device to restore");

        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        chain.replaceSlot (previousSlot_->id(), *previousSlot_);
        track.setDeviceChain (std::move (chain));
        restoreAutomationLanes (track, removedAutomationLanes_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

MoveTrackDeviceCommand::MoveTrackDeviceCommand (std::string trackId, sequencing::DeviceSlotId slotId, std::size_t targetIndex)
    : trackId_ (std::move (trackId)),
      slotId_ (std::move (slotId)),
      targetIndex_ (targetIndex)
{
}

std::string MoveTrackDeviceCommand::name() const
{
    return "Move Track Device";
}

CommandResult MoveTrackDeviceCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        previousIndex_ = chain.indexOf (slotId_);
        chain.moveSlot (slotId_, targetIndex_);

        if (const auto validation = validateDeviceChain (track, chain); validation.failed())
            return validation;

        track.setDeviceChain (std::move (chain));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult MoveTrackDeviceCommand::undo (ProjectCommandContext& context)
{
    try
    {
        if (! previousIndex_.has_value())
            return CommandResult::failure ("No previous device position to restore");

        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        chain.moveSlot (slotId_, *previousIndex_);
        track.setDeviceChain (std::move (chain));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

SetTrackDeviceBypassCommand::SetTrackDeviceBypassCommand (std::string trackId, sequencing::DeviceSlotId slotId, bool bypassed)
    : trackId_ (std::move (trackId)),
      slotId_ (std::move (slotId)),
      bypassed_ (bypassed)
{
}

std::string SetTrackDeviceBypassCommand::name() const
{
    return "Set Track Device Bypass";
}

CommandResult SetTrackDeviceBypassCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        const auto* previous = chain.findSlot (slotId_);
        if (previous == nullptr)
            return CommandResult::failure ("Device chain does not contain the requested slot");

        previousBypassed_ = previous->bypassed();
        chain.setSlotBypassed (slotId_, bypassed_);
        track.setDeviceChain (std::move (chain));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult SetTrackDeviceBypassCommand::undo (ProjectCommandContext& context)
{
    try
    {
        if (! previousBypassed_.has_value())
            return CommandResult::failure ("No previous device bypass state to restore");

        auto& track = requireTrack (context.project(), trackId_);
        auto chain = track.deviceChain();
        chain.setSlotBypassed (slotId_, *previousBypassed_);
        track.setDeviceChain (std::move (chain));
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

SetTrackAutomationLaneCommand::SetTrackAutomationLaneCommand (std::string trackId, sequencing::AutomationLane lane)
    : trackId_ (std::move (trackId)),
      lane_ (std::move (lane))
{
}

std::string SetTrackAutomationLaneCommand::name() const
{
    return "Set Track Automation Lane";
}

CommandResult SetTrackAutomationLaneCommand::execute (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (const auto* previous = track.findAutomationLane (lane_.target()))
            previousLane_ = *previous;

        track.setAutomationLane (lane_);
        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}

CommandResult SetTrackAutomationLaneCommand::undo (ProjectCommandContext& context)
{
    try
    {
        auto& track = requireTrack (context.project(), trackId_);
        if (previousLane_.has_value())
            track.setAutomationLane (*previousLane_);
        else
            track.removeAutomationLane (lane_.target());

        return CommandResult::success();
    }
    catch (const std::exception& exception)
    {
        return failureFromException (exception);
    }
}
}
