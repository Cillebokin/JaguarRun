# JaguarCore 与 jagua-rs 公开 API 映射（B0 阶段记录）

状态：B0 静态映射完成；E4 基线验收完成 · 更新日期：2026-10-17 · 证据类型：锁定源码静态核对

本文记录 B0 的静态逐项映射结果。已按 E4 锁定 revision 复核 Rust 导出闭包，并将 crate 外可达类型、显式字段/方法/变体和 C++ 落点纳入台账；需由 B1–B15 验收的运行语义在相应工作项中处理。本文不把签名和类型落点等同于行为等价。

## 基线与盘点方法

- Rust 基线为 E4 两处 `Cargo.lock` 解析到的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`；本次从该提交的只读源码快照复核，来源：[jagua-rs pinned revision](https://github.com/JeroenGar/jagua-rs/tree/f5e2b5ffd14931f17a20ce283a2122e877953ceb/jagua-rs)。revision 记录位于 `JaguarCore/tools/RustOracle/Cargo.lock` 与 `SparrowCore/tests/RustReference/Cargo.lock`；Rust 1.98.1 分别由两个目录中的 `rust-toolchain.toml` 指定。
- 对 pinned 快照与 `CodeResource/jagua-rs/jagua-rs/src` 做换行归一化后逐文件比较：两侧各 85 个 Rust 源文件，78 个相同，7 个不同：`geometry/primitives/simple_polygon.rs`、`geometry/shape_modification.rs`、`probs/mspp/entities/problem.rs`、`probs/mspp/entities/strip.rs`、`probs/spp/entities/problem.rs`、`probs/spp/entities/strip.rs`、`probs/spp/io/import.rs`。原始字节哈希差异会受换行影响，不能直接当作源码逻辑差异。
- 上述 7 个差异文件中，模块声明、重导出路径未变化；公开声明文本的签名差异集中在 SPP/MSPP `Problem` 方法，详见下文。`simple_polygon.rs`、`shape_modification.rs`、两个 `strip.rs` 与 SPP `io/import.rs` 仍存在实现或诊断文本漂移，不能据此宣称行为一致。
- pinned crate 的 `Cargo.toml` 依赖 `geo = { workspace = true }`；本地镜像改为 `geo-buffer = { workspace = true }`，且另有 `homepage` 字段。这与 `shape_modification.rs` 的实现差异相关，是 E4/B2 后续核查项。
- C++ 公开头文件以 `JaguarCore/Include/JaguarCore` 为准；`JaguarCore/JaguarCore.pro` 的 `INCLUDEPATH` 指向 `JaguarCore/Include`。
- 对 `JaguarCore/Include/JaguarCore` 与 `JaguarCore/include/JaguarCore` 两份 33 个头文件做了相对路径和 SHA-256 静态比对，当前文件名集合与文件内容均一致。该结果只说明本次盘点时两份副本相同，不说明大小写目录可以从仓库中删除。
- Rust `lib.rs` 暴露 collision_detection、entities、geometry、io、util、probs 六个根模块及 Instant 类型别名。probs 下的 SPP、BPP、MSPP 分别受 spp、bpp、mspp feature 控制。
- 对 pinned 与本地 `src` 使用同一文本规则清点，双方均匹配到 479 行 `pub` 模块/重导出/类型/常量/函数声明，以及 197 行具名公开字段。该计数是原始文本扫描，不等同于 rustdoc/API dump；其中位于私有模块或受条件编译控制的项必须按导出图判断，宏生成项也未由编译器展开。

## 模块级映射初审

| Rust 公开面 | C++ 头文件与主要符号 | 初审结论 |
|---|---|---|
| `geometry::primitives`、`geometry::transformation`、`geometry::original_shape` | `Geometry/Point.h`、`Edge.h`、`Rect.h`、`Circle.h`、`SimplePolygon.h`、`Transformation.h`、`OriginalShape.h` | 主要几何类型有对应实现；Rust `Point/Edge/Rect/Circle/SPolygon/Transformation` 对应 C++ `Point2d/Edge2d/Rect2d/Circle2d/SimplePolygon/Transform2d`，命名及公开成员形态不同。`DTransformation` 的旋转和平移映射到 `Transform2d`，B1 已确认不需独立结构；`normalize_rotation` 映射到 `NormalizeRotationRustF32`。 |
| `geometry::geo_enums`、`geo_traits`、`convex_hull`、`shape_modification`、`fail_fast` | `Geometry/Rect.h`、`ConvexHull.h`、`ShapeModification.h`、`PointOfInaccessibility.h`、`FailFast/Surrogate.h` | 几何关系、旋转、变换、凸包、形状修改和 surrogate 能力大体有 C++ 入口；Rust trait 与 C++ 具体成员函数/自由函数不是一对一类型映射。`GeoRelation` 与 `RectRelation`、surrogate 类型等仍需比较变体、签名和失败语义。 |
| `collision_detection`、`hazards`、`quadtree` | `Collision/CollisionEngine.h`、`Hazard.h`、`HazardFilter.h`、`HazardCollector.h`、`Quadtree.h` | 核心引擎、hazard、过滤、收集和四叉树均有公开头文件。Rust `CDEngine/CDESnapshot` 与 C++ `CollisionEngine/CollisionSnapshot` 名称不同；Rust collector/filter/query trait 与 C++ 类型的抽象方式不同。不能仅凭头文件存在判为等价。 |
| `entities` | `Entities/Item.h`、`Container.h`、`PlacedItem.h`、`Layout.h` | Item、Container、PlacedItem、Layout 和快照均可找到对应类型。`InferiorQualityZone`/`QualityZone`、Rust key/ID 与 C++ handle、容器身份及 restore 的语义仍需逐方法核对。 |
| `io::ext_repr`、`io::import`、`io::export` | `IO/ExternalRepr.h`、`Importer.h`、`JsonCodec.h` | 外部表示和导入器有对应类型与入口；Rust `Ext*` 与 C++ `External*` 命名不同，字段、默认值、拒绝条件和转换失败出口待按 B8、B14 逐项核验。 |
| `io::svg` | `IO/SvgExport.h` | Rust `SvgDrawOptions/Color/layout_to_svg*` 与 C++ `SvgExportOptions/SvgColor/ExportLayoutSvg` 形态不同；主题字段、坐标和错误返回需与 SVG 对拍结论关联。 |
| `probs::spp` | `Problems/Spp.h` | `Strip`、`SPInstance`、`SPPlacement`、`SPProblem`、`SPSolution` 均有对应公开面；`SPProblem` 的主要成员已逐项映射，Rust 公开状态字段与 C++ 封装状态的形态不同，见下表。 |
| `probs::bpp` | `Problems/Bpp.h` | Bin、BPInstance、BPPlacement、BPProblem、BPSolution 有对应公开面；Rust `BPLayoutType` 与 C++ `LayoutTargetKind` 等名称/枚举映射待核，完整行为见 B7。 |
| `probs::mspp` | `Problems/Mspp.h` | `MSPInstance`、`MSPPlacement`、`MSPProblem`、`MSPSolution` 均有对应公开面；Rust `Strip` 对应 C++ `MultiStrip`，`LayKey` 对应索引/代数句柄策略；`MSPProblem` 的主要成员已逐项映射，见下表。 |
| `util` 与断言模块 | `Assertions.h`、`NumericPolicy.h`、`RustF32.h`、`Result.h` | Rust `FPA`、公开断言函数与 C++ 数值策略/断言/错误结果类型不能按名称直接等同；断言用途、容差和是否属于产品 API 待分类。 |

## E4 锁定 revision 的导出图复核

逐层检查 pinned `lib.rs`、各公开 `mod.rs`、`pub mod` 与 `pub use` 后，crate 根导出图与本地镜像一致。问题模块仍由 Cargo feature 控制：`spp`、`bpp`、`mspp`。下表记录 pinned 下 crate 外可达的主要路径与 C++ 落点；同名或可映射不代表签名和行为等价。

| pinned Rust 可达路径与导出项 | C++ 落点 | 映射备注 |
|---|---|---|
| 根：`Instant`；`collision_detection`、`entities`、`geometry`、`io`、`probs`、`util` | JaguarCore 对应的 Collision、Entities、Geometry、IO、Problems 公共头；无 Rust `Instant` 的直接同名 C++ 类型 | crate 根的六个模块和别名路径均在 pinned 源码中确认；C++ 时间戳由其自身表示承接。 |
| `geometry::primitives::{Circle, Edge, Point, Rect, SPolygon}`；`Transformation`、`DTransformation`、`OriginalShape`、`normalize_rotation`；`convex_hull`、`geo_enums`、`geo_traits`、`shape_modification`、`fail_fast::{generate_piers, generate_surrogate_poles, compute_pole, SPSurrogate, SPSurrogateConfig}` | `Geometry/{Circle,Edge,Point,Rect,SimplePolygon,Transformation,OriginalShape,ConvexHull,ShapeModification,PointOfInaccessibility}.h`、`FailFast/Surrogate.h` | `DTransformation` 的数据由 `Transform2d` 承接，Rust 独立公开 helper `normalize_rotation` 对应 `NormalizeRotationRustF32`；若干 Rust trait 仍通过 C++ 具体成员函数组合映射。 |
| `collision_detection::{CDEConfig, CDESnapshot, CDEngine}`；`hazards::{HazKey, Hazard, HazardEntity}`；`quadtree::{QTHazPresence, QTHazard, QTNode, QTQueryable}` | `Collision/{CollisionEngine,Hazard,HazardFilter,HazardCollector,Quadtree}.h` | C++ `CollisionEngine/CollisionSnapshot` 与 Rust 命名不同；部分 Rust trait/collector/query 由 C++ 具体对象和回调承接。 |
| `entities::{Item, Layout, ContainerMismatch, LayoutSnapshot, PlacedItem, PItemKey, Container, InferiorQualityZone, N_QUALITIES}` | `Entities/{Item,Layout,PlacedItem,Container}.h` 与 `Collision/*` | Rust key/ID 与 C++ handle 不同；`ContainerMismatch`、质量区和 restore 的可观察契约需要按 B3/B7 对照。 |
| `io::{ext_repr, import, export, svg}`；`svg` 重导出 `layout_to_svg*`、`Color`、`SvgDrawOptions`、`SvgLayoutTheme` | `IO/{ExternalRepr,Importer,JsonCodec,SvgExport}.h` | Rust `Ext*` 与 C++ `External*` 适配；SVG 绘制辅助项不一定是 C++ 独立公开 API。 |
| `util::{FPA, assertions::*}` | `Assertions.h`、`NumericPolicy.h`、`RustF32.h`、`Result.h` | 公开断言函数属于 crate 可达面，但不自动等于 JaguarCore 产品接口；按用途区分移植目标与 Rust 内部验证支持。 |
| `probs::spp::entities::{SPInstance, SPPlacement, SPProblem, SPSolution, Strip}`；`io::{export, import_instance, import_solution}` | `Problems/Spp.h`、`IO/Importer.h`、`IO/JsonCodec.h` | SPP feature 开启时可达；详细成员映射见下一节。 |
| `probs::bpp::entities::{Bin, BPInstance, BPLayoutType, BPPlacement, BPProblem, LayKey, BPSolution}`；`io::{export, import_instance, import_solution}` | `Problems/Bpp.h`、`IO/Importer.h`、`IO/JsonCodec.h` | BPP feature 开启时可达；`BPLayoutType`/`LayKey` 与 C++ 布局种类/索引的签名映射仍需逐成员登记。 |
| `probs::mspp::entities::{MSPInstance, LayKey, MSPPlacement, MSPProblem, MSPSolution, Strip}`；`io::{export, import_instance, import_solution}` | `Problems/Mspp.h`、`IO/Importer.h`、`IO/JsonCodec.h` | MSPP feature 开启时可达；`LayKey` 是 slotmap 代数 key，C++ 以索引及 generation 保护失效句柄。 |

## 几何、碰撞与实体成员映射

下表把公开类型及其主要公开成员落到 C++ 类型。类内所有 Rust `pub` 字段/方法仍需按签名列和数值/失败用例继续核对；这里的“对应”仅表示可定位的 C++ API 落点。

| pinned Rust 类型 / 公开成员组 | C++ 落点 | B0 映射结论 |
|---|---|---|
| `Point(x, y)`；`x()`、`y()` | `Point2d` 字段/坐标访问 | 直接类型映射，Rust 是 tuple struct；C++ 字段访问方式不同。 |
| `Edge { start, end }`；`try_new`、`extend_at_front/back`、`scale`、`reverse`、`collides_at`、`closest_point_on_edge`、`x/y_min/max`、`length`、`centroid`、`bbox` | `Edge2d::{Create,ExtendedAtFrontRustF32,ExtendedAtBackRustF32,ScaledRustF32,ReversedRustF32,IntersectionPointRustF32,ClosestPointRustF32,XMin/YMin/XMax/YMaxRustF32,LengthRustF32,CentroidRustF32,BoundingBox}` | 类型对应、公开方法名重写；Rust `collides_at` 与 C++ intersection-point 方法映射，`f32` 语义使用 RustF32 系列。 |
| `Circle { center, radius }`；`try_new`、`bounding_circle`、`area`、`bbox`、`diameter` | `Circle2d::{Create,BoundingCircleRustF32,AreaRustF32,BoundingBoxRustF32,DiameterRustF32}` | 类型/成员对应；Rust 字段公开，C++ 值类型公开字段；距离、包含和相交 trait 操作由 C++ 方法补齐。 |
| `Rect { x_min, y_min, x_max, y_max }`；`try_new/from_diagonal_corners/relation_to/almost_relation_to/inflate_to_square/scale/resize_by/quadrants/corners/sides/edges/width/height/intersection/bounding_rect/centroid/area/diameter`；常量 `QUADRANT_NEIGHBOR_LAYOUT` | `Rect2d`；`RectRelation`、`GeoPosition`；`Rect2d` 的 Relation/AlmostRelation、geometry helper methods | `Rect`→`Rect2d`；`GeoRelation`→`RectRelation`，`GeoPosition` 同名。`QUADRANT_NEIGHBOR_LAYOUT` 未找到同名公共项；公共字段与 C++ `xMin/...` 字段命名不同。另有已裁决差异：Rust 在 f32 子象限退化时 panic，C++ `QuadrantsRustF32()` 返回退化子矩形；保留现有冻结签名并限定等价域，详见 B1 第八轮。 |
| `SPolygon { vertices, bbox, area, diameter, poi, surrogate }`；`new/generate_surrogate/vertex/edge/edge_iter/n_vertices/surrogate/calculate_diameter/generate_bounding_box/calculate_area/calculate_poi/centroid` | `SimplePolygon::{Create,Vertices,BoundingBox,Area,Diameter,PointOfInaccessibility,GenerateSurrogate,GetSurrogate,Edges,VertexCount,Centroid}` | 主要能力有落点，但 Rust 公开派生缓存字段，C++ 通过只读 accessor 封装。四个接受任意原始点列的 Rust helper 没有同形 C++ 公共 API；按冻结边界保留为 API 映射差异，合法多边形缓存值仍有对拍证据，详见 B1 第八轮。 |
| `Transformation`：`empty/from_translation/from_rotation/rotate/translate/rotate_translate/translate_rotate/transform/transform_from_decomposed/inverse/is_empty/matrix/decompose`；`DTransformation`：`new/compose/rotation/translation`；`normalize_rotation` | `Transform2d::{Identity,FromRotationTranslation,ApplyRustF32,InverseRustF32,FollowedByRustF32}`；`NormalizeRotationRustF32` | `Transformation` 可映射 `Transform2d`，但矩阵表示与旋转+平移表示不同；`DTransformation` 的分解数据由 `Transform2d` 承接，`normalize_rotation` 有直接对应的 C++ helper；精度与规范化见 B1。 |
| `OriginalShape` 公开字段与 `convert_to_internal/centroid/area/bbox/diameter`；`ShapeModifyMode/ShapeModifyConfig`；`simplify_shape/shape_modification_valid/offset_shape/close_narrow_concavities` | `OriginalShape` 的公开字段、其 `shape` 所属的 `SimplePolygon` 查询器；`ShapeModifyMode/ShapeModifyConfig`；`SimplifyShape`、`ShapeModificationValid`、`OffsetShape(s)`、`CloseNarrowConcavities` | 类型和主要函数有落点；`OriginalShape` 的便捷查询通过其 polygon 字段组合，不是同名 C++ 方法。pinned 使用 `geo::Buffer`，本地镜像使用 `geo-buffer`，同名接口不能替代 B2 的算法对拍。 |
| `GeoRelation`、`GeoPosition`、`RotationRange`；`CollidesWith`、`AlmostCollidesWith`、`DistanceTo`、`SeparationDistance`、`Transformable`、`TransformableFrom` traits | `RectRelation`、`GeoPosition`、`RotationRange`；各 geometry 类型的具体成员方法、`Item` 旋转配置 | enum 有直接/改名映射；Rust trait 没有同构 C++ trait/interface，按实现到的具体类型方法逐项归属。 |
| `convex_hull_from_points/convex_hull_from_surrogate/convex_hull_indices`；`SPSurrogate { poles, piers, convex_hull_indices, convex_hull_area, config }` 的 `new/ff_poles/ff_piers`；`SPSurrogateConfig` 的 `none`；`generate_piers/generate_surrogate_poles/compute_pole` | `ConvexHull`、`Surrogate/SurrogateConfig`、`Surrogate::Create` 与 `Poles/Piers/ConvexHull*` accessor | surrogate 类型改名；构造、分解函数在 C++ 收敛为类工厂和 accessor，非一对一自由函数。 |
| `CDEConfig`、`CDEngine`、`CDESnapshot`；`new/hazards/register_hazard/deregister_hazard_by_key/deregister_hazard_by_entity/detect_poly_collision/detect_containment_collision/detect_surrogate_collision/collect_poly_collisions/collect_poly_collisions_until/collect_surrogate_collisions/collect_surrogate_collisions_until/save/restore`，以及 `bbox/get_virtual_root/haz_key_from_pi_key` | `CDEConfig`、`CollisionEngine`、`CollisionSnapshot`；`RegisterHazard/DeregisterHazard`、`DetectPolygonCollision(s)`、`CollectPolygonCollisions*`、`DetectSurrogateCollision`、`Save/Restore` | 类型改名和错误模型不同；Rust `hazards`、按 entity 删除、`bbox/get_virtual_root/haz_key_from_pi_key` 等没有完全同形 C++ 公共入口，C++ 另有 batch/parallel 扩展。 |
| `Hazard { entity, shape, dynamic }` 与 `new/scope`；`HazardEntity` 变体；`HazKey`；`HazardFilter::is_irrelevant`、`HazKeyFilter::{from_irrelevant_qzones,from_keys}`、`NoFilter`；`HazardCollector` trait 与 `BasicHazardCollector::{new,with_capacity,insert,remove,retain,clear,len,is_empty,contains_key,iter}`；`QTHazPresence/QTHazard/QTNode/QTQueryable` 及其 `constrict/from_root/n_edges/register/deregister/collect/collides` 操作 | `Hazard/HazardEntity/HazardHandle/HazardKind`、`HazardFilter/NoFilter`、`HazardCollector`、`QuadTree/HazardPresence` | key 与 handle 改名；Rust filter/collector/query traits 与 C++ concrete callback/collector 形态转换；`QTHazPresence`→`HazardPresence`，`QTQueryable` 无独立 C++ 接口。 |
| `Item { idx, external_id, shape_orig, shape_cd, allowed_rotation, min_quality, surrogate_config }`；`new/area`；`Container { outer_orig, outer_cd, quality_zones, base_cde }` 的 `new/area`；`InferiorQualityZone { quality, shapes_orig, shapes_cd }` 的 `new/to_hazards/area` | `Item` 的 `Create/Index/ExternalId/GetOriginalShape/CollisionShape/GetSurrogateConfig/MinQuality/AllowedRotation/Area`；`Container/QualityZone` 的 `Create/Id/Area/GetOriginalShape/CollisionShape/QualityZones/Bounds` | Rust 字段公开，C++ 通过 getter 封装；Importer中的Rust `quality_zones` 是按quality索引的固定数组，且空质量组也占槽；C++ `QualityZones()`/`OriginalQualityZones()` 返回按quality分组的vector并省略Importer产生的空组。仓内hazard与SVG消费者只处理实际shape，故空组不改变这些运行行为；调用方读取数组/向量长度仍可观察表示差异。`InferiorQualityZone`→`QualityZone`，`shapes_orig/shapes_cd` 在 C++ 采用不同组合表示。 |
| `Layout { container, placed_items }`；`new/from_snapshot/swap_container/save/restore/place_item/remove_item/is_empty/density/placed_item_area/items/cde/is_feasible`；`LayoutSnapshot { container, placed_items, cde_snapshot }` 的 `density/placed_item_area`；`ContainerMismatch` | `Layout::{Layout,PlaceItem,RemoveItem,SwapContainer,IsFeasible,PlacedItemArea,Density,Save,Restore,GetContainer,PlacedItemCount,Items}`；`LayoutSnapshot`；`Result` error | Rust `SlotMap` 字段无 C++ 直出，C++ 操作返回 `Result`；快照 map 对 vector 的布局及 `ContainerMismatch` 错误包装要按 B3/B7 核对。 |
| `PItemKey`；`PlacedItem { item, d_transf, shape }`；`PlacedItem::new` | `PlacedItemHandle`；`PlacedItem::{Create,GetItem,GetItemIndex,Transform,CollisionShape,Area}` | key 改为 handle；Rust 放置项直接公开内部 item/变换/shape，C++ 提供只读访问器。 |
| `util::FPA(pub f32)` 的 `From/PartialEq/PartialOrd/Display`；`snapshot_matches_layout/collision_hazards_sorted_correctly/qt_contains_no_dangling_hazards/layout_qt_matches_fresh_qt/quadrants_have_valid_layout/print_layout`；SPP/MSPP `problem_matches_solution/instance_item_indices_correct`；BPP `problem_matches_solution/instance_item_bin_ids_correct` | `NumericPolicy.h`、`RustF32.h`、`Assertions.h` 的 `ValidateLayout/Validate*Solution/DebugAssert*` | `FPA` 是公开容差比较包装，无同名 C++ 类型；C++ 验证接口返回 `Result<void>`，Rust assertion helpers 返回 `bool`/打印。此组需标为内部验证支持还是产品 API；不能把 `Assertions.h` 的同类用途直接算作一一等价。 |

## Problem 状态 API 的 pinned 签名差异

### SPP

| pinned Rust 项 | C++ 对应项 | 映射结论 |
|---|---|---|
| `SPProblem::new(SPInstance) -> Result<Self>` | `SPProblem::Create(SPInstance) -> Result<SPProblem>` | 名称适配；两边都表达构造失败。**本地镜像返回 `Self`，少了 pinned 的 `Result`**。 |
| `change_strip_width(f32) -> Result<()>`、`fit_strip() -> Result<()>` | `ChangeStripWidth(double) -> Result<void>`、`FitStrip() -> Result<void>` | 返回类型形态与错误出口对应。**本地镜像这两个方法返回 `()`**；pinned 在容器重建失败时保留旧状态，语义需由 B7 失败用例验证。 |
| `place_item(SPPlacement) -> PItemKey`、`remove_item(PItemKey) -> SPPlacement` | `PlaceItem(SPPlacement) -> Result<PlacedItemHandle>`、`RemoveItem(PlacedItemHandle) -> Result<SPPlacement>` | 操作对应；C++ 增加错误包装并将裸 key 换为 handle，输入/失败合同需按 B7 对拍。 |
| `save() -> SPSolution`、`restore(&SPSolution) -> ()` | `Save() -> SPSolution`、`Restore(const SPSolution&) -> Result<void>` | C++ restore 可报告失败；Rust 不返回错误。保存快照结构对应。 |
| `density() -> f32`、`strip_width() -> f32`、`n_placed_items() -> usize` | `Density() -> double`、`StripWidth() -> double`、`PlacedItemCount() -> size_t` | 名称改写及 `f32/double`、`usize/size_t` 语言类型映射。 |
| 公共字段 `instance`、`strip`、`layout`、`item_demand_qtys` | C++ `SPProblem` 私有状态；通过 `Instance`、`StripWidth`、`RemainingDemand`、`PlacedItemCount`、`Save` 暴露受控视图 | 结构差异：Rust 可直接改写字段，C++ 不开放原始布局和需求容器；调用者可表达的状态修改能力不完全相同，需评估是否为有意封装边界。 |
| `SPPlacement { item_idx, d_transf }` | `SPPlacement { itemIndex, transform }` | 字段重命名；`DTransformation` 经 C++ `Transform2d` 表达，需在 B1 对齐精度与旋转归一化。 |

### MSPP

| pinned Rust 项 | C++ 对应项 | 映射结论 |
|---|---|---|
| `MSPProblem::new(MSPInstance) -> Self` | `MSPProblem::Create(MSPInstance) -> Result<MSPProblem>` | C++ 暴露可失败的工厂；pinned 构造本身不返回错误。 |
| `add_layout_from_strip(Strip) -> Result<LayKey>`、`remove_layout(LayKey) -> ()` | `AddLayout(MultiStrip) -> Result<size_t>`、`RemoveLayout(size_t) -> Result<void>` | 操作对应；`Strip`→`MultiStrip`，`LayKey`→索引/generation 句柄；**本地镜像 `add_layout_from_strip` 返回裸 `LayKey`**。 |
| `change_strip_width(LayKey, f32) -> Result<()>`、`fit_strip(LayKey) -> Result<()>` | `ChangeStripWidth(size_t,double) -> Result<void>`、`FitStrip(size_t) -> Result<void>` | 返回形态对应。**本地镜像两方法均返回 `()`**；pinned 对宽度正值/最大值校验，构造成功后才替换 layout/strip，失败时不改状态，需按 B7 验证。 |
| `place_item(MSPPlacement) -> (LayKey, PItemKey)`、`remove_item(LayKey, PItemKey) -> MSPPlacement` | `PlaceItem(MSPPlacement) -> Result<MSPPlacedHandle>`、`RemoveItem(MSPPlacedHandle) -> Result<MSPPlacement>` | C++ 以组合 handle 和 `Result` 包装；Rust 返回 tuple/placement。 |
| `save() -> MSPSolution`、`restore(&MSPSolution) -> bool` | `Save() -> MSPSolution`、`Restore(const MSPSolution&) -> Result<bool>` | bool 含义对应 key 集合是否变化；C++ 另外报告 restore 错误。 |
| `density`、`total_strip_width`、`extendable_strips`、`n_placed_items`、`all_layouts` | `Density`、`TotalStripWidth`、`ExtendableStrips`、`PlacedItemCount`；布局明细通过 `Save`/`MSPLayoutSnapshot` 暴露 | 主要查询有入口；`all_layouts` 返回 Rust 内部 `Layout` 迭代器，C++ 没有同形公开迭代器。 |
| 公共字段 `instance`、`layouts`、`strips`、`item_demand_qtys` | C++ `MSPProblem` 私有状态；对应查询由 `Instance`、`LayoutCount`、`StripWidth`、`RemainingDemand`、`Save` 提供 | 结构差异；Rust 暴露 SlotMap/SecondaryMap，C++ 不允许外部直接改写状态。 |
| `MSPPlacement { lk, item_idx, d_transf }`、`LayKey` | `MSPPlacement { layoutIndex, itemIndex, transform }`、`size_t` 索引及 generation | 字段重命名与 key 表示改变；删除/恢复后 key 有效性必须由 B7 生命周期用例验证。 |

### BPP

| pinned Rust 项 | C++ 对应项 | 映射结论 |
|---|---|---|
| `BPProblem::new(BPInstance) -> Self` | `BPProblem::Create(BPInstance) -> Result<BPProblem>` | 工厂命名不同；Rust 构造不返回错误，C++ 返回 `Result`。 |
| `place_item(BPPlacement) -> (LayKey, PItemKey)`、`remove_item(LayKey, PItemKey) -> BPPlacement` | `PlaceItem(BPPlacement) -> Result<BPPlacedHandle>`、`RemoveItem(BPPlacedHandle) -> Result<BPPlacement>` | 操作对应；C++ 将布局 key 与 placed-item key 合并进 handle，并增加错误出口。Rust 在移除最后一个物品时关闭布局，两侧生命周期合同需由 B7 对拍。 |
| `remove_layout(LayKey) -> ()`、`save() -> BPSolution`、`restore(&BPSolution) -> bool` | `RemoveLayout(size_t) -> Result<void>`、`Save() -> BPSolution`、`Restore(const BPSolution&) -> Result<bool>` | 主要操作对应；C++ 对无效索引/快照可返回错误，Rust 不返回错误。restore 的 bool 都表示 layout key 集合是否变化。 |
| `density`、`item_placed_qtys`、`bin_used_qtys`、`bin_cost`、`n_placed_items` | `Density`、`RemainingDemand`、`RemainingBinStock`、`BinCost`、`PlacedItemCount` | 密度/成本/数量均可通过 C++ 查询；已用 item/bin 数由总量减剩余量得到，没有同名迭代器 API。 |
| 公共字段 `instance`、`layouts`、`layout_bins`、`item_demand_qtys`、`bin_stock_qtys` | C++ `BPProblem` 私有状态；通过 `Instance`、`RemainingDemand`、`RemainingBinStock`、`OpenLayoutCount`、`Save` 提供受控视图 | 与 SPP/MSPP 相同，Rust 原始状态可直接访问，C++ 采用封装 API。 |
| `BPPlacement { layout_id, item_idx, d_transf }`；`BPLayoutType::{Open(LayKey), Closed { bin_id }}` | `BPPlacement { targetKind, layoutIndex, binIndex, itemIndex, transform }`；`LayoutTargetKind::{Open, Closed}` | Rust 变体载荷由 C++ placement 拆成字段。已检查实现：`Open` 指已有布局、`Closed` 指新开容器，与 Rust 语义一致；`Bpp.h` 两条 enum 注释把这两个含义写反，属于已确认的注释错误。 |
| `BPPlacement::from_placed_item`、`BPSolution { layout_snapshots, layout_bins, time_stamp }`、`Bin` 与 `BPInstance` | C++ `BPPlacement` 调用方组装、`BPSolution.layouts`/`BPLayoutSnapshot`、`Bin`/`BPInstance` | 解快照从两张 Rust map 变成 C++ 布局记录集合；Rust 时间戳未见 C++ 对应字段。`Bin`、`BPInstance` 的字段与查询方法仍需逐项对照默认值和输入拒绝条件。 |

### 实例、条带与解 DTO

| pinned Rust API 项 | C++ 对应项 | 映射结论 |
|---|---|---|
| `SPInstance { items, base_strip }`；`new`、`item_area`、`item_qty`、`total_item_qty`、`item`、`item_idx` | `SPInstance::Create`、`TotalItemArea`、`ItemDemand`、`TotalDemand`、`GetItem`、`ItemIndexFromExternalId`、`ItemCount`、`BaseStrip` | `Vec<(Arc<Item>, qty)>` 在 C++ 拆成 `vector<Item>` 与 `vector<size_t>`；Rust 字段公开，C++ 实例字段私有并由查询器访问。 |
| `MSPInstance { items, base_strip }`；`new`、`item_area`、`item_qty`、`total_item_qty`、`item`、`item_idx` | `MSPInstance::Create` 及 SPP 同组查询器，`BaseStrip() -> MultiStrip` | 同样是 items/demands 分列；Rust `Strip` 映射 C++ `MultiStrip`。 |
| `BPInstance { items, bins }`；`new`、`item_area`、`item_qty`、`bins`、`bin_qty`、`total_item_qty`、`item`、`item_idx` | `BPInstance::Create`、`TotalItemArea`、`ItemDemand`、`GetBin`、`BinCount`、`GetBin(index).stock`、`TotalDemand`、`GetItem`、`ItemIndexFromExternalId` | `bins()` 迭代器对应 `BinCount/GetBin`；`bin_qty` 由 `GetBin(index).stock` 承接；`Bin` 的 stock/cost 字段进入 C++ `Bin` DTO。默认值、索引越界和无效 demand 语义待 B7。 |
| SPP `Strip { fixed_height, cde_config, shape_modify_config, width }` 与 MSPP `Strip { max_width, fixed_height, cde_config, shape_modify_config, width }`；`new`、`set_width` | SPP `Strip`、MSPP `MultiStrip` 的公开配置字段、`Create`、`SetWidth`、`CreateContainer` | 字段对应但命名/casing 与 `f32/double` 不同；C++ `SetWidth` 可返回错误，Rust setter 是无返回错误的直接赋值；有效区间合同应与问题级 ChangeStripWidth 分开对拍。 |
| `SPProblem`/`MSPProblem`/`BPProblem` 的 `save/restore` 返回各自 solution；`SPSolution { strip, layout_snapshot, time_stamp }`、`MSPSolution { layout_snapshots, strips, time_stamp }`、`BPSolution { layout_snapshots, layout_bins, time_stamp }` | C++ `SPSolution`、`MSPSolution`、`BPSolution` 与对应 `*LayoutSnapshot` | 内容映射存在；MSP/BPP 从 map/key 表示改为 vector/index 表示。三个 Rust solution 都公开 `time_stamp`，C++ 对应 solution 头文件未见同字段。BPP 的 density/cost 对应 `Density/Cost`，SPP/MSPP 的 width/density 对应同名查询。 |

本节依据 pinned problem 文件的公开签名与本地 C++ 头文件/实现静态对照；[SPP problem.rs](https://raw.githubusercontent.com/JeroenGar/jagua-rs/f5e2b5ffd14931f17a20ce283a2122e877953ceb/jagua-rs/src/probs/spp/entities/problem.rs)、[MSPP problem.rs](https://raw.githubusercontent.com/JeroenGar/jagua-rs/f5e2b5ffd14931f17a20ce283a2122e877953ceb/jagua-rs/src/probs/mspp/entities/problem.rs)、[BPP problem.rs](https://raw.githubusercontent.com/JeroenGar/jagua-rs/f5e2b5ffd14931f17a20ce283a2122e877953ceb/jagua-rs/src/probs/bpp/entities/problem.rs)。签名对上不等于失败原子性、句柄生命周期和状态副作用已通过。

## IO 入口与外部 DTO 映射

| pinned Rust 可达项 | C++ 对应项 | 映射结论 |
|---|---|---|
| `Importer { shape_modify_config, cde_config }`；`new`、`with_min_item_separation`、`import_item`、`import_container` | `ImporterConfig`、`Importer` 构造、`ImportItem`、`ImportContainer` | Rust 直接公开两个配置字段；C++ 通过 `ImporterConfig` 构造，字段名和变更方式不同。 |
| `io::import::{import_simple_polygon, centering_transformation, ext_to_int_transformation, eliminate_degenerate_vertices}` | `Importer` 内部 polygon/transform 转换辅助代码 | Rust 这些自由函数可从公开 `io::import` 模块调用；C++ 未提供同名公共导出，属于 Rust 可见面差异，需判定它们是支持函数还是移植合同。 |
| `io::export::{export_layout_snapshot, int_to_ext_transformation}` | `ExternalLayoutFromLayout`、JSON codec 内部变换转换 | 布局快照转换有公开落点；变换 helper 没有同名 C++ 公共函数。 |
| SPP/BPP/MSPP 各自 `io::{import_instance, import_solution, export}` | `Importer::Import*Instance/Import*Solution`、`External*SolutionFromSolution` | 入口功能有对应，但签名和错误模型不完全相同：C++ importer 返回 `Result`；Rust exporter 直接返回 DTO，C++ exporter 返回 `Result`。 |
| SPP `import_solution(&SPInstance, &ExtSPSolution) -> Result<SPSolution>` | `Importer::ImportSPSolution` | 类型和失败出口基本对应；仍需 B8 按字段/转换失败逐项核验。 |
| BPP `import_solution(&BPInstance, &ExtBPInstance) -> BPSolution`；MSPP `import_solution(&MSPInstance, &ExtMSPInstance) -> MSPSolution` | `Importer::ImportBPSolution`、`Importer::ImportMSPSolution`，输入为对应 solution DTO，返回 `Result` | pinned Rust 这两个公开函数参数用了 instance DTO，函数体为 `unimplemented!()`；C++ 有可调用的 solution 导入实现。这是明确的公开 API/能力不等价，不是单纯改名，关联 B8 与 E4。 |
| 通用 `ExtItem/ExtOrientation/ExtRotation/ExtShape/ExtPolygon/ExtSPolygon/ExtContainer/ExtQualityZone/ExtLayout/ExtPlacedItem/ExtTransformation` | `ExternalItem/RotationPolicy/ExternalShape/ExternalPolygon/ExternalSimplePolygon/ExternalContainer/ExternalQualityZone/ExternalLayout/ExternalPlacedItem/ExternalTransformation` | 类型有对应；Rust enum 变体与 C++ `variant`/kind 字段重构，轮转角单位、点坐标精度和多边形孔表示要按 B8 核验。 |
| `ExtSPInstance/ExtSPSolution`、`ExtBPInstance/ExtBPSolution`、`ExtMSPInstance/ExtMSPSolution` 及各自 `ExtItem/ExtBin/ExtMSPStrip` | `ExternalSPInstance/ExternalSPSolution`、`ExternalBPInstance/ExternalBPSolution/ExternalBin`、`ExternalMSPInstance/ExternalMSPSolution/ExternalMSPStrip`、`ExternalDemandItem` | DTO 主体字段对应但命名/数值类型不同；Rust problem 专用 item 包一层通用 `ExtItem` 再带 demand，C++ 用 `ExternalDemandItem`。 |
| `io::svg::{layout_to_svg, s_layout_to_svg, layout_to_svg_group, Color, SvgDrawOptions, SvgLayoutTheme}` | `ExportLayoutSvg`、`SvgColor`、`SvgExportOptions`、`SvgLayoutTheme` | 主导出入口与选项类型有映射；Rust 另有 group/单条带输出和公开主题字段，C++ 导出结果为 `Result<string>`。具体 DRAW_OPTIONS、默认值及错误路径按 B8/S22 核验。 |

本节的 BPP/MSPP 导入缺口基于 pinned [BPP importer](https://raw.githubusercontent.com/JeroenGar/jagua-rs/f5e2b5ffd14931f17a20ce283a2122e877953ceb/jagua-rs/src/probs/bpp/io/import.rs) 与 [MSPP importer](https://raw.githubusercontent.com/JeroenGar/jagua-rs/f5e2b5ffd14931f17a20ce283a2122e877953ceb/jagua-rs/src/probs/mspp/io/import.rs) 中的公开签名和函数体；“公开可达”不代表实现完整。

### External DTO 字段表

| pinned Rust DTO 与公开字段/变体 | C++ DTO | 字段/结构差异 |
|---|---|---|
| `ExtItem { id, orientation, shape, min_quality }`；`ExtOrientation { rotation }`；`ExtRotation::{Discrete { angles }, Stepped { step }, Continuous {}}` | `ExternalItem { id, rotation, shape, minQuality }`；`RotationPolicy { kind, anglesDegrees, stepDegrees }` | Rust 要求 orientation.rotation；C++ policy 有 `None` 状态。字段名与 casing 不同，角度 `f32`→`double`；单位/归一化/空离散集合合同见 B8。 |
| `ExtShape::{Rectangle { x_min,y_min,width,height }, SimplePolygon, Polygon, MultiPolygon}`；`ExtSPolygon(Vec<(f32,f32)>)`；`ExtPolygon { outer, inner }` | `ExternalShape` variant：`ExternalRectangle/ExternalSimplePolygon/ExternalPolygon/ExternalMultiPolygon`；`ExternalPolygon { outer, holes }` | 多边形孔字段 `inner`→`holes`；tuple points→`vector<Point2d>`；坐标 `f32`→`double`。 |
| `ExtContainer { id, shape, zones }`；`ExtQualityZone { quality, shape }` | `ExternalContainer { id, shape, zones }`；`ExternalQualityZone { quality, shape }` | 字段概念对应；C++ shape/quality zone 名称重排，缺省空 zones 的序列化规则要按 JSON 对拍。 |
| `ExtLayout { container_id, placed_items, density }`；`ExtPlacedItem { item_id, transformation }`；`ExtTransformation { rotation, translation }` | `ExternalLayout { containerId, placedItems, density }`；`ExternalPlacedItem { itemId, transformation }`；`ExternalTransformation { rotationDegrees, translation }` | 字段重命名；layout 核心字段概念对应；transform 角度单位为度，数值精度、坐标次序和序列化规则需 B8 验证。 |
| SPP `ExtSPInstance { name, min_item_separation, items, strip_height }`；problem `ExtItem { base, demand }`；`ExtSPSolution { strip_width, layout, density, run_time_sec }` | `ExternalSPInstance { name, minItemSeparation, items, stripHeight }`；`ExternalDemandItem { item, demand }`；`ExternalSPSolution { stripWidth, layout, density, runTimeSec }` | 语义字段对应；Rust `base` 嵌套通用 item，C++ `ExternalDemandItem.item`；整数/浮点宽度和默认值待核。 |
| BPP `ExtBPInstance { name, min_item_separation, items, bins }`；`ExtItem { base, demand }`；`ExtBin { base, stock, cost }`；`ExtBPSolution { cost, layouts, density, run_time_sec }` | `ExternalBPInstance`、`ExternalDemandItem`、`ExternalBin`、`ExternalBPSolution` | 结构对应；Rust solution 公开总 cost/runtime，C++ DTO 同样字段；Rust nested layout/bin key 结构与 C++ vector layout 快照需核。 |
| MSPP `ExtMSPInstance { name, min_item_separation, items, strips: ExtMSPStrip }`；`ExtItem { base, demand }`；`ExtMSPStrip { height, max_width }`；`ExtMSPSolution { layouts, containers, density, run_time_sec }` | `ExternalMSPInstance`、`ExternalDemandItem`、`ExternalMSPStrip`、`ExternalMSPSolution` | 字段主体对应；pinned `strips` 是一个条带配置对象而非数组；solution 分别保存 layouts/containers；命名 casing 和 `f32/double` 仍需 B8 对拍。 |

## B0 逐项映射台账（按 pinned crate 外可达面）

下表按 Rust 可达类型列出其全部显式公开字段、常量、枚举变体和固有方法；对应项落到 C++ 公开头文件或标成无直接公开接口。前述章节保留了 Problem 与 DTO 的详细签名/差异。可达性按 pinned 的 `pub mod`、`pub use` 和类型成员关系判断；源文件中的原始 `pub` 行数不作为 API 覆盖数。

### 几何与形状

| Rust 可达项及逐成员清单 | C++ 落点 | B0 归类 |
|---|---|---|
| `Point(pub 0, pub 1)`；`x`、`y` | `Geometry/Point.h`：`Point2d` 坐标 | 类型、两个坐标和 accessor 对应；tuple 字段访问形式不同。 |
| `Edge { start, end }`；`try_new`、`extend_at_front`、`extend_at_back`、`scale`、`reverse`、`collides_at`、`closest_point_on_edge`、`x_min`、`y_min`、`x_max`、`y_max`、`length`、`centroid`、`bbox` | `Geometry/Edge.h`：`Edge2d`、`Create`、`ExtendedAtFrontRustF32`、`ExtendedAtBackRustF32`、`ScaledRustF32`、`ReversedRustF32`、`IntersectionPointRustF32`、`ClosestPointRustF32`、边界/长度/质心/包围盒查询 | 按签名落点映射；Rust 返回碰撞点而 C++ 接口需按实际返回值区分“检测”与“交点”。 |
| `Circle { center, radius }`；`try_new`、`bounding_circle`、`area`、`bbox`、`diameter` | `Geometry/Circle.h`：`Circle2d`、`Create`、`BoundingCircleRustF32`、`AreaRustF32`、`BoundingBoxRustF32`、`DiameterRustF32` | 公开字段和方法均有对应落点；数值边界归 B1。 |
| `Rect { x_min, y_min, x_max, y_max }`；`QUADRANT_NEIGHBOR_LAYOUT`；`try_new`、`from_diagonal_corners`、`relation_to`、`almost_relation_to`、`inflate_to_square`、`scale`、`resize_by`、`quadrants`、`corners`、`sides`、`edges`、`width`、`height`、`intersection`、`bounding_rect`、`centroid`、`area`、`diameter` | `Geometry/Rect.h`：`Rect2d` 字段、`RectRelation`、构造/Relation/AlmostRelation/尺寸与几何查询；邻象限常量对应 `Quadtree.cpp` 私有 `kQuadrantNeighborLayout`，不是 JaguarCore 公开头文件项 | 类型和操作有映射；该常量只在 C++ 内部使用；退化矩形、`resize_by` 的 `Option` 合同需按 B1/B4 核验。 |
| `SPolygon { vertices, bbox, area, diameter, poi, surrogate }`；`new`、`generate_surrogate`、`vertex`、`edge`、`edge_iter`、`n_vertices`、`surrogate`、`calculate_diameter`、`generate_bounding_box`、`calculate_area`、`calculate_poi`、`centroid` | `Geometry/SimplePolygon.h`：`SimplePolygon`、`Create`、顶点/边/数量/缓存查询、`GenerateSurrogate`、`Centroid` | 六个 Rust 公开缓存字段在 C++ 侧由构造和 accessor 管理；辅助计算方法有组合落点，访问/失效语义归 B1。 |
| `Transformation`；`empty`、`from_translation`、`from_rotation`、`rotate`、`translate`、`rotate_translate`、`translate_rotate`、`transform`、`transform_from_decomposed`、`inverse`、`is_empty`、`matrix`、`decompose` | `Geometry/Transformation.h`：`Transform2d::{Identity, FromRotationTranslation, ApplyRustF32, InverseRustF32, FollowedByRustF32}` | 类型映射但矩阵表示与旋转+平移表示不同；B1 第二轮以原点和单位基向量验证矩阵作用，`decompose` 映射到旋转/平移字段；C++ 无同形 `matrix` accessor 和 `is_empty` 方法，后者通过精确零字段表达。剩余组合顺序与精度按 B1 继续验收。 |
| `DTransformation { rotation, translation }`；`new`、`empty`、`rotation`、`translation`、`compose`；自由函数 `normalize_rotation` | `Transform2d` 的 `rotationRad/translation` 字段与构造/组合操作；`NormalizeRotationRustF32(float)` | 没有同名 `DTransformation` 结构；其分解值由 `Transform2d` 承接。`normalize_rotation` 有直接对应 helper；逐项数值语义由 B1 验收。 |
| `OriginalShape { shape, pre_transform, modify_mode, modify_config }`；`convert_to_internal`、`centroid`、`area`、`bbox`、`diameter` | `Geometry/OriginalShape.h`、`Geometry/SimplePolygon.h` 的 polygon 查询和转换接口 | 字段概念可对应；Rust 便捷查询由 `shape` 等字段组合，C++ 没有同形四个成员函数。 |
| `GeoPosition::{Exterior, Interior}`；`GeoRelation::{Intersecting, Enclosed, Surrounding, Disjoint}`；`RotationRange::{None, Continuous, Discrete(Vec<f32>)}` | `Geometry/Rect.h`：`GeoPosition`、`RectRelation`；`Item` 的允许旋转配置 | `GeoRelation` 改名映射；`RotationRange` 对应 `RotationPolicy`/允许旋转字段，变体载荷和单位归 B1/B8。 |
| `CollidesWith::collides_with`；`AlmostCollidesWith::almost_collides_with`；`DistanceTo::{distance_to, sq_distance_to}`；`SeparationDistance::{separation_distance, sq_separation_distance}`；`Transformable::{transform, transform_clone}`；`TransformableFrom::transform_from` | `Geometry/*.h` 中各具体类型的碰撞、距离、分离、变换成员 | Rust trait 泛型约束无同构 C++ interface；已按实现类型落到 concrete API，逐类型算法归 B1/B4。 |
| `convex_hull_indices`、`convex_hull_from_surrogate`、`convex_hull_from_points`；`ShapeModifyMode::{Inflate, Deflate}`；`ShapeModifyConfig { simplify_tolerance, offset, narrow_concavity_cutoff }`；`simplify_shape`、`offset_shape`、`close_narrow_concavities`、`shape_modification_valid` | `Geometry/ConvexHull.h`、`Geometry/ShapeModification.h` | 函数和配置类型有改名/组合映射；pinned 使用 `geo`，本地镜像使用 `geo-buffer`，算法结论归 B2。 |
| `SPSurrogate { poles, piers, convex_hull_indices, convex_hull_area, config }`；`new`、`ff_poles`、`ff_piers`；`SPSurrogateConfig { n_pole_limits, ff_pole_area_ratio, n_ff_piers }`、`none`；`generate_piers`、`generate_surrogate_poles`、`compute_pole` | `FailFast/Surrogate.h`、`Geometry/PointOfInaccessibility.h`：`Surrogate`、`SurrogateConfig`、`Create` 与 poles/piers/hull accessor | 类型、成员和自由函数合并到类工厂/accessor；无逐函数同名一对一接口，算法归 B1。 |

### 碰撞、hazard 与通用实体

| Rust 可达项及逐成员清单 | C++ 落点 | B0 归类 |
|---|---|---|
| `CDEngine { quadtree, hazards_map, config }`；`new`、`register_hazard`、`deregister_hazard_by_entity`、`deregister_hazard_by_key`、`save`、`restore`、`hazards`、`detect_poly_collision`、`detect_surrogate_collision`、`detect_containment_collision`、`collect_poly_collisions`、`collect_poly_collisions_until`、`collect_surrogate_collisions`、`collect_surrogate_collisions_until`、`get_virtual_root`、`bbox`、`haz_key_from_pi_key`；`CDEConfig { quadtree_depth, cd_threshold, item_surrogate_config }`；`CDESnapshot { dynamic_hazards }` | `Collision/CollisionEngine.h`：`CollisionEngine`、`CollisionSnapshot`、注册/注销、polygon/surrogate 检测与收集、Save/Restore | 类型改名与主要调用对应；Rust 公开内部状态、virtual root 与部分查询在 C++ 无同形公开接口；C++ batch/parallel 视为扩展。 |
| `HazKey`（slotmap 生成的不透明 key）；`Hazard { entity, shape, dynamic }`、`new`；`HazardEntity::PlacedItem { id, dt, pk }`、`Exterior`、`Hole { idx }`、`InferiorQualityZone { quality, idx }`；`scope`；`From<(PItemKey, T: Borrow<PlacedItem>)>` | `Collision/Hazard.h`、`HazardHandle`/`HazardKind` | key 与 handle 改名；实体载荷拆到 C++ kind/id/index 字段；Rust tuple 转换没有同形 C++ trait，生命周期归 B3/B4。 |
| `HazardFilter::is_irrelevant`；`HazKeyFilter(pub SecondaryMap<...>)`、`from_keys`、`from_irrelevant_qzones`；`NoFilter`；`HazardCollector` 的 `contains_key`、`contains_entity`、`insert`、`remove_by_key`、`remove_by_entity`、`is_empty`、`len`、`iter`、`keys`、`entities`；`BasicHazardCollector` 的 `new`、`with_capacity`、`clear`、`contains_key`、`insert`、`remove`、`retain`、`is_empty`、`len`、`iter` | `Collision/HazardFilter.h`、`HazardCollector.h`：filter interface 与 collector 类型 | 抽象接口、返回值和 collector 操作有对应；Rust `HazKeyFilter` 元组字段暴露 `SecondaryMap`，C++ 未公开等价底层容器。 |
| `QTHazard { qt_bbox, hkey, entity, presence }`；`QTHazPresence::{None, Partial(QTHazPartial), Entire}`；`from_root`、`constrict`、`n_edges`；`QTNode { level, bbox, children, hazards, cd_threshold }`；`new`、`register_hazard`、`deregister_hazard`、`collides`、`collect_collisions`、`collect_collisions_until`；`QTQueryable::{collides_with_quadrants, quadrant_order}` | `Collision/Quadtree.h`：`HazardPresence`、`QTHazard`、`QuadTree`/节点内部实现 | 可映射类型和操作；C++ 另公开导出 `RegisterHazard(handle, Rect2d)` bounds-only 扩展，仓内无调用，以对角 Edge 作为代理且可能漏掉子节点范围查询（B5）；`QTHazPartial`、`QTHazardVec` 所在模块私有且未重导出，却出现在 `QTHazPresence::Partial` 和 `QTNode::hazards` 的公开类型位置，属于 pinned API 可见性边界问题，不能视为独立可构造公开类型。 |
| `Item { idx, external_id, shape_orig, shape_cd, allowed_rotation, min_quality, surrogate_config }`；`new`、`area`；`Container { outer_orig, outer_cd, quality_zones, base_cde }`；`new`、`area`；`InferiorQualityZone { quality, shapes_orig, shapes_cd }`；`new`、`to_hazards`、`area`；`N_QUALITIES` | `Entities/Item.h`、`Container.h`：`Item`、`Container`、`QualityZone`、getter/config | 类型和值可映射；Rust公开固定质量槽数组，C++ getter返回稀疏vector；空组对仓内hazard/SVG消费者无影响，但直接读取组数的调用方可观察表示差异。 |
| `PlacedItem { item, d_transf, shape }`；`new`；`PItemKey`（slotmap 不透明 key）；`Layout { container, placed_items }`；`new`、`from_snapshot`、`swap_container`、`save`、`restore`、`place_item`、`remove_item`、`is_empty`、`density`、`placed_item_area`、`items`、`cde`、`is_feasible`；`ContainerMismatch`（unit struct）；`LayoutSnapshot { container, placed_items, cde_snapshot }`、`density`、`placed_item_area` | `Entities/PlacedItem.h`、`Layout.h`、`Container.h`：`PlacedItemHandle`、`Layout`、`LayoutSnapshot`、`Result` | key 改为 handle、公开 map 改为封装操作；Rust `ContainerMismatch` 到 C++ error code/result 映射。 |

### SPP、BPP、MSPP 问题模型

| Rust 可达项及逐成员清单 | C++ 落点 | B0 归类 |
|---|---|---|
| SPP `Strip { fixed_height, cde_config, shape_modify_config, width }`；`new`、`set_width`；`SPInstance { items, base_strip }`；`new`、`item_area`、`item_qty`、`total_item_qty`、`item`、`item_idx`；`SPProblem { instance, strip, layout, item_demand_qtys }`；`new`、`change_strip_width`、`fit_strip`、`place_item`、`remove_item`、`save`、`restore`、`density`、`strip_width`、`n_placed_items`；`SPPlacement { item_idx, d_transf }`；`SPSolution { strip, layout_snapshot, time_stamp }`、`density`、`strip_width` | `Problems/Spp.h`：`Strip`、`SPInstance`、`SPPlacement`、`SPProblem`、`SPSolution` | 已在 Problem 表列明 pinned/local Result 差异；实例 items/demand 拆分、Solution timestamp 和 Rust/C++ 字段开放性均为形态差异。 |
| BPP `Bin { idx, external_id, container, stock, cost }`、`new`；`BPInstance { items, bins }`；`new`、`item_area`、`item_qty`、`bins`、`bin_qty`、`total_item_qty`、`item`、`item_idx`；`BPProblem { instance, layouts, layout_bins, item_demand_qtys, bin_stock_qtys }`；`new`、`remove_layout`、`place_item`、`remove_item`、`save`、`restore`、`density`、`item_placed_qtys`、`bin_used_qtys`、`bin_cost`、`n_placed_items`；`BPPlacement { layout_id, item_idx, d_transf }`、`from_placed_item`；`BPLayoutType::{Open(LayKey), Closed { bin_id }}`；`BPSolution { layout_snapshots, layout_bins, time_stamp }`、`density`、`cost` | `Problems/Bpp.h`：`Bin`、`BPInstance`、`BPPlacement`、`LayoutTargetKind`、`BPProblem`、`BPSolution` | 成员均有类型/操作落点；Rust key/map 改为 C++ index/generation 与 vector；`Bpp.h` 枚举注释反向问题已登记。 |
| MSPP `Strip { max_width, fixed_height, cde_config, shape_modify_config, width }`；`new`、`set_width`；`MSPInstance { items, base_strip }`；与 SPP 同组实例查询；`MSPProblem { instance, layouts, strips, item_demand_qtys }`；`new`、`add_layout_from_strip`、`remove_layout`、`fit_strip`、`place_item`、`remove_item`、`save`、`restore`、`change_strip_width`、`density`、`all_layouts`、`extendable_strips`、`total_strip_width`、`n_placed_items`；`MSPPlacement { lk, item_idx, d_transf }`；`MSPSolution { layout_snapshots, strips, time_stamp }`、`density`、`total_strip_width`；`LayKey`（slotmap 不透明 key） | `Problems/Mspp.h`：`MultiStrip`、`MSPInstance`、`MSPPlacement`、`MSPProblem`、`MSPSolution`、组合 handle | 类型/主要操作有对应；`Strip`→`MultiStrip`、key/map→index/generation；`all_layouts` 无同形公开迭代器，pinned Result 合同见 Problem 表。 |

### IO、SVG 与 util

| Rust 可达项及逐成员清单 | C++ 落点 | B0 归类 |
|---|---|---|
| `Importer { shape_modify_config, cde_config }`；`new`、`with_min_item_separation`、`import_item`、`import_container`；公开 `io::import` helper：`import_simple_polygon`、`centering_transformation`、`ext_to_int_transformation`、`eliminate_degenerate_vertices`；`io::export`：`export_layout_snapshot`、`int_to_ext_transformation` | `IO/Importer.h`、`JsonCodec.h` 及其内部 shape/transform 转换 | importer 主入口对应；四个 Rust import helper 可经公开 `io::import` 访问，C++ 没有同形公共导出；export helper 有组合落点。 |
| 通用 `ExtItem { id, orientation, shape, min_quality }`；`ExtOrientation { rotation }`；`ExtRotation::{Discrete { angles }, Stepped { step }, Continuous {}}`；`ExtContainer { id, shape, zones }`；`ExtShape::{Rectangle { x_min, y_min, width, height }, SimplePolygon, Polygon, MultiPolygon}`；`ExtPolygon { outer, inner }`；`ExtSPolygon(pub 0)`；`ExtQualityZone { quality, shape }`；`ExtLayout { container_id, placed_items, density }`；`ExtPlacedItem { item_id, transformation }`；`ExtTransformation { rotation, translation }`；`From<DTransformation>` 与 `From<ExtTransformation>` | `IO/ExternalRepr.h`：`ExternalItem`、`RotationPolicy`、`ExternalShape`、`ExternalPolygon`、`ExternalSimplePolygon`、`ExternalContainer`、`ExternalQualityZone`、`ExternalLayout`、`ExternalPlacedItem`、`ExternalTransformation` | 字段与 variant 全部登记；两个 transformation 转换 trait 由 importer/exporter 组合承担；Rust serde 规则、角度单位、`f32/double`、`inner/holes` 及导入拒绝条件归 B8。 |
| SPP 外部 DTO：`ExtSPInstance { name, min_item_separation, items, strip_height }`、`ExtItem { base, demand }`、`ExtSPSolution { strip_width, layout, density, run_time_sec }`；BPP：`ExtBPInstance { name, min_item_separation, items, bins }`、`ExtItem { base, demand }`、`ExtBin { base, stock, cost }`、`ExtBPSolution { cost, layouts, density, run_time_sec }`；MSPP：`ExtMSPInstance { name, min_item_separation, items, strips: ExtMSPStrip }`、`ExtItem { base, demand }`、`ExtMSPStrip { height, max_width }`、`ExtMSPSolution { layouts, containers, density, run_time_sec }` | `ExternalSP/BP/MSPInstance`、`ExternalDemandItem`、`ExternalBin`、`ExternalMSPStrip`、`ExternalSP/BP/MSPSolution` | 字段重命名和嵌套关系有落点；注意 pinned MSP `strips` 是单个 `ExtMSPStrip`，不是数组；序列化、默认值和时间单位归 B8。 |
| feature IO：每类 `export`；`import_instance`；`import_solution`；SVG `Color`、`SvgDrawOptions { theme, quadtree, surrogate, highlight_collisions, draw_cd_shapes, highlight_cd_shapes }`；`SvgLayoutTheme { stroke_width_multiplier, container_fill, item_fill, hole_fill, qz_fill, qz_stroke_opac, collision_highlight_color }`、常量 `EARTH_TONES`/`GRAY`；`s_layout_to_svg`、`layout_to_svg`、`layout_to_svg_group` | `IO/Importer.h`、`JsonCodec.h`、`SvgExport.h`：problem import/export、`SvgColor`、`SvgExportOptions`、`SvgLayoutTheme`、`ExportLayoutSvg` | 入口/DTO 有映射；Rust 有组/单条带输出和公开 theme 字段；`Color` 以 `From<String/&str>`、`Display`、serde 暴露，tuple 字段私有；BPP/MSPP `import_solution` 仍是 stub，归 B8。 |
| `FPA(pub f32)`；其 `From`、`PartialEq`、`PartialOrd`、`Display` 实现；`util::assertions`：`snapshot_matches_layout`、`collision_hazards_sorted_correctly`、`qt_contains_no_dangling_hazards`、`layout_qt_matches_fresh_qt`、`quadrants_have_valid_layout`、`print_layout`；SPP/MSPP `problem_matches_solution`、`instance_item_indices_correct`；BPP `problem_matches_solution`、`instance_item_bin_ids_correct`；根别名 `Instant` | `NumericPolicy.h`、`RustF32.h`、`Assertions.h`、`Result.h`；C++ 自身时间/计时类型 | 数值包装和验证 helper 有用途对应但不是一对一产品 API；`Instant` 只有时间戳用途映射。 |

### 原始 `pub` 扫描中的非 crate 外项

| 源码位置 | 项 | 可达性结论 |
|---|---|---|
| `collision_detection/quadtree/qt_partial_hazard.rs`、`qt_hazard_vec.rs` | `QTHazPartial`、`QTHazardVec` 及其公开成员 | 模块私有且类型未重导出；独立类型/方法不列作 crate 外 API。但 `QTHazPresence::Partial` 和 `QTNode::hazards` 引用这些类型，已在上表记录为可见性边界。 |
| `collision_detection/quadtree/qt_traits.rs` | `CollisionHalf` | 模块私有且未重导出，不属于 crate 外 API；`QTQueryable` 因单独 `pub use` 可达。 |
| `geometry/shape_modification.rs` | 私有辅助类型 `Corner` 的 `flip`、`to_points`、`from` | 方法所在接收类型私有，不属于 crate 外 API；该模块中的四个自由函数与配置/枚举仍可达。 |
| `io/svg/svg_util.rs` | `change_brightness`、`blend_colors`、`original_shape_data`、`simple_polygon_data`、`quad_tree_data`、`data_to_path`、`point`、`circle`、`edge_data`、`aa_rect_data` | `svg_util` 模块私有且这些函数未重导出，不属于 crate 外 API；`Color`、`SvgDrawOptions`、`SvgLayoutTheme` 经 `pub use` 可达。 |

因此，原始文本扫描的 479 个 `pub` 声明和 197 个公开字段用于 pinned/镜像差异盘点，不能被表述为“479 个 crate 外 API 项”。本表覆盖导出闭包中的显式类型成员；Rust derive/标准 trait 能力按类型声明归属，宏展开与编译器 rustdoc 导出清单尚未通过构建产物核验。

## 已确认的形态差异与待判定项

1. **公开面存在类型重命名或结构转换。**例如 `CDEngine`→`CollisionEngine`、`CDESnapshot`→`CollisionSnapshot`、`Ext*`→`External*`、`SPSurrogate`→`Surrogate`。这些是映射线索，不等于语义已通过。
2. **Rust trait 与 C++ 具体类型并非同构。**`CollidesWith`、`Transformable`、`HazardFilter`、`HazardCollector`、`QTQueryable` 的静态成员落点已在台账登记为具体几何方法、filter 或 collector；这不证明其实现范围、调用顺序和边界语义一致，需按 B1/B4/B9 对拍。
3. **显式变换映射已由 B1 确认。**Rust `DTransformation` 仅封装刚体旋转和平移，由 `Transform2d` 字段和 f32 操作承接，不需增加独立结构；`normalize_rotation` 有直接的 `NormalizeRotationRustF32` 公共函数映射。
4. **Rust 的 feature 条件与 C++ 公开面需分开登记。**Rust SPP/BPP/MSPP 分别受 Cargo feature 控制；不能仅根据 C++ 头文件存在，推断对应 feature 的导入、导出和问题操作已完整对齐。
5. **C++ 另有基础设施和扩展 API。**`Result/ErrorCode`、`NumericPolicy`、`Version`、批量碰撞选项及 SoA/AVX2 接口等并非 Rust 同名公开项。后续需标注为语言适配、C++ 扩展或产品接口，不纳入 Rust 等价缺口，除非它们改变 Rust 可表达输入下的可观察行为。

## 完成状态与后续核对

B0 的 pinned 静态映射台账已补齐：crate 外可达模块、类型、显式字段/方法/枚举变体、feature IO、util helper，以及非重导出 `pub` 项的排除边界均已登记。仍有三项范围限制：本轮未生成编译器 rustdoc/API dump；不以静态映射证明算法或状态行为等价；需要行为裁决的差异须进入 B1–B15 对拍。没有运行构建、测试或 Rust oracle。

E4 的导出图和上述静态签名核对已完成；锁定 oracle 运行、受影响 fixture 复核/重生成及基线哈希已按 [E4 执行记录](E4-oracle-fixture-baseline-2026-10-16.md) 完成。默认 cold-start Rust trace 已在 Debug/Release 下逐行匹配，SparrowCore Debug/Release 全套 runner 均通过。E4 完成不代表所有算法语义已等价；各项边界仍以工作项清单及对应记录为准。另需注意 `Cargo.lock` 锁依赖版本和 Git revision；Rust 1.98.1 由两个 oracle 目录中的 `rust-toolchain.toml` 指定，不能说成由 Cargo.lock 锁定。
