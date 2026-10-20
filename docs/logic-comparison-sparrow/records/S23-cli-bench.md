# S23. main.rs + cli.rs + bench.rs 逐文件逻辑比对

**Rust**：`CodeResource/sparrow/src/main.rs`、`src/cli.rs`、`src/util/io.rs`、`src/bench.rs`  
**C++**：`SparrowCore/Tools/SparrowCli/Main.cpp`、`SparrowCore/Bench/BenchMain.cpp`  
**判定**：部分。CLI 主参数、阶段时限、默认输出路径、worker覆盖、帮助入口，以及任意实例多次Bench和统计输出已接入；日志/监听器行为、随机种子派生、Bench并发批大小及底层运行轨迹仍有边界差异。

## CLI 参数与配置映射

| 语义 | Rust CLI | C++ SparrowCli | 判定 |
|---|---|---|---|
| 输入 | `-i/--input`，可读实例或完整输出文件 | `-i/--input`，支持同两类文件，完整输出解作为warm-start | 已映射，完整输出回读验收见S22 |
| 总时限 | `-t/--global-time <秒>`，按80%/20%分给探索与压缩 | 同名参数，设置两个phase timeLimit | 已映射 |
| 分阶段时限 | `-e/--exploration`与`-c/--compression`必须成对 | 同名参数，必须成对且与总时限互斥 | 已映射 |
| 未指定时限 | 总计600秒，探索480秒、压缩120秒 | 使用相同phase预算 | 已映射；库自身默认值仍为9分钟/60秒，覆盖仅属于CLI |
| 零时限 | 零时长phase deadline | `--global-time 0`或`-e 0 -c 0`启动零时长phase deadline | 已映射；历史扩展`--time-ms 0`仍表示关闭全局硬时限 |
| 早停 | `-x/--early-termination` | 同名短长选项 | 已映射：探索连续失败阈值10，压缩使用FailureBased衰减 |
| RNG种子 | `-s/--rng-seed <u64>`；未指定时随机产生 | 支持`-s/--rng-seed`及兼容旧名`--seed`；未指定时由库随机产生 | 参数已映射；随机源和内部派生序列不同 |
| Worker | `--workers <NonZeroUsize>`覆盖两阶段 | `--workers <正整数>`覆盖两阶段separator | 已映射；0及溢出值作为参数错误拒绝 |
| 输出 | `output/final_{name}.json`和最终SVG | 相同默认文件名；`--output`、`--svg`可覆盖路径 | 默认路径已映射；Rust的中间SVG与`data/live`仍由S24/宿主监听器范围承接 |
| C++扩展 | 无`--samples`、`--time-ms` | 保留`--samples`；`--time-ms`是包含构造阶段的全局硬上限 | 扩展项；`--time-ms`与Rust式阶段时限互斥 |

Rust式phase时限从构造或warm-start恢复完成后开始，C++的`Optimize`也在构造完成后启动phase deadline。C++额外的`--time-ms`在调用`Optimize`前启动全局计时，因此仍会把构造耗时计入。总时限拆分使用整数纳秒比例；对CLI接收的整秒值，结果与Rust 0.8/0.2拆分一致。

未指定seed时，Rust CLI使用`rand::random()`生成seed；C++优化器使用`std::random_device`和均匀分布生成seed，并把实际seed写到stderr。旧记录中“C++默认seed=0”的说法有误，已更正。显式seed入口对应，但两侧RNG算法和后续流派生仍由S01及各算法项的oracle覆盖范围界定。

帮助信息写入stdout，参数/输入/优化/导出错误写入stderr；C++保留退出码2至10的细分错误，Rust CLI错误统一由进程入口返回失败。C++输出实际seed和最终宽度/密度摘要；Rust还初始化stdout与`output/log.txt`日志，并通过listener导出中间SVG、可选live SVG。日志、事件频率和中间产物不宣称等价。

## Bench入口与统计

`SparrowCoreBench all`保留原来的两个固定回归场景、固定seed=42及CSV/checksum输出。新增Rust式统计模式：

```text
SparrowCoreBench <input.json> <time-limit-seconds> <runs>
```

统计模式可读任意SPP实例或完整实例+解输出；完整输出中的解不作为Bench warm-start，这与Rust `bench.rs`只取`read_spp_input`返回的实例并从LBF构造的流程一致。每次run取得独立随机seed，探索和压缩时限按80%/20%分配，并以`Optimize`运行完整求解；run结果按默认separator worker数分批异步执行。批次大小用`std::thread::hardware_concurrency()/nWorkers`估算并至少为1，Rust则按物理核心数计算，所以并发批次上限不是逐平台相同。

每次实际run导出`output/final_bench_{i}.svg`，最佳利用率解导出`output/final_best_{name}.svg`。控制台列出`requested_runs`与`actual_runs`、宽度/利用率序列以及worst、25%、median、75%、best、average、总体标准差，百分位按Rust使用的Excel `(n-1)`插值公式计算。C++使用f32统计值以接近Rust `f32`输出。

Rust Bench逐段调用LBFBuilder、exploration_phase与compression_phase，并从一个root RNG派生每次run和每个阶段的seed；C++统计模式调用顶层`Optimize`，seed由系统随机源分别生成。因此这套统计入口用于覆盖任意实例、多run分布和输出产物，不构成逐run确定性轨迹对拍。Rust打印git commit hash，C++打印编译版本串；本次没有运行Git命令。

### 末批run数裁决（2026-10-19）

锁定Rust `bench.rs` 先以`ceil(requested_runs / runs_per_batch)`计算批数，随后每批都创建完整`runs_per_batch`个结果槽；末批余数也会执行完整批次。D2将Rust Bench多run统计纳入认证范围，因此C++现在按同一批次形状执行，接受实际运行数超过`<runs>`的 pinned 行为，不保留“严格按请求数截断”的产品化差异。C++与Rust仍分别使用逻辑硬件线程数和物理核心数估算批大小，因此两侧批容量不同的平台上，超出的run总数也可能不同；这项调度残差仍属于S23未闭合范围。

## 运行验收（2026-10-19）

- VS2019 MSVC 14.29 + Qt 6.5.3 `msvc2019_64` Release增量构建：SparrowCli与SparrowCoreBench均通过。
- SparrowCli `--help`退出码0且只写stdout；未知选项退出码2并只写stderr；Rust式时间参数冲突及`--workers 0`均退出码2。
- CLI及Bench对超出有符号纳秒表示范围的秒数均拒绝，退出码2。
- 使用`RustReference/spp_instance.json`、`--global-time 0 --workers 1 -s 42`运行CLI，退出码0；省略`--output/--svg`时也生成默认`output/final_differential-spp.json/.svg`。
- SparrowCoreBench对同一实例运行`0`秒、`2`次，退出码0，输出统计项，并生成两个run SVG与一个best SVG；再用`1`秒总时限、`2`次自然时限运行，约1.03秒完成且退出码0。
- SparrowCli用`-t 1 --workers 1 -s 42`自然时限运行约1.03秒并退出0。
- 以上为VS2019 Release定向构建及CLI/Bench smoke；未运行全量测试套件，不外推为算法结果parity。

## 尚存范围

1. Rust stdout/file logger、live及intermediate SVG listener路径与事件顺序仍按S24和S22登记。
2. Rust未指定seed时的entropy来源、Bench root RNG到每阶段的seed派生没有逐位对齐。
3. Rust Bench按物理核心数限并发；C++目前使用标准库报告的硬件线程数估算。
4. Rust Bench逐阶段编排与C++调用顶层`Optimize`存在应用层轨迹差异；统计输出及SVG格式/内容继承S22记录的SVG结构边界。
5. `--time-ms`是C++扩展，含构造阶段的全局硬截止；Rust式时限参数仅限制各phase，构造阶段不消耗phase预算。
6. C++已复现Rust末批分配完整批容量的行为；请求run数不能被解读为保证实际执行次数。由于Rust按物理核心、C++按逻辑硬件线程估算批容量，跨平台的实际超额数量仍不保证相同。
7. C++以有符号`std::chrono::nanoseconds`承载时限，秒数超过其约292年表示上限会作为参数错误拒绝；Rust `Duration`的秒字段范围更大。实际排样预算远低于该上限，未为超大时限增加更宽的内部表示。
