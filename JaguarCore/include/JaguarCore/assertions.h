#pragma once

#include "JaguarCore/export.h"
#include "JaguarCore/result.h"

namespace jaguar
{
class Layout;
struct LayoutSnapshot;
class SPProblem;
struct SPSolution;
class BPProblem;
struct BPSolution;
class MSPProblem;
struct MSPSolution;

JAGUARCORE_API Result<void> validateLayout(const Layout& layout);
JAGUARCORE_API Result<void> validateLayoutSnapshot(
    const Layout& layout, const LayoutSnapshot& snapshot);
JAGUARCORE_API Result<void> validateSPSolution(
    const SPProblem& problem, const SPSolution& solution);
JAGUARCORE_API Result<void> validateBPSolution(
    const BPProblem& problem, const BPSolution& solution);
JAGUARCORE_API Result<void> validateMSPSolution(
    const MSPProblem& problem, const MSPSolution& solution);

JAGUARCORE_API void debugAssertLayout(const Layout& layout);
JAGUARCORE_API void debugAssertSPSolution(const SPProblem& problem);
JAGUARCORE_API void debugAssertBPSolution(const BPProblem& problem);
JAGUARCORE_API void debugAssertMSPSolution(const MSPProblem& problem);
}
