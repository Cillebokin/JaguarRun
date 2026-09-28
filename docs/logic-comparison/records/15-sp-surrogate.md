# 15. SP Surrogate（fail_fast/sp_surrogate.rs）逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/fail_fast/sp_surrogate.rs`（170 行）
**C++**: `JaguarCore/Include/JaguarCore/FailFast/Surrogate.h`（92 行）+ `JaguarCore/Source/FailFast/Surrogate.cpp`（724 行，本记录覆盖其余部分：config 归一、ff 前缀、hull 索引、生成顺序、Transformed；poles/piers 部分见记录 13/14）
**判定**: 基本等价（配置、ff 前缀缓存、hull 索引语义一致；生成顺序不同：hull↔poles 位置互换；ff_poles()/ff_piers() 前缀切片 API 缺失；hull 点匹配回索引的机制不同）

## 函数逐条比对

### `struct SPSurrogate`（Rust 17-30 行）
- C++ 对应：`class Surrogate`（Surrogate.h:52-91）。
- 判定：一致（字段一一对应）：`poles` ↔ `m_Poles`（h:85）；`piers` ↔ `m_Piers`（h:86）；`convex_hull_indices` ↔ `m_ConvexHullIndices`（h:88）；`convex_hull_area` ↔ `m_ConvexHullArea`（h:89）；`config` ↔ `m_Config`（h:90）；`n_ff_poles` ↔ `m_FailFastPoleCount`（h:87）。

### `SPSurrogate::new`（Rust 35-72 行）
- C++ 对应：`Surrogate::Create`（Surrogate.cpp:575-641）。
- 判定：基本等价（核心逻辑一致，顺序不同，见差异 #2）。

**ff_pole_area_ratio 校验**（Rust 36-39：`ensure!((0.0..=1.0).contains(...))`）
- C++ cpp:580-586：`!isfinite || < 0.0 || > 1.0` → `InvalidInput "invalid fail-fast pole area ratio"`。
- 判定：一致（闭区间 [0,1] 含边界；C++ 额外挡 NaN/Inf——Rust `contains` 对 NaN 为 false 也会失败，等价）。

**config f32 归一**（C++ cpp:578-579 + `normalizeSurrogateConfigRustF32` cpp:15-26）
- Rust：无对应步骤（`SPSurrogateConfig` 字段本身就是 f32，序列化即 f32）。
- C++：`SurrogateConfig` 存 double，故在使用前把 `poleLimits[].second`（阈值）与 `failFastPoleAreaRatio` 过一遍 `float` 再转回 double。
- 判定：C++ 额外（模拟层）。语义即"把 double 配置收敛到 Rust f32 可表示值"，与 Rust 直接持有 f32 等价；`==` 运算符（h:25-41）比较时同样先 float 化，自洽。

**凸包计算**（Rust 40-45：`convex_hull::convex_hull_indices(simple_poly)` → 取点 → `SPolygon::calculate_area`）
- C++ cpp:615-636：`convexHullRustF32(shape.Vertices())`（cpp:507-539，Andrew monotone chain：stable_sort by f32 x、下链/上链 cross ≤ 0 弹出、去尾拼接——与 Rust `convex_hull.rs` 的实现对应，见记录 07）→ 逐 hull 点**在原顶点表中线性查找** f32 坐标相等者回索引（cpp:618-635），找不到报 `InvalidPolygon "convex hull point is not present in the polygon"`；`polygonAreaRustF32(hull)`（cpp:541-557，`(yi+yj)*(xi-xj)` 累加公式）。
- 判定：一致（语义：hull 顶点 → 原多边形索引 + hull 面积）。实现机制差异：Rust `convex_hull_indices` 在算法内直接产出索引；C++ 先算 hull 点再反查索引。反查用 f32 相等匹配，正常输入（hull 点来自原顶点集）必命中；重复坐标顶点时 Rust 取算法自然序、C++ 线性查找取**首个**匹配——在重合顶点场景下索引可不同（记录 07 已记录该点）。

**poles 生成**（Rust 46：`pole::generate_surrogate_poles(simple_poly, &config.n_pole_limits)?`）
- C++ cpp:588-591：`generatePoles(shape, rustConfig)`（见记录 13）。一致。

**ff 前缀计数**（Rust 47-59）
```rust
let n_ff_poles = if config.ff_pole_area_ratio == 0.0 { 0 } else {
    let target_area = simple_poly.area * config.ff_pole_area_ratio;
    let mut covered_area = 0.0;
    poles.iter().position(|pole| { covered_area += pole.area(); covered_area >= target_area })
        .map_or(poles.len(), |i| i + 1)
};
```
- C++ cpp:593-606：
```cpp
if (static_cast<float>(rustConfig.failFastPoleAreaRatio) != 0.0F) {
    for (const Circle2d& pole : poles) {
        poleArea += circleAreaRustF32(pole); ++failFastCount;
        if (poleArea >= targetArea) break;
    }
}
```
- 判定：一致。逐条：`ratio == 0 → 0`（Rust 47-48 ↔ cpp:597 条件不成立保持 0）；`target = area * ratio`（Rust 50 ↔ cpp:595-596，f32 化）；累加 `>= target` 即停（Rust 54-58 ↔ cpp:601-604）；未达标取全部（Rust `map_or(poles.len(), i+1)`：position 找到则 i+1（含达标那颗），找不到则 len ↔ C++ 循环自然耗尽取全部）。边界：Rust `position` 的闭包先加再判（达标 pole 计入），C++ 同序（先 `++` 再判 break，达标 pole 计入）。逐字对齐。

**piers 生成用 ff 前缀 poles**（Rust 60-62：`&poles[..n_ff_poles]` → `generate_piers(simple_poly, config.n_ff_piers, relevant)`）
- C++ cpp:608-613：`polesForPiers(poles.begin(), poles.begin() + failFastCount)` → `generatePiers(shape, config.failFastPiers, polesForPiers)`，失败透传。
- 判定：一致（含 eef0830 后的 Result 透传，见记录 14）。

**生成顺序**（结构差异）
- Rust 40-62：hull（40-45）→ poles（46）→ ff 前缀（47-59）→ piers（61-62）。
- C++ cpp:588-615：poles（588）→ ff 前缀（593）→ piers（610）→ hull（615）。
- 判定：差异（顺序互换，无行为影响）。hull 计算不依赖 poles/piers，poles/piers 不依赖 hull；两段互为独立纯函数，顺序不影响任何输出值。唯一可见差别是失败时哪一步先报错（如 hull 反查失败 vs poles 超限同时存在时，Rust 先报 hull、C++ 先报 poles——正常输入不并发失败）。

### `ff_poles()`（Rust 76-78 行：`&self.poles[..self.n_ff_poles]`）
- C++ 对应：`FailFastPoleCount()`（h:64，cpp:653-656）只暴露计数，无前缀切片 API。
- 判定：C++ 缺失（API 形态）。调用方需 `poles.begin(), begin+count` 自行取前缀；信息等价（count + 完整 poles 即可重构前缀），但缺便捷访问器。

### `ff_piers()`（Rust 81-83 行：`&self.piers`）
- C++ 对应：`Piers()`（h:62）返回全部 piers——语义相同（Rust 的 ff_piers 本就返回全部 piers，无前缀截断）。
- 判定：一致（Rust 的 ff_piers 是全量；C++ `Piers()` 同为全量，仅命名不带 ff 前缀）。

### `impl Transformable for SPSurrogate`（Rust 86-110 行：in-place 变换 poles/piers）
- C++ 对应：`Transformed`（cpp:674-697，double 精度）与 `TransformedRustF32`（cpp:699-723，f32 精度）——返回新对象而非 in-place。
- 判定：一致（数学等价）。逐点变换 poles 的 center 与 piers 的 start/end；hull 索引/面积/config/ff 计数不变（Rust 90-95 解构显式跳过这些字段 ↔ C++ 直接按成员拷贝）。C++ 额外有非 finite 变换守卫（cpp:676-680, 701-705）。`TransformedRustF32` 用 `ApplyRustF32` 对齐 Rust f32 路径（Rust 的 `Transformation` 全程 f32）。

### `impl TransformableFrom`（Rust 112-137 行：参照变换）
- C++ 对应：无（`Transformed`/`TransformedRustF32` 均为绝对变换）。
- 判定：C++ 缺失。`transform_from` 用于"以参照代理为基准应用相对变换"（`p.transform_from(&ref_p, &t)` 即 `p = ref_p 变换后的值`），是 jagua-rs 增量布局的优化路径；C++ 调用方用整体重变换承担（结果数值可能有 f32 结合次序差，见记录 06）。数学上 `transform_from` 等价于"先算 reference 的绝对值再赋给 self"，故 C++ 用 `Transformed` 可得相同几何。

### `const N_POLE_LIMITS: usize = 3`（Rust 140 行）
- C++ 对应：`std::array<std::pair<std::size_t, double>, 3>`（h:20）。
- 判定：一致（3 个上限槽位）。

### `struct SPSurrogateConfig`（Rust 143-159 行）
- C++ 对应：`struct SurrogateConfig`（h:17-49）。
- 判定：一致（`n_pole_limits: [(usize, f32); 3]` ↔ `poleLimits` array 3；`ff_pole_area_ratio` ↔ `failFastPoleAreaRatio`；`n_ff_piers` ↔ `failFastPiers`）。`operator==`（h:25-48）对应 Rust derive 的 `PartialEq`（f32 字段按 float 化比较，模拟 f32 相等；usize 精确比较）。

### `SPSurrogateConfig::none()`（Rust 161-170 行）
- C++ 对应：`SurrogateConfig` 的成员默认值（h:20-22：`poleLimits{{0,0}×3}`、`failFastPoleAreaRatio=0.0`、`failFastPiers=0`）。
- 判定：一致（默认构造即 none 语义：全 0 上限（`coverage > 0` 首轮即触发 `len >= 0` 停止——即只保留初始 POI……注意 Rust `[(0, 0.0); 3]` 同样 `n_poles=0`，首轮 coverage>0 即停，等价）、ratio 0（无 ff poles）、piers 0）。
- 差异（API 形态）：Rust 提供显式 `none()` 构造器；C++ 靠默认值，无具名 none()。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 结构字段（poles/piers/hull 索引/hull 面积/config/ff 计数） | 一致 | 无 | Rust 17-30 vs Surrogate.h:52-91 |
| 2 | 生成顺序 hull→poles→piers vs poles→piers→hull | 差异 | 无行为影响（互不依赖）；并发失败时报错先后不同 | Rust 40-62 vs Surrogate.cpp:588-636 |
| 3 | ff_pole_area_ratio ∈ [0,1] 校验 | 一致 | 无 | Rust 36-39 vs cpp:580-586 |
| 4 | `normalizeSurrogateConfigRustF32`（double→f32 归一） | C++ 额外 | 模拟层，等价于 Rust 字段原生 f32 | cpp:15-26, 578-579 |
| 5 | ff 前缀计数（ratio=0→0；累加 ≥ target 停；未达标全取） | 一致 | 无 | Rust 47-59 vs cpp:593-606 |
| 6 | piers 用 ff 前缀 poles + Result 透传 | 一致 | 无（eef0830 后透传） | Rust 60-62 vs cpp:608-613 |
| 7 | hull：monotone chain + 索引 + f32 面积 | 一致 | 重复坐标顶点时索引反查可取不同匹配 | Rust 40-45 vs cpp:507-557, 615-636 |
| 8 | `ff_poles()` 前缀切片 API | C++ 缺失 | 调用方需 count+Poles() 自行重构前缀 | Rust 76-78 vs h:60-64 |
| 9 | `ff_piers()`（实为全量 piers） | 一致 | 仅命名差异 | Rust 81-83 vs h:62 |
| 10 | 变换：in-place transform ↔ Transformed/TransformedRustF32 返回新对象 | 一致 | 数学等价；C++ 额外非 finite 守卫 | Rust 86-110 vs cpp:674-723 |
| 11 | `transform_from`（参照式变换） | C++ 缺失 | C++ 用绝对重变换承担；数值结合次序可差 | Rust 112-137 vs h:56-75（无） |
| 12 | `SPSurrogateConfig::none()` 显式构造器 | 一致 | C++ 以默认成员值承担 | Rust 161-170 vs h:20-22 |
| 13 | `PartialEq` ↔ `operator==`（f32 化比较） | 一致 | 无 | Rust 143 vs h:25-48 |

## 结论
SPSurrogate 的数据模型（六字段）、ff 前缀计数算法（ratio 归零、累加达标即停、未达标全取）、hull 索引与面积、配置校验与 none 语义、变换语义在双侧完全对齐，eef0830 后 piers 的错误传播也已接通。差异均为形态层面：生成顺序 hull↔poles 互换（互不依赖、无行为影响）、`ff_poles()`/`transform_from` 两个 API 缺失（调用方可用现有接口等价重构）、double 配置经 f32 归一模拟、hull 点反查索引在重复坐标下可能取不同匹配。正常输入下生成的代理内容一致。
