#pragma once
#include "types.h"
#include <QMutex>
#include <QWaitCondition>
#include <functional>
#include <memory>

class QThread;
namespace csa {
class BackgroundMonitor final : public Monitor {
public:
    explicit BackgroundMonitor(std::unique_ptr<Monitor> source, std::function<qint64()> clock = {});
    ~BackgroundMonitor() override;
    Observation scan(qint64 now) override;
    Observation verify(qint64 now) override;
private:
    Observation readSource(qint64 now);
    void run();
    std::unique_ptr<Monitor> source_;
    std::function<qint64()> clock_;
    QThread *worker_ = nullptr;
    QMutex sourceMutex_, cacheMutex_, waitMutex_;
    QWaitCondition wake_;
    Observation cached_;
};
} // namespace csa
