#include "policy.h"
#include <stdexcept>
#include <cmath>

namespace csa {
void validateSettings(const Settings &settings) {
    if (settings.settleSeconds < 60 || settings.settleSeconds > 3600
        || settings.countdownSeconds < 60 || settings.countdownSeconds > 3600
        || (settings.language != "en" && settings.language != "ar"))
        throw std::runtime_error("Invalid settings. Timings must be 60 to 3600 seconds.");
}
Policy::Policy(Settings settings) : settings_(settings) { validateSettings(settings); cancel(); }
void Policy::cancel() {
    phase_ = "disarmed";
    phaseStartedAt_ = 0;
    revision_.clear(); tracked_.clear(); observedTurns_.clear(); terminalBaselines_.clear(); blockers_.clear();
    message_ = "Shutdown is OFF. Enable once for your current batch.";
}
bool Policy::fresh(const Observation &observation, qint64 now) {
    return observation.capturedAt > 0 && now - observation.capturedAt >= -1000
        && now - observation.capturedAt <= 10000;
}
void Policy::arm(const Observation &observation, qint64 now) {
    if (phase_ != "disarmed") throw std::runtime_error("Shutdown is already enabled for this batch.");
    if (!observation.healthy || !fresh(observation, now))
        throw std::runtime_error("Reliable monitoring is unavailable. Resolve the monitor problem first.");
    for (const auto &thread : observation.threads) {
        if (thread.workPending || thread.status == "running") tracked_.insert(thread.id);
        observedTurns_.insert(thread.id, thread.turnId);
        terminalBaselines_.insert(thread.id, thread.terminalOrdinal);
    }
    if (tracked_.isEmpty()) throw std::runtime_error("Start Codex work before enabling shutdown.");
    phase_ = "waiting";
    message_ = "Enabled once. Waiting for all local Codex work to finish.";
    evaluate(observation, now);
}
void Policy::configure(const Settings &settings) {
    validateSettings(settings); settings_ = settings; cancel();
}
bool Policy::evaluate(const Observation &observation, qint64 now) {
    if (phase_ == "disarmed" || phase_ == "requesting") return false;
    for (const auto &thread : observation.threads) {
        const bool newTurn = !observedTurns_.contains(thread.id)
            || observedTurns_.value(thread.id) != thread.turnId;
        const bool newTerminal = thread.terminalOrdinal > terminalBaselines_.value(thread.id, -1);
        if (thread.workPending || thread.status == "running" || newTurn || newTerminal) tracked_.insert(thread.id);
        observedTurns_.insert(thread.id, thread.turnId);
        const bool terminal = newTerminal || thread.turnStatus == "failed" || thread.turnStatus == "interrupted"
            || thread.status == "failed" || (thread.status == "blocked" && !thread.workPending);
        if (tracked_.contains(thread.id) && terminal) {
            cancel();
            message_ = "A monitored chat failed or was interrupted. Resolve it, then Enable a new batch.";
            blockers_.append(Blocker{"authorization-expired", thread.title + ": " + thread.reason});
            return false;
        }
    }
    blockers_ = observation.blockers;
    if (!observation.healthy) blockers_.append(Blocker{"monitor-unavailable", "Reliable monitoring is unavailable."});
    if (!fresh(observation, now)) blockers_.append(Blocker{"monitor-stale", "Observation is stale. Shutdown is blocked."});
    QSet<QString> present;
    for (const auto &thread : observation.threads) {
        present.insert(thread.id);
        if (thread.status != "completed" && (tracked_.contains(thread.id)
            || thread.status == "running" || thread.status == "unknown"))
            blockers_.append(Blocker{"thread-" + thread.status, thread.title + ": " + thread.reason});
    }
    for (const auto &id : tracked_) if (!present.contains(id))
        blockers_.append(Blocker{"missing-thread", "A monitored chat disappeared before verified completion."});
    if (!blockers_.isEmpty()) {
        phase_ = "waiting"; phaseStartedAt_ = 0; revision_.clear();
        message_ = "Waiting. The computer stays on while work or a blocker remains.";
        return false;
    }
    if (phase_ == "waiting" || revision_ != observation.revision) {
        phase_ = "settling"; phaseStartedAt_ = now; revision_ = observation.revision;
        message_ = "All monitored chats finished. Checking that saved state stays quiet.";
    }
    if (phase_ == "settling" && now - phaseStartedAt_ >= settings_.settleSeconds * 1000LL) {
        phase_ = "countdown"; phaseStartedAt_ = now;
        message_ = "Ready to shut down. Cancel now to keep this computer on.";
    }
    return phase_ == "countdown" && now - phaseStartedAt_ >= settings_.countdownSeconds * 1000LL;
}
void Policy::consume() {
    phase_ = "requesting";
    message_ = "Authorization consumed. Requesting a normal Windows shutdown.";
}
PolicyView Policy::view(qint64 now) const {
    PolicyView result;
    result.phase = phase_; result.message = message_; result.blockers = blockers_;
    result.trackedCount = tracked_.size();
    result.armed = phase_ == "waiting" || phase_ == "settling" || phase_ == "countdown";
    if (phase_ == "settling" || phase_ == "countdown") {
        const int duration = phase_ == "settling" ? settings_.settleSeconds : settings_.countdownSeconds;
        result.remainingSeconds = qMax(0, int(std::ceil(duration - (now - phaseStartedAt_) / 1000.0)));
    }
    return result;
}
} // namespace csa
