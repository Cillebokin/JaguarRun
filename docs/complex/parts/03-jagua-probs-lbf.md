# jagua-rs probs 与 lbf 代码逻辑说明

本文基于源码阅读整理，路径相对仓库根 `CodeResource/jagua-rs`，行号取自阅读时的版本。核心碰撞检测只读到调用边界。

## 一、总体结构

`jagua-rs/src/probs/` 下的 `spp`、`bpp`、`mspp` 三个子模块由 feature 开关控制，互不依赖。每个子模块都是四层结构：`entities/instance.rs` 保存不可变实例，`entities/problem.rs` 保存可修改的工作态，`entities/solution.rs` 保存快照，`io/` 负责与 `ext_repr.rs` 定义的外部 JSON 互转。`lbf` 只启用了 `spp` 与 `bpp`（`lbf/Cargo.toml`），`mspp` 只在 dev-dependencies 中出现，所以 MSPP 目前是"数据模型完成、求解器与解导入缺失"的状态。

三类问题都遵循"实例与工作态分离"：`Problem` 持有 `Layout` 与按物件类型记录的剩余需求 `item_demand_qtys`，需求由 `place_item`/`remove_item` 增减，而不是从 layout 反推。

## 二、三类问题的数据模型与状态转移

SPP 的 `SPInstance`（`spp/entities/instance.rs`）包含 `(Arc<Item>, demand)` 列表与 `base_strip`。`Strip`（`spp/entities/strip.rs`）是固定高度、可变宽度的条带，经 `From<Strip> for Container` 转为矩形外轮廓的 `Container`。导入时初始宽度为 `Σ(area×demand)/height`，即 100% 密度宽度（`spp/io/import.rs:43-57`）。`SPProblem`（`spp/entities/problem.rs:11-17`）持有 `strip`、`layout` 与 `item_demand_qtys`。`place_item`（第 61-67 行）先扣减需求，再由 `Layout::place_item`（`jagua-rs/src/entities/layout.rs:93-105`）把放置物登记为 CDE 中的 hazard；`remove_item`（第 69-79 行）为逆操作。`change_strip_width`（第 34-37 行）调用 `Layout::swap_container`（`layout.rs:46-56`），从 `base_cde` 重建 CDE 并重新注册动态 hazard。`fit_strip`（第 40-59 行）把宽度收缩到所有放置物 `bbox.x_max × 1.00001 + offset`。`save`/`restore`（第 81-114 行）基于 `LayoutSnapshot`，`Layout::restore` 要求快照与当前容器的 `base_cde` 为同一 `Arc`（`layout.rs:74-89`），宽度变化后只能走 `from_snapshot` 重建。

BPP 的 `Bin`（`bpp/entities/bin.rs`）带有 `stock` 与 `cost`。导入时按 `base.id` 而非成本排序并过滤零库存（`bpp/io/import.rs:36-58`）。`BPProblem`（`bpp/entities/problem.rs:18-25`）的核心是 `SlotMap<LayKey, Layout>`、`layout_bins` 以及 `bin_stock_qtys`。`place_item`（第 48-66 行）对 `BPLayoutType::Closed` 会新建 layout 并调用 `register_layout`，后者通过 `open_bin` 扣减库存；`remove_item`（第 69-80 行）在 layout 变空时关闭箱子并归还库存。`bin_cost`（第 203-208 行）由库存差值乘以箱型成本得到。

MSPP 的 `Strip`（`mspp/entities/strip.rs`）比 SPP 多一个 `max_width`，初始宽度直接取 `max_width`（`mspp/io/import.rs:38-48`）。`MSPProblem`（`mspp/entities/problem.rs:16-27`）用 `SlotMap` 存 layout，并用 `SecondaryMap<LayKey, Strip>` 存每个条带。`add_layout_from_strip`、`remove_layout`、`change_strip_width`（第 180-188 行）、`fit_strip(lk)`（第 57-76 行）分别管理条带。`extendable_strips` 与 `total_strip_width`（第 234-245 行）对应多条带的扩展与目标。

三类问题的 `save` 都带有 `debug_assert!(problem_matches_solution(...))`。BPP 的校验（`bpp/util/assertions.rs:7-27`）是 O(L²) 的嵌套遍历，而 `Cargo.toml` 的 `[profile.test]` 开启了 debug-assertions，测试中会真实付出这部分开销。

## 三、lbf 主流程

入口是 `lbf/src/main.rs:38-87`。它用 clap 解析 `Cli`（`lbf/src/io/cli.rs`，参数为 `-i -s -c -l -p bpp|spp`），配置缺省时使用 `LBFConfig::default()`（`lbf/src/config.rs:197-217`，`n_samples=5000`、`ls_frac=0.2`、`prng_seed=None`），然后用 `read_spp_instance`/`read_bpp_instance`（`lbf/src/io/mod.rs:267-277`）把 JSON 读成 `ExtSPInstance`/`ExtBPInstance`。

随后 `main_spp`（第 89-127 行）构造 `Importer`，按 `prng_seed` 选择 `SmallRng::seed_from_u64` 或 `rand::make_rng()`（第 100-103 行），调用 `spp::io::import_instance`（`spp/io/import.rs:12-60`）得到实例，再执行 `LBFOptimizerSP::new(...).solve()`，最后导出 JSON 与 SVG。`main_bpp`（第 129-169 行）结构相同，但导出时需要 `instance` 以解析箱的外部 id。

`LBFOptimizerSP::solve`（`lbf/src/opt/lbf_spp.rs:40-128`）先用 `item_placement_order`（`lbf/src/opt/search.rs:252-258`）按直径降序排列物件类型，然后对每个需求未满足的物件调用 `search`。若物件带 `min_quality`，则用 `HazKeyFilter::from_irrelevant_qzones` 构造过滤器（第 59-70 行）。搜索成功则 `place_item`；失败则把宽度乘以 1.1 并通过 `strip_width_is_in_check`（`lbf/src/util/assertions.rs:5-13`）检查（第 91-104 行）。全部放完后执行 `fit_strip` 并 `save`（第 108-114 行）。

`LBFOptimizerBP::solve`（`lbf/src/opt/lbf_bpp.rs:39-96`）的骨架相同，但每次放置前调用 `search_layouts`（第 99-144 行）。候选顺序是：先按 `layouts.keys()` 遍历所有打开的 layout，再遍历 `bin_stock_qtys>0` 的箱型，箱型用 `container.base_cde`（空箱 CDE）搜索。第一个成功的候选即被采用，这是 first-fit。若全部失败则 `break 'inner`（第 73 行）跳过该物件的剩余需求，且没有任何日志或错误。

候选搜索是核心，位于 `lbf/src/opt/search.rs:161-250`。预算在第 180-181 行切分：`ls_sample_budget = n_samples × ls_frac`，其余给均匀采样。均匀阶段（第 183-213 行）用 `UniformRectSampler`（`lbf/src/samplers/uniform_rect_sampler.rs`）在 `cde.bbox()` 内随机采样平移，旋转按 `allowed_rotation` 从连续区间、离散集合或固定值中选取。每个样本先用 `detect_surrogate_collision`（`jagua-rs/src/collision_detection/cd_engine.rs:205`）做快速拒绝，通过后用 `buffer.transform_from` 计算包围框损失 `LBFLoss`（`lbf/src/opt/loss.rs`，成本为 `10·x_max + y_max`）。只有损失严格更优时才调用精确的 `detect_poly_collision`（`cd_engine.rs:161`）。一旦接受新最优，`tighten_sample_bbox`（`loss.rs:163-171`）就把采样区域的 `x_max` 收紧到 `cost/10`。局部搜索阶段（第 216-249 行）以最优为均值，用 `LSSampler`（`lbf/src/samplers/ls_sampler.rs`）做正态扰动，平移标准差从容器包围框最大边长的 1% 指数衰减到 0.05%，旋转标准差从 2° 衰减到 0.5°，每次改进调用 `shift_mean`。若均匀阶段一无所获，第 216 行的 `?` 直接返回 `None`，局部搜索预算被整体跳过。

输出阶段，JSON 由 `io/output.rs` 中的 `SPOutput`/`BPOutput` 合并实例、解与配置后写出（`lbf/src/io/mod.rs:279-290`），SVG 由 `s_layout_to_svg` 生成。lbf 自身不调用 `remove_item`、`restore`、`remove_layout`，因此这些路径只被测试或外部调用方使用。

## 四、与 jagua-rs 核心的调用关系

lbf 只使用 `Layout::cde()`、`Layout::place_item`、`Layout::save`、`CDEngine::detect_surrogate_collision`、`CDEngine::detect_poly_collision`、`CDEngine::bbox`、`Container::base_cde` 与 `Item::shape_cd.surrogate()`。`Container::new`（`jagua-rs/src/entities/container.rs:54-69`）用 `outer.bbox.inflate_to_square()` 构造 `base_cde`，所以 `cde.bbox()` 是容器的膨胀正方形而非容器本身。`Layout::new`（`layout.rs:27-34`）与 `swap_container` 都会深克隆 `base_cde`，BPP 每开一个箱子、SPP 每改一次宽度都要付出一次 CDE 重建的代价。`place_item` 与 `remove_item` 在 debug 构建中还会执行 `layout_qt_matches_fresh_qt` 全量校验。

## 五、随机数与并行

随机源是贯穿 `search`、`search_layouts` 与所有采样器的单个 `SmallRng`，通过 `&mut impl Rng` 传递。有种子时结果可复现，无种子时依赖系统熵。WASM 演示在无种子时使用固定种子 `0x12345678`（`lbf/src/util/wasm_demo.rs:84`、`161`）。并行只出现在导入阶段，SPP/BPP/MSPP 的 `import_instance` 使用 `into_par_iter`/`par_iter`（如 `spp/io/import.rs:27`、`bpp/io/import.rs:25`、`46`），`collect` 保证顺序。求解器本身是串行的，采样未使用 rayon，这是潜在的加速点。

## 六、疑点、隐含假设与风险

第一，质量过滤器的方向很可能是反的。`HazKeyFilter::from_irrelevant_qzones`（`jagua-rs/src/collision_detection/hazards/filter.rs`，实现约在第 31 行）把 `quality < required_quality` 的质量区域标为"无关"，即忽略其碰撞。但同函数文档（第 20 行）写的是"质量大于等于要求的区域被忽略"；`container.rs:86` 说明"质量越高越好"；`ext_repr.rs:13-14` 说明 `min_quality` 是最低要求；`layout_to_svg.rs` 约 405-416 行中跳过条件是 `quality >= required`。三处都与代码相反。若判断正确，带 `min_quality` 的物件会被允许压在低质量区域上，结果非法。`assets/baldacci1.json` 含 `min_quality: 1`，建议用小算例核实。

第二，`strip_width_is_in_check` 是启发式：宽度不小于物件直径和的两倍时 panic，且不考虑条带高度。若某物件直径超过条带高度，它永远放不下，宽度会按 10% 步长增长直至 panic，而不是返回错误。

第三，BPP 的失败是静默的。`break 'inner` 之后未放置的需求既不记录也不报错，返回值中数量悄悄变少。

第四，BPP 不做成本决策。候选顺序由 SlotMap 遍历顺序与外部 id 决定，`bin_cost` 只用于报告。每个候选都用完整的 5000 次预算搜索，一个放不下的物件会在所有打开的 layout 与所有有库存箱型上各采样一遍，开销约为 `(打开 layout 数 + 有库存箱型数) × n_samples`，是箱型多时的主要瓶颈。

第五，采样区域是膨胀正方形（`container.rs:61`）。宽高比悬殊的条带会浪费大量样本在外壳之外，局部搜索的标准差也因此偏大（`ls_sampler.rs:98-100`）。

第六，`tighten_sample_bbox` 的正确性依赖于形状局部坐标非负这一未经断言的前提。若 `cost/10 ≤ x_min`，`UniformRectSampler::new` 中的 `Uniform::new(...).unwrap()`（`uniform_rect_sampler.rs:22-23`）会 panic。

第七，`search.rs:196` 的 `partial_cmp(...).unwrap()` 遇到 NaN 会 panic；`rotation_distr.rs:192` 的 `choose(...).unwrap()` 在离散旋转集为空时 panic。两者都属低概率。

第八，`SPProblem::fit_strip`（`spp/entities/problem.rs:40-59`）与 `MSPProblem::fit_strip`（`mspp/entities/problem.rs:57-76`）对 `max_by(...).unwrap()` 没有空检查，布局为空时会 panic。lbf 路径保证至少放入一个物件，但作为库 API 是隐患。

第九，`restore` 不保留 `LayKey`。`BPProblem::restore`（`bpp/entities/problem.rs:133-139`）与 `MSPProblem::restore`（`mspp/entities/problem.rs:149-155`）对快照中新增的 layout 调用 `insert`，产生新 key。调用方若在快照前保存了 key，恢复后可能失效，返回的 `bool` 需要被显式检查。

第十，文档与实现不一致。`MSPProblem::change_strip_width` 的注释说宽度非正时 layout 会被移除（`mspp/entities/problem.rs:178-179`），实际是 `assert!` 直接 panic。`restore` 注释中的"different bin"（第 141 行）是从 BPP 复制的残留。`config.rs:22` 的 `ls_frac` 注释提到的 `n_samples_per_item` 并不存在。

第十一，`bpp::io::import_solution`（`bpp/io/import.rs:63-67`）与 `mspp::io::import_solution` 都是 `unimplemented!()`，BPP 与 MSPP 无法热启动，只有 SPP 能通过逐个放置重建解（`spp/io/import.rs:63-80`）。


## 七、测试与基准

`lbf/tests/` 以标准算例（`assets/`）做完整求解回归，并包含与 C++ 参考实现的差分测试（从文件名判断，断言细节未逐行核对）。`lbf/benches/` 主要测 CDE 的收集、检测、更新与快速失败性能。

总体上 SPP 路径完整，BPP 路径可用但策略粗糙，MSPP 仅完成数据模型。优先核实的是第六节第一条的过滤器方向，其次是 BPP 候选搜索成本与采样区域膨胀问题。
