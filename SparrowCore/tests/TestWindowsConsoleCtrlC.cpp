#include "SparrowCore/Optimizer/OptimizationTerminator.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif

bool RunImmediateStopOptimizeConsoleProbe(
    std::ostream& output,
    const std::function<bool(sparrow::OptimizationTerminator&)>& signalTrigger);

namespace
{
#ifdef _WIN32
class ScopedEventHandle final
{
public:
    explicit ScopedEventHandle(const char* name)
    {
        const std::wstring wideName(name, name + std::char_traits<char>::length(name));
        m_Handle = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE,
                              FALSE,
                              wideName.c_str());
    }

    ~ScopedEventHandle()
    {
        if (m_Handle != nullptr)
        {
            CloseHandle(m_Handle);
        }
    }

    ScopedEventHandle(const ScopedEventHandle&) = delete;
    ScopedEventHandle& operator=(const ScopedEventHandle&) = delete;

    bool IsValid() const noexcept
    {
        return m_Handle != nullptr;
    }

    bool Set() const noexcept
    {
        return m_Handle != nullptr && SetEvent(m_Handle) != FALSE;
    }

    bool Wait(std::chrono::milliseconds timeout) const noexcept
    {
        return m_Handle != nullptr
            && WaitForSingleObject(m_Handle, static_cast<DWORD>(timeout.count()))
                == WAIT_OBJECT_0;
    }

private:
    HANDLE m_Handle = nullptr;
};

bool WaitForCtrlCObservation(
    sparrow::OptimizationTerminator& terminator,
    std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (terminator.ShouldTerminate(0U))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return terminator.ShouldTerminate(0U);
}
#endif
}

bool RunWindowsConsoleCtrlCProbe(int argc, char** argv, std::ostream& output)
{
#ifndef _WIN32
    (void)argc;
    (void)argv;
    output << "Windows console Ctrl-C probes require Windows\n";
    return false;
#else
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "--windows-console-ctrlc-probe"
        && mode != "--windows-console-ctrlc-manual")
    {
        output << "usage: SparrowCoreTests --windows-console-ctrlc-probe "
                  "<ready-event> <release-event> <observed-event>\n"
                  "   or: SparrowCoreTests --windows-console-ctrlc-manual\n";
        return false;
    }

    const bool manual = mode == "--windows-console-ctrlc-manual";
    if ((!manual && argc != 5) || (manual && argc != 2))
    {
        output << "invalid Windows console Ctrl-C probe arguments\n";
        return false;
    }

    ScopedEventHandle ready(manual ? "" : argv[2]);
    ScopedEventHandle release(manual ? "" : argv[3]);
    ScopedEventHandle observed(manual ? "" : argv[4]);
    if (!manual && (!ready.IsValid() || !release.IsValid() || !observed.IsValid()))
    {
        output << "cannot open Windows console Ctrl-C handshake events\n";
        return false;
    }

    const auto trigger = [&](sparrow::OptimizationTerminator& terminator) {
        if (manual)
        {
            output << "READY: press Ctrl+C in this console now\n";
            output.flush();
        }
        else
        {
            if (!ready.Set())
            {
                output << "cannot signal probe readiness\n";
                return false;
            }
            if (!release.Wait(std::chrono::seconds{10}))
            {
                output << "launcher did not release the probe callback\n";
                return false;
            }
        }

        const auto timeout = manual
            ? std::chrono::seconds{60}
            : std::chrono::seconds{5};
        const bool observedCtrlC = WaitForCtrlCObservation(terminator, timeout);
        if (!observedCtrlC)
        {
            output << "probe callback did not observe a Ctrl-C stop request\n";
            return false;
        }
        output << "WINDOWS_CTRL_C_SIGNAL_OBSERVED\n";
        output.flush();
        if (!manual && !observed.Set())
        {
            output << "cannot signal Ctrl-C observation to launcher\n";
            return false;
        }
        return true;
    };

    const bool passed = RunImmediateStopOptimizeConsoleProbe(output, trigger);
    if (passed)
    {
        output << "WINDOWS_CTRL_C_PROBE_PASS\n";
    }
    return passed;
#endif
}
