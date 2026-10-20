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

// Runs Optimize once and returns a malloc-owned buffer containing
// JSON\0SVG\0. The lengths exclude the trailing NUL bytes.
int SparrowOptimizeWithSvg(const char* inputJson,
                           std::size_t inputSize,
                           char** outputBuffer,
                           std::size_t* outputJsonSize,
                           std::size_t* outputSvgSize);

void SparrowFreeOutput(char* outputBuffer);
}
