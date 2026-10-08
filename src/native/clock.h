#pragma once
#include <QDateTime>
#include <QElapsedTimer>
#include <functional>

namespace csa {
inline std::function<qint64()> makeSteadyClock() {
    QElapsedTimer timer; timer.start();
    const qint64 base = QDateTime::currentMSecsSinceEpoch();
    return [timer, base] { return base + timer.elapsed(); };
}
} // namespace csa
