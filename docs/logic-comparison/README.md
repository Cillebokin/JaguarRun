# jagua-rs ↔ JaguarCore 逐文件逻辑比对记录

比对基线：

- Rust：`CodeResource/jagua-rs/jagua-rs/src`（jagua-rs 0.9.0，pinned toolchain 1.90.0）
- C++：`JaguarCore/Include` + `JaguarCore/Source`
- 代码状态：含 eef0830（2026-09-28 "处理其他差异点的统一"）提交；所有记录均按该状态逐行验证

方法：按下方清单逐个文件比对，每完成一个文件就在 `records/` 下落一份记录，再进入下一个文件。每份记录格式统一：判定 → 按 Rust 函数逐条比对（【一致】/【差异】/【C++缺失】/【C++额外】四档 + 双侧 file:line 证据）→ 差异汇总表 → 结论。后续任一侧文件修改后，只需复核对应编号的记录并更新其差异表。

## 文件映射清单（比对顺序）

进度：`[ ]` 未开始 / `[~]` 进行中 / `[x]` 已完成并有记录

### A. geometry 基础层

| # | Rust | C++ | 进度 |
|---|---|---|---|
| 01 | `geometry/primitives/point.rs` | `Geometry/Point.h` | [x] |
| 02 | `geometry/primitives/rect.rs` | `Geometry/Rect.h` | [x] |
| 03 | `geometry/primitives/edge.rs` | `Geometry/Edge.h` + `Source/Geometry/Primitives/Edge.cpp` | [x] |
| 04 | `geometry/primitives/circle.rs` | `Geometry/Circle.h` + `Source/Geometry/Primitives/Circle.cpp` | [x] |
| 05 | `geometry/primitives/simple_polygon.rs` | `Geometry/SimplePolygon.h` + `Source/Geometry/Primitives/SimplePolygon.cpp` | [x] |
| 06 | `geometry/transformation.rs` + `d_transformation.rs` | `Geometry/Transformation.h` + `Source/Geometry/Transformation/Transformation.cpp` | [x] |
| 07 | `geometry/convex_hull.rs` | `Geometry/ConvexHull.h` + `Source/Geometry/ConvexHull/ConvexHull.cpp` | [x] |
| 08 | `geometry/geo_enums.rs` + `geo_traits.rs` | 分散于 `Rect.h`、`Item.h`、命名约定 | [x] |

### B. 形状处理与 fail_fast 层

| # | Rust | C++ | 进度 |
|---|---|---|---|
| 09 | `geometry/original_shape.rs` | `Geometry/OriginalShape.h` + `Source/Geometry/OriginalShape/OriginalShape.cpp` | [x] |
| 10 | `geometry/shape_modification.rs`（simplify 部分） | `Geometry/ShapeModification.h` + `Source/Geometry/ShapeModification/Simplify.cpp` | [x] |
| 11 | `geometry/shape_modification.rs`（D5 `offset_shape`，使用`geo::Buffer`） | `Source/Geometry/ShapeModification/Offset.cpp` + `StraightSkeleton.cpp/.h` | [x] |
| 12 | `geometry/shape_modification.rs`（narrow concavity 部分） | `Source/Geometry/ShapeModification/NarrowConcavity.cpp` | [x] |
| 13 | `geometry/fail_fast/pole.rs` | `Geometry/PointOfInaccessibility.h` + `Source/Geometry/Primitives/PointOfInaccessibility.cpp` + `Source/FailFast/Surrogate.cpp`（generate poles） | [x] |
| 14 | `geometry/fail_fast/piers.rs` | `Source/FailFast/Surrogate.cpp`（piers 部分） | [x] |
| 15 | `geometry/fail_fast/sp_surrogate.rs` | `FailFast/Surrogate.h` + `Source/FailFast/Surrogate.cpp`（其余部分） | [x] |

### C. collision_detection 层

| # | Rust | C++ | 进度 |
|---|---|---|---|
| 16 | `collision_detection/hazards/hazard.rs` | `Collision/Hazard.h` | [x] |
| 17 | `collision_detection/hazards/filter.rs` | `Collision/HazardFilter.h` | [x] |
| 18 | `collision_detection/hazards/collector.rs` | `Collision/HazardCollector.h` + `Source/Collision/Hazards/Collector.cpp` | [x] |
| 19 | `collision_detection/quadtree/qt_node.rs` + `qt_traits.rs` | `Collision/Quadtree.h` + `Source/Collision/Quadtree/Quadtree.cpp`（node/query 部分） | [x] |
| 20 | `collision_detection/quadtree/qt_hazard.rs` + `qt_partial_hazard.rs` + `qt_hazard_vec.rs` | `Source/Collision/Quadtree/Quadtree.cpp`（presence/classify 部分） | [x] |
| 21 | `collision_detection/quadtree/assertions.rs` | `Source/Support/Assertions.cpp`（quadtree 校验） | [x] |
| 22 | `collision_detection/cd_engine.rs` | `Collision/CollisionEngine.h` + `Source/Collision/CollisionEngine.cpp` | [x] |

### D. entities 层

| # | Rust | C++ | 进度 |
|---|---|---|---|
| 23 | `entities/item.rs` | `Entities/Item.h` + `Source/Entities/Item.cpp` | [x] |
| 24 | `entities/container.rs` | `Entities/Container.h` + `Source/Entities/Container.cpp` | [x] |
| 25 | `entities/placed_item.rs` | `Entities/PlacedItem.h` + `Source/Entities/PlacedItem.cpp` | [x] |
| 26 | `entities/layout.rs` | `Entities/Layout.h` + `Source/Entities/Layout.cpp` | [x] |

### E. probs 层

| # | Rust | C++ | 进度 |
|---|---|---|---|
| 27 | `probs/spp/entities/*.rs`（strip/instance/problem/solution） | `Problems/Spp.h` + `Source/Problems/SPP/*.cpp` | [x] |
| 28 | `probs/spp/util/assertions.rs` | `Source/Support/Assertions.cpp`（SP 校验） | [x] |
| 29 | `probs/bpp/entities/*.rs` | `Problems/Bpp.h` + `Source/Problems/BPP/*.cpp` | [x] |
| 30 | `probs/bpp/util/assertions.rs` | `Source/Support/Assertions.cpp`（BP 校验） | [x] |
| 31 | `probs/mspp/entities/*.rs` | `Problems/MSpp.h` + `Source/Problems/MSPP/*.cpp` | [x] |
| 32 | `probs/mspp/util/assertions.rs` | `Source/Support/Assertions.cpp`（MSP 校验） | [x] |

### F. io 层

| # | Rust | C++ | 进度 |
|---|---|---|---|
| 33 | `io/ext_repr.rs` | `IO/ExternalRepr.h` + `Source/IO/ExternalRepr.cpp` | [x] |
| 34 | `io/import.rs` | `IO/Importer.h` + `Source/IO/Importer.cpp` | [x] |
| 35 | `io/export.rs` | `Source/IO/JsonCodec.cpp`（导出部分） | [x] |
| 36 | `io/svg/*.rs` | `IO/SvgExport.h` + `Source/IO/SvgExport.cpp` | [x] |
| 37 | `probs/*/io/*.rs`（三组 ext_repr/import/export） | `Source/IO/JsonCodec.cpp` + `Importer.cpp`（prob 部分） | [x] |

### G. util 与杂项

| # | Rust | C++ | 进度 |
|---|---|---|---|
| 38 | `util/fpa.rs` | `Geometry/RustF32.h`（Importer比较直接委托公共比较器） | [x] |
| 39 | `util/assertions.rs` | `Assertions.h` + `Source/Support/Assertions.cpp` | [x] |
| 40 | `lib.rs`（公共配置、features） | `NumericPolicy.h`、`Result.h`、`Version.h` 等 | [x] |

## 记录索引

已完成 38 份记录（30 号并入 28/29，32 号并入 31），共约 3900 行。判定分布：行为等价 ×1、结果等价/实现不同 ×2、wire 基本一致 ×2、基本等价 ×28、不等价 ×5（06/11/21/36/40）。

| # | 记录 | 判定 | 差异条数 |
|---|---|---|---|
| 01 | [records/01-point.md](records/01-point.md) | 基本等价 | 6 |
| 02 | [records/02-rect.md](records/02-rect.md) | 基本等价 | 5 |
| 03 | [records/03-edge.md](records/03-edge.md) | 基本等价 | 5 |
| 04 | [records/04-circle.md](records/04-circle.md) | 基本等价 | 5 |
| 05 | [records/05-simple-polygon.md](records/05-simple-polygon.md) | 基本等价 | 9 |
| 06 | [records/06-transformation.md](records/06-transformation.md) | **部分等价**（已增加私有f32旋转矩阵状态并通过有限输入定向复核；一般仿射与完整角度/非有限域仍未覆盖） | 8 |
| 07 | [records/07-convex-hull.md](records/07-convex-hull.md) | 基本等价 | 4 |
| 08 | [records/08-geo-enums-traits.md](records/08-geo-enums-traits.md) | 基本等价 | 7 |
| 09 | [records/09-original-shape.md](records/09-original-shape.md) | 基本等价 | 7 |
| 10 | [records/10-simplify.md](records/10-simplify.md) | 基本等价 | 11 |
| 11 | [records/11-offset.md](records/11-offset.md) | 部分对齐（D5锁定基线下25个固定有限配置；通用overlay与fallback仍未收口） | 11 |
| 12 | [records/12-narrow-concavity.md](records/12-narrow-concavity.md) | 基本等价 | 12 |
| 13 | [records/13-pole.md](records/13-pole.md) | 基本等价 | 12 |
| 14 | [records/14-piers.md](records/14-piers.md) | 基本等价（空候选报错 eef0830 已对齐；缺"端点在形外"断言） | 15 |
| 15 | [records/15-sp-surrogate.md](records/15-sp-surrogate.md) | 基本等价 | 13 |
| 16 | [records/16-hazard.md](records/16-hazard.md) | 基本等价 | 5 |
| 17 | [records/17-hazard-filter.md](records/17-hazard-filter.md) | 基本等价（from_irrelevant_qzones 无直接对应） | 6 |
| 18 | [records/18-collector.md](records/18-collector.md) | 基本等价（bloom 快筛无对应） | 6 |
| 19 | [records/19-quadtree-node.md](records/19-quadtree-node.md) | 逐行结构移植（2026-09-29 重写：懒扩展 constrict + 边子集 + activeEdges；旧“全量重建”差异已消除） | 9 |
| 20 | [records/20-quadtree-presence.md](records/20-quadtree-presence.md) | 逐行等价（2026-09-29 重写后 Constrict 含邻居传播/质心回退） | 7 |
| 21 | [records/21-quadtree-assertions.md](records/21-quadtree-assertions.md) | **不等价**（4 项 quadtree 专项自检缺失，多为架构豁免） | 6 |
| 22 | [records/22-cd-engine.md](records/22-cd-engine.md) | 逐行等价（2026-09-29 重写：短路 detect/containment/surrogate 树内查询；collect-any 差异已消除，Restore 仍为重建式但结果等价） | 13 |
| 23 | [records/23-item.md](records/23-item.md) | 基本等价 | 5 |
| 24 | [records/24-container.md](records/24-container.md) | 基本等价 | 8 |
| 25 | [records/25-placed-item.md](records/25-placed-item.md) | 基本等价 | 6 |
| 26 | [records/26-layout.md](records/26-layout.md) | 基本等价 | 14 |
| 27 | [records/27-spp.md](records/27-spp.md) | 基本等价（Restore 前置校验已删除对齐 Rust；demand 耗尽 debug-only） | 10 |
| 28 | [records/28-prob-assertions.md](records/28-prob-assertions.md) | 基本等价（SP/BP 合一） | 10 |
| 29 | [records/29-bpp.md](records/29-bpp.md) | 基本等价（LayoutTargetKind 注释写反为文档错误） | 12 |
| 31 | [records/31-mspp.md](records/31-mspp.md) | 基本等价（Restore strip 校验已删除对齐 Rust） | 13 |
| 33 | [records/33-ext-repr.md](records/33-ext-repr.md) | wire 基本一致 | 7 |
| 34 | [records/34-import.md](records/34-import.md) | 基本等价（非连续重复顶点由下游构造拒绝，诊断不同） | 12 |
| 35 | [records/35-export.md](records/35-export.md) | wire 基本一致（SPP container_id 双侧实际均为 0；min_item_separation 零值差异仍在） | 7 |
| 36 | [records/36-svg.md](records/36-svg.md) | **不等价**（有意重写：y-flip/viewBox/图层公式；quality_zones 已补，quadtree/可见标签/开关仍缺） | 15 |
| 37 | [records/37-prob-io.md](records/37-prob-io.md) | 基本等价 | 14 |
| 38 | [records/38-fpa.md](records/38-fpa.md) | 核心算法及Importer相邻顶点比较对齐pinned float-cmp 0.10.0；整体多边形非有限契约另核 | 7 |
| 39 | [records/39-assertions.md](records/39-assertions.md) | 基本等价 | 9 |
| 40 | [records/40-lib-config.md](records/40-lib-config.md) | **不等价**（结构层：CDEConfig 默认值 4/32 vs Rust 无默认/sparrow 用 4/64；无 feature 门控/Instant） | 9 |

## 汇总结论

逐文件比对完成（38 份记录，覆盖 Rust 侧全部 79 个源文件 → C++ 40 个映射）。总体判定与全局比对一致，但每条差异现在有了文件级出处与证据行号：

1. **正确性风险**（结果可能分叉，3 项）：
   - **offset（#11，唯一算法本体替换）**：geo-buffer → 私有 straight-skeleton，C++ 额外的保持性强校验 + fallback + Deflate POI 半径判定使失败边界不同（Rust warn 取首组件 vs C++ 可能 OffsetFailed）。
   - **Transformation（#06）**：Rust公开3×3矩阵API与C++旋转+平移API仍有形态差异；C++现以私有四系数f32状态传播刚体变换的合成/求逆结果，修复了定向有限输入探针观察到的点应用超容差。Importer、SVG和JSON的生产调用深度已静态核对；完整外部角度域、非有限输入和所有调用点的偏差预算仍未收口。
   - **cd-engine（#22，两条已定位的判定分歧）**：HazardCollides(Exterior, depth>0) 全顶点检查比 Rust POI 捷径更严（病态凹形判定相反）；surrogate pole 完全在容器外且不贴边时 Rust 报碰撞、C++ 报不碰撞（上层 poly 检测兜底使端到端仍一致）。Edge::try_new 双 NaN 端点（#03）及多边形构造器对非有限坐标的失败路径（#34）是边缘输入风险；Importer相邻点比较谓词本身已与Rust一致（#38）。
2. **性能风险**（结果同、复杂度不同）：四叉树全量重建 vs 增量 constrict（#19/#20），每步 place/remove O(N)；DetectPolygonCollision collect-any 非短路（#22）；Restore 全量重建（#22）。
3. **wire/输出格式差异**：min_item_separation 零值序列化（#35，Rust 总是输出 vs C++ 省略零值）；SVG 整体（#36，非兼容重写）；Solution time_stamp 缺失与无 feature 门控（#40）。
4. **架构豁免的检查缺失**：quadtree 4 项自检（#21，重建式架构下部分不适用）、密度/成本一致性断言（#28/31，被结构匹配校验覆盖）、非连续重复顶点检查（#34）。
5. **API 缺口清单**（供 SparrowCore/lbf 后续移植参考）：normalize_rotation（#06）、convex_hull_indices/from_surrogate（#07）、From<Rect>（#05）、TransformableFrom（#05/06/08）、ff_poles/ff_piers 前缀切片（#15）、from_irrelevant_qzones（#17）、cde()/is_empty()（#26）、item_placed_qtys/bin_used_qtys（#29）、all_layouts()（#31）。
6. **C++ 额外物**（Rust 无，调用方需自知）：double 无后缀 API 族与 Intersects(tolerance) 共线重叠语义（#02/03/05）、NumericPolicy 容差体系（#05/38）、SoA/AVX2/ThreadPool 并行批量（#22）、OffsetShapes 全组件 API（#11）、Result 错误通道（全局）、CDEConfig 默认值（#40，注意与 sparrow 实际用法 4/64 不一致）。

**对全局比对结论的修正**（逐文件验证发现的新事实）：

- 证伪了两条旧结论：Rect centroid的“double中转1ULP差”不存在（与Rust同路径）；SimplePolygon ±0.0/NaN重复点判定“相反”不成立（itertools::unique走PartialEq，与C++ `==`同语义）。Edge/Circle centroid曾登记1ULP差；调整后的x86_64 helper已与pinned Rust 1.98.1在10,012组有限输入上逐位一致，当前Release调用路径fixture也通过。但fixture只用4-ULP比较，旧的具体1ULP调用点向量未复现，仍保留为待复验项；NaN分歧仍见Edge::try_new与面积零容差。
- 新发现：CDEConfig 默认值与 sparrow 参考用法不一致（4/32 vs 4/64）；`AlmostEqualRustF32` 已按 pinned float-cmp 0.10.0 对齐非有限 ULP行为，Importer相邻点比较现对所有值直接委托该公共函数；非有限多边形整体失败路径仍单独核对；LayoutTargetKind注释与实现相反（文档错误，行为一致）。

**建议的收敛顺序**（按风险×成本）：

1. Transformation 的完整调用点偏差预算、外部角度域与非有限输入契约；有限矩阵及Importer形态定向探针已验证私有矩阵状态修复，后续按E1逐项收口剩余输入域。
2. min_item_separation 零值序列化——一处改动 + round-trip 测试。
3. offset 失败边界——补真实 CAD 病态轮廓的差分 fixture（等 Dxf00 回归一起做）。
4. cd-engine 两条判定分歧——均有上层兜底，可作为低优先修缮。
5. 四叉树增量式——等性能数据决定是否值得动架构。
