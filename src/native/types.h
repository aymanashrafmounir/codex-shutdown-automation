#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace csa {
struct Settings {
    int settleSeconds = 120;
    int countdownSeconds = 60;
    QString language = "en";
};
struct Blocker { QString code; QString message; };
struct Thread {
    QString id, title, status, turnStatus, turnId, reason;
    qint64 lastActivityAt = 0;
    qint64 terminalOrdinal = -1;
    bool workPending = false;
};
struct Observation {
    bool healthy = false;
    qint64 capturedAt = 0;
    QString revision, source;
    QVector<Thread> threads;
    QVector<Blocker> blockers;
};
struct PolicyView {
    QString phase = "disarmed", message;
    bool armed = false;
    int remainingSeconds = -1, trackedCount = 0;
    QVector<Blocker> blockers;
};
struct ViewState {
    Settings settings;
    Observation monitor;
    PolicyView policy;
    QVector<QJsonObject> history;
    QString error;
    bool simulation = false, startupEnabled = false;
};
class Monitor {
public:
    virtual ~Monitor() = default;
    virtual Observation scan(qint64 now) = 0;
    // A final verification must read the source, never reuse a background cache.
    virtual Observation verify(qint64 now) { return scan(now); }
};
class Shutdown {
public:
    virtual ~Shutdown() = default;
    virtual void request() = 0;
};
} // namespace csa
