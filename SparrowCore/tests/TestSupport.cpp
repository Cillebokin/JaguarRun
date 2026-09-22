#include "JaguarCore/Version.h"
#include "SparrowCore/Version.h"

#include <iostream>

bool RunRngTests(std::ostream& output);
bool RunSamplingTests(std::ostream& output);
bool RunLbfLossTests(std::ostream& output);
bool RunSppLbfTests(std::ostream& output);
bool RunBppLbfTests(std::ostream& output);
bool RunQuantifyTests(std::ostream& output);
bool RunSeparatorTests(std::ostream& output);
bool RunExplorationTests(std::ostream& output);

int main()
{
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
    return RunExplorationTests(std::cerr) ? 0 : 1;
}
