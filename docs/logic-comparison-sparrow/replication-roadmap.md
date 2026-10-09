# Sparrow → SparrowCore 完整等价复刻路线图

> **执行说明：**逐个 Rust 函数和分支建立 C++ 对应实现与跨语言测试；每项变更先有可复现的 Rust 参考结果，再做最小实现并运行 Debug/Release 验证。除用户在当前任务中明确授权外，不创建 Git commit。

**目标：**在明确、固定的 Rust Sparrow 版本、功能集、目标平台和输入域内，使 SparrowCore 的可观察行为与 Rust Sparrow 等价，并为每个源码路径保留可复核的迁移和验收证据。

**实施策略：**按 README 的 Rust→C++ 文件映射逐函数、逐分支对照，实际修改按依赖顺序推进；保留 Rust 的状态转移、浮点运算域、随机数消耗次序、错误出口和停止条件。语法不机械逐行翻译；Rust 所有权、迭代器和错误类型要映射到 C++ 结构，但不能因此改变外部结果。

**技术基线：**以 2026-10-09 当前工作区及 `docs/logic-comparison-sparrow/README.md` 所列 RustReference 为起点：rustc 1.98.1、Cargo.lock 固定依赖（含 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`）、`x86_64-pc-windows-msvc`；oracle 显式关闭路径依赖默认 features，启用 jagua-rs `spp` 和 Sparrow `oracle-trace`，不启用 SIMD。当前工作区描述还不足以独立重建全部源码；阶段 0 必须登记 Sparrow 与 SparrowCore 源码修订号或逐文件清单哈希、Cargo.lock 哈希、rustc/Cargo、MSVC 工具集版本、C++ 构建类型和关键编译选项。基线变化时重跑受影响 oracle 并更新记录日期。

## 全局约束

- Rust 当前基线是目标语义。发现 Rust 自身的缺陷时，不得只在 C++ 侧修正后仍宣称等价；先固定 Rust 实际行为，或明确记录经用户认可的共同算法变更。
- 同一对照必须使用相同实例、稳定 item ID、有效配置、显式 seed、feature、profile 和停止条件。随机 seed 必须比较相同的有效 seed；不能把 Rust `None` 与 C++ 的 0 当作同一输入。未显式 seed 的生产运行按非确定性语义验收：核对熵源初始化、有效 seed 的生成/保存/暴露和 RNG 健康；逐条轨迹 parity 使用相同的有效 seed。不能要求两次独立的熵源运行产生相同布局，也不能把不同 seed 轨迹不相同当成 parity 失败。
- 测试 oracle 必须由 Rust 参考路径生成或核验。C++ 自生成基线只能用于 C++ 内部回归，不能证明 Rust/C++ parity。
- 离散选择、项目身份、事件顺序、阶段和终止原因按用例精确比较；浮点字段按各记录预先定义的字段容差比较。要求位级一致的字段必须另做 raw-bit 断言。
- Debug 与 Release 都是迁移范围。Rust panic 与 C++ exception 可以采用不同语言机制，但需比较触发条件、失败类别、失败前后的可观察状态和是否继续执行。
- 目标是对已声明的有效输入域、feature 集和平台建立完整 parity；不把有限样例表述为对数学上所有输入、所有编译器和所有硬件的证明。
- Rust `Ord` 被非传递比较违反时，必须枚举实际消费者、触发输入和 Debug/Release 行为。不得把有限排列结果推广到任意输入；若合法生产输入能触发且 Rust 行为跨 profile 不稳定，则该域在基线修订前不能声明完成，不能由 C++ 单方面定义替代结果。
- 所有可观察失败按共用错误分类表比较：触发条件、Rust panic/Result/None 与 C++ exception/Result/optional 的映射、失败前后的布局和计数、捕获边界、listener 事件、线程/进程是否退出及是否继续下一阶段。语言级文本只有在属于公开接口时才要求逐字一致。

## 完整复刻的认证范围

本路线按用户提出的“完整、等价复刻”采用完整外部行为范围，不把已有的 TUI/Demo 或 CLI 差异默认为排除项：S01–S19 的算法与支持 feature、S20–S21 的停止和报告、S22–S23 的文件/CLI/Bench 输入输出、S24 的 TUI/Demo 算法状态和交互控制都要有结论。UI 绘制技术和视觉皮肤可以不同；必须一致的是其传递给算法的输入、seed、运行预算、进度/取消行为和用户可观察的结果。当前 scalar RustReference 是最低认证 profile；若产品支持 Rust opt-in SIMD 或其他 Rust feature，每个受支持 feature 集都要建立独立 oracle 和 C++ 对照。阶段 0 必须逐项标注“必需并验收”或“经用户确认排除”，其余章节统一引用该清单。

外部输出的比较口径也要固定：CLI exit code、解析后的有效配置和停止原因必须精确一致；JSON 按解析后的结构、稳定 item ID 和离散字段精确比较，浮点字段沿用预先登记的字段容差，要求位级一致时比较 raw bits，只有序列化字节格式属于公开契约时才要求字节相同；SVG 按几何、ID 和所需标签比较，浮点坐标沿用记录容差，只有格式本身是契约时才要求字节相同；stdout/stderr、错误文本和帮助信息按明确的公开接口逐项比较；TUI/Demo 比较报告、日志、进度、取消和最终状态，不要求像素级一致。

## 当前完成度与剩余缺口

现有记录已覆盖 Rust Sparrow 的 24 个比对单元和 42 个 `.rs` 文件（40 个实现/应用文件、2 个嵌套 TUI 测试文件），并逐模块记录 C++ 对应路径和差异。RustReference 的 Debug、Release 各运行 72 个集成测试函数，SparrowCore Debug、Release 全量测试程序通过。当前共有 21 条实际 Explore 跨语言轨迹；第一组独立几何和第二组 H/I 几何均覆盖多个 seed，H/I seed=10–14 的 Rust Debug/Release 重复运行共 50/50 匹配 golden。S06 数值边界、随机采样、双侧 contained relocation、Compression 部分路径、worker move 和若干终止交接都有定向证据。四实例 LBF 构造对照中的 64/0/1 指 `nContainerSamples=64`、`nFocussedSamples=0`、`nCoordinateDescents=1`；Rust Sparrow 默认 `LBF_SAMPLE_CONFIG` 为 1000/0/3，即 1000 个 container samples、0 个 focused samples、3 次 coordinate descent。

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
| [S01 配置](records/S01-config.md)、[S02 常量/RNG](records/S02-consts-lib.md) | `None` seed 与零 seed 不同；sub-ms 时限映射不无损；`UniformIndex(0)` 的失败机制不同；随机流虽有固定 trace，仍需核对入口组合的消耗顺序 | 定义有效 seed 和计时输入的共同映射；对零上界、无 seed、边界 seed、采样调用序分别做 Rust oracle 与 C++ 状态/错误对照 | P0 |
| [S03 LBF](records/S03-lbf.md)、[S04 Separator](records/S04-separator.md)、[S05 Worker](records/S05-worker.md) | 默认 LBF 1000/0/3（1000 container、0 focused、3 coordinate descents）、累计宽度尝试及 focused 字段仍有差异；初始已分离时空 worker round/progress 不同；C++ best sample 可空并跳过 | 对正常默认、构造失败、零/小预算、初始已分离、无 best 候选、1/2/多 worker 分别比较选择、布局、计数、回调和失败状态 | P0 |
| [S06 Explore](records/S06-explore.md) | 已有 21 条实际 trace 和多项边界修复，但仍未证明其他几何、候选池长度、随机流、完整默认预算下的 disruption/relocation 与最终布局 | 按 [S06 记录](records/S06-explore.md) 中的状态转移和事件字段清单继续覆盖未测路径；扩展 host/child 形状、离散角、contained 子件数、候选池长度、seed 与失败轮数；逐事件记录 pool、RNG、swap、move、failure snapshot 和最终状态 | P0 |
| [S07 Compression](records/S07-compress.md)、[S08 编排](records/S08-optimizer-mod.md) | stopwatch 起点、`f32`/`double`、毫秒量化和直接 `timeLimit=0` 仍不同；自然完整时限、一般错误出口和其他配置下完整阶段结果未证 | 对 warm/cold start、各 shrink mode、直接阶段入口、deadline 前后和错误退出分别对照；固定时钟/回调触发时点，并核对当前尝试是否完成、阶段交接、最终布局和报告 | P0 |
| [S09 LBF evaluator](records/S09-lbf-evaluator.md)、[S10 SampleEval](records/S10-sample-eval.md) | C++ evaluator 使用布局快照，只有生命周期内布局不变才与 Rust live borrow 等价；Rust `Ord` 非传递风险使任意序列不具一般排序保证；NaN 比较的失败机制不同 | 对 evaluator 生命周期内布局变化做用例；枚举所有排序消费者和稳定性要求；在锁定 Rust 工具链下扩展触发序列、相等值和 NaN 错误前后状态。若合法输入下 Rust 跨 profile 不稳定，则先限定可认证输入域或提出共同算法修订，不得只凭有限排列声称一般等价 | P0 |
| [S11 Separator evaluator](records/S11-sep-evaluator.md)、[S12 Collision loss](records/S12-collision-loss.md) | 指定 hazard、pole 顺序和早停路径已对齐；其他几何/pole 集合、Exterior hazard、上界邻域以及全部 unsupported hazard 组合仍不足 | 建立多 hazard 次序、多个 pole、Exterior、上界相邻浮点值和不支持 hazard 的成对 oracle；核对分类、首个早停项、累计损失、失败状态和 Rust panic/C++ exception 的映射 | P0 |
| [S13 Search](records/S13-search.md)、[S14 Coordinate descent](records/S14-coord-descent.md)、[S15 BestSamples](records/S15-best-samples.md) | 固定零预算和 32 项非传递序列已覆盖；S14 的 5°、0.05° 转弧度值有 1 ULP 差异且消费者影响未测；其他搜索预算、插入序列和容量/相似度组合未证明 | 用跨边界角度直接比较转换值及选择结果；扩展搜索预算、reference placement、空候选和混合相似度插入序列；每一步比较保留项、状态、loss bits、评估次数和 RNG 状态 | P0 |
| [S16 Uniform sampler](records/S16-uniform-sampler.md) | Rust 接受 singleton bbox 而 C++ 拒绝；C++ 多类构造错误被折叠为 no-fit；现有 raw-bit 证据只覆盖指定区间、角度和 seed | 覆盖 singleton X/Y/双轴、极窄区间、invalid bbox/rotation/非有限变换、多 seed 和后续搜索消费；分别比较成功样本、错误类别、随机消耗与布局影响 | P0 |
| [S17 PairMatrix](records/S17-overlap-pair.md)、[S18 Tracker](records/S18-tracker.md) | PairMatrix 遍历/累加次序不同且 C++ overlap API 有默认 epsilon；Rust tracker 初建行清零、C++ 只写命中项，在非对称检测器下可能残留状态；任意移动序列未覆盖 | 穷举小 N 合法 pair 映射与 Rust 逻辑遍历顺序；测试默认/显式 epsilon；以固定随机 move 序列逐步比较全部 loss、weight、weighted loss 和未移动行，覆盖 Debug/Release | P0 |
| [S19 SIMD](records/S19-simd.md) | 当前 RustReference 固定为不启用 SIMD；Rust SIMD 是 opt-in，C++ AVX2/SoA 路径没有 parity fixture | 阶段 0 列明产品支持的 Rust feature 集；scalar 是最低目标，每个实际支持的 SIMD/其他 feature 集均单独冻结构建条件并比较候选、早停、损失和容差。仅未支持的 feature 可列为经确认的范围排除 | P2 |
| [S20 Terminator](records/S20-terminator.md)、[S21 Listener](records/S21-listener.md) | 固定交接和 phase-stop 有证据；OS 信号接线、更多报告类型/回调频率/错误出口未覆盖 | 对全局 stop、phase stop、重置、Ctrl-C/宿主取消及每类 listener 事件分别验证顺序、计数、线程可见状态和阶段间传播；确认观察回调不会改变算法结果 | P1 |
| [S22 IO](records/S22-util-io.md)、[S23 CLI/Bench](records/S23-cli-bench.md)、[S24 TUI/Demo](records/S24-tui.md) | warm-start 文件级闭环缺失；CLI 全局与分阶段时限、零值和计时起点不同；浏览器 Demo 对 JSON/SVG 分别完整运行 Optimize，与 Rust TUI 的交互模型不相同 | 用相同文件和参数完成导入—有效配置—优化—导出—再读回；核对退出码/错误/时限/seed；修正 Demo 重复运行或证明单次算法结果复用；实现语义相同的状态、取消与输出行为 | P1 |

## 分阶段执行路线

### 阶段 0：冻结范围和迁移账本

- 再确认 Rust 源码快照、RustReference 锁、feature、target、C++ 构建配置和实际对外支持平台；把版本信息写入本路线图和 README。
- 以 README 的 S01–S24 映射为唯一清单，为全部 Rust 函数/测试/公共入口补齐 Rust→C++ 目标、迁移状态、差异 ID、oracle 和调用者。把当前 `records/` 中已闭合与未闭合结论同步到矩阵。
- 把 parity 输入域分为：有效普通输入、边界/退化输入、非法输入、随机/时限运行、宿主入口；为每类明确需要匹配的输出和错误语义。
- **验收：**登记 Rust/SparrowCore 源码修订号或逐文件清单哈希、Cargo.lock 哈希、jagua-rs revision、rustc/Cargo/MSVC 版本、C++ build configuration/defines 和 feature 集；所有 42 个 `.rs` 文件均有映射（实现/应用文件对应到 C++ 符号，嵌套测试文件对应到测试覆盖）；认证范围清单对算法模块、CLI、IO、Bench、TUI/Demo、SIMD/其他 feature、平台逐项标记必需或经确认排除；建立共用错误分类表，覆盖 panic/Result/None/exception/optional 映射、状态副作用、捕获边界及线程/进程结果；没有无说明的 orphan 函数、feature 或入口。

### 阶段 1：按依赖顺序闭合核心语义差异

按 `migration-decision-matrix.md` 的依赖顺序推进，不同时改动互相依赖的算法层：

1. **配置与随机数（S01–S02）。**统一有效 seed、随机数初始化、整数映射、Normal 采样、时限类型和零上界错误；用状态 trace 验证消费量。
2. **几何量化与损失状态（S17–S18、S12）。**先闭合 pair 索引、遍历顺序、epsilon、tracker 初始化/移动更新，再扩展 collision loss、hazard 分类、pole 次序和错误出口。
3. **评估、采样和搜索（S09–S16）。**逐函数闭合 evaluator 快照/lifetime 假设、排序比较、BestSamples 插入、UniformSampler 构造/样本、CoordinateDescent 数值和 Search 预算/空候选。
4. **LBF、worker、separator、Explore、Compression、编排（S03–S08）。**复现默认构造、worker round、失败回滚与报告、所有 Explore 状态转移、Compression 重试、阶段间 best/separatorState 交接和自然停止。
- 每个子项独立建立 Rust reference、C++ comparator 和定向实现验证；前序层不通过时不把下游端到端失败归因给更高层。
- **验收：**已知 P0 差异全部变为通过的 Rust/C++ 定向回归；未稳定的 Rust 行为保留为显式域限制，不以近似相同或偶然 seed 通过关闭。

### 阶段 2：补齐边界、失败和随机轨迹覆盖

- 为阶段 1 中每个已知差异建立成对用例；每个用例至少包含正常值、阈值两侧、相邻浮点值（适用时）、空/单项/多项集合和失败后状态。
- 为 S06、S10、S15、S17、S18 建立 deterministic differential corpus：覆盖凸/凹宿主、轴对齐/非轴对齐、不同子件数、distinct pool 不同长度、重复/近似相等 loss、移动成功/失败和多轮回滚。用显式 seed 固定，并在首个分歧处缩减为最小 fixture。对 Rust `Ord` 非传递输入，逐消费者确认其是否出现在有效输入域；固定工具链下 Rust Debug/Release 结果如不稳定，则暂停该输入域的 parity 声明并提出共同修订方案。
- 为 worker 数、搜索预算、压缩模式、终止源和 Debug/Release 建立配置矩阵。对并行行为同时记录调度约束；不能仅因某次运行最终布局相同就断言随机流或回调等价。
- 每个新发现都必须成为可自动运行的最小 Rust oracle 和 C++ 测试；更新黄金文件时记录工具链、配置、生成方式和 SHA-256。
- **验收：**覆盖表中的每个算法分支至少有一个 Rust 导出的正例或负例；边界差异均有两侧比较；所有 differential 失败可以复现和定位。排序比较器违反 Rust `Ord` 全序要求的输入域不能用有限排列测试证明为一般等价，必须有稳定的基线行为和明确的认证域，或共同修订后重建 oracle。

### 阶段 3：完整 optimize → compress 端到端验收

- 当前阶段早停用例之外，建立 Rust/C++ 共享输入集，覆盖冷启动 `initial_solution=None`、warm-start、LBF 构造、可行与不可行初态、单/多 worker、可控时间运行和多种终止源。
- 每个用例固定输入文件、item ID、显式 seed、有效配置、feature 和终止设置；记录初始化布局、阶段顺序、best 与 separatorState、worker 结果、最终布局和错误出口。
- 精确 parity 用例使用相同的固定工作步数或测试时钟，比较 placed/remaining item 身份、可行状态、每件 transform/bounds、width、density、loss、停止原因、评估/移动/迭代计数；离散决策要求完全一致，浮点按事先记录的字段容差。
- 单独做自然时限运行，验证 Rust 9 分钟 Explore、60 秒 Compression 默认预算能正确进入、停止和收尾。自然计时受机器负载与实现速度影响，只比较阶段/停止语义、合法状态和资源边界，不要求迭代数、布局或逐事件 trace 相同；若要比较 time-based 决策轨迹，给两侧注入相同的单调时钟序列。
- **验收：**受支持输入集中所有确定性端到端 fixtures 在 Rust Debug/Release 与 C++ Debug/Release 通过；每个确定性 fixture 的最终结果可从相同输入、有效 seed 和工作预算重放；自然时限运行按独立口径验收，不要求相同最终布局或迭代数；任何 parity 不一致已转成定向回归。

### 阶段 4：公共入口、文件和产品层完整性

- S20/S21：闭合 OS 信号或宿主取消的接线、listener 所有报告类型的字段/顺序/频率和 phase reset 后状态。
- S22/S23：闭合文件导入/导出 round-trip、warm-start 文件闭环、CLI 参数到有效算法配置的映射、exit code、stdout/stderr、公开错误文本、帮助信息、零时限和阶段时限语义。JSON/SVG 按认证范围中的外部输出规则比较。
- S24：将 Rust TUI 暴露的算法状态、进度、日志、取消和最终结果映射到 SparrowCore 的宿主 UI/Demo。界面实现可以不同，但不能重复执行算法、改变 seed/配置或丢失停止语义。若产品不要求迁移 TUI，则把明确排除写进最终认证范围。
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
| 共用错误分类表 | `docs/logic-comparison-sparrow/error-semantics.md` | 对照 Rust panic/Result/None 与 C++ exception/Result/optional，记录触发条件、状态副作用、捕获边界、报告和线程/进程结果 |
| Rust oracle | `SparrowCore/Tests/RustReference/tests`、`src/bin`、测试辅助代码 | Rust 实际行为、跨 profile 边界和可复现 trace/golden 生成 |
| C++ differential 测试 | `SparrowCore/Tests/Test*.cpp` | 读取共同输入与 Rust golden，断言选择、状态、错误和字段容差 |
| 共享测试输入与 golden | `SparrowCore/Tests/Fixtures` | 输入、有效配置、显式 seed、预期事件/状态和生成来源 |
| 构建/验证入口 | Rust `verify-rust-reference.ps1` 与 SparrowCore 当前构建/测试入口 | 锁定基线的格式、构建、Debug/Release 与全量验收 |
| 本路线图 | `docs/logic-comparison-sparrow/replication-roadmap.md` | 范围、阶段、差异闭合状态、完成定义 |

## 完成定义

只有同时满足以下条件，才把结论写成“在声明范围内完整、等价复刻”：

- `CodeResource/sparrow/src` 的每个实现文件、公开入口、feature 和会影响算法的分支都有 SparrowCore 对应实现或明确的范围裁定；没有仅凭历史计划状态标为完成的项。
- 共用错误分类表覆盖全部公开失败路径及记录到的退化输入，且每个映射都有 Rust/C++ 对照用例或明确的范围限制。
- 每一项已知行为差异均已修复并由 Rust oracle + C++ 对照测试验证，或被明示为不属于当前认证范围。Rust 自身不稳定的行为按锁定工具链实测结果限定结论。
- 已验证定向边界、随机/worker/错误出口、完整 `optimize → compress`、文件/CLI/宿主入口，以及纳入范围的 SIMD/平台构建；全部 Rust/C++ Debug 与 Release 套件通过。
- 结果、容差、有效输入域、停止条件、feature、目标平台和排除项都写入最终证据文档。未覆盖域继续标为未知，不能用“整体基本一致”替代。
- 任何基线源文件或依赖变化之后，受影响的 Rust oracle 和 C++ 对照已重跑；最终结论只描述实测过的版本和配置。

这条路线以“逐源码路径核对、先建 Rust 参考、再移植与验证”为主线。它能把差异持续缩小并让每个结论可复现，但“完整等价”最终仍必须按明确的版本、功能集、平台和输入域认证。
