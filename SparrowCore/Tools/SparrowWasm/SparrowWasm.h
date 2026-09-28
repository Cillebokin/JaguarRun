#pragma once

#include <cstddef>

extern "C"
{
int SparrowOptimizeJson(const char* inputJson,
                        std::size_t inputSize,
                        char* outputJson,
                        std::size_t outputCapacity);

int SparrowOptimizeSvg(const char* inputJson,
                       std::size_t inputSize,
                       char* outputSvg,
                       std::size_t outputCapacity);
}
