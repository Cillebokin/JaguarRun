# sparrow（Rust）与 SparrowCore（C++）代码逻辑全景

## 总览

本文档覆盖两个相关项目：sparrow 是基于 jagua-rs 碰撞引擎的 2D 不规则条带排料启发式求解器（MIT 许可），SparrowCore 是其纯 C++ 移植，目标是与 Rust 参考实现在固定种子下做轨迹级对拍（MPL/MIT 许可边界见 `SparrowCore/docs/SourceMap.md`）。两者共享一条主线：先用 LBF（Left-Bottom-Fill）贪心构造初始可行解，再进入 exploration 阶段（逐步收缩条带宽度，允许不可行状态，靠 separator 修复重叠），最后进入 compression 阶段（在可行前提下压缩）。Separator 通过 CollisionTracker 维护物品对与容器的重叠损失，并以权重（weight）引导移动；采样与坐标下降负责为单个物品寻找低损失位姿。

阅读顺序建议：先读 S1 理解从命令行到 `Optimize` 的入口与配置；再读 S2 把握 exploration、separator 与 compression 的控制流；随后读 S3（sparrow 的采样、评估与量化）或 S5（SparrowCore 对应部分）理解每一步的评分函数；S4 讲 SparrowCore 优化器层与 Rust 的差异；S6 给出工程结构、测试分层与发布门禁。

## 需要特别注意的结论

第一，Separator 的终止条件是 strike 计数而非单纯迭代上限：每轮 strike 结束时若初始损失 initial_strike_loss * 0.98 <= min_loss 则增加 strike，否则清零（sparrow/src/optimizer/separator.rs 约 172–177 行）；外层循环同时受 strike_limit 与 kill() 约束，并在结束时回滚到记录的最小损失解（约 179 行）。第二，exploration 的收缩步长为 current_width * (1.0 - config.shrink_step)（sparrow/src/optimizer/explore.rs 约 195 行）。第三，SparrowCore 测试以固定种子回归为主，容差比较和逐位比较须按字段及测试分别说明。第四，CLI 与两个 WASM C ABI 导出已有异常边界，CLI 异常退出码为 10，WASM 异常返回码为 2。第五，发布清单已纳入 Ctrl-C、Demo 与 C++/Rust 比较；当前待办集中在 oracle fixture 复核、重复大小写目录漂移、路径假设及尚未执行的 Demo 验收。

## 验证说明

本文主体基于源码阅读与静态检查。10 月 16 日的 Windows MSVC 2019 x64 / Qt 5.12.10 Debug、Release 与 CLI 执行记录见移植工作项清单第十四节，记录由用户提供，本次文档修订没有独立复跑。当前结论须区分源码核对、fixture 对照和运行记录：默认 cold-start 的 C++ 测试使用 compression phase deadline override，而 Rust fixture 仍需去除墙钟截止时间后重生成和复核；浏览器 Demo 也尚未运行。


# sparrow 入口、配置、CLI、IO 与工具模块代码逻辑说明

本文覆盖 `src/main.rs`、`src/lib.rs`、`src/cli.rs`、`src/config.rs`、`src/consts.rs`、`src/util/**`，浏览 `src/bench.rs`，并说明 `Cargo.toml` 中 features 的含义以及 `README.md` 的用法部分。jagua-rs 的内部实现不展开，只说明 sparrow 调用了它的哪些接口。文中路径相对于仓库根 `sparrow/`，行号以当前源码为准。

## 一、版本前提

阅读前需要先区分源码镜像与 oracle 锁定基线。CodeResource/sparrow/Cargo.toml:18 把 jagua-rs 指向 git 分支 jg-explicit-rotations-v0.9，而同目录 README.md:106-107 写的是 jg-container-identity。本地 jagua-rs 镜像的 Cargo.toml 版本为 0.9.0、HEAD 为 develop 的 65dffaaf；源码接口也与 Sparrow 的调用不同（src/optimizer/mod.rs:47-48 调用 .expect(...)，本地 src/probs/spp/entities/problem.rs:20 直接返回 Self）。因此本文的源码说明基于本地镜像，RustReference 实际依赖基线以其 Cargo.lock 为准。CodeResource/sparrow crate 根目录没有 Cargo.lock；SparrowCore 与 JaguarCore 的 oracle 目录各自有锁文件，当前配置锁定 jagua-rs revision f5e2b5f。本地镜像与 pinned revision 的差异仍按 D5/E4 复核。

## 二、整体流程概览

程序入口是 `src/main.rs:11-13`，它的 `main` 只调用 `run()`。由于 `main.rs:1-9` 用 `cfg(feature = "tui")` 二选一地引入模块，默认构建（不启用 `tui`）使用 `cli::run`，启用后使用 `tui::run`。两条路径都返回 `anyhow::Result<()>`，错误由 `main` 直接冒泡并以非零码退出。

默认路径 `cli::run`（`src/cli.rs:29-141`）的执行顺序可以概括为：创建 `output/` 目录并初始化日志；解析命令行；根据时间参数算出探索与压缩时长；叠加早停与命令行覆盖项；生成随机数发生器；读取输入文件并解析为 jagua-rs 的外部表示；用 `Importer` 导入为内部 `SPInstance`；若输入中含有解则导入为初始解；构造 `SvgExporter` 与 `CtrlCTerminator`；调用 `optimizer::optimize`；最后把返回的解导出为 JSON。真正的搜索逻辑在 `src/optimizer/mod.rs:28-81` 的 `optimize` 中，它先构造或恢复初始解，再依次运行 exploration 与 compression 两个阶段。CLI 层做的全部是准备与收尾工作。

## 三、lib.rs 与全局常量

`src/lib.rs:1` 通过 `cfg_attr(feature = "simd", feature(portable_simd))` 打开 nightly 的 portable SIMD，因此 `simd` feature 必须在 nightly 工具链下编译。`lib.rs:2-3` 关闭了 `const_item_mutation` 与 `unused_imports` 两类警告，这会掩盖一些本应被提示的问题，阅读时要留意。

`lib.rs:17` 定义了 `pub static EPOCH: LazyLock<Instant>`，它是全程的时间零点。`Instant` 来自 jagua-rs 重导出的 `web_time::Instant`（jagua-rs `src/lib.rs:36`）。`EPOCH` 有两个用途：日志前缀里的相对时间（`io.rs:94`），以及输出 JSON 中 `run_time_sec` 的计算（`cli.rs:136`）。`EPOCH` 是惰性初始化的，它第一次被访问发生在 `init_logger` 写入 `[EPOCH]` 那一行时，因此时间零点近似等于日志初始化完成的时刻，而非进程启动时刻。

`lib.rs:19-23` 的 `FMT` 生成三位有效数字的数字格式，用于日志中的损失值与宽度。`lib.rs:25-35` 定义了两个布尔常量 `EXPORT_LIVE_SVG` 与 `EXPORT_ONLY_FINAL_SVG`，它们由 features 决定。但在全仓库检索中，这两个常量只被定义、没有任何地方读取，实际的 SVG 行为是由 `cli.rs:108-116` 直接用 `cfg!(feature = ...)` 判断的。这两个常量目前是死代码，修改其中一个并不会改变行为。`lib.rs:37-38` 用 `compile_error!` 禁止同时启用 `live_svg` 与 `only_final_svg`，这一约束是在库 crate 中强制的，因此对 bin 与 bench 同样生效。

## 四、命令行解析

命令行结构定义在 `src/util/io.rs:15-61` 的 `MainCli` 中，由 clap derive 生成。参数包括：`-i/--input`（必填，instance 或 solution 的 JSON 路径）、`-t/--global-time`、`-e/--exploration`、`-c/--compression`、`-x/--early-termination`、`-s/--rng-seed`（u64）以及 `--workers`（`NonZeroUsize`）。时间参数之间的约束写在属性中：`-t` 与 `-e`、`-c` 互斥（`io.rs:26`），`-e` 要求 `-c` 同时出现（`io.rs:33`），`-c` 要求 `-e`（`io.rs:42`）。因此合法组合只有三种：只给 `-t`；`-e` 与 `-c` 同时给；全部不给。`cli.rs:54` 的 `bail!` 理论上不可达，注释也承认这一点。

README 的 CLI 列表（`README.md:72`）列出了 `--min-item-separation`，但 `MainCli` 中并不存在这个参数。`README.md:110-111` 又说明 CLI 与求解器配置已不再覆盖该值，因此 README:72 那一行属于残留文档，应当删除。

## 五、日志与输出目录的初始化顺序

`cli.rs:32-37` 先调用 `fs::create_dir_all("output")`，再初始化日志，然后才在 `cli.rs:39` 调用 `MainCli::parse()`。这个顺序带来一个副作用：执行 `sparrow --help` 也会创建 `output/` 目录，并删除旧的 `output/log.txt`。`init_logger`（`io.rs:85-117`）第一行就是 `fs::remove_file`，因此每次运行都会覆盖上一次的日志，而不是追加。日志路径是相对于当前工作目录的，而不是相对于可执行文件，从别的目录启动会在别处生成输出。

日志级别由编译配置决定。`cli.rs:34-37` 在 `debug_assertions` 开启时使用 `LOG_LEVEL_FILTER_DEBUG`，否则使用 `LOG_LEVEL_FILTER_RELEASE`（均定义在 `consts.rs:37-39`，分别为 Debug 与 Info）。日志同时写入 stdout 与文件，格式为 `[LEVEL] [HH:MM:SS] <线程名>` 加消息（`io.rs:90-109`）。`config.rs` 中探索阶段分离器的 `log_level` 为 Info（`config.rs:61`），压缩阶段为 Debug（`config.rs:78`），所以 release 构建下压缩阶段分离器的详细日志会被过滤掉。

## 六、参数到配置的映射

所有配置的起点是 `config.rs:51-98` 的常量 `DEFAULT_SPARROW_CONFIG`，它是 `Copy` 类型，`cli.rs:30` 复制一份后再修改。对 CLI 用户来说，其中有一部分默认值会被立刻覆盖。时间限制是典型例子：`config.rs:55` 中探索的 `time_limit` 为 9 分钟，`config.rs:73` 中压缩为 60 秒，但 `cli.rs:56-57` 总是在之后用命令行得出的值覆盖，因此这两个数字对 CLI 没有任何作用，只对直接调用库且不设置时间的用户有意义。

时间的换算规则在 `cli.rs:41-55`。`-t T` 时探索得到 `T × 0.8`、压缩得到 `T × 0.2`，比例来自 `consts.rs:30-31`。`-e E -c C` 时直接取 `E` 与 `C`。两者都不给时使用 600 秒作为总时间（`cli.rs:47-53`），并输出一条 warn 日志，换算后即 480 秒探索加 120 秒压缩。注意 README:60-62 写的 80%/20% 只适用于 `-t` 的情况，用 `-e/-c` 时比例由用户决定。

`-x` 早停（`cli.rs:58-63`）同时修改两个阶段的配置。探索阶段设置 `max_conseq_failed_attempts = Some(10)`（`consts.rs:33`），即当前宽度下连续 10 次分离失败就结束探索。压缩阶段将 `shrink_decay` 改为 `FailureBased(0.9)`（`consts.rs:35`）。`tui.rs:90-94` 有完全相同的逻辑，两条路径保持一致。

`apply_config_overrides`（`io.rs:63-76`）只覆盖两项：`--rng-seed` 写入 `config.rng_seed`，`--workers` 同时写入探索与压缩两个分离器的 `n_workers`。它在早停之后执行，但两者互不影响。

配置结构的各字段含义如下。`ExplorationConfig`（`config.rs:25-33`）包含 `shrink_step`（默认 0.001，每次探索成功后宽度乘以 `1 - shrink_step`，见 `explore.rs` 中 `next_width` 的计算）、`solution_pool_distribution_stddev`（默认 0.25，用于从不可行解池中按正态分布选择回滚点，见 `explore.rs:251`）、`large_item_ch_area_cutoff_percentile`（默认 0.75，决定扰动时哪些物品被视为"大件"，见 `explore.rs:342`）以及 `separator_config`。`CompressionConfig`（`config.rs:35-41`）包含 `shrink_range`（默认 `(0.0005, 0.00001)`）、`shrink_decay`（默认 `TimeBased`）和 `separator_config`。`SeparatorConfig` 的字段（`optimizer/separator.rs:20-30`）包括 `iter_no_imprv_limit`（探索 200，压缩 100，即一轮内无改进的迭代上限）、`strike_limit`（探索 3，压缩 5，即外层重试次数上限）、`n_workers`（默认 3）以及 `SampleConfig`（容器采样 50、聚焦采样 25、坐标下降 3）。

`SampleConfig` 在 `sample/search.rs` 中定义，`consts.rs:50-54` 的 `LBF_SAMPLE_CONFIG` 是初始构造阶段专用的版本，容器采样数为 1000，聚焦采样为 0。`consts.rs:4-28` 中的其余常量（GLS 权重的增减比例与衰减、重叠代理的 epsilon、坐标下降的步长与旋转步长、唯一性判断阈值等）属于分离器与采样模块的内部参数，它们不暴露到 CLI，修改它们等于修改算法本身。

`cde_config` 与两个几何参数决定了 jagua-rs 的内部行为。`CDEConfig`（`config.rs:87-95`）的 `quadtree_depth` 为 4，`cd_threshold` 为 64，含义是四叉树最大深度以及节点边数低于阈值时直接做碰撞收集（见 jagua-rs `cd_engine.rs:404-411`）。`item_surrogate_config` 是物品替代形状（surrogate）的生成参数，`n_pole_limits` 为 `[(64, 0.0), (16, 0.8), (8, 0.9)]`，表示覆盖率低于 80% 时最多 64 个极点，之后按覆盖率提高而收紧。`poly_simpl_tolerance = Some(0.001)`（`config.rs:96`）允许物品形状简化时向外膨胀的最大相对误差；`narrow_concavity_cutoff_ratio = Some((0.01, 0.01))`（`config.rs:97`）会闭合距离与面积都低于阈值的窄凹口。两者都只作用于物品而不作用于容器（jagua-rs `probs/spp/io/import.rs:51-56`），因此最终解所依据的几何已是修改后的形状，与原始 SVG 之间存在微小差别，这在对外交付时需要说明。

## 七、输入读取与 JSON 格式

输入解析在 `src/util/io.rs:150-162` 的 `read_spp_input` 中完成，它先把整个文件读成字符串，然后尝试把它解析为 `ExtSPOutput`（`io.rs:78-83`）。这个结构用 `#[serde(flatten)]` 把 `ExtSPInstance` 的字段摊平在顶层，并额外要求一个 `solution` 字段。若成功，就同时得到 instance 与 solution，即进入"warm start"分支。若失败，则回退为只解析 `ExtSPInstance`，成功则返回 `(instance, None)`，进入从零构造的分支。因此 sparrow 的输出 JSON 可以直接作为下一次运行的输入，这是迭代求解的标准用法。

这里有一个容易踩的坑。第一次解析失败的错误信息被 `Err(_)` 直接丢弃（`io.rs:155`），回退路径只报告"could not parse instance from input file"。如果用户提供的是一个格式错误的 solution 文件（instance 部分正确），报错会误导成 instance 有问题，真正的原因（solution 字段缺失或内部格式错误）被隐藏。另外，文件会被解析两次，对大文件有额外开销。

instance JSON 的结构由 jagua-rs 的 `ExtSPInstance`（`jagua-rs/src/probs/spp/io/ext_repr.rs:6-19`）定义：`name`、可选的 `min_item_separation`（缺省为 0，`#[serde(default)]`，null 非法）、`items` 数组以及 `strip_height`。每个物品由 `ExtItem` 扩展而来（`ext_repr.rs:22-28`），包含基础字段 `id`、`orientation.rotation`（`mode` 为 `discrete`、`stepped` 或 `continuous`，见 `jagua-rs/src/io/ext_repr.rs:21-41`）、`shape` 以及 `demand`。solution JSON 则包含 `strip_width`、`layout.placed_items`（每项有 `item_id` 与 `transformation`）、`density` 与 `run_time_sec`（`ext_repr.rs:32-40`）。输出 JSON 由 `cli.rs:133-138` 的 `ExtSPOutput` 写出，形式是 instance 的所有字段加上一个顶层 `solution` 对象。

## 八、构造 jagua-rs 的 Importer 与 Instance

`cli.rs:88-93` 先用 `cde_config`、`poly_simpl_tolerance` 与 `narrow_concavity_cutoff_ratio` 调用 `Importer::new`（jagua-rs `src/io/import.rs:30-43`），它只保存形状修改与碰撞检测的配置，并不做实际导入。接着调用 `jagua_rs::probs::spp::io::import_instance(&importer, &ext_instance)`，该函数的行为在 jagua-rs `src/probs/spp/io/import.rs:12-60` 中可以核对：它首先调用 `importer.with_min_item_separation(ext_instance.min_item_separation)`（`import.rs:13`），因此 `min_item_separation` 完全由 instance JSON 决定，sparrow 不做覆盖。其含义是物品向外膨胀半个间距、容器向内收缩半个间距（`import.rs:45-54`）。随后它校验物品 `id` 唯一（`import.rs:14-21`），并把 `demand` 为 0 的物品过滤掉（`import.rs:25`）。这意味着过滤后物品的内部下标与外部 `id` 不再相同，`instance.item_idx(id)` 这类映射才是正确的查找方式，`cli.rs` 本身不处理这件事。

导入后的初始 strip 宽度由 `total_item_area / strip_height` 计算（`import.rs:38-46`），即 100% 密度对应的宽度，之后的所有搜索都从这一宽度开始向内收缩。sparrow 日志中打印的"density"即以此为基准。`cli.rs:101-103` 用 `instance.total_item_qty()` 打印物品总件数，该方法定义在 `jagua-rs/src/probs/spp/entities/instance.rs:43`。

若输入含有解，`cli.rs:95-97` 调用 `import_solution`（jagua-rs `import.rs:63-80`）。它先 `change_strip_width(ext_solution.strip_width)`，再逐个放置 `placed_items`，未知的 `item_id` 会报错（`import.rs:68-70`），最后 `prob.save()` 返回 `SPSolution`。`density` 与 `run_time_sec` 字段在导入时被忽略。

## 九、初始解与 warm start 分支

`optimizer/mod.rs:39-52` 决定起点。若没有初始解，调用 `LBFBuilder::new(instance, next_rng(), LBF_SAMPLE_CONFIG).construct()?`，用 LBF（最低左下角）启发式构造一个可行布局，失败时返回 `ConstructionError`（`optimizer/lbf.rs:16-30`），经 `?` 与 anyhow 一起冒泡为程序错误。若提供了初始解，则 `SPProblem::new(instance)` 后调用 `prob.restore(init_sol)`（`mod.rs:45-51`）。jagua-rs 的 `restore`（`problem.rs:94-111`）会先尝试按快照恢复布局，失败则从快照重建，然后按实例重新计算每种物品的剩余需求量，再逐个递减已放置物品的计数。

这里有两个隐含假设需要说明。其一，sparrow 不检查 warm start 的解是否可行。`explore.rs:164` 直接把 `sep.prob.save()` 作为 `feasible_sols[0]`，并在 `explore.rs:166` 以 `ReportType::ExplFeas` 上报，即默认它是可行的。如果输入的解本身有重叠，它会被当作可行解参与后续压缩，最终输出可能不可行。其二，`restore` 中的需求量递减使用 `usize` 的 `-= 1`（`problem.rs:110-111`），若输入解放置的某类物品数超过 instance 的 demand，会发生下溢。在 dev/test 配置下（`overflow-checks = true`，`Cargo.toml:54` 与 `68`）会 panic，在 release 下则会回绕。这些检查属于 jagua-rs 本地副本的行为，应结合实际链接的版本确认。

`optimizer/mod.rs:55-58` 之前，`terminator.new_timeout` 还没有被调用，所以 LBF 构造阶段不计入时间预算，并且 `CtrlCTerminator::kill` 在此阶段也不会被检查（LBF 的 `construct` 中没有调用 `kill`）。

## 十、optimize 主流程

`optimize`（`optimizer/mod.rs:28-81`）的执行次序是：构造起点；`report_phase(Exploration)`；`terminator.new_timeout(expl_config.time_limit)`；用探索配置构造 `Separator`，调用 `exploration_phase`，取返回向量的最后一个作为 `final_explore_sol`（`mod.rs:59`）。这里的 `unwrap()` 是安全的，因为 `exploration_phase` 的 `feasible_sols` 初始就包含一个元素（`explore.rs:164`），并且只增不减。随后 `report_phase(Compression)`，`terminator.new_timeout(cmpr_config.time_limit)`，以探索阶段结束时的 `expl_separator.prob` 构造压缩分离器（`mod.rs:64-68`），调用 `compression_phase`，最后 `report(ReportType::Final, ...)` 并返回压缩结果。压缩阶段的每次尝试都会通过 `sep.rollback(init_sol, None)` 回到当前最好解（`compress.rs:85`），因此探索阶段结束时分离器残留的状态不会污染压缩结果。

`optimize` 返回 `Result<SPSolution, ConstructionError>`，`cli.rs:131` 用 `?` 处理。

## 十一、终止器与时间限制

终止逻辑由 `util/terminator.rs` 中的 `Terminator` trait 定义（`terminator.rs:4-14`），核心是三个方法：`kill()` 返回是否应结束、`new_timeout(d)` 设置一个从当前时刻起算的期限、`timeout_at()` 读取期限。`BasicTerminator`（`terminator.rs:16-45`）只根据期限判断；`new_timeout` 调用时把 `timeout` 设为 `Instant::now() + d`，`kill` 则判断 `Instant::now() > timeout`。bench 与集成测试都使用这一版本（`bench.rs:102`，`tests/tests.rs:58`）。

`kill()` 在求解器中被检查的位置很有限。全仓库中只有三处调用：探索主循环 `explore.rs:177`、分离器外层循环 `separator.rs:107`、压缩主循环 `compress.rs:39`。分离器的内层循环 `separator.rs:116` 的条件只有 `n_iter_no_improvement < iter_no_imprv_limit`，不检查时间。因此，一次分离可能要在时间到期后继续运行完整的内层迭代才返回，超时是"软"的，实际结束时间可能明显晚于设定值，超出量取决于物品数量与 `iter_no_imprv_limit`。这一点没有实测数据，属于从代码结构推断的结论，需要用大算例验证。另外，`-t` 计时只覆盖搜索两阶段，不含 instance 解析、LBF 构造以及最终的 SVG/JSON 导出。

`new_timeout` 在每个阶段开始时调用：`optimizer/mod.rs:56` 为探索，`mod.rs:63` 为压缩。压缩阶段的 `TimeBased` 步长也以 `time_limit` 为分母线性推进（`compress.rs:25-31`）。

## 十二、Ctrl+C 处理

`util/ctrlc_terminator.rs` 的 `CtrlCTerminator` 在 `new`（`ctrlc_terminator.rs:20-36`）中调用 `ctrlc::set_handler`，注册一个闭包，闭包打印 `" terminating..."` 并把 `Arc<AtomicBool>` 置为 true。`set_handler` 只能成功一次，`expect` 会在重复注册时 panic，因此 `CtrlCTerminator::new` 只能在进程中调用一次。`cli.rs:121` 是唯一的调用点，`bench.rs` 与测试都使用 `BasicTerminator`，不会冲突。

真正的语义在 `new_timeout`（`ctrlc_terminator.rs:45-49`）中。它先把标志位清零，再设置新的期限。于是，探索阶段按 Ctrl+C 会令 `kill()` 返回 true，探索循环结束，`optimize` 随即调用压缩阶段的 `new_timeout`，清零标志，压缩正常开始。这就是 README:62 所说的"移动到下一阶段"。压缩阶段按 Ctrl+C 则结束压缩并进入导出。由此也可以看出两个局限。其一，Ctrl+C 只在下一次 `kill()` 检查时生效，结合上一节的内层循环问题，响应并不是 README 所说的"立即"，可能要等一整轮内层迭代。其二，由于注册的是自定义处理器，第二次 Ctrl+C 也不会强制退出，只会再次置位标志；如果阶段卡在不检查 `kill()` 的代码中，用户只能从外部终止进程。此外，warn 消息写的是 "terminating..."，即便只是跳过阶段也显示同样的文字，容易让用户误以为程序已经退出。

TUI 模式不使用 `CtrlCTerminator`，而是 `tui/runtime.rs:227-262` 的 `TuiTerminator`，它的 `kill` 同时检查 `quit`（q 或 Esc）、`interrupt_phase`（Ctrl-C，对应界面中的"skip phase"提示，见 `tui/dashboard.rs:227`）以及期限。二者的阶段切换语义相同。

## 十三、输出文件与时机

最终 JSON 在 `cli.rs:133-138` 中生成，写入 `output/final_{name}.json`，其中 `name` 取自 instance 的 `name` 字段，内容为 `ExtSPOutput`，即 instance 字段加上 `solution`，`solution` 由 `jagua_rs::probs::spp::io::export(&solution, *EPOCH)` 导出（jagua-rs `probs/spp/io/export.rs:8-15`）。`run_time_sec` 是解生成时刻相对 `EPOCH` 的秒数，截断为整数。JSON 只在 `optimize` 正常返回后写入一次，中途被 kill -9 时不会留下任何 JSON。

SVG 由 `SvgExporter`（`util/svg_exporter.rs:8-86`）负责，它实现了 `SolutionListener` trait。最终 SVG 只在 `ReportType::Final` 上报时写入 `output/final_{name}.svg`（`svg_exporter.rs:73-84`），时机是 `optimize` 的最后一步，早于 JSON。中间解写入 `output/sols_{name}/`，文件名格式为 `{计数}_{宽度:.3}_{后缀}.svg`，后缀由 `svg_exporter.rs:45-52` 决定，取值 `expl_f`、`expl_nf`、`cmpr`、`final`。`ExplImproving` 不写中间 SVG（`svg_exporter.rs:64-66`），计数只在真正写入时递增。注意 `Final` 也会在 `sols_` 目录中再写一份 `_final.svg`。`SvgExporter::new`（`svg_exporter.rs:18-41`）在构造时会删除中间目录中所有 `.svg` 文件，`remove_file(...).unwrap()` 失败时会 panic。

实时 SVG 由 `live_path` 控制，`cli.rs:113-116` 中为 `data/live/.live_solution.svg`，只在 `live_svg` feature 下启用。`svg_exporter.rs:59-63` 对每一次 `report`（包括 `ExplImproving` 与 `ExplInfeas`）都完整序列化并覆盖写入该文件，写入日志级别为 Trace。`separator.rs:154` 在分离器每次刷新最小损失时都会上报 `ExplImproving`，因此 live 模式下磁盘写入会非常频繁，这是一个潜在的性能开销，需要实测。`data/live/live_viewer.html:51` 通过相对路径 `.live_solution.svg?t=...` 读取该文件，所以查看器只有在仓库根目录启动程序时才能正确读到它。

SVG 的绘制选项 `DRAW_OPTIONS` 定义在 `consts.rs:41-48`，使用灰色主题，开启 `highlight_collisions`，关闭四叉树与替代形状的绘制，即最终图中的碰撞会被高亮显示，这是检查解是否合法的直接手段。`io.rs:119-148` 的 `write_svg` 与 `write_json` 会先创建父目录，并通过 `fs::canonicalize(...).expect` 打印绝对路径，`SvgExporter` 中的 `expect("failed to write ...")` 则会在磁盘错误时直接 panic，发生在优化线程中。

## 十四、Cargo features 的含义

`Cargo.toml:46-51` 定义了五个 feature，默认均不启用。`live_svg` 打开实时 SVG 写入，并与 `data/live/live_viewer.html` 配合使用，只影响 CLI 路径（TUI 路径始终写实时 SVG，见 `tui/runtime.rs:183`，它没有受 feature 控制）。`only_final_svg` 让 `cli.rs:108-111` 不再创建 `sols_` 中间目录，只输出最终 SVG 与 JSON。`simd` 打开 `lib.rs:1` 的 portable SIMD，并切换 `eval/collision_loss.rs` 与 `quantify/mod.rs:8` 的实现，需要 nightly 工具链。`oracle-trace` 在 `explore.rs`、`sample/search.rs`、`sample/mod.rs` 与 `optimizer/mod.rs:124-127` 中编译追踪事件与 `minimum_strip_width_for_trace` 导出，用于与外部 oracle 对照，本仓库内没有消费者。`tui` 切换 `main.rs` 的入口，同时引入 crossterm 与 ratatui，`tui.rs:30` 使用自己的日志模块写 `output/log.txt`，并不走 `cli.rs` 的导出逻辑。

`Cargo.toml:53-68` 还定义了 `profile.dev`、`profile.debug-release` 与 `profile.release`。其中 `debug-release` 继承 release 并开启 `debug-assertions`，即 README:167-171 所说"在 release 性能下启用所有 `debug_assert!`"的方式。`profile.release` 使用 `lto = "fat"`，编译耗时较长。`Cargo.toml:43-44` 仅在非 wasm32 目标上引入 `ctrlc`，`.cargo/config.toml` 为 wasm32 设置了原子与 SIMD 目标特性，说明仓库同时考虑过浏览器端编译。

## 十五、assertions 何时启用

`util/assertions.rs:12-139` 的 `tracker_matches_layout` 是唯一的对外检查函数。它被 `debug_assert!` 包裹地调用于 `optimizer/separator.rs:267,294`、`optimizer/worker.rs:83,128` 与 `quantify/tracker.rs:51,106,120`。因此它只在 `debug_assertions` 开启时运行，即 dev、test 以及 `debug-release` 配置，release 构建中不会执行。函数内部虽然使用了无条件的 `assert!`，但它本身只通过 `debug_assert!` 进入，行为一致。

检查内容包括：布局中的每个 `pk` 都已登记在跟踪器的映射中（`assertions.rs:13`）；jagua-rs 的四叉树与重新构建的结果一致（`assertions.rs:14`）；自身的成对损失为 0（`assertions.rs:20`）；对每一对物品，用 jagua-rs 的碰撞收集结果与存储的损失比较，两个方向的 `quantify_collision_poly_poly` 只要有一个在 10% 误差内即通过（`assertions.rs:30-31`），若碰撞检测两个方向不对称则只打 warn（`assertions.rs:48-50`），其余不一致则 `panic!("tracker error")`（`assertions.rs:79-90`）；容器损失则要求 ulps 不超过 5（`assertions.rs:128-135`）。代价是每次调用需要对所有物品对做碰撞查询，开销随物品数的平方增长，因此 debug 构建在大算例上会非常慢，这是使用 `debug-release` 进行性能评估时要避免的情形。

另外，`optimizer/mod.rs` 与 `sample/` 中还有若干 `debug_assert!`（例如 `separator.rs:251` 检查条带宽度，`best_samples.rs:73,86` 检查采样排序），它们遵循同样的启用规则。

## 十六、bench 与集成测试

`src/bench.rs` 是独立的第二个二进制（`Cargo.toml:13-15`），用法是 `bench <input> <总时间秒> <运行次数>`，三者均为位置参数，缺失时用 `expect` 直接 panic。它与 CLI 的差别包括：不做 Ctrl+C 注册，全部使用 `BasicTerminator`；不接受 `-x`、`--workers` 等参数；探索与压缩阶段都使用 `DummySolListener`，不写中间 SVG；并行度由 `num_cpus::get_physical() / expl_cfg.separator_config.n_workers` 决定（`bench.rs:69-70`）。每个批次用 rayon 并行运行若干次，每次结束后写 `output/final_bench_{i}.svg`，最后输出 `final_best_{name}.svg`，并打印宽度与利用率的统计量。`get_git_commit_hash`（`bench.rs:267-277`）调用外部 `git` 命令。它存在两处边界问题：当物理核心数小于 `n_workers`（默认 3）时，`n_runs_per_iter` 为 0，`n_batches` 为浮点除以零得到的 inf，经 `as usize` 饱和为 `usize::MAX`，外层循环会打印几乎无穷多个空批次；若 `n_runs_total` 为 0，则 `n_batches` 为 NaN 并变成 0，随后对空结果集取 `unwrap` 会 panic。这两点来自代码阅读，尚未实际运行验证。

`tests/tests.rs` 中的集成测试对 `swim`、`shirts`、`trousers` 三个算例运行 LBF 构造、探索与压缩。它为每个物品重新分配 id（`tests.rs:34-36`），用 `BasicTerminator` 分别限定 10 秒，并在 LBF 构造后验证导出再导入的解是可行的（`tests.rs:63-69`）。测试只检查构造与导入的正确性，不对最终的压缩结果做断言。

## 十七、README 用法与代码核对

`README.md:52-81` 的基本用法与代码一致：`cargo run --release -- -i ... -t ...`，默认 600 秒。但 `README.md:72` 的 `--min-item-separation` 不存在，应删除。`README.md:106-107` 的分支名与 `Cargo.toml:18` 不一致。`README.md:62` 说 Ctrl+C "立即"移动到下一阶段，但实际只能在下一次 `kill()` 检查时生效，见第十二节。`README.md:60-62` 的 80%/20% 只适用于 `-t`。`README.md:83-100` 的实时可视化要求从仓库根目录启动，否则查看器找不到 `data/live/.live_solution.svg`。`README.md:119-143` 对输出路径的描述与 `cli.rs:106-138` 一致，包括 `sols_` 目录与 `only_final_svg` 的开关。`README.md:145-157` 关于 `target-cpu=native` 与 nightly 的说明与 `simd` feature 的实现一致。`README.md:159-171` 关于 `debug_assert!` 与 `debug-release` 的描述也与代码一致。

## 十八、疑点与可能的坑

把上面各节的问题归纳起来，有以下几点值得团队特别注意。第一，依赖版本不一致（分支名与本地副本的 `new` 签名）是最需要先解决的问题，否则所有 jagua-rs 接口的描述都可能与实际编译结果不符。第二，warm start 的可行性与 demand 上限都没有前置校验，错误输入可能导致不可行的输出或在 debug 模式下 panic。第三，`time` 预算不包括 LBF 构造，且分离器内层循环不检查时间，实际运行时长可能超过 `-t`，Ctrl+C 的响应也有延迟。第四，`--help` 会产生输出目录与日志文件的副作用，而日志每次运行都会覆盖。第五，输入解析的错误信息会丢失 solution 解析失败的原因。第六，固定 `-s` 种子并不能保证结果可复现，因为终止与压缩步长都依赖墙钟时间（`compress.rs:25-31` 的 `TimeBased` 与 `kill()` 的时间判断），另外分离器使用多线程（`separator.rs:42-58`），其是否保证确定性未作验证。第七，`FailureBased` 的失败计数在 `compress.rs` 中不会因成功而清零，名称中的"fail"实际指累计失败次数，与"连续失败"的直觉不同，`-x` 下压缩会在约 38 次累计失败后结束（`0.0005 × 0.9^38 < 0.00001`）。第八，`EXPORT_LIVE_SVG` 与 `EXPORT_ONLY_FINAL_SVG` 两个常量是死代码，阅读时容易误以为它们控制行为。第九，`bench` 在物理核心数不足或运行次数为 0 时会出现空批次循环或空结果 panic。第十，`SvgExporter` 中的 `unwrap` 与 `expect` 使磁盘错误变成优化线程的 panic，而不是可恢复的错误。


# sparrow 优化器（src/optimizer）代码逻辑说明

阅读范围：`sparrow/src/optimizer/` 下的 `mod.rs`、`lbf.rs`、`explore.rs`、`separator.rs`、`worker.rs`、`compress.rs`，并追到其直接依赖的 `config.rs`、`consts.rs`、`quantify/tracker.rs`、`sample/search.rs`、`eval/sep_evaluator.rs`、`eval/collision_loss.rs`、`util/ctrlc_terminator.rs`、`cli.rs`，以及 jagua-rs 中 `SPProblem`（`jagua-rs/jagua-rs/src/probs/spp/entities/problem.rs`）和 `DTransformation` 组合语义（`jagua-rs/jagua-rs/src/geometry/transformation.rs`）。文中路径均相对 `sparrow/`，行号以当前源码为准。凡是由代码推导而未经实际运行验证的结论，文中均已注明。

## 一、顶层调用顺序与阶段切换

入口是 `optimize`（`src/optimizer/mod.rs:28-81`），对应论文 Algorithm 11。它先用 `next_rng` 闭包（mod.rs:37）从主随机数发生器中依次取出 u64 作为子种子，然后按以下顺序执行。第一步，若没有给出初始解，则调用 `LBFBuilder` 构造初始布局（mod.rs:40-44）；若给出了 `initial_solution`，则用 `SPProblem::restore` 直接还原（mod.rs:45-51）。第二步，调用 `terminator.new_timeout(expl_config.time_limit)` 设置探索阶段的时限，并以 `Separator::new` 建立探索用的分离器 `expl_separator`，再进入 `exploration_phase`（mod.rs:54-59）。探索结束后取返回向量的最后一个元素作为 `final_explore_sol`（mod.rs:59）。第三步，重置时限为压缩阶段的 `cmpr_config.time_limit`，并把 `expl_separator.prob` 移入新建的 `cmpr_separator`，调用 `compression_phase`（mod.rs:62-75）。最后报告 `ReportType::Final` 并返回（mod.rs:77-80）。

阶段切换的条件非常朴素：探索阶段没有“成功判据”的显式切换，而是由 `exploration_phase` 内部的任一终止条件结束后直接进入压缩；这些条件包括时限或 Ctrl-C（`term.kill()`）、宽度降到 `min_width` 以下、以及可选的连续失败次数上限。阶段之间唯一传递的状态是 `expl_separator.prob`（布局本身）和 `final_explore_sol`（快照）。注意两者并不一致：压缩分离器的起始状态来自探索结束时的布局，它可能是不可行的，但压缩阶段第一次尝试时会先 `change_strip_width` 并 `rollback` 到 `final_explore_sol`，因此起始状态会被覆盖，不影响正确性（见 `compress.rs:84-85`）。

另一处需要注意的是时限的实际来源。`DEFAULT_SPARROW_CONFIG`（`src/config.rs:51-98`）中写的是探索 9 分钟、压缩 60 秒，但 `cli.rs:36-57` 在命令行未指定时会用 `600 秒 × 0.8 / 0.2` 覆盖，因此默认实际为探索约 480 秒、压缩约 120 秒。`-t` 参数同理按 0.8 和 0.2 切分。`--early-termination` 会把 `max_conseq_failed_attempts` 设为 10，并把压缩的收缩策略换成 `FailureBased(0.9)`（cli.rs:58-62）。

`minimum_strip_width`（mod.rs:83-122）为两阶段共用的宽度下界。它取三者的最大值：面积下界 `总面积 / 容器高度`（乘以 `1 - 1e-6`），以及每种离散旋转下可放入高度的物品的最小包围盒宽度，最后再加上容器内缩量 `width_inset`。连续旋转物品仅计入面积下界（mod.rs:101-102 的 ponytail 注释亦承认这一点）。注意 `item.shape_cd.area` 取的是碰撞检测形状而非原始形状，多边形简化与膨胀会使其略偏大，因此该下界可能比真实下界保守。

## 二、LBF 初始解构造

`LBFBuilder::construct`（`src/optimizer/lbf.rs:55-86`）先对物品排序，排序键为 `Reverse(凸包面积 × 直径)`（lbf.rs:58-64），即大而“宽”的物品先放。排序以物品类型为单位，同一类型的多个副本随后按数量连续展开（lbf.rs:65-69），使用 `sorted_by_cached_key`，相同键保持稳定顺序。这个键把面积与长度相乘，量纲并不统一，但只起启发式排序作用。

放置由 `place_item`（lbf.rs:88-122）完成。对每个待放物品，先调用 `find_placement`（lbf.rs:124-142）。它以 `LBF_SAMPLE_CONFIG`（`src/consts.rs:50-54`：容器内 1000 个均匀采样、无聚焦采样、3 次坐标下降）调用 `search_placement`，评估器为 `LBFEvaluator`（`src/eval/lbf_evaluator.rs:33-59`）。该评估器只返回 `Invalid` 或 `Clear`，`Clear` 的损失为 `10·x + y`（`X_MULTIPLIER=10`，`Y_MULTIPLIER=1`），因此近似实现“左优先、再下优先”的左下填充。只有最优样本是 `Clear` 时才接受放置。

若找不到可行放置，则进入宽度扩张：`next_width = 当前宽度 × 1.2`（lbf.rs:102），并以 `2 × Σ(直径 × 数量)` 作为上限 `width_limit`（lbf.rs:104-110）。若 `next_width ≥ width_limit` 则返回 `ConstructionError`（lbf.rs:111-113），`optimize` 因而以 `Err` 退出（mod.rs:42 的 `?`）。注释明确声明这不是不可行证明（lbf.rs:13-14, 103）。扩张使用的是 `SPProblem::change_strip_width`，它只改变右端位置、不平移已放物品（jagua-rs problem.rs:33-37），这与探索阶段使用的 `Separator::change_strip_width` 不同，后者会平移右侧物品。构造完成后调用 `fit_strip`（lbf.rs:77-79）把宽度收缩到最右物品的包围盒，这也是探索开始时的宽度。

这里有两个隐含假设。其一，LBF 的每个物品只有一次“采样 1000 点”的机会，采样稀疏时可能找不到狭缝中的可行位置，从而被迫扩张宽度，结果质量依赖随机种子，但不影响正确性。其二，`width_limit` 与实例规模线性相关，对小实例上限很宽松，对很长的实例可能产生非常宽的初始条带，这会增加探索阶段的收缩次数。

## 三、探索阶段（exploration_phase_impl）

`exploration_phase_impl`（`src/optimizer/explore.rs:153-299`）对应论文 Algorithm 12。它维护三个状态：`current_width`、`best_width`，以及 `feasible_sols`（已发现的可行解序列）；另有 `infeas_sol_pool`（按总损失升序排列的不可行解池，元素为 `(SPSolution, f32)`，见 explore.rs:173）。

初始化时 `feasible_sols` 被直接放入 `sep.prob.save()`（explore.rs:164），而不论其是否可行。随后进入主循环 `while !term.kill()`（explore.rs:177）。每轮先调用 `sep.separate(term, sol_listener)`（explore.rs:179）在当前宽度下尝试消除碰撞，并取回最小损失状态及其 CT 快照。

若 `total_loss == 0.0`（explore.rs:182），则记录可行解：仅当 `current_width < best_width` 时才压入 `feasible_sols`（explore.rs:184-193）。随后计算 `next_width = current_width × (1 - shrink_step)`（explore.rs:195），`shrink_step` 在默认配置中为 0.001（`src/config.rs:54`），即每次成功收缩 0.1%，且**不随时间或失败次数衰减**（衰减只存在于压缩阶段）。若 `next_width < min_width` 或 `next_width ≥ current_width` 则退出循环（explore.rs:196-199）。否则调用 `sep.change_strip_width(next_width, None)`（explore.rs:206），清空不可行池（explore.rs:208），进入下一轮。

若 `total_loss > 0`（explore.rs:210），则该结果被视为失败。失败的解按损失升序插入不可行池（explore.rs:220-227），插入位置由 `binary_search_by` 决定，并用 `partial_cmp().unwrap()`，遇到 NaN 会 panic。连续失败次数即为池的长度（池只在成功时清空），若达到 `max_conseq_failed_attempts`（默认 `None`，即 `usize::MAX`，见 `config.rs:56`）则终止（explore.rs:239-245）。

池的采样方式在 explore.rs:248-284。取 `raw = N(0, σ=0.25)`，再取绝对值并截断到 0.999，然后映射为 `floor(sample × 池长)`。因此该分布是半正态分布，`P(sample < 0.25) ≈ 68%`，即约 68% 的概率选中池中前四分之一的解，这体现了“越好的解被选中概率越大”的设计。被选中的解以 `sep.rollback(selected_sol, None)`（explore.rs:287）恢复。注意这里传入的快照为 `None`，因此 `rollback` 会重建 CT（separator.rs:259-262），所有 GLS 权重被重置为 1，这与 `separate` 内部回滚保留权重的行为不同（见下一节）。

恢复之后调用 `disrupt_solution`（explore.rs:288，实现于 explore.rs:315-514）。它的思路是选两个“大”物品交换位置。“大”的判定（explore.rs:333-372）是按凸包面积降序累加，直到累计面积超过全部物品凸包面积的 `large_item_ch_area_cutoff_percentile = 0.75`（config.rs:69），取此时的物品面积作为阈值。这个定义的含义并不是“前 75% 物品”，而是“覆盖约 75% 凸包面积的最大物品集合”。若实例中所有物品尺寸相同，则阈值会落在第一个物品上，所有物品都成为“大”物品，这一点在下文的疑点中还会提到。

第一个物品 `pk1` 从大物品集合中均匀随机选取（explore.rs:384-387）。第二个物品 `pk2` 优先从面积和直径都与 `pk1` 相差超过 1% 的大物品中选取（explore.rs:391-397）；若没有则退化为从所有其他已放物品中随机选取（explore.rs:398-402）。随后两物品的位姿互换（explore.rs:407-442），并用 `convert_sample_to_closest_feasible`（`src/sample/uniform_sampler.rs:115-135`）把旋转吸附到各自允许的离散集合。

互换之后是“包含物”搬迁（explore.rs:444-513）。对于 `pk1` 新位置的碰撞物中、其 POI（`shape.poi.center`）落在 `pk1` 形状内部的物品（`practically_contained_items`，explore.rs:517-539），计算一个刚体变换 `dt1_old ∘ dt1_new⁻¹`，把这些物品的位姿从新位置参考系搬到旧位置参考系（explore.rs:450、460-464），再吸附旋转后移动。`pk2` 对称处理（explore.rs:484-512）。这一步的意图是：大物品被放到小物品的位置后，原来包围小物品的若干物品会被大物品包住，搬迁它们到大物品原来的空位，可以减少立即产生的大面积重叠。这里 `a.transform(&b)` 在 jagua-rs 中的语义是先作用 `a` 再作用 `b`（transformation.rs:70-73），以上解读依据于此。该变换只保证相对位姿近似保持，旋转吸附后并不精确。

探索循环的其余细节包括：`best_width` 与 `current_width` 实际上同步变化（成功时两者同时变为新宽度），因此 `current_width < best_width` 在成功后恒为真，除非初始解不可行；这一点与下文的疑点有关。循环结束时返回 `feasible_sols`，`mod.rs:59` 取其最后一个作为探索结果。

## 四、Separator：在给定宽度下消除重叠

`Separator::separate`（`src/optimizer/separator.rs:75-202`）对应 Algorithm 9。它的外层是 strike 循环（separator.rs:107），内层是无改进迭代循环（separator.rs:116）。

内层每一轮调用 `move_items_multi`（separator.rs:119，实现于 205-248），然后读取 `loss` 与加权损失 `w_loss`。若 `loss == 0`，则记录并 `break 'outer`，即成功（separator.rs:137-146）。若 `loss < min_loss`，则更新最小损失快照 `min_loss_sol`，并且只有当 `loss < min_loss × 0.98` 时才把 `n_iter_no_improvement` 清零（separator.rs:147-160）。这意味着 2% 以内的微小改进不会延长内层耐心，但仍会更新最优快照。否则计数加一（separator.rs:161-164）。每轮结束后调用 `self.ct.update_weights()`（separator.rs:168）做 GLS 权重更新，`update_weights` 的规则见 `src/quantify/tracker.rs:124-155`：有碰撞的对乘以 `1.2 + 0.8 × loss/max_loss`，无碰撞的对以 0.95 衰减，下限为 1.0，**没有上限**。

内层退出后，strike 判定在 separator.rs:172-178。若 `initial_strike_loss × 0.98 ≤ min_loss`，即本次 strike 中没有达到 2% 的改进，则 `n_strikes += 1`，否则 `n_strikes` 清零。随后无论如何都会执行 `self.rollback(&min_loss_sol.0, Some(&min_loss_sol.1))`（separator.rs:179），回到本次 strike 中的最优状态，并且**保留 GLS 权重**（`restore_but_keep_weights`，tracker.rs:94-107）。成功（`break 'outer`）时直接跳过这次回滚，但此时布局本身即为可行状态。

`separate` 的返回值是 `(min_loss_sol.0, min_loss_sol.1)`（separator.rs:201），即全程最优状态，无论是否可行。调用方通过 `get_total_loss` 判断成败。

这里的限制条件可以归纳如下：strike 上限在探索中为 3、压缩中为 5（`src/config.rs:58-60, 75-85`）；内层无改进上限在探索中为 200、压缩中为 100（同处）。`n_strikes` 只在某次 strike 的 2% 改进不成立时增加，因此一个 `separate` 调用至少会跑 `3 × 200 = 600` 次 `move_items_multi`（探索）。`min_loss` 是跨 strike 的全局最优，因此 strike 的判定是相对“本次 strike 起点”而言的。

回滚的条件有三种。其一是 strike 结束时回滚到本次全局最优（separator.rs:179）。其二是成功时不回滚，因为当前即为可行解。其三是外部调用 `rollback`（separator.rs:250-264），若传入快照则保留权重，否则 `CollisionTracker::new` 重建，权重全部归 1。`rollback` 的 `None` 分支被探索的失败恢复（explore.rs:287）与压缩的每次尝试（compress.rs:85）调用，因此这两处每次都会重置 GLS 权重，这是一个重要的设计事实。

`move_items_multi`（separator.rs:205-248）是多 worker 并行的核心，在下一节展开。`change_strip_width`（separator.rs:299-337）用于收缩条带：它先把中线右侧（`split_position`，默认为宽度中点，压缩时为随机位置）的物品整体平移 `Δ`（separator.rs:305-318），然后调用 `SPProblem::change_strip_width`，最后重建 CT 并用 `self.rng` 重新生成所有 worker 的种子（separator.rs:325-335）。注意平移过程中的 `move_item` 是在旧宽度的容器中进行的，因此中间状态可能出现越界，但最终的 CT 是重建的，不会残留。

## 五、Worker 并行结构与随机数

每个 `Separator` 在构造时为 `n_workers` 个 worker 各自复制一份 `prob` 与 `ct`，并用主随机数生成器的 `rng.random()` 依次为每个 worker 派生一个种子（separator.rs:40-49）。随后创建专属的 rayon `ThreadPoolBuilder`，线程数等于 `n_workers`（separator.rs:51-62），wasm32 时退化为全局线程池。探索与压缩各自拥有一个 `Separator`，因此各有一个独立的线程池，且只要 `Separator` 存活线程池就存在。在 `optimize` 中，探索的分离器在压缩阶段开始后仍然存活到函数结束（mod.rs:57, 64），因此两组线程会共存，其中探索的一组在压缩期间空闲。这是资源上的小浪费，不影响正确性。

`move_items_multi` 的并行语义如下。每轮迭代开始时，主线程保存一份 `master_sol = self.prob.save()`（separator.rs:206）。随后在线程池中对 `workers.par_iter_mut()` 执行：每个 worker 通过 `load` 恢复到 `master_sol` 并复制一份主 CT（worker.rs:26-31），然后调用 `move_items`（worker.rs:34-80）。`move_items` 先收集所有仍有损失的物品，用 worker 自己的随机数打乱顺序（worker.rs:36-43），然后逐个重新搜索位置，搜索使用 `SeparationEvaluator`（sep_evaluator.rs:49-97）与 `search::search_placement`（sample/search.rs:231-318），并把当前位姿作为参考候选加入（search.rs:255 附近），因此每次移动不会比当前更差，这与 worker.rs:122-127 中 `new_w_l <= old_w_l × 1.001` 的 debug 断言一致。

并行结束后，主线程用 `min_by_key` 在所有 worker 中选择**加权**总损失最小者（separator.rs:236-241），把它的 `prob` 与 `ct` 恢复到主状态（separator.rs:244-245）。其余 worker 的工作全部被丢弃。注意这里的选择标准是加权损失，而 `separate` 的最优记录是非加权的 `get_total_loss`（separator.rs:147-160）。两者不一致意味着某一轮被选中的 worker 可能并不是非加权损失最小者，这是一个设计选择，但文档中并未说明。

随机数的分配可以归纳为一棵由单一种子展开的树。顶层种子经 `optimize` 的 `next_rng` 依次派生出 LBF 构造用的种子、探索分离器种子、压缩分离器种子（mod.rs:37-68）。每个分离器再用自身的 `rng` 为各 worker 派生种子（separator.rs:42-49）。每个 worker 使用自己的 `rng` 做打乱与采样（worker.rs:43, 64）。探索的池采样与交换选择使用 `sep.rng`（explore.rs:253, 386, 397），压缩的随机分割位置也使用 `sep.rng`（compress.rs:89）。每次 `change_strip_width` 都会用 `self.rng` 重新派生 worker 种子（separator.rs:328-334），因此种子序列依赖于宽度变化的次数，但仍是确定的。

由此可得到两条结论。第一，各 worker 之间没有共享的可变状态，`master_sol` 与 `self.ct` 只被只读借用，worker 的结果仅依赖于自己的状态与种子，`SepStats` 的求和与 `min_by_key` 的平局规则（取首个）也与线程调度无关，因此在相同种子下迭代序列是确定的，不存在因调度引起的数据竞争或结果漂移。第二，程序的非确定性只来自墙钟时间：`Terminator` 的超时（explore.rs 中的 `term.kill()`、compress.rs:28-30 中按时间衰减的步长）以及 `Instant` 相关的日志。因此“同一种子，结果一致”只在不触发时限的意义上成立。

## 六、压缩阶段（compression_phase）

`compression_phase`（`src/optimizer/compress.rs:11-74`）对应 Algorithm 13。它以 `final_explore_sol` 为初始 `best_sol`，循环尝试进一步收缩。每轮的步长由闭包 `shrink_step_size` 给出（compress.rs:24-36）。`ShrinkDecayStrategy::TimeBased` 时，步长随已用时间线性衰减，`step = shrink_range.0 + (elapsed/time_limit) × (shrink_range.1 - shrink_range.0)`，默认 `shrink_range = (0.0005, 0.00001)`（config.rs:72），即从 0.05% 线性降到 0.001%。`FailureBased(r)` 时，`step = shrink_range.0 × r^n_failed`，只随失败次数衰减，成功不会恢复步长。

循环条件为 `while !term.kill() && let step = ... && step >= shrink_range.1`（compress.rs:39-42）。因此 TimeBased 的终止点恰好是时限（此时步长降至 0.00001），FailureBased（r = 0.9）在约 38 次失败后因步长低于 0.00001 而终止，哪怕时间仍然充裕。此外 compress.rs:45-48 还要求 `next_width ≥ min_width` 且 `next_width < width`，否则退出。

每次尝试由 `attempt_to_compress`（compress.rs:76-98）完成。它先用 `change_strip_width(init_sol.strip_width())` 把宽度恢复到 `best_sol` 的宽度（compress.rs:84），再 `rollback(init_sol, None)`（compress.rs:85），此时权重被重置。然后在 `[0, 当前宽度)` 之间随机选一个分割点（compress.rs:89），把宽度收缩 `step` 比例（compress.rs:88, 90），中线右侧物品随之左移。随后调用 `sep.separate`（compress.rs:93），仅当总损失为 0 时返回可行解。成功时 `best_sol` 被替换，失败时 `n_failed_attempts` 加一（compress.rs:50-65）。

压缩阶段的每次尝试都是从 `best_sol` 出发的一次独立试验，失败不会修改 `best_sol`，因此压缩结果单调不劣于探索结果。压缩与探索的区别可概括为：探索是“宽度逐步下降，失败后从不可行池扰动”，压缩是“每次尝试都从最优可行解出发，一次性收缩一个随机位置的较大比例，失败则放弃该次尝试”，没有不可行池，也没有交换扰动。

## 七、疑点、隐含假设与风险

第一，可行性不被保证的路径。`feasible_sols` 在 explore.rs:164 无条件放入初始解。若用 `initial_solution` 热启动且初始解不可行，并且在探索收敛到任何可行解之前时限到期，则 `feasible_sols.last()` 仍是这个不可行解，mod.rs:59 会将其交给压缩，压缩若无一次成功，最终输出 `cmpr_sol` 即为不可行解。此外，若初始解不可行，但在当前宽度下随后变为可行，则 explore.rs:184 的 `current_width < best_width` 为假，这个可行状态不会被记录，只有在后续更窄的成功时才会入列。调用方（`cli.rs` 的导出，cli.rs:125-141）并不检查最终解的可行性。

第二，GLS 权重无上界，且存在溢出风险（按代码推导，未运行验证）。`update_weights` 对有碰撞的对每轮乘以 1.2 至 2.0 之间的系数（tracker.rs:141-153），f32 在 `2^128` 附近溢出。若一对始终是最严重碰撞，约 128 轮后权重即为 `inf`。若该对之后损失降为 0，则 `0.95 × inf` 仍为 `inf`，`weighted_loss = inf × 0.0 = NaN`，NaN 会传入 `get_total_weighted_loss`。在 debug 构建中，separator.rs:130-135 与 worker.rs:122-127 的断言会因 NaN 比较失败而 panic；在 release 构建中，`min_by_key` 配合 `OrderedFloat` 会把 NaN 视为最大值，从而使该 worker 几乎不会被选中。非加权的 `get_total_loss`（tracker.rs:199-214）不依赖权重，因此池的排序不受影响。由于权重在一次 `separate` 内跨 strike 保留（tracker.rs:94-107），在压缩的 5 个 strike 与 100 次无改进迭代的组合下，此风险并非遥远。即使每轮都取下界乘子 1.2，约 490 轮后也会溢出，因此长时间的单次 `separate` 本身就足以触发。

第三，内层循环没有时限检查。`separate` 的外层循环开头检查 `term.kill()`（separator.rs:107），但内层 `while n_iter_no_improvement < iter_no_imprv_limit`（separator.rs:116）不检查。因此时限到达后，当前 strike 仍需完整跑完，探索中最多多出一个 strike（约 200 次以上的 `move_items_multi`），压缩中最多多出约 100 次以上。对于大规模实例，单次 `move_items_multi` 可达秒级，这会使时限的实际超出不可忽略。此外，Ctrl-C 对探索阶段的中断也存在滞后。

第四，Ctrl-C 语义。`CtrlCTerminator::new_timeout` 会把 `ctrlc` 标志清零（util/ctrlc_terminator.rs:45-49），而 `optimize` 在探索结束后调用 `new_timeout`（mod.rs:63）。因此，探索期间按下 Ctrl-C 只会结束探索并自动进入完整的压缩阶段，而不是整个程序退出。这可能与用户预期不符，应当在使用说明中注明，或者改为仅在压缩阶段清零标志。

第五，`min_width` 的收敛性。探索的终止依赖 `next_width < min_width`（explore.rs:196），但 `min_width` 是面积下界，一般远不可达。因此探索的实际终止几乎只能靠时限或连续失败上限。默认配置中 `max_conseq_failed_attempts` 为 `None`（config.rs:56），因此在默认设置下探索会一直跑满时限。另外，连续旋转的物品不计入宽度下界（mod.rs:101-102），这同样使下界偏弱。

第六，不可行池的内存增长。池中每个元素都是完整的 `SPSolution`（含 CDE 快照），池大小只在成功时清零（explore.rs:208）。若探索长期失败，池会持续增长，且池的插入使用二分查找但没有容量上限。在默认时限下这通常不是问题，但若将 `max_conseq_failed_attempts` 设为 `None` 且失败频繁，内存占用会线性增长。

第七，交换扰动的退化。大物品的阈值（explore.rs:341-372）在实例中所有物品尺寸相同时会退化为“全部物品都是大物品”，此时第二个物品的过滤条件（面积与直径都相差超过 1%，explore.rs:391-397）总是为空，退化为随机选择任一其它物品。这意味着在同质实例中，交换的语义从“大小物品互换”变为“随机两物品互换”，搬迁逻辑对于大小相近的物品的意义也随之减弱。另外 `practically_contained_items` 的判定只看 POI 是否在形状内（explore.rs:533-537），对于形状高度凹陷的物品，POI 可能落在空隙中，导致判定不准确。

第八，并行与性能。并行结构本身没有数据竞争：worker 不共享可变状态，主线程的 `prob.restore` 与 `ct = best_ct.clone()` 只在 `pool.install` 返回后执行（separator.rs:236-245）。但每一轮都要对全部 worker 执行 `load`，即一次 `SPProblem::restore`（jagua-rs problem.rs:94-114）与一次 CT 克隆，以及对主状态的一次 `save`，这些都是 O(n) 的操作，在迭代次数很多时开销显著。`ReportType::ExplImproving` 的参数 `&self.prob.save()`（separator.rs:154）在报告前就会被计算，即使监听器不使用它，每次改进都要付出一次快照成本。`attempt_to_compress` 中的 `change_strip_width(init 宽度)`（compress.rs:84）在宽度相同时仍会对右半物品执行 `move_item`（delta 为 0 的空移动），随后又被 `rollback` 覆盖，属于可以省去的开销。

第九，`separate` 内 `loss == 0.0` 的判断依赖精确相等。由于每一对碰撞的损失为正（tracker.rs:81, 86 的 `assert!(loss > 0.0)`），总损失为零当且仅当无碰撞，因此该判断是安全的。但 `minimum_strip_width` 中的 `REL_TOL` 与 `shape_cd` 的膨胀仍可能让“看起来刚好能放下”的情形在数值上被判为碰撞，这属于几何精度问题而非逻辑错误。

第十，一致性的若干断言。`register_item_move` 通过复用 `idx` 槽位维持物品与 CT 下标的稳定对应（tracker.rs:113-121），`restore_but_keep_weights` 在同一 `separate` 调用内使用同源快照，因此权重与物品的对应关系不会错位。但若将来有人在不同 CT 谱系之间使用 `restore_but_keep_weights`，就会出现权重错配，此处的正确性依赖于调用约定而非类型系统保证。

## 八、收敛与终止情形小结

探索的正常终止有三种：时限或 Ctrl-C；宽度降到 `min_width` 以下；连续失败达到配置的上限（默认关闭）。压缩的正常终止有两种：时限（TimeBased 与时限重合）；步长降到 `shrink_range.1` 以下（FailureBased 的失败次数达到约 38 次）。`separate` 本身不会无限循环：strike 计数有上限，而每次“2% 改进”的重置只能发生有限次，因为损失有正的下界（由 `overlap_area_proxy` 中的 `epsilon²` 项保证，quantify/mod.rs:16-19 与 overlap_proxy.rs:15-18），且每次重置要求损失至少乘以 0.98。这两点合起来保证 `separate` 有限终止，但在大规模实例上，有限可能意味着很长。

需要注意的死循环形态有两种。第一种是探索中的“成功—失败交替”：在窄宽度附近，`separate` 可能反复失败，但由于池会不断被扰动，实际上会一直运行到时限，属于正常的工作状态而非死循环。第二种是在 `disrupt_solution` 的 `placed_items.len() < 2` 分支（explore.rs:321-324）中，扰动直接返回而不改变布局，若此时仍处于不可行状态，则下一轮 `separate` 将从相同状态重新开始，形成没有新信息的重复。该分支只在仅有一个物品时触发，因此在多物品实例中并不会出现。

## 九、建议的验证点

建议团队在阅读本文后，结合以下几点做进一步确认。其一，用热启动的不可行解跑一次短时限探索，检查输出是否可行，以验证第七节第一点的推断。其二，在 debug 构建中对一个高冲突实例长时间运行 `separate`，观察 GLS 权重是否出现 `inf` 与 NaN，以验证第二点的推断。其三，统计 `separate` 中内层循环在时限后的实际超出时长，以验证第三点。其四，在同质物品实例上对比“大物品交换”的日志，确认第七点中的退化是否发生。以上结论均为代码推导，尚未通过实际运行验证。


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

第一，`SampleEval` 的 `Ord` 使用 `FPA`，其相等判断基于锁定的 `float-cmp 0.10.0` `F32Margin` 默认值：绝对差不超过 `f32::EPSILON` 或 ULP 距离不超过 4 时视为相等。该近似相等关系不具传递性，因此 Rust `Ord` 契约被违反。固定 Rust 1.98.1 Debug/Release profile 已覆盖七组边界的 42 个排列、四值非传递链的 24 个排列、BestSamples 32 项真实插入及 CoordinateDescent 近似并列轨迹，C++ 对应消费者匹配这些 fixture；任意非传递序列仍属 R3 未认证域，不能推广固定排序结果。详见 [S10 逐文件记录](../logic-comparison-sparrow/records/S10-sample-eval.md)。

第二，`update_weights` 的权重没有上界，且每次碰撞迭代都可能乘以最多 2 倍（`tracker.rs:146-153`）。一个持续碰撞的成对条目在约 128 次连续乘以 2 后会超过 f32 的最大值。此后衰减 `inf * 0.95` 仍为 `inf`，而 `get_weighted_loss` 中 `inf * 0.0` 会得到 `NaN`（`189-197`、`216-230`），进而污染总损失与工作线程的 `min_by_key`。权重在一次 `separate` 调用的多次应变之间保留（见第七节），因此理论上可能在长时间运行中累积到溢出。这是一个需要通过计数或上界钳制来防范的风险，目前代码中没有相应检查。

第三，`overlap_area_proxy` 与 `quantify_collision_poly_container` 的 `debug_assert!(is_normal())`（`overlap_proxy.rs:323`、`mod.rs:347`、`375`）要求结果为正规数，即不为 0 也不为次正规数。在正常几何下 `ε²` 保证了前者，但对于极远距离的极圆，`g(pd)` 的值可能很小，极端情况下的次正规数只会导致调试断言失败，而不影响发布构建。

第四，`dtransfs_are_similar` 中的旋转比较使用 `%` 而非循环距离（`best_samples.rs:444-447`），如前所述。第五，分离代价中 `Clear` 恒为 0（`sep_evaluator.rs:139`），因此所有无碰撞样本在 `BestSamples` 中互相"相等"，接受规则要求严格小于，于是采样顺序中最先出现的无碰撞样本胜出，这是一个由采样顺序决定的结果。第六，`collision_loss.rs:262-266` 的剩余预算 `remaining = bound − loss` 与最后的 `self.loss > self.loss_bound` 比较使用的是同一组浮点数，但 SIMD 路径中还要做一次 `max_loss / weight` 与 `* weight` 的换算，在边界处可能与精确比较不一致，只产生微小偏差。

## 十三、性能瓶颈

评估一次候选的主要开销是形状变换与碰撞检测。`sep_evaluator.rs:116-118` 每次都要变换全部顶点与极圆，`collision_loss.rs:256-257`（SIMD）还会复制极圆，而随后的代理检测与精确检测都依赖 CDE 的四叉树遍历。`BestSamples` 的上界截断在这里起到关键作用：当候选集合填满后，上界迅速收紧，大量候选在前几个极圆或前几条边上就被截断为 `Invalid`。

追踪器层面，`get_loss` 与 `get_weighted_loss` 是 O(n) 的行求和，而 `move_items` 对每个物品都调用 `get_loss` 作为过滤条件（`worker.rs:41`），因此每轮的开销是 O(n²)。`get_total_loss` 与 `get_total_weighted_loss` 遍历整个矩阵（O(n²)），在 `separator.rs:110`、`117-118`、`120` 中每轮调用多次。`save` 与 `CollisionTracker` 的克隆也是 O(n²)，而 `move_items_multi` 每轮都执行 `self.prob.save()`（`separator.rs:206`）与 `self.ct = best_ct.clone()`（`245`），因此每次迭代都复制一次完整矩阵。对于数百个物品的算例，这些操作的开销值得在分析时单独测量。

坐标下降在平台区域上浪费评估次数，如上文所述。`UniformBBoxSampler::new` 对每个旋转都要变换一次形状（`uniform_sampler.rs:246-248`），每次 `search_placement` 调用两次，即 O(旋转数 × 顶点数)，相对于数百次评估而言不算大，但可以缓存。SIMD 对极圆数量较少的形状收益有限，因为其内层循环在 `p2` 数量小于 4 时几乎全部落入标量尾部（`overlap_proxy_simd.rs:164-181`）。

## 十四、疑点与待核实事项

第一，`search.rs:265-266` 与 `uniform_sampler.rs:257-258` 把绝对包围盒当作平移区间使用，其语义依赖于"平移 ≈ 质心"的近似，建议团队确认这是有意设计，并在注释中写明。第二，`best_samples.rs:444-447` 的旋转相似性判定不是循环距离，接近 0 与 2π 的样本可能被重复保留；对连续旋转物品建议改用 `normalize_rotation` 后的最小角差。第三，`best_samples.rs:426` 在 `size == 0` 时会下溢，需要在配置层校验。第四，`tracker.rs:146-153` 的权重无上界，存在溢出为 `inf` 并产生 `NaN` 的风险。第五，`sample_eval.rs:7` 的 `PartialEq` 与 `Ord` 不一致，`Eq` 实现（`39`）名不副实。第六，`SeparationEvaluator` 中 `Invalid` 的双重含义会让日志与追踪难以解读，建议在 `SampleEval` 的文档中明确区分"早停"与"几何无效"，或引入独立的 `Pruned` 变体。第七，坐标下降在 `Clear` 平台上继续运行（`search.rs:299-309` 对所有保留样本都运行），可以考虑在评估值已为 `Clear { loss: 0.0 }` 时跳过。第八，`collision_loss.rs:253` 的 `reload` 参数 `_shape` 在非 SIMD 构建中未使用，SIMD 构建中使用，条件编译下的参数命名容易误导阅读者。第九，`poles_overlap_area_proxy_simd_bounded`（`overlap_proxy_simd.rs:164-181`）的标量尾部循环中，第 `167` 行用同名变量 `p2` 遮蔽了参数 `p2`，虽然语义正确，但极易在修改时引入错误。第十，`tracker.rs` 中对称矩阵的后写覆盖语义在 CDE 检测不对称时可能导致漂移，目前仅以调试断言与 10% 容差兜底（`util/assertions.rs:30-31`）。

本文初版结论来自静态阅读；后续执行证据以迁移决策矩阵和 S01–S24 逐文件记录为准。`float-cmp 0.10.0` 默认容差已在 S10 核实，`rand 0.10` 的有效 `choose` 调用组合与随机状态核对见 S02。性能判断仍需独立基准数据支持。


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

样本比较 `IsBetterSample`（`SampleEval.h` 80–87 行）的等级为 CLEAR 低于 COLLISION 低于 INVALID，损失用 `RustApproxEqualF32` 比较，同 kind NaN 比较会抛出 `std::domain_error`。它对应锁定 `float-cmp 0.10.0` 的 `f32::EPSILON` 或 4 ULP 默认 margin；Rust 同 kind NaN 在 `partial_cmp(...).unwrap()` 处 panic。已列明的边界排列、BestSamples 插入与 CoordinateDescent 近似并列路径有 Debug/Release 参考；Rust `Ord` 非传递造成的任意序列仍未认证，见 S10 与风险 R3。

`UniformRectSampler`（`Source/Sampling/UniformRectSampler.cpp`，172 行）在 `Create`（95–199 行）中为每个旋转计算可行范围，即容器减去旋转后包围盒，再与采样包围盒求交。`Sample`（201–210 行）依次抽取旋转索引、x、y，与 Rust `uniform_sampler.rs` 的 `sample`（79–89 行）顺序一致。`SampleAxis`（69–77 行）使用 `minValue + (maxValue-minValue)*Uniform01F32()`，缺少 rand 在上端点的舍入重试，注释却声称与 rand 0.10 对齐，这一点需要修正注释或补齐逻辑。连续旋转角 `TWO_PI*i/16` 与 Rust `i*(2π/16)` 的 16 个角在当前构建下与 Rust 参考输出一致，由 `UniformSamplerContinuousGridRust.tsv` 按记录容差比较验证（S16）；这不证明跨平台或跨编译器的逐位相同。它被 `Optimizer/LbfSearch.cpp:148` 与 `SeparatorWorker.cpp:180、184` 调用。

`LocalSearchSampler`（`Source/Sampling/LocalSearchSampler.cpp`，206 行）的 `Sample`（152–169 行）使用 `Normal01F32`，`DecayStddev`（194–205 行）按 `init*(limit/init)^progress` 衰减。Rust `sample/` 中没有对应实现，只被 `tests/TestSampling.cpp` 使用。`RotationDistribution`（97 行）的 `FromItem`（60–105 行）构造 16 个连续角或离散列表，`Sample`（107–128 行）只消耗一次 `UniformIndex`，同样只被 LocalSearchSampler 与测试使用。

`SeparatorWorker.cpp` 把上述组件串起来：`BestSamples(nCoordinateDescents, minDim*0.05)`（154–156 行），预细化在 `best.Samples()` 上进行（225–236 行），最终细化在 `best.Best()` 上进行（238–251 行）。`MakeRefinementConfig`（21–45 行）中预细化的平移与旋转步长为 0.25/0.02 与 5°/1°，最终细化为 0.01/0.001 与 0.5°/0.05°，与 `CodeResource/sparrow/src/consts.rs` 10–28 行一致。

## 五、Random：SparrowRng 与 Rust rand 的对齐

`SparrowRng`（`Source/Random/SparrowRng.cpp`，117 行）实现 xoshiro256++，`NextU64`（71–84 行）为标准更新，`NextU32` 取高 32 位，种子由 SplitMix64 展开（45–69 行）。这与 Rust `Xoshiro256PlusPlus::seed_from_u64`（`cli.rs` 75 行、`optimizer/mod.rs` 37 行）的算法结构一致，`Cargo.lock` 锁定 rand 0.10.3 与 rand_distr 0.6.0。

`UniformIndex`（91–111 行）采用 Lemire 宽乘法加进位的第二次抽取，注释称其与 rand 0.10 的 u32 区间映射一致。目前只在算法层面确认，尚未运行验证。金标准包括 `SparrowCore/tests/fixtures/SparrowUniformIndexRustReference.tsv`（`tests/TestRng.cpp` 170 行引用）、seed 42 的 `LbfSeed42Reference.tsv` 检查，以及 `tests/RustReference/src/main.rs` 1146–1175 行生成的 `random_range` 数据，建议构建后执行。`Uniform01F32`（113–117 行）取 23 位尾数。

`Normal01F32`（119–140 行）是带 spare 的 Box-Muller，而 Rust 的 `StandardNormal` 使用 ziggurat，两者分布相同但比特序列不同，因此 LocalSearchSampler 路径无法逐位复现 Rust。`Source/Optimizer/Exploration.cpp` 约 373–440 行的 `NormalZigguratTables` 与 `RustStandardNormalF64` 按 rand_distr 0.6 的 f64 ZIGNOR 镜像了 Ziggurat，因此 Exploration 路径可以对齐，LocalSearchSampler 不能。

RNG 消耗顺序决定复现性：每次 `UniformRectSampler::Sample` 依次消耗旋转索引、x、y；`CoordinateDescent` 每次未改进后重新选轴；`RotationDistribution::Sample` 只消耗一次索引。Rust `choose` 在单元素切片上是否消耗随机数尚未核实，应以固定种子回放对比。

## 六、浮点精度策略

Quantify 与 Eval 全程使用 float32，`CalculatePolygonCollisionLoss` 与 Rust 的 f32 路径一致。为尽量复现 Rust 的 f32 舍入顺序，C++ 通过 `TransformedRustF32`、`ApplyRustF32`、`CentroidRustF32` 显式模拟了 f32 中间舍入；这是源码级对应，类型转换细节尚未完全验证。构建必须关闭 FMA 收缩（GCC/Clang 用 `-ffp-contract=off`，MSVC 用 `/fp:precise`），否则 `minValue + (maxValue-minValue)*u` 与重叠公式可能被融合，产生超过 1 ULP 的偏差。`DebugAssertMatchesLayout`（933–1088 行）中的 `WithinUlps(...,5)` 与 10% pair 容差只用于 Debug 一致性检查。

## 七、差异与待办

第一，`CollisionTracker::EvaluateCandidateLoss(item, transform, idx)`（583–635 行）使用硬编码 epsilon 0.01 且不加 `eps²`，并无条件加入容器惩罚，与 Rust 及精确版本不一致，全仓库无生产调用者，建议删除或改为调用精确版本。第二，`SampleAxis` 缺少上端点重试，需按 rand `UniformFloat::sample_single` 行为补齐或修正注释。第三，`LocalSearchSampler` 与 `RotationDistribution` 在 Rust 中无对应实现，`Normal01F32` 分布也不同，不应视为逐位对齐组件。第四，两级 `lossBound` 的边界差异需对照测试确认。

性能热点包括：`BuildFromLayout` 的 O(n²) 建表与线性 handle 查找；`UpdateMovedItem` 的 `layout.Save()` 拷贝与 O(n²) 放置匹配（180–208 行）；无缓存的全量损失扫描；`BestSamples::Report` 每次构建 `similarIndices` 并做完整插入排序。Rust 侧同样是 O(n²) 建表，但没有 C++ 额外的 `Save()` 拷贝。

## 八、JaguarCore 依赖的调用关系

SparrowCore 通过 JaguarCore 访问碰撞引擎。`SeparationEvaluator`、`LbfEvaluator` 与 `CollisionTracker` 调用引擎的 `CollectPolygonCollisionsUntil` 与 `CollectSurrogateCollisionsUntil`，以回调方式接收 hazard。`CollectSurrogateCollisionsUntil` 中"先回调再插入"的语义尚未验证，它直接影响 `SeparationEvaluator` 中 `lossBound` 的提前停止逻辑，应作为后续核对重点。


# SparrowCore 工程结构说明：公共接口、构建产物、外围工具与测试/Rust 差分体系

## 一、工程概览与构建产物

SparrowCore 根目录为 SparrowCore/，依赖同级 JaguarCore。SparrowCore.pro 通过 $$PWD/../../JaguarCore/Include 引入 JaguarCore 头文件，SparrowCore/SparrowCore.pro 使用 $$PWD/Include/...；仓库同时存在 Include/ 与 include/ 副本，目标 .pro 使用的大写目录存在。Linux 构建失败不能仅由目录大小写推断，需比较两套头文件并确认权威副本和 include root。

除静态库外，工程有四类外围产物。第一是命令行工具 SparrowCli，由 Tools/SparrowCli/SparrowCli.pro 定义，源文件为 Main.cpp 与 OptimizationSignalHandler.cpp，链接 -lSparrowCore -lJaguarCore。它读取 JSON 实例或Rust兼容的完整实例+解输出（`-i/--input`），后者的solution会作为warm-start；默认写入`output/final_<name>.json`与`.svg`，可用`--output`和`--svg`覆盖。支持Rust式`-t/--global-time`总时限按80/20分配、成对`-e/--exploration`与`-c/--compression`分阶段时限、`--workers`两阶段覆盖、`-s/--rng-seed`、`-x/--early-termination`及`--help`；无时限选项时使用480秒探索+120秒压缩。`--time-ms`保留为互斥的C++全局硬时限扩展。未指定seed时，C++由`std::random_device`生成并向stderr报告seed，不是固定0。退出码2至10分别覆盖参数、输入读取、导入、优化、格式转换、序列化、输出、SVG和未处理异常。Ctrl-C由OptimizationSignalHandler.cpp中的HandleCtrlC通过`std::signal(SIGINT, ...)`接收，ScopedCtrlCHandler将其接到phase-stop标志。

第二是 WASM 产物，位于 `Tools/SparrowWasm/`，包括 `SparrowWasm.pro`（staticlib）与 `CMakeLists.txt`。在 Emscripten 分支中，CMake 生成可执行产物，链接参数包括 `-sMODULARIZE=1`、`-sEXPORT_NAME=SparrowWasmModule` 以及 `-sEXPORTED_FUNCTIONS=['_SparrowOptimizeJson','_SparrowOptimizeSvg']`；在非 Emscripten 分支中则生成静态库，输出名为 `sparrow_wasm`。CMake 通过 `SPARROWCORE_LIBRARY` 与 `JAGUARCORE_LIBRARY` 两个变量接收依赖库路径。C 接口声明在 `SparrowWasm.h`，实现在 `SparrowWasm.cpp`。

第三是浏览器 Demo，位于 Demo/，由 Index.html、App.js、Style.css 组成。页面没有网络依赖，包含一个文件选择框、JSON Optimize 运行按钮与独立的 Export SVG preview 按钮、一个状态段落、一个预览图和一个输出块；浏览器端验收仍待完成。

第四是性能基准 `SparrowCoreBench`，由`Bench/SparrowCoreBench.pro`定义，源文件为`BenchMain.cpp`。`SparrowCoreBench all`仍运行`lbf_spp_small`（读取`Tests/Fixtures/LbfSppSmall.json`）和`lbf_spp_large`（代码合成的8类矩形），输出包含可行率、采样拒绝、精确碰撞和checksum的CSV回归指标。新增`SparrowCoreBench <input.json> <time-limit-seconds> <runs>`统计模式，接受任意SPP实例、多次异步run，输出每run和最佳SVG及宽度/利用率的worst、quartile、median、best、average和总体标准差。C++用`std::thread::hardware_concurrency()/nWorkers`估算批次并发，Rust按物理核心数决定，seed也没有逐位派生对齐；模式用于多run统计覆盖，不等价于确定性轨迹对拍。

第五是测试可执行文件 `SparrowCoreTests`，见第三节。

## 二、公共接口与典型调用顺序

公共接口集中在 `include/SparrowCore/` 下。`optimizer/SparrowOptimizer.h` 定义了 `SparrowOptimizationResult`，成员为 `jaguar::SPSolution solution`、`LbfMetrics metrics` 与 `OptimizationHistory history`。`Optimize` 有两个重载：一个接收 `(instance, config, OptimizationListener*, OptimizationObserver*)`，另一个接收 `(instance, config, OptimizationTerminator&, listener, observer)`。`optimizer/SparrowConfig.h` 中的 `SparrowConfig` 包含可选的 `seed`、可选的 `warmStart`（`SPSolution`）、`constructor`、`exploration`、`compression` 与 `terminator`。`MakeRustDefaultImporterConfig()` 返回与 Rust 参考一致的导入参数，其中 quadtreeDepth 为 4，cdThreshold 为 64，poleLimits 为 `{{64,0.0},{16,0.8},{8,0.9}}`，simplifyTolerance 为 0.001。

典型调用顺序是：先由 JSON 字符串构造 `SPInstance`（经 `JsonCodec` 与 `ValidateSPSolution` 等辅助函数），再构造 `SparrowConfig`（CLI 与 WASM 都以 `MakeRustDefaultImporterConfig()` 为基础修改），然后调用 `sparrow::Optimize(instance, config)` 得到 `SparrowOptimizationResult`。SparrowCli现把输入实例与优化解序列化为Rust同形的扁平实例字段加嵌套`solution` JSON，并可将该完整输出文件回读为warm-start；WASM JSON C ABI保留solution-only返回结构。SVG由`ExportLayoutSvg`导出，Sparrow CLI/WASM入口使用Gray主题并关闭surrogate与坐标轴。WASM 的 `OptimizeInput` 固定 seed 为 0，`constructor.nContainerSamples=128`，`exploration.maxIterations=2`，`maxConsecutiveFailures=3`，`compression.maxIterations=2`，`terminator.maxIterations=64`。`SparrowOptimizer.cpp` 中的 `OptimizeWithTerminator` 按顺序派生阶段种子，依次调用 `StartPhase(exploration.timeLimit)` 与 `Explore`，再调用 `StartPhase(compression.timeLimit)` 与 `Compress`，最后通过 `SPProblem::Restore` 与 `Layout::Restore` 校验，失败时返回 `ErrorCode::Infeasible`。

WASM 当前有两个 C ABI 导出：SparrowOptimizeJson 与 SparrowOptimizeSvg。空输入返回 1；输入已接受后的解析、优化、恢复或序列化失败返回 2；输出缓冲区不足返回 3。两个导出入口都捕获 C++ 异常，并以代码 2 和 {"error":"internal error"} 负载返回；CLI main 也捕获异常并以退出码 10 返回。Deployment.md 已按此契约更新。异常路径的外部触发以及 CLI 退出码 10 的运行验证仍待完成。

## 三、测试体系的四层结构

测试入口是 `tests/TestSupport.cpp` 的 `main`（已核对）。若 `argc > 1`，它调用 `RunWindowsConsoleCtrlCProbe`；否则先检查 `sparrow::Version()` 与 `jaguar::Version()` 非空，然后按顺序运行 `RunRngTests`、`RunSamplingTests`、`RunLbfLossTests`、`RunSppLbfTests`、`RunBppLbfTests`、`RunQuantifyTests`、`RunSeparatorTests`、`RunExplorationTests`、`RunWasmTests`，任一失败即返回 1。工程文件 `tests/SparrowCoreTests.pro` 编译全部 `Test*.cpp`，并把 `Tools/SparrowCli/OptimizationSignalHandler.cpp` 与 `Tools/SparrowWasm/SparrowWasm.cpp` 一并编入，因此 WASM 的 C ABI 也能在原生环境直接测试。Debug 链接 `-lSparrowCore -lJaguarCored`，Release 链接 `-lJaguarCore`。

第一层是单元测试，覆盖 RNG（`TestRng.cpp`，7 个）、采样（`TestSampling.cpp`，21 个，使用 `FloatBits` 做逐位比较）、LBF 损失（`TestLbfLoss.cpp`，8 个）、SPP 与 BPP 的 LBF 构造（`TestSppLbf.cpp` 12 个，`TestBppLbf.cpp` 5 个）、量化（`TestQuantify.cpp`，31 个）。第二层是行为不变式测试，主要是 `TestSeparator.cpp`（8 个）与 `TestExploration.cpp`（66 个，约 10668 行）。第三层是 Rust 差分（oracle）测试，核心位于 `tests/RustReference/`。它是一个 Cargo 包 `sparrow-rust-reference`（edition 2024，rust-version 1.90），依赖 `jagua-rs 0.9.0`（git 来源，分支 `jg-explicit-rotations-v0.9`，feature `spp`）与 `rand 0.10` 等，并通过 `sparrow = { path = "../../../CodeResource/sparrow", features = ["oracle-trace"] }` 直接引用 Rust 参考实现。`rust-toolchain.toml` 锁定 1.98.1，目标为 `x86_64-pc-windows-msvc`。`tests/*.rs` 共 56 个文件、145 个 `#[test]`。`pair_matrix_reference.rs` 在 inline `mod quantify` 中使用 `#[path = "../../../../../CodeResource/sparrow/src/quantify/pair_matrix.rs"]`，层数正确；`best_samples_*` 在顶层使用 `#[path = "../../../../CodeResource/sparrow/src/..."]`，同样正确。`exploration_pool_sampling_trace_reference.rs` 使用 `include_str!("../../Fixtures/ExplorationEqualLossPoolSamplerRustTrace.tsv")`。`src/main.rs` 接收多个路径参数，把 Rust 侧轨迹写成 TSV，默认回退路径为 `../Fixtures/...`。

第四层是固定种子回归，即 C++ 侧的 `*CppReference.tsv` 与 `PhaseReference.tsv` 基线，由 `tests/fixtures/` 中约 142 个文件提供，包括 `LbfSppSmall.json`、`LbfBppSmall.json`、`RustSppSingleItem.json`、`SeparatorRoundRustTrace.json`，以及大量 `*RustTrace.tsv` 与 `*RustReference.tsv`。本段列举的 fixture 路径已静态确认存在；整个目录的引用关系与孤立项尚未完成清点（见 E7）。

更新 fixture 的方法是：在 Rust 侧修改后，通过 `RustReference` 的 `main.rs` 重新生成 TSV，人工核对差异后替换 `tests/fixtures/` 中对应文件，并重新运行 `SparrowCoreTests.exe`。C++ 测试代码中没有自动写基线文件的逻辑（grep 未发现 ofstream 写基线），因此基线更新完全依赖人工操作。Rust 侧的脚本是 `tests/RustReference/verify-rust-reference.ps1`，它检查 `rustc -vV` 的 release 为 1.98.1、commit 为 `48a229ceaefd4985c50990b14116b6d856af0985`，然后依次执行 `cargo fmt --check`、`cargo test --locked --target x86_64-pc-windows-msvc` 与 `cargo test --locked --release`。该脚本不生成轨迹，也不运行 C++ 侧的比对。

Windows Ctrl-C 探针由 `tests/TestWindowsConsoleCtrlC.cpp`（仅 `_WIN32`）与 `tests/run-windows-console-ctrlc-probe.ps1` 组成。脚本创建命名事件，以 `--windows-console-ctrlc-probe <ready> <release> <observed>` 启动子进程，父进程调用 `SetConsoleCtrlHandler(NULL, true)`，发送 `GenerateConsoleCtrlEvent(0,0)`，要求子进程退出码为 0 并输出 `WINDOWS_CTRL_C_PROBE_PASS`，默认超时 15 秒。

## 四、许可证边界与覆盖声明

SparrowCore/docs/SourceMap.md 记录了 jagua-rs/lbf 与 sparrow 的来源、许可证和 C++ 文件映射。当前文档使用仓库内 CodeResource/jagua-rs 本地镜像说明，并注明 sparrow 工作源码由负责人本地指定；此前的 NestLab 绝对路径已清理。JaguarCore/tools/RustOracle 当前锁定 Git revision f5e2b5f，而本地镜像曾被记录为与该 revision 有差异；两者对应关系与 fixture 生成基线见移植工作项清单 D5/E4。

`docs/Differential.md` 包含约 70 行的 fixture 矩阵，并定义了验收容差：采样与 CDE 的变换 1e-6、损失 1e-5；LBF 宽度 4e-3、密度 1e-3；Exploration 1e-5；限制为 6 的 Explore 位姿 2e-5、包围盒 2e-4、损失 1e-3。文档还记录了 2026-10-08 与 2026-10-09 两次重复性运行，每个 profile 跑 5 个进程，并给出 SHA-256 黄金值。重跑方法为在 `build\build_Kit_MSVC_2019_x64-*` 目录下执行 `nmake /A /f Makefile.Debug` 与 `Makefile.Release`，并执行 `cargo test --locked --manifest-path SparrowCore/Tests/RustReference/Cargo.toml`。

这里要区分三类证据：FloatBits 断言可证明指定字段和用例逐位相同；固定 fixture 的字段容差比较只证明该输入集合满足容差；SHA-256 重复性记录只证明同平台重复运行的输出稳定，不能证明 Rust 与 C++ 逐位一致。Exploration、Separator、Compression 等大部分轨迹比对属于固定种子回归，不是通用 bit-exact 证明。Differential.md 中的相关表述应按字段与测试逐条理解；矩阵未覆盖的 Rust 参考文件也不能仅凭目录存在推断已纳入 C++ 对拍。

## 五、发布门禁与文档一致性问题

SparrowCore/docs/ReleaseChecklist.md 现已列出 MSVC/Qt Release 构建、测试程序、同平台 checksum、CLI 冒烟及退出码、Windows Ctrl-C、C++/Rust 比较、WASM、Demo 与最终解可行性检查。Bench checksum 只适合同一平台和工具链内比较。verify-rust-reference.ps1 只验证 Rust 侧，不能单独证明 C++ parity；E4 fixture 基线及默认 cold-start 的 Rust/C++ Debug/Release 对照已有 2026-10-17 执行记录，其余门禁仍按各自状态验收。

当前仍需处理的事项如下。其一，SparrowCore 同时存在 Include/include、Tests/tests 两套大小写不同的目录；当前 .pro 所引用的大写目录存在，因此仅凭大小写差异不能断言 Linux 构建失败，G1 的真实风险是两套目录内容漂移。其二，TestWasm.cpp 仍保留依赖目录布局的 fixture 候选路径，需在目标构建目录验证。其三，E4 的锁定 oracle 基线、受影响 fixture 核对及默认 cold-start 的 Debug/Release 对照已有记录。其四，ReleaseChecklist 已列入 Ctrl-C 探针、Demo 与 C++/Rust 比较；用户记录中浏览器 Demo 未运行，CLI 退出码 10 也未外部触发。SourceMap 与 Deployment 的旧绝对路径已清理，根目录生成的 Makefile 已删除。

综上，SparrowCore 的结构包括静态库、CLI、WASM、Demo、Bench 与测试体系。当前应先闭合已决范围内的 oracle/fixture 基线复核与差分覆盖，再处理 Include/include、Tests/tests 双目录的内容漂移、TestWasm.cpp 路径假设和浏览器 Demo 验收。10 月 16 日的 MSVC Debug/Release 全套与 CLI Release 冒烟已有用户执行记录；它们支持该次配置下的构建与测试结论，尚不能替代 E4 的 Rust 基线复核或全范围 parity 验收。
