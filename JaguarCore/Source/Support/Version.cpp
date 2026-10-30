#include "JaguarCore/Version.h"

namespace jaguar
{
const char* Version() noexcept
{
    return "0.0.1.1030_Beta";
}
}

extern "C" JAGUARCORE_API const char* JaguarCoreVersion() noexcept
{
    return jaguar::Version();
}

extern "C" JAGUARCORE_API std::uint32_t JaguarCoreApiVersion() noexcept
{
    return 2U;
}
