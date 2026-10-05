# S09. lbf_evaluator（LBF 采样评估器）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/lbf_evaluator.rs`（59 行）+ `CodeResource/sparrow/src/eval/mod.rs`（4 行）
**C++**: `SparrowCore/Include/SparrowCore/eval/LbfEvaluator.h`（35 行）+ `Source/Eval/LbfEvaluator.cpp`（213 行）+ `Include/SparrowCore/eval/SparrowLbfEvaluator.h`（26 行）+ `Source/Eval/SparrowLbfEvaluator.cpp`（23 行）
**判定**: 基本等价（列差异）

评估语义被 `LbfSearchRustTrace.tsv`（158 条 eval 的 kind + loss f32 位模式，逐位锁定，TestSampling.cpp:1001 起消费）与 `SparrowLbfRustReference.tsv`（LBFBuilder 端到端：placed/width/density，TestSppLbf.cpp:471 消费）锁定。`SparrowLbfEvaluator` 是 LbfEvaluator 的纯转发包装（无逻辑，兼容层入口）。

## 函数逐条比对

### `X_MULTIPLIER = 10.0` / `Y_MULTIPLIER = 1.0`（lbf_evaluator.rs:9-10）
- C++ 对应：LbfLoss.cpp:9 `constexpr float X_MULTIPLIER = 10.0F;`（Y 乘子 1.0 隐含在公式里）。eval 层的 loss 公式直接写 `10.0F * (poiX + bboxX) + (poiY + bboxY)`（LbfEvaluator.cpp:102），optimizer 层 LbfLoss::Cost 同常量（`xMax * 10 + yMax`，LbfLoss.cpp:25）。
- 判定：【一致】
- 证据：lbf_evaluator.rs:9-10 vs LbfEvaluator.cpp:102、LbfLoss.cpp:9,25。

### `LBFEvaluator::new`（lbf_evaluator.rs:22-30）
- C++ 对应：构造函数（LbfEvaluator.cpp:141-156）。Rust 保存 `layout` 引用、`item` 引用、`shape_buff`（item.shape_cd 的克隆，评估时复用缓冲）、n_evals=0。C++ 保存 m_Item 指针、m_Filter（经 MakeEffectiveFilter 包装，见下）、**以及一个 Layout 快照的独立 CollisionEngine**（cpp:147-155 `layout.Save()` → `snapshot.container.CreateCollisionEngine()` → `Restore(snapshot.cdeSnapshot)`）——因为 C++ 的 CollisionEngine 不绑定 Layout 生命周期，需要快照重建。Rust 直接借用 layout 的 cde（每次 evaluate 用 `self.layout.cde()`，:36）。
- 判定：【差异】（实现形式：借用 layout 的 CDE vs 快照重建独立引擎；语义等价——快照即当前 layout 状态）
- 证据：lbf_evaluator.rs:23-30 vs LbfEvaluator.cpp:141-156；评估侧 Rust :36 `self.layout.cde()` vs C++ cpp:182-202 `m_Engine->Detect...`。

### `SampleEvaluator::evaluate_sample`（lbf_evaluator.rs:34-54）
- C++ 对应：`LbfEvaluator::Evaluate`（LbfEvaluator.cpp:158-207）。逐段核对：
  1. **计数**：Rust :35 `self.n_evals += 1`；C++ cpp:162 `++m_EvaluationCount`。【一致】
  2. **代理碰撞**：Rust :38 `cde.detect_surrogate_collision(item.shape_cd.surrogate(), &transf, &NoFilter)` → true 则 Invalid；C++ cpp:182-187 `m_Engine->DetectSurrogateCollision(*surrogate, *legalTransform, m_Filter)`，失败或 true 则 Invalid。差异点：(a) C++ 先做 `LegalizeTransform`（旋转合法化，None→0、Discrete→最近允许角，cpp:168-172 / cpp:31-87）——Rust 无此步，因为采样器产出的旋转本就合法（uniform_sampler 只从合法旋转集采样）；这是防御性冗余，正常路径恒等变换（Discrete 时最近角=自身）。(b) C++ 的 filter 经 `MakeEffectiveFilter` 包装（cpp:110-136）：把 Hole 类型的 hazard 强制为"不可忽略"（`entity.kind == Hole → return false` 即不跳过），再叠加调用方的 irrelevant/irrelevantHandle——对应 jagua-rs `NoFilter`（不过滤任何 hazard）时，若调用方传空 filter，效果等同 NoFilter + Hole 例外。Rust `NoFilter` 语义是**完全不滤**。hole 是否参与 LBF 评估？jagua-rs 的 detect_surrogate_collision 对容器外环/洞的处理：容器洞是 hazard（jagua-rs 容器 hole 视为 hazard，碰撞含洞内区域）。Rust NoFilter 意味着洞也参与碰撞检测。C++ 的 Hole 例外强制洞参与——与 NoFilter 一致（洞不被滤掉）。【差异】（C++ 额外的 LegalizeTransform 防御 + Hole 语义显式化；行为等价）
  3. **精确形状碰撞**：Rust :41-43 `shape_buff.transform_from(item.shape_cd, &transf)` → `cde.detect_poly_collision(&shape_buff, &NoFilter)` → true 则 Invalid。C++ cpp:189-204：`TransformedRustF32(legalTransform)`（对应 jagua-rs 的 f32 变换路径）→ **先算 loss（cpp:196）再检测多边形碰撞（cpp:202）**——顺序与 Rust 相反（Rust 先碰撞后算 loss）。由于碰撞时 C++ 只丢弃 loss（返回 INVALID），不产生额外副作用，两者对外行为一致；但 C++ 在碰撞场景多算了一次 POI/bbox。C++ 侧有注释明确说明这是刻意的"fail-fast 对齐"（cpp:180-181 注释讲的是 surrogate 先行；顺序差异见差异表 #4）。另注意 jagua-rs detect_poly_collision 是精确多边形（非 surrogate），TransformedRustF32 名义对齐 f32 语义。【差异】（loss 计算与碰撞检测顺序互换；无外部行为影响）
  4. **损失公式**：Rust :46-49：`poi = shape_buff.poi.center`（变换后形状的 POI 中心）、`bbox_corner = shape_buff.bbox.corners()[0]`、`loss = 10.0*(poi.0 + bbox_corner.0) + 1.0*(poi.1 + bbox_corner.1)`。jagua-rs `Rect::corners()[0]`（rect.rs:161-169）= `Point(x_max, y_max)`——**右上角**。C++ cpp:89-108 `CalculateLbfLoss`：`poi = shape.PointOfInaccessibility().center`、`bbox = shape.BoundingBox()`、`bboxX = static_cast<float>(bbox.xMax)`、`bboxY = bbox.yMax`（cpp:95-96）、`value = 10.0F*(poiX+bboxX) + (poiY+bboxY)`。逐项一致（xMax/yMax = 右上角）。【一致】
  5. **精度路径**：Rust 全 f32（shape_buff 为 f32 多边形，POI/bbox 均 f32，loss f32）；C++ POI/BoundingBox 由 JaguarCore 给出 double（PointOfInaccessibility().center 是 Point2d(double)，BoundingBox() 是 Rect2d(double)），在 cpp:93-96 cast 为 float 后按 float 算 loss。即 C++ 内部几何是 double 双轨（JaguarCore 封存特性），仅在 loss 装配点转 f32——与 RustF32 对齐（TransformedRustF32 保证变换后的顶点与 jagua-rs f32 一致，后续 POI/bbox 在 double 上算再 cast，可能有 ulp 级差别，但被 LbfSearchRustTrace.tsv 的 loss 位模式逐位验证通过）。【一致】（经 fixture 证实位级等价）
  6. **Invalid 语义**：Rust 没有显式 `SampleEval::Collision` 分支——LBF 评估器从不返回 Collision（:39,:43 只返回 Invalid，:49 返回 Clear）。C++ 同样只返回 INVALID 或 CLEAR（cpp:186,204,206）。【一致】
  7. **upper_bound 忽略**：Rust `_upper_bound: Option<SampleEval>` 显式忽略（:34）；C++ 同样不使用（cpp:159-160 空参数名）。LBF loss 无上界提前终止。【一致】
- 判定：【一致】（含两条顺序/防御差异）
- 证据：fixture LbfSearchRustTrace.tsv 全部 158 条 eval 的 kind/loss 位模式（如首行 `clear 42afe4d4` = 82.99f）与 C++ trace 逐位匹配（TestSampling.cpp:1001 起的逐行比对），覆盖 container/prerefine/finalrefine 三阶段。

### `n_evals`（lbf_evaluator.rs:56-58）
- C++ 对应：`EvaluationCount()`（LbfEvaluator.cpp:209-212）。计数器只加不减。【一致】
- 证据：lbf_evaluator.rs:56-58 vs cpp:209-212；SparrowLbfEvaluator.cpp:19-22 转发。

### `SparrowLbfEvaluator`（SparrowLbfEvaluator.h:10-26 / cpp 全文）
- Rust 对应：无（Rust 直接用 `LBFEvaluator`）。C++ 的 SparrowLbfEvaluator 是 LbfEvaluator 的公开包装类：构造转发（cpp:5-10）、Evaluate 转发（cpp:12-17）、EvaluationCount 转发（cpp:19-22），零逻辑。存在原因是把内部 LbfEvaluator 暴露为带 SPARROWCORE_API 宏的稳定入口。
- 判定：【C++ 额外】（纯转发包装层）
- 证据：SparrowLbfEvaluator.cpp:12-17 `return m_Delegate.Evaluate(transform, upperBound);`。

### `eval/mod.rs`（mod.rs:1-4）
- 模块声明：collision_loss（私有）、pub lbf_evaluator、pub sample_eval、pub sep_evaluator。C++ 对应头文件分发（LbfEvaluator.h/SeparationEvaluator.h/SampleEval.h 公开；碰撞损失逻辑在 Quantify/ 内部，见 S12）。组织性对应，无逻辑。
- 判定：【一致】
- 证据：mod.rs:1-4。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | LegalizeTransform 防御（旋转合法化，None→0、Discrete→最近允许角） | C++ 额外 | 采样器产出本就合法，正常路径恒等；提供 API 层稳健性 | LbfEvaluator.cpp:168-172,31-87 vs lbf_evaluator.rs:34-54 无对应 |
| 2 | 布局引擎持有方式：借用 layout.cde() vs Save() 快照重建独立 CollisionEngine | 差异 | 实现形式差异；语义等价（快照=当前布局），多一次快照成本 | lbf_evaluator.rs:36 vs cpp:147-155,182 |
| 3 | filter 语义：Rust NoFilter vs C++ MakeEffectiveFilter 强制 Hole 不被忽略 + 叠加调用方 filter | 差异 | 默认（无调用方 filter）下与 NoFilter 等价；Hole 例外是 jagua-rs 语义的显式化 | lbf_evaluator.rs:38,42 vs cpp:110-136 |
| 4 | loss 计算与精确碰撞检测的顺序：Rust 先检测后算 loss，C++ 先算 loss 后检测 | 差异 | 对外行为一致（碰撞时 loss 被丢弃）；碰撞场景多算一次 POI/bbox | lbf_evaluator.rs:41-49 vs cpp:189-204 |
| 5 | 几何精度路径：Rust 全 f32，C++ TransformedRustF32 + POI/bbox 在 double 上计算后 cast f32 | 差异 | ulp 级风险，已被 loss 位模式 fixture 验证等价 | lbf_evaluator.rs:46-48 vs cpp:93-102 |
| 6 | `SparrowLbfEvaluator` 纯转发包装 | C++ 额外 | API 形态差异，无行为影响 | SparrowLbfEvaluator.cpp:12-22 |

## 结论

LBF 评估器的核心语义——"surrogate 先行、精确多边形随后、无碰撞时按 10·(poiX+bboxXMax)+(poiY+bboxYMax) 计 loss、从不返回 Collision"——逐项对齐，loss 的 f32 位模式被 LbfSearchRustTrace.tsv 全量锁定，端到端构造被 SparrowLbfRustReference.tsv 锁定。差异集中在实现形式（快照引擎 vs 借用 CDE、合法化防御、loss/检测顺序）与既定 API 形态（SparrowLbfEvaluator 包装、Result 错误通道），正常输入下行为等价。

### 补充审计（2026-09-29）
1. **【核对确认：59+4 行逐项覆盖】** lbf_evaluator.rs 全量复核：X/Y_MULTIPLIER 常量、LBFEvaluator 结构（4 字段）、new、evaluate_sample（含 n_evals 计数/surrogate 检测/精确检测/loss 公式）、n_evals；eval/mod.rs 4 行模块声明——主文 7 小节全覆盖无遗漏。
2. **【浅覆盖补全：LegalizeTransform 的角距公式（S09 侧差异 #1 的精确化）】** 主文差异 #1 笼统写"Discrete→最近允许角"。现场核对 LbfEvaluator.cpp:19-29/31-87：其 `ShortestRotationDistance = NormalizeRotation(fmod)→min(d, 2π-d)` **是循环最短角距**，与 Rust `convert_sample_to_closest_feasible`（normalize 后 abs，非环距）在**跨 ±π 边界的角集**（如 {350°,10°}）可选不同角——与 S11 补充审计第 3 条发现的 SeparationEvaluator **朴素绝对差版**（无归一化，偏离更大）构成**三版 Legalize**。主文差异 #1 判定"正常路径恒等"维持（采样器产合法旋转，Legalize 为恒等），但应知悉三版并非同源实现。判定：维持【C++ 额外（防御）】+ 边界差并入 S06 差异 #5 类。
3. **【浅覆盖补全：MakeEffectiveFilter 的 Hole 例外与 evaluator 独立引擎】** 现场确认（LbfEvaluator.cpp:110-136）：filter 语义为 `entity.kind == Hole → 不忽略`（强制参与）+ 调用方 irrelevant/irrelevantHandle 叠加——主文差异 #3 已记；补记：LbfEvaluator 的独立引擎（快照重建，:141-155）**不注册 Exterior 静态 hazard**？现场核对构造（:141-156）仅 `Restore(snapshot.cdeSnapshot)` 恢复引擎全部静态 hazard（含容器外环/洞）——语义=布局 CDE 快照，与 Rust `layout.cde()` 同内容，主文差异 #2 判定维持。
