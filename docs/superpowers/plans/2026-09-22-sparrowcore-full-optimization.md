# SparrowCore Full Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 将 SparrowCore 从 LBF 初始排料模块扩展为完整的 Sparrow 两阶段优化器，并按《代码规范.docx》完成源码和构建结构规范化。

**Architecture:** SparrowCore 只依赖 JaguarCore 公共头文件；Separator 持有单任务状态，Worker 只生成候选并返回值，Exploration 和 Compression 通过稳定的优化监听/终止接口驱动。CLI、WASM 和浏览器 Demo 只调用 SparrowCore 公共接口。

**Tech Stack:** C++17、Qt 5.12 qmake（仅构建编排，不引入 Qt 运行时）、MSVC2019 x64、JaguarCore public API、Emscripten/JSON WASM 边界。

## Global Constraints

- 不修改 NestLab 生产调用和 JaguarCore 几何算法。
- 不创建 Git commit；每个阶段只保留工作区修改和验证结果。
- 所有源码目录、模块目录和文件名首字母大写。
- 所有函数接口使用 UpperCamelCase；命名空间使用全小写。
- 不使用 `using namespace`、`try/catch`、全局可变变量或 Qt 运行时依赖。
- 公共错误使用 `Result<T>`；输入和不可行状态不能通过异常表达。
- 固定 seed、指标、需求/库存守恒和 JaguarCore validator 是每个优化阶段的验收条件。

### Task 1: 恢复基线并完成目录/API 命名迁移

**Files:**
- Modify: `SparrowCore/SparrowCore.pro`
- Modify: `SparrowCore/Tests/SparrowCoreTests.pro`
- Modify: `SparrowCore/Bench/SparrowCoreBench.pro`
- Rename: `SparrowCore/include` → `SparrowCore/Include`
- Rename: `SparrowCore/src` → `SparrowCore/Source`
- Rename: `SparrowCore/tests` → `SparrowCore/Tests`
- Rename: `SparrowCore/bench` → `SparrowCore/Bench`
- Rename: module directories and all source/header files to UpperCamelCase
- Modify: existing SparrowCore headers, sources and tests for UpperCamelCase functions, enum values, includes and member names

- [ ] 先在独立构建目录复现现有测试的 Windows loader 错误，记录 `0xC0000139` 与缺失/冲突 DLL。
- [ ] 使用临时目录名完成 Windows 大小写不敏感文件夹迁移，保证源码目录不出现重复路径。
- [ ] 更新 qmake 的 `HEADERS`、`SOURCES`、`INCLUDEPATH`、库路径和所有 include。
- [ ] 将 `solveSppLbf`、`solveBppLbf`、`searchLbf`、`version` 等函数改为 `SolveSppLbf`、`SolveBppLbf`、`SearchLbf`、`Version`。
- [ ] 将 `SampleKind` 枚举值改为全大写下划线形式，并将 bool 状态命名为 `is...`/`has...`/`can...`。
- [ ] 运行 `rg` 检查没有 `using namespace`、`try`/`catch`、旧小写公共路径和旧函数名。
- [ ] 重新生成 Debug/Release 工程并运行基线测试；若运行时仍失败，只记录具体 DLL 边界，不在算法代码中掩盖它。

### Task 2: 完成 Sparrow 风格 LBFBuilder

**Files:**
- Create: `SparrowCore/Include/SparrowCore/Optimizer/SparrowLbfBuilder.h`
- Create: `SparrowCore/Source/Optimizer/SparrowLbfBuilder.cpp`
- Create: `SparrowCore/Include/SparrowCore/Eval/SparrowLbfEvaluator.h`
- Create: `SparrowCore/Source/Eval/SparrowLbfEvaluator.cpp`
- Modify: `SparrowCore/Include/SparrowCore/LbfConfig.h`
- Modify: `SparrowCore/Tests/TestSppLbf.cpp`

**Interface:**

```cpp
struct SparrowConstructorConfig
{
    std::uint64_t seed = 0;
    std::size_t nContainerSamples = 1000;
    std::size_t nFocussedSamples = 0;
    std::size_t nCoordinateDescents = 3;
};

Result<SppLbfResult> ConstructSparrowInitialSolution(
    const jaguar::SPInstance& instance,
    const SparrowConstructorConfig& config);
```

- [ ] 先写排序、采样预算、不可行宽度上限和固定 seed 测试。
- [ ] 按 convex-hull-area × diameter 排序并逐 item 构造初始解。
- [ ] 接入 BestSamples、两阶段 Coordinate Descent 和 `SparrowLbfEvaluator`。
- [ ] 复用 JaguarCore external solution conversion，不复制 JSON parser。
- [ ] 验证初始解的 demand、feasibility、strip width 和 placement index。

### Task 3: 实现碰撞量化和分离评估

**Files:**
- Create: `SparrowCore/Include/SparrowCore/Quantify/OverlapProxy.h`
- Create: `SparrowCore/Source/Quantify/OverlapProxy.cpp`
- Create: `SparrowCore/Include/SparrowCore/Quantify/PairMatrix.h`
- Create: `SparrowCore/Source/Quantify/PairMatrix.cpp`
- Create: `SparrowCore/Include/SparrowCore/Quantify/CollisionTracker.h`
- Create: `SparrowCore/Source/Quantify/CollisionTracker.cpp`
- Create: `SparrowCore/Include/SparrowCore/Eval/SeparationEvaluator.h`
- Create: `SparrowCore/Source/Eval/SeparationEvaluator.cpp`
- Modify: `SparrowCore/Tests/TestQuantify.cpp`

- [ ] 先测试 polygon-polygon、polygon-container、零/近零重叠和 NaN/Infinity 防护。
- [ ] 实现 overlap proxy 和形状 penalty，保持 Rust f32 决策容差。
- [ ] 实现无序 item pair 的稳定索引和对称更新。
- [ ] CollisionTracker 支持初始化、保存、恢复、移动 item、权重更新和总 loss。
- [ ] SeparationEvaluator 将 Clear、Collision、Invalid 统一映射为 SparrowCore SampleEval。
- [ ] 验证 Layout 快照恢复后 tracker 与 Layout 一致。

### Task 4: 实现 Separator 和 Worker

**Files:**
- Create: `SparrowCore/Include/SparrowCore/Optimizer/Separator.h`
- Create: `SparrowCore/Source/Optimizer/Separator.cpp`
- Create: `SparrowCore/Include/SparrowCore/Optimizer/SeparatorWorker.h`
- Create: `SparrowCore/Source/Optimizer/SeparatorWorker.cpp`
- Create: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationTerminator.h`
- Create: `SparrowCore/Source/Optimizer/OptimizationTerminator.cpp`
- Modify: `SparrowCore/Tests/TestSeparator.cpp`

**Interface:**

```cpp
Result<std::pair<jaguar::SPSolution, CollisionSnapshot>> Separate(
    Separator& separator,
    const OptimizationTerminator& terminator,
    const SeparatorConfig& config);
```

- [ ] 先写空布局、单 item、碰撞 item、rollback 和无改进终止测试。
- [ ] Worker 只使用任务本地评估对象，不共享可变 Layout。
- [ ] Separator 实现 item 移动、rollback、冲突权重、strike limit 和 no-improvement limit。
- [ ] 通过 JaguarCore Layout 快照提交/恢复候选。
- [ ] 用固定 seed 验证多次运行指标一致。

### Task 5: 实现 Exploration

**Files:**
- Create: `SparrowCore/Include/SparrowCore/Optimizer/Exploration.h`
- Create: `SparrowCore/Source/Optimizer/Exploration.cpp`
- Create: `SparrowCore/Include/SparrowCore/Optimizer/OptimizationListener.h`
- Create: `SparrowCore/Source/Optimizer/OptimizationListener.cpp`
- Modify: `SparrowCore/Include/SparrowCore/SparrowConfig.h`
- Modify: `SparrowCore/Tests/TestExploration.cpp`

- [ ] 先测试可行解宽度收缩、不可行解池和终止器。
- [ ] 实现按 shrink step 收缩 strip width。
- [ ] 实现 infeasible solution pool，按 loss 分布选择恢复解。
- [ ] 实现大 item disruption 和 contained item relocation。
- [ ] 每次可行改进通过 listener 报告，终止时返回最佳可行解。

### Task 6: 实现 Compression 和顶层 Optimize

**Files:**
- Create: `SparrowCore/Include/SparrowCore/Optimizer/Compression.h`
- Create: `SparrowCore/Source/Optimizer/Compression.cpp`
- Create: `SparrowCore/Include/SparrowCore/Optimizer/SparrowOptimizer.h`
- Create: `SparrowCore/Source/Optimizer/SparrowOptimizer.cpp`
- Modify: `SparrowCore/Tests/TestCompression.cpp`
- Modify: `SparrowCore/Tests/TestSparrowOptimizer.cpp`

**Interface:**

```cpp
Result<SparrowOptimizationResult> Optimize(
    const jaguar::SPInstance& instance,
    const SparrowConfig& config,
    OptimizationListener* listener = nullptr);
```

- [ ] 先测试 shrink range、time-based decay、failure-based decay 和不可行回滚。
- [ ] 从 Exploration 最佳解开始逐步压缩 strip width。
- [ ] 压缩失败恢复上一个可行解，并按配置衰减收缩比例。
- [ ] 顶层 Optimize 串联 LBF 初始解、Exploration、Compression 和最终 metrics。
- [ ] 支持初始 solution warm start，并验证恢复后的 demand/layout。
- [ ] 完成 SPP 真实小实例端到端测试。

### Task 7: CLI、WASM、Demo、差分和发布验收

**Files:**
- Create: `SparrowCore/Tools/SparrowCli/`
- Create: `SparrowCore/Tools/SparrowWasm/`
- Create: `SparrowCore/Demo/`
- Create: `SparrowCore/Docs/Compatibility.md`
- Create: `SparrowCore/Docs/Deployment.md`
- Create: `SparrowCore/Tests/Differential/`
- Modify: `SparrowCore/Bench/BenchMain.cpp`
- Modify: qmake project files for native, CLI, WASM and Demo targets

- [ ] CLI 使用 JSON 输入、配置参数、终止时间和 JSON/SVG 输出。
- [ ] WASM 只暴露 JSON 输入和 JSON/SVG 输出，不暴露 STL 或 Qt ABI。
- [ ] Demo 支持加载实例、配置 seed/budget、运行优化、展示指标和 SVG。
- [ ] Rust/C++ 差分验证 RNG、LBF 初始解、Separator loss、最终 metrics 和 JSON restore。
- [ ] Bench 输出有效样本率、Surrogate reject、exact collision、耗时和 checksum。
- [ ] Debug/Release 原生构建、WASM 构建和 Demo 静态资源检查全部通过。
- [ ] 扫描 SparrowCore 源码，确认无低级命名违规、Qt/Rust/NestLab 运行时依赖和旧小写 include。
