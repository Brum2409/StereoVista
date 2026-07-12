#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Undo/redo core (Vulkan rewrite of the GL Engine::UndoManager).
//
// The GL version dragged in Loaders/ModelLoader.h and a whole Undo:: namespace
// of scene-specific helpers (recordModelEdit / deleteModel / ...) bound to the
// old global scene containers. That coupling is gone: this is the generic
// command-stack engine only. Scene-specific commands are built with
// LambdaUndoCommand (or a small UndoCommand subclass) at the call site — the
// tools and the SceneManager own their own capture/apply logic and keep this
// header free of any scene, GPU or GL dependency.
//
// Actions are recorded AFTER they are performed, so undo() reverts and redo()
// re-applies. The manager is owned by the Application (no global singleton) and
// handed to tools/plugins through the PluginContext.
// ============================================================================

namespace core {

class UndoCommand {
public:
    virtual ~UndoCommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual std::string description() const = 0;
};

// Generic command built from a pair of closures — the common case for property
// edits where before/after snapshots are cheap to capture.
class LambdaUndoCommand : public UndoCommand {
public:
    LambdaUndoCommand(std::string description, std::function<void()> undoFn,
                      std::function<void()> redoFn)
        : desc_(std::move(description)), undoFn_(std::move(undoFn)),
          redoFn_(std::move(redoFn)) {}

    void undo() override { if (undoFn_) undoFn_(); }
    void redo() override { if (redoFn_) redoFn_(); }
    std::string description() const override { return desc_; }

private:
    std::string desc_;
    std::function<void()> undoFn_;
    std::function<void()> redoFn_;
};

class UndoManager {
public:
    // Record an already-performed action. Clears the redo stack.
    void record(std::unique_ptr<UndoCommand> command);
    // Convenience: build + record a LambdaUndoCommand from two closures.
    void record(std::string description, std::function<void()> undoFn,
                std::function<void()> redoFn);

    bool undo();
    bool redo();

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    std::string undoDescription() const;
    std::string redoDescription() const;
    size_t undoCount() const { return undoStack_.size(); }
    size_t redoCount() const { return redoStack_.size(); }

    // ── History timeline (UI redesign Pass 3 §9) ─────────────────────────────
    // Human labels for the whole stack, oldest→newest (the applied/past edits)
    // and next-redo→furthest (the undone/future edits). The History panel
    // renders them as one timeline and jumps by calling undo()/redo() N times.
    std::vector<std::string> undoDescriptions() const;
    std::vector<std::string> redoDescriptions() const;

    // Last-saved marker: the app calls markSaved() after a successful scene
    // save; the History panel draws a "saved" divider at savedDepth() (the
    // undo-stack depth at that moment). hasSavedMark() is false until the first
    // save or after clear().
    void markSaved() { savedDepth_ = undoStack_.size(); hasSavedMark_ = true; }
    bool hasSavedMark() const { return hasSavedMark_; }
    size_t savedDepth() const { return savedDepth_; }

    // Drops all history. Call when a scene file is loaded (recorded indices
    // would no longer match) or when the referenced objects are destroyed.
    void clear();

    // Fired after any successful undo/redo so dependent systems (bounds,
    // selection, shadow/BVH rebuilds) can resynchronize.
    void setSceneChangedCallback(std::function<void()> callback) {
        sceneChanged_ = std::move(callback);
    }
    // Fired whenever the scene is mutated through the manager (record/undo/redo)
    // so the host can flag unsaved changes. NOT fired by clear().
    void setModifiedCallback(std::function<void()> callback) {
        modified_ = std::move(callback);
    }

private:
    static constexpr size_t kMaxUndoEntries = 128;

    std::vector<std::unique_ptr<UndoCommand>> undoStack_;
    std::vector<std::unique_ptr<UndoCommand>> redoStack_;
    std::function<void()> sceneChanged_;
    std::function<void()> modified_;
    size_t savedDepth_ = 0;
    bool hasSavedMark_ = false;
};

} // namespace core
