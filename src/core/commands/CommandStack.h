#pragma once

#include "core/commands/Command.h"
#include "core/commands/ProjectCommandContext.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tsq::core::commands
{
class CommandStack
{
public:
    explicit CommandStack (ProjectCommandContext& context) noexcept;

    CommandResult execute (std::unique_ptr<Command> command);
    CommandResult undo();
    CommandResult redo();
    CommandResult rollbackLastExecuted();

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    std::optional<std::string> nextUndoName() const;
    std::optional<std::string> nextRedoName() const;
    std::size_t undoDepth() const noexcept;
    std::size_t redoDepth() const noexcept;
    void clearHistory() noexcept;
    void setChangeCallback (std::function<void()> callback);
    void setChangeCallback (std::function<void(PlaybackSyncCategory)> callback);

private:
    void notifyChanged (PlaybackSyncCategory category);

    ProjectCommandContext& context_;
    std::vector<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
    std::function<void(PlaybackSyncCategory)> changeCallback_;
};
}
