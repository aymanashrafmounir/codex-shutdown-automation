#pragma once
#include "types.h"
#include <QCryptographicHash>

namespace csa {
class SimulationMonitor final : public Monitor {
public:
    explicit SimulationMonitor(qint64 started) : started_(started) {}
    Observation scan(qint64 now) override {
        Observation result;
        result.healthy = true; result.capturedAt = now; result.source = "Synthetic local chats. Simulation only.";
        for (int index = 0; index < 2; ++index) {
            Thread thread;
            thread.id = QString::number(index); thread.turnId = "demo-turn";
            thread.title = index == 0 ? "Prepare changes" : "Run verification";
            thread.workPending = now - started_ < (index + 1) * 90000;
            thread.status = thread.workPending ? "running" : "completed";
            thread.turnStatus = thread.workPending ? "inProgress" : "completed";
            thread.reason = thread.workPending ? "Synthetic work in progress." : "Synthetic work completed.";
            thread.lastActivityAt = started_; result.threads.append(thread);
        }
        result.revision = result.threads[0].status + result.threads[1].status;
        return result;
    }
private:
    qint64 started_;
};
class RecordingShutdown final : public Shutdown {
public:
    void request() override { ++calls; }
    int calls = 0;
};
} // namespace csa
