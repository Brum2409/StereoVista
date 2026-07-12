#include "Core/CommandRegistry.h"

#include <chrono>
#include <cmath>

namespace core {

namespace {

int64_t nowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Frecency decay: a use's weight halves every two weeks.
constexpr double kHalfLifeSeconds = 14.0 * 24.0 * 3600.0;

double decayedScore(const CommandRegistry::FrecencyEntry& entry, int64_t now) {
    if (entry.score <= 0.0)
        return 0.0;
    const double age = double(now - entry.lastUse);
    if (age <= 0.0)
        return entry.score;
    return entry.score * std::pow(0.5, age / kHalfLifeSeconds);
}

} // namespace

void CommandRegistry::add(Command command) {
    auto it = index_.find(command.id);
    if (it != index_.end()) {
        commands_[it->second] = std::move(command); // re-register in place
        return;
    }
    index_.emplace(command.id, commands_.size());
    commands_.push_back(std::move(command));
}

bool CommandRegistry::run(const std::string& id) {
    const auto it = index_.find(id);
    if (it == index_.end())
        return false;
    const Command& command = commands_[it->second];
    if (!command.action || !isEnabled(command))
        return false;

    // Record the use before running: decay the stored score to now, add one.
    const int64_t now = nowUnixSeconds();
    FrecencyEntry& entry = frecency_[id];
    entry.score = decayedScore(entry, now) + 1.0;
    entry.lastUse = now;

    command.action();
    return true;
}

const Command* CommandRegistry::find(const std::string& id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &commands_[it->second];
}

void CommandRegistry::forEachInCategory(
    const std::string& category,
    const std::function<void(const Command&)>& fn) const {
    for (const Command& command : commands_)
        if (command.category == category)
            fn(command);
}

double CommandRegistry::frecency(const std::string& id) const {
    const auto it = frecency_.find(id);
    if (it == frecency_.end())
        return 0.0;
    return decayedScore(it->second, nowUnixSeconds());
}

} // namespace core
