#pragma once
#include "types.h"
#include "policy.h"
#include "store.h"
#include <QObject>
#include <QTimer>
#include <functional>

namespace csa {
class Controller : public QObject {
    Q_OBJECT
public:
    Controller(Monitor &monitor, Store &store, Shutdown &shutdown,
               bool simulation = false, std::function<qint64()> clock = {}, QObject *parent = nullptr);
    ViewState view() const;
    void start();
    bool arm();
    void cancel();
    bool configure(const Settings &settings);
    void tick();
    void setStartupEnabled(bool enabled);
signals:
    void changed();
private:
    void record(const QString &type, const QString &message);
    void requestShutdown();
    void fail(const std::exception &error);
    Monitor &monitor_;
    Store &store_;
    Shutdown &shutdown_;
    std::function<qint64()> clock_;
    Policy policy_;
    Observation observation_;
    QTimer timer_;
    QString error_;
    bool busy_ = false, simulation_ = false, startupEnabled_ = false;
};
} // namespace csa
