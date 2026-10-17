# S09. lbf_evaluator（LBF 采样评估器）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/lbf_evaluator.rs`（59 行）+ `CodeResource/sparrow/src/eval/mod.rs`（4 行）
**C++**: `SparrowCore/Include/SparrowCore/Eval/LbfEvaluator.h` + `SparrowCore/Source/Eval/LbfEvaluator.cpp`。历史转发类 `SparrowLbfEvaluator` 已按 G9 删除。
**判定**: 固定 LBF search reference 的评分、分类和离散搜索事件与 Rust 对齐；C++ 头文件记录的 layout/item 稳定性前置条件与 Rust 共享借用对应。C++ 仍有 LegalizeTransform 防御、可选 filter 和碰撞检查顺序差异，按下文限定范围登记。

评估语义由两类有限参考覆盖：`LbfSearchRustTrace.tsv` 是 seed=42、16 container/0 focused/2 CD 的直接搜索轨迹，共 104 条 evaluator 记录（75 Clear、29 Invalid）。测试精确比较事件类型/阶段、evaluation kind、accepted、retained 数量与 evaluation count；transform 容差为 2e-5，所有 Clear loss 以 raw `f32` bits 精确比较（`TestSampling.cpp::SameTraceSample`），并已在 SparrowCore Debug/Release 全量 runner 中通过。`SparrowLbfRustReference.tsv` 是单件 SPP builder 参考：placed/remaining/feasible 精确比较，width/density 分别用 4e-3/1e-3 容差（TestSppLbf.cpp:522-533）。这些 fixture 不外推到任意几何或默认整条优化流水线。

## 函数逐条比对

### `X_MULTIPLIER = 10.0` / `Y_MULTIPLIER = 1.0`（lbf_evaluator.rs:9-10）
- C++ 对应：LbfLoss.cpp:9 `constexpr float X_MULTIPLIER = 10.0F;`（Y 乘子 1.0 隐含在公式里）。eval 层的 loss 公式直接写 `10.0F * (poiX + bboxX) + (poiY + bboxY)`（LbfEvaluator.cpp:102），optimizer 层 LbfLoss::Cost 同常量（`xMax * 10 + yMax`，LbfLoss.cpp:25）。
- 判定：【一致】
- 证据：lbf_evaluator.rs:9-10 vs LbfEvaluator.cpp:102、LbfLoss.cpp:9,25。

### `LBFEvaluator::new`（lbf_evaluator.rs:22-30）
- C++ 对应：构造函数（LbfEvaluator.cpp:141-156）。Rust 保存 `layout` 引用、`item` 引用、`shape_buff`（item.shape_cd 的克隆，评估时复用缓冲）、n_evals=0。C++ 保存 m_Item 指针、m_Filter（经 MakeEffectiveFilter 包装，见下）、**以及一个 Layout 快照的独立 CollisionEngine**（cpp:147-155 `layout.Save()` → `snapshot.container.CreateCollisionEngine()` → `Restore(snapshot.cdeSnapshot)`）——因为 C++ 的 CollisionEngine 不绑定 Layout 生命周期，需要快照重建。Rust 直接借用 layout 的 cde（每次 evaluate 用 `self.layout.cde()`，:36）。
- 判定：【条件等价】（layout/item 在 evaluator 生命周期内不变时，快照重建与 Rust 借用 live CDE 等价；Rust `&Layout`/`&Item` 借用阻止安全代码同时取得可变引用，C++ 头文件现记录相同稳定性前置条件）
- 证据：lbf_evaluator.rs:23-30,36 vs LbfEvaluator.cpp:141-156,182-202。Rust 对 layout 的不可变借用通常保证评估期间状态稳定；C++ evaluator 保存独立快照，若调用方通过其他别名继续修改 layout，C++ 可能读取旧布局，Rust 安全借用接口不能以相同方式形成该状态。

### `SampleEvaluator::evaluate_sample`（lbf_evaluator.rs:34-54）
- C++ 对应：`LbfEvaluator::Evaluate`（LbfEvaluator.cpp:158-207）。逐段核对：
  1. **计数**：Rust :35 `self.n_evals += 1`；C++ cpp:162 `++m_EvaluationCount`。【一致】
  2. **代理碰撞**：Rust :38 `cde.detect_surrogate_collision(item.shape_cd.surrogate(), &transf, &NoFilter)` → true 则 Invalid；C++ cpp:182-187 `m_Engine->DetectSurrogateCollision(*surrogate, *legalTransform, m_Filter)`，失败或 true 则 Invalid。差异点：(a) C++ 先做 `LegalizeTransform`（旋转合法化，None→0、Discrete→最近允许角，cpp:168-172 / cpp:31-87）——Rust 无此步，因为采样器产出的旋转本就合法（uniform_sampler 只从合法旋转集采样）；这是防御性冗余，正常路径恒等变换（Discrete 时最近角=自身）。(b) C++ 的 filter 经 `MakeEffectiveFilter` 包装（cpp:110-136）：把 Hole 类型的 hazard 强制为"不可忽略"（`entity.kind == Hole → return false` 即不跳过），再叠加调用方的 irrelevant/irrelevantHandle——对应 jagua-rs `NoFilter`（不过滤任何 hazard）时，若调用方传空 filter，效果等同 NoFilter + Hole 例外。Rust `NoFilter` 语义是**完全不滤**。hole 是否参与 LBF 评估？jagua-rs 的 detect_surrogate_collision 对容器外环/洞的处理：容器洞是 hazard（jagua-rs 容器 hole 视为 hazard，碰撞含洞内区域）。Rust NoFilter 意味着洞也参与碰撞检测。C++ 的 Hole 例外强制洞参与——与 NoFilter 一致（洞不被滤掉）。【差异】（C++ 额外的 LegalizeTransform 防御 + Hole 语义显式化；行为等价）
  3. **精确形状碰撞**：Rust :41-43 `shape_buff.transform_from(item.shape_cd, &transf)` → `cde.detect_poly_collision(&shape_buff, &NoFilter)` → true 则 Invalid。C++ cpp:189-204：`TransformedRustF32(legalTransform)` 后先算 loss（cpp:196）再检测多边形碰撞（cpp:202），顺序与 Rust 相反（Rust 先碰撞后算 loss）。碰撞时 C++ 丢弃该 loss 并返回 INVALID，故当前对外 SampleEval 相同，但多做 POI/bbox 工作。源码注释只说明 surrogate 检测先行，不证明该精确检测顺序是有意设计，本文不推断其设计意图。另注意 jagua-rs detect_poly_collision 是精确多边形（非 surrogate），TransformedRustF32 名义上选择 f32 变换路径。【实现顺序差异；指定路径外未验证】
  4. **损失公式**：Rust :46-49：`poi = shape_buff.poi.center`（变换后形状的 POI 中心）、`bbox_corner = shape_buff.bbox.corners()[0]`、`loss = 10.0*(poi.0 + bbox_corner.0) + 1.0*(poi.1 + bbox_corner.1)`。jagua-rs `Rect::corners()[0]`（rect.rs:161-169）= `Point(x_max, y_max)`——**右上角**。C++ cpp:89-108 `CalculateLbfLoss`：`poi = shape.PointOfInaccessibility().center`、`bbox = shape.BoundingBox()`、`bboxX = static_cast<float>(bbox.xMax)`、`bboxY = bbox.yMax`（cpp:95-96）、`value = 10.0F*(poiX+bboxX) + (poiY+bboxY)`。逐项一致（xMax/yMax = 右上角）。【一致】
  5. **精度路径**：Rust 使用 f32。C++ `TransformedRustF32` 以 f32 变换顶点和 POI 中心，并由这些 f32 顶点重算 bbox；JaguarCore 用 `Point2d`/`Rect2d` 保存结果，但对应值是 f32 可表示数，再由 `CalculateLbfLoss` 按 f32 公式计算。固定 search trace 的 75 条 Clear evaluator loss 在 Debug/Release 均与 Rust raw bits 相同。该 fixture 证明所列输入和变换，不外推任意几何、所有平台或其他浮点编译选项。【源码路径对应；固定 fixture 逐位支持】
  6. **Invalid 语义**：Rust 没有显式 `SampleEval::Collision` 分支——LBF 评估器从不返回 Collision（:39,:43 只返回 Invalid，:49 返回 Clear）。C++ 同样只返回 INVALID 或 CLEAR（cpp:186,204,206）。【一致】
  7. **upper_bound 忽略**：Rust `_upper_bound: Option<SampleEval>` 显式忽略（:34）；C++ 同样不使用（cpp:159-160 空参数名）。LBF loss 无上界提前终止。【一致】
- 判定：【一致】（含两条顺序/防御差异）
- 证据：fixture `LbfSearchRustTrace.tsv` 共 159 条数据记录，其中 104 条 eval（16 container / 56 prerefine / 32 finalrefine），另有 report/keep/result 事件覆盖三阶段，末尾 RNG 哨兵的消费后对照见 S13。`TestRustLbfSearchTrace` 精确比较事件类型、阶段、分类、报告/保留样本和 evaluation count；`SameTraceSample` 对 transform 使用 2e-5 容差，对所有 Clear loss 比较 raw f32 bits。

### `n_evals`（lbf_evaluator.rs:56-58）
- C++ 对应：`EvaluationCount()`（LbfEvaluator.cpp:209-212）。计数器只加不减。【一致】
- 证据：lbf_evaluator.rs:56-58 vs cpp:209-212。

### `eval/mod.rs`（mod.rs:1-4）
- 模块声明：collision_loss（私有）、pub lbf_evaluator、pub sample_eval、pub sep_evaluator。C++ 对应头文件分发（LbfEvaluator.h/SeparationEvaluator.h/SampleEval.h 公开；碰撞损失逻辑在 Quantify/ 内部，见 S12）。组织性对应，无逻辑。
- 判定：【一致】
- 证据：mod.rs:1-4。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | LegalizeTransform 防御（旋转合法化，None→0、Discrete→最近允许角） | C++ 额外 | 采样器产出本就合法，正常路径恒等；提供 API 层稳健性 | LbfEvaluator.cpp:168-172,31-87 vs lbf_evaluator.rs:34-54 无对应 |
| 2 | 布局引擎持有方式：Rust 借用 live `layout.cde()` vs C++ Save() 快照重建独立 CollisionEngine | Rust 有效借用域内等价；C++ 已记录稳定性前置条件 | Rust 安全借用期间不能合法修改 layout；可经其他别名修改的 C++ 用法不属于本 parity 输入域 | lbf_evaluator.rs:23-30,36; `LbfEvaluator.h`; cpp:141-156,182 |
| 3 | filter 语义：Rust NoFilter vs C++ MakeEffectiveFilter 强制 Hole 不被忽略 + 叠加调用方 filter | 差异 | 默认（无调用方 filter）下与 NoFilter 等价；Hole 例外是 jagua-rs 语义的显式化 | lbf_evaluator.rs:38,42 vs cpp:110-136 |
| 4 | loss 计算与精确碰撞检测的顺序：Rust 先检测后算 loss，C++ 先算 loss 后检测 | 差异 | 对外 SampleEval 相同；碰撞场景多算一次 POI/bbox。注释没有证明此顺序是刻意选择 | lbf_evaluator.rs:41-49 vs cpp:189-204 |
| 5 | 几何精度路径：Rust f32；C++ `TransformedRustF32` 以 f32 计算变换后的顶点、POI 中心及 bbox 输入，再以 f32 公式计算 loss | 固定 trace 一致；任意几何未认证 | 75 条 Clear evaluator loss 在 Debug/Release raw bits 相同；固定 fixture 不外推任意几何和平台 | lbf_evaluator.rs:46-53; SimplePolygon.cpp:338-367; LbfEvaluator.cpp:89-108; TestSampling.cpp::TestRustLbfSearchTrace |
| 6 | 历史 `SparrowLbfEvaluator` 转发封装 | 已删除（G9） | 当前没有对应源文件或运行时逻辑 | 移植工作项清单 G9；SparrowCore.pro |

## 结论

LBF 评估器的主语义——"surrogate 先行、精确多边形检测、无碰撞时按 10·(poiX+bboxXMax)+(poiY+bboxYMax) 计 loss、从不返回 Collision"——可由源码逐项对应。固定 LBF search trace 的离散事件和 loss raw bits 精确匹配，transform 使用 2e-5 容差；单件 builder reference 只核对 placed/remaining/feasible 及 width/density 容差。结论限定在该 seed、输入、预算和默认标量路径，不证明任意几何或完整优化流水线。Rust 共享借用与 C++ evaluator 稳定性前置条件对齐；保留的差异是 LegalizeTransform 防御、C++ filter 扩展及 loss/精确碰撞检测顺序。

### 补充审计（2026-09-29）
1. **【核对确认：59+4 行逐项覆盖】** lbf_evaluator.rs 全量复核：X/Y_MULTIPLIER 常量、LBFEvaluator 结构（4 字段）、new、evaluate_sample（含 n_evals 计数/surrogate 检测/精确检测/loss 公式）、n_evals；eval/mod.rs 4 行模块声明——主文 7 小节全覆盖无遗漏。
2. **【浅覆盖补全：LegalizeTransform 的角距公式（S09 侧差异 #1 的精确化）】** 主文差异 #1 笼统写"Discrete→最近允许角"。现场核对 LbfEvaluator.cpp:19-29/31-87：其 `ShortestRotationDistance = NormalizeRotation(fmod)→min(d, 2π-d)` **是循环最短角距**，与 Rust `convert_sample_to_closest_feasible`（normalize 后 abs，非环距）在**跨 ±π 边界的角集**（如 {350°,10°}）可选不同角——与 S11 补充审计第 3 条发现的 SeparationEvaluator **朴素绝对差版**（无归一化，偏离更大）构成**三版 Legalize**。主文差异 #1 判定"正常路径恒等"维持（采样器产合法旋转，Legalize 为恒等），但应知悉三版并非同源实现。判定：维持【C++ 额外（防御）】+ 边界差并入 S06 差异 #5 类。
3. **【浅覆盖补全：MakeEffectiveFilter 的 Hole 例外与 evaluator 独立引擎】** 现场确认（LbfEvaluator.cpp:110-136）：filter 语义为 `entity.kind == Hole → 不忽略`（强制参与）+ 调用方 irrelevant/irrelevantHandle 叠加——主文差异 #3 已记；补记：LbfEvaluator 的独立引擎（快照重建，:141-155）**不注册 Exterior 静态 hazard**？现场核对构造（:141-156）仅 `Restore(snapshot.cdeSnapshot)` 恢复引擎全部静态 hazard（含容器外环/洞）——语义=布局 CDE 快照，与 Rust `layout.cde()` 同内容，主文差异 #2 判定维持。

### 收口复核（2026-10-17）

S12 完成后进入 S09。重审源码发现历史 `SparrowLbfEvaluator` 包装类已经按 G9 删除；并且 C++ `TransformedRustF32` 不会在 POI/bbox 上额外进行 double 几何运算：变换后的顶点与 POI 中心均经 f32 路径产生，bbox 从这些顶点重算后以 double 容器保存，loss 再按 f32 算术装配。已将 `TestRustLbfSearchTrace` 对所有 Clear loss 的断言提升为 raw-bit 相等。fixture 含 104 条 evaluator 记录（75 Clear、29 Invalid）。MSVC x64 Debug 与 Release SparrowCore 全量 runner 均以退出码 0 通过；fixture 固定 seed=42、单件 SPP 输入、16 container/0 focused/2 CD，不能推广到任意几何或其他平台/浮点编译选项。

可变 layout/item 在 evaluator 生命周期内修改仍不属于 Rust 安全共享借用可表达的输入；C++ 头文件保留稳定性前置条件。LegalizeTransform 与可选 filter 是 C++ API 防御/扩展，正常 Rust 采样轨迹中 LegalizeTransform 保持恒等、默认调用不额外过滤 hazard。loss 在精确碰撞检测前计算，碰撞结果仍为 Invalid；此顺序差异继续作为实现差异记录。按以上固定输入、reference 与稳定性契约，S09 P0 项标记完成，任意几何和扩展 filter 行为不外推。
