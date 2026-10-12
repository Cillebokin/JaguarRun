#include "OptimizationSignalHandler.h"

#include "SparrowCore/Optimizer/OptimizationTerminator.h"

#include <atomic>

namespace
{
std::atomic<bool> g_CtrlCRequested{false};

static_assert(std::atomic<bool>::is_always_lock_free,
              "Ctrl-C handling requires a lock-free atomic bool");

void HandleCtrlC(int) noexcept
{
    g_CtrlCRequested.store(true, std::memory_order_relaxed);
}
}

namespace sparrow::cli
{
ScopedCtrlCHandler::ScopedCtrlCHandler(
    OptimizationTerminator& terminator) noexcept
    : m_Terminator(terminator)
{
    g_CtrlCRequested.store(false, std::memory_order_relaxed);
    m_Terminator.SetExternalPhaseStopFlag(&g_CtrlCRequested);

    m_PreviousHandler = std::signal(SIGINT, HandleCtrlC);
    if (m_PreviousHandler == SIG_ERR)
    {
        m_Terminator.SetExternalPhaseStopFlag(nullptr);
        return;
    }
    m_Installed = true;
}

ScopedCtrlCHandler::~ScopedCtrlCHandler()
{
    if (!m_Installed)
    {
        return;
    }
    std::signal(SIGINT, m_PreviousHandler);
    m_Terminator.SetExternalPhaseStopFlag(nullptr);
}

bool ScopedCtrlCHandler::IsInstalled() const noexcept
{
    return m_Installed;
}
}
