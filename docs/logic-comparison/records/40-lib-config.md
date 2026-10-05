# 40. lib.rs / Cargo features / 公共配置 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/lib.rs`（36 行）+ `Cargo.toml`（42 行）+ `CDEConfig` 定义（`collision_detection/cd_engine.rs:404-411`）
**C++**: 无 feature 门控 + `JaguarCore/Include/JaguarCore/NumericPolicy.h`（52 行）+ `result.h`（90 行）+ `version.h`（16 行）+ `Version.cpp`（19 行）+ `CDEConfig`（`collision/CollisionEngine.h:34-38`）
**判定**: 不等价（结构层差异：C++ 无 feature 门控（三问题类型全量编译）、无 `Instant`/时间戳体系；CDEConfig C++ 提供默认值（quadtreeDepth=4/cdThreshold=32）而 Rust 无 `Default`——注意 sparrow 调用方实际用 cd_threshold=64；`Result`/错误码为 C++ 对 anyhow/thiserror 的形态替代，`NumericPolicy` 为 C++ 额外，版本串两侧各表）

## 逐条比对

### 模块声明（lib.rs:17-33）
- C++ 对应：目录结构即模块结构（`Include/JaguarCore/{Collision,Entities,Geometry,IO,Problems,Support,FailFast}`），无门控。
- 判定：基本等价（组织形态差异）。
- 证据：`collision_detection` ↔ `Collision/`；`entities` ↔ `Entities/`；`geometry` ↔ `Geometry/`；`io` ↔ `IO/`；`util` ↔ 分散（`Geometry/RustF32.h` + `Support/Assertions.cpp`，见记录 38/39）；`probs` ↔ `Problems/`（`Spp.h`/`Bpp.h`/`MSpp.h`）。

### Cargo features：`spp`/`bpp`/`mspp`（Cargo.toml:32-38）
- C++ 对应：无——`Problems/` 三组头文件与源码全量编译，无条件编译。
- 判定：差异（门控缺失）。
- 证据：
```toml
[features]
## Enables support for the Strip Packing Problem
spp = []
## Enables support for the Bin Packing Problem
bpp = []
## Enables support for the Multi Strip Packing Problem
mspp = []
```
（Cargo.toml:32-38）。Rust `probs/mod.rs` 按 feature 导出子模块（`#[cfg(feature = "spp")] pub mod spp;` 等）；C++ 使用方（SparrowCore）链接全部。影响：C++ 二进制含未用问题类型；Rust 可裁剪。无行为差异，属部署形态差异。

### `#![doc = document_features::document_features!()]`（lib.rs:15）
- C++ 对应：无（无 feature 即无文档特性表）。判定：C++ 缺失（随 feature 体系消失）。

### `pub type Instant = web_time::Instant;`（lib.rs:36）
- C++ 对应：无。
- 判定：C++ 缺失。
- 证据：Rust 导出 web 兼容 `Instant`（`web_time` crate，Cargo.toml:30），probs 解的 `time_stamp` 与导出 `run_time_sec` 依赖它（`*Solution::new(Instant::now())`、`export(..., epoch).duration_since`）。C++ 侧解结构无时间戳字段，`runTimeSec` 由调用方传参（记录 37 差异 1）。影响：C++ 无法在库内度量求解耗时，需调用方计时。

### `CDEConfig`（cd_engine.rs:404-411 vs CollisionEngine.h:34-38）
- 判定：差异（默认值）。
- 证据：
```rust
#[derive(Serialize, Deserialize, Clone, Copy, Debug, PartialEq)]
pub struct CDEConfig {
    pub quadtree_depth: u8,
    pub cd_threshold: u8,
    pub item_surrogate_config: SPSurrogateConfig,
}
```
（cd_engine.rs:403-411，**无 `Default` 实现**——调用方必须显式给值）↔
```cpp
struct CDEConfig
{
    std::uint8_t quadtreeDepth = 4;        // 四叉树最大深度
    std::uint8_t cdThreshold = 32;         // 碰撞检测阈值
    SurrogateConfig itemSurrogateConfig{}; // 物品代理配置
};
```
（CollisionEngine.h:34-38，**默认值 quadtreeDepth=4 / cdThreshold=32**）。
- 实际取值分析：Rust 库内不提供默认，仓库内唯一构造点在调用方 sparrow：`CDEConfig { quadtree_depth: 4, cd_threshold: 64, ... }`（sparrow/src/config.rs:87-89）——**cd_threshold=64 与 C++ 默认 32 不一致**。即：C++ `ImporterConfig cdeConfig{}`（importer.h:18）等零初始化路径会取 4/32，而 Rust 参考用法是 4/64。C++ 默认值是自造的便利层，与 Rust 语义（无默认、调用方必填）不对齐，且默认数值与参考调用方实际值不同。
- `SPSurrogateConfig` 默认：C++ `{}` 默认构造；Rust 无 Default（sparrow 给 `n_pole_limits: [(64,0.0),(16,0.8),(8,0.9)], ff_pole_area_ratio: 0.5, n_ff_piers: 0`）。C++ `SurrogateConfig` 的默认成员值见记录 15。

### 错误处理形态：anyhow/thiserror ↔ `Result.h`
- 判定：基本等价（形态替代）。
- 证据：Rust io 层用 `anyhow::{Result, bail, ensure}`（import.rs:10）+ `thiserror`（shape_modification 等）；C++ `Result<T>` = `std::variant<T, Error>` + `ErrorCode` 枚举 14 值（result.h:10-26：`InvalidInput`/`InvalidPolygon`/`SelfIntersection`/`ZeroArea`/`UnsupportedShape`/`OffsetFailed`/`CollisionEngineFailure`/`SnapshotMismatch`/`Infeasible`/`Cancelled`/`TimeLimit`/`SerializationFailure`/`UnsupportedOperation` 等）+ 消息字符串。语义映射：`ensure!`/`bail!` → `FromError({code, message})`；`unimplemented!` → `UnsupportedOperation`（记录 34/37）；`assert!`（debug）→ `InvalidInput` 或保留 `DebugAssert*`（记录 39）。本系列逐记录的"panic→Result"约定即源于此。`Result<void>` 特化（result.h:67-89）对应 Rust `Result<()>`。

### `NumericPolicy.h` ↔ Rust 对应
- 判定：C++ 额外（无 Rust 对应；详见记录 38 差异 7）。1e-9/1e-7/0.0/1e-3 四容差 + `NearlyEqual`，服务 Assertions（记录 39）与 Importer `near()`（Importer.cpp:219-222）。

### `version.h`/`Version.cpp` ↔ Cargo version
- 判定：差异（各表其值，无对齐要求）。
- 证据：Cargo.toml:3 `version = "0.9.0"`（jagua-rs 0.9.0）；C++ `Version()` 返回 `"0.1.0-dev"`（Version.cpp:7-10）+ C 链接探针 `JaguarCoreVersion()`（cpp:14-17）与 `JaguarCoreApiVersion() = 2`（cpp:19-21）。版本串语义不同（库自身版本 vs API 探针），无互操作需求。

### clippy 门面（lib.rs:1-9）
- C++ 对应：无（代码规范另由 `代码规范.docx` 约束）。判定：C++ 缺失（工具链差异，非行为）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | feature 门控 spp/bpp/mspp | C++ 缺失 | C++ 三问题类型全量编译；Rust 可按需裁剪。无行为差异 | Cargo.toml:32-38 vs 无 |
| 2 | `Instant`/web_time 与 time_stamp | C++ 缺失 | C++ 库内无法计时，`run_time_sec` 依赖调用方传参（记录 37 差异 1） | lib.rs:36 vs 无 |
| 3 | CDEConfig 默认值 | 差异 | C++ `quadtreeDepth=4/cdThreshold=32`；Rust 无 Default，参考调用方 sparrow 实际用 4/64——C++ 零初始化路径取 32 与参考用法不一致 | cd_engine.rs:404-411 vs CollisionEngine.h:34-38 + sparrow/config.rs:87-89 |
| 4 | CDEConfig 序列化 | C++ 缺失 | Rust 派生 `Serialize/Deserialize`（快照/配置 JSON 用）；C++ 手工比对（Assertions.cpp:25-31 `sameCdeConfig`） | cd_engine.rs:403 vs 无 |
| 5 | 错误形态 anyhow/thiserror ↔ Result/ErrorCode | 基本等价 | panic→Result 系约定的根源；错误码 14 值覆盖 io/几何/碰撞全路径 | import.rs:10 vs result.h:10-64 |
| 6 | `NumericPolicy` 容差体系 | C++ 额外 | 无 Rust 对应；1e-9 体系服务校验层 | NumericPolicy.h:8-51 vs 无 |
| 7 | 版本标识 | 差异（各自表述） | Rust 0.9.0（Cargo）；C++ "0.1.0-dev" + ApiVersion=2 探针，无对齐语义 | Cargo.toml:3 vs Version.cpp:7-21 |
| 8 | `document_features` 文档 | C++ 缺失 | 随 feature 体系消失 | lib.rs:15 vs 无 |
| 9 | 模块组织（mod 声明 ↔ 目录） | 基本等价 | util 模块在 C++ 分散到 Geometry/Support（记录 38/39） | lib.rs:17-33 vs Include/JaguarCore/ 目录 |

## 结论

库级配置的形态差异大于行为差异：C++ 移除了 feature 门控与 `Instant`/时间戳体系（三问题类型全量编译、计时责任上移调用方），错误处理以 `Result`/错误码完整替代 anyhow/thiserror（即本系列"panic→Result"约定的源头），`NumericPolicy` 为额外自有体系。唯一实质性语义差异是 CDEConfig 默认值：C++ 自造 `4/32` 默认而 Rust 无默认（参考调用方 sparrow 实际取 `4/64`），零初始化路径下 C++ 的 cd_threshold 与参考用法不一致，移植调参时需注意。

### 补充审计（2026-09-29）

对 lib.rs 36 行 + Cargo.toml 42 行逐行复核。原记录已覆盖模块声明/features/`document_features`/`Instant`/CDEConfig/错误形态/NumericPolicy/版本/clippy 共 9 项；补充遗漏/需修正各点：

1. **Cargo.toml `[dependencies]` 18 项依赖的映射全景（遗漏）**——原记录未逐项盘点依赖。Rust 依赖 → C++ 承担方式：
   - `serde`（17 处 derive/手工 impl）→ JsonCodec.cpp 手工解析/序列化（1977 行，记录 33/35/37）；
   - `anyhow`/`thiserror` → result.h（记录 40 差异 5）；
   - `float-cmp` → RustF32.h + Importer.cpp 私有拷贝（记录 38）；
   - `itertools`（unique/all_unique/collect_vec 等）→ std::set/手工循环（Importer.cpp:158-168 等）；
   - `svg` crate → SvgExport.cpp 手工拼 XML（记录 36）；
   - `rayon` → 顺序循环（记录 37 差异 9）；
   - `slotmap` → `handle{index, generation}` 手工实现（Assertions.cpp/placed item 处处）；
   - `log` → 无对应（C++ 无日志设施）；
   - `ndarray`/`rand_distr`/`getrandom`/`ordered-float`/`geo-types`/`geo-buffer` → 库内使用点分散（ndarray 系 surrogate 配置矩阵、geo-types/geo-buffer 系 sp 特性，C++ 用 std 容器替代）；
   - `document-features`/`web-time` → 无对应（记录 40 差异 8/2）。
   判定：依赖层面已全部有承接或显式缺失记录，无新增缺口。
2. **`Instant` 类型别名的 `pub type`（浅覆盖→补齐 C++ 替代细节）**——Rust `pub type Instant = web_time::Instant;`（lib.rs:36）为**类型重导出**（非 newtype），Rust 调用方 `use jagua_rs::Instant` 获得 web 兼容 Instant。C++ 无时间抽象，`runTimeSec` 参数（JsonCodec.h:37,42,47 默认 0）+ 调用方计时。原记录差异 2 已覆盖，补充：`Instant::now()` 的调用点在 Rust `*Solution::new`（probs entities 层，记录 27/29/31 范畴），本记录不重复。
3. **`#![warn(clippy::pedantic ...)]` + `#![allow(missing_panics_doc, missing_errors_doc)]`（原记录 73 行已提，补齐 allow 项）**——原记录只提 warn 集合，遗漏 `#![allow]`（lib.rs:9）：显式豁免 panics/errors 文档 lint——印证本系列"panic 是库的显式行为"的定位（与 C++ panic→Result 约定的背景一致）。判定：工具链差异，无行为影响。
4. **Cargo.toml `[package.metadata.docs.rs] all-features = true`（遗漏）**——docs.rs 构建时全 feature 开启。无 C++ 对应（无 feature 体系）。判定：随差异 1/8 消失。
5. **`probs/mod.rs` 的 feature 门控（原记录 27 行提到，补齐证据）**——`#[cfg(feature = "spp")] pub mod spp;` 等（probs/mod.rs）。C++ Problems/ 三组头全量。原记录差异 1 成立。
6. **CDEConfig 差异 3 的现状复核（无变化）**——CollisionEngine.h:35-36 仍为 `quadtreeDepth = 4 / cdThreshold = 32`；sparrow/src/config.rs:88-89 仍为 `4/64`。原记录差异 3 **继续成立**（未在 9-29 改动中修正）。提醒：若 SparrowCore 调用方零初始化 `ImporterConfig{}`，将得到 cdThreshold=32 而非参考的 64——需调用方显式传值。
7. **`SPSurrogateConfig` 默认值交叉核验（补齐）**——原记录 59 行称"C++ `{}` 默认构造；Rust 无 Default（sparrow 给 …）"。补充：C++ `SurrogateConfig{}` 的成员默认值若与 sparrow 实际值不同，则与差异 3 同性质（默认 vs 参考用法偏离）。记录 15 已覆盖该结构体，本记录交叉引用不重复判定。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | 18 项依赖映射全景 | 一致（补齐盘点） | 每项依赖均有承接或显式缺失记录 | Cargo.toml:13-30 vs 各 C++ 模块 |
| S2 | `#![allow(missing_panics_doc/errors_doc)]` | 工具链差异（补齐） | 印证 panic 为显式行为的库定位 | lib.rs:9 |
| S3 | `[package.metadata.docs.rs]` | 随 feature 体系消失（补齐） | 无行为影响 | Cargo.toml:40-42 |
| S4 | CDEConfig 4/32 vs sparrow 4/64 | 差异 3 复核成立（未修正） | 零初始化路径取 32 偏离参考用法 | CollisionEngine.h:35-36 + sparrow/config.rs:88-89 |
