#pragma once
#include "types.h"

namespace csa {
class Store {
public:
    explicit Store(QString directory);
    Settings settings() const;
    void saveSettings(const Settings &settings);
    void append(const QJsonObject &decision);
    QVector<QJsonObject> history() const;
    QString directory() const { return directory_; }
private:
    QString directory_;
};
} // namespace csa
