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
