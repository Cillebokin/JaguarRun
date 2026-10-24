# 06. transformation.rs + d_transformation.rs 逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/geometry/transformation.rs`（223 行）+ `CodeResource/jagua-rs/jagua-rs/src/geometry/d_transformation.rs`（85 行）
**C++**: `JaguarCore/Include/JaguarCore/Geometry/Transformation.h` + `JaguarCore/Source/Geometry/Transformation/Transformation.cpp`
**判定**: 部分等价。Rust公开完整3×3矩阵，C++公开旋转+平移；C++内部现以私有f32四系数保存并传播刚体变换的旋转矩阵状态，有限矩阵与Importer形态定向探针通过。一般仿射能力、公共矩阵API、完整外部角度域及非有限输入仍有差异或未验证。

## 总体结构

- Rust `Transformation`（transformation.rs:13-15）：`matrix: [[NotNan<f32>; 3]; 3]`——完整矩阵表示。所有组合运算（rotate/translate/transform）通过 `dot_prod`（transformation.rs:171-193）做 3×3 矩阵乘法，每个元素 3 次 f32 乘法 + 2 次 f32 加法，舍入逐次发生。
- Rust `DTransformation`（d_transformation.rs:11-16）：`(rotation: NotNan<f32>, translation: (NotNan<f32>, NotNan<f32>))`——分解表示（rotate-then-translate）。
- C++ `Transform2d`（Transformation.h）：公开字段仍为`rotationRad + translation`，但`ApplyRustF32`使用私有四系数f32旋转矩阵状态；`InverseRustF32`与`FollowedByRustF32`也按矩阵系数计算并传播该状态。C++仍不能表示任意缩放/剪切仿射矩阵，也没有Rust公开的完整矩阵访问API。

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
- 判定：【部分等价】（组合方向一致；私有矩阵状态已修复所测有限域的点应用漂移）
- 证据与说明：Rust `rotate(angle)`（transformation.rs:42-45）通过`dot_prod(&rot_m(angle), &self.matrix)`左乘并保留每个矩阵元素的f32舍入结果。C++ `FollowedByRustF32`同样按`next * self`计算四个有效旋转系数和两项平移，并把四个系数保存到私有状态；`atan2(matrix10, matrix00)`只更新公开角度字段，后续`ApplyRustF32`使用缓存矩阵系数，不再由该角度重新计算sin/cos。修复前的同目标探针曾有32/3840个点应用字段超容差；修复后的通用有限探针及Importer形态探针在已测输入内通过。一般缩放/剪切矩阵与完整外部角度、非有限输入仍未覆盖。

### transform (self.transform(other))（transformation.rs:68-73）
- C++ 对应：`FollowedByRustF32(next)`（Transformation.cpp:71-91，`self.matrix = dot_prod(&other.matrix, &self.matrix)` 语义）。
- 判定：【部分等价】（方向一致；f32接口传播矩阵状态，double接口仍是独立实现）
- 证据与说明：transformation.rs:71 `dot_prod(&other.matrix, &self.matrix)`；对应的`FollowedByRustF32(next)`按`M_next * M_self`计算并缓存四个旋转矩阵系数，修复后的有限输入探针通过。公开的double版`FollowedBy`仍按角度+平移实现，不属于该f32对齐路径；一般仿射矩阵仍无法表达。

### transform_from_decomposed（transformation.rs:76-78）
- C++ 对应：`FromRotationTranslation`（transformation.h:20-24）+ `FollowedBy` 组合可复刻，但无专门 API。
- 判定：【C++缺失】（专门方法缺失；因 C++ 本身就是分解表示，`rotate_translate` 语义天然内建）
- 证据与说明：Rust `self.rotate_translate(other.rotation(), other.translation())`（transformation.rs:77）= 左乘 rot_transl(other)。C++ 表示模型下等价于 `FollowedBy(FromRotationTranslation(rot, transl))`。语义可组合达到，无独立入口。

### inverse（transformation.rs:80-85）
- C++ 对应：`InverseRustF32`（Transformation.cpp:49-64）。
- 判定：【部分等价】（所测可表达的有限刚体输入已对齐；一般仿射能力仍有差异）
- 证据与说明：Rust `inverse`（transformation.rs:196-223）对3×3矩阵计算行列式与伴随矩阵。C++ `InverseRustF32`现按`[[m00,m01,tx],[m10,m11,ty],[0,0,1]]`计算行列式及六个有效逆矩阵系数，并把四个旋转系数保存在私有缓存中；`atan2`仅更新公开角度字段，`ApplyRustF32`继续使用缓存矩阵。Importer形态和通用有限探针已在已测输入内通过；任意缩放/剪切矩阵不属于C++公开模型，完整外部角度及非有限输入仍待验证。

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
- 证据与说明：rot_transl_m(rot, transl)（transformation.rs:146-154）生成的矩阵由`ApplyRustF32`通过私有f32旋转系数应用；普通double版`Apply`仍直接用double角度计算。公开的`Transform2d{rot, transl}`仍对应DTransformation输入形态，但内部矩阵状态只在f32兼容路径中维护。

### dot_prod / inverse（私有函数，transformation.rs:171-223）
- C++ 对应：`FollowedByRustF32` / `InverseRustF32` 的内联展开（见上）。
- 判定：【部分等价】（f32兼容路径传播六个有效矩阵元素；公开表示与一般仿射能力仍有差异）
- 证据与说明：Rust内部矩阵第三行固定为`[0,0,1]`，其余六个有效系数覆盖2×2线性块和平移列。C++ `FollowedByRustF32`与`InverseRustF32`计算这些系数并保存四个线性系数及公开平移结果；`ApplyRustF32`继续使用缓存线性块。定向有限输入在已测范围通过。Rust公开完整矩阵API，C++仍公开旋转+平移且不支持任意缩放/剪切；一般角度和非有限值仍未验完。

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
- C++ 对应：全局函数`NormalizeRotationRustF32(float)`（Transformation.h:12、Transformation.cpp:16-22）。
- 判定：【有对应实现；定向探针一致，完整有限f32域未穷尽】
- 证据与说明：Rust执行`r % (2π)`后对负余数加`2π`，C++以`std::fmod`和同样的负余数修正规则实现。MSVC 2019 14.29 Release同目标raw-bit探针中，16个有限弧度样本及NaN、正负无穷共19项输出位模式一致；负零仍保留负号。样本覆盖极大有限值、正负π/τ、次正规数和临近π的相邻f32，但不构成全部有限f32值的穷举。该函数在jagua-rs核心中无内部调用方，是公共API面差异而非核心调用链缺口。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|----|------|------|------|
| 1 | 表示模型：Rust公开3×3矩阵 vs C++公开旋转+平移 | 差异（API/能力） | C++私有四系数状态可保留已支持刚体变换的矩阵系数，但不能表示一般缩放/剪切仿射，也不提供公共`matrix()`和矩阵相等判定 | transformation.rs:13-15 vs Transformation.h |
| 2 | 组合运算：dot_prod 全矩阵舍入链 vs C++私有矩阵系数传播 | 定向有限输入已对齐；全域未证 | C++仍把`atan2`结果写入公开角度字段以兼容调用，但后续`ApplyRustF32`使用缓存系数，不再经该角度重新三角化；有限矩阵与Importer形态探针通过 | transformation.rs:171-193 vs Transformation.cpp:80-105 |
| 3 | inverse：通用伴随矩阵法 vs C++刚体公开模型 | 定向有限输入已对齐；能力仍有差异 | C++现按3×3嵌入矩阵的伴随公式求逆并缓存四个系数；一般非刚体矩阵不属于C++公开模型，完整角度与非有限域未验证 | transformation.rs:196-223 vs Transformation.cpp:49-78 |
| 4 | sin/cos 求值：sin_cos 单调用 vs 分两次调用 | 差异（1 ULP 级、平台相关） | 依赖 libm 的 sincosf 与 sinf/cosf 是否同路径 | transformation.rs:131,147,158 vs Transformation.cpp:31-32, 52-53, 76-79 |
| 5 | normalize_rotation | C++全局`NormalizeRotationRustF32(float)`对应；已测样本逐位一致 | C++是命名空间级helper而非`DTransformation`方法；完整有限f32域仍未穷举 | d_transformation.rs:76-85；Transformation.h:12；Transformation.cpp:16-22 |
| 6 | is_empty / matrix() / From<Transformation> / Display / transform_from_decomposed | C++缺失 | 矩阵形态 API 家族缺失（模型差异的自然结果） | transformation.rs:87-95, 76-78；d_transformation.rs:55-62, 64-74 |
| 7 | DTransformation::new 的 NaN panic vs C++ 无构造校验（IsFinite 事后） | 差异（防御形态） | NaN 角度/平移在 Rust 崩溃、在 C++ 需调用方自觉检查 | d_transformation.rs:22-25 vs transformation.h:20-24, 27-30 |
| 8 | ToDegreesRustF32（RustF32.h:12-17） | C++额外（孤立） | 复刻了 to_degrees 常量但几何层无调用方 | RustF32.h:12-17 |

## 结论

该组仍有公开API形态差异：C++没有Rust公开的3×3 `matrix()`、`is_empty()`和通用矩阵接口。为保持Rust f32变换状态，C++ `Transform2d`现通过私有四系数缓存保留合成/求逆后的旋转矩阵，并在后续`ApplyRustF32`中使用该矩阵；公开角度被修改时缓存按当前角度重建，平移字段始终实时读取。Importer生产形态与有限矩阵定向探针在已测输入内通过；有限外部角度采样通过，但不是全部f32穷举；非有限构造与失败出口仍有契约差异，全部RustF32调用点预算也未收口。该缓存改变了`Transform2d`公开结构体的布局，调用方需用新头文件重编译。`normalize_rotation`已有全局`NormalizeRotationRustF32(float)`对应实现，jagua-rs核心内无消费者。

## E1静态调用深度核对（2026-10-22）

- C++生产调用点：`Importer.cpp:139`执行`InverseRustF32().FollowedByRustF32(...)`；`SvgExport.cpp:195`与`JsonCodec.cpp:1782`各执行一次`FollowedByRustF32`。当前未检出在优化循环内反复组合变换的生产路径。
- 其他`ApplyRustF32`调用直接对几何对象应用分解变换，不再组合`Transform2d`，因此不会累积组合时的角度往返。
- 当时裁决：保留分解表示，暂不加入矩阵状态缓存；该裁决后来被下方同目标数值差分推翻。经Importer形态探针确认存在超容差点应用差异后，用户于2026-10-22批准增加矩阵状态。

## E1有限输入探针（2026-10-22）

- 临时探针依赖锁定的jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`与Rust 1.98.1；C++直接编译当前`Transformation.cpp`。首轮输入包含16个有限角度、有限平移与点坐标，覆盖单步合成、求逆、Importer式“求逆后合成”，共768条路径记录、3840个标量字段。此前Clang 23.1.3 `x86_64-windows-gnu`对Rust MSVC目标的53/3840差异仅是跨运行库探索，现由同目标结果取代，不作实现裁决。
- 修复前使用VS2019 MSVC 14.29 `x86_64-pc-windows-msvc`重跑时，C++ Debug/Release的768条记录逐字节一致；Rust Debug/Release也逐字节一致。两侧同目标比较有32/3840个标量超出`AlmostEqualRustF32`（绝对epsilon或4 ULP），超限字段全部是最终点应用坐标。修复前差异来自组合矩阵经`atan2`压回单角度后再计算三角函数；后续私有矩阵状态改为传播原始f32系数，已由下方变更后探针复核。
- 另以Importer实际形态构造“零旋转中心预变换→InverseRustF32→FollowedByRustF32→ApplyRustF32”同目标探针：角度覆盖整数`-360°…360°`及15个小数/边界角，平移、中心及点坐标均为有限f32，共736条路径、3680个字段。Rust 1.98.1 Release与C++ MSVC 14.29 Release有14个点坐标超出比较容差（旋转/平移字段均逐位一致），最大绝对差`1.5258789e-5`；正角`180°`、`182°`、`204°`、`214°`、`221°`、`260°`等也出现差异。此探针匹配Importer的变换调用形态，但不是完整几何导入fixture。
- 修复前的2026-10-22 JaguarCore Release runner通过既有固定几何差分fixture：变换矩阵探针覆盖10条路径×3个点，另比较10组分解字段，均按既有比较器通过；该fixture未覆盖上述生产形态角度矩阵。修复前`Transform2d`只保留角度和平移，无法在组合后保留Rust矩阵的四个独立f32旋转系数。
- 实施与复验（2026-10-22）：按批准的方案，`Transform2d`增加私有f32旋转矩阵系数与单个角度快照；`InverseRustF32`、`FollowedByRustF32`传播矩阵系数，`ApplyRustF32`直接使用它们。公开角度字段被改写时会使缓存失效并按当前f32角度重建矩阵；平移不参与旋转矩阵，应用时仍从公开平移字段实时读取。既有双参数花括号构造仍由显式构造函数支持。
- 新增的Importer形态回归固定Rust revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`、零角中心预变换、180°外部放置及点`(0,16)`；Rust oracle输出`x=0.7500012516975403`、`y=985`。初次断言误用了探针另一行的期望值，已按该精确输入的Rust与C++独立探针逐位结果修正。回归还覆盖公开角度和平移字段改写，确认角度改写触发缓存失效、平移改写反映到输出。Debug几何专项runner退出码0；Release JaguarCore全量runner从仓库根目录退出码0；Debug全量runner通过几何组后进入既有`runCollisionTests()`，在`coveringShape`对应的Quadtree Debug断言处等待，随后手动中止，因此不计为全量Debug通过。
- SparrowCore消费者复验（2026-10-22）：Qt 6.5.3/MSVC 14.29在临时隔离目录从当前源码重建SparrowCore静态库及Debug/Release测试runner，编译均成功；Release全量runner从仓库根目录退出码0。Debug全量runner启动后持续进行高CPU用例，运行约1分40秒后为控制本轮耗时手动中止，不计为通过。原有增量构建目录曾因MSVC运行库符号缺失而链接失败，隔离全量重编未复现该问题。
- 变更后重跑临时同目标探针：通用有限输入矩阵768条路径×5字段，MSVC Debug/Release各0个超容差字段；Importer形态736条路径×5字段，Release为0个超容差字段。探针只覆盖声明的角度、平移与点集合，不外推全部有限或非有限输入。
- 裁决：此前14/3680个Importer形态差异已在所测集合内关闭；E1仍部分。完整RustF32调用点偏差预算、全部外部角度域与非有限输入整体契约仍未收口。
- 固定几何fixture的逐字段raw-bit补充采样（2026-10-22）：MSVC 2019 14.29 Release临时探针复核角度归一化、DTransformation、Apply、组合顺序、10条路径的基向量应用与分解，以及组合/求逆，共117项。116项raw bits一致；唯一差异是`geometry.transformation_decompositions[6]`，相差1 ULP（Rust `0x3ec00001`、C++ `0x3ec00000`，绝对差`2.980232239e-08`），低于现有比较器容差。该点来自Rust旋转矩阵`decompose()`的`atan2(sin,cos)`回算与C++直接保留`0.375`输入角度的表示路径差异；不据此外推到其它角度或矩阵。逐字段证据见[Transformation raw-bit记录](../../logic-comparison-jagua/records/80-transform-f32-raw-bits.tsv)。

## E1外部角度与非有限输入边界探针（2026-10-22）

- 使用锁定Rust revision`f5e2b5ffd14931f17a20ce283a2122e877953ceb`、Rust 1.98.1与MSVC 2019 14.29 Release DLL。有限弧度直接构造探针覆盖16个指定f32位模式（±最大有限值、±τ、±π、±0、次正规数、最小正规数、0.375、π相邻f32、2²⁴/2²⁵及1e20）；对单位基向量应用后16/16组四坐标raw bits一致。`NormalizeRotationRustF32`与Rust`normalize_rotation`对相同16个有限值和NaN、±Inf共19项raw bits一致。
- 外部角度直连探针按度解释并转成弧度：Rust`f32::to_radians()`与C++ Importer使用的`degrees * (π/180.0F)`表达式对19个有限输入样本产生相同弧度位模式；以平移`(2.25,-3.5)`变换点`(1.25,-0.75)`后，19/19组输出坐标也逐位一致。样本含±最大有限度、±1e20度、±720/360/180度、360相邻f32、±0、次正规数及90/0.1度。该探针直测角度转换和变换应用，没有经过完整Importer组装链；此前Importer形态探针仍只覆盖其登记的角度集合。这些边界样本不穷举全部有限f32度值。
- 非有限构造契约有差异：Rust`DTransformation::new`对NaN旋转或平移panic；±Inf本身可由`NotNan`保存。±Inf旋转在`ext_to_int_transformation`组装矩阵时经`sin_cos`产生NaN，再由`NotNan::new(...).expect(...)`触发panic。C++`Transform2d`构造器不拒绝NaN/Inf，`IsFinite()`返回false，后续Apply传播NaN/Inf。Importer层也不同：Rust锁定版`ExtTransformation -> DTransformation`先调用`DTransformation::new`，所以NaN旋转/平移panic；Inf旋转随后在矩阵构造panic。Inf平移没有在该转换边界被拒绝，后续矩阵和几何可能传播Inf；本轮静态审查未证明完整`import_solution`最终必然返回或panic。C++ `externalToInternal`在转成f32后对所有非有限旋转/平移返回`SnapshotMismatch`。这组差异属于错误出口/输入契约，不是数值容差差异。
- 探针原始输出及两侧位模式见[Transformation边界raw-bit TSV](../../logic-comparison-jagua/records/80-transform-domain-f32-samples.tsv)。定向样本通过不等于完整有限域证明；非有限异常/错误出口契约仍未统一，E1继续保持部分状态。

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
- 主记录判 C++ 缺失（无矩阵可提取）。补充全库检索：jagua-rs 核心中该转换的调用方为 `io/ext_repr.rs`、`entities/layout.rs` 等的 `Into<DTransformation>` 链——C++ 侧因模型统一（Transform2d 即分解态）该转换退化为恒等，无行为损失（主记录结论维持）。`normalize_rotation`的全库消费检索结果：**jagua-rs核心代码无任何调用方**（仅`geometry/mod.rs`再导出）——主记录称"被entities/quantum等层使用"，**经检索不成立**；它是无内部消费者的公共API。C++现有全局`NormalizeRotationRustF32(float)`已覆盖其数值操作，剩余仅为命名/API形态差异。
- 判定：【数值helper已对应；方法形态不同】。
- 证据：d_transformation.rs:76-85；全库检索`normalize_rotation`仅定义与再导出两处；Transformation.h:12、Transformation.cpp:16-22。

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

## E1 `ImportSPSolution`变换非有限入口与f32边界复核（2026-10-22）

- Rust类型化入口链：`probs/spp/io/import.rs::import_solution`（63-79）将外部`ExtTransformation`转为`DTransformation`，再调用`ext_to_int_transformation`，完成转换后才进入`prob.place_item`。
- `DTransformation::new`只拒绝NaN，不拒绝±Inf（`d_transformation.rs:20-26`）。对于Inf平移，`ext_to_int_transformation`的`transform_from_decomposed`调用`rotate_translate`；构造的矩阵在平移槽含Inf，`dot_prod`计算线性矩阵项时会执行`Inf * 0`，得到NaN。矩阵元素类型`NotNan<f32>`不接受NaN运算结果，因此静态推导该调用在`.decompose()`及`prob.place_item`之前panic，`import_solution`不会以`Result::Err`返回。
- C++ `Importer::ImportSPSolution`调用`externalToInternal`前先将外部值收窄为f32；该助手在旋转或任一平移非有限时直接返回`SnapshotMismatch`（`Importer.cpp:116-135,519-551`）。因此两侧失败通道不同。
- JSON路径与类型化路径分开：锁定`serde_json 1.0.151`默认feature为`std`，当前workspace未启用`float_roundtrip`。Rust解析有限JSON数值`3.5e38`时，serde_json交给f32 visitor的值为有限f64；serde的`visit_f64`经`as f32`得到`+Inf`，本轮Debug/Release oracle均确认随后在`NotNan`矩阵乘法panic。C++ `JsonCodec`的`strtod`先得到有限double，但`numberValue`立即收窄为f32并拒绝Inf，`ImportSPSolutionJson`返回`SerializationFailure`；类型化`ExternalSPSolution`则绕过该JSON收窄点，在Importer返回`SnapshotMismatch`。
- rotation字段的同值探针：JSON `rotation: 3.5e38`在Rust Debug/Release解析为`+Inf`，`DTransformation::new`接受Inf，随后`rot_transl_m`计算`cos(Inf)`得到NaN并触发`NotNan` panic；C++ JSON入口返回`SerializationFailure`，类型化`rotationDegrees = 3.5e38`入口返回`SnapshotMismatch`。C++ MSVC14.29 Release全量runner已确认这两条出口。
- 符号与上界补测：rotation和translation中的JSON `-3.5e38`在Rust Debug/Release均解析为`-Inf`并于导入中panic；C++ JSON入口对两字段均返回`SerializationFailure`，类型化入口对两字段均返回`SnapshotMismatch`。JSON `3.4028234663852886e38`在Rust两字段解析为`f32::MAX`；C++ `ParseExternalSPSolutionJson`成功，收窄到f32也等于`std::numeric_limits<float>::max()`。这个具体JSON样本仍只验证解析/窄化；其临界前驱值的完整Importer几何已在下一条单独验收。
- 舍入溢出临界点：正向中点为`2^128 - 2^103 = 340282356779733661637539395458142568448`；其前一个f64值为`340282356779733623858607532500980858880`。Rust serde Debug/Release将前值舍入为`±f32::MAX`（raw bits `0x7f7fffff`/`0xff7fffff`），将正负中点舍入为`±Inf`（`0x7f800000`/`0xff800000`）。C++ JsonCodec接受前值并拒绝正负中点，返回`SerializationFailure`。这部分只验证JSON解析和f64→f32舍入边界；完整Importer结果见下一条。
- 完整`ImportSPSolutionJson`复核使用与C++现有fixture相同的SPP实例（item id 201，2×2矩形，strip height 4）。前驱值放在rotation、translation保持`[2,2]`时，Rust Debug/Release与C++ MSVC Release均成功；最终变换raw bits均为`bf45abe3,405a8071,400132dd`，面积均为4，包围盒raw bits均为`3fffffff,3f1ac9ae,409a8071,405bb34e`，四个顶点raw bits逐一相同（`3fffffff,40000000`、`405bb34e,3f1ac9ae`、`409a8071,400265ba`、`40594d94,405bb34e`）。前驱值改放translation x、rotation=0时，Rust Debug/Release在`SPolygon::transform`重算包围盒时因`x_min == x_max == f32::MAX`触发panic；C++ JSON解析成功，`TransformedRustF32`的包围盒有效性检查将同一退化形状作为`InvalidInput: invalid rectangle`返回。该差别是完整导入失败通道差异，不是解析舍入差异。
- **本轮证据**：Rust Debug/Release `--import-boundary-parity-only`与`cargo fmt --check`通过；MSVC 2019 14.29 C++ Release `JaguarCoreTests.exe`全量runner exit 0，`TestJsonCodec`新增完整导入断言与raw-bit几何对拍。该C++ Debug全量runner未在本轮复跑；已知Quadtree测试断言不计入本次证据。
- **证据等级**：Rust JSON解析与导入路径已由Debug/Release `--import-boundary-parity-only`确认，均exit 0；C++ `TestJsonCodec` JSON入口与`TestIo`类型化入口已在MSVC14.29 Release `JaguarCoreTests`全量runner中确认，exit 0。既有输入结果不能外推为所有非有限值或平台的统一契约。
