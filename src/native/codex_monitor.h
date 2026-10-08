#pragma once
#include "types.h"

namespace csa {
class CodexMonitor final : public Monitor {
public:
    explicit CodexMonitor(QString codexHome);
    Observation scan(qint64 now) override;
private:
    QString codexHome_;
};
} // namespace csa
