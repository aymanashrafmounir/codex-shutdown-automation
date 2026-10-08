#include "controller.h"
#include "clock.h"
#include <QTimeZone>
#include <stdexcept>

namespace csa {
Controller::Controller(Monitor &monitor, Store &store, Shutdown &shutdown, bool simulation,
                       std::function<qint64()> clock, QObject *parent)
    : QObject(parent), monitor_(monitor), store_(store), shutdown_(shutdown),
      clock_(clock ? std::move(clock) : makeSteadyClock()),
      policy_(store.settings()), simulation_(simulation) {
    lastHistoryPruneAt_ = clock_();
    store_.pruneHistory(lastHistoryPruneAt_);
    record("started", "Monitor started OFF. Previous authorization is never restored.");
    observation_ = monitor_.scan(clock_());
    timer_.setInterval(2000);
    connect(&timer_, &QTimer::timeout, this, &Controller::tick);
}
ViewState Controller::view() const {
    ViewState result;
    result.settings = policy_.settings(); result.monitor = observation_;
    result.policy = policy_.view(clock_()); result.error = error_;
    result.simulation = simulation_; result.startupEnabled = startupEnabled_;
    try { result.history = store_.history(); }
    catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
    return result;
}
void Controller::start() { timer_.start(); tick(); }
void Controller::record(const QString &type, const QString &message) {
    store_.append({{"timestamp", QDateTime::fromMSecsSinceEpoch(clock_(), QTimeZone::UTC).toString(Qt::ISODateWithMs)},
                   {"type", type}, {"message", message}, {"mode", simulation_ ? "simulation" : "live"}});
}
void Controller::fail(const std::exception &error) {
    policy_.cancel(); error_ = QString::fromUtf8(error.what());
    observation_.healthy = false;
    observation_.blockers.append(Blocker{"controller-error", error_});
}
bool Controller::arm() {
    if (busy_) return false;
    try {
        error_.clear(); observation_ = monitor_.scan(clock_());
        policy_.arm(observation_, clock_());
        if (!policy_.view(clock_()).armed) {
            record("authorization-expired", policy_.view(clock_()).message);
            emit changed(); return false;
        }
        record("armed", "One-time shutdown enabled for the current batch.");
    } catch (const std::exception &error) { fail(error); emit changed(); return false; }
    emit changed(); return policy_.view(clock_()).armed;
}
void Controller::cancel() {
    policy_.cancel();
    try { record("cancelled", "One-time authorization cancelled."); error_.clear(); }
    catch (const std::exception &error) { fail(error); }
    emit changed();
}
bool Controller::configure(const Settings &settings) {
    if (busy_) return false;
    policy_.cancel();
    try {
        policy_.configure(settings); store_.saveSettings(settings);
        record("settings-saved", "Settings saved. Shutdown remains OFF."); error_.clear();
    } catch (const std::exception &error) { fail(error); emit changed(); return false; }
    emit changed(); return true;
}
void Controller::tick() {
    if (busy_) return;
    busy_ = true;
    try {
        const auto now = clock_();
        if (now - lastHistoryPruneAt_ >= 60000) {
            store_.pruneHistory(now);
            lastHistoryPruneAt_ = now;
        }
        observation_ = monitor_.scan(clock_());
        const bool wasArmed = policy_.view(clock_()).armed;
        if (policy_.evaluate(observation_, clock_())) requestShutdown();
        else if (wasArmed && !policy_.view(clock_()).armed)
            record("authorization-expired", policy_.view(clock_()).message);
    } catch (const std::exception &error) { fail(error); }
    busy_ = false; emit changed();
}
void Controller::requestShutdown() {
    const QString readyRevision = observation_.revision;
    store_.saveSettings(policy_.settings());
    record("shutdown-checkpoint", "Settings and decision saved. Rechecking Codex before shutdown.");
    record("shutdown-requested", "Intent saved. Final verification must pass before execution.");
    observation_ = monitor_.verify(clock_());
    if (!policy_.evaluate(observation_, clock_()) || observation_.revision != readyRevision) {
        record("shutdown-deferred", "New activity or a blocker cancelled the shutdown request."); return;
    }
    policy_.consume();
    // No persistence, event-loop reentry, or other work between final scan and OS request.
    try { shutdown_.request(); }
    catch (const std::exception &error) {
        policy_.cancel();
        record("shutdown-failed", "Shutdown was not accepted: " + QString::fromUtf8(error.what()));
        throw;
    }
    policy_.cancel();
    record("shutdown-command-accepted", "Command accepted. Actual power-off is not verified.");
}
void Controller::setStartupEnabled(bool enabled) { startupEnabled_ = enabled; emit changed(); }
} // namespace csa
