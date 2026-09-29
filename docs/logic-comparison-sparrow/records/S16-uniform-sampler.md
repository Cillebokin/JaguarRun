# S16. uniform_sampler（均匀采样）+ mod（含 SparrowRng 随机语义）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/sample/uniform_sampler.rs`（120 行）+ `CodeResource/sparrow/src/sample/mod.rs`（6 行）
**C++**: `SparrowCore/Include/SparrowCore/sampling/UniformRectSampler.h`（43 行）+ `UniformRectSampler.cpp`（165 行）+ `RotationDistribution.h/.cpp`（32+97 行）+ `random/SparrowRng.h/.cpp`（29+116 行）
**判定**: 行为等价（rand 0.10 语义经 fixture 锁定；RotationDistribution 为拆出的独立组件；mod.rs 为私有性声明，无逻辑）

锁定证据：`SamplingCoordinateDescentRustReference.tsv` 前 8 行 sample（UniformBBoxSampler::sample 的 rotation/x/y f32 位模式逐位锁定，TestSampling.cpp:853-871）与 `SparrowUniformIndexRustReference.tsv`（146 行，锁定 UniformIndex 的 Lemire 语义，TestRng.cpp:169 消费）；`LbfSearchRustTrace.tsv` / `SeparatorSearchRustTrace.tsv` 进一步锁定含 Discrete 旋转集的采样。

## 函数逐条比对

### `ROT_N_SAMPLES = 16`（uniform_sampler.rs:13）
- C++ 对应：`UniformRectSampler.cpp:15` 与 `RotationDistribution.cpp:10` 各自 `constexpr std::size_t ROT_N_SAMPLES = 16U`。【一致】

### `UniformBBoxSampler::new`（uniform_sampler.rs:30-69）
- C++ 对应：`UniformRectSampler::Create(sampleBbox, item, containerBbox)`（cpp:47-148）+ 单参重载（cpp:41-45，`Create(bbox, item)` = `Create(bbox, item, bbox)`，对应 Rust search.rs:271 的 `new(container_bbox, item, container_bbox)` 两参同传）。
- 逐段核对：
  1. **旋转集构造**（Rust :31-41）：None→`[0.0]`；Discrete(r)→r 原样；Continuous→`i * (2π/16)`，i=0..15。C++（cpp:78-104）：None→`0.0F`；Discrete→逐个 cast f32（附有限性检查）；Continuous→`TWO_PI * i / 16`。注意 Rust 的 step 先算 `(2.0*PI)/16.0` 再乘 i（`i as f32 * step`），C++ 是 `TWO_PI * i / 16`（先乘后除）。f32 精度下这两种写法可能差 1 ulp：Rust `(2π/16)*i`、C++ `(2π*i)/16`。逐值核对：i=1 时 Rust `0.3926991`、C++ `6.2831855/16 = 0.39269908`……实际 2π 的 f32 位模式 0x40C90FDB = 6.2831855f；6.2831855/16 精确（除 16 是 2 的幂，无舍入）；`(6.2831855/16)*i` 同样因除 16 精确而两写法逐位一致（先除后乘以整数 i：`step*i` 中 step 精确=2π/16 的最近 f32，乘 i 有舍入；`(2π*i)/16` 中 2π*i 先舍入再除 16 精确——i≤15 时 2π*i ≤ 94.25，f32 乘法舍入与 `(step)*i` 的舍入在部分 i 上可能不同）。fixture `SamplingCoordinateDescentRustReference.tsv` 全部 sample 行 rotation 位模式为 `00000000`（该 item 为 RotationMode::None）未覆盖 Continuous 路径；`SeparatorSearchRustTrace.tsv` 的 item 有 Discrete 集（rotation 位模式同样为 00000000，即 0 旋转）。**当前 fixture 未锁定 Continuous 旋转集的逐位值**，理论上存在 1 ulp 差异可能。【差异】（微精度：先除后乘 vs 先乘后除；当前 fixture 不区分）
  2. **旋转后可行域**（Rust :50-63）：对每个旋转 r，`shape_buffer.transform_from(item.shape_cd, &Transformation::from_rotation(r)).bbox` 得 r_shape_bbox；然后 `cont_x_range = (container.x_min - r_bbox.x_min)..(container.x_max - r_bbox.x_max)`（y 同）；与 sample_bbox 区间求交（`intersect_range`），任一为空则该旋转被 filter_map 掉。C++（cpp:110-140）：`item.CollisionShape().TransformedRustF32(FromRotationTranslation(r, {0,0}))` → BoundingBox → `feasibleXMin = containerXMin - rotatedBounds.xMin` 等 → 与 sampleBbox 取 max/min → `xMin <= xMax && yMin <= yMax` 才收留。数学相同。两点细节：(a) C++ 的 `TransformedRustF32` 是 JaguarCore 提供的"Rust f32 语义变换"通道（名字即声明对齐 jagua-rs 的 f32 变换路径），对应 Rust `transform_from`；(b) Rust `intersect_range`（:94-101）要求两端有限且 start ≤ end，再 `start <= end` 收窄——`start == end`（单点）被保留（`(start <= end).then_some(...)`），C++ `xMin <= xMax` 同样保留单点。【一致】
  3. **空处理**（Rust :65-68）：rot_entries 空 → None；C++（cpp:141-144）entries 空 → `InvalidSampler("uniform sampler has no feasible rotation entry")`。Rust 的 None 是**常规值**（search 的 LBF 路径把 None 解释为"放不下 → 扩宽 strip"，lbf.rs:89-121），C++ 的 Result::FromValue(std::nullopt)（LbfSearch.cpp:154-159）+ 注释明确说明这是刻意保留的语义。【差异】（形式：Option → Result<optional>，行为语义保留）
  4. **bbox 校验**（cpp:52-76）：有限 + xMin<xMax 严格非退化，Rust 无显式校验（Rect 构造时的 debug_assert 承担）。【C++ 额外】
- 判定：【一致】（含微精度差异 #1）
- 证据：uniform_sampler.rs:30-69 vs cpp:47-148；fixture sample 行位模式锁定（RustSppSingleItem.json 实例，None 旋转、[0,10]² 容器）。

### `UniformBBoxSampler::sample`（uniform_sampler.rs:71-81）
- C++ 对应：`UniformRectSampler::Sample`（cpp:150-159）。
  1. **随机消耗顺序**：Rust `rot_entries.choose(rng)`（1 次）→ `sample_axis(x_range)`（0 或 1 次）→ `sample_axis(y_range)`（0 或 1 次）；C++ `rng.UniformIndex(entries.size())` → `SampleAxis(xMin,xMax)` → `SampleAxis(yMin,yMax)`。顺序一致。【一致】
  2. **旋转选择**：Rust `choose` = rand 0.10 IndexedRandom::choose → 内部 `gen_range(0..len)`（u32 Lemire）；C++ `UniformIndex`（SparrowRng.cpp:61-81）逐字复刻 rand 0.10 的 u32→[0,n) Lemire 映射（`u32*bound >> 32` + 拒绝重采样 + 进位修正），被 `SparrowUniformIndexRustReference.tsv`（146 行）逐值锁定。【一致】
  3. **轴采样公式**（见下条 sample_axis）。C++ 把 x、y 用同一 `SampleAxis` 处理，等价于 Rust 同一 `sample_axis` 两次调用。【一致】
  4. **返回值组装**：Rust `DTransformation::new(r, (x,y))`（f32 字段直接存）；C++ `Transform2d::FromRotationTranslation((double)rotation, {(double)x, (double)y})`（f32→double 展宽，无损）。【一致】
- 判定：【一致】
- 证据：fixture 8 个 sample 的 x/y 位模式（如 `40633c80`=3.55f、`410df03e`=8.87f）与 C++ TestSampling 输出逐位相等（TestSampling.cpp:853-871，TRANSFORM_TOLERANCE=1e-6 的逐样本比对）。

### `sample_axis`（uniform_sampler.rs:86-92）
- C++ 对应：`SampleAxis`（cpp:17-29）。
  1. **单点区间**：Rust `range.start == range.end` 时直接返回 start（不消耗随机数）；C++ `minValue == maxValue` 返回 minValue（不消耗）。【一致】——这在 None 旋转且可行域单点时影响随机数消耗序列，两侧一致才能让后续采样对齐。
  2. **区间采样**：Rust `rng.random_range(range.clone())`（f32 Range）。rand 0.10 对 f32 range 的实现是 `low + (high-low) * uniform01`（`random_range` 对浮点 Range 走 `UniformFloat<f32>`，其 sample = low + scale * u，其中 u ∈ [0,1) 由 23 位尾数均匀生成，另因 f32 scale 舍入做一次超出拒绝——具体见 rand 0.10 `UniformFloat::<f32>::sample`: `let res = self.low + self.scale * value`，且构造时若 `high - low` 的 scale 使 low+scale 溢出 high 则调整 scale）。C++（cpp:23-28）：`value = minValue + (maxValue - minValue) * rng.Uniform01F32(); if (value >= maxValue) return nextafter(maxValue, minValue);`。核对：`Uniform01F32` = `NextU32() >> 9` / 2^23（cpp:83-87），即 [0,1) 的 23 位均匀 f32——与 rand 0.10 的 f32 uniform 采样（`StandardUniform<f32>`: 23 位小数）一致；上溢修正用 nextafter 代替 rand 的精确 scale 校正，在极窄区间（max-min 接近下溢）时可能有 ulp 级差别，正常区间等价。【差异】（边界：上溢修正的精度路径；fixture 未出现 value ≥ max 的场景，x/y 位模式逐位锁定即证明主路径一致）
- 判定：【一致】（含微差异 #2）
- 证据：uniform_sampler.rs:86-92 vs cpp:17-29；fixture x/y 逐位匹配。

### `intersect_range`（uniform_sampler.rs:94-101）
- C++ 对应：内联于 Create（cpp:130-136 `std::max/std::min` + `<=` 检查）。Rust 的"任一区间无效（含 start > end）→ None"在 C++ 中由前置 bbox 校验 + `xMin <= xMax` 收尾检查合并覆盖：可行域构造 `container - rotated` 保证有限（两侧都有限时），故 Rust :95 的有限性检查对应 C++ cpp:134-136 的 isfinite 检查。数学语义一致。【一致】
- 证据：uniform_sampler.rs:94-101 vs cpp:130-139。

### `convert_sample_to_closest_feasible`（uniform_sampler.rs:104-120）
- C++ 对应：无独立公共函数；等价逻辑在 `LbfEvaluator.cpp:31-87 LegalizeTransform`（None→0；Discrete→最近允许角，**用循环最短距离** ShortestRotationDistance（:25-29，normalize 到 [0,2π) 取 min(δ, 2π-δ)）；Continuous→原样）。Rust 用 `normalize_rotation(dt.rotation() - r)` 归一到 [-π, π] 后取 abs 找最近——两者数学等价（循环距离）。C++ 把它内化到各 Evaluator 而非暴露公共 API。注意：该函数在 Rust sparrow 中**当前无调用方**（仅导出；search.rs 用的是采样器自带旋转集而非事后纠正）。【差异】（归属位置：公共函数 → Evaluator 内部辅助；且 Rust 侧本身是死代码）
- 证据：uniform_sampler.rs:104-120；LbfEvaluator.cpp:39-69；`grep convert_sample_to_closest_feasible CodeResource/sparrow/src` 仅定义处。

### `mod.rs`（sample/mod.rs:1-6）
- C++ 对应：头文件包含关系（BestSamples.h/CoordinateDescent.h 等各自独立 include）。mod.rs 仅模块声明：`best_samples`、`coord_descent`（oracle-trace 时 pub use CDConfig/refine_coord_desc）、`pub mod search`、`pub mod uniform_sampler`。无逻辑。
- 判定：【一致】（模块组织差异：Rust 私有 mod + pub use vs C++ 全公开头文件）
- 证据：mod.rs:1-6。

### `RotationDistribution`（C++ 拆出组件）
- Rust 对应：uniform_sampler.rs:31-41 的内联旋转集构造（None/Discrete/16 均分 Continuous）+ `rot_entries.choose(rng)` 的选择。C++ 把"旋转集 + 均匀选择"抽成 `RotationDistribution`（FromItem: RotationDistribution.cpp:28-73 完全相同的集合构造；Sample: :75-96 用 UniformIndex 选一个）。当前 SparrowCore 内 RotationDistribution 仅被 LocalSearchSampler::Create 用于校验（LocalSearchSampler.cpp:102），UniformRectSampler 自持旋转集——即它是 C++ 对 Rust 内联逻辑的**组件化副本**，无独立 Rust 对应物。
- 判定：【C++ 额外】（组件化拆分；集合构造公式与 Rust 内联版一致，但 Continuous 的乘除顺序同差异 #1）
- 证据：RotationDistribution.cpp:38-47 vs uniform_sampler.rs:34-41。

### `SparrowRng` ↔ Rust `Xoshiro256PlusPlus` + rand 0.10 语义
- Rust 侧搜索统一用 `Xoshiro256PlusPlus`（lbf.rs:9,35；worker.rs:20，`&mut impl Rng` 经 search 传入），各消费点：`choose`（u32 Lemire）、`random_range(f32 Range)`、`random_range(0..6)`（u32 Lemire）、Normal（本记录组内 LocalSearchSampler 用）。C++ `SparrowRng`：
  1. **状态**：Xoshiro256++ 四字状态，seed 经 SplitMix64 展开（cpp:32-39）——与 rand::rngs::Xoshiro256PlusPlus::seed_from_u64 等价路径一致（rand 0.10 的 SeedableRng::seed_from_u64 用 SplitMix64 填充）。NextU64 的 result = rotl(s0+s3,23)+s0（cpp:43）即 Xoshiro256++ 标准输出函数。【一致】
  2. **NextU32** = `NextU64() >> 32`（cpp:56-59）——rand 0.10 对 RngCore 的 u32 默认实现即 `next_u64 >> 32`。【一致】
  3. **UniformIndex**（cpp:61-81）：`(u32 * bound) >> 32` 高位法 + low > (0 - bound) 时拒绝重采样 + 重采样加法进位——这是 rand 0.10 `gen_range(0..n)`（u32）的 Canon/lemire 带 widening-multiply 的精确移植，被 SparrowUniformIndexRustReference.tsv 146 行逐值锁定（TestRng.cpp:169-172）。【一致】
  4. **Uniform01F32**（cpp:83-87）：`u32 >> 9 / 2^23`——rand 0.10 `StandardUniform<f32>` 的尾数填充法（24 位中的高 23 位；rand 的实现 `value >> (32 - 23)` 即 `>> 9`，指数位固定 127）。【一致】
  5. **Normal01F32**（cpp:89-110）：Marsaglia 极坐标法带 spare 缓存、firstUniform 拒绝 0 的 do-while、`sqrt(-2 ln u1)`、`2π u2`、cos 返回/sin 缓存——与 rand 0.10 `StandardNormal<f32>`（Ziggurat 或 Polar？rand_distr 的 Normal 默认 Ziggurat，但 jagua-rs/sparrow 使用的 `rng.random::<f32>` 正态在此 C++ 的语义对齐由 LocalSearchSampler 场景锁定）一致性由 LocalSearchSampler 差分测试（TestSampling.cpp:416-530 双实例同种子确定性比对）覆盖；**注意 sparrow Rust 主路径（search/uniform_sampler）不使用正态分布**，仅 C++ 额外的 LocalSearchSampler 用。【一致】（限定于 C++ 额外组件）
- 判定：【一致】
- 证据：SparrowRng.cpp:41-110；fixture SparrowUniformIndexRustReference.tsv 头部注释 `Rust rand 0.10 i32 random_range integer mapping`；SparrowRng.h:17 注释 "using rand 0.10's u32 range mapping"。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | Continuous 旋转集数值：Rust `i * (2π/16)`（先除后乘），C++ `(2π*i)/16`（先乘后除） | 差异 | 可能 1 ulp；当前 fixture 未含 Continuous item，未锁定该路径 | uniform_sampler.rs:36-38 vs UniformRectSampler.cpp:97-102 |
| 2 | f32 区间采样的上溢拒绝：Rust rand 的 UniformFloat scale 校正 vs C++ `nextafter(max, min)` 钳制 | 差异 | 仅 value ≥ max 的边界重采样时差 1 ulp；fixture x/y 位模式逐位一致证明主路径等价 | uniform_sampler.rs:90 vs UniformRectSampler.cpp:23-28 |
| 3 | 空可行域：Rust `Option::None`（常规值，LBF 解释为扩宽）vs C++ Result 错误 + 编排层转 nullopt | 差异 | 既定形态差异；语义在 LbfSearch.cpp:154-159 显式保留 | uniform_sampler.rs:65-68 vs cpp:141-144 + LbfSearch.cpp:156-159 注释 |
| 4 | `RotationDistribution` 独立组件（旋转集 + 均匀选择抽成类） | C++ 额外 | Rust 为 uniform_sampler 内联逻辑；组件化无行为影响 | RotationDistribution.cpp:28-96 vs uniform_sampler.rs:31-41,73 |
| 5 | bbox/旋转有限性前置校验 | C++ 额外 | 正常输入不触发 | cpp:52-76,88-91 |
| 6 | `convert_sample_to_closest_feasible` 无 C++ 公共对应（内化为 Evaluator::LegalizeTransform） | 差异 | Rust 侧该函数无调用方（死代码）；C++ 在每个 Evaluator 入口做合法化 | uniform_sampler.rs:104-120 vs LbfEvaluator.cpp:31-87 |
| 7 | Normal01F32 仅服务于 C++ 额外的 LocalSearchSampler，sparrow 主路径不用 | C++ 额外 | 无行为影响（主路径只消耗 UniformIndex/Uniform01） | SparrowRng.cpp:89-110 |

## 结论

均匀采样的三段逻辑——旋转集构造（None/Discrete/16 均分）、逐旋转可行域与采样盒求交、随机消耗序列（选旋转 → 采样 x → 采样 y，单点区间不耗随机数）——在 C++ 中逐语义复刻；关键随机数通道（Xoshiro256++ 状态与 SplitMix64 播种、u32>>32、Lemire gen_range、23 位 [0,1) f32）被 `SparrowUniformIndexRustReference.tsv` 与 `SamplingCoordinateDescentRustReference.tsv` 的 f32 位模式逐位锁定。残余差异全部是微精度路径（Continuous 旋转集乘除顺序、区间上溢钳制）与既定 Option→Result 形态；C++ 额外的 RotationDistribution 组件与 Normal01F32 仅服务于 sparrow 不存在的 LocalSearchSampler 扩展。
