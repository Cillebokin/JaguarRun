#include "JaguarCore/Version.h"
#include "SparrowCore/Version.h"

#include <iostream>
#include <string_view>

bool RunRngTests(std::ostream& output);
bool RunSamplingTests(std::ostream& output);
bool RunLbfLossTests(std::ostream& output);
bool RunSppLbfTests(std::ostream& output);
bool RunBppLbfTests(std::ostream& output);
bool RunQuantifyTests(std::ostream& output);
bool RunSeparatorTests(std::ostream& output);
bool RunWorkerLossSelectorTests(std::ostream& output);
bool RunE5FixedNonFiniteWorkerRounds(std::ostream& output);
bool RunExplorationTests(std::ostream& output);
bool RunE5NaturalTimeAcceptance(std::ostream& output);
bool RunWasmTests(std::ostream& output);
bool RunWindowsConsoleCtrlCProbe(int argc, char** argv, std::ostream& output);

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--e5-natural-time")
    {
        return RunE5NaturalTimeAcceptance(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--worker-loss-selector")
    {
        return RunWorkerLossSelectorTests(std::cerr) ? 0 : 1;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--e5-fixed-nonfinite-worker-rounds")
    {
        return RunE5FixedNonFiniteWorkerRounds(std::cerr) ? 0 : 1;
    }
    if (argc > 1)
    {
        return RunWindowsConsoleCtrlCProbe(argc, argv, std::cerr) ? 0 : 1;
    }

    const char* version = sparrow::Version();
    if (version == nullptr || version[0] == '\0')
    {
        std::cerr << "SparrowCore version is empty\n";
        return 1;
    }
    const char* jaguarVersion = jaguar::Version();
    if (jaguarVersion == nullptr || jaguarVersion[0] == '\0')
    {
        std::cerr << "JaguarCore version is empty\n";
        return 1;
    }

    if (!RunRngTests(std::cerr))
    {
        return 1;
    }
    if (!RunSamplingTests(std::cerr))
    {
        return 1;
    }
    if (!RunLbfLossTests(std::cerr))
    {
        return 1;
    }
    if (!RunSppLbfTests(std::cerr))
    {
        return 1;
    }
    if (!RunBppLbfTests(std::cerr))
    {
        return 1;
    }
    if (!RunQuantifyTests(std::cerr))
    {
        return 1;
    }
    if (!RunSeparatorTests(std::cerr))
    {
        return 1;
    }
    if (!RunExplorationTests(std::cerr))
    {
        return 1;
    }
    return RunWasmTests(std::cerr) ? 0 : 1;
}
