# 06. transformation.rs + d_transformation.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/transformation.rs`（223 行）+ `CodeResource/jagua-rs/jagua-rs/src/geometry/d_transformation.rs`（85 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/transformation.h`（48 行）+ `JaguarCore/Source/Geometry/Transformation/Transformation.cpp`（92 行）
**判定**: 不等价（表示模型根本不同：Rust 是 3×3 矩阵（可表任意仿射），C++ 是旋转+平移分解（仅刚体）；矩阵合成路径的舍入与 Rust 存在系统性偏差，但常用刚体链路经 atan2 重构后近似等价）

## 总体结构

- Rust `Transformation`（transformation.rs:13-15）：`matrix: [[NotNan<f32>; 3]; 3]`——完整矩阵表示。所有组合运算（rotate/translate/transform）通过 `dot_prod`（transformation.rs:171-193）做 3×3 矩阵乘法，每个元素 3 次 f32 乘法 + 2 次 f32 加法，舍入逐次发生。
- Rust `DTransformation`（d_transformation.rs:11-16）：`(rotation: NotNan<f32>, translation: (NotNan<f32>, NotNan<f32>))`——分解表示（rotate-then-translate）。
- C++ `Transform2d`（transformation.h:11-47）：`rotationRad + translation`——**只实现了 DTransformation 的表示模型**，没有 3×3 矩阵形态。Rust 的 `From<DTransformation> for Transformation`（矩阵化）在 C++ 中体现为 Apply/ApplyRustF32 内联展开 `cos*x - sin*y + tx`（Transformation.cpp:19-20, 34-35），等价于 rot_transl_m（transformation.rs:146-154）与向量的乘积。

## 方法逐条比对（transformation.rs）

### empty / Default（transformation.rs:18-24, 119-123）
- C++ 对应：`Transform2d::Identity()`（transformation.h:17）+ 默认成员初始化（rotationRad=0, translation=0）。
- 判定：【一致】
- 证据与说明：EMPTY_MATRIX 为单位阵（transformation.rs:128）↔ rotation=0、translation=(0,0)。数值等价。

### from_translation（transformation.rs:26-31）
- C++ 对应：`FromRotationTranslation(0.0, offset)`（transformation.h:20-24）或直接构造 `Transform2d{0, {tx,ty}}`。
- 判定：【一致】
- 证据与说明：transl_m（transformation.rs:138-143）↔ (rotation=0 → cos=1,sin=0，平移生效)。矩阵元素对应。

### from_rotation（transformation.rs:33-38）
- C++ 对应：`Transform2d{angle, {0,0}}` / `FromRotationTranslation(angle, {0,0})`。
- 判定：【一致】
- 证据与说明：rot_m（transformation.rs:130-136）。注意 sin/cos 求法差异：Rust 用 `angle.sin_cos()`（transformation.rs:131，一次调用同时返回 sin、cos——x86 上常为 SINCOS 指令或同一多项式路径，保证 sin²+cos² 的一致性）；C++ `std::sin(angle)` / `std::cos(angle)`（Transformation.cpp:31-32）**分两次独立调用**。f32 精度下 `sin_cos` 与分别调用的结果：Rust 的 f32 `sin_cos` 内部实为 `sincosf`（f32 libc），MSVC/libstdc++ 的 `sinf`/`cosf` 与 `sincosf` 一般使用相同多项式→同位级结果，但**无保证**（不同 libm 实现可有 1 ULP 差）。列【差异】（1 ULP 级、平台相关）：后续所有 ApplyRustF32/FollowedByRustF32 均受此影响。本条判定【差异】（低危）。

### rotate / translate / rotate_translate / translate_rotate（transformation.rs:40-66）
- C++ 对应：`FollowedBy` / `FollowedByRustF32`（transformation.cpp:66-91）。
- 判定：【差异】（根本性：Rust 矩阵乘法保序舍入，C++ 分解域合成+atan2 重构）
- 证据与说明：Rust `rotate(angle)`（transformation.rs:42-45）= `dot_prod(&rot_m(angle), &self.matrix)`——**矩阵左乘**，组合 T' = R·T，每个矩阵元素经历 3 乘 2 加的 f32 舍入链。C++ `FollowedByRustF32`（Transformation.cpp:71-91）：把 next 视为 rot_transl 矩阵，与本变换矩阵相乘后**只提取** matrix00/matrix10（→ atan2 得新角度）与平移列（Transformation.cpp:80-90）。数值链对比：`matrix00 = nextCos*cos + (-nextSine)*sine`（Transformation.cpp:80）正是 dot_prod 第一行第一列（transformation.rs:178 `l[0][0]*r[0][0] + l[0][1]*r[1][0] + l[0][2]*r[2][0]`，其中 l=next 的 rot_transl 阵、r=self 的矩阵）——**当 self 本身是刚体（rot_transl 形态）时**，第三项 `l[0][2]*r[2][0]` = h*0 = 0，加 0 不改变值，前两项与 C++ 相同。因此对刚体链，matrix00/matrix10 的舍入链一致；随后 C++ 用 `atan2(matrix10, matrix00)`（Transformation.cpp:89）重构角度，而 Rust 保留完整矩阵——**后续应用时**：Rust 用矩阵元素直接乘（m[0][0]=cos 等已含舍入），C++ 用 atan2 角度再 sin/cos（Transformation.cpp:31-32 in ApplyRustF32）——**多一次三角往返**，结果可偏离 Rust 的矩阵元素 1 ULP 级。且 C++ 平移列：`translatedX = nextCos*tx + (-nextSine)*ty + nextTx`（Transformation.cpp:86）对应 dot_prod 第一行第三列（transformation.rs:180 `l[0][0]*r[0][2] + l[0][1]*r[1][2] + l[0][2]*r[2][2]` = nextCos*tx + (-nextSine)*ty + h*1）——一致。综合：**刚体链下每个合成步骤的中间量舍入链基本一致，但 C++ 存储形态损失（角度化+再三角化）引入 1 ULP 级往返误差；非刚体矩阵（Rust 允许任意仿射，如 scale）C++ 完全无法表示**。判【差异】。

### transform (self.transform(other))（transformation.rs:68-73）
- C++ 对应：`FollowedByRustF32(next)`（Transformation.cpp:71-91，`self.matrix = dot_prod(&other.matrix, &self.matrix)` 语义）。
- 判定：【差异】（同上：矩阵域 vs 分解域；方向一致：均为 other 左乘 self）
- 证据与说明：transformation.rs:71 `dot_prod(&other.matrix, &self.matrix)`；C++ `Transform2d::FollowedBy(next)` 命名上"this 之后跟 next"，其实现（Transformation.cpp:66-68，double 版）`rotationRad + next.rotationRad; next.Apply(translation)`——即 M = M_next · M_self 的分解近似。方向一致；数值链差异同上一条。

### transform_from_decomposed（transformation.rs:76-78）
- C++ 对应：`FromRotationTranslation`（transformation.h:20-24）+ `FollowedBy` 组合可复刻，但无专门 API。
- 判定：【C++缺失】（专门方法缺失；因 C++ 本身就是分解表示，`rotate_translate` 语义天然内建）
- 证据与说明：Rust `self.rotate_translate(other.rotation(), other.translation())`（transformation.rs:77）= 左乘 rot_transl(other)。C++ 表示模型下等价于 `FollowedBy(FromRotationTranslation(rot, transl))`。语义可组合达到，无独立入口。

### inverse（transformation.rs:80-85）
- C++ 对应：`InverseRustF32`（Transformation.cpp:49-64）。
- 判定：【差异】（求值路径显著不同：Rust 通用 3×3 伴随矩阵法，C++ 刚体解析式+atan2）
- 证据与说明：Rust `inverse`（transformation.rs:196-223）：先 det（6 乘 3 加的 f32 链，transformation.rs:200-204），再用伴随矩阵每项除 det（transformation.rs:206-221）。对 rot_transl 刚体阵，det = cos²+sin²（f32 下可能 ≠ 1.0，存在 1-2 ULP 偏差），随后每个伴随项都除以这个非精确 det。C++ `InverseRustF32`（Transformation.cpp:49-64）：`determinant = cosine*cosine + sine*sine`（Transformation.cpp:56）——**刻意复刻了 det = cos²+sin² 的 f32 计算**（含其可能的 ≠1 偏差）；`inverseCosine = cosine / determinant`、`inverseSine = -sine / determinant`（Transformation.cpp:57-58）对应伴随除 det；平移项 `inverseX = ((-sine)*ty - tx*cosine) / determinant`（Transformation.cpp:59）——与 Rust 伴随矩阵第三列（transformation.rs:209-210 `(m[0][2]*m[2][1] - m[0][1]*m[2][2]) / det` 等）在刚体特例下需逐项核对：Rust 阵 m = [[c,-s,h],[s,c,k],[0,0,1]]，伴随第三列第一项 = `(m[0][2]*m[2][1] - m[0][1]*m[2][2]) / det` = `(h*0 - (-s)*1)/det = s/det`——而 C++ 的 inverseX 是平移分量（-s·k + c·h 的变体）……对照：C++ 的最终输出是**分解形式**（角度 atan2(inverseSine, inverseCosine) + 平移 (inverseX, inverseY)），Rust 输出是矩阵。平移分量核对：逆变换的平移 = R⁻¹·(-t) = [[c,s],[-s,c]]·(-h,-k) = (-(c·h + s·k), -(-s·h + c·k))。Rust 伴随矩阵路径给出相同值但经 det 除法：伴随元 (2,0)=`(m[1][0]*m[2][1]-m[1][1]*m[2][0])/det` = (s*0 - c*0)/det = 0 ✓；(0,2) = `(m[0][1]*m[1][2]-m[0][2]*m[1][1])/det` = `((-s)*k - h*c)/det` = -(s·k + c·h)/det ✓。C++ Transformation.cpp:59-60：`inverseX = ((-sine)*ty - tx*cosine)/det = -(s·k + c·h)/det` ✓、`inverseY = (tx*sine - cosine*ty)/det = (h·s - c·k)/det`——Rust (1,2) = `(m[0][2]*m[1][0] - m[0][0]*m[1][2])/det` = `(h·s - c·k)/det` ✓。**逐项一致**（含 /det 的次序）。差异点：(1) C++ 最后 `atan2(inverseSine, inverseCosine)`（Transformation.cpp:62）把 c/det、-s/det 折叠回角度，应用时再 sin/cos——**多一次三角往返**（Rust 直接存矩阵元素 c/det 等）；(2) 非刚体矩阵（含缩放/剪切）Rust 通用逆成立，C++ 模型不存在。判【差异】（刚体下中间量逐项一致，存储往返引入 1 ULP 级；非刚体不支持）。

### is_empty（transformation.rs:87-90）
- C++ 对应：无（可用 `rotationRad==0 && translation=={0,0}` 逼近，但无 API；且矩阵 == 与分解 == 判定集不同）。
- 判定：【C++缺失】
- 证据与说明：Rust 判 EMPTY_MATRIX（矩阵逐元素相等——**注意 cos/sin 的 1 ULP 噪声会使"语义上为空"的矩阵不等于 EMPTY_MATRIX**）；C++ 无对应。近似等价物是 Identity 比较，未提供。

### matrix()（transformation.rs:92-95）
- C++ 对应：无（分解表示无矩阵暴露）。
- 判定：【C++缺失】
- 证据与说明：Rust 把矩阵引用给 TRANSFORM_FORMULA（point.rs:26-30）等直接展开使用。C++ 由 ApplyRustF32 内联承担。

### decompose（transformation.rs:97-103）
- C++ 对应：`Transform2d` 本身（表示即分解）；无显式 decompose 方法（不需要）。
- 判定：【一致】（表示模型使然；角度提取 `m[1][0].atan2(m[0][0])`（transformation.rs:100）与 C++ 合成时 atan2 的使用同式）
- 证据与说明：Rust 从矩阵提取角度与平移；C++ 天然存储这两者。刚体矩阵下数值一致（atan2(sin, cos) = angle 的 f32 往返精度）。非刚体矩阵 decompose 会丢失信息（Rust 允许，C++ 不存在该路径）。

### From<DTransformation> for Transformation（transformation.rs:106-117）
- C++ 对应：表示模型内建（Transform2d 即 DTransformation），无转换 API。
- 判定：【一致】（模型统一）
- 证据与说明：rot_transl_m(rot, transl)（transformation.rs:146-154）的矩阵 ↔ Transform2d{rot, transl} 的 Apply 展开（Transformation.cpp:34-35：`cos*x - sin*y + tx` = m[0][0]x + m[0][1]y + m[0][2]）。一致。

### dot_prod / inverse（私有函数，transformation.rs:171-223）
- C++ 对应：`FollowedByRustF32` / `InverseRustF32` 的内联展开（见上）。
- 判定：【差异】（见 rotate 与 inverse 条；矩阵乘法的完整 9 元素舍入链 C++ 不完整保留——只算 2 个旋转元素+2 个平移元素）
- 证据与说明：Rust 保留全部 9 元素（第三行恒 [0,0,1] 实际只需 4+2）；C++ 只算 4 个有效元素（matrix00/matrix10/translatedX/translatedY），**略去的元素恰是恒等行/列，无信息丢失**。数值链上已算元素的乘加次序与 dot_prod 一致（见 rotate 条核对）。差异仅在矩阵形态的保留（影响后续再组合的舍入链起点：Rust 从已舍入的矩阵元素出发继续乘；C++ 从 atan2 重构的 angle 出发重新 sin/cos——**连续多次组合时偏差会累积放大**）。

## 方法逐条比对（d_transformation.rs）

### DTransformation::new（d_transformation.rs:19-28）
- C++ 对应：`Transform2d{rotation, translation}` 聚合构造 / `FromRotationTranslation`（transformation.h:20-24）。
- 判定：【差异】（NaN 校验：Rust NotNan::new().expect panic；C++ 无构造校验，靠 IsFinite 事后检查）
- 证据与说明：d_transformation.rs:22-25 三个 NotNan::new().expect——NaN 输入 panic；C++ 构造无校验（transformation.h:20-24 直接返回），`IsFinite()`（transformation.h:27-30）供使用方前置检查（如 SimplePolygon.cpp:311, 341）。失败通道：panic vs 延迟检查（未检查时静默传播 NaN）。判【差异】（防御形态）。

### empty（d_transformation.rs:30-37）
- C++ 对应：`Identity()`（transformation.h:17）。
- 判定：【一致】
- 证据与说明：rotation=0, translation=(0,0) 两侧同。

### rotation() / translation()（d_transformation.rs:39-47）
- C++ 对应：公有成员 `rotationRad` / `translation`（transformation.h:13-14）。
- 判定：【一致】
- 证据与说明：直接访问器。

### compose（d_transformation.rs:49-52）
- C++ 对应：无专门方法（表示即分解，无需 compose）；等价操作是恒等。
- 判定：【C++缺失】（API；语义上 C++ 模型使其退化为恒等映射）
- 证据与说明：Rust `self.into()`（转矩阵）；C++ 的 Transform2d 直接可 Apply。无信息变化。

### From<Transformation> for DTransformation（d_transformation.rs:55-62）
- C++ 对应：无（不存在矩阵表示，无逆向转换）。
- 判定：【C++缺失】（模型缺失的自然结果）
- 证据与说明：Rust 经 decompose 提取；C++ 无矩阵可提取。

### Display（d_transformation.rs:64-74）
- C++ 对应：无 operator<< / ToString。
- 判定：【C++缺失】（调试输出；无行为影响）
- 证据与说明：`r: {:.3}°, t: (...)` 格式（d_transformation.rs:66-72），含 `to_degrees()`（与 RustF32.h:12-17 `ToDegreesRustF32` 的常量 57.2957795130823208767981548141051703F 对应——该常量出现在 RustF32.h 但**在本层 C++ API 中未被任何方法调用**，属于孤立复刻）。

### normalize_rotation（d_transformation.rs:76-85）
- C++ 对应：无。
- 判定：【C++缺失】
- 证据与说明：`r % (2π)` 后负值加 2π（d_transformation.rs:79-84）。验证背景陈述"缺 normalize_rotation"成立。该函数是 Rust 侧把角度规范到 [0, 2π) 的公共 API（被 entities/quantum 等层使用），C++ 几何层无对应。调用方需自行 `%` 处理。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | 表示模型：3×3 矩阵 vs 旋转+平移分解 | 差异（根本） | C++ 无法表示缩放/剪切等非刚体仿射；Rust 矩阵 API（matrix()、矩阵相等判定）无对应 | transformation.rs:13-15 vs transformation.h:11-14 |
| 2 | 组合运算：dot_prod 全矩阵舍入链 vs atan2 角度重构 | 差异 | 单步刚体合成中间量一致，但 C++ 存储→再三角化多一次往返（1 ULP 级）；多次连续组合时偏差累积 | transformation.rs:171-193 vs Transformation.cpp:80-90 |
| 3 | inverse：通用伴随矩阵法 vs 刚体解析式 | 差异（刚体下中间量逐项一致） | det=cos²+sin² 两侧刻意复刻；C++ 输出经 atan2 折叠回角度（往返误差）；非刚体不可逆 | transformation.rs:196-223 vs Transformation.cpp:49-64 |
| 4 | sin/cos 求值：sin_cos 单调用 vs 分两次调用 | 差异（1 ULP 级、平台相关） | 依赖 libm 的 sincosf 与 sinf/cosf 是否同路径 | transformation.rs:131,147,158 vs Transformation.cpp:31-32, 52-53, 76-79 |
| 5 | normalize_rotation | C++缺失 | 角度规范化公共 API 缺失，上层需自补 | d_transformation.rs:76-85 vs C++ 无 |
| 6 | is_empty / matrix() / From<Transformation> / Display / transform_from_decomposed | C++缺失 | 矩阵形态 API 家族缺失（模型差异的自然结果） | transformation.rs:87-95, 76-78；d_transformation.rs:55-62, 64-74 |
| 7 | DTransformation::new 的 NaN panic vs C++ 无构造校验（IsFinite 事后） | 差异（防御形态） | NaN 角度/平移在 Rust 崩溃、在 C++ 需调用方自觉检查 | d_transformation.rs:22-25 vs transformation.h:20-24, 27-30 |
| 8 | ToDegreesRustF32（RustF32.h:12-17） | C++额外（孤立） | 复刻了 to_degrees 常量但几何层无调用方 | RustF32.h:12-17 |

## 结论

该组是 8 组中偏离最大的一组：C++ 放弃了 Rust 的 3×3 矩阵表示，改用 DTransformation 式分解模型，因此矩阵族 API（matrix、is_empty、通用 inverse、任意仿射组合）整体缺失。在刚体变换这一实际使用域内，C++ 的 ApplyRustF32 与 Rust 的矩阵-向量乘逐元素一致，合成与求逆的中间量（含 det=cos²+sin² 的刻意复刻）也逐项对齐，但"存储角度、用时再 sin/cos"的往返使连续组合存在 1 ULP 级的累积偏差。normalize_rotation 是上层有实际依赖的缺失项。

## 补充审计（2026-09-29）

逐行复核 transformation.rs 全部 223 行（17 个方法/函数 + EMPTY_MATRIX + 4 个矩阵构造函数 + dot_prod/inverse + 2 个 From + Default）与 d_transformation.rs 全部 85 行（6 个方法 + From + Display + normalize_rotation + 常量）。确认主记录已覆盖全部函数。以下为补充核对项：

### 【遗漏→补齐】`Default for Transformation`（transformation.rs:119-123）
- 主记录在 empty 条中提到 Default 但未单列。补充确认：`Default::default() = Self::empty()`（单位矩阵）↔ C++ `Transform2d` 的默认成员初始化（transformation.h:13-14 `rotationRad = 0.0; translation{}`）。**注意一个细节**：C++ 中 `Transform2d{}` 与 `Identity()` 等价，但**聚合初始化 `Transform2d{rot, off}` 不带默认值**（h:20-24 FromRotationTranslation 直接透传）——与 Rust 一致（Rust 聚合字面量也无默认）。一致。
- 判定：【一致】。
- 证据：transformation.rs:119-123 vs transformation.h:13-14。

### 【遗漏→补齐】常量 `_0`/`_1`/`EMPTY_MATRIX`（transformation.rs:125-128）
- `NotNan::new_unchecked(0.0/1.0)` 的 unsafe 常量 + 单位矩阵。C++ 无显式常量（Identity 用 0/默认值隐式表达）。数值等价（0.0/1.0 字面量两侧位级相同）。`unsafe { NotNan::new_unchecked }` 的安全性契约（0.0/1.0 恒非 NaN）无 C++ 对应物，无行为影响。
- 判定：【一致】（数值；形态缺失无影响）。
- 证据：transformation.rs:125-128 vs transformation.h:17。

### 【遗漏→补齐】`From<T: Borrow<Transformation>> for DTransformation`（d_transformation.rs:55-62）——主记录已列但补充消费链核实
- 主记录判 C++ 缺失（无矩阵可提取）。补充全库检索：jagua-rs 核心中该转换的调用方为 `io/ext_repr.rs`、`entities/layout.rs` 等的 `Into<DTransformation>` 链——C++ 侧因模型统一（Transform2d 即分解态）该转换退化为恒等，无行为损失（主记录结论维持）。normalize_rotation 的全库消费检索结果：**jagua-rs 核心代码无任何调用方**（仅 geometry/mod.rs:34 再导出）——主记录称"被 entities/quantum 等层使用"，**经检索不成立**，它是无消费方的公共 API（lbf 仓库或外部用户可能使用）。缺失影响从"上层有实际依赖"降级为"无内部依赖，纯 API 面"。
- 判定：【C++缺失】（维持；影响评级下调）。
- 证据：d_transformation.rs:76-85；全库 grep `normalize_rotation` 仅定义与再导出两处。

### 【浅覆盖→补齐】Display 的 to_degrees 常量核对（RustF32.h:12-17 的孤立性确认）
- 主记录称 ToDegreesRustF32 在几何层无调用。补充全库检索：C++ 中该函数的调用方为 `Source/IO/SvgExport.cpp:90,188` 与 `Source/IO/JsonCodec.cpp:1769`——**并非孤立**，是 IO 层（Rust 的 layout_to_svg.rs:485 / ext_repr.rs:174 的 `dt.rotation().to_degrees()` 对应物）。
- 修正主记录"孤立复刻（差异表 #8）"的表述：该常量是 io 属性转换的真实复刻，归属于 33/36 号记录范围。Rust `to_degrees`（f32）内部 = `self * (180.0/PI)`（core 实现：乘以 57.29577951308232 的 f32 舍入）↔ RustF32.h:14-16 `static_cast<float>(radians) * 57.2957795130823208767981548141051703F`——**乘法与常量位级一致**（同一实数舍入到 f32）。
- 判定：【一致】（常量位级；归属记录修正）。
- 证据：RustF32.h:12-17；SvgExport.cpp:90；layout_to_svg.rs:485；ext_repr.rs:174。

### 【浅覆盖→补齐】FollowedByRustF32 的 matrix10 确认（rotation 非负角方向的合成核对）
- 核对 Transformation.cpp:81 `matrix10 = nextSine * cosine + nextCosine * sine` 对应 dot_prod 第二行第一列（transformation.rs:183 `l[1][0]*r[0][0] + l[1][1]*r[1][0] + l[1][2]*r[2][0]`，l=next 阵 [[c,-s,h],[s,c,k],[0,0,1]]，r=self 阵）= nextSine*cos + nextCosine*sin + k*0 ✓。与主记录 rotate 条的推导一致，此处补齐第二元素核对（主记录只推了 matrix00）。
- 判定：【一致】（维持）。
- 证据：Transformation.cpp:80-81 vs transformation.rs:178-185。

### 【遗漏→补齐】cfg(test)——两文件均无测试模块
- transformation.rs / d_transformation.rs 均无 `#[cfg(test)]`。判定：不适用。

补充结论：06 记录函数级覆盖完整。三处修正/补充：(1) normalize_rotation 在 jagua-rs 核心**无调用方**（主记录"上层有实际依赖"不成立，影响降级）；(2) ToDegreesRustF32 并非孤立——是 IO 层 to_degrees 的真实复刻（归属修正至 33/36 号记录，常量位级一致）；(3) Default/常量/matrix10 均确认一致。
