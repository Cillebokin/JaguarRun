# 09. OriginalShape 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/original_shape.rs`（73 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/OriginalShape.h`（22 行）+ `JaguarCore/Source/Geometry/OriginalShape/OriginalShape.cpp`（47 行）
**判定**: 基本等价（管线顺序一致；`debug_assert!` 升级为正式错误返回；C++ 各子步骤带 `Result` 错误传播）

## 函数逐条比对

### `struct OriginalShape`（Rust 15-20 行）
- C++ 对应：`OriginalShape.h:11-20` 的 `struct OriginalShape`。
- 判定：一致。
- 证据：字段一一对应——`shape: SPolygon` ↔ `SimplePolygon shape`（h:13）；`pre_transform: DTransformation` ↔ `Transform2d preTransform`（h:14）；`modify_mode: ShapeModifyMode` ↔ `ShapeModifyMode modifyMode`（h:15）；`modify_config: ShapeModifyConfig` ↔ `ShapeModifyConfig modifyConfig`（h:16）。
- 差异说明（类型层面，不影响逻辑）：Rust 的 `SPolygon` 自带缓存字段（`area`/`bbox`/`diameter`），C++ `SimplePolygon` 为按需计算（见记录 05）；`DTransformation` 为"相对参考系的变换"，C++ 直接用 `Transform2d`（见记录 06）。

### `convert_to_internal(&self) -> Result<SPolygon>`（Rust 23-52 行）
- C++ 对应：`OriginalShape.cpp:5-46` 的 `OriginalShape::ConvertToInternal`。
- 判定：基本等价（步骤顺序、条件完全一致；断言语义与错误传播不同）。

按管线逐步对照：

**步骤 1：预变换**（Rust 25 行 `self.shape.transform_clone(&self.pre_transform.compose())`）
- C++：`OriginalShape.cpp:7` `shape.TransformedRustF32(preTransform)`，失败时 cpp:8-9 直接透传错误。
- 判定：一致（`RustF32` 后缀保证 f32 精度路径对齐；`compose()` 的展开由 `Transform2d` 直接承载，见记录 06）。

**步骤 2：offset**（Rust 27-32 行：`if let Some(offset) = ... offset != 0.0` 才执行 `offset_shape`）
- C++：cpp:12-18 `if (modifyConfig.offset.has_value() && *modifyConfig.offset != 0.0)`，调用 `OffsetShape`，失败透传。
- 判定：一致（守卫条件与 Rust 完全相同：`Some` 且非零）。

**步骤 3：simplify 主步骤**（Rust 33-36 行）
- C++：cpp:19-26，`SimplifyShape` 失败透传（cpp:24-25）。
- 判定：一致。Rust 的 `simplify_shape` 不返回 `Result`（内部 `SPolygon::new().unwrap()` 可能 panic），C++ 版本改为 `Result` 并在此透传——这是记录 10 中"panic→Result"约定差异的传播点，此处只是承接。

**步骤 4：close_narrow_concavities + tolerance/10 再简化**（Rust 37-43 行）
- C++：cpp:27-36。守卫 `modifyConfig.narrowConcavityCutoff.has_value()`（cpp:27）↔ Rust 37 行；`SimplifyShape(..., *modifyConfig.simplifyTolerance / 10.0)`（cpp:31-32）↔ Rust 42 行 `tolerance / 10.0`。
- 判定：一致。常量 `tolerance / 10.0` 双侧相同。差异仅错误通道：`SimplifyShape` 的 `Result` 在 cpp:33-34 透传，Rust 无此路径（panic）。

**步骤 5：有效性断言**（Rust 44-48 行）
```rust
debug_assert!(shape_modification_valid(
    &pre_simplified, &internal, self.modify_mode
));
```
- C++：cpp:38-43
```cpp
if (!ShapeModificationValid(preSimplified, internal, modifyMode))
{
    return Result<SimplePolygon>::FromError(
        {ErrorCode::InvalidPolygon,
         "shape modification does not preserve inflate/deflate relation"});
}
```
- 判定：差异（debug 语义升级为正式错误）。
- 说明：Rust 的 `debug_assert!` 仅在 debug 构建生效，release 下无效修改会静默通过；C++ 在所有构建下返回 `InvalidPolygon` 错误。校验本体 `shape_modification_valid` ↔ `ShapeModificationValid`（Simplify.cpp:280-309）逐行等价（见记录 10）。这是"更严格"方向的偏差：debug 构建下行为一致（都会中止），release 构建下 C++ 会拒绝被 Rust 接受的形状。
- 注意：`pre_simplified` 的取样点一致——Rust 34 行在 simplify 之前克隆，C++ cpp:21 `const SimplePolygon preSimplified = internal;` 同样在 simplify 之前。

### `centroid()`（Rust 54-57 行）
- C++ 对应：无独立方法；调用方直接使用 `shape.Centroid()`（如 Surrogate.cpp:201 `shape.Centroid()`）。
- 判定：C++ 缺失（薄封装，无独立逻辑，等价承载）。

### `area()`（Rust 59-62 行）
- C++ 对应：无独立方法；`SimplePolygon::Area()` 按需计算。
- 判定：C++ 缺失（薄封装；Rust 读缓存字段 `shape.area`，C++ 每次重算，见记录 05）。

### `bbox()`（Rust 64-67 行）
- C++ 对应：无独立方法；`shape.BoundingBox()` 按需。
- 判定：C++ 缺失（薄封装）。

### `diameter()`（Rust 69-72 行）
- C++ 对应：无独立方法；`shape.Diameter()` 按需。
- 判定：C++ 缺失（薄封装）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `convert_to_internal` 管线（transform→offset→simplify→concavity→simplify/10） | 一致 | 无 | Rust 25-43 vs OriginalShape.cpp:7-36 |
| 2 | offset 守卫 `Some && != 0.0` | 一致 | 无 | Rust 27-29 vs cpp:12 |
| 3 | 二次简化 `tolerance / 10.0` | 一致 | 无 | Rust 42 vs cpp:31-32 |
| 4 | `shape_modification_valid` 断言：debug_assert → 正式错误 | 差异 | release 构建下 C++ 拒绝 Rust 静默接受的形状（更严格） | Rust 44-48 vs cpp:38-43 |
| 5 | 子步骤错误传播（TransformedRustF32/OffsetShape/SimplifyShape 均可失败） | C++ 额外 | Rust 对应路径是 panic/unwrap；C++ 返回 `Result` | cpp:8-9, 15-17, 24-25, 33-34 |
| 6 | `centroid/area/bbox/diameter` 四个访问器 | C++ 缺失 | 无逻辑影响，调用方直接用 `SimplePolygon` API | Rust 54-72 vs h:11-20（无对应成员函数） |
| 7 | struct 字段默认值（`preTransform=Identity`、`modifyMode=Inflate`） | C++ 额外 | Rust 无 Default；语义合理，不影响等价 | h:14-15 |

## 结论
`convert_to_internal` 的五步管线（变换→offset→simplify→窄凹陷闭合→tolerance/10 再简化→有效性校验）在双侧顺序与守卫条件上完全一致，核心行为等价。主要偏差是错误通道：Rust 的 `debug_assert!`/unwrap 在 C++ 中全部升级为 `Result` 错误返回，其中有效性断言从 debug-only 变为全构建生效，使 C++ 在 release 下比 Rust 更严格。四个只读访问器（centroid/area/bbox/diameter）在 C++ 中未做独立封装，由 `SimplePolygon` 按需计算承担。

## 补充审计（2026-09-29）

逐行复核 original_shape.rs 全部 73 行（struct 定义 + convert_to_internal + 4 个访问器），确认主记录已覆盖全部条目。以下为补充核对项：

### 【浅覆盖→补齐】offset 守卫的 NaN 语义（`*offset != 0.0` 对 NaN 的行为）
- D5锁定Rust的 `original_shape.rs` 中 `NaN != 0.0` 为 true，因此会进入 `offset_shape`。当前Debug下该Rust调用panic，Release下返回空多边形错误；C++同样通过守卫进入 `OffsetShape`，由 `normalizeOffsetRustF32` 的 `!isfinite` 检查返回 `OffsetFailed`。两侧最终均不产生形状，但错误出口/profile行为不同；11号记录补充了当前基线和证据。OriginalShape层的守卫本身没有额外分叉。
- 判定：【有界一致】（均进入偏移并失败；Rust Debug panic、Rust Release error、C++ Result error 不同）。
- 证据：original_shape.rs:27-32 vs OriginalShape.cpp:12-18 + Offset.cpp:266-270。

### 【浅覆盖→补齐】simplify_tolerance 守卫的嵌套结构（narrow_concavity 依赖 simplify 存在）
- Rust original_shape.rs:33-49：`narrow_concavity_cutoff` 的处理**嵌套在 `simplify_tolerance.is_some()` 块内**——若只配 narrow_concavity 而不配 simplify_tolerance，**窄凹陷闭合不执行**（静默忽略）。C++ OriginalShape.cpp:19-44：同样嵌套于 `simplifyTolerance.has_value()` 内（cpp:27 在 cpp:19 块内）。逐字对齐（含这个"静默忽略无 simplify 的 concavity 配置"的隐性契约）。
- 判定：【一致】（嵌套守卫对齐）。
- 证据：original_shape.rs:33-43 vs OriginalShape.cpp:19-36。

### 【浅覆盖→补齐】debug_assert 的 pre_simplified 取样点在二次简化失败时的行为
- Rust original_shape.rs:34 `pre_simplified = internal.clone()` 在**首次 simplify 之前**取样；若 concavity/10 二次 simplify 产生非法形状（Rust panic 在 simplify 内部 unwrap），断言不执行。C++ cpp:21 同点取样；二次 SimplifyShape 失败直接透传（cpp:33-34），断言（cpp:38-43）同样不执行。两侧失败优先级一致。
- 判定：【一致】。
- 证据：original_shape.rs:34, 44-48 vs OriginalShape.cpp:21, 31-43。

### 【遗漏→补齐】derive 集合（Clone, Debug——无 PartialEq/Default）
- original_shape.rs:10 `#[derive(Clone, Debug)]`；C++ OriginalShape.h:11-20 有默认成员值（preTransform=Identity、modifyMode=Inflate——主记录差异表 #7 已记）。补充确认 Rust 无 Default derive，C++ 的默认值是额外便利——若 C++ 调用方默认构造后不设置 modifyConfig（offset/simplify 均 nullopt），ConvertToInternal 只做 transform，等价于 Rust 显式全 None 配置。无行为分叉。
- 判定：【一致】（维持 #7 判定）。
- 证据：original_shape.rs:10 vs OriginalShape.h:14-16。

### 【遗漏→补齐】cfg(test)——original_shape.rs 无测试模块
- original_shape.rs 73 行无 `#[cfg(test)]`。判定：不适用。

补充结论：09 记录覆盖完整，无遗漏函数。三处补充（NaN 偏移边界、嵌套守卫的静默忽略契约、失败优先级）均确认一致，不改变既有判定。
