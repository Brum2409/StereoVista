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

std::vector<std::string> UndoManager::undoDescriptions() const {
    std::vector<std::string> out;
    out.reserve(undoStack_.size());
    for (const std::unique_ptr<UndoCommand>& c : undoStack_) // oldest -> newest
        out.push_back(c->description());
    return out;
}

std::vector<std::string> UndoManager::redoDescriptions() const {
    std::vector<std::string> out;
    out.reserve(redoStack_.size());
    // redoStack_.back() is the next redo; walk back->front = next -> furthest.
    for (auto it = redoStack_.rbegin(); it != redoStack_.rend(); ++it)
        out.push_back((*it)->description());
    return out;
}

void UndoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
    hasSavedMark_ = false;
    savedDepth_ = 0;
}

} // namespace core
