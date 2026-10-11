# S12. collision_loss（碰撞损失流式评估）+ Quantify 权重接线 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/eval/collision_loss.rs`（97 行）+ 权重入口 `quantify/mod.rs`（quantify_collision_poly_poly / quantify_collision_poly_container）+ `quantify/tracker.rs` 的 get_pair_weight/get_container_weight
**C++**: `SparrowCore/Include/SparrowCore/Optimizer/LbfLoss.h`（仅 LBF 损失）+ `Source/Quantify/CollisionTracker.cpp`（`EvaluateCandidateHazardLoss` 逐 hazard 损失 helper；另含两个整项 `EvaluateCandidateLoss` API）+ `Source/Quantify/OverlapProxy.cpp`（碰撞量化公式）+ `Source/Eval/SeparationEvaluator.cpp`（CDE 回调、上界累加与早停）
**判定**: 标量默认 feature 的已测路径等价（多 hazard 顺序、逐 hazard 早停、Exterior 损失与上界相邻浮点值均有 Rust/C++ reference；Hole/QZone 还新增一个 SeparatorWorker 搜索调用链用例，外层多 worker/listener/phase 与 opt-in SIMD 仍未认证）

归属说明：`LbfLoss.h/.cpp` 只含 LBF 的 `xMax*10+yMax` 损失与 bbox 收紧，不含碰撞损失。SparrowCore 分离评估的实际 Rust 对应路径是 `SeparationEvaluator::Evaluate` 逐个调用 `CollisionTracker::EvaluateCandidateHazardLoss`，并将每项与 pair/container weight 相乘；公式位于 `OverlapProxy.cpp`。公开 `EvaluateCandidateLoss` 重载仍供其他调用方使用，但它们不是分离评估器当前的逐 hazard 主路径。

## 函数逐条比对

### `CollisionLossEvaluator::new`（collision_loss.rs:25-35）
- C++ 对应：无独立结构。Rust 辅助状态内联于 `SeparationEvaluator::Evaluate` 的累计变量和 `EvaluateCandidateHazardLoss` 的逐项公式。
- 判定：【一致】（内联化；Rust 该结构本身是 pub(super) 私有辅助）
- 证据：collision_loss.rs:14-22 vs SeparationEvaluator.h:32-42、CollisionTracker.cpp:449-488。

### `reload`（collision_loss.rs:37-42）
- Rust：每次 evaluate 前重置 loss 和 bound；C++ 在 `SeparationEvaluator::Evaluate` 中为每个候选重新初始化累计值，并经 `EvaluateCandidateHazardLoss` 逐 hazard 加和。`simd` 时 Rust 还装载 poles SoA；C++ 没有该 Rust bounded-SIMD 路径。
- 判定：【一致】（逐候选状态重置一致；上界与 loss 在逐 hazard 路径中联动）
- 证据：`collision_loss.rs:37-42` vs `SeparationEvaluator.cpp` 的 `Evaluate` 与 `CollisionTracker.cpp` 的 `EvaluateCandidateHazardLoss`。

### `add`（collision_loss.rs:44-56）— 逐 hazard 累加 + 短路
- C++ 对应：`SeparationEvaluator::Evaluate` 在 CDE 遍历回调中逐项调用 `CollisionTracker::EvaluateCandidateHazardLoss`，按 Rust 的剩余预算和累计上界短路；不再全量收集后统一计算。
- 语义核对：
  1. **每项贡献**：Rust PlacedItem → `quantify_collision_poly_poly(other_shape, shape) * pair_weight`（:86-88），Exterior → `quantify_collision_poly_container(shape, container_bbox) * container_weight`（:90-93）。C++ helper 对应使用 `CalculatePolygonCollisionLoss(otherShape, transformedCandidate) * PairWeight(...)` 或 `CalculateContainerPenalty(...) * ContainerWeight(...)`。
  2. **逐 hazard 短路**：PlacedItem 单项 loss 大于剩余预算时，C++ 回调在累计前返回 stop；其他项加入累计后，以严格 `accumulatedLoss > lossBound` 决定是否停止。三项 hazard 的当前 fixture 揭示一个重要浮点边界：无界累计结果为 `0x4ab5c931`，上界恰为该值时 Rust 返回 `Invalid`，上界为下一相邻 `f32`（`0x4ab5c932`）时返回 `Collision(0x4ab5c931)`。原因是后续 hazard 使用 `lossBound - accumulatedLoss` 形成单项剩余预算，舍入后可能小于该项 loss；C++ 在 Debug/Release 对该夹具复现相同分类。Clear bound 下的零损失碰撞仍分类为 `Collision(0)`。
  3. **累加次序**：CDE 的 callback discovery order 决定 f32 累加次序。三项 PlacedItem fixture 的 Rust 顺序 `[0,1,2]` 得到 `0x4ab5c931`，逆序得到 `0x4ab5c932`；C++实际 evaluator raw bits 匹配 Rust 顺序。该证明只覆盖这组权重和几何。
  4. **`unimplemented!` 分支**（Rust :94，非 PlacedItem/Exterior hazard）：Rust 对 Hole 与 InferiorQualityZone 都 panic。质量区由 jagua-rs CDE 正常注册；导入时 quality=0 的 container holes 会合并进 zone 并变成 Hole（`jagua-rs/src/io/import.rs:175-194`; `entities/container.rs:114-123`），不是被 Rust 上游过滤。C++ 现不再过滤 Hole；对 Hole 和 InferiorQualityZone，`EvaluateCandidateHazardLoss` 均抛出 `std::logic_error("unsupported hazard entity")`，不再忽略或作为零损失碰撞分类。Rust `collision_loss_unsupported_hazard_reference.rs` 对真实导入布局分别断言两类 hazard 触发 panic；C++ `TestUnsupportedContainerHazardsMatchRustPanic` 对应断言异常类型和消息。新增 worker 搜索用例再以实际随机候选触发 InferiorQualityZone，逐项核对异常冒泡、RNG-after、布局/Tracker 不变和候选回调未触发。故已关闭静默忽略差异和该 worker 搜索调用链差异，语言级 panic/exception 机制仍不同；多 worker 外层与 optimizer listener/phase 传播仍开放。
- 判定：【PlacedItem 主路径及 Hole/InferiorQualityZone 不支持输入均有定向 Rust/C++ reference；异常机制按各语言表达】。
- 证据：`collision_loss.rs:44-56,62-96`；`CollisionTracker.cpp` 的 `EvaluateCandidateHazardLoss`；`SeparationEvaluator.cpp` 的 `Evaluate`；`collision_loss_stream_reference.rs`、`collision_loss_unsupported_hazard_reference.rs`、`TestQuantify.cpp`。

### `calc_weighted_loss_bounded`（collision_loss.rs:62-96）— 量化公式
- C++ 对应：`CalculatePolygonCollisionLoss`（OverlapProxy.cpp:137-187）与 `CalculateContainerPenalty`（cpp:185-246）。逐公式核对：
  1. **poly-poly**（Rust quantify_collision_poly_poly，mod.rs:15-24）：`epsilon = max(d1,d2) * 0.01`（OVERLAP_PROXY_EPSILON_DIAM_RATIO，consts.rs:7）；`overlap_proxy = overlap_area_proxy(sur1, sur2, eps) + eps²`；`penalty = sqrt(sqrt(hull1) * sqrt(hull2))`（calc_shape_penalty，mod.rs:26-30：`p1=sqrt(ch_area1), p2=sqrt(ch_area2), (p1*p2).sqrt()`）；`loss = sqrt(overlap_proxy) * penalty`。C++ CalculatePolygonCollisionLoss：epsilonDiameterRatio=0.01（cpp:159）✓；`rawProxy = CalculateOverlapProxy(...Identity,Identity,eps)`、`overlapProxy = rawProxy + eps*eps`（cpp:170-171）✓；ShapePenalty = `sqrt(sqrt(a1)*sqrt(a2))`（cpp:98-100）✓；`sqrt(overlapProxy) * shapePenalty`（cpp:180）✓。**注意**：C++ 传给 CalculateOverlapProxy 的是两个 **Identity 变换**（cpp:163-169）——因为两个形状都已变换到世界系（transformed 与 placed 的 CollisionShape），Rust 的 overlap_area_proxy 收到的同样是变换后的 SPolygon 的 surrogate（:86 `quantify_collision_poly_poly(other_shape, shape)`，两形状均为世界系）。等价 ✓。
  2. **overlap_area_proxy**（overlap_proxy.rs:8-27）：遍历所有 pole 对并累加 `pd_decay * min(r1,r2)`，最后乘 PI。C++ `CirclePairContribution`（OverlapProxy.cpp:26-70）公式相同。Rust `overlap_area_proxy(other_shape, shape)` 以 `other_shape` 的 poles 为外层、候选 `shape` 为内层；对应的 C++ layout 路径由 `EvaluateCandidateHazardLoss` 调用 `CalculatePolygonCollisionLoss(otherShape, transformedCandidate)`（CollisionTracker.cpp:469-472），参数顺序与 Rust 一致。旧 C++ 调用顺序相反；非对称方形/矩形夹具在 0.137 rad 下曾得到 Rust `0x32560315`、旧 C++ `0x32560316`（差 1 ULP）。改为 Rust 参数顺序后，该 pair 的 11 个测试旋转角均逐位相同。此结论限于 layout poly-poly 路径及该单 pair 夹具；三项 hazard 的指定累加顺序另由 `collision_loss_stream_reference.rs` 覆盖，C++ tracker-only 重载仍是额外 API。
  3. **poly-container**（Rust quantify_collision_poly_container，mod.rs:36-55）：有交集 → `(shape_area - inter_area) + 0.0001*shape_area`；无交集 → `shape_area + centroid 距离`；`loss = 2.0 * sqrt(overlap) * penalty(s,s)`。C++ CalculateContainerPenalty（cpp:185-245）：交集条件 `intersectionXMin < intersectionXMax && ...`（严格小于，对应 Rust `Rect::intersection` 的 Some/None——jagua-rs intersection 在退化零面积时返回 None？经核验 Rect::intersection 对零宽/高返回 None，即严格正交集，与 C++ 严格 < 一致）✓；`(shapeArea - intersectionArea) + 0.0001F * shapeArea`（cpp:221-223）✓；无交集 `shapeArea + RectCentroidDistance`（cpp:225-227，中心距离在 f32 上算 ✓）；`2.0F * sqrt(overlap) * shapePenalty`（cpp:239）✓。
  4. **精度路径与累加序**：碰撞量化和逐 hazard loss 的计算使用 Rust 对应的 f32 表达式；JaguarCore 几何对象的坐标/变换存储仍含 double 字段，不能据计算中间量就概括整个 geometry 层为 f32。单 pair 非对称 fixture 的 11 个旋转候选逐位一致，三项 hazard 指定次序的加权总损失逐位一致；Exterior fixture 的完整容器惩罚也逐位为 `0x391152f2`。普通 trace 的 `loss_f32_bits` 会转为浮点后按容差比较，不能当作位断言；这些 raw-bit 结果仍不覆盖任意 geometry、所有 pole 集合或所有 CDE hazard 次序。
  5. **C++ 额外的退化防御**：处处 isfinite/<=0 检查返回 0（Rust debug_assert is_normal + 直接算）。【C++ 额外】
- 判定：【一致】（公式逐字对应；防御差异另计）
- 证据：collision_loss.rs:62-96 + quantify/mod.rs:15-55 + overlap_proxy.rs:8-27 vs OverlapProxy.cpp:26-70,98-100,108-131,137-187,185-246；`SeparatorSearchRustTrace.tsv` 的 `loss_f32_bits` 会被读取为浮点值并按测试容差比较，不是逐位比较。

### 权重接线 `get_pair_weight` / `get_container_weight`（tracker.rs，经 collision_loss.rs:71,92 调用）
- C++ 对应：`CollisionTracker::PairWeight(first, second)` 与 `ContainerWeight(index)`；当前主路径由 `EvaluateCandidateHazardLoss` 为每个 PlacedItem/Exterior 贡献乘相应权重，再交给 `SeparationEvaluator::Evaluate` callback 累加。tracker 和权重更新公式本身属 S18。
- 判定：【一致】
- 证据：`collision_loss.rs:70-71,90-92`、`CollisionTracker.cpp` 的 `EvaluateCandidateHazardLoss` 与 `SeparationEvaluator.cpp` 的 `Evaluate`。

### `EvaluateCandidateLoss` 的两个整项 API
- Rust 的 separation 主路径是 CDE 遍历 + `CollisionLossEvaluator::add` 的逐 hazard callback；C++ 对应 `SeparationEvaluator::Evaluate` + `EvaluateCandidateHazardLoss`。
- C++ 仍公开两个整项 API：(a) tracker-only overload 遍历其他件并算 overlap proxy；(b) layout overload 先收集再对已发现 hazard 求整项损失。两者是额外接口，不再是 `SeparationEvaluator` 当前使用的实现路径。
- 判定：【C++ 额外】（接口仍可用；Sparrow 分离主路径走逐 hazard Rust 对应）。
- 证据：`CollisionTracker.cpp` 的两个 `EvaluateCandidateLoss` overload 与 `EvaluateCandidateHazardLoss`；`SeparationEvaluator.cpp` 的 `Evaluate`。

### SIMD 有界变体（collision_loss.rs:73-82，`quantify_collision_poly_poly_simd_bounded`）
- C++ 对应：`CalculatePolygonCollisionLoss` 无 SIMD、无 `max_loss / weight` 的预除界（SIMD 通道见 S19——C++ 的 SoA/AVX2 是双精度独立通道，不是本函数的位等价替换）。即 Rust simd feature 下"先除权重再界后乘回"的有界短路路径在 C++ 侧无对应（C++ 默认对应 Rust 的 `#[cfg(not(simd))]` 标量路径，:84-88）。
- 判定：【C++ 缺失】（simd 特性路径；标量路径等价，Rust 默认构建也是标量）
- 证据：collision_loss.rs:73-88 vs OverlapProxy.cpp:137-187。

### `LbfLoss.h/.cpp`（任务指定核对项）
- 内容核验：`LbfLoss` = {xMax, yMax}，`FromBoundingBox`（取 bbox.xMax/yMax）、`Cost() = xMax*10 + yMax`（LbfLoss.cpp:25，X_MULTIPLIER=10 同 lbf_evaluator.rs:9）、`TightenSampleBBox`（Cost/10 收紧采样盒 xMax）、`operator<`（NaN 处理后比 Cost）。**不含任何碰撞损失**。它与 Rust `X_MULTIPLIER/Y_MULTIPLIER` 的对应已在 S09 记录（lbf_evaluator.rs:9-10）。TightenSampleBBox 的 Rust 对应在 S03（lbf.rs 的 bbox 收紧）。
- 判定：【一致】（归属确认：LBF 损失公式对应 lbf_evaluator.rs:48 的 10x+y；碰撞损失不在该文件）
- 证据：LbfLoss.cpp:14-56 vs lbf_evaluator.rs:9-10,46-49。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 流式短路与多 hazard 累加顺序：三项 PlacedItem fixture 的顺序 raw bits 为 `0x4ab5c931`，逆序 Rust 为 `0x4ab5c932`；bound=`0x4ab5c930/0x4ab5c931` 均返回 Invalid，bound=`0x4ab5c932` 返回 Collision，C++ 分类一致 | 已按固定 fixture 验证 | 不证明其他几何、所有 CDE 顺序及更宽的上界区间 | `collision_loss.rs:44-56`; `EvaluateCandidateHazardLoss`; `collision_loss_stream_reference.rs`; `TestQuantify.cpp` |
| 2 | SIMD 有界变体（`max_loss/weight` 预除 + bounded SIMD） | C++ 缺失 | Rust 默认构建同用标量路径，等价；simd feature 不对齐 | collision_loss.rs:73-82 vs OverlapProxy.cpp:137-187 |
| 3 | Hole：Rust 在损失分派中 panic；C++ 对应分派抛 `std::logic_error("unsupported hazard entity")`，Tracker 初始化与 moved-row 更新也进入同类失败通道 | 核心分派、Tracker 失败状态和 worker 搜索候选传播有 Rust/C++ reference；该新搜索夹具针对 QZone，Hole 的候选分派仍是直接 evaluator 路径 | worker 搜索调用链已证明异常冒泡、随机状态与布局/Tracker 不变、失败候选无 trace callback；多 worker 外层与 listener/phase 仍未逐入口认证 | `collision_loss.rs:68-95`; `container.rs:114-123`; `import.rs:175-194`; `SeparatorWorker.cpp`; `SeparatorWorkerTrace.h`; `collision_loss_unsupported_hazard_reference.rs`; `TestSeparator.cpp` |
| 4 | InferiorQualityZone：Rust 在损失分派中 panic；C++ 抛 `std::logic_error("unsupported hazard entity")`，Tracker 初始化同样失败 | 核心分派、Tracker 初始化和一条实际 worker 搜索调用链均按 Rust reference 验证 | Rust panic 与 C++ exception 在该调用链均中断搜索；外层多 worker/listener/phase 传播仍未完全闭合 | `collision_loss.rs:68-95`; `SeparationEvaluator.cpp`; `SeparatorWorker.cpp`; unsupported-hazard references; `TestSeparator.cpp` |
| 5 | 两个 `EvaluateCandidateLoss` 整项重载（tracker-only 与 layout） | C++ 额外 | 作为独立 API 保留；`SeparationEvaluator` 当前使用逐 hazard helper，不使用这两个重载 | `CollisionTracker.cpp`; `SeparationEvaluator.cpp` |
| 6 | 量化公式退化防御（isnormal 断言 → isfinite/0 检查返回 0） | C++ 额外 | panic→0 化；正常数据不触发 | mod.rs:24,53 + overlap_proxy.rs:24 vs OverlapProxy.cpp 各处 return 0.0F |
| 7 | layout poly-poly pole 参数顺序、默认 importer 多 pole 几何、三项 hazard 固定次序及 Exterior 容器损失 | 已修复并由 Rust/C++ Debug/Release references 覆盖 | 只证明列出的几何、权重和发现顺序；其他 pole 集合与 hazard kind 仍未验证 | `overlap_proxy.rs:8-27`; `collision_loss_floor_reference.rs`; `collision_loss_stream_reference.rs`; `CollisionTracker.cpp`; `TestQuantify.cpp` |
| 8 | 候选 loss 下限：旧 `std::max(loss, 1e-6)` 已删除，当前返回逐 hazard 累计原值 | 已修复（Rust 原值语义） | 11 个 sub-micro 候选逐位匹配；多 hazard case 另验证累计 raw bits | `SeparationEvaluator.cpp`; `sep_evaluator.rs:92-93`; `collision_loss_floor_reference.rs`; `collision_loss_stream_reference.rs`; `TestQuantify.cpp` |

## 结论

碰撞损失公式与权重接线在源码层面对应，且 C++ 分离评估器已用 CDE callback 实现 Rust 的逐 hazard 加和和上界早停。非对称方形/矩形 fixture 的 11 个 sub-micro 候选位模式一致；三项 PlacedItem hazard fixture 的指定发现顺序 raw loss 为 `0x4ab5c931`，逆序 Rust 累加为 `0x4ab5c932`，C++ 按相同顺序匹配；Exterior 容器损失 raw bits 为 `0x391152f2`。上界邻域也已测明：bound 与无界累计结果相等仍返回 Invalid，增加 1 ULP 后恢复 Collision，Rust/C++ Debug/Release 一致。Clear 上界下零损失碰撞均为 `Collision(0)`。Hole 与 InferiorQualityZone 的损失分派、Tracker 初始化/更新，以及单个 SeparatorWorker 搜索候选的异常传播进入 Rust panic、C++ `std::logic_error` 对应失败通道；worker fixture 还证明只消耗首个候选的 RNG、布局/Tracker 不变且不发出失败候选 callback。多 worker 外层和 listener/phase 真实传播仍开放。其他几何/pole 集合和 opt-in SIMD 仍未认证。`LbfLoss.h` 只包含 LBF 目标函数（10x+y），不包含碰撞损失。

### 实现与回归验证（2026-10-09）
1. **Exterior reference**：在 `CollisionLossFloorRust.json` 几何上按 Rust 顺序先设 base strip width=1.0，再创建问题；导入器的面积推导初始宽度无法构造该窄件夹具的 CDE POI，因此 C++ reference 显式重建同一 Items/Demands/Strip 后建问题。放置位置与候选 transform 对齐，Rust/C++ `SeparationEvaluator` 均分类为 Exterior collision，loss raw bits 均为 `0x391152f2`。
2. **累计上界相邻值**：三项 hazard unbounded loss 为 `0x4ab5c931`。Rust Debug/Release 与 C++ Debug/Release 对 `next_down(total)` 和 `total` 返回 Invalid；对 `next_up(total)` 返回 Collision，损失仍为 `0x4ab5c931`。这记录了逐项剩余预算扣减的浮点行为，不将其简化成“等于总损失即可接受”。
3. **回归范围**：RustReference 全量 Debug/Release 与 SparrowCore 全量 Debug/Release 在本轮均通过；对应 test names 为 `exterior_hazard_loss_matches_rust_container_quantification` 与 `collision_upper_bound_matches_adjacent_float_neighbors`。该验证不覆盖 listener/worker/phase 捕获边界。
4. **worker 搜索失败传播**：`unsupported_hazard_unwinds_from_worker_search_before_candidate_callback` 与 C++ `TestUnsupportedHazardPropagatesThroughWorkerSearch` 使用 seed=42，让首个容器样本命中由该样本 bbox 构造的 InferiorQualityZone；当前参考放置先评估为 Clear 并正常发出 Evaluation/Report，随后失败候选在 SeparationEvaluator 分派处 Rust panic/C++ `logic_error`。两侧 RNG-after 均等于只采样这一个候选后的状态；失败候选没有 Evaluation/Report/Result callback，布局和 Tracker 不变。RustReference 当前 Debug/Release、SparrowCore Debug/Release 全量 runner 均通过。此项只覆盖单 worker 搜索，不代表 `Separator::Run` 的并发任务、外层 listener 或阶段捕获已闭合。

### 补充审计（2026-09-29）
以下为 2026-09-29 时点的审计补记；其中关于“全量收集后事后比较”的表述已被 2026-10-06 的逐 hazard 实现取代，现行结论以本文主文、差异表和当日实现验证为准。

1. **【遗漏补记：`reload` 的 simd 分支内容】** 主文 reload 条目写"（simd 时）装载 poles SoA"但未展开。现场确认：collision_loss.rs:41 `poles_soa.load(&_shape.surrogate().poles)` 把**候选形状（被评估的移动件候选变换）**的 pole 装入 SoA（p2 方向），与标量路径的 `calc_weighted_loss_bounded`（:73-88 的 `quantify_collision_poly_poly_simd_bounded(other_shape, shape, &poles_soa, max_loss/weight)` 预除界）共同构成 simd feature 的有界短路链——C++ 无对应（归 S19 差异 #2，判定不变），此处补记函数体细节以闭合 reload 条目。
2. **【遗漏补记：`add` 的 remaining 计算】** Rust 对无界上界保持 `remaining=INFINITY`；当前 C++ 同样以正无穷作无界 sentinel，并在 callback 的 PlacedItem 单项比较/累计比较中避免将其误当有限预算。等号不触发 stop，超过才 stop。该控制流现位于 `SeparationEvaluator::Evaluate`，不再是事后全量比较。判定：【实现已按当前路径对齐】。
3. **【浅覆盖补全：calc_weighted_loss_bounded 的 Exterior 分支单项语义】** Rust Exterior 分支生成完整的容器惩罚乘 weight，不做 PlacedItem 专属的单项剩余预算比较，之后由 `add` 的总累计检查早停。当前 C++ `EvaluateCandidateHazardLoss` 也直接返回 Exterior 加权项，由 `SeparationEvaluator::Evaluate` 的累计上界检查决定是否停止；Exterior 专项 parity 仍应补测。
4. **【核对确认：任务指定项 LbfLoss 归属】** 现场再验 LbfLoss.h（21 行）/LbfLoss.cpp（57 行）全文：`FromBoundingBox/TightenSampleBBox/Cost()/operator<`，无碰撞损失代码——主文归属判定维持（碰撞损失链在 OverlapProxy+CollisionTracker）。

### 实现与回归验证（2026-10-06）
1. **pole 参数顺序的 RED/GREEN 证据**：保留 Rust reference 的方形/矩形几何，在 0.137 rad 下，Rust 为 `0x32560315`，旧 C++ layout 调用顺序为 `0x32560316`；将 `CalculatePolygonCollisionLoss` 的实参改为 `(otherShape, transformedCandidate)` 后该断言通过。Rust 与 C++ 随后对 0、0.013、0.137、0.281、0.5235988、0.7853982、1.234、2.345、3.1415927、4.712389、5.999 rad 共 11 个候选逐位相同。
2. **历史 Rust/C++ 回归验证（2026-10-06）**：当日 RustReference 全量 `cargo test --locked` 的 12 个测试函数通过；Rust 两项 multi-hazard 测试核对发现次序、raw loss 和 early-stop；当时新增的 warm-start optimize→explore→compress 参考另见 README。2026-10-07 新增的 worker-move 压缩参考及当前套件 13 项结果记录在 README/S07/S18；本段只记录 10-06 的状态，不代表当前测试总数。以上 fixture 均不覆盖所有配置、几何、终止条件或 cold-start 默认整条 optimize 流程。
