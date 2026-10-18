# Sparrow → SparrowCore 完整等价复刻路线图

> **执行说明：**逐个 Rust 函数和分支建立 C++ 对应实现与跨语言测试；每项变更先有可复现的 Rust 参考结果，再做最小实现并运行 Debug/Release 验证。除用户在当前任务中明确授权外，不创建 Git commit。

**目标：**在明确、固定的 Rust Sparrow 版本、功能集、目标平台和输入域内，使 SparrowCore 的可观察行为与 Rust Sparrow 等价，并为每个源码路径保留可复核的迁移和验收证据。

**实施策略：**按 README 的 Rust→C++ 文件映射逐函数、逐分支对照，实际修改按依赖顺序推进；保留 Rust 的状态转移、浮点运算域、随机数消耗次序、错误出口和停止条件。语法不机械逐行翻译；Rust 所有权、迭代器和错误类型要映射到 C++ 结构，但不能因此改变外部结果。

**技术基线：**以 2026-10-09 当前工作区及 `docs/logic-comparison-sparrow/README.md` 所列 RustReference 为起点：rustc 1.98.1、Cargo.lock 固定依赖（含 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`）、`x86_64-pc-windows-msvc`。RustReference 对路径依赖 `sparrow` 显式关闭默认 features，并启用 `oracle-trace`；它自己的 jagua-rs 依赖边关闭默认 features，但 `CodeResource/sparrow/Cargo.toml` 的传递依赖会重新启用 jagua-rs `default`，实际 feature 合并结果为 `jagua-rs default+spp`、`sparrow oracle-trace`，未启用 Sparrow `simd`（已用 `cargo tree -e features -i jagua-rs` 核对）。SparrowCore 的几何、碰撞和 IO 又委托 JaguarCore，因此可复现基线还必须包含 Rust/C++ 两侧所依赖的 jagua-rs/JaguarCore 源码快照及构建产物。当前工作区描述还不足以独立重建全部源码；阶段 0 必须登记 Sparrow、SparrowCore、JaguarCore 源码修订号或逐文件清单哈希、Cargo.lock 哈希、rustc/Cargo/MSVC 工具集版本、C++ 构建类型、链接的 JaguarCore 版本/构建配置和关键编译选项。基线变化时重跑受影响 oracle 并更新记录日期。

**冻结指针：**进入本轮 P0 源码修复前的不可变快照见 [baseline-2026-10-09.md](baseline-2026-10-09.md) 与 [逐文件 SHA-256 manifest](baseline-2026-10-09.json)，生成器为 [freeze-baseline.ps1](tools/freeze-baseline.ps1)。Rust 侧分别固定工作区 Sparrow 源码与 RustReference 实际解析的 jagua-rs checkout；`CodeResource/jagua-rs` 本地副本单独记录。C++ 侧固定 JaguarCore/SparrowCore 源码、项目配置、测试/fixture，以及当前 Debug/Release 测试运行器和 JaguarCore DLL 的哈希。构建 kit 当前由生成的 Makefile 证实为 Qt 6.5.3、qmake 3.1、`win32-msvc`、C++17；Rust 认证 target 为 `x86_64-pc-windows-msvc`。本基线只认证当前测量的平台和 feature，不能外推到其他平台或 Rust SIMD。

## 全局约束

- Rust 当前基线是目标语义。发现 Rust 自身的缺陷时，不得只在 C++ 侧修正后仍宣称等价；先固定 Rust 实际行为，或明确记录经用户认可的共同算法变更。
- 同一对照必须使用相同实例、稳定 item ID、有效配置、显式 seed、feature、profile 和停止条件。随机 seed 必须比较相同的有效 seed；不能把 Rust `None` 与 C++ 的 0 当作同一输入。未显式 seed 的生产运行按非确定性语义验收：核对熵源初始化、有效 seed 的生成/保存/暴露和 RNG 健康；逐条轨迹 parity 使用相同的有效 seed。不能要求两次独立的熵源运行产生相同布局，也不能把不同 seed 轨迹不相同当成 parity 失败。
- 测试 oracle 必须由 Rust 参考路径生成或核验。C++ 自生成基线只能用于 C++ 内部回归，不能证明 Rust/C++ parity。
- 离散选择、项目身份、事件顺序、阶段和终止原因按用例精确比较；浮点字段按各记录预先定义的字段容差比较。要求位级一致的字段必须另做 raw-bit 断言。
- Debug 与 Release 都是迁移范围。Rust panic 与 C++ exception 可以采用不同语言机制，但需比较触发条件、失败类别、失败前后的可观察状态和是否继续执行。
- 目标是对已声明的有效输入域、feature 集和平台建立完整 parity；不把有限样例表述为对数学上所有输入、所有编译器和所有硬件的证明。
- Rust `Ord` 被非传递比较违反时，必须枚举实际消费者、触发输入和 Debug/Release 行为。不得把有限排列结果推广到任意输入；若合法生产输入能触发且 Rust 行为跨 profile 不稳定，则该域在基线修订前不能声明完成，不能由 C++ 单方面定义替代结果。
- 所有可观察失败按共用错误分类表比较：触发条件、Rust panic/Result/None 与 C++ exception/Result/optional 的映射、失败前后的布局和计数、捕获边界、listener 事件、线程/进程是否退出及是否继续下一阶段。语言级文本只有在属于公开接口时才要求逐字一致。
- Sparrow 没有对应配置项的 C++ 扩展（例如额外的迭代上限、失败池容量或独立求解入口）必须与 Rust 可表达的基准配置分开记录；Rust/C++ parity 用例要关闭会改变搜索、停止或输出的扩展选项，扩展功能另行测试，不把扩展路径的结果称为 Sparrow parity。

## 完整复刻的认证范围

本路线按用户提出的“完整、等价复刻”采用端到端产品范围：S01–S19 的算法与支持 feature、S20–S21 的停止和报告、S22–S23 的文件/CLI/Bench 输入输出，以及 S24 的算法状态、进度、日志、取消和最终结果都要有结论。S24 的 ratatui 绘制不必进入 SparrowCore 库；等价语义应由纳入目标的 C++ 宿主 UI/Demo 承接。README 中“`tui/` 不属于算法核心”描述的是代码归属，不自动等同于从完整产品验收中排除；若最终目标仅限核心库，阶段 0 必须明确把 CLI/Bench/宿主 UI 列为范围排除，并把该裁定反映到验收结论。UI 绘制技术和视觉皮肤可以不同；必须一致的是传递给算法的输入、seed、运行预算、进度/取消行为和用户可观察的结果。当前 scalar RustReference 是最低认证 profile；若产品支持 Rust opt-in SIMD 或其他 Rust feature，每个受支持 feature 集都要建立独立 oracle 和 C++ 对照。阶段 0 必须逐项标注“必需并验收”或“经用户确认排除”，其余章节统一引用该清单。

外部输出的比较口径也要固定：CLI exit code、解析后的有效配置和停止原因必须精确一致；JSON 按解析后的结构、稳定 item ID 和离散字段精确比较，浮点字段沿用预先登记的字段容差，要求位级一致时比较 raw bits，只有序列化字节格式属于公开契约时才要求字节相同；SVG 按几何、ID 和所需标签比较，浮点坐标沿用记录容差，只有格式本身是契约时才要求字节相同；stdout/stderr、错误文本和帮助信息按明确的公开接口逐项比较；TUI/Demo 比较报告、日志、进度、取消和最终状态，不要求像素级一致。

## 当前完成度与剩余缺口

现有记录覆盖 Rust Sparrow 的 24 个比对单元和 42 个 `.rs` 文件（40 个实现/应用文件、2 个嵌套 TUI 测试文件），并逐模块记录 C++ 对应路径和差异。历史测试次数保留在各阶段记录中，仅代表相应日期的快照；当前通过状态以紧接的“当前验证”条目为准。固定 fixture 只证明其列明的输入、profile 和比较字段，不外推一般等价；S06、Restore、worker、时限等范围边界见对应记录。四实例 LBF 构造对照使用固定预算 `nContainerSamples=64`、`nFocussedSamples=0`、`nCoordinateDescents=1`，这组对照本身不代表 Rust 默认的 1000/0/3；另有 seed=42 的默认预算完整 builder 输出及相邻初始宽度 reference。上述构造用例均不代表完整 `optimize → compress`。
- **当前验证（2026-10-17）**：RustReference Debug/Release 各 153 个测试通过（另有 2 个 ignored 搜索用例），cargo fmt --check 通过，含 S15 精确同 loss tie 及 S03 默认 LBF/相邻宽度 Rust references；S12 现有的 Exterior 距离平方溢出、GLS `+Inf` 权重和容器最终乘法用例继续通过，S12 的四项 `OverlapProxy` reference 覆盖两条真实 evaluator subnormal 路径和直接 helper `epsilon=f32::MAX` 的 NaN profile 边界；新增有限有效方形经有限平移导致 bbox `shapeArea=+Inf` 的低层 reference，已移除旧 C++ 前置归零并通过 Debug/Release 对照；另有 Rust public-field malformed 零/负 bbox reference（零面积 Debug panic/Release +0，负面积两 profile NaN），以及正 shoelace 面积/零 bbox 乘积夹具：Rust POI unwrap panic、C++ InvalidPolygon，均未进入 loss helper；41,761 个有界候选无 Rust 构造成功，但全域不可达性仍未证明。两条实际路径 Debug 按 Rust 断言失败、Release loss 位模式分别为 pair `0x000162e2` 和 container `0x00014e8e`；直接 helper 在 Debug 断言、Release 保留 NaN。C++ SparrowCore Debug/Release 全量 runners 均退出码 0；新增缺失 surrogate pair/container 调用边界 Rust/C++ 用例覆盖 Debug/Release，并确认失败不改 polygon 状态。此前容器最终乘法 fixture 的成功导入、偏置/简化几何及公式操作数逐位匹配；几何 reference 覆盖普通斜四边形、循环起点、sub-ULP 方形、凹形 L Inflate、凸形 Deflate、顺时针/共线输入及 adapter 指数临界两侧，另有固定 SPP importer→真实 evaluator 用例。一般几何与更广泛 SPP 输入仍开放。本轮 SparrowCore Debug 全量 runner 首次在 unseeded effective-seed replay 断言失败，随后完整重跑退出码 0；间歇原因尚未定位。S21 原生触发频率、异常边界及 observer 扩展范围见 [S21 记录](records/S21-listener.md)。

这些结果说明核心移植已经广泛落地，不代表完整等价已经完成。主要空缺是：若干已知边界语义仍有差异；很多几何、候选顺序、错误出口和运行预算只测固定夹具；完整自然运行的 `optimize → compress` 覆盖不足；CLI、IO、Demo/TUI 和 opt-in feature 的 parity 尚需按认证范围验收。现有四实例 LBF 对照使用固定的 nContainerSamples=64、nFocussedSamples=0、nCoordinateDescents=1 构造预算，不是完整优化流程；目前也没有代表性实例集的总体宽度、密度或布局差异统计。

## 逐文件迁移方法

每个工作项均按以下循环完成，并在相应的 `records/Sxx-*.md` 中更新源码锚点、差异结论和测试证据：

1. **建立源码清单。**按 Rust 函数记录参数、返回值、前置条件、内部状态、每个分支、调用顺序及错误出口；为每个函数指定 SparrowCore 对应文件/函数，标注一对一、拆分、合并或无对应项。
2. **写行为约定。**注明状态所有者、可变状态更新时点、`f32`/`f64` 转换位置、迭代次序、比较规则、随机数消耗、时间读取点和停止条件。不能只记录“功能相同”。
3. **先固定 Rust 预期。**在 RustReference 新增最小输入与测试，直接调用目标 Rust 路径，记录返回值、错误、状态快照和必要的事件/RNG trace；使用 Rust Debug 与 Release 核验基线是否稳定。
4. **移植最小 C++ 改动。**复用 SparrowCore 与 JaguarCore 现有类型和模块，不顺带重构。由 C++ 测试读取同一输入/fixture，并在 Debug、Release 与 Rust 结果对照。
5. **闭合差异。**遇到不一致，沿第一个分歧的事件或字段回溯，不以最终平均值掩盖离散选择差异；修复后运行目标用例、相关模块测试和全量 Debug/Release 测试。
6. **更新记录。**只有 Rust oracle、C++ 对照器、实际运行结果和边界限制齐全，才把条目标成已验证。同步更新 README、迁移矩阵和相关 Sxx 记录；移除过期结论并注明不能外推的范围。

## 剩余差异清单

下表总结现有逐文件记录中仍需闭合的项目。P0 表示可能改变算法结果、候选选择、随机轨迹或停止；P1 表示完整可观察接口和运行路径必须闭合；P2 表示目标 feature/产品入口需先确认纳入范围，若纳入就必须完成 parity。

| 单元 | 已知差异或证据不足 | 剩余工作与验收结果 | 优先级 |
|---|---|---|---|
| [S01 配置](records/S01-config.md)、[S02 常量/RNG](records/S02-consts-lib.md) | S01 seed/纳秒时限、有效配置端点与 cold/warm 子 RNG 顺序已于 2026-10-17 收口；S02 的 19 个常量字段及 `lib.rs` 导出面逐项复核，空范围失败副作用、132 组 shuffle 及连续 sampler 16 项 choose 的采样后 RNG 状态、当前 Sparrow 的 `IteratorRandom::choose` 调用组合已由 Rust/C++ fixture 覆盖 | 保留固定 seed 与列明的 shuffle/choose 组合；任意外部 iterator size hint、`usize` 极大长度和一般 Explore 几何不由固定 fixture 外推，其他采样与几何边界仍按 S05/S06/S16 记录继续扩展 | P0 |
| [S03 LBF](records/S03-lbf.md)、[S04 Separator](records/S04-separator.md)、[S05 Worker](records/S05-worker.md) | Rust 标准构造预算 1000/0/3；零样本、item 0 构造失败、超过 32 次扩宽成功、focused 字段未映射、固定多实例构造已定向核对。Rust 与 C++ Sparrow SPP 均不能表达 quality zones，BPP 质量区是 C++ 扩展。Sparrow constructor 默认不设尝试次数 cap；通用 `LbfConfig`/直接 `SolveSppLbf` 仍默认 cap=32、0 不表示无限，作为低层 C++ API 单独记录；显式正数 constructor cap 是 C++ 扩展。初始已分离且正内层预算的空 worker round/progress 已对齐；S04 的 0.98 双侧阈值、内层迭代上限、零限额与 4-worker 空 round 不变量已按固定矩阵闭合；S05 另有 1/2/3 worker 固定整轮轨迹和 4-worker 活动负载自然调度不变量，双 worker 失败/恢复见 S12；合法正碰撞路径的 Rust best-sample 不变量未发现可触发反例，C++ 空 transform continue 留作域外防御分支 | 保留 1/2/3 worker、搜索 trace、构造失败、零采样、超过 32 次扩宽与 focused 字段 references；默认 1000/0/3 完整构造及相邻宽度已由 S03 固定矩阵覆盖。随机 shuffle 的一般消耗序仍未证明；自然多 worker 的选中结果/RNG 次序不比较，S12 固定双 worker unsupported-hazard/listener/phase/worker RNG 重试已验证，但其他 worker 数与错误源不外推；C++ 独有构造/求解入口单独标为扩展 | P0 |
| [S06 Explore](records/S06-explore.md) | empty-large 与“失败→恢复→收缩→再失败”的池清理/计数重置均已有 Rust/C++ reference；后者在正上限 2 下核对池从 `[0]` 清空并以 `[1]` 重建、failure ID 递增且新失败仍抽样。单候选 denominator=1 和恰有 10 个 distinct 候选的实际轨迹均已闭合；更高候选规模仍只有固定样本证据；partial/overlapping restorable warm start 的报告类别、实际可行性字段及顶层接受契约已有定向对照；`RotationRange::None`/`Continuous` 已有固定实际轨迹；低层 `Discrete([])` 实际 Explore 路径已核对 Rust panic/C++ Infeasible 及错误前状态，外部 importer 拒绝空角列表；contained collector 有效空结果与混合 POI 过滤已由实际 Rust/C++ 轨迹闭合；warm-start Restore 已有 15 个 Rust/C++ 配对场景：原 9 个需求/定义/hazard 缺失或错身份/额外实体/越界顺序/sparse SlotMap 场景，加上重复实体、错误 shape、错误 pk、错误 transform、dynamic 列表中的非 dynamic hazard、与静态 Exterior identity 冲突 6 种 CDE hazard 变异；Rust Debug/Release 与 C++ Debug/Release 的已测状态、失败顺序及回调边界均有对照。另有 3 个 C++ 表示层检查（generation 不匹配、free-slot 缺项、null shape），Rust 安全 SlotMap/Arc 无直接构造对应项；其他损坏形态仍开放。 | 更高候选规模和一般 Explore 几何继续按固定 reference 扩展，不外推普遍等价；已测 Restore profile/state/callback 边界对齐；后续针对多重/重复快照条目的具体顺序、未测 CDE entity 字段组合、placement 排序及其他损坏快照继续建立具体 Rust/C++ reference。C++ 无效 generation/free-slot 元数据作为没有安全 Rust 构造对应项的表示层防御单独记录，不外推为跨语言失败等价。候选数大于 10 时继续用固定用例逐项声明边界，不外推一般等价 | P0 |
| [S07 Compression](records/S07-compress.md)、[S08 编排](records/S08-optimizer-mod.md) | SparrowCore config/deadline/elapsed 的纳秒量化差异已修复；stopwatch 起点、`f32`/`double`、直接 `timeLimit=0` 仍不同；自然完整时限、一般错误出口和其他配置下完整阶段结果未证 | 对 warm/cold start、各 shrink mode、直接阶段入口、deadline 前后和错误退出分别对照；固定时钟/回调触发时点，并核对当前尝试是否完成、阶段交接、最终布局和报告 | P0 |
| [S09 LBF evaluator](records/S09-lbf-evaluator.md)、[S10 SampleEval](records/S10-sample-eval.md) | S09 固定 LBF trace 的 75 条 Clear evaluator loss 已在 Debug/Release 与 Rust raw-bit 相同；另有 29 条 Invalid 分类，transform 用 2e-5 容差。live-layout mutation 不属于 Rust 安全共享借用可表达输入，C++ evaluator 头文件记录不变性前置条件。S10 固定 profile 已覆盖七组边界 42 排列、四值非传递链 24 排列、BestSamples 32 项插入/NaN 失败状态及 CoordinateDescent 近似并列；Rust `Ord` 违反传递性使任意序列无通用保证 | 保留 S09 稳定性契约；S09 的精确分数结论限于固定 seed、输入与预算，任意几何未认证。S10 按列出的 Rust 1.98.1 Debug/Release profile 与消费者轨迹收口；其他非传递序列仍列为 R3 未认证域，不把有限排列推广到任意输入 | P0 |
| [S11 Separator evaluator](records/S11-sep-evaluator.md)、[S12 Collision loss](records/S12-collision-loss.md) | 多 hazard 顺序、固定多 pole 几何、Exterior raw loss、首个超预算 hazard 与累计上界相邻值均有 Rust/C++ Debug/Release reference；Hole/QZone 候选与 Tracker 核心失败状态已对照；单 worker 搜索与双 worker QZone failure 夹具核对 RNG-after、布局/Tracker、异常传播和 trace/callback cutoff；候选平移/旋转分量 `±Inf` 的指定边界，以及有限候选导致的 Exterior 质心距离平方溢出、有限 raw loss 与 128 次 GLS 更新形成的 `+Inf` 权重相乘两条路径，均已按 Debug/Release 对照；本轮另对照实际 pair/container subnormal loss 路径、直接 helper NaN 断言和有限几何经有限变换导致 bbox shapeArea=+Inf 的守卫差异（溢出用例是低层 helper）；容器最终乘法已由 Rust/C++ 成功导入 Item 后的 Layout/CDE/SeparationEvaluator 双侧触发为 Collision(+Inf)，该 fixture 的有序几何和公式操作数逐位匹配；另有凹形 Inflate、凸形 Deflate、顺时针/共线输入、adapter 指数临界和单一 SPP importer→evaluator fixture；其余几何/模式、广泛 SPP 输入、一般位级一致、零面积乘积的全域可达性、其他有限/非有限公式输入及 pole/CDE 顺序仍开放；固定零乘积夹具在两侧构造阶段拒绝，41,761 个有界 Rust 候选无构造成功，其他 worker 数与错误源不外推 | 保留已测 raw-bit 与上界分类；subnormal 用例只证明列出的合法输入、Debug 失败边界、Release loss 位模式和状态，不证明其他尺寸或公式组合；最终乘法 fixture 的实际 evaluator 分类及偏置/简化几何已逐位对照；零面积乘积的全域可达性仍未证明（固定夹具构造失败，41,761 个有界候选无成功构造）；无效/缺失 surrogate、矩形防御及未测几何仍按具体可达边界补 oracle；保留列明双 worker QZone oracle，不外推一般等价 | P0 |
| [S13 Search](records/S13-search.md)、[S14 Coordinate descent](records/S14-coord-descent.md)、[S15 BestSamples](records/S15-best-samples.md) | LBF/Separator 搜索现覆盖 seed=42 的两组采样预算并比较完整 trace/post-search next_u64；Separator 单点重合 fixture 核对精确同 collision loss 的首个接受与后续拒绝。S14 的 FPA 近似并列、loss raw bits 完全相同的精确并列、Rust Debug 1000 次评估断言/Release 无此上限，以及 C++ `maxEvaluations` 0/1/2 和默认超过 1000 次评估边界均有 Rust/C++ Debug/Release 证据；C++ 预算接口作为扩展单独记录。S15 的 32 项混合序列/容量/相似度/NaN 状态、四个不同 transform 的精确同 loss tie 和零容量 profile 行为均有 Rust/C++ Debug/Release reference。BestSamples 本身无 RNG | S13 其他搜索预算/配置、不同 transform 的同 loss Search tie、一般 sampler 错误和 Search-level CD 预算回退仍未认证；S15 任意非传递插入顺序仍未认证。保留已测定向 fixtures，不外推到任意输入 | P0 |
| [S16 Uniform sampler](records/S16-uniform-sampler.md) | singleton bbox、one-ULP 区间 16 个样本与 post-sample RNG、no-fit 分类、所测有限 double bounds 溢出到 f32 infinity 均有 Rust/C++ 对照；signed-zero singleton 求交有 Rust 位模式 oracle，C++ Debug/Release 全量 runner 已通过；NaN 离散角在 LBF/Separator 核对 panic/error 时点、事件、布局/Tracker 与 RNG 状态 | 保留当前定向 fixtures；其他浮点表示边界和 JaguarCore shape-transform Result 错误仍开放。增加用例时优先从 Rust 可构造输入映射，避免把未触发错误分支标成已等价 | P0 |
| [S17 PairMatrix](records/S17-overlap-pair.md)、[S18 Tracker](records/S18-tracker.md) | PairMatrix 映射/求和/越界/长度边界已闭合；新增共享 Rust/C++ PairMatrix+Tracker fixture，逐位锁定初始化、GLS 权重更新、单项移动后的全部 pair/container 项及汇总状态；Tracker 的 NaN/±Inf loss 传播与权重 floor、moved-row、getter failure、Hole/QZone 初始化/搬迁失败均有 Rust/C++ Debug/Release reference。Tracker/Layout Debug-only CDE 检查的创建、恢复、移动及 worker/separator 调用边界已逐项映射；Exterior 碰撞和 U 形单向发现均有真实 Layout fixture。近等 bbox U 形单向发现的 pair loss bits 均为 `0x409a27bb`；S12 已加入 OverlapProxy 非正规中间值和有限变换导致 bbox shapeArea=+Inf 的 Rust Debug/Release 对照；容器 centroid 已改用 `Rect2d::CentroidRustF32`，端点求和溢出 fixture 的位模式与 Debug/Release loss 已对照；fixture 仅隔离公式；有效 missing surrogate 调用边界已按 Rust panic/C++ `std::logic_error` 对照；malformed 矩形防御仍有边界差异 | 保留并运行近等 bbox U 形跨语言 golden；若继续扩展则覆盖更多 bbox/FPA 临界和 quadtree 分区；S12 的 subnormal/NaN fixtures 仅证明列明输入及 profile 结果，其他 pole/CDE 顺序、零面积乘积全域可达性和 API 表示边界仍开放；固定候选在 Rust POI panic/C++ InvalidPolygon 阶段被拒绝 | P0 |
| [S19 SIMD](records/S19-simd.md) | 当前 RustReference 固定为不启用 SIMD；Rust SIMD 是 opt-in，C++ AVX2/SoA 路径没有 parity fixture | 阶段 0 列明产品支持的 Rust feature 集；scalar 是最低目标，每个实际支持的 SIMD/其他 feature 集均单独冻结构建条件并比较候选、早停、损失和容差。仅未支持的 feature 可列为经确认的范围排除 | P2 |
| [S20 Terminator](records/S20-terminator.md)、[S21 Listener](records/S21-listener.md) | S21 Rust 原生回调触发条件已逐项映射；默认 cold-start 顺序/计数/字段、phase stop/reset、SeparationProgress panic/exception 截断和 FINAL stop/throw 有跨语言用例。C++ 独有 `OnProgress`/`OnImprovement` 已隔离为显式可选 `OptimizationObserver`；`OptimizationHistory` 是返回值扩展。S20 的 SparrowCli SIGINT、原生宿主取消及 Windows `CTRL_C_EVENT` 子进程轨迹已通过 Debug/Release 验证；真实键盘 Release 冒烟已由用户回报通过；S12 固定双 worker unsupported-hazard 错误传播已闭合；WASM 同步 C ABI 无运行中取消入口 | S21 原生 Listener 面按 S21 记录验收；observer 启用时按 C++ 扩展路径处理，不计为 Rust parity。S20 的 CLI SIGINT phase reset、宿主跨线程全局取消、Windows console event trace 与一次真实键盘冒烟已按 S20 reference 验收；固定双 worker 错误传播按 S12 验收；不以 S21 callback fixtures 替代 | P1 |
| [S22 IO](records/S22-util-io.md)、[S23 CLI/Bench](records/S23-cli-bench.md)、[S24 TUI/Demo](records/S24-tui.md) | warm-start 文件级闭环缺失；CLI 全局与分阶段时限、零值和计时起点不同；SVG surrogate 显示选项和目录创建行为不同；Rust Bench 是任意实例、多 run 统计基准，C++ Bench 是固定场景回归基准；浏览器 Demo 对 JSON/SVG 分别完整运行 Optimize，且未覆盖 Rust TUI 的实时状态/日志/交互 | 用相同文件和参数完成导入—有效配置—优化—导出—再读回；核对退出码/错误/时限/seed/SVG 选项；补齐或明确裁定 Bench 的用户实例、多 run 和统计输出范围；修正 Demo 重复运行或证明单次算法结果复用；实现语义相同的状态、日志、取消与输出行为 | P1 |

## 分阶段执行路线

### 阶段 0：冻结范围和迁移账本

- 已冻结 Sparrow、Cargo 实际解析的 jagua-rs、jagua-rs 本地镜像、JaguarCore、SparrowCore 和 S01–S24 比较记录的逐文件哈希；基线说明、锁定 feature 图及生成方式见上方冻结指针。后续 P0 修复不得覆盖这个快照。
- 已建立[共用错误分类表](error-semantics.md)，记录触发条件、Rust/C++ 出口、状态副作用、传播边界及当前未闭合项。jagua-rs 与 JaguarCore 的既有逻辑比对记录只能作为对应依赖路径的证据，阶段 0 仍须确认被 SparrowCore 调用的几何、碰撞、导入和导出 API 落在认证范围内。
- 继续确认全部 Rust 源码到 C++ 符号/测试的映射、实际对外支持平台及各 feature 的必需/排除状态；不要把本机 x64 基线扩大表述为跨平台保证。
- 以 README 的 S01–S24 映射为唯一清单，为全部 Rust 函数/测试/公共入口补齐 Rust→C++ 目标、迁移状态、差异 ID、oracle 和调用者。把当前 `records/` 中已闭合与未闭合结论同步到矩阵。
- 把 parity 输入域分为：有效普通输入、边界/退化输入、非法输入、随机/时限运行、宿主入口；为每类明确需要匹配的输出和错误语义。
- **验收：**登记 Sparrow、SparrowCore、JaguarCore 源码修订号或逐文件清单哈希、Cargo.lock 哈希、jagua-rs revision 与实际合并 feature、rustc/Cargo/MSVC 版本、C++ build configuration/defines、链接的 JaguarCore 构建和 feature 集；所有 42 个 `.rs` 文件均有映射（实现/应用文件对应到 C++ 符号，嵌套测试文件对应到测试覆盖）；认证范围清单对算法模块、CLI、IO、Bench、宿主 UI、SIMD/其他 feature、平台逐项标记必需或经确认排除；建立共用错误分类表，覆盖 panic/Result/None/exception/optional 映射、状态副作用、捕获边界及线程/进程结果；没有无说明的 orphan 函数、feature 或入口。

### 阶段 1：按依赖顺序闭合核心语义差异

按 `migration-decision-matrix.md` 的依赖顺序推进，不同时改动互相依赖的算法层：

1. **配置与随机数（S01–S02）。**保留 Rust `None` 与 `Some(0)` 的区别，对齐显式有效 seed 下的随机数初始化、整数映射和 Normal 采样；按 Rust 的 `Duration` 语义处理时限精度与零上界失败，不把不同输入压成共同默认值；用状态 trace 验证消费量。
2. **几何量化与损失状态（S17–S18、S12）。**先闭合 pair 索引、遍历顺序、epsilon、tracker 初始化/移动更新，再扩展 collision loss、hazard 分类、pole 次序和错误出口。
3. **评估、采样和搜索（S09–S16）。**逐函数闭合 evaluator 快照/lifetime 假设、排序比较、BestSamples 插入、UniformSampler 构造/样本、CoordinateDescent 数值和 Search 预算/空候选。
4. **LBF、worker、separator、Explore、Compression、编排（S03–S08）。**复现默认构造、`NoFilter` hazard 语义、worker round、失败回滚与报告、所有 Explore 状态转移、Compression 重试、阶段间 best/separatorState 交接和自然停止；不对应 Rust 的 C++ 扩展配置不得进入 parity 夹具。
- 每个子项独立建立 Rust reference、C++ comparator 和定向实现验证；前序层不通过时不把下游端到端失败归因给更高层。
- **验收：**已知 P0 差异全部变为通过的 Rust/C++ 定向回归；未稳定的 Rust 行为保留为显式域限制，不以近似相同或偶然 seed 通过关闭。

### 阶段 2：补齐边界、失败和随机轨迹覆盖

- 为阶段 1 中每个双方都能表达的已知差异建立成对用例；每个用例至少包含正常值、阈值两侧、相邻浮点值（适用时）、空/单项/多项集合和失败后状态。质量区过滤只在 Rust 对应问题模型可表达时建立 parity fixture；当前 Sparrow SPP 的 `SPInstance`/`Strip` 不承载 quality zones，C++ BPP 质量区作为扩展单独记录，不构造虚假的 Rust 输入。Tracker 用例应另覆盖非有限 loss 的权重更新、总损失和失败状态。
- 为 S06、S10、S15、S17、S18 建立 deterministic differential corpus：覆盖凸/凹宿主、轴对齐/非轴对齐、不同子件数、distinct pool 不同长度、重复/近似相等 loss、移动成功/失败和多轮回滚。用显式 seed 固定，并在首个分歧处缩减为最小 fixture。对 Rust `Ord` 非传递输入，逐消费者确认其是否出现在有效输入域；固定工具链下 Rust Debug/Release 结果如不稳定，则暂停该输入域的 parity 声明并提出共同修订方案。
- 为 worker 数、搜索预算、压缩模式、终止源和 Debug/Release 建立配置矩阵。对并行行为同时记录调度约束；不能仅因某次运行最终布局相同就断言随机流或回调等价。
- 每个新发现都必须成为可自动运行的最小 Rust oracle 和 C++ 测试；更新黄金文件时记录工具链、配置、生成方式和 SHA-256。
- **验收：**覆盖表中的每个算法分支至少有一个 Rust 导出的正例或负例；边界差异均有两侧比较；所有 differential 失败可以复现和定位。排序比较器违反 Rust `Ord` 全序要求的输入域不能用有限排列测试证明为一般等价，必须有稳定的基线行为和明确的认证域，或共同修订后重建 oracle。

### 阶段 3：完整 optimize → compress 端到端验收

- 当前阶段早停用例之外，建立 Rust/C++ 共享输入集，覆盖冷启动 `initial_solution=None`、warm-start、LBF 构造、可行与不可行初态、单/多 worker、可控时间运行和多种终止源。仅当两侧问题模型都能表示质量区时，才将质量区过滤纳入跨语言输入集；Sparrow SPP 当前不满足该条件。
- 每个用例固定输入文件、item ID、显式 seed、有效配置、feature 和终止设置；记录初始化布局、阶段顺序、best 与 separatorState、worker 结果、最终布局和错误出口。
- 精确 parity 用例使用相同的固定工作步数或测试时钟，比较 placed/remaining item 身份、可行状态、每件 transform/bounds、width、density、loss、停止原因、评估/移动/迭代计数；离散决策要求完全一致，浮点按事先记录的字段容差。
- 单独做自然时限运行，验证 Rust 9 分钟 Explore、60 秒 Compression 默认预算能正确进入、停止和收尾。自然计时受机器负载与实现速度影响，只比较阶段/停止语义、合法状态和资源边界，不要求迭代数、布局或逐事件 trace 相同；若要比较 time-based 决策轨迹，给两侧注入相同的单调时钟序列。
- **验收：**受支持输入集中所有确定性端到端 fixtures 在 Rust Debug/Release 与 C++ Debug/Release 通过；每个确定性 fixture 的最终结果可从相同输入、有效 seed 和工作预算重放；自然时限运行按独立口径验收，不要求相同最终布局或迭代数；任何 parity 不一致已转成定向回归。

### 阶段 4：公共入口、文件和产品层完整性

- S20：SparrowCli SIGINT phase-stop、原生宿主跨线程全局取消、Windows `CTRL_C_EVENT` 子进程轨迹及一次真实键盘 Release 冒烟已完成；剩余为 WASM 同步 ABI 中途取消入口；S12 的固定双 worker错误传播已闭合。
- S21：Rust 原生 listener 触发条件、默认路径的顺序/代表频率/字段、phase stop/reset、已测 callback panic/exception 与 FINAL 返回边界按 [S21 记录](records/S21-listener.md)验收。`OnProgress`/`OnImprovement` 已隔离到单独可选的 `OptimizationObserver`，严格 Rust parity 用例不传 observer；启用 observer 的扩展运行需单独验收其 stop/throw 影响。`OptimizationHistory` 保留为无 listener 交互的返回值扩展。
- S22/S23：闭合文件导入/导出 round-trip、warm-start 文件闭环、CLI 参数到有效算法配置的映射、exit code、stdout/stderr、公开错误文本、帮助信息、零时限和阶段时限语义；对照 SVG 的绘制选项（包括 surrogate）、父目录创建和输出结构。Bench 另需明确是否迁移 Rust 的任意实例、多 run/并行、分布统计和逐 run 产物；若纳入，建立 Rust 驱动的结果 parity 与独立的性能统计验收。
- S24：将 Rust TUI 暴露的算法状态、进度、日志、取消和最终结果映射到纳入目标的 C++ 宿主 UI/Demo；不要求在 SparrowCore 库内复刻 ratatui 绘制。界面实现可以不同，但不能重复执行算法、改变 seed/配置或丢失停止语义。若产品不要求迁移任何宿主 UI，则把明确排除写进最终认证范围。
- S19：对选定的 Rust feature 集完成 scalar 和 SIMD（如纳入）两套构建/参考/测试矩阵，记录平台与硬件约束。
- **验收：**对每个纳入范围的用户入口完成输入—运行—取消/失败—输出闭环；没有改变算法输入或终止行为的隐式适配。

### 阶段 5：完整回归、性能检查和发布结论

- 使用当前 RustReference 验证脚本完成 fmt、依赖锁、工具链、Debug/Release tests；运行 SparrowCore Debug/Release 全量测试及端到端 corpus。
- 运行所有 oracle fixture 并检查 golden 哈希；从失败日志提取的任何差异先复现，再修复并增加回归。版本/feature/平台变化触发对应 oracle 重跑。
- 单独做性能报告：同一输入/seed 下报告冷/热启动时间、峰值内存、worker 扩展、关键检测器调用和完整阶段耗时。若产品没有另定性能预算，本报告用于量化差异而不作为 parity 通过/失败门槛；性能数据不能替代行为 parity。
- 更新 README、Sxx 记录、迁移矩阵和本路线图，清除过期状态；最后发布的结论列出认证版本、feature、平台、输入域、测试覆盖和剩余排除项。
- **验收：**对声明范围不存在已知未解决 P0/P1 差异；全部源文件和入口有迁移结论；完整 Debug/Release 跨语言套件通过；任何 P2 排除均得到明确范围说明。

## 任务交付物与建议文件

| 交付物 | 位置 | 内容 |
|---|---|---|
| 逐函数迁移账本 | `docs/logic-comparison-sparrow/records/S01-S24` 与 README 索引 | Rust 函数/分支到 C++ 符号的映射、语义、已知差异、证据和限制 |
| 基线冻结与源指纹 | `docs/logic-comparison-sparrow/baseline-2026-10-09.md`、`baseline-2026-10-09.json`、`tools/freeze-baseline.ps1` | 记录四组件和实际 Rust 依赖源码、feature、工具链、C++ build kit、现有运行器/DLL 的逐文件哈希与生成口径 |
| 共用错误分类表 | `docs/logic-comparison-sparrow/error-semantics.md` | 对照 Rust panic/Result/None 与 C++ exception/Result/optional，记录触发条件、状态副作用、捕获边界、报告和线程/进程结果；所有 Sxx 记录和新测试共用稳定错误 ID |
| Rust oracle | `SparrowCore/Tests/RustReference/tests`、`src/bin`、测试辅助代码 | Rust 实际行为、跨 profile 边界和可复现 trace/golden 生成 |
| C++ differential 测试 | `SparrowCore/Tests/Test*.cpp` | 读取共同输入与 Rust golden，断言选择、状态、错误和字段容差 |
| 共享测试输入与 golden | `SparrowCore/Tests/Fixtures` | 输入、有效配置、显式 seed、预期事件/状态和生成来源 |
| 构建/验证入口 | Rust `verify-rust-reference.ps1` 与 SparrowCore 当前构建/测试入口 | 锁定基线的格式、构建、Debug/Release 与全量验收 |
| 本路线图 | `docs/logic-comparison-sparrow/replication-roadmap.md` | 范围、阶段、差异闭合状态、完成定义 |

## 完成定义

只有同时满足以下条件，才把结论写成“在声明范围内完整、等价复刻”：

- `CodeResource/sparrow/src` 的每个实现文件、公开入口、feature 和会影响算法的分支都有 SparrowCore 对应实现或明确的范围裁定；SparrowCore 委托的 JaguarCore API 也有固定源码/构建基线及适用的跨语言证据；没有仅凭历史计划状态标为完成的项。
- 共用错误分类表覆盖全部公开失败路径及记录到的退化输入，且每个映射都有 Rust/C++ 对照用例或明确的范围限制。
- 每一项已知行为差异均已修复并由 Rust oracle + C++ 对照测试验证，或被明示为不属于当前认证范围。Rust 自身不稳定的行为按锁定工具链实测结果限定结论。
- 已验证定向边界、随机/worker/错误出口、完整 `optimize → compress`、文件/CLI/宿主入口，以及纳入范围的 SIMD/平台构建；全部 Rust/C++ Debug 与 Release 套件通过。
- 结果、容差、有效输入域、停止条件、feature、目标平台和排除项都写入最终证据文档。未覆盖域继续标为未知，不能用“整体基本一致”替代。
- 任何基线源文件或依赖变化之后，受影响的 Rust oracle 和 C++ 对照已重跑；最终结论只描述实测过的版本和配置。

这条路线以“逐源码路径核对、先建 Rust 参考、再移植与验证”为主线。它能把差异持续缩小并让每个结论可复现，但“完整等价”最终仍必须按明确的版本、功能集、平台和输入域认证。

