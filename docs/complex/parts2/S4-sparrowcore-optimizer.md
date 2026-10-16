# S4 SparrowCore 优化器层代码逻辑说明

本文阅读范围为 `SparrowCore/include/SparrowCore/optimizer/*.h` 与 `SparrowCore/Source/Optimizer/*.cpp`、`*.h`（含 `*Trace.h`），并与 `CodeResource/sparrow/src/optimizer/*.rs` 对照。路径均相对于 `/sessions/eloquent-compassionate-cori/mnt/JaguarRun/`。文中引用的行号以各文件当前版本为准。

公开入口分为四类。`SparrowOptimizer` 是完整的两阶段优化入口，其主函数 `OptimizeWithTerminator` 位于 `SparrowCore/Source/Optimizer/SparrowOptimizer.cpp:112-244`，执行顺序是先构造初始解，再进入 Explore，随后进入 Compress，最后做可行性校验并生成报告。`SppLbfOptimizer` 对应单条带宽（SPP）的 LBF 求解，核心在 `SparrowCore/Source/Optimizer/SppLbfOptimizer.cpp:34-173` 的 `SolveSppLbf`，其中 `localSamples` 由 `localSearchFraction` 决定（`:121-124`），若已放置件数与实例不符则在 `:149` 返回 `Infeasible`。`BppLbfOptimizer` 对应多容器装箱（BPP），入口为 `SparrowCore/Source/Optimizer/BppLbfOptimizer.cpp:27-191` 的 `SolveBppLbf`。`SparrowLbfBuilder` 则是 Sparrow 的构造器，`ConstructSparrowInitialSolution` 位于 `SparrowLbfBuilder.cpp:95-240`，负责按面积与直径乘积排序后逐件放置，并通过 `widthGrowthRatio` 与 `widthCeiling` 在失败时扩大带宽（`:111-143`）。

阶段切换与终止由 `OptimizationTerminator` 统一管理。`StartPhase`（`OptimizationTerminator.cpp:35-51`）会清空阶段级停止标志，而 `ShouldTerminate`（`:69-91`）依次检查外部取消的原子停止标志、迭代上限、全局时长以及当前阶段的截止时间。外部取消通过 `std::atomic<bool>` 实现，读写均为默认的顺序一致语义，因此跨线程可见性有保证，但取消是协作式的：只有运行到检查点的代码才会感知到停止。Explore 与 Compress 各自调用 `StartPhase`，阶段时长不同，所以 Compress 不会因 Explore 耗尽时间而立即退出。

Explore 的收缩循环位于 `SparrowCore/Source/Optimizer/Exploration.cpp:1003-1224`，主循环 `ExploreImpl` 每轮先尝试以当前带宽求解 Separator，若成功则收缩带宽并把当前可行解保存为最佳解；若失败则把解放入不可行池（infeasible pool），插入逻辑位于 `:1100-1161`。下一步从池中抽样时，代码先用 `std::min(std::abs(rawSample), 0.999F)` 把样本限制在 `[0, 0.999]` 内（`:1183-1190`），再映射为池下标。这里的 `std::min` 在样本为 NaN 时行为不确定，因为 `std::min` 对 NaN 的比较结果取决于参数顺序，这是一个值得关注的边界条件。抽样后若发生回滚，调用 `:1208` 处的回滚逻辑，然后在 `:1215` 执行扰动（`DisruptSolution`，`:604-882`）。扰动包含两类动作：交换两件物品（Swap），以及把被包含的物品重新放置（contained relocation）。`CollectContainedHandles`（`:496-574`）会按 hazard 索引排序，以模拟 Rust 中 `SecondaryMap` 的遍历顺序，这是为了保证与 Rust 逐步一致。

需要特别说明的是 `ExplorationConfig` 中的 `disruptionCount` 与 `containedRelocationCount`。在 `Exploration.cpp` 中，它们仅在 `config.disruptionCount == 0U && config.containedRelocationCount == 0U` 时作为一个提前返回条件（见该文件的相关分支），除此之外并未驱动扰动的实际次数，因此它们目前对行为的影响是有限的。这与 Differential 文档中"任意扰动配置未验证"的说明一致。

Separator 是 Explore 与 Compress 共同依赖的求解器，主体为 `SparrowCore/Source/Optimizer/Separator.cpp:493-749` 中的 `Separator::Run`。它维护两个计数器：strike（连续失败的轮次）与 no-improvement（没有改进的轮次）。每轮 `RunWorkerRound`（`:173-306`）启动多个工作线程，每个线程使用各自的随机数生成器。线程通过 `std::async(std::launch::async, ...)` 创建（`:557-582`），每个任务捕获异常并保存在 `std::exception_ptr` 中，最终在主线程 `get()` 时重新抛出。这是一种按轮次创建线程的模式，与 Rust 中 rayon `ThreadPool` 的复用线程池不同，前者每轮付出线程创建的开销，但实现简单，结果也更容易归并。最佳工作线程的选择在 `:609-615`，按加权损失取最小值，遇到并列时取第一个最小者，这与 Rust `min_by_key` 的语义一致。GLS 权重通过 `CollisionTracker::UpdateWeights` 更新。

Separator 的回滚有两种语义。`Separator::Rollback`（`:409-417`）调用 `RebuildTracker`，权重被重置，对应 Rust 的 `rollback(None)`。另一种 `RestoreTrackerButKeepWeights` 保留权重并恢复跟踪器，对应 Rust 的 `rollback(Some(ots))`（Rust 侧见 `separator.rs:250-264`）。`Separator::ChangeStripWidth`（`:353-407`）在改变带宽后调用重建逻辑，因此每次改宽都会得到干净的跟踪器。`MoveItem`（`:419-471`）完成单件移动的状态更新。

SeparatorWorker 的核心是 `FindBestSeparatorMoveImpl`（`SparrowCore/Source/Optimizer/SeparatorWorker.cpp:118-284`），依次执行参考位姿评估、聚焦采样（focused sample）、容器采样（container sample）、预精炼（pre-refine）以及最终精炼（final refine）。`InvalidWorker` 在参数无效时返回错误而不是崩溃，这与 Rust 的 `panic` 路径不同。值得注意的是 Rust 侧的 `worker.rs:25-132` 中 `move_items` 没有终止检查，而 C++ 的 Separator 内层循环在 `Separator.cpp:540` 处存在一次终止检查，Rust 侧 `separator.rs:116` 附近则没有对应检查，这是一处已知的行为差异，它可能导致两侧在超时边界处的停止点不同。

Compression 阶段位于 `SparrowCore/Source/Optimizer/Compression.cpp:191-385`。它以基于时间或基于失败次数的方式逐步收缩带宽，收缩映射见 `:249-273`。拆分（split）时使用 `separator.Random().Uniform01F32() * width`（`:307-308`）取随机位置。每个工作线程的随机数在 `:231` 被重置，以保证每次收缩的可复现性。接受条件在 `:349`，为 `loss == 0.0F && metrics.feasible`。Rust 侧 `compress.rs:94` 的接受条件只检查 `loss == 0`，缺少 `metrics.feasible` 这一项。因此在损失为零但仍被判为不可行的边缘情况下，两侧结果可能不同。Differential 文档中"多工作线程压缩"被列为未验证项。

随机数方面，`SparrowRng` 基于 xoshiro 系列实现，`UniformIndex` 用于离散选择。为复现 Rust 的 `IteratorRandom::choose`，代码中提供了 `RustCoinFlipper`（`Exploration.cpp:236-321`）与 `ChooseFromRustFilteredRange`（`:323-339`）。为模拟 `rand_distr` 0.6 的标准正态分布，`RustStandardNormalF64`（`:427-462`）采用 Ziggurat 算法，`detail::RustNormalF32` 在 `ExplorationNormalSampler.h` 中声明。`IsRustF32Distinct`（`ExplorationNumeric.h:7-22`）则用 f32 精度与 1% 容差判断候选解是否与首个解不同。相关的 `random_range` f32 行为也在对应函数中按 Rust 语义处理。

构造阶段的 warm start 存在一处异常与 `Result` 不一致的问题。`MakeWarmStart`（`SparrowOptimizer.cpp:39-105`）在 `:90` 直接抛出 `std::logic_error`，而其他路径都以 `sparrow::Result<T>` 返回 `jaguar::ErrorCode`。最终校验（`:201-227`）则返回 `Infeasible` 错误。因此调用方在处理 warm start 时需要同时处理异常与错误码两种形式。另外，warm start 时会跳过构造器的一次随机数抽取，RNG 的种子顺序因而与冷启动不同，阶段间的随机数顺序由 `:131-157` 确定。

LBF 搜索与损失函数部分，`LbfSearch.cpp:118-273` 的 `SearchLbfImpl` 只执行容器范围的采样，随后做预精炼与最终精炼。其中 `localSearchFraction` 在 `:215-217` 未被使用，因此在 SPP 路径中它只通过 `SppLbfOptimizer` 影响采样数量，而在 LBF 搜索本身不生效。`LbfLoss` 的代价为 `xMax * 10 + yMax`（`LbfLoss.cpp:19-26`），当坐标非有限值时返回无穷大；`TightenSampleBBox`（`:28-41`）利用当前代价上界收紧样本包围盒的 `xMax`，以加速剪枝。`operator<`（`:43-56`）把 NaN 代价排在最后。`BppLbfOptimizer` 的排序键由 `useSparrowConstructorOrder` 决定：为真时取凸包面积乘直径，否则取直径（`BppLbfOptimizer.cpp:52-54`），并在 `:61-68` 以键降序、下标升序排序，保证确定性。若某件物品的键非有限，则返回 `InvalidInput`（`:55-58`）。BPP 放置时先在已开放的版图中搜索（`:86-122`），失败后在尚有库存的新箱中搜索（`:126-162`），两者都失败时结束该件物品的放置（`:164-167`），此时剩余需求大于零，最终 `feasible` 为假（`:188-189`）。

Differential 文档 `SparrowCore/docs/Differential.md` 覆盖的内容包括固定夹具矩阵（约第 11-70 行）、接受规则（约第 105 行）以及重跑说明（第 175 行起）。它覆盖了 Rust 与 C++ 在若干固定种子与固定配置下的轨迹一致性，并通过 `Tests/Fixtures` 中的 Rust trace 文件比对。它未覆盖的内容包括：任意扰动配置（即 `disruptionCount` 与 `containedRelocationCount` 的非默认值）、多工作线程压缩、工作线程数超过 3 的情形，以及完整的端到端一致性。因此上文提到的内层终止检查差异（`Separator.cpp:540` 与 `separator.rs:116` 的对应）以及压缩接受条件差异（`Compression.cpp:349` 与 `compress.rs:94`）都属于未被夹具覆盖的区域。

与 Rust 实现的其他差异还包括：线程模型上，C++ 每轮新建线程而 Rust 复用 rayon 线程池；错误模型上，C++ 使用 `Result` 与异常混用，Rust 使用 `unwrap`、`expect` 与 `panic`；构造阶段的增长参数也不同，C++ 的 `SparrowLbfBuilder.cpp:139-187` 与 Rust `lbf.rs:102-113` 的步长计算并不完全一致。

线程安全方面存在以下风险。第一，`std::async` 的任务共享 `Separator` 的部分状态时，必须保证每个任务只写自身的 worker 状态，否则会出现数据竞争；当前代码通过为每个任务复制 RNG 与跟踪器视图来规避，但这依赖于调用约定而非类型系统保证。第二，`OptimizationTerminator` 的标志读写虽为原子操作，但 `StartPhase` 清空标志与 `ShouldTerminate` 读取之间没有同步屏障，若外部线程在二者之间设置取消，则该次取消可能推迟到下一个检查点才生效。第三，死锁风险较低，因为代码中没有多把互斥锁嵌套，主要的阻塞点是 `future::get()`，若某个工作线程陷入无界循环则会导致主线程无法返回，目前仅依靠协作式终止缓解。第四，异常传播路径为 worker 中 `catch(...)` 保存 `std::exception_ptr`，主线程在 `get()` 时重新抛出，但 `MakeWarmStart` 的 `std::logic_error` 会直接穿透到调用方，需要调用方额外捕获。第五，`std::min` 在 NaN 输入下的行为会影响池抽样，应在上游保证样本有限。

综上，本层的逻辑框架清晰，两阶段结构与 Rust 基本对应。剩余风险主要集中在三处：未被夹具覆盖的扰动与压缩配置、两处已识别的语义差异（内层终止检查与压缩接受条件），以及线程模型与错误模型的结构性差异。后续若要扩大一致性验证，应优先为这三类场景补充固定种子的 Rust trace 夹具。
