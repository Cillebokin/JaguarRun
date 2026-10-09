# S11. sep_evaluator（分离评估器）逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/sep_evaluator.rs`（103 行）
**C++**: `SparrowCore/Include/SparrowCore/Eval/SeparationEvaluator.h`（44 行）+ `Source/Eval/SeparationEvaluator.cpp`（215 行）
**判定**: 基本等价（逐 hazard 短路已实现并有定向 reference；端到端覆盖边界见下文）

评估语义有直接搜索和整轮两层 Rust trace 覆盖。`SeparatorSearchRustTrace.tsv` 是 seed=42、16 container/8 focused/2 CD 的单次 separator move 搜索；测试精确比较事件类型、阶段、kind、accepted、retained 数量及结果计数，transform 容差 2e-5、非 Invalid loss 容差 2e-3（TestSeparator.cpp:782-799,901-958）。`loss_f32_bits` 字段会解析成 float 后用容差比较。单/多 worker round fixtures 只支持各自记录的输入、worker 数和 seed，pair loss/weight 容差分别为 2e-2/5e-2；不能外推为通用位级证明（见 S04/S05）。

## 函数逐条比对

### `SeparationEvaluator::new`（sep_evaluator.rs:21-47）
- C++ 对应：构造函数（SeparationEvaluator.cpp:63-114）。逐项：
  1. **当前 hazard 识别**：Rust :28-35 `layout.cde().haz_key_from_pi_key(current_pk).expect(...)` 取移动件在 CDE 中的 hazard key 与 entity；C++ :77-101 从布局快照逐放置注册 hazard，`placement.handle == movingHandle` 时记 `m_MovingHazardHandle`；找不到则引擎置空（后续 Evaluate 全部 INVALID）。【差异】（实现路径：直接查 CDE 注册表 vs 快照重建 + 匹配 handle；语义等价——移动件的 hazard 都被识别）
  2. **collector 预置**：Rust :40 `BasicHazardCollector::with_capacity(placed_items.len()+1)`；C++ 用 `jaguar::HazardCollector collisions`（cpp:137）且把"跳过移动件自身"编码进 filter（`irrelevantHandle = (handle == movingHazardHandle)`，cpp:105-110），对应 Rust 在 evaluate 里 `collector.insert(current_hazard)`（:67）令遍历跳过自身。两处机制不同但语义一致：碰撞遍历忽略移动件旧位置。【一致】
  3. **loss 评估器**：Rust 内联 `CollisionLossEvaluator`；C++ 持 `m_Tracker` 与 `m_MovingIndex`，按 hazard 调 `EvaluateCandidateHazardLoss`，再由 CDE 回调按 Rust 上界语义累加/短路（详见 S12）。
  4. **shape_buff 克隆**：Rust :43 复用缓冲多次变换；C++ 每次 Evaluate 调 `TransformedRustF32` 新建（cpp:149）。性能形式差异，无行为影响。【差异】（缓冲复用 vs 逐次构造）
- 判定：【一致】（形式差异如上）
- 证据：sep_evaluator.rs:21-47 vs SeparationEvaluator.cpp:63-114。

### `evaluate_sample`（sep_evaluator.rs:52-98）— 上界解析
- Rust 将 Collision 上界映射为该 loss、Clear 上界映射为 0、无上界映射为正无穷；每个 hazard 加入 loss 后以严格 `>` 检查是否超过上界。PlacedItem 的单项 loss 也受剩余预算约束，超出时当前 hazard 触发停止。
- C++ 将相同规则接入 `CollectSurrogateCollisionsUntil` 和 `CollectPolygonCollisionsUntil` 的逐 hazard 回调。每次通过 `EvaluateCandidateHazardLoss` 计算单项贡献；PlacedItem 超过剩余预算时立即停止，累计 loss 超界时也停止。surrogate 停止后直接返回 Invalid；exact 阶段对 surrogate 阶段已经发现的实体去重，不重复计损失。
- Clear 上界下，正 loss 会超过 0 上界并返回 Invalid；若碰撞贡献恰为 0，Rust 的严格 `>` 不早停，最终返回 `Collision{loss: 0}`。C++ 现也保留该分类，不再因 Clear 上界将它改为 Invalid。`collision_loss_stream_reference.rs::clear_upper_bound_preserves_a_zero_loss_collision_classification` 与 `TestRustMultiHazardLossAndEarlyStop` 用最小正 f32 权重产生真实重叠，并分别断言 Rust/C++ 得到 `Collision(0)`；该边界已从旧代码差异改为定向对齐证据。
- `CollisionLossMultiHazardRust.json` / `collision_loss_stream_reference.rs` / `TestRustMultiHazardLossAndEarlyStop` 覆盖三项 PlacedItem hazard：Rust 发现顺序 `[0,1,2]`，固定权重下 raw loss 为 `0x4ab5c931`，逆序累计为 `0x4ab5c932`；C++ evaluator 的 raw loss 位模式匹配 `0x4ab5c931`。正损失与 Clear 上界比较时返回 Invalid；另一个真实零损失碰撞在 Clear 上界下返回 `Collision(0)`；超预算 surrogate loss 会在 exact collection 前停止。
- 判定：【已修复并通过定向 Rust/C++ reference】（逐 hazard 短路、该输入的 hazard 次序与多 hazard raw loss 对齐；其他几何、Exterior hazard 和所有上界邻域仍未覆盖）。
- 证据：`sep_evaluator.rs:57-98`、`collision_loss.rs:44-56`，`SeparationEvaluator.cpp` 的 `Evaluate` 与 `CollisionTracker.cpp` 的 `EvaluateCandidateHazardLoss`；`collision_loss_stream_reference.rs` 和 `TestQuantify.cpp`。

### `evaluate_sample` — 变换与两段碰撞收集
- Rust 在候选变换后先做 surrogate hazard 遍历，再做 precise polygon 遍历；两段都通过 `loss_evaluator.add` 更新同一 collector 和累计 loss，任一阶段超界都会返回 Invalid。移动件自身 hazard 预置在 collector 中，以便被去重。
- C++ 保留 `LegalizeTransform` 防御（见 S09 边界），分别运行 surrogate 与 precise 遍历，并将逐 hazard 累加接入两个 stop callback。exact 阶段通过 `ContainsEntity` 跳过 surrogate collector 已见实体，结束后合并新实体。
- 无碰撞时返回 `Clear{0}`；有碰撞时返回按回调发现顺序累加的 loss。此前 C++ 的 `1e-6` 下限已移除；非对称方形/矩形 fixture 的 11 个候选 raw-loss 位模式逐一匹配 Rust，pole 参数顺序修正见 S12。多 hazard 次序与早停已有上一节的指定 fixture 覆盖，不应再标为未实现。
- 判定：【基本等价】（分阶段结构与 collector 形态不同；流式计分及指定多 hazard 结果已由 reference 覆盖）。
- 证据：`sep_evaluator.rs:62-97`、`SeparationEvaluator.cpp` 的 `Evaluate`；`collision_loss_floor_reference.rs`、`collision_loss_stream_reference.rs` 与 `TestQuantify.cpp`。

### `n_evals`（sep_evaluator.rs:100-102）
- C++ 对应：`EvaluationCount()`（cpp:198-201）。【一致】

### 【C++ 额外】统计计数器 SurrogateRejectCount / ExactCollisionCount（h:29-30, cpp:203-211）
- C++ 额外维护 surrogate 命中数与精确碰撞数两个诊断计数器（SeparatorWorkerResult 上报，见 S05），Rust 无对应物（SepStats 只有 total_moves/total_evals，worker.rs:88-91）。纯诊断信息，不回灌算法。
- 判定：【C++ 额外】

### 【Rust→C++ 缺失】BasicHazardCollector 的容量语义
- Rust :40 `with_capacity(placed_items.len()+1)` 预分配；C++ HazardCollector 默认构造。无行为影响。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 逐 hazard 上界早停：Rust 与 C++ 都在 hazard 回调中累加并按严格 `>` 停止；三项 PlacedItem reference 得到 raw loss `0x4ab5c931`，逆序 Rust 累加为 `0x4ab5c932` | 已修复并由当前 fixture 验证 | 指定权重、布局和 PlacedItem hazard 次序一致；不证明其他几何、Exterior hazard 或所有上界邻域 | `collision_loss.rs:44-56`; `SeparationEvaluator.cpp`; `CollisionTracker.cpp`; `collision_loss_stream_reference.rs`; `TestQuantify.cpp` |
| 2 | 候选 loss 下限：旧 C++ `std::max(loss, 1e-6)` 已删除，现直接返回 `loss.Value()` | 已修复（Rust 原值语义） | 非对称方形/矩形夹具的 11 个候选（均低于 1e-6）与 Rust raw-loss 位模式一致；三项 PlacedItem 的多 hazard 累加另由 S12 fixture 覆盖；其他几何仍未覆盖 | SeparationEvaluator.cpp:198; `collision_loss_floor_reference.rs`; `collision_loss_stream_reference.rs`; TestQuantify.cpp:307-483 |
| 3 | LegalizeTransform 防御（旋转合法化） | C++ 额外 | 正常路径恒等（调用方采样器产合法旋转） | cpp:126-130,15-58 vs sep_evaluator.rs:52-64 无 |
| 4 | 自身排除机制：Rust collector 预置当前 hazard vs C++ filter 的 irrelevantHandle | 差异 | 实现形式差异，语义等价（遍历都跳过移动件旧位置） | sep_evaluator.rs:65-67 vs cpp:105-110 |
| 5 | hazard 注册：Rust 查 cde 注册表（expect panic）vs C++ 快照重建 + handle 匹配（失败置空引擎→全 INVALID） | 差异 | 既定 panic→Result 形态差异；正常输入不触发 | sep_evaluator.rs:28-31 vs cpp:77-101 |
| 6 | shape 缓冲复用 vs 逐次 TransformedRustF32 构造 | 差异 | 性能形式差异，无行为影响 | sep_evaluator.rs:62-64 vs cpp:149 |
| 7 | 诊断计数器 SurrogateRejectCount/ExactCollisionCount | C++ 额外 | 纯诊断，不回灌算法 | h:29-30, cpp:156-157,164-167 |
| 8 | Clear 上界遇零损失碰撞：Rust 与 C++ 均返回 `Collision{loss:0}` | 已按 Rust 修复并由直接 evaluator reference 验证 | 分类、trace 与直接 evaluator API 结果一致；证据仅覆盖最小正权重产生的零舍入 loss 这一输入 | `collision_loss_stream_reference.rs::clear_upper_bound_preserves_a_zero_loss_collision_classification`; `TestRustMultiHazardLossAndEarlyStop`; SeparationEvaluator.cpp |

## 结论

SeparationEvaluator 的上界解析、surrogate/precise 两段检测、移动件自身过滤、正损失短路及 Clear 上界的零损失碰撞分类均有 Rust/C++ 定向 reference。三项 PlacedItem hazard 的指定累计顺序 raw bits 为 `0x4ab5c931`，逆序敏感性由 Rust oracle 给出 `0x4ab5c932`，低于 `1e-6` 的候选损失下限已移除，非对称方形/矩形的 11 个候选位模式一致。证据只适用于所列夹具，不外推到所有几何、Exterior hazard 或退化浮点输入。`LegalizeTransform` 的边界见补充审计，损失公式和其他限制详见 S12。

### 补充审计（2026-09-29，实体句柄专项）
**【任务指定核对项】SeparationEvaluator 私有引擎的实体句柄（2026-09-29 修复，commit 6910145）现场复验**：

1. **修复确认**：构造函数（SeparationEvaluator.cpp:76-101）注册 hazard 的实体字段已从 `{PlacedItem, itemIndex, 0, 0}` 补齐为 `{PlacedItem, itemIndex, 0, 0, placement.handle, placement.placedItem.Transform()}`（+`m_MovingHazardHandle` 记录移动件 hazard 句柄）。修复前同 item 多放置实例会在私有引擎触发 `RegisterHazard` 的重复实体拒绝（CollisionEngine.cpp:101-112），构造函数 `m_Engine.reset()` → 后续 Evaluate 全 INVALID；修复后以 (itemIndex, 布局句柄, f32 变换) 三元组保证实体唯一，与 jagua-rs `HazardEntity::PlacedItem { pk }` 的 slotmap 唯一键语义对应（与 pk 的语义等价性分析见 S18 补充审计第 2-3 条，此处不重复）。
2. **主文条目 1 更新**：主文写"hazard 注册：…找不到则引擎置空（后续 Evaluate 全部 INVALID）"——现状不变，但置空的一个历史诱因（重复实体）已被修复消除，剩余置空条件为 handle 不在快照（正常输入不触发）。判定维持【差异】（既定 panic→Result 形态）。
3. **【浅覆盖补全：Evaluate 的 LegalizeTransform 角距公式与 LbfEvaluator 不同源】** 主文引用的 LegalizeTransform 是 LbfEvaluator.cpp:31-87（`ShortestRotationDistance` = 归一化+min(d,2π-d) 循环最短角距，LbfEvaluator.cpp:25-29）。现场确认 **SeparationEvaluator.cpp 自带一份不同的 LegalizeTransform**（:15-58）：其 Discrete 分支用 `std::abs(result.rotationRad - allowed)` **朴素绝对差**（非归一化、非最短角距）——对跨 ±π 的离散角集（如 {350°, 10°}）会选错最近角，与 LbfEvaluator 版和 Rust 版（normalize_rotation 非循环）**都不同**。三处 Legalize 语义对照：Rust `convert_sample_to_closest_feasible`（归一化到 [0,2π) 后 abs，跨边界角不取环距）≈ LbfEvaluator 版（取环距）；SeparationEvaluator 版（无归一化朴素差，跨 ±π 且角度未归一时偏离最大）。**新发现差异（判定：差异/边界）**：正常调用链（采样器/CD 产出的角度已归一且角集常规）三版同选；非常规角集 + 未归一角度时 SeparationEvaluator 版偏差最大。影响低（S06 差异 #5 的既有分析已涵盖 Legalize 边界类差异的同类风险），此处补记防止后续维护者误以为两处 Legalize 同源。
4. **【核对确认：主文行号基线】** 6910145 向本文件增 4 行，当前 216 行（主文头部"212 行"）；主文引用行号（:63-114/:126-195）整体 +0~+4 偏移仍在有效范围。
