#include "core/commands/CommandStack.h"

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

    auto result = command->execute (context_);
    if (result.failed())
        return result;

    undoStack_.push_back (std::move (command));
    redoStack_.clear();
    notifyChanged();
    return CommandResult::success();
}

CommandResult CommandStack::undo()
{
    if (undoStack_.empty())
        return CommandResult::failure ("There is no command to undo");

    auto command = std::move (undoStack_.back());
    undoStack_.pop_back();

    auto result = command->undo (context_);
    if (result.failed())
    {
        undoStack_.push_back (std::move (command));
        return result;
    }

    redoStack_.push_back (std::move (command));
    notifyChanged();
    return CommandResult::success();
}

CommandResult CommandStack::redo()
{
    if (redoStack_.empty())
        return CommandResult::failure ("There is no command to redo");

    auto command = std::move (redoStack_.back());
    redoStack_.pop_back();

    auto result = command->execute (context_);
    if (result.failed())
    {
        redoStack_.push_back (std::move (command));
        return result;
    }

    undoStack_.push_back (std::move (command));
    notifyChanged();
    return CommandResult::success();
}

CommandResult CommandStack::rollbackLastExecuted()
{
    if (undoStack_.empty())
        return CommandResult::failure ("There is no command to roll back");

    auto command = std::move (undoStack_.back());
    undoStack_.pop_back();

    auto result = command->undo (context_);
    if (result.failed())
    {
        undoStack_.push_back (std::move (command));
        return result;
    }

    notifyChanged();
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
    changeCallback_ = std::move (callback);
}

void CommandStack::notifyChanged()
{
    if (changeCallback_)
        changeCallback_();
}
}
