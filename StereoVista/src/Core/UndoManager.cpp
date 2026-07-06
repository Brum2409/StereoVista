#include "Core/UndoManager.h"

namespace core {

void UndoManager::record(std::unique_ptr<UndoCommand> command) {
    if (!command)
        return;
    redoStack_.clear();
    undoStack_.push_back(std::move(command));
    // Bound the history: drop the oldest entry once the cap is exceeded.
    if (undoStack_.size() > kMaxUndoEntries)
        undoStack_.erase(undoStack_.begin());
    if (modified_)
        modified_();
}

void UndoManager::record(std::string description, std::function<void()> undoFn,
                         std::function<void()> redoFn) {
    record(std::make_unique<LambdaUndoCommand>(std::move(description),
                                               std::move(undoFn), std::move(redoFn)));
}

bool UndoManager::undo() {
    if (undoStack_.empty())
        return false;
    std::unique_ptr<UndoCommand> command = std::move(undoStack_.back());
    undoStack_.pop_back();
    command->undo();
    redoStack_.push_back(std::move(command));
    if (sceneChanged_)
        sceneChanged_();
    if (modified_)
        modified_();
    return true;
}

bool UndoManager::redo() {
    if (redoStack_.empty())
        return false;
    std::unique_ptr<UndoCommand> command = std::move(redoStack_.back());
    redoStack_.pop_back();
    command->redo();
    undoStack_.push_back(std::move(command));
    if (sceneChanged_)
        sceneChanged_();
    if (modified_)
        modified_();
    return true;
}

std::string UndoManager::undoDescription() const {
    return undoStack_.empty() ? std::string() : undoStack_.back()->description();
}

std::string UndoManager::redoDescription() const {
    return redoStack_.empty() ? std::string() : redoStack_.back()->description();
}

void UndoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace core
