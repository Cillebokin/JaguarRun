# B15：jagua-rs 测试逐项映射

## 基线、范围与计数

- 行为基线为 E4/D5 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`，工具链 Rust 1.98.1。pinned 测试源码从 Cargo checkout 静态读取；本次没有运行 Rust 或 C++ 测试。
- 范围包括 `jagua-rs/src/collision_detection/quadtree/tests.rs` 与 pinned `lbf/tests/{tests,container_identity,external_item_ids}.rs`。测试目标分层：几何、实体和 IO 映射到 JaguarCore；LBF 优化器集成映射到 SparrowCore，并在适用时补列 JaguarCore 的 pinned LBF comparator。
- pinned 基线共有 10 个测试函数、27 个展开后的测试实例：`tests.rs` 的 19 个 `test_case` 和 5 个单项测试共 24 个，再加 `container_identity`、`external_item_ids` 与 quadtree 各 1 个。
- `CodeResource/jagua-rs/lbf/tests` 是 downstream fork：本地有 6 个测试文件，pinned 有 3 个；本地 `container_identity.rs`、`tests.rs` 与 pinned 不同，`differential_matrix.rs`、`differential_trajectory.rs`、`optimizer_fixture.rs` 只在本地存在，`external_item_ids.rs` 与 pinned 相同。quadtree 测试源码在两处一致。
- 本地 fork 的静态测试实例数为 30：`tests.rs` 的 19 个参数实例和 4 个单项测试、container identity 1、external IDs 1、differential matrix 2、trajectory 1、optimizer fixture 1，以及 quadtree 1。此数用于清点，不是本次运行结果。

表中的“映射”表示找到了覆盖同一功能域的 C++ 测试；“未逐项复现”列出的输入、轨迹或断言仍属于覆盖边界，不能据映射推导行为等价。

## pinned 基线测试

| Rust 测试 | C++ 对应测试 | 映射与边界 |
|---|---|---|
| `jagua-rs/src/collision_detection/quadtree/tests.rs::edge_quadrants_allow_f32_false_positive_at_bisector` | `JaguarCore/Tests/TestCollision.cpp::runCollisionTests`；`JaguarCore/Tests/Differential/CompareGeometry.cpp::runGeometryDifferentialTests` | C++ 覆盖 quadtree 分区查询与矩形象限边界；当前几何差分覆盖退化象限和 ULP 边界。尚无 C++ 用例使用 Rust 测试中的同一条 f32 bisector edge，并逐项比较两组四象限布尔数组。 |
| `lbf/tests/container_identity.rs::restore_uses_static_geometry_and_bin_stock_uses_bin_identity` | `JaguarCore/Tests/Differential/CompareLayoutLifecycle.cpp::runLayoutLifecycleDifferentialTests`；`JaguarCore/Tests/Differential/CompareProblem.cpp::runProblemDifferentialTests`；`JaguarCore/Tests/TestBpp.cpp::runBppTests`；`JaguarCore/Tests/TestMspp.cpp::runMsppTests` | B6 覆盖 Layout base identity、不同 base 恢复拒绝及失败状态、BPP 库存/成本和 MSPP 生命周期。Rust 用同一外部 container ID 配不同形状或 quality zones、非顺序 bin IDs `900/70`，并在单测中组合 Layout/BPP/MSPP；C++ 没有将这些输入合并成同一个成对场景。 |
| `lbf/tests/external_item_ids.rs::sparse_item_ids_survive_json_svg_and_warm_start` | `JaguarCore/Tests/TestIo.cpp::runIoTests`；`JaguarCore/Tests/TestJsonCodec.cpp::runJsonCodecTests`；`JaguarCore/Tests/TestSvgExport.cpp::runSvgExportTests`；`JaguarCore/Tests/TestLayout.cpp::runLayoutTests` | C++ 覆盖外部 ID 导入、解码/编码、SVG 输出和布局项目顺序；`ItemId` 为 `uint64_t`。未找到以 `u64::MAX` 作为 item ID 并贯穿 solution/SVG/warm-start 的同一条 C++ 用例。 |
| `lbf/tests/tests.rs::test_strip_packing`，13 个参数：`albano`、`blaz1`、`dagli`、`fu`、`jakobs1`、`jakobs2`、`mao`、`marques`、`shapes0`、`shapes1`、`shirts`、`swim`、`trousers` | `SparrowCore/Tests/TestSppLbf.cpp::TestSmallSppFixture`、`SparrowCore/Tests/TestSppLbf.cpp::TestSparrowRustReferenceMetrics`、`SparrowCore/Tests/TestSppLbf.cpp::TestSparrowRustMultiInstanceLbfParity`；`SparrowCore/Tests/TestSampling.cpp::TestLbfSearch`；`JaguarCore/Tests/Differential/CompareOptimizer.cpp::runOptimizerDifferentialTests` | C++ 有 SPP LBF 构造、搜索和固定 Rust 指标/多实例对拍。Rust 测试对 13 个资产各跑 quadtree depth `0/3/10`，执行 solve、移除最多 5 件、保存、再 solve、恢复、第三次 solve；没有同一套 13 个资产和完整操作序列的 C++ 矩阵。 |
| `lbf/tests/tests.rs::test_bin_packing`，6 个参数：`baldacci1`、`baldacci2`、`baldacci3`、`baldacci4`、`baldacci5`、`baldacci6` | `SparrowCore/Tests/TestBppLbf.cpp::TestSmallBppFixture`、`SparrowCore/Tests/TestBppLbf.cpp::TestOpenLayoutReuseAndInventory`、`SparrowCore/Tests/TestBppLbf.cpp::TestClosedBinFallbackDoesNotEmitEmptyLayouts`、`SparrowCore/Tests/TestBppLbf.cpp::TestBppJsonRoundTrip`；`JaguarCore/Tests/TestBpp.cpp::runBppTests` | C++ 覆盖 BPP LBF、开/闭 bin、库存、成本和 IO。Rust 六个资产 × 三种 quadtree depth 的求解及移除/保存/恢复/重跑序列没有同构 C++ 矩阵。 |
| `lbf/tests/tests.rs::test_rejects_self_intersecting_polygon` | `JaguarCore/Tests/TestPolygon.cpp::runPolygonTests`；`JaguarCore/Tests/TestIo.cpp::runIoTests` | C++ bow-tie 多边形构造返回 `SelfIntersection`，并有导入错误测试；Rust 该用例走 SPP instance import，错误阶段不完全同构。 |
| `lbf/tests/tests.rs::test_inflate_all_dataset_items` | `JaguarCore/Tests/TestShapeModification.cpp::runShapeModificationTests`；`SparrowCore/Tests/TestQuantify.cpp::TestSppImportedOffsetReachesSeparationEvaluator` | 两侧覆盖 offset/inflate 的固定几何及 SPP 导入接入。Rust 会遍历资产目录中的全部 JSON 并用 separation `2.0` 导入每个 item；没有同范围 C++ 全资产扫描。 |
| `lbf/tests/tests.rs::item_holes_are_rejected_but_container_holes_are_preserved` | `JaguarCore/Tests/TestIo.cpp::runIoTests` | C++ 对带孔 item 返回 `UnsupportedShape`，容器多边形内环可导入并参与碰撞判定；该测试功能有直接对应。 |
| `lbf/tests/tests.rs::instance_separation_controls_item_and_container_geometry` | `JaguarCore/Tests/TestIo.cpp::runIoTests`；`JaguarCore/Tests/TestJsonCodec.cpp::runJsonCodecTests`；`JaguarCore/Tests/TestShapeModification.cpp::runShapeModificationTests` | C++ 覆盖正 separation 下 item/container offset 和外部 JSON 字段往返。Rust 另测 `-1`、`NaN`、`+Inf` 输入拒绝；对应的完整接受/拒绝集合未在同一 C++ 用例中成对验证，仍归 B14。 |
| `lbf/tests/tests.rs::explicit_rotations_import_solve_and_round_trip` | `JaguarCore/Tests/TestIo.cpp::runIoTests`；`JaguarCore/Tests/TestJsonCodec.cpp::runJsonCodecTests`；`SparrowCore/Tests/TestSampling.cpp::TestRotationModes`；`SparrowCore/Tests/TestSppLbf.cpp::TestSppJsonRoundTrip` | C++ 覆盖离散/步进/连续角策略、解导入导出和优化器输入。Rust 用一条测试贯通角度归一化、合法化、求解与 solution round-trip，并包含多种非法 rotation JSON；完整输入集合与端到端结果未一一配对，见 B14/B8。 |

## 本地 fork 测试映射

| 本地 Rust 测试 | C++ 对应测试 | 基线处理 |
|---|---|---|
| `lbf/tests/container_identity.rs::restore_uses_static_geometry_and_bin_stock_uses_bin_identity`（本地 fork 版本） | JaguarCore B6 的 `CompareLayoutLifecycle`、`CompareProblem`、`TestBpp`、`TestMspp` | 相关状态行为已映射；本地版本因依赖 API 和实现与 pinned 不同，不能作为 D5 Rust 参考证据。 |
| `lbf/tests/external_item_ids.rs::sparse_item_ids_survive_json_svg_and_warm_start` | `TestIo`、`TestJsonCodec`、`TestSvgExport`、`TestLayout` | 文件与 pinned 一致，映射同上；只计作本地重复清点，不另计一项 pinned 测试。 |
| `lbf/tests/tests.rs`（本地版本）：`test_strip_packing` 13 参数、`test_bin_packing` 6 参数、`test_rejects_self_intersecting_polygon`、`item_holes_are_rejected_but_container_holes_are_preserved`、`instance_separation_controls_item_and_container_geometry`、`explicit_rotations_import_solve_and_round_trip` | 对应 pinned 表中同名测试的 C++ 落点 | 同名不代表测试体相同。本地文件 280 行，pinned 文件 345 行；pinned 的 `test_inflate_all_dataset_items` 在本地缺失，B15 清单按 pinned 版本保留该行。 |
| `lbf/tests/differential_matrix.rs::edge_matrix_matches_shared_reference`、`containment_matrix_matches_rust_cde_semantics` | `JaguarCore/Tests/Differential/CompareMatrix.cpp::runEdgeMatrixDifferentialTests`、`JaguarCore/Tests/Differential/CompareMatrix.cpp::runContainmentMatrixDifferentialTests` | 两项本地 fork 测试读取共享 TSV；B11 的 C++ comparator 已接入 D5 RustOracle fixture。local-path Rust 测试不作为 pinned revision 的运行证据。 |
| `lbf/tests/differential_trajectory.rs::shared_seed_trajectory_matches_cpp_oracle` | `JaguarCore/Tests/Differential/CompareTrajectory.cpp::runTrajectoryDifferentialTests` | 轨迹字段映射到 C++ comparator 和 pinned fixture；local-path Rust 测试仍只属 fork 回归。 |
| `lbf/tests/optimizer_fixture.rs::fixed_seed_optimizer_fixture_has_stable_metrics` | `JaguarCore/Tests/Differential/CompareOptimizer.cpp::runOptimizerDifferentialTests`；`SparrowCore/Tests/TestSppLbf.cpp::TestLegacyLbfMetricsReferenceFixture` | 对应固定 seed 的 SPP LBF metrics；JaguarCore 的 committed fixture 已从 D5 pinned LBF CLI 生成。该本地 Rust 测试本身不证明 pinned 输出。 |

## B15 结论与未覆盖项

B15 的映射验收已完成：pinned revision 中发现的每个 Rust 测试函数及其参数展开均有 C++ 测试落点；本地 fork 的测试增量和改动也逐项登记。没有将任何测试标成“不适用”，因为每项都存在相关 C++ 功能测试；映射层级与未覆盖范围已在表中注明。

这项完成只表示测试可追溯，不表示测试断言、输入集或运行结果已逐项相等。仍未闭合的覆盖包括：13/6 个资产与三档 quadtree depth 的完整 LBF 重跑矩阵、全部资产的 separation inflate 扫描、quadtree bisector false-positive 的精确边界输入，以及 rotation 数值错误集合的逐项端到端对拍。B14 已另行执行 `u64::MAX` 接受、零需求重复 ID 拒绝、orientation 必填/旧字段拒绝、带孔 item 拒绝和 min-separation 输入域，见 [B14 输入契约执行](91-input-contracts.md)；其余几何与完整 IO 行为仍按 B1/B4/B8 与 Sparrow L 线验收。

本记录本身是 B15 源码静态盘点，没有运行测试或构建。后续 B14 的定向 Rust/C++ 测试证据单独登记于 91；B15 映射完成状态不得被用作其它未闭合差异已通过的证据。
