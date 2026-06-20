#include "core/commands/CommandStack.h"

#include "core/diagnostics/PerformanceTrace.h"

namespace tsq::core::commands
{
CommandStack::CommandStack (ProjectCommandContext& context) noexcept
    : context_ (context)
{
}

CommandResult CommandStack::execute (std::unique_ptr<Command> command)
{
    if (command == nullptr)
        return CommandResult::failure ("Cannot execute a null command");

    const auto commandName = command->name();
    const auto category = command->playbackSyncCategory();
    core::diagnostics::ScopedPerformanceTimer timer { "CommandStack::execute ", commandName };
    auto result = command->execute (context_);
    if (result.failed())
        return result;

    undoStack_.push_back (std::move (command));
    redoStack_.clear();
    notifyChanged (category);
    return CommandResult::success();
}

CommandResult CommandStack::undo()
{
    if (undoStack_.empty())
        return CommandResult::failure ("There is no command to undo");

    auto command = std::move (undoStack_.back());
    undoStack_.pop_back();

    const auto commandName = command->name();
    const auto category = command->playbackSyncCategory();
    core::diagnostics::ScopedPerformanceTimer timer { "CommandStack::undo ", commandName };
    auto result = command->undo (context_);
    if (result.failed())
    {
        undoStack_.push_back (std::move (command));
        return result;
    }

    redoStack_.push_back (std::move (command));
    notifyChanged (category);
    return CommandResult::success();
}

CommandResult CommandStack::redo()
{
    if (redoStack_.empty())
        return CommandResult::failure ("There is no command to redo");

    auto command = std::move (redoStack_.back());
    redoStack_.pop_back();

    const auto commandName = command->name();
    const auto category = command->playbackSyncCategory();
    core::diagnostics::ScopedPerformanceTimer timer { "CommandStack::redo ", commandName };
    auto result = command->execute (context_);
    if (result.failed())
    {
        redoStack_.push_back (std::move (command));
        return result;
    }

    undoStack_.push_back (std::move (command));
    notifyChanged (category);
    return CommandResult::success();
}

CommandResult CommandStack::rollbackLastExecuted()
{
    if (undoStack_.empty())
        return CommandResult::failure ("There is no command to roll back");

    auto command = std::move (undoStack_.back());
    undoStack_.pop_back();

    const auto commandName = command->name();
    const auto category = command->playbackSyncCategory();
    core::diagnostics::ScopedPerformanceTimer timer { "CommandStack::rollbackLastExecuted ", commandName };
    auto result = command->undo (context_);
    if (result.failed())
    {
        undoStack_.push_back (std::move (command));
        return result;
    }

    notifyChanged (category);
    return CommandResult::success();
}

bool CommandStack::canUndo() const noexcept
{
    return ! undoStack_.empty();
}

bool CommandStack::canRedo() const noexcept
{
    return ! redoStack_.empty();
}

std::optional<std::string> CommandStack::nextUndoName() const
{
    if (undoStack_.empty())
        return std::nullopt;

    return undoStack_.back()->name();
}

std::optional<std::string> CommandStack::nextRedoName() const
{
    if (redoStack_.empty())
        return std::nullopt;

    return redoStack_.back()->name();
}

std::size_t CommandStack::undoDepth() const noexcept
{
    return undoStack_.size();
}

std::size_t CommandStack::redoDepth() const noexcept
{
    return redoStack_.size();
}

void CommandStack::clearHistory() noexcept
{
    undoStack_.clear();
    redoStack_.clear();
}

void CommandStack::setChangeCallback (std::function<void()> callback)
{
    if (! callback)
    {
        changeCallback_ = {};
        return;
    }

    changeCallback_ = [callback = std::move (callback)] (PlaybackSyncCategory)
    {
        callback();
    };
}

void CommandStack::setChangeCallback (std::function<void(PlaybackSyncCategory)> callback)
{
    changeCallback_ = std::move (callback);
}

void CommandStack::notifyChanged (PlaybackSyncCategory category)
{
    core::diagnostics::ScopedPerformanceTimer timer { "CommandStack::notifyChanged" };

    if (core::diagnostics::performanceTraceEnabled())
    {
        core::diagnostics::writePerformanceTrace (
            std::string { "CommandStack::change category=" } + playbackSyncCategoryLabel (category),
            0);
    }

    if (changeCallback_)
        changeCallback_ (category);
}
}
