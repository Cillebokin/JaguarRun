# JaguarCore 核心行为等价收尾计划

> **范围：** 对齐 `jagua-rs/jagua-rs` 核心 crate 的功能逻辑；保留 JaguarCore 的 C++ `Result` 接口。排除 LBF/Sparrow 优化器，并按此前约定跳过第 8 项。

**目标：** 将已确认的 1–7 项差异逐项修复并以 Rust oracle 验收，最终只在清单闭合后判断核心逻辑等价。

**执行方式：** 复用 JaguarCore 现有模块和差分测试设施。每个仍未闭合的差异先添加 Rust 对照输入/结果，再修改最小范围的 C++ 逻辑；已完成的修复只纳入回归验收，不重复实现。

**技术栈：** Rust `jagua-rs` oracle、C++17、现有 qmake/MSVC2019 x64 Debug/Release 测试工程。

## 全局约束

- 参照物仅为 `CodeResource/jagua-rs/jagua-rs` 核心 crate；不对齐 `lbf`、Sparrow 搜索路径或优化器输出顺序。
- 按结果、状态变更、输入接受边界和核心序列化语义判断等价；保留 C++ `Result<T>` 错误传递方式。
- 不重写已通过的模块，不重构仅影响性能的四叉树架构，不把第 8 项或展示样式差异混入本轮验收。
- 不创建或修改 Git commit；已有工作区修改保持原样并纳入验收。
- 每条差异都要记录 Rust 参考、C++ 对照、配置（Debug/Release）及结论；没有对应证据的条目不得标为等价。

---

### 任务 1：收口当前 Exterior/quadtree 碰撞差分

**文件：**
- 已修改：`JaguarCore/Source/Collision/CollisionEngine.cpp`
- 已修改：`JaguarCore/Source/Collision/Quadtree/Quadtree.cpp`
- 已修改：`JaguarCore/include/JaguarCore/collision/quadtree.h`
- 已修改：`JaguarCore/tests/TestCollision.cpp`
- 已修改：`JaguarCore/tools/RustOracle/src/main.rs`

- [ ] 运行 Rust 定向 oracle：`cargo run --offline --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --collision-parity-only`；确认 notch 的 high/low threshold 检测均为 true，collector 数量分别为 0 和 1，pier-only 检测为 true。
- [ ] 运行 `.style-build-debug-tests\debug\JaguarCoreTests.exe` 和 `.style-build-debug-tests\release\JaguarCoreTests.exe`；两者均须退出码为 0。
- [ ] 保留 `cd_threshold` 改变 collect 结果、但不改变 Rust `detect` 结果的用例；保留 U-notch 及 pier-only 用例。
- [ ] 若 oracle 与 C++ 任一输出不同，只修改上述碰撞路径和对应测试，直到输出逐项一致。

**验收：** 定向 Rust oracle、C++ Debug 和 Release 测试通过；本项碰撞语义差异关闭。

### 任务 2：修正 `min_item_separation = 0` 的 JSON 输出

**文件：**
- 修改：`JaguarCore/Source/IO/JsonCodec.cpp`
- 测试：`JaguarCore/Tests/TestJsonCodec.cpp`
- 参考：Rust external representation 序列化实现及 Rust oracle 输出

- [ ] 为 SPP、BPP、MSPP 各构造 `min_item_separation` 为零的最小 external representation，并记录 Rust JSON 中该字段是否存在及其值。
- [ ] 在 C++ JSON 编码测试中断言相同字段存在性和值；保持对象键顺序不作为验收条件。
- [ ] 只调整字段省略条件，使零值输出遵循 Rust；非零和缺省解码行为保持现有契约。
- [ ] 运行 `JaguarCoreTests.exe` 的 Debug 和 Release 配置。

**验收：** 三类 problem 的零值字段行为与 Rust 相同，JSON round-trip 和现有 fixture 回归通过。

### 任务 3：对齐 Importer 与几何输入边界

**文件：**
- 按差异落点修改：`JaguarCore/Source/IO/Importer.cpp`、`JaguarCore/Source/Geometry/Primitives/SimplePolygon.cpp` 或对应 primitive 实现
- 测试：`JaguarCore/Tests/TestIo.cpp`、`JaguarCore/Tests/TestPolygon.cpp`、`JaguarCore/Tests/TestBoundary.cpp`
- 对照：`JaguarCore/tools/RustOracle/src/main.rs` 与 `JaguarCore/Tests/Fixtures/RustReference/`

- [ ] 用 Rust oracle 固定非相邻重复顶点、首尾重复点、`+0.0/-0.0`、NaN/Infinity 和边界接触输入的接受/拒绝结果及错误类别。
- [ ] 为每个已确认分叉写一个最小 C++ 回归，比较是否成功、几何结果/碰撞布尔值及状态是否变化。
- [ ] 只修复 Rust 与 C++ 结果不同的输入边界；保留 JaguarCore 的 `Result` 错误表达。
- [ ] 分别运行 Debug、Release 测试，确认 `debug_assert` 类行为没有被错误地当作 Release 保证。

**验收：** 上述输入的成功/失败边界和可观察结果逐项与 Rust 对照一致；没有新增与本差异无关的校验规则。

### 任务 4：补齐 Offset 算法本体的行为覆盖

**文件：**
- 主要实现：`JaguarCore/Source/Geometry/ShapeModification/Offset.cpp`、`JaguarCore/Source/Geometry/ShapeModification/StraightSkeleton.cpp`
- 测试：`JaguarCore/Tests/TestShapeModification.cpp`、`JaguarCore/Tests/Differential/CompareGeometry.cpp`
- 对照：`JaguarCore/tools/RustOracle/src/main.rs` 和 RustReference fixtures

- [ ] 使用同一组简单多边形和距离运行 Rust `geo-buffer` 路径与 C++ `OffsetShape`/`OffsetShapes`，覆盖凸形、凹形、内缩分裂、组件消失、多个外轮廓及已有 fallback 拓扑。
- [ ] 对每个 case 比较成功/错误、空结果、组件数量、首组件选择、顶点数、面积和边界框；坐标容差按 Rust `f32` 与 C++ 计算精度明确记录。
- [ ] 先固定失败 fixture，再针对导致分叉的事件/回退分支做最小修正；不以“面积接近”替代组件拓扑和 API 返回语义。
- [ ] 保持 Rust 不进入 JaguarCore 运行时依赖；运行 Debug/Release 几何与差分测试。

**验收：** 已发现的 Offset 分叉均有可复现 Rust fixture；fixture 的拓扑和数值断言在 Debug/Release 通过。未覆盖的输入不宣称已证明等价。

### 任务 5：复核已修复的状态与模型差异，不重复实现

**文件：**
- 测试：`JaguarCore/Tests/TestSpp.cpp`、`JaguarCore/Tests/TestBpp.cpp`、`JaguarCore/Tests/TestMspp.cpp`、`JaguarCore/Tests/TestProblemRandom.cpp`、`JaguarCore/Tests/TestJsonCodec.cpp`
- Rust 对照：`JaguarCore/tools/RustOracle/src/main.rs` 与现有 RustReference fixtures

- [ ] 将已报告修复的 SPP 需求溢出、超需求放置 Debug/Release 行为、MSPP snapshot restore、MSPP 导出 ID 和 `quality_zones` 输出列入逐项回归表。
- [ ] 对照 Rust 行为运行相关现有测试；仅在某项缺少 Rust 参考或回归失败时补 fixture/测试或修复实现。
- [ ] 在记录中区分 Debug-only 断言、Release 状态变化和 JSON 可观察输出，不用单一 Debug 结果代替双配置验收。

**验收：** 以上已修复项目都有可重复的 Rust 对照或明确的 Rust 行为来源，Debug/Release 结果符合该来源；不产生重复重写。

### 任务 6：统一差分闭环并关闭清单

**文件：**
- 更新：`JaguarCore/Tests/Differential/README.md`
- 更新：`docs/compatibility.md`
- 使用：`JaguarCore/Tests/Differential/CompareGeometry.cpp`、`CompareProblem.cpp`、`CompareMatrix.cpp`、`CompareTrajectory.cpp`、`CompareBoundary.cpp` 及全量测试工程

- [ ] 为 1–7 项逐条登记状态：未验收、已复现、已修复、Debug/Release 通过或明确保留的接口差异；每个“通过”关联测试/fixture。
- [ ] 运行 Rust oracle 与 JaguarCore 完整 Debug、Release 测试；确认本计划新增的每个 fixture 在两侧均消费。
- [ ] 复查边界分类、需求/库存守恒、snapshot 前后状态和 JSON round-trip；性能差异单独记录，不作为功能逻辑失败或通过的证据。
- [ ] 只在所有范围内条目均有证据且无未解释输出分叉时，更新 compatibility 结论为已完成；否则保留未完成状态和具体阻塞条目。

**验收：** 1–7 项全部有来源、对照用例和 Debug/Release 结果；第 8 项及优化器仍明确排除。
