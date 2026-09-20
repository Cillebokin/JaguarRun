#pragma once

#if defined(_WIN32)
#  if defined(JAGUARCORE_BUILD)
#    define JAGUARCORE_API __declspec(dllexport)
#  else
#    define JAGUARCORE_API __declspec(dllimport)
#  endif
#else
#  define JAGUARCORE_API
#endif
