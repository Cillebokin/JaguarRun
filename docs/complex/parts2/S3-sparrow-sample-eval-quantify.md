# sparrow 采样（sample）、评估（eval）与量化（quantify）代码逻辑说明

本文基于仓库 `CodeResource/sparrow` 的 `src/sample/`、`src/eval/`、`src/quantify/` 源码，并对照 `CodeResource/jagua-rs` 核对了碰撞检测接口的语义。除特别说明外，路径相对于 sparrow 仓库根，行号为当前文件中的行号。文中出现的"论文算法编号"均指 `search.rs:190` 注释所引 arXiv:2509.13329 的 Algorithm 编号。

## 一、放置搜索 search_placement 的总体流程

`search_placement`（`src/sample/search.rs:191-208`，实现位于 `231-319`）是整个模块的入口，对应论文 Algorithm 6。它接收一个 `Layout`、一个待放置的 `Item`、可选的参考物品键 `ref_pk`、一个实现了 `SampleEvaluator` 的评估器、`SampleConfig`（容器采样数、聚焦采样数、坐标下降起点数，见 `search.rs:15-20`）以及随机数发生器，返回最优 `(DTransformation, SampleEval)` 与评估次数。

流程分五步。第一步是建立候选集合 `BestSamples`，其容量等于 `n_coord_descents`，唯一性阈值为物品最小包围盒边长乘以 `UNIQUE_SAMPLE_THRESHOLD = 0.05`（`search.rs:240-245`，常量见 `consts.rs:28`）。第二步在 `ref_pk` 存在时（即分离阶段，物品当前位置已知），先把当前位置作为候选直接评估，此次评估使用 `Invalid` 作为上界，即不做早停（`search.rs:253-262`），然后用当前包围盒构造聚焦采样器（`265-266`）。第三步执行聚焦采样（`274-282`）与容器全局采样（`285-293`），两者都调用 `best_samples.upper_bound()` 作为评估上界，并把结果交给 `report`。第四步是粗精修（PreRefine，`298-303`）：对候选集合的一份快照（`samples.clone()`）中的每个起点各运行一次坐标下降，结果再次交给 `report`。第五步是精精修（FinalRefine，`306-309`）：只取粗精修后集合中的最优解再运行一次更细步长的坐标下降，最后返回结果与评估次数（`316-318`）。

LBF 构造阶段调用同一函数，但 `ref_pk` 为 `None`，因此没有聚焦采样，且 `LBFEvaluator` 忽略上界（`lbf.rs:124-142`，`consts.rs:50-54` 中 `LBF_SAMPLE_CONFIG` 为 1000 次容器采样、0 次聚焦采样、3 个起点）。分离阶段的参数来自 `config.rs:64-66` 与 `81-83`，即每个物品 50 次容器采样、25 次聚焦采样、3 个起点。

粗精修与精精修的步长由 `prerefine_cd_config` 与 `final_refine_cd_config` 给出（`search.rs:321-343`）。平移步长的初值与下限分别取物品最小包围盒边长的比例：粗精修为 0.25 与 0.02（`consts.rs:16`），精精修为 0.01 与 0.001（`consts.rs:22`）；旋转步长粗精修为 5° 到 1°（`consts.rs:19`），精精修为 0.5° 到 0.05°（`consts.rs:25`）。只有 `RotationRange::Continuous` 物品才开启 wiggle 轴（`search.rs:323`、`335`）。这种"粗步长筛选多个起点、细步长打磨单个最优点"的两段式设计，是把大部分预算放在采样上，把少量预算用于局部极小化。

观察器（`SearchObserver`、`ObservedEvaluator`，`search.rs:31-99`）与 `oracle-trace` 特性下的回调（`search.rs:101-229`）只读取评估与报告事件，不影响控制流；由于 `NoopSearchObserver` 是单态化的空实现，非追踪构建下开销可忽略。

## 二、候选集合 BestSamples 的保留策略

`BestSamples`（`src/sample/best_samples.rs:340-345`）按评估值升序保存至多 `size` 个样本，并保证两两不相似。`report`（`356-419`）的决策逻辑如下。首先，只有 `eval < upper_bound()` 才可能被接受（`357`）。`upper_bound` 在集合未满时返回 `Invalid`，即任何非 `Invalid` 的评估都可接受；集合满时返回第 `size` 名（最差）的评估值（`421-431`）。其次，若新样本与已有样本都不相似（`dtransfs_are_similar`，见下），且集合已满则淘汰最差者，然后接受（`365-371`）。若存在相似样本，则只有当新样本严格优于所有相似样本时，才删除全部相似样本并接受新样本（`372-396`）。这种"同一邻域只保留一个代表"的策略避免了粗精修起点在同一个局部盆地中重复出现。接受后执行排序（`402`），并在调试构建中断言唯一性（`403-413`，只检查非 `Invalid` 样本）。

相似性判定 `dtransfs_are_similar`（`434-451`）要求平移在 x 与 y 两个方向上各自的绝对差都小于阈值（这是矩形盒判定，不是欧氏距离），并且旋转角对 2π 取模后的差小于 1°。需要注意，取模后的比较不是循环比较：359° 与 1° 会被判为不相似，负角度经 `%` 运算后仍保留负号，因此这一判定对接近 0 与接近 2π 的旋转会失效。对于离散旋转物品这一缺陷影响有限，因为不同离散角之间的差远大于 1°；对连续旋转物品，wiggle 步长可能在 0 与 2π 附近产生被重复保留的近似重复样本（见第十四节）。

`size` 为 0 时 `upper_bound` 中的 `self.size - 1`（`426`）会发生 usize 下溢，因此配置必须保证 `n_coord_descents >= 1`，这一前提未在配置加载处显式校验。

## 三、采样器 UniformBBoxSampler

`UniformBBoxSampler`（`src/sample/uniform_sampler.rs:208-285`）为每个允许的旋转预先计算一个可行的平移矩形。`new`（`224-272`）遍历旋转集合：连续旋转取 16 个等间距角（`208`、`230-232`），离散旋转取给定列表，无旋转则取 0。对每个旋转，它把物品形状旋转后求包围盒 `r_shape_bbox`（`246-248`），再用容器包围盒减去该包围盒的最小、最大角得到"使形状完全位于容器内"的平移区间（`251-254`），再与采样区域的平移区间求交（`257-258`，`297-307`）。交集为空的旋转被丢弃（`268-271`）。

采样（`274-284`）先从可行旋转中均匀选一个（`choose`），再在 x、y 两个区间内各均匀取一个值（`sample_axis`，`289-295`）。退化区间（起止相等）不调用随机数而直接返回端点，注释说明碰撞检测负责判定该候选是否可行。这一点影响随机数消耗顺序，详见第十一节。

容器采样器的区域就是容器包围盒（`search.rs:270-271`），聚焦采样器的区域是参考物品的绝对包围盒 `l.placed_items[ref_pk].shape.bbox`（`search.rs:265-266`）。这里需要注意一个语义细节：采样区域是绝对坐标包围盒，却直接与平移区间求交。由于 jagua 在导入时把物品形状平移到质心为原点（`jagua-rs/src/io/import.rs:262-266` 与 `85` 的 `pre_transform`），平移量近似等于质心位置，因此该近似的含义是"质心落在原位置包围盒内"，对于旋转后形状偏移较大的情况会有系统性偏差，但不会破坏可行性，因为可行性仍由第 `251-258` 行的容器约束保证。另外，`convert_sample_to_closest_feasible`（`310-330`）只在 `optimizer/explore.rs:413-500` 的交换与修复路径中使用，不属于本模块的主流程。

## 四、坐标下降 refine_coord_desc

`refine_coord_desc`（`src/sample/coord_descent.rs:24-73`）以一个已评估样本为起点，循环调用 `CoordinateDescent::ask` 生成候选，评估后用 `tell` 反馈。每轮生成两个候选（`ask`，`97-123`），候选之间的关系取决于当前轴 `CDAxis`：水平轴为 `(±sx, 0)`，竖直轴为 `(0, ±sy)`，两条对角线分别为 `(+sx,+sy),(-sx,-sy)` 与 `(-sx,+sy),(+sx,-sy)`，wiggle 轴只改变旋转 `r±sr`（`111-117`）。停止条件（`101-106`）是两个平移步长都小于 `t_step_limit`，并且旋转步长已小于 `r_step_limit` 或物品不允许 wiggle。

每轮评估时，两个候选都使用当前点的评估值 `cd.eval` 作为上界（`48`），这意味着明显差于当前点的候选会被评估器提前截断为 `Invalid`，从而节省计算。两个候选的较优者由 `min_by_key` 选出（`50-54`），在评估值相等时 `min_by_key` 返回先出现的候选，即正方向或 `+` 候选，这一平局规则会影响可复现性。选出的候选交给 `tell`（`57`）。调试构建下会断言单次下降的评估次数不超过 1000（`59-62`），发布构建不做此检查。

`tell`（`126-162`）的规则如下。若候选严格优于当前点则接受并以 `CD_STEP_SUCCESS = 1.1` 放大当前轴步长（`128-142`，`consts.rs:10`）；若两者相等则也接受位置（因为 `!worse`，见 `132-135`），但步长按失败处理，乘以 `CD_STEP_FAIL = 0.5`（`consts.rs:13`）；若候选更差则位置不变，同样步长减半。对角轴的步长以 `sqrt(m)` 同时调整两个分量（`148-152`），旋转轴只调整 `r_step`（`153-155`）。只要本次不是严格改进，就用 `CDAxis::random` 重新抽取下一个轴（`159-161`）。因此算法更像带随机方向的模式搜索，而不是固定顺序的坐标轮换。`CDAxis::random`（`180-194`）在允许 wiggle 时从 0..6 中抽取，其中 4 与 5 映射为 wiggle，所以 wiggle 的概率为 1/3，否则从 0..4 抽取。

这里有一个值得注意的特点：成功时步长放大而不是保持，因此步长序列并非单调递减，终止性依赖于"失败次数最终占优"与评估值的有限性。在实践中这不会成为死循环，但它说明单纯看步长上下限无法精确预测评估次数。另外，相等评估被当作"接受的侧移"，在平台区域（例如分离阶段的 `Clear { loss: 0.0 }`）中，坐标下降会在无改进的情况下持续消耗评估次数，直到步长降到下限。

## 五、SampleEval 三态及排序

`SampleEval`（`src/eval/sample_eval.rs:7-17`）有三种状态。`Clear { loss }` 表示没有碰撞，`loss` 的含义由评估器决定；`Collision { loss }` 表示有碰撞，`loss` 为碰撞代价；`Invalid` 表示样本被丢弃。`Ord` 实现（`25-37`）规定：`Invalid` 最大；`Clear` 小于 `Collision`，即任何无碰撞样本都优于有碰撞样本；同类之间按 `loss` 比较，比较经由 `FPA`（`jagua-rs/src/util/fpa.rs:7-31`）进行近似相等判断，近似相等被视为 `Equal`。

需要特别注意两点。第一，`Invalid` 在两个评估器中含义不同。在 `LBFEvaluator` 中，`Invalid` 确实表示任何碰撞（`lbf_evaluator.rs:195`、`199`）；在 `SeparationEvaluator` 中，`Invalid` 则主要表示"代价超过上界而被早停"（`sep_evaluator.rs:131`、`137`、`111-115` 与 `collision_loss.rs:260-272`），它并不意味着几何上无效。因此 `Invalid` 不能用作"碰撞判定"的依据，只能说明"不优于上界"。第二，`#[derive(PartialEq)]`（`sample_eval.rs:7`）使用精确的 f32 比较，而 `Ord` 使用 FPA 的近似相等，二者不一致，`Eq` 的手工实现（`39`）因此并不严格成立。这在 `BestSamples` 的 `*eval != SampleEval::Invalid`（`best_samples.rs:406`）中无害，但不宜在其他地方混用 `==` 与 `cmp`。

## 六、评估器：SeparationEvaluator 与 LBFEvaluator

分离阶段的评估器 `SeparationEvaluator`（`src/eval/sep_evaluator.rs:61-152`）在构造时记录当前物品的 hazard 键（`72-85`），并把 `CollisionLossEvaluator` 绑定到当前物品键与碰撞追踪器（`92`）。`evaluate_sample`（`102-147`）的步骤如下。它首先根据上界确定损失上界 `loss_bound`：`Collision{loss}` 对应 `loss`，`Clear` 对应 0，其他情况为无穷大（`111-115`）。随后把候选变换应用到形状缓冲区（`116-118`），清空收集器，并把当前物品自身的 hazard 预先插入收集器，使遍历跳过它（`119-123`）。`n_ignored_hazards` 记录此时收集器的大小，即忽略集的大小（`123`）。

检测分两阶段。先执行代理检测 `collect_surrogate_collisions_until`（`126-128`），它只遍历形状代理的 fail-fast 极圆前缀（jagua `cd_engine.rs:343-364`，`sp_surrogate.rs:76-78`；sparrow 中 `config.rs:92` 的 `ff_pole_area_ratio = 0.5` 决定前缀覆盖一半多边形面积）。若代理检测在回调 `should_stop` 的作用下提前停止，则结果为 `Invalid`（`131`）。否则再执行精确检测 `collect_poly_collisions_until`（`133-134`）。精确检测会跳过已在收集器中的 hazard，因此每个 hazard 的代价只计算一次。最终判定为：若精确检测也被截断则为 `Invalid`；若收集器大小仍等于忽略集大小，则没有碰撞，返回 `Clear { loss: 0.0 }`（`136-139`）；否则返回 `Collision { loss: self.loss_evaluator.loss() }`（`141-143`）。

回调 `should_stop` 实际调用 `CollisionLossEvaluator::add`（`collision_loss.rs:260-272`），它为每个新发现的 hazard 计算加权代价并累加。当累计代价超过上界时返回 `true`，即请求停止遍历。`calc_weighted_loss_bounded`（`278-312`）对两类 hazard 给出代价。对于其他物品 `PlacedItem`，权重来自碰撞追踪器的成对权重 `get_pair_weight(current_pk, other_pk)`（`287`），代价为两个形状的重叠代理乘以该权重（非 SIMD 路径为 `300-304`，SIMD 路径为 `289-298`）。对于容器 `Exterior`，代价为 `quantify_collision_poly_container` 乘以物品的容器权重（`306-309`）。这意味着分离代价的定义是：候选位置处，该物品与所有当前已放置物品及容器碰撞的加权代理之和，其中忽略物品自身的旧位置。

这里有一个重要的语义结论：代理阶段与精确阶段是"同一集合的两次收集"，最终的 `loss` 对所有被收集的 hazard 都使用完整极圆集合计算，因此代理阶段只改变收集的先后顺序和早停时机，不改变最终代价的定义。按 jagua 的设计意图，极圆取自多边形内部，因此代理阶段命中的 hazard 应为真实重叠，不会造成误报（这一点未在本文中逐行核实）。

`LBFEvaluator`（`src/eval/lbf_evaluator.rs:167-216`）是构造阶段的评估器。它先用代理检测（`194`），再用精确检测（`198`），任何碰撞均返回 `Invalid`，无碰撞时返回 `Clear { loss }`，其中 `loss = 10 * (poi.x + bbox.x_max) + (poi.y + bbox.y_max)`（`204-205`，`X_MULTIPLIER = 10`、`Y_MULTIPLIER = 1` 见 `161-162`）。这里 `Rect::corners()[0]` 实际是右上角 `(x_max, y_max)`（`jagua-rs/src/geometry/primitives/rect.rs:161-168`），因此 `loss` 是对右上角与重心坐标的线性加权，越靠左下越小。注释所说的"左下角"是语义上的，其数值来源是右上角，这一点读代码时需要留意。LBF 评估器忽略上界 `_upper_bound`（`189`），其 `loss` 与上界无关。

## 七、CollisionTracker：物品对损失、GLS 权重与快照

`CollisionTracker`（`src/quantify/tracker.rs:13-19`）维护三类信息：每个物品的 `pk_idx_map`（`PItemKey` 到矩阵下标的映射）、成对的 `PairMatrix`（同时保存损失与权重），以及每个物品与容器的 `container_collisions`。初始化（`24-54`）时所有权重为 1.0，并对全部物品调用 `recompute_loss_for_item`（`47-49`）。

`recompute_loss_for_item`（`56-92`）先把该物品对应的一整行成对损失清零（`62-64`）并清零容器损失（`65`），然后用 CDE 的精确检测收集所有与之碰撞的 hazard，并删除自身（`68-71`）。对每个碰撞的其他物品，计算 `quantify_collision_poly_poly` 并写入成对矩阵（`74-83`）；对容器碰撞则写入 `container_collisions`（`84-88`）。`assert!(loss > 0.0)`（`81`、`86`）保证了碰撞项的损失为正。由于 `PairMatrix` 是对称存储，同一个条目同时代表 (i,j) 与 (j,i)，因此重算一个物品会同时更新与其碰撞的对方。这种"后写覆盖"语义在 CDE 检测不对称时可能产生漂移，`src/util/assertions.rs:48-50` 中的注释已承认这一点，并以容差与告警处理。

`register_item_move`（`113-121`）在物品从 `old_pk` 移到 `new_pk` 时把下标转移到新键，并重算新位置的损失。`get_loss`（`179-187`）返回物品的未加权总损失（一行求和加容器），`get_weighted_loss`（`189-197`）返回加权和。全局的 `get_total_loss` 与 `get_total_weighted_loss`（`199-231`）遍历整个矩阵。

GLS 权重更新 `update_weights`（`124-155`）对应论文 Algorithm 8。它先在全部成对条目与容器条目上求最大损失 `max_loss`（`126-132`）。随后对每个条目：若损失为 0，则权重乘以衰减系数 `GLS_WEIGHT_DECAY = 0.95`（`141-145`，`consts.rs:6`）；否则乘以 `1.2 + (2.0 - 1.2) * loss / max_loss`（`146-151`，`consts.rs:4-5`）；最后 `weight = max(weight * multiplier, 1.0)`（`153`）。因此权重下界为 1，衰减回到 1 的过程是指数的，增长是按相对严重程度的。注意这里没有上界。

快照机制由 `save`（`109-111`）与 `restore_but_keep_weights`（`94-107`）组成。`save` 直接克隆整个 `CollisionTracker`，包括权重；`restore_but_keep_weights` 只恢复 `pk_idx_map` 与各条目的损失，保留当前的权重。这样回滚时不会丢失 GLS 的学习历史，这是分离过程的关键设计：权重代表跨多次迭代与多次应变的"难度记忆"。`separator.rs:80`、`144`、`159` 在每次找到更优解时保存快照，`separator.rs:179` 的 `rollback` 在每个应变结束时回滚到最优快照，`250-264` 说明在没有快照的路径中则直接重建追踪器（权重重置）。

## 八、重叠代理 OverlapProxy 与容器损失

成对碰撞损失的核心是 `quantify_collision_poly_poly`（`src/quantify/mod.rs:340-352`）。令 `ε = 0.01 × max(d1, d2)`（`342`，`d` 为形状直径，比例常量见 `consts.rs:7`），则代理重叠面积为 `S = overlap_area_proxy(...) + ε²`（`344-345`），最终损失为 `sqrt(S) × penalty`（`351`）。

`overlap_area_proxy`（`src/quantify/overlap_proxy.rs:306-326`）对两个形状的每对极圆 (p, q) 计算穿透深度 `pd = r_p + r_q - |c_p - c_q|`（`312`），并通过分段函数 `g(pd) = pd`（当 `pd >= ε`）或 `g(pd) = ε² / (2ε - pd)`（当 `pd < ε`，`314-317`）进行平滑。累加 `min(r_p, r_q) × g(pd)`（`319`），最后乘以 π（`322`）。整体公式为：

S_overlap = π · Σ_{p∈P1} Σ_{q∈P2} min(r_p, r_q) · g(pd_pq)，损失 = sqrt(S_overlap + ε²) · penalty。

分段函数在 `pd = ε` 处连续（两端都等于 ε），导数也连续（两端导数都为 1），因此代理对于小幅平移是光滑的，这对基于梯度与坐标下降的搜索有利。`pd` 为很大的负数（极圆相距很远）时 `g` 趋于 0，保证远距离时代价平滑衰减到 0 而非突变。`ε²` 的加入保证了即使极圆没有重叠，只要两个形状被判定为碰撞，代价也严格为正，这是 `assert!(loss > 0.0)` 成立的前提。

`penalty` 由 `calc_shape_penalty`（`354-359`）给出，即两形状凸包面积平方根的几何平均，即 `(sqrt(A1) · sqrt(A2))^(1/2)`。对于两个相同形状，它退化为 `sqrt(A)`，因此大物品之间的碰撞代价更高，单位也与面积一致。

容器碰撞 `quantify_collision_poly_container`（`362-380`）使用的是形状包围盒而非多边形本身。若包围盒与容器包围盒有交（`365`），则以包围盒面积减去交面积再加上 `0.0001 × 包围盒面积` 为重叠量（`368`）；若无交，则以包围盒面积加上两包围盒中心距离作为引导项（`372`）；最终代价为 `2 · sqrt(overlap) · penalty`（`379`）。这种构造使得损失对"超出容器的包围盒面积"敏感，并在完全无交时仍能给出朝向容器的梯度信号。需要注意它对非矩形形状并不精确，因为包围盒面积远大于多边形面积。

## 九、PairMatrix：对称三角存储

`PairMatrix`（`src/quantify/pair_matrix.rs:250-270`）用一维向量存储 `n` 个物品的成对条目，长度为 `n(n+1)/2`，包含对角线。这里的"对称三角"指的是：下标函数 `calc_idx`（`286-299`）对 `row <= col` 与 `row > col` 分别按行优先上三角计算偏移，因此 (i,j) 与 (j,i) 映射到同一条目。文件中的注释（`287-292`）给出了 4 阶的示例，我用其验证公式：当 `row <= col` 时下标为 `row·n + col − row(row+1)/2`，例如 n=4 时 (1,1) 得 4、(2,2) 得 7、(3,3) 得 9，与示例一致。对角线条目始终保持损失为 0（`tracker_matches_layout` 中断言，`util/assertions.rs:20`），它在权重衰减时仍参与遍历，属于少量浪费。

`calc_idx` 只在调试构建中断言 `row < size && col < size`（`293`），发布构建依赖 `Vec` 的越界检查。`pk_idx_map` 的索引（`tracker.rs:158`、`170`）依赖 `SecondaryMap` 的 panic 语义，因此对未注册的键会直接崩溃，不会静默出错。

## 十、SIMD 实现及其前提

SIMD 路径由 Cargo 特性 `simd`（`Cargo.toml:49`）控制，并在 `src/lib.rs:1` 处通过 `feature(portable_simd)` 启用。`portable_simd` 是 nightly 才可用的特性门（按其性质推断，需要 nightly 工具链；仓库中未检查工具链文件）。在特性开启时，`CollisionLossEvaluator` 额外持有一个 `CirclesSoA`（`collision_loss.rs:236-237`、`248-249`），每次 `reload` 时把候选形状的极圆复制进去（`256-257`）；`add` 中的成对代价改由 `quantify_collision_poly_poly_simd_bounded` 计算（`289-298`）。

该函数（`src/quantify/simd/mod.rs:18-46`）的正确性依赖以下前提。第一，`poles2` 必须与第二个形状的极圆一致。调用处传入的是 `(other_shape, shape, &poles_soa)`，而 `poles_soa` 恰好由 `shape` 在同一次评估的 `reload` 中装载（`sep_evaluator.rs:124` 触发 `reload`，`collision_loss.rs:253-258` 装载），因此前提在 `SeparationEvaluator` 中成立。若将来有人复用缓冲而忘记 `reload`，发布构建将静默使用错误极圆，只有调试构建的 `debug_assert`（`overlap_proxy_simd.rs:190-200`）能发现。第二，单调截断的正确性依赖于每一项非负：`pd_decay` 在两个分支中都为正（分母 `2ε − pd` 在 `pd < ε` 时大于 ε），`min_r` 为正，因此部分和单调不减，可以用 `total_overlap > max_unscaled_overlap` 提前返回 `None`（`overlap_proxy_simd.rs:183-185`）。第三，上界换算 `max_unscaled = (M² − ε²)/π`（`mod.rs:28-35`）与标量路径 `S = π·Σ + ε²` 一致，若 `M < ε`（即无法满足下界）则直接返回 `None`（`31-33`）。

SIMD 向量宽度为 4（`overlap_proxy_simd.rs:96`），按 4 个一组处理 `poles2`（`137-162`），不足 4 个的尾部用标量循环处理（`164-181`）。`pd_decay` 用 `simd_ge` 生成掩码后 `select`（`154-156`），两个分支的值都被计算，分母接近 0 的通道在掩码中被丢弃，不会产生异常。`reduce_sum` 的求和顺序与标量不同，因此两条路径的结果不是逐位相等的，调试构建只要求相对误差 1e-3（`190-200`）。`CirclesSoA` 的 `#[repr(align(32))]`（`circles_soa.rs:53`）只影响结构体本身，`from_slice` 使用非对齐加载，因此对齐并非正确性前提。

## 十一、随机数消耗顺序与可复现性

随机数的消耗顺序直接决定可复现性，分为三层。在 `move_items` 中（`worker.rs:34-80`），首先对所有仍有碰撞的物品做一次 `shuffle`（`36-43`），然后按打乱后的顺序逐个处理，每个物品处理前再次检查是否仍有碰撞（`51`）。在 `search_placement` 中，参考位置的评估不消耗随机数；聚焦采样与容器采样每个样本依次消耗：选旋转（`choose`，消耗次数取决于 rand 0.10 的实现，本文未核实）、x 方向一次（退化区间除外，`sample_axis` 不调用随机数）、y 方向一次（同上）。在两段坐标下降中，每次 `refine_coord_desc` 先用 `CDAxis::random` 抽取一次初始轴（`coord_descent.rs:37`），之后每次非严格改进都再抽取一次（`159-161`）。因此坐标下降的随机数消耗量依赖评估结果，即依赖几何与权重，这使得随机数流与整个搜索过程耦合。

工作线程各自持有独立的 `Xoshiro256PlusPlus`，在 `change_strip_width` 中由主随机数种子派生（`separator.rs:332`）。`move_items_multi` 并行运行各工作线程（`separator.rs:209-219`），但每个线程的随机数流只依赖其自身输入，结果汇总时按 `min_by_key(OrderedFloat(total_weighted_loss))` 选择最优者（`236-241`），平局时选先出现者，因此在确定的种子与迭代次数下整体结果可复现。需要注意，`separate` 的终止条件中包含时间限制 `term.kill()`（`107`），时间相关的终止会使迭代次数与最终解随机依赖于运行速度，因此严格复现需要关闭时间限制或以迭代数代替。

由于 `search_placement` 中的早停（上界截断为 `Invalid`）不改变坐标下降的决策（两者在"更优/更差"上一致），早停不会改变随机数流，这一点是可复现性上的正面事实。

## 十二、浮点精度与数值容差

第一，`SampleEval` 的 `Ord` 使用 `FPA`，其相等判断基于 `float_cmp::approx_eq!`（`jagua-rs/src/util/fpa.rs:18-21`），这是带容差的比较。近似相等的关系不一定满足传递性，因此在排序与 `min_by_key` 中可能出现非确定的边界行为。由于容差取值依赖 `float_cmp` 的默认参数（本文未在依赖源码中核实具体数值），在 `loss` 很大时相对容差很小，在 `loss` 很小时绝对容差起主导，两者都可能使"微小改进"被视为平局，从而在坐标下降中被当作侧移。

第二，`update_weights` 的权重没有上界，且每次碰撞迭代都可能乘以最多 2 倍（`tracker.rs:146-153`）。一个持续碰撞的成对条目在约 128 次连续乘以 2 后会超过 f32 的最大值。此后衰减 `inf * 0.95` 仍为 `inf`，而 `get_weighted_loss` 中 `inf * 0.0` 会得到 `NaN`（`189-197`、`216-230`），进而污染总损失与工作线程的 `min_by_key`。权重在一次 `separate` 调用的多次应变之间保留（见第七节），因此理论上可能在长时间运行中累积到溢出。这是一个需要通过计数或上界钳制来防范的风险，目前代码中没有相应检查。

第三，`overlap_area_proxy` 与 `quantify_collision_poly_container` 的 `debug_assert!(is_normal())`（`overlap_proxy.rs:323`、`mod.rs:347`、`375`）要求结果为正规数，即不为 0 也不为次正规数。在正常几何下 `ε²` 保证了前者，但对于极远距离的极圆，`g(pd)` 的值可能很小，极端情况下的次正规数只会导致调试断言失败，而不影响发布构建。

第四，`dtransfs_are_similar` 中的旋转比较使用 `%` 而非循环距离（`best_samples.rs:444-447`），如前所述。第五，分离代价中 `Clear` 恒为 0（`sep_evaluator.rs:139`），因此所有无碰撞样本在 `BestSamples` 中互相"相等"，接受规则要求严格小于，于是采样顺序中最先出现的无碰撞样本胜出，这是一个由采样顺序决定的结果。第六，`collision_loss.rs:262-266` 的剩余预算 `remaining = bound − loss` 与最后的 `self.loss > self.loss_bound` 比较使用的是同一组浮点数，但 SIMD 路径中还要做一次 `max_loss / weight` 与 `* weight` 的换算，在边界处可能与精确比较不一致，只产生微小偏差。

## 十三、性能瓶颈

评估一次候选的主要开销是形状变换与碰撞检测。`sep_evaluator.rs:116-118` 每次都要变换全部顶点与极圆，`collision_loss.rs:256-257`（SIMD）还会复制极圆，而随后的代理检测与精确检测都依赖 CDE 的四叉树遍历。`BestSamples` 的上界截断在这里起到关键作用：当候选集合填满后，上界迅速收紧，大量候选在前几个极圆或前几条边上就被截断为 `Invalid`。

追踪器层面，`get_loss` 与 `get_weighted_loss` 是 O(n) 的行求和，而 `move_items` 对每个物品都调用 `get_loss` 作为过滤条件（`worker.rs:41`），因此每轮的开销是 O(n²)。`get_total_loss` 与 `get_total_weighted_loss` 遍历整个矩阵（O(n²)），在 `separator.rs:110`、`117-118`、`120` 中每轮调用多次。`save` 与 `CollisionTracker` 的克隆也是 O(n²)，而 `move_items_multi` 每轮都执行 `self.prob.save()`（`separator.rs:206`）与 `self.ct = best_ct.clone()`（`245`），因此每次迭代都复制一次完整矩阵。对于数百个物品的算例，这些操作的开销值得在分析时单独测量。

坐标下降在平台区域上浪费评估次数，如上文所述。`UniformBBoxSampler::new` 对每个旋转都要变换一次形状（`uniform_sampler.rs:246-248`），每次 `search_placement` 调用两次，即 O(旋转数 × 顶点数)，相对于数百次评估而言不算大，但可以缓存。SIMD 对极圆数量较少的形状收益有限，因为其内层循环在 `p2` 数量小于 4 时几乎全部落入标量尾部（`overlap_proxy_simd.rs:164-181`）。

## 十四、疑点与待核实事项

第一，`search.rs:265-266` 与 `uniform_sampler.rs:257-258` 把绝对包围盒当作平移区间使用，其语义依赖于"平移 ≈ 质心"的近似，建议团队确认这是有意设计，并在注释中写明。第二，`best_samples.rs:444-447` 的旋转相似性判定不是循环距离，接近 0 与 2π 的样本可能被重复保留；对连续旋转物品建议改用 `normalize_rotation` 后的最小角差。第三，`best_samples.rs:426` 在 `size == 0` 时会下溢，需要在配置层校验。第四，`tracker.rs:146-153` 的权重无上界，存在溢出为 `inf` 并产生 `NaN` 的风险。第五，`sample_eval.rs:7` 的 `PartialEq` 与 `Ord` 不一致，`Eq` 实现（`39`）名不副实。第六，`SeparationEvaluator` 中 `Invalid` 的双重含义会让日志与追踪难以解读，建议在 `SampleEval` 的文档中明确区分"早停"与"几何无效"，或引入独立的 `Pruned` 变体。第七，坐标下降在 `Clear` 平台上继续运行（`search.rs:299-309` 对所有保留样本都运行），可以考虑在评估值已为 `Clear { loss: 0.0 }` 时跳过。第八，`collision_loss.rs:253` 的 `reload` 参数 `_shape` 在非 SIMD 构建中未使用，SIMD 构建中使用，条件编译下的参数命名容易误导阅读者。第九，`poles_overlap_area_proxy_simd_bounded`（`overlap_proxy_simd.rs:164-181`）的标量尾部循环中，第 `167` 行用同名变量 `p2` 遮蔽了参数 `p2`，虽然语义正确，但极易在修改时引入错误。第十，`tracker.rs` 中对称矩阵的后写覆盖语义在 CDE 检测不对称时可能导致漂移，目前仅以调试断言与 10% 容差兜底（`util/assertions.rs:30-31`）。

最后，本文的结论来源于静态阅读，未运行单元测试或基准测试，也未验证 `rand 0.10` 中 `choose` 的随机数消耗次数与 `float_cmp` 的默认容差参数。上述数值与性能判断应在团队的基准环境中复核。
