#pragma once

#include "JaguarCore/Export.h"
#include "JaguarCore/Result.h"

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

JAGUARCORE_API Result<void> ValidateLayout(const Layout& layout);
JAGUARCORE_API Result<void> ValidateLayoutSnapshot(
    const Layout& layout, const LayoutSnapshot& snapshot);
JAGUARCORE_API Result<void> ValidateSPSolution(
    const SPProblem& problem, const SPSolution& solution);
JAGUARCORE_API Result<void> ValidateBPSolution(
    const BPProblem& problem, const BPSolution& solution);
JAGUARCORE_API Result<void> ValidateMSPSolution(
    const MSPProblem& problem, const MSPSolution& solution);

JAGUARCORE_API void DebugAssertLayout(const Layout& layout);
JAGUARCORE_API void DebugAssertSPSolution(const SPProblem& problem);
JAGUARCORE_API void DebugAssertBPSolution(const BPProblem& problem);
JAGUARCORE_API void DebugAssertMSPSolution(const MSPProblem& problem);
}
