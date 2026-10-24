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

- [x] 运行 Rust 定向 oracle：`cargo run --offline --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --collision-parity-only`；确认 notch 的 high/low threshold 检测均为 true，collector 数量分别为 0 和 1，pier-only 检测为 true。
- [x] 运行当前 checkout 对应的 MSVC2019 x64 Debug 与 Release `JaguarCoreTests.exe` 全套；两者均退出码为 0。
- [x] 保留 `cd_threshold` 改变 collect 结果、但不改变 Rust `detect` 结果的用例；保留 U-notch 及 pier-only 用例。
- [x] Oracle 与 C++ 对照输出逐项一致。

**验收：** 定向 Rust oracle、C++ Debug 和 Release 测试通过；本项碰撞语义差异关闭。

### 任务 2：修正 `min_item_separation = 0` 的 JSON 输出

**文件：**
- 修改：`JaguarCore/Source/IO/JsonCodec.cpp`
- 测试：`JaguarCore/Tests/TestJsonCodec.cpp`
- 参考：Rust external representation 序列化实现及 Rust oracle 输出

- [x] 为 SPP、BPP、MSPP 各构造 `min_item_separation` 为零的最小 external representation；Rust JSON 均包含字段，值均为 `0.0`。
- [x] 在 C++ JSON 编码测试中断言相同字段存在性和值；保持对象键顺序不作为验收条件。
- [x] 只调整字段省略条件，使零值输出遵循 Rust；非零和缺省解码行为保持现有契约。
- [x] 运行当前 checkout 的 Debug 和 Release `JaguarCoreTests.exe`；两者均退出码为 0。

**验收：** 三类 problem 的零值字段行为与 Rust 相同，JSON round-trip 和现有 fixture 回归通过。

### 任务 3：对齐 Importer 与几何输入边界

**文件：**
- 按差异落点修改：`JaguarCore/Source/IO/Importer.cpp`、`JaguarCore/Source/Geometry/Primitives/SimplePolygon.cpp` 或对应 primitive 实现
- 测试：`JaguarCore/Tests/TestIo.cpp`、`JaguarCore/Tests/TestPolygon.cpp`、`JaguarCore/Tests/TestBoundary.cpp`
- 对照：`JaguarCore/tools/RustOracle/src/main.rs` 与 `JaguarCore/Tests/Fixtures/RustReference/`

- [x] Rust oracle Debug/Release 已固定非连续重复、`-0.0` 闭合点、NaN/Infinity 多边形及解变换、端点接触：两侧均拒绝/失败或均接受；Rust 非有限路径 panic，C++ 映射为 `Result` 错误。
- [x] C++ 回归覆盖相同输入：`ImportItem` 的成功/错误码和顶点数、SP 解导入错误码、端点接触交点；相关几何/导入测试纳入全套运行。
- [x] 复核确认这些项没有接受边界分叉；不添加重复的生产校验，保留 JaguarCore 的 `Result` 错误表达，并更正逻辑比较记录中的非连续重复顶点误判。
- [x] MSVC2019 x64 Debug 与 Release 全套测试均通过；Rust oracle 的 Debug/Release 非有限输入结果一致。

**验收：** 上述输入的成功/失败边界和可观察结果逐项与 Rust 对照一致；没有新增与本差异无关的校验规则。

### 任务 4：补齐 Offset 算法本体的行为覆盖

**文件：**
- 主要实现：`JaguarCore/Source/Geometry/ShapeModification/Offset.cpp`、`JaguarCore/Source/Geometry/ShapeModification/StraightSkeleton.cpp`
- 测试：`JaguarCore/Tests/TestShapeModification.cpp`、`JaguarCore/Tests/Differential/CompareGeometry.cpp`
- 对照：`JaguarCore/tools/RustOracle/src/main.rs` 和 RustReference fixtures

**已完成子项：**

> **基线范围说明（2026-10-22）**：以下B4历史验收基于当时RustOracle使用的 `CodeResource/jagua-rs` 本地fork。当前Oracle直接锁定D5 revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`，其Offset实现改为 `geo::Buffer`；因此下面Offset的fixture通过记录只说明历史本地fork基线，不能作为当前D5等价证明。当前复核见 `docs/logic-comparison/records/11-offset.md`。
- [x] **历史本地fork基线**：Rust Debug/Release oracle 对 NaN、+Infinity、-Infinity 均返回 `Offset resulted in an empty polygon`；C++ `OffsetShape`/`OffsetShapes` 统一映射为 `OffsetFailed` 与相同消息。C++ Debug/Release 全量测试通过。当前D5的NaN profile出口不同，见11号Offset记录。
- [x] 圆角方向：C++ 改为按 Rust `Skeleton::get_orientation()` 从首个骨架顶点判定环方向；梳形 deflate 的 raw loop 顶点数、面积与边界和 Rust raw oracle 一致。Rust 核心只选择首组件，因此该 fixture 的额外第二组件不作为核心 API 返回值验收。
- [x] 分裂事件回放：在 split 后、生成两个新环的 shrink 事件前补上 Rust `VertexQueue::cleanup()`。

- [x] 使用同一组简单多边形和距离运行 Rust `geo-buffer` 路径与 C++ `OffsetShape`/`OffsetShapes`，覆盖凸形、凹形、内缩分裂、组件消失、多个外轮廓及已有 fallback 拓扑；多组件 raw probe 按核心 API 的 `f32` 距离输入量化。
- [x] 对固定 fixture 比较成功/错误、空结果、首组件选择、顶点数、面积和边界框；`TestShapeModification.cpp` 中的阈值、凹形分裂、梳形、L 形 fallback、近零边和非有限距离断言均来自 Rust oracle，容差按 `f32` 输入与 C++ 计算精度设定。Rust 的原始多组件数只用于解释 Rust 取首组件，不误作 `OffsetShapes` 的 API 对照。
- [x] 按失败 fixture 修正圆角方向判定和 split 后事件队列清理；当前固定用例无未修复分叉，不以面积接近替代拓扑及 API 结果断言。
- [x] 保持 Rust 不进入 JaguarCore 运行时依赖；运行 Rust Debug/Release oracle 与 JaguarCore Debug/Release 全量测试。

**验收：** 已发现的 Offset 分叉均有可复现 Rust fixture；fixture 的拓扑和数值断言在 Debug/Release 通过。未覆盖的输入不宣称已证明等价。

### 任务 5：复核已修复的状态与模型差异，不重复实现

**文件：**
- 测试：`JaguarCore/Tests/TestSpp.cpp`、`JaguarCore/Tests/TestBpp.cpp`、`JaguarCore/Tests/TestMspp.cpp`、`JaguarCore/Tests/TestProblemRandom.cpp`、`JaguarCore/Tests/TestJsonCodec.cpp`
- Rust 对照：`JaguarCore/tools/RustOracle/src/main.rs` 与现有 RustReference fixtures

**本轮复核证据（2026-09-28）：** 新增 `--model-boundary-parity-only` Rust oracle，并分别运行 Debug/Release。Rust Debug 对 `[usize::MAX, 1]` 的 `total_item_qty()` 求和 panic；Release 返回 `0`。需求为 0 时，SPP Debug panic 前状态为 `remaining=0, placed=0`；BPP/MSPP 则先放入布局再 panic，状态为 `remaining=0, placed=1`（BPP 还已消耗一个 bin）。Release 三个模型均成功放置、需求回绕为 `usize::MAX`。JaguarCore Release 行为已有回归覆盖；Debug 侧目前把需求耗尽映射为 `Infeasible`。因此本项不能只以错误码作为对照，Rust panic 后状态是否属于验收契约仍待确定。

MSPP restore 的宽度不一致快照也需单列：现有 C++ `sameStripContainer` 预检会拒绝 strip.width 与 layout snapshot 容器不一致的解；Rust restore 直接复制快照 strip，Release 接受该不一致状态，Debug 的一致性断言可能在状态已变更后 panic。已有 C++ 用例锁定拒绝行为，不能将其误标成已对齐。

新增 `--restore-boundary-parity-only` oracle 后，Debug/Release 结果为：SPP 的 CDE 配置变化和 strip/layout 宽度不一致均返回成功（宽度 case 的 strip=5、layout 宽度=10）；MSPP 的 CDE 配置变化均成功，宽度不一致在 Debug panic（状态已变为 strip=5、layout 宽度=10）而 Release 成功。当前 C++ SPP 对配置/宽度变化均返回错误，MSPP 对配置变化接受但对宽度变化在两配置均返回错误。Debug panic 的状态口径仍待确认；Release 输入接受边界属于已批准的等价修复范围。

- [x] 复核现有 C++ 用例已覆盖 SPP 需求和超需求 Release、BPP/MSPP 超需求 Release、MSPP snapshot restore 配置/宽度、稀疏 MSPP 导出 ID 与 `quality_zones` SVG 输出。
- [x] 新增 `--model-boundary-parity-only` 和 `--restore-boundary-parity-only` Rust oracle；Debug/Release 均已运行并把需求、放置数、库存、strip 与 layout 宽度状态写入本计划及对应逻辑记录。
- [ ] 按用户选择的 panic→Result 状态口径补齐 C++ Debug 对照，尤其是 BPP/MSPP 超需求下溢后的部分状态。
- [x] 修正并双配置验收 Rust 已接受而 C++ Restore 拒绝的快照边界；现有相关 C++ 测试仍锁定旧的拒绝行为。（2026-09-29 完成：SPP/MSPP `Restore` 删除 `sameCdeConfig`/`sameShapeModifyConfig`/`sameStripContainer` 与 strip 数值前置校验，快照 strip 值直接生效；`TestSpp.cpp`/`TestMspp.cpp`/`TestJsonCodec.cpp` 断言改为接受并校验恢复后的 strip 状态；`ExternalSPSolutionFromSolution` 等三个导出函数移除导出前 `Validate*Solution` 前置拒绝（jagua-rs export 是纯函数）。Debug/Release 全量测试退出码 0，`--restore-boundary-parity-only` oracle 复验通过。）
- [x] 已把 SPP `TotalDemand` Debug 溢出与超需求放置结论写入记录 27/29/31；MSPP 导出 ID 有记录 35 与 `TestJsonCodec.cpp` 稀疏槽位用例，`quality_zones` 有记录 36 与 `TestSvgExport.cpp` 图层/填充/碰撞几何断言。
- [x] 本轮合并后 checkout 的 MSVC2019 x64 Debug 与 Release `JaguarCoreTests.exe` 全量运行均退出码 0；这只验证现有断言，不关闭上述未对齐边界。

**验收：** 以上已修复项目都有可重复的 Rust 对照或明确的 Rust 行为来源，Debug/Release 结果符合该来源；不产生重复重写。

### 任务 6：统一差分闭环并关闭清单

**文件：**
- 更新：`JaguarCore/Tests/Differential/README.md`
- 更新：`docs/compatibility.md`
- 使用：`JaguarCore/Tests/Differential/CompareGeometry.cpp`、`CompareProblem.cpp`、`CompareMatrix.cpp`、`CompareTrajectory.cpp`、`CompareBoundary.cpp` 及全量测试工程

**2026-09-29 差分对齐轮（四项工作）完成记录：**

1. **Restore 接受边界（原任务 5 未关闭项）**：已闭合，见任务 5 的 2026-09-29 记录。
2. **四叉树增量式移植**：`Quadtree.h/.cpp` 重写为 jagua-rs quadtree 的逐行结构移植——`Node` 持有按 presence 降序的 `NodeHazard`（含 `PartialHazard` 边子集 + `ffBbox`），注册走懒扩展 + `Constrict`（含 `QUADRANT_NEIGHBOR_LAYOUT` 邻居 presence 传播与质心包含回退），注销走递归移除 + 无 Partial 时折叠子节点；`activeEdges` 缓存使 `cd_threshold` 恢复为 Rust 的查询时语义。`CollisionEngine` 的 `DetectPolygonCollision`/`CollectPolygonCollisionsUntil` 改为 Rust 的短路边遍历 + `detect_containment_collision`（POI 单点 + scope 解释）路径；`DetectSurrogateCollision`/`CollectSurrogateCollisionsUntil` 改为 `QTNode::collides/collect_collisions_until` 移植（含 Circle 象限优先顺序与 Edge 半段相交象限分类 `edge_intersection_half`/`half_intersect`）。根节点一律 Partial（对齐 `QTHazard::from_root`）。notch 高/低阈值与 pier-only oracle 用例逐项一致；Debug/Release 全量测试退出码 0。
3. **float 重存储**：按决策保持 double + RustF32 双轨，作为既定接口差异记录（不改动）。
4. **geo-buffer 逐行对齐（历史本地fork基线）**：当时对 `StraightSkeleton.cpp` 与 geo-buffer 0.2.0 源码（cargo registry）逐函数审计——VertexQueue/Index/Node、Ray、Timeline、事件回放和圆角路径均已逐项核对；修复一处本地fork基线的cleanup分叉。所列12项结果只描述本地fork，不是当前D5 `geo::Buffer` 输出或验收结果。当前基线见11号Offset记录。

- [x] 为 1–7 项逐条登记状态：未验收、已复现、已修复、Debug/Release 通过或明确保留的接口差异；每个“通过”关联测试/fixture。
- [x] 运行 Rust oracle 与 JaguarCore 完整 Debug、Release 测试；确认本计划新增的每个 fixture 在两侧均消费。
- [ ] 复查边界分类、需求/库存守恒、snapshot 前后状态和 JSON round-trip；性能差异单独记录，不作为功能逻辑失败或通过的证据。
- [ ] 只在所有范围内条目均有证据且无未解释输出分叉时，更新 compatibility 结论为已完成；否则保留未完成状态和具体阻塞条目。

**验收：** 1–7 项全部有来源、对照用例和 Debug/Release 结果；第 8 项及优化器仍明确排除。
