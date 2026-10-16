# SparrowCore Quantify、Eval、Sampling 代码逻辑说明

## 一、总体定位与对照范围

SparrowCore 是 Sparrow 求解器的纯 C++ 移植，源码位于 `SparrowCore/Source/` 与 `SparrowCore/include/SparrowCore/`。本文覆盖 Quantify（碰撞量化）、Eval（候选位姿评估）、Sampling（采样与局部搜索）、Random 与 Support，并与 `CodeResource/sparrow/src/` 下的 `quantify/`、`eval/`、`sample/` 逐点对照。总体上 C++ 版与 Rust 一一对应，差异集中在 RNG 分布实现、未被调用的辅助函数，以及性能问题。

## 二、Quantify：CollisionTracker、OverlapProxy 与 PairMatrix

`CollisionTracker`（`Source/Quantify/CollisionTracker.cpp`，1090 行）维护三类状态：三角形式的 `PairMatrix`、每个物品对容器的惩罚（`m_ContainerEntries`），以及全局可行标志 `m_IsSeparated`。`BuildFromLayout`（817–930 行）对每个物品调用 `CollectPolygonCollisionsUntil`，是 O(n²) 的全量建表，其中 `FindCollisionHandle`（46–58 行）与 `IndexForHandle`（439–450 行）都是线性查找。

增量路径是 `UpdateMovedItem`（135–332 行）。它先调用 `layout.Save()`，再在 180–208 行做 O(n²) 的放置匹配，随后清零被移动物品所在的行（269–272 行）。只清行不清列是正确的，因为矩阵对称，列由重算补齐。出错时保留部分更新状态，注释见 266–268 行。快照回滚由 `Save`、`Restore`、`RestoreButKeepWeights`（334–432 行）完成，后者先保存上三角权重与容器权重，恢复损失后再写回权重，保证 GLS 权重积累不因回滚丢失。

损失函数 `ItemLoss`、`TotalWeightedLoss` 等（474–576 行）都是无缓存的全量扫描。`EvaluateCandidateHazardLoss`（488–540 行）对 PlacedItem 使用 `CalculatePolygonCollisionLoss * PairWeight`，对 Exterior 使用 `CalculateContainerPenalty * ContainerWeight`，其中 Exterior 的无穷大原样传递，其他非有限值作为错误抛出。

GLS 权重更新 `UpdateWeights()`（748–787 行）与 Rust `tracker.rs` 124–155 行一致：损失为 0 时乘子 0.95，否则为 `1.2 + 0.8 * (loss / max)`，权重下限钳制在 1.0。同文件 789–815 行的双参数版本是 Rust 中不存在的乘性变体，没有生产调用者。

`PairMatrix`（`Source/Quantify/PairMatrix.cpp`，108 行）用一维数组存储上三角的 `CollisionEntry{loss=0, weight=1}`。其 `Index` 快路径（290–298 行）与 Rust `pair_matrix.rs` 96–108 行的 `calc_idx` 代数等价，我已手工核对第 0 至 4 行的索引。

`OverlapProxy`（`Source/Quantify/OverlapProxy.cpp`，192 行）的 `CirclePairContribution`（21–44 行）在穿透深度小于 epsilon 时用 `epsilon²/(-pd+2eps)` 衰减，并乘以 π 与较小半径。`CalculatePolygonCollisionLoss`（111–140 行）的公式为 `sqrt(proxy + eps²) * sqrt(sqrt(A1) * sqrt(A2))`，其中 `eps = 0.01 * max(直径)`。`CalculateContainerPenalty`（142–191 行）为 `2 * sqrt(overlap) * shapePenalty`，相交时 overlap 为 `(面积 - 交集面积) + 0.0001 * 面积`，不相交时为面积加质心距离。这些公式与 Rust `quantify/mod.rs`、`overlap_proxy.rs` 一致，π 取 `3.14159265358979323846F`。

## 三、Eval：SeparationEvaluator 与 LbfEvaluator

`SeparationEvaluator`（`Source/Eval/SeparationEvaluator.cpp`，281 行）在构造函数（63–115 行）中把所有已放置物品注册进私有引擎，并记录被移动物品的 hazard handle，由 `irrelevantHandle` 排除。`Evaluate`（117–265 行）是两级检测：第一级代理检测（216–234 行）只计算代理损失，超过 `lossBound` 即以 INVALID 提前返回；第二级精确检测（236–255 行）通过 `ContainsEntity` 跳过已计入的实体，避免重复计数。`lossBound` 由上界样本决定（181–192 行）：COLLISION 取其损失，CLEAR 取 0，其他为无穷大。`addHazardLoss`（195–213 行）是共享累加器，`extra > remaining` 时停止。旋转为无穷大直接抛出（139–142 行），平移无穷大的 Debug/Release 分支见 143–161 行。

Rust 侧 `eval/sep_evaluator.rs` 使用共享 `collector`，损失由 `collision_loss.rs` 的 `add`（44–56 行）与 `calc_weighted_loss_bounded`（62–96 行）完成。两级 `lossBound` 的边界行为需对照测试确认。

`LbfEvaluator`（`Source/Eval/LbfEvaluator.cpp`，213 行）的 `LegalizeTransform`（31–87 行）用 float 计算旋转。损失 `CalculateLbfLoss`（89–108 行）为 `10*(poi.x + bbox.xMax) + (poi.y + bbox.yMax)`。经核对，jagua-rs `geometry/primitives/rect.rs` 的 `corners()` 第一个元素是 `(x_max, y_max)`，所以 Rust 的 `bbox.corners()[0]`（`eval/lbf_evaluator.rs` 147–151 行）与 C++ 使用的点相同，此前怀疑的不一致已排除。主要差异是 Rust 忽略上界并使用 `NoFilter`，而 C++ 使用 `MakeEffectiveFilter`（110–136 行），且从不忽略 Hole 实体。

## 四、Sampling：采样器、保留策略与坐标下降

`BestSamples`（`Source/Sampling/BestSamples.cpp`，179 行）的 `Report`（90–170 行）拒绝 INVALID 样本，满员时拒绝不优于末尾的样本。相似性 `IsSimilar`（57–88 行）用 `fmod` 比较旋转而非圆周距离，阈值 `ROTATION_THRESHOLD = 0.01745329251994329577F` 即 1 度的弧度值。仅当新样本优于全部相似样本时才会驱逐它们。排序是插入排序加截断，与 Rust `best_samples.rs` 26–89 行、104–121 行语义等价，只是 Rust 使用 `sort_by_key`。

`CoordinateDescent`（`Source/Sampling/CoordinateDescent.cpp`，239 行）的 `ChooseAxis`（30–49 行）用 `UniformIndex(6)` 或 `UniformIndex(4)` 选轴，其中 4、5 映射为 Wiggle。`BuildCandidates`（84–130 行）每步生成两个候选，对角方向步长乘以 `sqrt(multiplier)`（`UpdateSteps`，132–154 行）。`RefineCoordinateDescent`（159–238 行）比 Rust 多一个 `maxEvaluations` 预算，两个候选以 `state.evaluation` 为上界评估，最优候选由 `IsBetterSample(evaluated[1], evaluated[0]) ? 1 : 0` 选出，平局取索引 0，与 Rust `min_by_key` 行为一致。成功时步长乘 1.1，失败乘 0.5，并在每次未改进后重新选轴。Rust `sample/coord_descent.rs` 的 `ask`（97–123 行）、`tell`（126–162 行）与 `CDAxis::random`（180–194 行）等价，并带有 1000 次评估的 debug 断言。

样本比较 `IsBetterSample`（`SampleEval.h` 80–87 行）的等级为 CLEAR 低于 COLLISION 低于 INVALID，损失用 `RustApproxEqualF32` 比较，NaN 直接抛出。该函数的语义是 `|diff| ≤ FLT_EPSILON` 或 4 ULP 以内，对应 Rust `jagua-rs` `util/fpa.rs` 的 `float_cmp::approx_eq!`，所用 `float_cmp` 的具体版本仍待确认。

`UniformRectSampler`（`Source/Sampling/UniformRectSampler.cpp`，172 行）在 `Create`（95–199 行）中为每个旋转计算可行范围，即容器减去旋转后包围盒，再与采样包围盒求交。`Sample`（201–210 行）依次抽取旋转索引、x、y，与 Rust `uniform_sampler.rs` 的 `sample`（79–89 行）顺序一致。`SampleAxis`（69–77 行）使用 `minValue + (maxValue-minValue)*Uniform01F32()`，缺少 rand 在上端点的舍入重试，注释却声称与 rand 0.10 对齐，这一点需要修正注释或补齐逻辑。连续旋转角 `TWO_PI*i/16` 与 Rust `i*(2π/16)` 经 float32 模拟核对，i=0 至 15 逐位一致。它被 `Optimizer/LbfSearch.cpp:148` 与 `SeparatorWorker.cpp:180、184` 调用。

`LocalSearchSampler`（`Source/Sampling/LocalSearchSampler.cpp`，206 行）的 `Sample`（152–169 行）使用 `Normal01F32`，`DecayStddev`（194–205 行）按 `init*(limit/init)^progress` 衰减。Rust `sample/` 中没有对应实现，只被 `tests/TestSampling.cpp` 使用。`RotationDistribution`（97 行）的 `FromItem`（60–105 行）构造 16 个连续角或离散列表，`Sample`（107–128 行）只消耗一次 `UniformIndex`，同样只被 LocalSearchSampler 与测试使用。

`SeparatorWorker.cpp` 把上述组件串起来：`BestSamples(nCoordinateDescents, minDim*0.05)`（154–156 行），预细化在 `best.Samples()` 上进行（225–236 行），最终细化在 `best.Best()` 上进行（238–251 行）。`MakeRefinementConfig`（21–45 行）中预细化的平移与旋转步长为 0.25/0.02 与 5°/1°，最终细化为 0.01/0.001 与 0.5°/0.05°，与 `CodeResource/sparrow/src/consts.rs` 10–28 行一致。

## 五、Random：SparrowRng 与 Rust rand 的对齐

`SparrowRng`（`Source/Random/SparrowRng.cpp`，117 行）实现 xoshiro256++，`NextU64`（71–84 行）为标准更新，`NextU32` 取高 32 位，种子由 SplitMix64 展开（45–69 行）。这与 Rust `Xoshiro256PlusPlus::seed_from_u64`（`cli.rs` 75 行、`optimizer/mod.rs` 37 行）的算法结构一致，`Cargo.lock` 锁定 rand 0.10.3 与 rand_distr 0.6.0。

`UniformIndex`（91–111 行）采用 Lemire 宽乘法加进位的第二次抽取，注释称其与 rand 0.10 的 u32 区间映射一致。目前只在算法层面确认，尚未运行验证。金标准包括 `SparrowCore/tests/fixtures/SparrowUniformIndexRustReference.tsv`（`tests/TestRng.cpp` 170 行引用）、seed 42 的 `LbfSeed42Reference.tsv` 检查，以及 `tests/RustReference/src/main.rs` 1146–1175 行生成的 `random_range` 数据，建议构建后执行。`Uniform01F32`（113–117 行）取 23 位尾数。

`Normal01F32`（119–140 行）是带 spare 的 Box-Muller，而 Rust 的 `StandardNormal` 使用 ziggurat，两者分布相同但比特序列不同，因此 LocalSearchSampler 路径无法逐位复现 Rust。`Source/Optimizer/Exploration.cpp` 约 373–440 行的 `NormalZigguratTables` 与 `RustStandardNormalF64` 按 rand_distr 0.6 的 f64 ZIGNOR 镜像了 Ziggurat，因此 Exploration 路径可以对齐，LocalSearchSampler 不能。

RNG 消耗顺序决定复现性：每次 `UniformRectSampler::Sample` 依次消耗旋转索引、x、y；`CoordinateDescent` 每次未改进后重新选轴；`RotationDistribution::Sample` 只消耗一次索引。Rust `choose` 在单元素切片上是否消耗随机数尚未核实，应以固定种子回放对比。

## 六、浮点精度策略

Quantify 与 Eval 全程使用 float32，`CalculatePolygonCollisionLoss` 与 Rust 的 f32 路径一致。为与 `TransformedRustF32`、`ApplyRustF32`、`CentroidRustF32` 逐位对齐，C++ 显式模拟了 f32 中间舍入，其类型转换细节尚未完全验证。构建必须关闭 FMA 收缩（GCC/Clang 用 `-ffp-contract=off`，MSVC 用 `/fp:precise`），否则 `minValue + (maxValue-minValue)*u` 与重叠公式可能被融合，产生超过 1 ULP 的偏差。`DebugAssertMatchesLayout`（933–1088 行）中的 `WithinUlps(...,5)` 与 10% pair 容差只用于 Debug 一致性检查。

## 七、差异与待办

第一，`CollisionTracker::EvaluateCandidateLoss(item, transform, idx)`（583–635 行）使用硬编码 epsilon 0.01 且不加 `eps²`，并无条件加入容器惩罚，与 Rust 及精确版本不一致，全仓库无生产调用者，建议删除或改为调用精确版本。第二，`SampleAxis` 缺少上端点重试，需按 rand `UniformFloat::sample_single` 行为补齐或修正注释。第三，`LocalSearchSampler` 与 `RotationDistribution` 在 Rust 中无对应实现，`Normal01F32` 分布也不同，不应视为逐位对齐组件。第四，两级 `lossBound` 的边界差异需对照测试确认。

性能热点包括：`BuildFromLayout` 的 O(n²) 建表与线性 handle 查找；`UpdateMovedItem` 的 `layout.Save()` 拷贝与 O(n²) 放置匹配（180–208 行）；无缓存的全量损失扫描；`BestSamples::Report` 每次构建 `similarIndices` 并做完整插入排序。Rust 侧同样是 O(n²) 建表，但没有 C++ 额外的 `Save()` 拷贝。

## 八、JaguarCore 依赖的调用关系

SparrowCore 通过 JaguarCore 访问碰撞引擎。`SeparationEvaluator`、`LbfEvaluator` 与 `CollisionTracker` 调用引擎的 `CollectPolygonCollisionsUntil` 与 `CollectSurrogateCollisionsUntil`，以回调方式接收 hazard。`CollectSurrogateCollisionsUntil` 中"先回调再插入"的语义尚未验证，它直接影响 `SeparationEvaluator` 中 `lossBound` 的提前停止逻辑，应作为后续核对重点。
