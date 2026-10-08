#include "background_monitor.h"
#include "clock.h"
#include <QMutexLocker>
#include <QThread>
#include <stdexcept>
#include <utility>

namespace csa {
BackgroundMonitor::BackgroundMonitor(std::unique_ptr<Monitor> source, std::function<qint64()> clock)
    : source_(std::move(source)), clock_(clock ? std::move(clock) : makeSteadyClock()) {
    if (!source_) throw std::invalid_argument("A monitor source is required.");
    cached_.source = "Codex local SQLite stores (initializing)";
    cached_.revision = "monitor-initializing";
    cached_.blockers.append(Blocker{"MONITOR_INITIALIZING", "Waiting for the first Codex observation."});
    worker_ = QThread::create([this] { run(); });
    worker_->start();
}

BackgroundMonitor::~BackgroundMonitor() {
    worker_->requestInterruption();
    {
        QMutexLocker lock(&waitMutex_);
        wake_.wakeAll();
    }
    worker_->wait();
    delete worker_;
    // source_ remains alive until the worker has exited and released its locks.
}

Observation BackgroundMonitor::scan(qint64 now) {
    Q_UNUSED(now);
    QMutexLocker lock(&cacheMutex_);
    return cached_; // Preserve source capturedAt so stale data cannot appear fresh.
}

Observation BackgroundMonitor::verify(qint64 now) { return readSource(now); }

Observation BackgroundMonitor::readSource(qint64 now) {
    QMutexLocker lock(&sourceMutex_);
    try {
        return source_->scan(now);
    } catch (...) {
        Observation failure;
        failure.capturedAt = now;
        failure.source = "Codex local SQLite stores (unavailable)";
        failure.revision = "monitor-read-failed";
        failure.blockers.append(Blocker{"MONITOR_READ_FAILED", "Codex monitoring failed. Shutdown is blocked."});
        return failure;
    }
}

void BackgroundMonitor::run() {
    while (!QThread::currentThread()->isInterruptionRequested()) {
        auto observation = readSource(clock_());
        {
            QMutexLocker lock(&cacheMutex_);
            cached_ = std::move(observation);
        }
        QMutexLocker lock(&waitMutex_);
        if (!QThread::currentThread()->isInterruptionRequested()) wake_.wait(&waitMutex_, 2000);
    }
}
} // namespace csa
