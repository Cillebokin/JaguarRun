# jagua-rs 模块导出与边界记录

**Rust 基线**：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`  
**C++ 范围**：`JaguarCore/Include/JaguarCore` 与 `JaguarCore/Source`  
**判定**：模块和主要导出已按 pinned revision 静态核对；函数语义仍由 B1–B15 验收

## 文件级记录

| Rust 文件 | 声明/分支 | C++ 对应范围 | 结论与证据 |
|---|---|---|---|
| `lib.rs` | crate 根暴露 `collision_detection`、`entities`、`geometry`、`io`、`probs`、`util`，并导出 `Instant` 类型别名 | JaguarCore 公共头文件集合；时间戳由 C++ 自身类型表达 | crate 根可达路径已在 [B0 API 映射](../../complex/JaguarCore与jagua-rs公开API映射.md) 逐模块登记；`Instant` 无同名 C++ 公共类型。无函数体、错误出口 |
| `collision_detection/mod.rs` | `cd_engine` 为私有模块；公开 `hazards`、`quadtree`；重导出 `CDEConfig`、`CDESnapshot`、`CDEngine` | `Collision/CollisionEngine.h`、`Hazard.h`、`HazardFilter.h`、`HazardCollector.h`、`Quadtree.h` | 导出闭包在 B0 核对；trait/collector 与 C++ 类型组织不同，行为由 B4/B11 验收。无函数体、错误出口 |
| `collision_detection/hazards/mod.rs` | `hazard` 私有；公开 `filter`、`collector`；重导出 `HazKey`、`Hazard`、`HazardEntity` | `Collision/Hazard.h`、`HazardFilter.h`、`HazardCollector.h` | 静态重导出路径已确认；过滤、收集顺序和失败条件由 B4/B11 验收。无函数体、错误出口 |
| `collision_detection/quadtree/mod.rs` | 私有 quadtree 实现；`assertions` 仅在 `debug_assertions` 或 `test` 下编译；`tests` 仅在 `test` 下编译；公开 `QTHazPresence`、`QTHazard`、`QTNode`、`QTQueryable` | `Collision/Quadtree.h` 及相关 `Collision/*` 类型 | profile 条件编译属于有效范围；C++ 断言启用条件由 E8 核对。无此处定义的函数体；算法由 B4 验收 |
| `entities/mod.rs` | 私有模块后重导出 `Item`、`Layout`、`ContainerMismatch`、`LayoutSnapshot`、`PlacedItem`、`PItemKey`、`Container`、`InferiorQualityZone`、`N_QUALITIES` | `Entities/Item.h`、`Layout.h`、`PlacedItem.h`、`Container.h` 与 `Collision/*` | 名称、封装和 key/handle 形态差异见 B0；生命周期、restore 与身份语义由 B6 验收。无函数体、错误出口 |
| `geometry/mod.rs` | 公开 `convex_hull`、`fail_fast`、`geo_enums`、`geo_traits`、`primitives`、`shape_modification`；私有 transformation/original-shape 实现重导出 `DTransformation`、`Transformation`、`OriginalShape`、`normalize_rotation` | `Geometry/*`、`FailFast/Surrogate.h` | 导出闭包在 B0 核对；`DTransformation` 无同名 C++ 类型。算法分别由 B1–B3 验收；无函数体、错误出口 |
| `geometry/fail_fast/mod.rs` | 私有 `piers`、`pole`、`sp_surrogate`；重导出 `generate_piers`、`generate_surrogate_poles`、`compute_pole`、`SPSurrogate`、`SPSurrogateConfig` | `FailFast/Surrogate.h`、`Geometry/PointOfInaccessibility.h` | 仅记录模块边界；数值、退化输入与缺失 surrogate 错误契约由 B3 验收。无函数体、错误出口 |
| `geometry/primitives/mod.rs` | 私有原语模块，重导出 `Circle`、`Edge`、`Point`、`Rect`、`SPolygon` | `Geometry/{Circle,Edge,Point,Rect,SimplePolygon}.h` | 类型映射见 B0；每个原语函数与退化边界由 B1 验收。无函数体、错误出口 |
| `io/mod.rs` | 公开 `ext_repr`、`import`、`export`、`svg` | `IO/ExternalRepr.h`、`Importer.h`、`JsonCodec.h`、`SvgExport.h` | 模块入口有静态落点；JSON 字段/错误与 SVG 几何由 B8 验收。无函数体、错误出口 |
| `io/svg/mod.rs` | 私有 `layout_to_svg`、`svg_util`；重导出 SVG 函数、`Color`、`SvgDrawOptions`、`SvgLayoutTheme` | `IO/SvgExport.h` | 导出形态已在 B0 核对；绘制选项、错误与几何结果由 B8 验收。无函数体、错误出口 |
| `probs/mod.rs` | `spp`、`bpp`、`mspp` 分别受 `spp`、`bpp`、`mspp` Cargo feature 控制 | `Problems/Spp.h`、`Bpp.h`、`Mspp.h` | 三个 feature 是认证范围的一部分（D2）；分别由 B7 与 E4 的启用配置记录验收。无函数体、错误出口 |
| `probs/spp/mod.rs` | 公开 `entities`、`io`、`util` | `Problems/Spp.h`、`IO/*`、`Assertions.h` | 受父模块 `spp` feature 控制；细节由 B7/B8/B9 验收 |
| `probs/spp/entities/mod.rs` | 私有实现模块；重导出 `SPInstance`、`SPPlacement`、`SPProblem`、`SPSolution`、`Strip` | `Problems/Spp.h` | 类型与成员落点见 B0；状态、放置和 strip 操作由 B7 验收 |
| `probs/spp/io/mod.rs` | `export`、`import` 私有；`ext_repr` 公开；重导出 `export`、`import_instance`、`import_solution` | `IO/Importer.h`、`JsonCodec.h` | SPP 导入/导出入口已静态映射；字段和错误语义由 B8/B14 验收 |
| `probs/spp/util/mod.rs` | 公开 `assertions` | `Assertions.h` | 断言功能与启用条件由 B9/E8 验收；不是产品 API 等价证据 |
| `probs/bpp/mod.rs` | 公开 `entities`、`io`、`util` | `Problems/Bpp.h`、`IO/*`、`Assertions.h` | 受父模块 `bpp` feature 控制；细节由 B7/B8/B9 验收 |
| `probs/bpp/entities/mod.rs` | 私有实现模块；重导出 `Bin`、`BPInstance`、`BPLayoutType`、`BPPlacement`、`BPProblem`、`LayKey`、`BPSolution` | `Problems/Bpp.h` | 类型落点见 B0；枚举注释方向疑点和操作行为由 B7 验收 |
| `probs/bpp/io/mod.rs` | `export`、`import` 私有；`ext_repr` 公开；重导出 `export`、`import_instance`、`import_solution` | `IO/Importer.h`、`JsonCodec.h` | 导出入口静态存在；pinned `import_solution` 的 `unimplemented!()` 与 C++ solution 导入入口是 B8 的已登记差异 |
| `probs/bpp/util/mod.rs` | 公开 `assertions` | `Assertions.h` | 断言语义与条件由 B9/E8 验收 |
| `probs/mspp/mod.rs` | 公开 `entities`、`io`、`util` | `Problems/Mspp.h`、`IO/*`、`Assertions.h` | 受父模块 `mspp` feature 控制；细节由 B7/B8/B9 验收 |
| `probs/mspp/entities/mod.rs` | 私有实现模块；重导出 `MSPInstance`、`LayKey`、`MSPPlacement`、`MSPProblem`、`MSPSolution`、`Strip` | `Problems/Mspp.h` | 类型落点见 B0；restore 与条带状态由 B6/B7 验收 |
| `probs/mspp/io/mod.rs` | `export`、`import` 私有；`ext_repr` 公开；重导出 `export`、`import_instance`、`import_solution` | `IO/Importer.h`、`JsonCodec.h` | pinned `import_solution` 为 `unimplemented!()`，与 C++ 入口的差异归 B8；输入契约归 B14 |
| `probs/mspp/util/mod.rs` | 公开 `assertions` | `Assertions.h` | 断言语义与条件由 B9/E8 验收 |
| `util/mod.rs` | 公开 `assertions`，私有 `fpa` 并重导出 `FPA` | `Assertions.h`、`NumericPolicy.h`、`RustF32.h`、`Result.h` | Rust/C++ 容差、断言条件与错误映射分别由 B9/E8 验收 |

## 结论边界

以上 24 个文件没有业务函数体；可观测差异来自模块可达性、feature 与编译条件、重导出边界。其余 61 个 Rust 文件包含实现或测试逻辑，其中 6 个数值/断言文件见 [10-utilities-and-assertions.md](10-utilities-and-assertions.md)，4 个 entities 文件见 [20-entities.md](20-entities.md)，10 个 collision_detection 文件见 [30-collision-detection.md](30-collision-detection.md)，5 个 io 文件见 [40-io.md](40-io.md)，7 个 BPP 文件见 [50-bpp.md](50-bpp.md)，7 个 SPP 文件见 [60-spp.md](60-spp.md)，7 个 MSPP 文件见 [70-mspp.md](70-mspp.md)，15 个 geometry 文件见 [80-geometry.md](80-geometry.md)。B13 的 85 个文件均已登记；逐项运行等价仍归 B1–B15 对应工作项验收。

