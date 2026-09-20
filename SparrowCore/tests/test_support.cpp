#include "JaguarCore/version.h"
#include "SparrowCore/version.h"

#include <iostream>

bool runRngTests(std::ostream& output);
bool runSamplingTests(std::ostream& output);
bool runLbfLossTests(std::ostream& output);
bool runSppLbfTests(std::ostream& output);
bool runBppLbfTests(std::ostream& output);

int main()
{
    const char* version = sparrow::version();
    if (version == nullptr || version[0] == '\0')
    {
        std::cerr << "SparrowCore version is empty\n";
        return 1;
    }
    const char* jaguarVersion = jaguar::version();
    if (jaguarVersion == nullptr || jaguarVersion[0] == '\0')
    {
        std::cerr << "JaguarCore version is empty\n";
        return 1;
    }

    if (!runRngTests(std::cerr))
    {
        return 1;
    }
    if (!runSamplingTests(std::cerr))
    {
        return 1;
    }
    if (!runLbfLossTests(std::cerr))
    {
        return 1;
    }
    if (!runSppLbfTests(std::cerr))
    {
        return 1;
    }
    return runBppLbfTests(std::cerr) ? 0 : 1;
}
