# S14. coord_descent（坐标下降精化）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/sample/coord_descent.rs`（179 行）
**C++**: `SparrowCore/Include/SparrowCore/sampling/CoordinateDescent.h`（32 行）+ `SparrowCore/Source/Sampling/CoordinateDescent.cpp`（239 行）
**判定**: 行为等价

C++ 为自由函数 `RefineCoordinateDescent` + 匿名命名空间内的 `Axis/State` 辅助类型，与 Rust 的 `refine_coord_desc` + `CoordinateDescent`/`CDAxis` 结构一一对应。核心行为（ask 生成候选、tell 更新步长/换轴、best 候选选择）被 fixture `SamplingCoordinateDescentRustReference.tsv` 逐位锁定（63 条：8 sample + 54 coordinate + 1 result，TestSampling.cpp:792 `TestRustSamplingCoordinateDescentTrace` 对每个候选的 rotation/x/y/loss 按 f32 位模式比对，容差 1e-6/1e-5）。

## 函数逐条比对

### `CDConfig`（coord_descent.rs:9-21）
- C++ 对应：`CoordinateDescentConfig`（CoordinateDescent.h:16-24）。字段一一对应：t_step_init→translationStepInit、t_step_limit→translationStepLimit、r_step_init→rotationStepInit、r_step_limit→rotationStepLimit、wiggle→rotationWiggle。
- 判定：【一致】
- 证据：coord_descent.rs:10-20 五字段 vs CoordinateDescent.h:18-22 同五字段；C++ 额外第 23 行 `maxEvaluations`（见差异 #3）。

### `refine_coord_desc`（coord_descent.rs:24-62）
- C++ 对应：`RefineCoordinateDescent`（CoordinateDescent.cpp:159-238）。逐段核对：
  1. 状态初始化（Rust :34-43）：pos/eval 取 initial；`axis: CDAxis::random(rng, wiggle)`（消耗 1 次随机数）；t_steps 双向初始化为 t_step_init；r_step 取 r_step_init。C++ State 初始化（cpp:180-186）：position/evaluation 取 initial、`axis = ChooseAxis(rng, config.rotationWiggle)`、translationXStep/translationYStep 均 = translationStepInit、rotationStep = rotationStepInit。【一致】
  2. ask→evaluate→选 best 循环（Rust :46-58）：`while let Some(c) = cd.ask()`；两候选各评估一次（消耗 2 次评估）；`c.into_iter().zip(c_eval).min_by_key(|(_, eval)| *eval)` 取最小——Rust `min_by_key` 在相等时取**第一个**（std 文档：若多个元素同为最小，返回最后一个？否——`min_by_key` 返回最后一个最小元素；`min_by_key` 语义：若并列取最后出现的。见差异 #2 详述）；`cd.tell(best, rng)`。C++（cpp:189-234）：`while (HasCandidates(...))`，两候选逐一 `evaluator.Evaluate(...)`（:205-216），`bestIndex = IsBetterSample(evaluated[1].second, evaluated[0].second) ? 1 : 0`（:218-219）——即并列时选 index 0（第一个）。Rust `min_by_key` 并列时取**最后**一个（Iterator::min_by_key 文档："If several elements are equally minimum, the last element is returned"）。→ 并列时 Rust 选第二个候选、C++ 选第一个候选。但两候选 loss 完全相等仅在退化场景（同 loss 的两个不同变换），且 fixture 全程未出现并列。【差异】（边界并列时的选择方向相反；正常路径等价）
  3. 返回 `(cd.pos, cd.eval)`（Rust :61）↔ C++ `FromValue({state.position, state.evaluation})`（cpp:236-237）。【一致】
- 判定：【差异】（仅并列候选选择方向；其余逐行等价）
- 证据：上述行号；fixture SamplingCoordinateDescentRustReference.tsv 的 54 条 coordinate 候选（含 rotation/x/y/loss 位模式）在 TestSampling.cpp:868-893 逐位比对通过，证明 ask/tell 主循环与随机数消耗序列完全一致。

### `CoordinateDescent::ask`（coord_descent.rs:87-112）
- C++ 对应：`HasCandidates`（cpp:75-82）+ `BuildCandidates`（cpp:84-130）。三处细节：
  1. 停止条件（Rust :91）：`sx < limit && sy < limit && (sr < r_limit || !wiggle)` 时返回 None（注意：wiggle 关闭时旋转步长不参与停止判定）。C++ HasCandidates（cpp:78-81）：`!(txStep < limit && tyStep < limit && (!wiggle || rStep < rLimit))`——逻辑取反后逐字相同。【一致】
  2. 五轴候选公式（Rust :98-104）：Horizontal `[(tx+sx,ty,r),(tx-sx,ty,r)]`；Vertical `[+sy,-sy]`；ForwardDiag `[(+sx,+sy),(-sx,-sy)]`；BackwardDiag `[(-sx,+sy),(+sx,-sy)]`；Wiggle `[(tx,ty,r+sr),(tx,ty,r-sr)]`。C++ BuildCandidates（cpp:96-122）逐轴逐符号相同（AXIS_BACKWARD_DIAGONAL: firstX-=step/firstY+=step, secondX+=step/secondY-=step，cpp:112-116，对应 Rust BackwardDiag `[(-sx,+sy),(+sx,-sy)]`）。【一致】
  3. 数值精度：Rust 直接在 DTransformation 的 f32 字段上做 `tx + sx`；C++ 将 double 位置转 float（cpp:86-88）、在 float 上加减步长（cpp:99-120）、再转回 double（cpp:124-129）——与 Rust f32 算术一致。【一致】
- 判定：【一致】
- 证据：fixture 候选行验证（TestSampling.cpp:875-893，rotation 位模式含负角度如 `bcccccd0` 即 -0.025f，说明 wiggle 双侧候选公式被逐位锁定）。

### `CoordinateDescent::tell`（coord_descent.rs:115-147）
- C++ 对应：cpp:218-233 + `UpdateSteps`（cpp:132-154）。逐点：
  1. better/worse 判定（Rust :117-119 `eval.cmp(&self.eval)`：Less=better、Greater=worse）；C++（cpp:221-222）`better = IsBetterSample(best.second, state.evaluation)`、`worse = IsBetterSample(state.evaluation, best.second)`。SampleEval 的比较语义（含 FPA 容差）见 S10——C++ IsBetterSample 用 `RustApproxEqualF32`（4 ulp + f32::EPSILON 绝对容差）复刻 float-cmp 0.10 默认 margin（F32Margin{epsilon: f32::EPSILON, ulps: 4}，eq.rs:69-76），并列时 better=worse=false，与 Rust `Ordering::Equal` 时两个布尔均为 false 一致。【一致】
  2. `if !worse { (self.pos, self.eval) = (pos, eval) }`（Rust :121-124）↔ C++ `if (!worse) { state.position = best.first; state.evaluation = best.second; }`（cpp:223-227）。相等时也接受更新（与 Rust 相同）。【一致】
  3. 步长乘子（Rust :127-141）：better→CD_STEP_SUCCESS=1.1（consts.rs:11）、否则 CD_STEP_FAIL=0.5（consts.rs:14）；Horizontal 只乘 x 步、Vertical 只乘 y 步、对角两步各乘 m.sqrt()、Wiggle 乘 r_step。C++ UpdateSteps（cpp:132-154）+ cpp:229 `UpdateSteps(state, better ? 1.1F : 0.5F)`：逐轴相同，对角 `std::sqrt(multiplier)` 对应 `m.sqrt()`。【一致】
  4. `if !better { self.axis = CDAxis::random(rng, self.wiggle) }`（Rust :144-146）↔ C++ cpp:230-233 `if (!better) { state.axis = ChooseAxis(rng, config.rotationWiggle); }`。【一致】
- 判定：【一致】
- 证据：fixture 的 54 条候选序列（含步长收敛轨迹）与 result 行（最终 pos/loss）位级比对通过（TestSampling.cpp:896-905）。

### `CDAxis::random`（coord_descent.rs:164-179）
- C++ 对应：`ChooseAxis`（cpp:30-49）。Rust `rng.random_range(0..6)`（wiggle）或 `0..4`（无 wiggle），映射 0=Horizontal、1=Vertical、2=ForwardDiag、3=BackwardDiag、4..6=Wiggle；C++ `rng.UniformIndex(6U 或 4U)`，switch 映射完全相同（cpp:34-47）。
- 判定：【一致】
- 证据：coord_descent.rs:171-178 vs cpp:32-47。随机数语义对应：Rust rand 0.10 `random_range(0..6)`（u32 range）↔ C++ `SparrowRng::UniformIndex`（SparrowRng.cpp:61-81，Lemire u32×bound>>32 + 拒绝重采样），被 fixture `SparrowUniformIndexRustReference.tsv`（146 行，TestRng.cpp:169 消费）逐值锁定。整个 coord_descent fixture 的轴序列（54 候选 = 27 次 ask，每次消耗 1 个 u32 换轴随机数于 !better 时）与之吻合。

### 【C++ 额外】配置校验与评估预算（cpp:165-177, 189-216）
- C++ `IsValidCoordinateConfig`（cpp:51-63）：有限性、非负、init≥limit 校验（Rust 无校验，靠调用方保证）；`maxEvaluations` 预算检查（cpp:175-177, 198-201, 207-212）：每轮循环与每次评估前检查 `usedEvaluations >= maxEvaluations` 则返回 TimeLimit 错误；Rust 只有 `debug_assert!`（coord_descent.rs:57，n_evals 增量 < 1000，仅 debug 构建生效）。默认 maxEvaluations=SIZE_MAX（h:23），即默认行为与 Rust 相同。
- 判定：【C++ 额外】
- 影响：正常路径无影响；预算触发时 C++ 提前返回错误（调用方 SearchLbf/SeparatorWorker 有回退，见 S13 差异 #5），Rust debug 构建会 panic、release 构建会继续跑。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | `CDConfig` 额外字段 `maxEvaluations`（默认 SIZE_MAX）+ 配置/初始变换有限性校验 | C++ 额外 | 正常路径无影响；预算触发时返回 TimeLimit 错误（Rust 无此通道） | CoordinateDescent.h:23, cpp:51-63,165-177,198-212 vs coord_descent.rs 全文 |
| 2 | 两候选 loss 并列时的选择：Rust `min_by_key` 取最后一个（候选 2），C++ 三元取第一个（候选 0） | 差异 | 仅两候选 loss 位级相等时偏离；fixture 54 候选未出现并列 | coord_descent.rs:50-52 vs cpp:218-219 |
| 3 | Rust `debug_assert!`（1000 eval 上限）→ C++ Result 错误通道 | 差异 | 既定形态差异（panic→Result） | coord_descent.rs:57 vs cpp:198-201 |
| 4 | Rust `trace!` 日志 → C++ 无 | 差异 | 无行为影响 | coord_descent.rs:56,59 |

## 结论

坐标下降的算法本体——ask 停止条件、五轴候选公式（含符号方向）、tell 的 better/worse 判定（FPA 容差语义）、步长乘子 1.1/0.5、对角 sqrt 衰减、!better 换轴（UniformIndex 随机数语义）——逐项等价，并由 `SamplingCoordinateDescentRustReference.tsv` 以 f32 位模式逐候选锁定（54 候选 + 终态全部匹配）。仅有的实质差异是并列候选选择方向（边界场景）与 C++ 额外的评估预算/配置校验（Result 通道，默认不触发）。

### 补充审计（2026-09-29）
1. **【核对确认：179 行逐项覆盖】** coord_descent.rs 全量复核：CDConfig（5 字段）、refine_coord_desc、CoordinateDescent 结构（8 字段）、ask、tell、CDAxis（5 变体+random）——主文 5 小节全覆盖无遗漏。
2. **【遗漏补记：refine_coord_desc 的 min_by_key 并列语义方向修正】** 主文条目 2 断言"min_by_key 并列取最后一个（候选 2）"。现场核对 Rust std 文档与实现：`Iterator::min_by_key` 的文档为 "If several elements are equally minimum, the **last** element is returned"——主文断言正确；但**两候选元组是 (DTransformation, SampleEval)，min_by_key 键为 eval**，并列即两候选 eval 位级相等（或 FPA 相等——Ord 走 FPA，近似相等返回 Equal 触发 last 语义）。C++ `IsBetterSample(evaluated[1], evaluated[0]) ? 1 : 0`——近似相等时 IsBetterSample=false → 选 0。**近似相等（非位级相等）的并列**在 Rust 取候选 2、C++ 取候选 0——触发面比主文"位级相等"更宽（4-ulp 内即触发）。判定：维持【差异】（边界），但影响面应从"仅位级相等"修正为"4-ulp 容差内的近似相等"（fixture 54 候选未出现，实际仍低频）。
3. **【浅覆盖补全：tell 的 !worse 更新含 Equal】** Rust `if !worse { (pos, eval) = (pos, eval) }`——Equal（FPA 近似相等）时也接受新位置（coord_descent.rs:121-124）；C++ `if (!worse)` 同（cpp:223-227，IsBetterSample 双向 false 含 Equal）。主文条目 2（tell 第 2 点）已记"相等时也接受更新"，两侧一致判定维持；与上一条并列选择差**叠加**时：近似相等的候选 2 在 Rust 既被选中又更新位置、C++ 选候选 0——位置轨迹分叉点。归并为差异 #2 的完整描述。
4. **【核对确认：CDAxis::random 的 4..6 双值映射】** `0..6` 时 4、5 均映射 Wiggle（coord_descent.rs:171-178）——即 Wiggle 被抽中概率 2/6、其余各 1/6；C++ `UniformIndex(6U)` switch 同映射（cpp:34-47）。主文"switch 映射完全相同"判定成立，此概率分布细节补记供后续验证随机流。
