# S16. uniform_sampler（均匀采样）+ mod（含 SparrowRng 随机语义）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/sample/uniform_sampler.rs`（121 行）+ `CodeResource/sparrow/src/sample/mod.rs`（6 行）
**C++**: `SparrowCore/Include/SparrowCore/Sampling/UniformRectSampler.h`（43 行）+ `UniformRectSampler.cpp`（161 行）+ `RotationDistribution.h/.cpp`（32+97 行）+ `Random/SparrowRng.h/.cpp`（29+116 行）
**判定**: 基本等价（均匀采样主路径按源码对应；探索中的离散角合法化已修复；区间乘加舍入到上界的指定边界已按 Rust 修复并由 raw-bit Rust/C++ 用例验证；全部 16 个 Continuous 旋转角均有 raw-bit Rust/C++ 用例）

证据范围：`SamplingCoordinateDescentRustReference.tsv` 前 8 行 sample 由 TestSampling.cpp:853-871 按变换容差比较（不是位相等）；`SparrowUniformIndexRustReference.tsv` 的 146 行包含 144 条数据记录，由 TestRng.cpp:169 消费并精确比较；`LbfSeed42Reference.tsv` 与 `rng_reference.rs` / TestRng.cpp:132-160 核对 4 个 `NextU64` 和 4 个 `Uniform01F32` 原始位模式。`LbfSearchRustTrace.tsv` / `SeparatorSearchRustTrace.tsv` 覆盖特定配置下含 Discrete 旋转集的轨迹。`UniformSamplerUpperBoundaryRust.tsv` 对 seed=10、单旋转项和窄区间的 X 坐标逐位核对，覆盖乘加舍入到上界的指定路径；`UniformSamplerContinuousGridRust.tsv` 锁定 16 个角值，C++ `UniformRectSampler` 与 `RotationDistribution` 均逐位匹配；`UniformSamplerSingletonAxisRust.tsv` 核对连续 8 个样本。所有 fixture 只支持列明的 seed、输入和入口，不证明任意几何下的布局逐位相同。

## 函数逐条比对

### `ROT_N_SAMPLES = 16`（uniform_sampler.rs:13）
- C++ 对应：`UniformRectSampler.cpp:15` 与 `RotationDistribution.cpp:10` 各自 `constexpr std::size_t ROT_N_SAMPLES = 16U`。【一致】

### `UniformBBoxSampler::new`（uniform_sampler.rs:30-69）
- C++ 对应：`UniformRectSampler::Create(sampleBbox, item, containerBbox)`（cpp:47-144）+ 单参重载（cpp:37-41，`Create(bbox, item)` = `Create(bbox, item, bbox)`，对应 Rust search.rs:271 的 `new(container_bbox, item, container_bbox)` 两参同传）。
- 逐段核对：
  1. **旋转集构造**（Rust :31-41）：None→`[0.0]`；Discrete(r)→r 原样；Continuous→`i * ((2π)/16)`，i=0..15。C++（cpp:75-100）对应 None/Discrete/Continuous 三种分支，但 Continuous 计算为 `TWO_PI * i / 16`。源码运算顺序不同，但 `UniformSamplerContinuousGridRust.tsv` 使用 Rust `UniformBBoxSampler` 为 16 个角各选固定 seed，并由 Rust 测试核对 fixture；C++ 对同一批 seed 分别经 `UniformRectSampler::Sample` 和 `RotationDistribution::Sample` 采样，全部 f32 raw bits 逐位匹配。当前锁定 Rust 依赖与 MSVC Debug 构建未观察到表达式差异造成的输出差异；不外推到其他编译器浮点选项。【表达式不同；指定构建下 16 个值逐位一致】
  2. **旋转后可行域**（Rust :50-63）：对每个旋转 r，`shape_buffer.transform_from(item.shape_cd, &Transformation::from_rotation(r)).bbox` 得 r_shape_bbox；然后 `cont_x_range = (container.x_min - r_bbox.x_min)..(container.x_max - r_bbox.x_max)`（y 同）；与 sample_bbox 区间求交（`intersect_range`），任一为空则该旋转被 filter_map 掉。C++（cpp:110-140）：`item.CollisionShape().TransformedRustF32(FromRotationTranslation(r, {0,0}))` → BoundingBox → `feasibleXMin = containerXMin - rotatedBounds.xMin` 等 → 与 sampleBbox 取 max/min → `xMin <= xMax && yMin <= yMax` 才收留。数学相同。两点细节：(a) C++ 的 `TransformedRustF32` 是 JaguarCore 提供的"Rust f32 语义变换"通道（名字即声明对齐 jagua-rs 的 f32 变换路径），对应 Rust `transform_from`；(b) Rust `intersect_range`（:94-101）要求两端有限且 start ≤ end，再 `start <= end` 收窄——`start == end`（单点）被保留（`(start <= end).then_some(...)`），C++ `xMin <= xMax` 同样保留单点。【一致】
  3. **空处理**（Rust :65-68）：rot_entries 空 → None；C++（cpp:137-144）entries 空 → `InvalidSampler("uniform sampler has no feasible rotation entry")`。Rust 的 None 是**常规值**（search 的 LBF 路径把 None 解释为"放不下 → 扩宽 strip"，lbf.rs:89-121），C++ 的 Result::FromValue(std::nullopt)（LbfSearch.cpp:154-159）+ 注释明确说明这是刻意保留的语义。【差异】（形式：Option → Result<optional>，行为语义保留）
  4. **bbox 校验**（cpp:48-71）：C++ 要求 bounds 有限且 `xMin<xMax/yMin<yMax`，不接受退化单点轴。Rust jagua-rs `Rect` 的坐标字段公开，`try_new` 有运行时条件校验（rect.rs:14-33），但 UniformBBoxSampler 不通过该构造函数重新验证传入 Rect；其 `intersect_range` 仅检查有限性与 start≤end，接受 start==end 的单点轴（uniform_sampler.rs:94-101）。因此对直接 sampler 输入，Rust 可接受 singleton bbox，而 C++ 返回错误；不是 Rust debug_assert 的简单差异。【边界差异】
- 判定：【指定 Continuous 旋转网格已逐位验证；其他构造输入仍按源码对应】
- 证据：uniform_sampler.rs:30-69 vs cpp:47-144；`UniformSamplerContinuousGridRust.tsv` + `uniform_sampler_continuous_grid_matches_the_rust_reference` + `TestSampling.cpp::TestUniformSamplerContinuousGridRustReference`。C++ UniformRectSampler 与 RotationDistribution 均逐位覆盖 16 个角值；大容器仅确保角项可行，不覆盖任意几何下的 rotation-entry 过滤组合。

### `UniformBBoxSampler::sample`（uniform_sampler.rs:71-81）
- C++ 对应：`UniformRectSampler::Sample`（cpp:146-155）。
  1. **随机消耗顺序**：Rust `rot_entries.choose(rng)`（1 次）→ `sample_axis(x_range)`（0 或 1 次）→ `sample_axis(y_range)`（0 或 1 次）；C++ `rng.UniformIndex(entries.size())` → `SampleAxis(xMin,xMax)` → `SampleAxis(yMin,yMax)`。顺序一致。【一致】
  2. **旋转选择**：Rust `choose` = rand 0.10 IndexedRandom::choose → 内部 `gen_range(0..len)`（u32 Lemire）；C++ `UniformIndex`（SparrowRng.cpp:61-81）复刻 rand 0.10 的 u32→[0,n) Lemire 映射（`u32*bound >> 32` + 拒绝重采样 + 进位修正），`SparrowUniformIndexRustReference.tsv` 共 146 行，其中 144 条数据记录逐值锁定。【一致】
  3. **轴采样公式**（见下条 sample_axis）。C++ 把 x、y 用同一 `SampleAxis` 处理，等价于 Rust 同一 `sample_axis` 两次调用。【一致】
  4. **返回值组装**：Rust `DTransformation::new(r, (x,y))`（f32 字段直接存）；C++ `Transform2d::FromRotationTranslation((double)rotation, {(double)x, (double)y})`（f32→double 展宽，无损）。【一致】
- 判定：【一致】
- 证据：fixture 的 8 个 sample 坐标由 TestSampling.cpp:853-871 按 `TRANSFORM_TOLERANCE=1e-6` 比较；TSV 保存位模式不代表断言是逐位相等。

### `sample_axis`（uniform_sampler.rs:86-92）
- C++ 对应：`SampleAxis`（cpp:17-26）。
  1. **单点区间**：Rust `range.start == range.end` 时直接返回 start（不消耗随机数）；C++ `minValue == maxValue` 返回 minValue（不消耗）。【一致】——单点坐标必须不消耗轴 RNG，否则后续轴/样本的随机流会错位。Rust/C++ `UniformSamplerSingletonAxisRust.tsv` 用相同 JSON、seed=42、可行 X 单点输入连续比较 8 个样本的 X/Y raw bits，验证了该路径。
  2. **区间采样**：Rust `rng.random_range(range.clone())`（f32 Range）在 rand 0.10.3 中走 `UniformFloat::sample_single`，再委托 `sample_single_inclusive`，计算为 `value0_1 * (high-low) + low`，其中 `value0_1` 由 `u32 >> 9` 构造；它不走 `Uniform::new` 的 `new_bounded` scale 预校正。C++ 现执行相同乘加并直接返回结果，不再把 `value >= max` 截到 `nextafter(max,min)`。seed=10 的窄区间 fixture 会使 Rust 乘加舍入到 `high=0x3f800002`，Rust 与 C++ 都逐位返回该值；该用例证明上界舍入这一条路径已对齐，不证明任意区间所有输出逐位相同。【指定边界已修复并验证】
- 判定：【指定上界舍入路径已修复；其他区间仍需覆盖】
- 证据：Rust `uniform_sampler.rs:86-92` 与 rand 0.10.3 `uniform_float.rs:159-199`；C++ `UniformRectSampler.cpp::SampleAxis`；`UniformSamplerUpperBoundaryRust.tsv`、`uniform_sampler_boundary_reference.rs`、`TestSampling.cpp::TestUniformSamplerUpperBoundaryRustReference`。旧 C++ 截断逻辑下该 C++ 测试曾以 Rust `0x3f800002`、C++ `0x3f800001` 失败。

### `intersect_range`（uniform_sampler.rs:94-101）
- C++ 对应：内联于 Create（cpp:126-132 `std::max/std::min` + `<=` 检查）。Rust 的"任一区间无效（含 start > end）→ None"在 C++ 中由前置 bbox 校验 + `xMin <= xMax` 收尾检查合并覆盖：可行域构造 `container - rotated` 保证有限（两侧都有限时），故 Rust :95 的有限性检查对应 C++ cpp:130-132 的 isfinite 检查。数学语义一致。【一致】
- 证据：uniform_sampler.rs:94-101 vs cpp:126-140。

### `convert_sample_to_closest_feasible`（uniform_sampler.rs:104-120）
- C++ 对应：Rust 函数由 `explore.rs:413-414,467,500` 四处调用；主要对应逻辑是 `Exploration.cpp:153-192 LegalizeTransform`。Rust 先计算 `normalize_rotation(dt.rotation()-r).abs()`，而 `normalize_rotation` 输出 `[0,2π)`；C++ 现已使用 `abs(NormalizeRotation(transform.rotationRad - allowed))`。旧实现曾用最短圆周距离，导致旧角 359°、允许角 `{1°,350°}` 时 C++ 选 1°、Rust 选 350°；2026-10-06 已修复，Rust helper 对 350° 的 f32 位模式做精确断言，C++ 实际 Explore 路径按 1e-6 rad 容差核对相同选择。项目其他评估器有各自的合法化实现，本结论只适用于探索路径。
- 判定：【已修复；定向 parity 回归通过】。空 Discrete 列表时 Rust `.unwrap()` 会 panic，C++ Exploration 的合法化路径返回默认 0°；Importer 通常会拒绝此配置，因此这是无效/内部不变量路径，不应与正常输入差异混为一谈，仍待边界用例确认。
- 证据：uniform_sampler.rs:104-120、explore.rs:413-414,467,500；Exploration.cpp:153-192,719-722,780-782,822-824。

### `mod.rs`（sample/mod.rs:1-6）
- C++ 对应：头文件包含关系（BestSamples.h/CoordinateDescent.h 等各自独立 include）。mod.rs 仅模块声明：`best_samples`、`coord_descent`（oracle-trace 时 pub use CDConfig/refine_coord_desc）、`pub mod search`、`pub mod uniform_sampler`。无逻辑。
- 判定：【一致】（模块组织差异：Rust 私有 mod + pub use vs C++ 全公开头文件）
- 证据：mod.rs:1-6。

### `RotationDistribution`（C++ 拆出组件）
- Rust 对应：uniform_sampler.rs:31-41 的内联旋转集构造（None/Discrete/16 均分 Continuous）+ `rot_entries.choose(rng)` 的选择。C++ 把"旋转集 + 均匀选择"抽成 `RotationDistribution`（FromItem: RotationDistribution.cpp:28-73 完全相同的集合构造；Sample: :75-96 用 UniformIndex 选一个）。当前 SparrowCore 内 RotationDistribution 仅被 LocalSearchSampler::Create 用于校验（LocalSearchSampler.cpp:102），UniformRectSampler 自持旋转集——即它是 C++ 对 Rust 内联逻辑的**组件化副本**，无独立 Rust 对应物。
- 判定：【C++ 额外】（组件化拆分；集合构造公式与 Rust 内联版一致，但 Continuous 的乘除顺序同差异 #1）
- 证据：RotationDistribution.cpp:38-47 vs uniform_sampler.rs:34-41。

### `SparrowRng` ↔ Rust `Xoshiro256PlusPlus` + rand 0.10 语义
- Rust Sparrow 主搜索使用 `Xoshiro256PlusPlus`（lbf.rs:9,35；worker.rs:20，`&mut impl Rng` 经 search 传入），消费 `choose`/整数 `random_range`、浮点 `random_range` 等 API；Explore 的正态调用见 S06。C++ `SparrowRng`：
  1. **状态**：Xoshiro256++ 四字状态，seed 经 SplitMix64 展开（cpp:32-39）——与 rand::rngs::Xoshiro256PlusPlus::seed_from_u64 等价路径一致（rand 0.10 的 SeedableRng::seed_from_u64 用 SplitMix64 填充）。NextU64 的 result = rotl(s0+s3,23)+s0（cpp:43）即 Xoshiro256++ 标准输出函数。【一致】
  2. **NextU32** = `NextU64() >> 32`（cpp:56-59）——rand 0.10 对 RngCore 的 u32 默认实现即 `next_u64 >> 32`。【一致】
  3. **UniformIndex**（cpp:61-81）：`(u32 * bound) >> 32` 高位法 + low > (0 - bound) 时拒绝重采样 + 重采样加法进位——这是 rand 0.10 `gen_range(0..n)`（u32）的 Canon/lemire 带 widening-multiply 的精确移植，被 SparrowUniformIndexRustReference.tsv 146 行逐值锁定（TestRng.cpp:169-172）。【一致】
  4. **Uniform01F32**（cpp:83-87）：`u32 >> 9 / 2^23`——rand 0.10 `StandardUniform<f32>` 的尾数填充法（24 位中的高 23 位；rand 的实现 `value >> (32 - 23)` 即 `>> 9`，指数位固定 127）。【一致】
  5. **Normal01F32**（cpp:89-110）：C++ 的独立额外 helper 使用三角 Box–Muller（`sqrt(-2 ln u1)`，`2πu2`，cos 返回/sin spare 缓存），用于额外的 `LocalSearchSampler`（:157,164-165）。Rust Sparrow 的采样/搜索没有该 helper 的对应调用；探索池使用 `rand_distr::Normal`，对应 C++ `Exploration.cpp` 的 `RustStandardNormalF64` 路径（见 S06）。TestRng/TestSampling 的 Normal01F32 检查仍只是同种子 C++ 实例间的确定性回归；探索采样另由 S06 固定 Rust fixture 做跨语言对照。【Normal01F32 为 C++ 额外 API；探索采样有 Rust 对照】
- 判定：Xoshiro256++、`NextU32`、`UniformIndex` 和 `Uniform01F32` 与 Rust 对应用途在源码层面对应；`Normal01F32` 是无 Rust 对应的 C++ 额外 helper，不能将整个 `SparrowRng` API 判为完全一致。
- 证据：SparrowRng.cpp:41-87；`SparrowUniformIndexRustReference.tsv` 逐值验证整数映射（TestRng.cpp:169-172）；`Normal01F32` 的测试只比较同种子 C++ 实例（TestRng.cpp:312、TestSampling.cpp:449），不构成该 helper 的 Rust parity 证据。Rust 探索 `Normal<f32>` 与 C++ 生产采样 helper 的固定序列对照见 S06 的 `ExplorationNormalPoolSamplesRust.tsv` 和 `TestNormalPoolSamplerMatchesRustReference`。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | Continuous 旋转集计算顺序：Rust `i * ((2π)/16)`，C++ `(2π*i)/16`；两种 C++ 入口的 16 个角输出 | 当前构建下逐位一致；Rust/C++ reference 覆盖 | Rust UniformBBoxSampler 与 C++ UniformRectSampler、RotationDistribution 对 16 个角值 raw bits 全部相同；不证明其他编译器浮点选项或完整布局轨迹 | uniform_sampler.rs:31-41; UniformRectSampler.cpp:75-100; RotationDistribution.cpp:36-44; `UniformSamplerContinuousGridRust.tsv`; `TestSampling.cpp::TestUniformSamplerContinuousGridRustReference` |
| 2 | f32 区间采样上界处理：Rust `sample_single` 不额外截断；C++ 已移除 `nextafter(max,min)` 截断 | 已修复并由指定 Rust reference 逐位验证 | seed=10 的窄区间乘加舍入到上界时两侧均为 `0x3f800002`；其他区间和随机输出未由此上界 fixture 证明 | rand 0.10.3 `uniform_float.rs:159-199`; `UniformSamplerUpperBoundaryRust.tsv`; `uniform_sampler_boundary_reference.rs`; `TestSampling.cpp::TestUniformSamplerUpperBoundaryRustReference` |
| 3 | 无可行旋转：Rust `Option::None` vs C++ Result 错误；LBF 编排只把 C++ 错误转成 nullopt | 差异（正常 no-fit 路径对应） | 无可行旋转时两侧都触发扩宽语义；但 C++ invalid bbox/rotation/转换错误也会被同一路径吞并为“放不下” | uniform_sampler.rs:65-68 vs UniformRectSampler.cpp:48-144; LbfSearch.cpp:152-159 |
| 4 | `RotationDistribution` 独立组件（旋转集 + 均匀选择抽成类） | C++ 额外 | Rust 为 uniform_sampler 内联逻辑；组件化无行为影响 | RotationDistribution.cpp:28-96 vs uniform_sampler.rs:31-41,73 |
| 5 | bbox/旋转有限性与非退化校验 | 直接 sampler 边界差异 | C++ 拒绝 singleton/invalid bbox；Rust 公共 Rect 可直接提供退化 bbox，sampler 的 range 交集接受 singleton | UniformRectSampler.cpp:48-71 vs uniform_sampler.rs:94-101; jagua-rs rect.rs:14-33 |
| 6 | `convert_sample_to_closest_feasible` 被 Rust 探索路径调用；C++ Exploration 已改用 Rust 的规范化差值绝对值公式 | 已修复（Rust helper 精确断言 + C++ Explore 定向回归通过） | 359° 输入、允许角 `{1°,350°}` 两侧都选 350°；空离散列表 panic/default 仍属无效输入边界 | uniform_sampler.rs:104-120; explore.rs:413-414,467,500; `Tests/RustReference/tests/rotation_reference.rs`; Exploration.cpp:153-192; Tests/TestExploration.cpp |
| 7 | `Normal01F32` 是 C++ 额外的三角 Box–Muller helper；Rust 探索正态由 `RustStandardNormalF64` 实现 rand_distr ZIGNOR 路径 | `Normal01F32` 是 C++ API 额外；探索采样固定 trace 已验证 | LocalSearchSampler 无 Rust 对应；S06 以 5 个 seed × 4 个标准差、每组 128 个样本，共 2,560 项，精确核对采样位模式、u64 消耗及后续 4 个 u64；其他 seed/参数和完整 Explore 随机流仍未证明 | SparrowRng.cpp:89-110; LocalSearchSampler.cpp:157,164-165; Exploration.cpp:425-465,1143-1167; `ExplorationNormalPoolSamplesRust.tsv`; S06 Rust/C++ reference tests |
| 8 | sampler 创建错误分类：Rust `None` 只表示没有可行旋转；C++ `Result` 同时编码 no-fit 与非法 bounds/rotation/数值转换失败，调用方统一当作无样本 | 【错误语义差异】 | 非法输入可能被误报成普通 no-fit 并触发扩宽或跳过阶段 | uniform_sampler.rs:65-69 vs UniformRectSampler.cpp:48-144; LbfSearch.cpp:152-159; SeparatorWorker.cpp:188-221 |

## 结论

均匀采样主路径的常规旋转构造、可行域求交和随机消耗顺序在源码层面对应，并有有限 raw-bit Rust reference；但直接输入边界仍不同：Rust 接受 sampler 的单点 bbox，C++ 创建时拒绝；C++ 的 invalid-input 错误还会被搜索编排与“无可行旋转”合并处理。整数 `UniformIndex` fixture 有 144 条数据记录，RNG seed-42 fixture 另核对 4 个原始 u64 与 4 个 f32 均匀值。区间舍入到上界的指定 seed=10 路径已修复，单点 X 的 RNG 消费由连续 8 样本 trace 核对；这些不证明任意浮点区间或完整搜索逐位一致。探索 `RustStandardNormalF64` 已有 5 个 seed、4 个标准差、每组 128 个样本的逐样本和后续 4 个 u64 输出对照；其余 seed/参数及完整搜索随机流仍需扩展。`Normal01F32` 是 LocalSearchSampler 的额外 helper，仍没有 Rust 对应。

### 补充审计（2026-09-29）
1. **【核对确认：121+6 行逐项覆盖】** uniform_sampler.rs 全量复核：ROT_N_SAMPLES、UniformBBoxSampler（rot_entries + new/sample）、RotEntry、sample_axis、intersect_range、convert_sample_to_closest_feasible；sample/mod.rs 6 行——主文 6 小节 + mod.rs 条目全覆盖无遗漏。
2. **【2026-10-06 更正：实际调用路径与角距】** 先前审计误称 `convert_sample_to_closest_feasible` 无调用方。当前 Rust 源码四处调用位于 `explore.rs:413-414,467,500`；修复前 C++ Exploration 选择最短圆周距离，可能改变选角，详见本节与 S06。项目其他 evaluator 各有实现，不能将其统称为该 helper 的等价版本。
3. **【浅覆盖补全：RotEntry 的 Range 语义与 intersect 的半开区间】** Rust `Range<f32>`（start..end 半开）+ `intersect_range` 返回 `(start <= end).then_some`——**start==end 单点保留**（主文条目 2 已记）；补记：`sample_axis` 用浮点 `==` 判单点（非近似比较），若 start/end 相差 1 ULP 则走 `random_range` 并消耗随机数。C++ `minValue == maxValue` 同样是浮点数值精确相等（cpp:21-23），不是按位比较。该细节影响随机流消耗序。
4. **【核对确认：Continuous 乘除顺序】** 主文差异 #1 的两个公式顺序不同。Rust reference fixture 覆盖全部 16 个角值，MSVC Debug 下 C++ UniformRectSampler 与 RotationDistribution 都逐位匹配；此前根据乘除顺序推测的 i=5 1 ULP 风险未在真实 f32 sampler 输出中复现，现不作为已观察差异。该结论仅适用于锁定 Rust 依赖和本次 C++ 测试构建。
5. **【2026-10-06 执行记录：探索离散角 parity 修复】** RustReference 测试直接调用 Rust helper，以 f32 原始位模式断言 359° 样本在 `{1°,350°}` 中选 350°；C++ `TestExploration.cpp` 通过实际 `Explore` 路径断言同一选择，容差为 1e-6 rad。旧最短圆周距离公式使 C++ 用例失败，替换为 `abs(NormalizeRotation(sample-allowed))` 后通过；Rust 锁定测试及 SparrowCore Debug 全量测试通过。该结果不覆盖 Continuous 旋转构造、区间边界或完整排料端到端差值。
6. **【历史执行记录：2026-10-06 区间上界舍入 parity 修复】** `uniform_sampler_boundary_reference.rs` 直接调用当时锁定的 Rust Sparrow `UniformBBoxSampler`，seed=10、单旋转项、X 区间 `[0x3f800000,0x3f800002]` 得到上界 `0x3f800002`；`TestSampling.cpp::TestUniformSamplerUpperBoundaryRustReference` 对同一 TSV 逐位比较。移除 C++ 的 `nextafter(max,min)` 截断前，测试稳定复现 Rust/C++ 分别为 `0x3f800002`/`0x3f800001`；移除后 Rust 锁定测试及 SparrowCore Debug 全量测试均通过（当日 cargo test --locked 为 12 个测试函数，C++ 测试程序 exit code 0）。该夹具只证明指定上界舍入路径，不覆盖全部浮点区间或 Continuous 旋转；当前测试数见 README。
7. **【2026-10-06 执行记录：singleton X RNG 序列 reference】** RustReference 用相同 `RustSppSingleItem.json`、seed=42、容器宽度等于 item 宽度构造唯一可行 X 坐标，记录连续 8 次样本的 X/Y f32 raw bits；C++ 测试导入同一 JSON 并逐位比较全部坐标。若 singleton X 错误消费随机数，后续 Y 与下一样本会错位。Rust 定向测试通过，C++ Debug 全量测试 exit code 0。范围仅含 singleton X、一个 seed 和八次采样，不覆盖 singleton Y 或双轴同时退化。
