#pragma once

#include <csignal>

namespace sparrow
{
class OptimizationTerminator;

namespace cli
{
class ScopedCtrlCHandler
{
public:
    explicit ScopedCtrlCHandler(OptimizationTerminator& terminator) noexcept;
    ~ScopedCtrlCHandler();

    ScopedCtrlCHandler(const ScopedCtrlCHandler&) = delete;
    ScopedCtrlCHandler& operator=(const ScopedCtrlCHandler&) = delete;
    ScopedCtrlCHandler(ScopedCtrlCHandler&&) = delete;
    ScopedCtrlCHandler& operator=(ScopedCtrlCHandler&&) = delete;

    bool IsInstalled() const noexcept;

private:
    OptimizationTerminator& m_Terminator;
    void (*m_PreviousHandler)(int) = SIG_DFL;
    bool m_Installed = false;
};
}
}
