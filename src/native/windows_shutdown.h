#pragma once
#include "types.h"
namespace csa {
class WindowsShutdown final : public Shutdown {
public:
    void request() override;
};
} // namespace csa
