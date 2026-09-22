# JaguarCore API 稳定、边界验证与发布工程设计

**日期：** 2026-09-21  
**状态：** 已批准，实施计划已生成  
**范围：** JaguarCore 本体，不包含 NestLab 兼容层

## 1. 目标

在 `0.1.0-dev` 阶段完成最后一轮最小破坏性公共 API 整理，然后冻结当前 JaguarCore C++ API 的语义边界，并建立可重复的四配置发布前验收流程。

稳定目标限定为当前工程已经支持的 C++17、MSVC2019 x64、动态 CRT 和兼容 `_ITERATOR_DEBUG_LEVEL` 组合。JaguarCore 不在本轮承诺跨编译器或跨标准库的 C++ ABI，也不引入独立 C ABI。

完成后，使用者应能：

1. 通过 `Result` 观察所有会因输入或几何变换失败的公共操作。
2. 依据公共头文件和部署文档判断线程、快照、句柄、配置和 ABI 契约。
3. 用一条本地 PowerShell 命令构建、测试、组装并验证 Debug/Release 与 AVX2/scalar 四个包变体。
4. 使用独立 consumer smoke test 验证包内头文件、import lib、DLL 和版本探针能够组成真实外部消费链路。

## 2. 非目标

- 不实现 NestLab 兼容层、DXF 或其他上层产品适配。
- 不重写 JaguarCore 的算法实现，不改变已验证的几何、碰撞、Layout、SPP/BPP/MSPP 业务语义。
- 不引入第三方 fuzz 框架、远程 CI 平台或新的运行时依赖。
- 不把现有 C++ API 改造成 C ABI。
- 不创建 Git 提交；版本控制集成由用户自行处理。

## 3. 公共 API 稳定设计

### 3.1 可失败变换统一走 `Result`

保持 `SimplePolygon::transformed(const Transform2d&)` 的现有 `Result<SimplePolygon>` 签名，并扩展同一语义到以下接口：

```cpp
Result<Surrogate> Surrogate::transformed(const Transform2d&) const;

Result<bool> CollisionEngine::detectSurrogateCollision(
    const Surrogate&, const Transform2d&, const HazardFilter&) const;

Result<bool> CollisionEngine::collectSurrogateCollisionsUntil(
    const Surrogate&, const Transform2d&, HazardCollector&,
    const HazardFilter&, const CollisionStopCallback&) const;

Result<std::vector<std::uint8_t>> CollisionEngine::detectPolygonCollisionsParallel(
    const std::vector<SimplePolygon>&, const HazardFilter&,
    const BatchCollisionOptions&) const;
```

非有限 `Transform2d` 返回 `ErrorCode::InvalidInput`。surrogate 查询中 `Result<bool>` 的值仍表示原有的碰撞或提前停止结果；错误只表示输入/执行无法完成，不改变正常查询语义。

并行批查询中 `workerCount == 0` 继续表示自动选择 worker 数，`grainSize` 必须大于零；非法批参数返回 `InvalidInput`。空输入仍返回成功的空结果。

`Transform2d::apply()`、`inverse()` 和 `followedBy()` 继续作为轻量 `noexcept` 值级数学操作；它们不新增 `Result` 层。凡是会重新构造几何、访问碰撞索引或依赖外部状态的操作，必须在公共入口验证变换并通过 `Result` 报告失败。

### 3.2 NumericPolicy、版本和线程契约

`NumericPolicy` 增加 `isValid() const noexcept`。四个公共容差必须是有限且非负的值；`SimplePolygon::create()` 在执行几何计算前拒绝非法策略。现有默认值和 `isFinite(double)` 保持兼容。

版本接口继续提供 `version()` 和 C-linkage 的 `JaguarCoreVersion()`。新增独立的 C-linkage API 版本探针：

```cpp
extern "C" JAGUARCORE_API std::uint32_t JaguarCoreApiVersion() noexcept;
```

本轮冻结后的 API 版本返回 `1`。产品公共版本字符串和 API 版本不互相替代；runtime verifier 和 consumer smoke test 都检查 API 版本探针。

`CollisionEngine` 的公共线程契约如下：

- 同一实例允许多个线程同时执行只读查询，包括普通查询、`save()` 和并行批查询。
- `registerHazard()`、`deregisterHazard()`、`restore()` 是状态变更操作，调用方必须保证其与任何查询以及其他状态变更互斥。
- 同一实例的并行批查询提交会被内部串行化，但每一批内部仍可并行处理。
- 并行批查询的 `HazardFilter` 可能被多个 worker 同时调用，调用方必须提供可并发调用的无副作用 filter。

当前 public STL 类型边界、MSVC2019 x64、动态 CRT 和兼容迭代器调试级别约束继续保留在部署文档中。

## 4. 边界验证设计

验证集中在公共工厂、变更操作和恢复操作，避免把重复检查散落到内部算法。

### 4.1 几何与数值

- 点、矩形、边、多边形和变换入口拒绝 NaN/Inf。
- `Rect2d::create()` 继续要求有限且严格有序的边界。
- `SimplePolygon::create()` 除现有点数、重复点、自交和零面积检查外，检查有符号面积、包围盒、质心、直径等派生值是否仍为有限值，防止极大有限坐标在中间运算中溢出后进入内部状态。
- 非法 `NumericPolicy` 在几何计算前返回 `InvalidInput`。
- 生成 surrogate 或执行 surrogate 查询前验证配置和变换；保留现有配置范围及算法语义上限。

### 4.2 业务配置和状态

- SPP/BPP/MSPP 的尺寸、宽度、高度、需求量、旋转范围和形状修改参数继续在工厂或变更入口验证有限性、正值、范围和数量关系。
- `CDEConfig`、批处理选项和 surrogate 配置补齐零值、非有限值、整数范围和可能导致资源/循环异常的边界检查；不为没有现有语义依据的参数添加任意业务上限。
- 无效或过期 `HazardHandle`、`PlacedItemHandle` 返回 `InvalidInput`，不修改状态。
- 快照恢复继续使用 `SnapshotMismatch` 区分静态状态不匹配、动态实体不匹配、重复/非法句柄和损坏的快照内容。

### 4.3 失败原子性

以下失败路径必须保持调用前可观察状态不变：

- `Layout::placeItem()`、`removeItem()`、`swapContainer()`；
- `Layout::restore()` 和 `CollisionEngine::restore()`；
- SPP/BPP/MSPP 的宽度变更、恢复和布局变更；
- 任何 surrogate/几何变换失败路径。

现有“先构建临时状态，最后一次性替换”的实现保留；新增测试在失败前后比较计数、碰撞结果、快照/布局可观察字段。

错误码继续复用现有 `InvalidInput`、`SnapshotMismatch`、`Infeasible` 等枚举，不在本轮扩张错误码表，避免冻结前产生不必要的错误码 ABI 负担。

## 5. 测试设计

在现有 assert 测试体系中增加可重复的边界矩阵，覆盖：

- NaN/Inf 点、矩形、变换、NumericPolicy 和配置；
- 极大有限坐标导致的派生值溢出；
- 零/负尺寸、零 grain、过期句柄、非法快照、重复快照句柄；
- `Surrogate::transformed()` 和两个 surrogate 碰撞接口的错误返回；
- 并行批查询的空输入、单 worker、不同 grain、输出顺序、重复并发提交；
- 无效恢复和放置失败后的状态不变性；
- 固定种子的有限随机多边形序列，验证不会产生非有限内部结果，并与已有几何/碰撞回归保持一致。

测试不引入外部 fuzz 依赖；随机测试使用现有测试运行时和固定种子。新增 API 变更的所有源码调用点、测试和 benchmark 必须同步迁移。

## 6. 发布工程设计

新增一个 JaguarCore 发布矩阵 PowerShell 入口，参数化以下路径和版本：Qt qmake、VS x64 环境脚本、构建根目录、包根目录、公共版本和文件版本。默认值匹配当前 workspace，但不依赖固定绝对路径才能运行。

脚本执行顺序：

1. 配置并构建 Debug/Release 与 AVX2/scalar 四套核心库。
2. 构建并运行四套匹配测试，运行时 PATH 指向对应核心 DLL。
3. 从已验证输出组装包内公共头文件、DLL 和 import lib。
4. 使用包内头文件和 import lib 编译、链接并运行 consumer smoke test。
5. 调用现有 `VerifyPackage.ps1`，核对 x64、公共版本、文件版本、动态 CRT、版本导出和依赖表。
6. 输出四配置结果；任何一步失败都使脚本失败。

consumer smoke test 只依赖 JaguarCore 公共头文件和包文件，不包含源码目录，不链接 Qt 或 Rust。至少验证：版本和 API 版本探针、正常 `SimplePolygon` 创建、非法变换的 `Result` 错误，以及一个基础碰撞查询。

包目录仅更新 JaguarCore 管理的明确子目录，不递归删除 workspace 或其他外部目录。发布文档在现有 `docs/deployment.md` 和 `JaguarCore/package/README.md` 中补充脚本用法、四配置矩阵、consumer smoke test 和 API/ABI 约束。

## 7. 验收标准

本设计实施完成后必须满足：

1. 四个核心配置和四个对应测试均成功退出。
2. 所有公共 API 调用点已迁移，非法输入能通过 `Result` 检查，失败操作不改变状态。
3. 包内公共头文件与源码公共头文件一致。
4. 四个包变体都能通过 consumer smoke test 的编译、链接、加载和运行。
5. `VerifyPackage.ps1` 和新增 API 版本检查全部通过。
6. 线程契约、ABI 限制和发布命令已写入公共头文件/部署文档。
7. 只修改 JaguarCore、其测试、发布脚本和相关部署文档；不修改 NestLab 兼容层，不新增第三方依赖，不创建 Git 提交。
