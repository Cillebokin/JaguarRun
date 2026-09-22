#pragma once

#include "JaguarCore/Export.h"

#include <cstdint>

namespace jaguar
{
JAGUARCORE_API const char* Version() noexcept;
}

// Stable loader/deployment probe. C++ API symbols remain compiler-specific;
// this entry point is intentionally C-linkage and returns the same string.
extern "C" JAGUARCORE_API const char* JaguarCoreVersion() noexcept;

extern "C" JAGUARCORE_API std::uint32_t JaguarCoreApiVersion() noexcept;
