#pragma once
#include "types.h"
#include <QHash>
#include <QSet>

namespace csa {
void validateSettings(const Settings &settings);
class Policy {
public:
    explicit Policy(Settings settings = {});
    void arm(const Observation &observation, qint64 now);
    void cancel();
    void configure(const Settings &settings);
    bool evaluate(const Observation &observation, qint64 now);
    void consume();
    PolicyView view(qint64 now) const;
    const Settings &settings() const { return settings_; }
private:
    static bool fresh(const Observation &observation, qint64 now);
    Settings settings_;
    QString phase_, message_, revision_;
    qint64 phaseStartedAt_ = 0;
    QSet<QString> tracked_;
    QHash<QString, QString> observedTurns_;
    QHash<QString, qint64> terminalBaselines_;
    QVector<Blocker> blockers_;
};
} // namespace csa
