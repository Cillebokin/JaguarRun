# S14. coord_descent（坐标下降精化）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/sample/coord_descent.rs`（180 行）
**C++**: `SparrowCore/Include/SparrowCore/Sampling/CoordinateDescent.h`（32 行）+ `SparrowCore/Source/Sampling/CoordinateDescent.cpp`（239 行）
**判定**: 候选方向、状态更新和随机轴映射结构对应；CD 角度常量已改为 Rust 等价的 factor-first `f32` 换算，并以位模式和实际 LBF/worker 消费路径回归核验

C++ 为自由函数 `RefineCoordinateDescent` + 匿名命名空间内的 `Axis/State` 辅助类型，与 Rust 的 `refine_coord_desc` + `CoordinateDescent`/`CDAxis` 结构一一对应。主 fixture `SamplingCoordinateDescentRustReference.tsv` 含 8 条 sample、54 条 coordinate 和 1 条 result；测试对候选与结果按数值容差比较（变换 1e-6、loss 1e-5），不做逐位相等断言。`SampleEvalCoordinateDescentRustReference.tsv` 覆盖候选比初始 loss 好 4 ULP、但 FPA comparator 判等的近似 tie；新增 `SampleEvalCoordinateDescentExactTieRustReference.tsv` 覆盖候选和初始 loss 位模式完全相同的精确 tie。两条 Rust/C++ trace 均逐行核对候选顺序、最终变换/loss bits 和评估数；结论限于所列 fixture。

## 函数逐条比对

### `CDConfig`（coord_descent.rs:9-21）
- C++ 对应：`CoordinateDescentConfig`（CoordinateDescent.h:16-24）。字段一一对应：t_step_init→translationStepInit、t_step_limit→translationStepLimit、r_step_init→rotationStepInit、r_step_limit→rotationStepLimit、wiggle→rotationWiggle。
- 判定：【一致】
- 证据：coord_descent.rs:10-20 五字段 vs CoordinateDescent.h:18-22 同五字段；C++ 额外第 23 行 `maxEvaluations`（见差异 #3）。

### `refine_coord_desc`（coord_descent.rs:24-62）
- C++ 对应：`RefineCoordinateDescent`（CoordinateDescent.cpp:159-238）。逐段核对：
  1. 状态初始化（Rust :34-43）：pos/eval 取 initial；`axis: CDAxis::random(rng, wiggle)`（消耗 1 次随机数）；t_steps 双向初始化为 t_step_init；r_step 取 r_step_init。C++ State 初始化（cpp:180-186）：position/evaluation 取 initial、`axis = ChooseAxis(rng, config.rotationWiggle)`、translationXStep/translationYStep 均 = translationStepInit、rotationStep = rotationStepInit。【一致】
  2. ask→evaluate→选 best 循环（Rust :46-58）：`while let Some(c) = cd.ask()`；两候选各评估一次；`c.into_iter().zip(c_eval).min_by_key(|(_, eval)| *eval)` 取最小。Rust `Iterator::min_by_key` 在多个元素并列最小时返回**第一个**（[标准库文档](https://doc.rust-lang.org/std/iter/trait.Iterator.html#method.min_by_key)）。C++（cpp:189-234）逐一评估（:205-216），`bestIndex = IsBetterSample(evaluated[1].second, evaluated[0].second) ? 1 : 0`（:218-219），近似相等时比较器不认为候选 1 更优，因此同样选 index 0。Rust/C++ 专项近似并列 fixture 已验证这一 FPA 相等情形；两侧 tie-break 一致。【指定近似并列输入一致】
  3. 返回 `(cd.pos, cd.eval)`（Rust :61）↔ C++ `FromValue({state.position, state.evaluation})`（cpp:236-237）。【一致】
- 判定：【一致】（并列选择方向一致；其余逐行等价）
- 证据：上述行号；主 fixture `SamplingCoordinateDescentRustReference.tsv` 的 54 条 coordinate 候选在 TestSampling.cpp:868-893 按容差比对通过；两个 `SampleEvalCoordinateDescent*RustReference.tsv` fixture 分别由 Rust 近似 tie/精确 tie 测试与 C++ `TestCoordinateDescentTieMatchesRustReference` 比较候选序列、结果位模式与评估数。它们覆盖列明的两种相等边界，不证明任意浮点输入逐位相等。

### `CoordinateDescent::ask`（coord_descent.rs:87-112）
- C++ 对应：`HasCandidates`（cpp:75-82）+ `BuildCandidates`（cpp:84-130）。三处细节：
  1. 停止条件（Rust :91）：`sx < limit && sy < limit && (sr < r_limit || !wiggle)` 时返回 None（注意：wiggle 关闭时旋转步长不参与停止判定）。C++ HasCandidates（cpp:78-81）：`!(txStep < limit && tyStep < limit && (!wiggle || rStep < rLimit))`——逻辑取反后逐字相同。【一致】
  2. 五轴候选公式（Rust :98-104）：Horizontal `[(tx+sx,ty,r),(tx-sx,ty,r)]`；Vertical `[+sy,-sy]`；ForwardDiag `[(+sx,+sy),(-sx,-sy)]`；BackwardDiag `[(-sx,+sy),(+sx,-sy)]`；Wiggle `[(tx,ty,r+sr),(tx,ty,r-sr)]`。C++ BuildCandidates（cpp:96-122）逐轴逐符号相同（AXIS_BACKWARD_DIAGONAL: firstX-=step/firstY+=step, secondX+=step/secondY-=step，cpp:112-116，对应 Rust BackwardDiag `[(-sx,+sy),(+sx,-sy)]`）。【一致】
  3. 数值精度：Rust 直接在 DTransformation 的 f32 字段上做 `tx + sx`；C++ 将 double 位置转 float（cpp:86-88）、在 float 上加减步长（cpp:99-120）、再转回 double（cpp:124-129）——与 Rust f32 算术一致。【一致】
- 判定：【一致】
- 证据：fixture 候选行验证（TestSampling.cpp:875-893，包含负角度如 `bcccccd0` 即 -0.025f；测试按容差验证 wiggle 双侧候选公式）。

### `CoordinateDescent::tell`（coord_descent.rs:115-147）
- C++ 对应：cpp:218-233 + `UpdateSteps`（cpp:132-154）。逐点：
  1. better/worse 判定（Rust :117-119 `eval.cmp(&self.eval)`：Less=better、Greater=worse）；C++（cpp:221-222）`better = IsBetterSample(best.second, state.evaluation)`、`worse = IsBetterSample(state.evaluation, best.second)`。SampleEval 的比较语义（含 FPA 容差）见 S10——C++ IsBetterSample 用 `RustApproxEqualF32`（4 ulp + f32::EPSILON 绝对容差）复刻 float-cmp 0.10 默认 margin（F32Margin{epsilon: f32::EPSILON, ulps: 4}，eq.rs:69-76），并列时 better=worse=false，与 Rust `Ordering::Equal` 时两个布尔均为 false 一致。【一致】
  2. `if !worse { (self.pos, self.eval) = (pos, eval) }`（Rust :121-124）↔ C++ `if (!worse) { state.position = best.first; state.evaluation = best.second; }`（cpp:223-227）。相等时也接受更新（与 Rust 相同）。【一致】
  3. 步长乘子（Rust :127-141）：better→CD_STEP_SUCCESS=1.1（consts.rs:11）、否则 CD_STEP_FAIL=0.5（consts.rs:14）；Horizontal 只乘 x 步、Vertical 只乘 y 步、对角两步各乘 m.sqrt()、Wiggle 乘 r_step。C++ UpdateSteps（cpp:132-154）+ cpp:229 `UpdateSteps(state, better ? 1.1F : 0.5F)`：逐轴相同，对角 `std::sqrt(multiplier)` 对应 `m.sqrt()`。【一致】
  4. `if !better { self.axis = CDAxis::random(rng, self.wiggle) }`（Rust :144-146）↔ C++ cpp:230-233 `if (!better) { state.axis = ChooseAxis(rng, config.rotationWiggle); }`。【一致】
- 判定：【一致】
- 证据：fixture 的 54 条候选序列（含步长收敛轨迹）与 result 行（最终 pos/loss）由 TestSampling.cpp:896-905 按容差比较。

### `CDAxis::random`（coord_descent.rs:164-179）
- C++ 对应：`ChooseAxis`（cpp:30-49）。Rust `rng.random_range(0..6)`（wiggle）或 `0..4`（无 wiggle），映射 0=Horizontal、1=Vertical、2=ForwardDiag、3=BackwardDiag、4..6=Wiggle；C++ `rng.UniformIndex(6U 或 4U)`，switch 映射完全相同（cpp:34-47）。
- 判定：【一致】
- 证据：coord_descent.rs:171-178 vs cpp:32-47。随机数语义对应：Rust rand 0.10 `random_range(0..6)`（u32 range）↔ C++ `SparrowRng::UniformIndex`（SparrowRng.cpp:61-81，Lemire u32×bound>>32 + 拒绝重采样），被 `SparrowUniformIndexRustReference.tsv`（146 行总计、144 条数据记录，TestRng.cpp:169 消费）逐值锁定。整个 coord_descent fixture 的轴序列（54 候选 = 27 次 ask，每次消耗 1 个 u32 换轴随机数于 !better 时）与之吻合。

### 【C++ 额外】配置校验与评估预算（cpp:165-177, 189-216）
- C++ `IsValidCoordinateConfig`（cpp:51-63）：有限性、非负、init≥limit 校验（Rust 无校验，靠调用方保证）；`maxEvaluations` 预算检查（cpp:175-177, 198-201, 207-212）：每轮循环与每次评估前检查 `usedEvaluations >= maxEvaluations` 则返回 TimeLimit 错误；Rust 只有 `debug_assert!`（coord_descent.rs:57，n_evals 增量 < 1000，仅 debug 构建生效）。默认 maxEvaluations=SIZE_MAX（h:23），即默认行为与 Rust 相同。
- 判定：【C++ 额外】
- 影响：正常路径无影响；预算触发时 C++ 提前返回错误（调用方 SearchLbf/SeparatorWorker 有回退，见 S13 差异 #5），Rust debug 构建会 panic、release 构建会继续跑。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `CDConfig` 额外字段 `maxEvaluations`（默认 SIZE_MAX）+ 配置/初始变换有限性校验 | C++ 额外 | 正常路径无影响；预算触发时返回 TimeLimit 错误（Rust 无此通道） | CoordinateDescent.h:23, cpp:51-63,165-177,198-212 vs coord_descent.rs 全文 |
| 2 | 两候选 loss 按 FPA 比较并列时的选择：Rust `min_by_key` 与 C++ 严格优胜比较均保留第一个候选 | FPA 近似并列与 raw bits 完全相同的精确并列均有固定 trace | 固定 fixture 覆盖两类输入；不证明任意浮点输入逐位相同 | coord_descent.rs:50-52; 两个 `SampleEvalCoordinateDescent*RustReference.tsv`; Rust 近似/精确 tie tests; C++ `TestCoordinateDescentTieMatchesRustReference` |
| 3 | Rust `debug_assert!`（1000 eval 上限）→ C++ Result 错误通道 | 差异 | 既定形态差异（panic→Result） | coord_descent.rs:57 vs cpp:198-201 |
| 4 | Rust `trace!` 日志 → C++ 无 | 差异 | 无行为影响 | coord_descent.rs:56,59 |
| 5 | 角度步长常量：Rust f32 `to_radians()` vs C++ conversion | 已修复（factor-first `f32` 计算） | 旧 C++ `degrees * PI / 180` 对 5° 得 `0x3db2b8c3`，Rust 为 `0x3db2b8c2`；共享 `RustF32DegreesToRadians` 后，5°/1°/0.5°/0.05° 四项 raw bits 精确匹配。LBF search 与 Separator worker 的 Rust reference traces 继续验证实际候选/结果 | `consts.rs:20,26`; `CoordinateDescent.h::RustF32DegreesToRadians`; `LbfSearch.cpp`; `SeparatorWorker.cpp`; `TestSampling.cpp`; `rotation_reference.rs` |

## 结论

坐标下降的状态机和五轴候选公式可对应。旧角度常量表达式在 5° 下曾稳定复现 1 ULP 差异；当前 LBF 与 Separator worker 共用 factor-first `f32` 转换，5°/1°/0.5°/0.05° 分别与 Rust `to_radians()` raw bits 一致。LBF/worker reference traces、FPA 近似并列与 raw-bit 精确并列 fixture 均在 Debug/Release 通过。C++ `maxEvaluations` 是 Rust 没有的扩展：C++ 预算 0/1/2 的错误时点、评估数和 RNG 副作用已有断言；Rust Debug 在累计 1000 次评估后触发 `debug_assert!`，Release 继续运行，C++ 默认配置也已验证可超过 1000 次。该预算扩展按各自运行契约记录，不宣称参数级 parity。搜索调用点原样传递零坐标下降数，因此 BestSamples 的零容量 Debug/Release 行为按 Rust 基线复现（见 S13/S15）。

### 实现与回归验证（2026-10-09）
1. RustReference `coordinate_descent_degree_constants_match_rust_f32_to_radians` 锁定 `[5°, 1°, 0.5°, 0.05°]` 的位模式 `[0x3db2b8c2, 0x3c8efa35, 0x3c0efa35, 0x3a64c388]`。
2. C++ RED 在旧表达式下首项为 `0x3db2b8c3`，证明 1 ULP 差异；改用 `RustF32DegreesToRadians(degrees)` 后四项与 Rust 相同。LBF Search 和 SeparatorWorker 两个生产配置入口都调用该 helper。
3. 角度换算修复时 RustReference Debug/Release 的 87 个 integration tests 与 SparrowCore 全量 runner 通过（当时包含 LBF search trace 与 Separator worker 对照）；随后新增 S14/S16 references 后各为 94 个；2026-10-09 加入 S03 扩宽、零采样和 S04 空 round 三个 Rust 测试后当前各为 97 个测试函数，见 README。证据限于当前 Windows x64 MSVC/Rust 1.98.1 scalar profile。

### 收口复核（2026-10-17）
1. `SampleEvalCoordinateDescentExactTieRustReference.tsv` 固定 seed=42、初始 loss 与 evaluator 返回 loss 均为 `0x3f800000`；Rust `min_by_key` 与 C++ 严格优胜比较都选取候选对中的第一个。两侧逐行比较 6 次候选评估、最终 transform/loss bits 和评估数。
2. C++ 预算边界用例核对 `maxEvaluations=0` 在评估和 RNG 消耗前返回 `TimeLimit`；预算为 1 时完成首个候选评估后、第二候选前返回 `TimeLimit`，仅消耗轴初始化随机数；预算为 2 的完整候选对超限路径由既有 `TestCoordinateDescentAxesAndBudget` 覆盖。默认预算测试确认可超过 1000 次评估。
3. Rust 共享长轨迹 evaluator：Debug 捕获 `debug_assert!` 并确认评估计数恰为 1000；Release 完成超过 1000 次评估。该断言是 Debug-only 源行为，C++ 的 `maxEvaluations` 是单独的调用限制。
4. 验证：`verify-rust-reference.ps1` 在 Rust Debug/Release 全套测试通过；SparrowCore Debug/Release 全量 runner 通过；`cargo fmt --check` 通过。范围限于锁定 Rust 1.98.1、Windows x64 MSVC、当前 scalar feature 与列明输入。

### 补充审计（2026-09-29）
1. **【核对确认：180 行逐项覆盖】** coord_descent.rs 全量复核：CDConfig（5 字段）、refine_coord_desc、CoordinateDescent 结构（8 字段）、ask、tell、CDAxis（5 变体+random）——主文 5 小节全覆盖无遗漏。
2. **【2026-10-06 更正：min_by_key 并列语义】** 先前审计把 Rust `Iterator::min_by_key` 的并列规则记反了。标准库规定多个最小键并列时返回**第一个**；C++ 的 `IsBetterSample(candidate1, candidate0) ? 1 : 0` 在并列时也选择 0。差异表已改为一致。截至 2026-10-06 尚无针对并列键的专门用例；2026-10-09 已补 FPA 近似并列 reference，结论更新见主文与本节第 5 条。
3. **【核对确认：tell 的 !worse 更新含 Equal】** Rust `if !worse` 和 C++ `if (!worse)` 在 Equal 时都接受所选候选（coord_descent.rs:121-124；cpp:223-227）。2026-10-17 新增的精确 tie fixture 与原 FPA 近似并列 fixture 分别覆盖同位模式和 comparator 判等路径。
4. **【核对确认：CDAxis::random 的 4..6 双值映射】** `0..6` 时 4、5 均映射 Wiggle（coord_descent.rs:171-178）——即 Wiggle 被抽中概率 2/6、其余各 1/6；C++ `UniformIndex(6U)` switch 同映射（cpp:34-47）。主文"switch 映射完全相同"判定成立，此概率分布细节补记供后续验证随机流。
5. **【2026-10-09 执行记录：FPA 近似并列】** Rust fixture 以初始 loss `0x3f800004` 和 4 ULP 更优的候选触发 comparator equality，记录候选序列及最终结果；C++ comparator fixture 复用同一 TSV，Rust Debug/Release 与 SparrowCore Debug/Release 均通过。该用例证明该近似并列输入下保留首候选及评估轨迹一致；不覆盖精确同 loss 位模式的独立组合或任意 evaluator。
