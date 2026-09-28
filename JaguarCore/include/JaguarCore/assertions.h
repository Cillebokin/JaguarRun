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

// 验证布局和问题解的状态

// 验证布局当前状态
JAGUARCORE_API Result<void> ValidateLayout(const Layout& layout);


// 验证布局与快照是否一致
JAGUARCORE_API Result<void> ValidateLayoutSnapshot(const Layout& layout,
                                                   const LayoutSnapshot& snapshot);


// 验证 SPP 解与当前问题状态是否一致
JAGUARCORE_API Result<void> ValidateSPSolution(const SPProblem& problem,
                                               const SPSolution& solution);


// 验证 BPP 解与当前问题状态是否一致
JAGUARCORE_API Result<void> ValidateBPSolution(const BPProblem& problem,
                                               const BPSolution& solution);


// 验证 MSP 解与当前问题状态是否一致
JAGUARCORE_API Result<void> ValidateMSPSolution(const MSPProblem& problem,
                                                const MSPSolution& solution);


// 调试模式下断言布局有效
JAGUARCORE_API void DebugAssertLayout(const Layout& layout);
// 调试模式下断言 SPP 解有效
JAGUARCORE_API void DebugAssertSPSolution(const SPProblem& problem);
// 调试模式下断言 BPP 解有效
JAGUARCORE_API void DebugAssertBPSolution(const BPProblem& problem);
// 调试模式下断言 MSP 解有效
JAGUARCORE_API void DebugAssertMSPSolution(const MSPProblem& problem);
}
