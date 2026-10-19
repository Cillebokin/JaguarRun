# E4 oracle 与 fixture 基线执行记录（2026-10-16）

## 结论与边界

两处 Rust oracle 已按锁定工具链与 jagua-rs revision 运行。Sparrow RustReference 的 Debug、Release 测试均通过；fixture 生成器连续运行两次得到相同结果，13 个生成文件在逐一核对后全部与工作区 fixture 一致。为匹配生成器输出，已更新 SeparatorRoundRustTrace.tsv 的注释头；C++ 读取器会跳过以 # 开头的注释行。2026-10-17 又在 Rust Debug/Release 下重跑默认 cold-start reference，并在重建后的 SparrowCore Debug/Release 全套测试程序中复核跨语言对照；E4 冷启动 fixture 待办据此收口。

Jaguar RustOracle 的默认入口与专用入口在 Debug、Release 下均退出码 0。输出揭示了若干既有的 profile 差异，记录在下文。2026-10-17 按锁定配置重算 B2，并新增 B4a filtered-`Entire` 可达性探针复核 Debug/Release 输出。Rust oracle 通过只证明参考程序成功运行，不单独证明 JaguarCore/SparrowCore 与 Rust 等价；冷启动 fixture 已在下文完成复核，E4 的基线与受影响 fixture 验收项现已完成。

2026-10-16 的 E4 baseline 运行未执行 C++ 构建或原生测试；2026-10-17 的 B2/B4a/B6/B7 复核各自记录了对应的 C++ 定向 runner。工作区中的构建产物未清理。

## 锁定基线与源码指纹

| 项目 | 值 |
| --- | --- |
| rustc | 1.98.1，commit 48a229ceaefd4985c50990b14116b6d856af0985，host x86_64-pc-windows-msvc，LLVM 22.1.8 |
| Cargo | 1.98.1（797e8a9bc，2026-08-05） |
| jagua-rs | 0.9.0，revision f5e2b5ffd14931f17a20ce283a2122e877953ceb |
| RustReference 本地 sparrow 依赖 | CodeResource/sparrow 是 path dependency，Cargo.lock 不锁其内容；155 文件树指纹见下表 |
| Sparrow feature | RustReference 的 jagua-rs 启用 spp；本地 sparrow 启用 oracle-trace |

Cargo 文件 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| JaguarCore/tools/RustOracle/Cargo.toml | F766C960B25A3EC2444DC29A554DB777C2CD0417DF28A1F10F486DDFAF3967F5 |
| JaguarCore/tools/RustOracle/Cargo.lock | 56A80C9D8AA29CCEB64BDCCFA6C29F37A57A17F191D6378D46A5ED9A233F00CC |
| JaguarCore/tools/RustOracle/rust-toolchain.toml | C4ECCA935CE816FF3DD750F0CBD1F0742CE3268BB2851345D33BD06CBF7E9E99 |
| SparrowCore/Tests/RustReference/Cargo.toml | B36C8903AC69CA22DC96CCB9562A52DC3F07C9EE79A725C749B6867970B90631 |
| SparrowCore/Tests/RustReference/Cargo.lock | 42564072F4E9C7198F28576EE00F5D8A22ED1583F715ABD86B8E9E94FDD3ED54 |
| SparrowCore/Tests/RustReference/rust-toolchain.toml | C4ECCA935CE816FF3DD750F0CBD1F0742CE3268BB2851345D33BD06CBF7E9E99 |

树指纹算法：排除目录名为 target 或 .git 的文件，按相对路径排序；将每个文件的“相对路径、NUL 字节、原始文件 SHA-256、换行”拼接为 UTF-8，再计算 SHA-256。

| 内容 | 文件数 | SHA-256 |
| --- | ---: | --- |
| CodeResource/sparrow | 155 | 7CA0B21CC46EEBAFC11477D4128F80985ACA45CC8EE22DFCB09C47EB6B10A675 |
| SparrowCore/Tests/RustReference/src | 7 | A730A4AEE96895C2A64242B54AC1DE298E2CF76361A3C065DF30346DB4DDE7F9 |
| SparrowCore/Tests/RustReference/tests | 56 | BA0EF8FF7D2E2DEAC2CA4D6E66A357A6B1AFD6F3C22BD47E342C93ED9A0F2846 |
| SparrowCore/Tests/Fixtures（更新后） | 142 | 0EED773EDA2AAB3E3A8E5C263EFBF73446A53389F1CDDB94EED1C0E78F1E8B7B |
| JaguarCore/tools/RustOracle/src | 1 | 0309C39B21347826550636F5D485A26CCBEA850F7F9F7AA385548401234FE0AB |

Sparrow fixture 生成器读取的输入 SHA-256：

| 输入 | SHA-256 |
| --- | --- |
| SparrowCore/Tests/Fixtures/RustSppSingleItem.json | 40A75AFCAA9EED4D058C27AAB420669481033CC862383B37A548424FAC6EEF72 |
| SparrowCore/Tests/Fixtures/SeparatorRustTrace.json | 042EAC8AD0AAD088C65BD4AD0BE348A2000439AC5EBFF8677575652199D6B187 |
| SparrowCore/Tests/Fixtures/SeparatorRoundRustTrace.json | AC17609E72DEF9EF43098ABE9F74EC4287B9A9CE8EB868CB969B0A3DB524E12B |
| SparrowCore/Tests/Fixtures/ExplorationPool.json | 68AD141621C41790CA209E794ACB44209D9341A1DF1ACC4A5B0BD3F323BF60BE |
| SparrowCore/Tests/Fixtures/ExplorationContained.json | 28D1AD5F10A184E0C80F56232E602EA96F3C0B4CB113D8B94D2ADA8DC05359AB |

## Sparrow RustReference

执行入口：

> .\SparrowCore\Tests\RustReference\verify-rust-reference.ps1

该脚本核对 rustc/Cargo 版本和 host，执行 cargo fmt --check，并分别执行锁定的 Debug 与 Release 测试。结果为 Debug 143 passed、0 failed、2 ignored；Release 143 passed、0 failed、2 ignored；脚本退出码 0。该脚本不生成 fixture。

生成器使用 Rust 1.98.1 Release、Cargo.lock 与 sparrow-rust-reference 二进制。生成输出连续运行两次完全一致。对比时将 CRLF 统一为 LF，再以 UTF-8 无 BOM 计算 SHA-256：

| 生成文件 | SHA-256 |
| --- | --- |
| SparrowLbfRustReference.tsv | 31495F5BBC22C8C8AEFB9D638D70FCA643B7E183FEFD50FA62AF7C5A48504786 |
| LbfSeed42Reference.tsv | 83C8DBF0399E5E6723ACE5754E4E2EEB2A912CD03167DBE135A0C66C7046BF95 |
| SamplingCoordinateDescentRustReference.tsv | D46B4004FCEE4C013B53B8BB51D489B22B19E792F7BF97C65E6077F565D3B63E |
| SparrowUniformIndexRustReference.tsv | 7105F81E4747890C5DCD38701B03CE0A0077B81F49683EAAD9B5C6A156858F49 |
| LbfSearchRustTrace.tsv | 82880C9CDDF985FA2A0150BF757DBE05950720D9E8C22AE31F330C8C8B095C04 |
| SeparatorSearchRustTrace.tsv | CE0DD8C97414DFA7CA8F08BBA434095A3E444AE0C7C04B307DB35EED6611D080 |
| SeparatorRoundRustTrace.tsv | 6EDB3ED8E1CEB4C9CA5A88CE034B6E9C9C9F3B69381EAC3AB205D377D6EC9E90 |
| SeparatorMultiWorkerRoundRustTrace.tsv | 8B07AB0323E4D5D5AEF8E58897FB48BF7EC23AF6666752381C5DC10DA2060C9F |
| SeparatorThreeWorkerSeed43RoundRustTrace.tsv | 21A194C37CA604A58BE02238E060034086083C1B56E1116EE4817B038CBD3583 |
| ExplorationRustTrace.tsv | 91E03C395161EE16D4376E9990A797FDEB497B7E23D41079A2A4BF31D819476A |
| ExplorationPoolRustTrace.tsv | 61D95AFAAA68961DA5D8149800135E1F9CD336247332885680964E471FFCD9D2 |
| ExplorationContainedRustTrace.tsv | CD6E44586DB283B43A89D0837453D664444D2AF89B4D34F4F2841CC6C142EC2E |
| UniformSamplerNarrowIntervalRust.tsv | C331D95F7B1922A46C70E8D5F22214AB4C9047B29C57FF2871485FC021872B31 |

首次比对有 12 个文件完全匹配；SeparatorRoundRustTrace.tsv 仅注释头不同：旧值为“with one worker”，生成器值为“with 1 workers”，其余 68 行相同。已将该文件更新为锁定生成器输出；SparrowCore/Tests/TestSeparator.cpp 的读取器跳过 # 注释行，数值记录未变。

旧文档生成命令因 Cargo 包含多个 binary 而未指定目标，运行时产生目标歧义；现在已补上 --bin sparrow-rust-reference，并补齐最后一个输出参数 UniformSamplerNarrowIntervalRust.tsv，避免其默认相对路径落到预期目录之外。E4 核查时所有新输出先写入临时目录，比较后才替换上述单个注释头。

## Jaguar RustOracle

命令模板：

> rustup run 1.98.1 cargo run --quiet --locked [--release] --manifest-path JaguarCore/tools/RustOracle/Cargo.toml -- [mode]

每个 mode 都分别运行 Debug 与 Release，均退出码 0。stdout 原始字节单独保存并计算 SHA-256；stderr 不纳入输出哈希，因为 catch_unwind 探针触发的 panic hook 含进程号。一次离线尝试因缓存缺少锁定的 serde_json 1.0.154 而失败；随后允许 Cargo 获取锁定依赖，以 --locked 重跑通过，Cargo.lock 未被更新。

| Mode | stdout 行数 | Debug stdout SHA-256 | Release stdout SHA-256 |
| --- | ---: | --- | --- |
| 默认完整输出 | 227 | 7721497B38E854E97FB1433156C81D1E23EA4EDF6BA1689A2DE8354F8593A1D1 | 0B5BD13069D821653942CEC6454135ADECBFC4F34777A5BD3B2F85513048B3B9 |
| collision-parity-only | 6 | BE63225D3C4417E95FAA51C0DC4D16604597C8C9E58EC7EE4567ACAE9EBAB7F5 | BE63225D3C4417E95FAA51C0DC4D16604597C8C9E58EC7EE4567ACAE9EBAB7F5 |
| model-boundary-parity-only | 4 | 291CC9E4ADCB325BF22543A027E9003E77702845E90461CFF185AF1F4A46B2A4 | 1CADE65E3CC2CEA9F7C0AED2E6F8FA5E056E0B5A4CB761A642136FE15C203FE9 |
| restore-boundary-parity-only | 4 | 1D8151021BA7B0962A336881CB66754A2B714553BE303643C640AB33B66DEC9B | A6D3F5049675CD3B9FD42B5098456FC65C0891EA06678ABEDBD6C8FF5B2B08A9 |
| offset-boundary-parity-only | 4 | 8242A346022F53D9BB8E6D6A5AB2C8E0187519E1B5446CFBD21B155305DDEC05 | 2B0099713CF2073CF27B1FB82AE3787F9BFD02C7A56204C45CBFA71279A145D4 |
| import-boundary-parity-only | 8 | 8EB10446F4F1C7B66CD4E5E238EB6D647494A538EDE6EBC827AF0FA56904CF21 | 8EB10446F4F1C7B66CD4E5E238EB6D647494A538EDE6EBC827AF0FA56904CF21 |
| zero-separation-parity-only | 6 | 5BFCA78E7E7D79FFF6905731B7AB83EF3771CB7560CC88B9BF99451E9D2E5C45 | 5BFCA78E7E7D79FFF6905731B7AB83EF3771CB7560CC88B9BF99451E9D2E5C45 |
| shape-modification-b2-parity-only（2026-10-17） | 3 | DD237C8CE327A252A384CF41FEA9F70B12E38634F9B8062466D3ED9D490BBD71 | DD237C8CE327A252A384CF41FEA9F70B12E38634F9B8062466D3ED9D490BBD71 |
| collision-entire-b4a-parity-only（2026-10-17） | 3 | DD6BC8B5C0F4A986FB16A61335121306B52119F15F0852042E540A84B8D5FDFF | DD6BC8B5C0F4A986FB16A61335121306B52119F15F0852042E540A84B8D5FDFF |
| layout-lifecycle-parity-only（2026-10-17） | 18 | F1C7267D87FBB547FA06B8F0C6D15DE3066D6D353757B1A6C0666C7CB8155A36 | F1C7267D87FBB547FA06B8F0C6D15DE3066D6D353757B1A6C0666C7CB8155A36 |
| problem-types-b7-parity-only（2026-10-17） | 21 | 0E92E7EB1E17D3B02685DB54B68284E339A109D6CF91B1C35B409D04399C9121 | 0E92E7EB1E17D3B02685DB54B68284E339A109D6CF91B1C35B409D04399C9121 |

### B2 形状修改重算（2026-10-17）

执行命令，Debug 与 Release 分别运行：

```powershell
rustup run 1.98.1 cargo run --quiet --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --shape-modification-b2-parity-only
rustup run 1.98.1 cargo run --quiet --release --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --shape-modification-b2-parity-only
```

两条命令均使用 `rustc 1.98.1`、Cargo 1.98.1 与 Cargo.lock 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。每个 profile 输出 3 行，Debug/Release 原始 stdout 均为 SHA-256 `DD237C8CE327A252A384CF41FEA9F70B12E38634F9B8062466D3ED9D490BBD71`；两者逐字匹配 fixture 非注释行。fixture `shape_modification_b2.txt` 共 3 个键，SHA-256 `540E5DEBB0123CD9A5FC6783EE9242EAF3AC2215AED5C4AF6276C8A38FE77F67`。

矩阵包括：非环绕凹陷在 `cutoff_area_ratio=0.0015` 时由半开切片得到 0.1 子面积并闭合为 6 顶点；若切片包含 j 端点则面积为 0.2、超过 0.147 上限。首尾环绕凹陷用于覆盖 wrap 分支。Deflate 窄凸起在 Rust/C++ 中都保持 7 顶点、面积 `100.099998474`。pinned Rust 注释要求 Deflate 采用正子面积，但实际代码与 Inflate 一样过滤所有非负子面积。B2 本轮选择保持 pinned Rust 当前行为；这是已记录的源代码注释/实现冲突，不是 Rust/C++ 的观测行为差异。本轮未修改生产几何算法。

当前 workspace JaguarCore 以 MSVC 2019 x64 分别新建 Debug/Release 构建；B2 定向 runner 双 profile 退出码 0，Release 断言启用；完整 `CompareGeometry` Release runner 也通过。完整 `JaguarCoreTests.exe` 未运行。RustOracle 源文件树指纹已在 B4a 新增入口后更新至上表；B2 fixture 与详细行为结论见 [几何记录](../logic-comparison-jagua/records/80-geometry.md#b2-形状修改算法核对)。

### B4a：虚拟根中的 filtered `Entire`（2026-10-17）

执行命令，Debug 与 Release 分别运行：

```powershell
rustup run 1.98.1 cargo run --quiet --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --collision-entire-b4a-parity-only
rustup run 1.98.1 cargo run --quiet --release --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --collision-entire-b4a-parity-only
```

RustOracle 确认 query virtual root 中有被过滤的 Hole/`Entire`；不加过滤时返回 `true`，以 `HazKeyFilter` 忽略 Hole 时捕获 `unreachable!()` panic。Debug/Release 均输出 3 行，原始 stdout SHA-256 都是 `DD6BC8B5C0F4A986FB16A61335121306B52119F15F0852042E540A84B8D5FDFF`，与 fixture 非注释行相同；fixture `collision_entire_b4a.txt` SHA-256 为 `C24C8598E2FDDDB0A21CA92C2CDBC49923735CC9AEEF4197782351451B197A09`。

C++ `CompareGeometry.cpp` 中的 B4a runner 用相同 bounds、Exterior、Hole、查询多边形和按 handle 过滤的 HazardFilter；MSVC 2019 x64 Debug/Release 均通过，Release 断言启用。未过滤查询返回 `true`，忽略 Hole 后返回 `false`。因此 `CollisionEngine.cpp:255` 的 `Entire → true` 只对应 Rust 边查询的 relevant-hazard 早退；运行差异在 Rust 后续 containment pass 无条件 `unreachable!()` 与 C++ 仅处理 `Partial`。本次处置为保留 C++ 的过滤器语义，不传播 Rust panic；这是固定合法输入上的已确认 pinned 行为差异，B4 整体仍部分。详细记录见 [B4a 碰撞记录](../logic-comparison-jagua/records/30-collision-detection.md#b4a虚拟根中的-filtered-entire-hazard)。

### B6 生命周期矩阵（2026-10-17）

执行命令，Debug 与 Release 分别运行：

~~~powershell
rustup run 1.98.1 cargo run --quiet --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --layout-lifecycle-parity-only
rustup run 1.98.1 cargo run --quiet --release --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --layout-lifecycle-parity-only
~~~

两条 Rust 命令均使用 Cargo.lock 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。每个 profile 输出 18 行，Debug/Release 原始 stdout 的 SHA-256 均为 `F1C7267D87FBB547FA06B8F0C6D15DE3066D6D353757B1A6C0666C7CB8155A36`，与 `layout_lifecycle.txt` 的非注释行逐行一致；fixture 文件 SHA-256 为 `867825C4F32C6F442CB083ACB72A8BD6AFA30F7CDCB72D1E8F5B2CCD3F31EAED`。

矩阵覆盖容器复制和同值重建的基础身份、同源快照恢复、恢复前后句柄有效性、动态 hazard 数量、换容器后旧快照拒绝及失败状态，以及 `Items()` 首次出现/去重次序。C++ `CompareLayoutLifecycle.cpp` 已加入 `JaguarCoreTests.pro` 和 `TestSupport`；MSVC 2019 x64 Debug/Release 均重建通过，B6 定向 runner 双 profile 退出码为 0，Release 测试断言启用。完整 `JaguarCoreTests.exe` 未运行。B6 固定矩阵及对象地址身份差异详见[实体记录](../logic-comparison-jagua/records/20-entities.md#b6-固定生命周期差分矩阵2026-10-17)。

### B7 SPP/BPP/MSPP 问题类型矩阵（2026-10-17）

执行命令，Debug 与 Release 分别运行：

```powershell
rustup run 1.98.1 cargo run --quiet --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --problem-types-b7-parity-only
rustup run 1.98.1 cargo run --quiet --release --locked --manifest-path JaguarCore/Tools/RustOracle/Cargo.toml -- --problem-types-b7-parity-only
```

两条命令使用 Cargo.lock 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`。Debug/Release 各输出 21 行，逐行匹配 `problem_types_b7.txt`；两份 stdout SHA-256 均为 `0E92E7EB1E17D3B02685DB54B68284E339A109D6CF91B1C35B409D04399C9121`。fixture SHA-256 为 `17336ED674AB4D02230D3F8AA31C771E60008FB94B40807FCFB1590003B22B7E`。Rust oracle 通过 pinned `io::import_instance` 读取同一 SPP/BPP/MSPP JSON，C++ 通过现有 JsonCodec/Importer 导入相同文件。C++ `CompareProblem.cpp` 定向 runner 的 MSVC 2019 x64 Debug 与 Release 均通过，Release 使用 `/UNDEBUG`、`JAGUARCORE_TEST_RELEASE` 和 `TestReleaseAsserts.h` 保持断言启用。

矩阵覆盖 SPP 初始化指标、改宽、放置、保存、移除与恢复；BPP 的 `Closed` 新开和 `Open` 复用、库存/成本/密度、保存、移除与恢复；MSPP 新增条带、放置、改宽、`FitStrip`、保存、移除与恢复。数值通过 `AlmostEqualRustF32` 比较。该结果关闭 B7 的固定有效流矩阵；欠需求/欠库存、空布局 fit、无效 key、损坏 snapshot 及完整 JSON round-trip 仍在矩阵外（solution IO 由 B8 跟踪），完整 `JaguarCoreTests.exe` 未运行。详见 [B7 问题类型记录](../logic-comparison-jagua/records/50-bpp.md#b7-固定有效流矩阵2026-10-17)、[SPP 记录](../logic-comparison-jagua/records/60-spp.md#b7-固定有效流矩阵2026-10-17) 与 [MSPP 记录](../logic-comparison-jagua/records/70-mspp.md#b7-固定有效流矩阵2026-10-17)。
## 当前可见的 profile 差异与后续项

- 默认输出的 7 行在 Debug/Release 不同：pier_lattice_case 一行，以及 U/L/C 三个形状的 6 组 pier 坐标。现有四个 surrogate_l_c/surrogate_u_shape fixture 的对应数值与 pinned oracle 输出相符，但其注释头仍写 Rust 1.90；目前在 JaguarCore 源码中未找到这些文件的读取者，来源清理与是否保留列入 E7。
- 模型边界探针：Debug 对总需求溢出和超量放置 panic；Release 的 usize 加法回绕，超量放置返回成功并留下剩余量为 usize::MAX 的状态。
- MSPP restore 探针：配置宽度 5、layout 宽度 10 的快照，Debug panic，Release 接受；两者记录的 placed 数均为 1。
- offset 探针：NaN 输入在 Debug panic，在 Release 返回空多边形错误；正负无穷与 collapse 场景在 Release 返回空多边形错误。
- import 探针的 stdout 在两种 profile 相同；stderr 中有被捕获异常的 panic hook 输出。
- collision 和 zero-separation 两个探针的 Debug/Release stdout 完全相同。
- 默认 cold-start 的墙钟截断疑点已由下节的 Rust Debug/Release 重跑和 C++ Debug/Release 全套测试复核；B2/B4a 的 pinned Rust 运行仍只证明各自 oracle 与 fixture，不替代相应跨语言差分结果。

## E5 Release NaN 影响复核（2026-10-19）

使用 `SparrowCore/Tests/RustReference/Cargo.lock` 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb` 与 Rust 1.98.1，在 `RustSppCompressWorkerMove.json`、seed=42、width=4.2 双矩形可行 warm-start、默认 3 workers 上做临时短时诊断；探索预算为 20 秒、压缩预算为 2 秒。Debug 在 3.95 秒于 `worker.rs:122` 重现 `weighted loss should never increase: ∞ > NaN` 并中止。Release 完成 22.02 秒诊断，最终布局可行、width=4.0000105。

Release 聚合点记录的首 12 个含 NaN 的轮次中，三个 worker 的 weighted-loss 总值均为 NaN。Rust `OrderedFloat` 将 NaN 排在有限数之后、多个 NaN 互相相等；该轮三者皆 NaN 时 `min_by_key` 保留索引 0。比如轮次 0 的 unweighted total collision loss 为 `[0.2639946, 0.2639217, 0.26390663]`，轮次 1 为 `[0.16895582, 0.10878802, 0.110008456]`，但两轮均选择索引 0。因此 Release 中断言消失并未消除 NaN；NaN 键会遮蔽 worker 间的加权排序，并在本输入上使较低原始 collision loss 的候选被丢弃。

C++ `Separator.cpp` 的 worker 聚合现通过内部 `WorkerLossSelector` 实现 Rust `OrderedFloat` 的 NaN 排序：有限键小于 NaN，多个 NaN 相等并保留先到者。新增 RustReference 测试 `worker_loss_nan_order_reference.rs` 与 C++ `TestWorkerLossSelectorNaNOrdering`，对 `[NaN,4,2,NaN]`、`[4,NaN]`、`[NaN,NaN]` 都选择索引 `2/0/0`；旧 C++ 普通 `<` 的首个混合用例先失败，修正后通过。Rust 1.98.1 Debug/Release oracle 与 MSVC 2019 C++ Release 定向测试、全量 runner 均通过。此受控序列证明比较规则对齐；Rust Release 短时生产路径的混合键观察见第八轮记录，C++ 第七轮未观察到该类状态。裁决为保留 pinned Rust 行为并把 C++ 选择器同步到其排序语义；不改 Rust oracle，也不对任一侧增加权重饱和，因此 Debug `∞ > NaN` 断言缺陷和 Release 全 NaN 候选选择退化仍按 pinned 现状记录。该短时诊断及合成选择器用例都不替代完整 E5 验收；E5 仍部分，完整确定性输入矩阵及 Rust Debug 自然时限完成证据尚缺。

### E5 第七轮：C++ Release 真实 worker 聚合（2026-10-19）

复用 E4 锁定的 `RustSppCompressWorkerMove.json`、seed=42、width=4.2 双矩形可行 warm-start 和默认 3 workers，在 MSVC 2019 C++ Release 上运行真实顶层 `Optimize → Compress`。使用真实单调时钟，将 Explore/Compression 预算分别缩为 20 秒/2 秒；这是短时聚合观测，不是默认 9 分钟/60 秒自然时限验收。第二次完整采样耗时 22007 ms，Explore/Compression 分别产生 63/5 个 separation result，最终状态 feasible，strip width 为 `4.0002169609069824`。

临时观测点位于生产 `Separator::Run` 的 worker-result 聚合处，逐轮记录三个 worker 的 `TotalWeightedLoss`、`TotalLoss` 与实际选中索引。Explore 共 8099 轮：5901 轮全为有限键、2198 轮 3/3 为 NaN；Compression 共 757 轮：592 轮全为有限键、165 轮 3/3 为 NaN。合计 8856 轮中，混合 NaN/非 NaN 键为 0，包含 Inf 且不含 NaN 的轮次为 0。NaN 示例与 Rust Release 对应值一致：某次 Explore `Separator::Run` 的第 448 轮 raw loss 为 `[0.263994604, 0.263921708, 0.263906628]`，轮次 449 为 `[0.168955818, 0.108788021, 0.110008456]`；两轮 weighted key 均为 `[NaN, NaN, NaN]`，选择器都选 worker 0，尽管 worker 2、worker 1 各自 raw loss 更低。

这证明在该固定 fixture、seed、三 worker 和短时 Release profile 下，C++ 生产聚合确实会出现全 NaN 组并产生同样的候选选择退化；本次没有观察到混合 NaN/有限键，不能外推为所有输入或默认自然预算都不会出现混合。临时环境变量追踪、短预算入口和日志已移除；永久保留的是此前与 pinned Rust 一致的 `WorkerLossSelector` 及其受控键序列回归。E5 仍部分：Rust Debug 默认自然时限会提前断言失败，完整确定性输入矩阵仍未完成。

### E5 第八轮：Rust Release 真实 worker 聚合与 C++ 并列复核（2026-10-19）

使用 Rust 1.98.1、Cargo.lock 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`，以及同一 `RustSppCompressWorkerMove.json`、seed=42、width=4.2 双矩形可行 warm-start 和默认 3 workers。以真实单调时钟执行顶层 `optimize → compress`，Explore/Compression 预算为 20秒/2秒；Rust Release 测试通过，运行 22014 ms，阶段 separation result 为 262/30，最终布局 feasible，width `4.000037`、density `0.6666605`。临时观测点位于生产 `move_items_multi` 的 `OrderedFloat::min_by_key` 选取之后，统计该次运行所有 worker 聚合轮次，并记录 weighted/raw loss 与实际选中索引的代表样本。

| 阶段 | 实现 | 聚合轮数 | 全有限键 | 全 NaN 键 | 混合 NaN/非 NaN | 有 Inf 且无 NaN | Separation result |
|---|---|---:|---:|---:|---:|---:|---:|
| Explore | C++ MSVC 2019 Release | 8099 | 5901 | 2198 | 0 | 0 | 63 |
| Explore | Rust 1.98.1 Release | 128066 | 95272 | 32752 | 34 | 8 | 262 |
| Compression | C++ MSVC 2019 Release | 757 | 592 | 165 | 0 | 0 | 5 |
| Compression | Rust 1.98.1 Release | 17268 | 12520 | 4725 | 15 | 8 | 30 |

Rust 示例与 C++ 第七轮数值相合：Rust 的 NaN 聚合轮次 447（该 `separate` 调用内零基编号）中 weighted keys 为 `[NaN, NaN, NaN]`，raw loss 为 `[0.2639946, 0.2639217, 0.26390663]`，实际选择 worker 0，尽管 worker 2 的 raw loss 更低；下一轮 raw loss `[0.16895582, 0.10878802, 0.110008456]` 也仍选择 worker 0。Rust 另外观测到混合键，例如 `[Inf, NaN, NaN]` 选择索引 0，以及 `[NaN, Inf, NaN]` 选择索引 1，与 `OrderedFloat` 把 NaN 排在 Inf 之后的规则相符。此处混合计数包含 NaN 与任一非 NaN 值（包括 Inf）；“有 Inf 且无 NaN”另行统计。

两侧都确认全 NaN 组会令原始 loss 更低的 worker 落选；受控选择器排序保持一致。Rust 两阶段合计 145334 轮：107792 全有限、37477 全 NaN、49 混合 NaN/非 NaN、16 轮含 Inf 且无 NaN。Rust 本次在短预算中观察到 49 轮混合键和 16 轮含 Inf 且无 NaN，C++ 第七轮的 8856 轮样本中两类均为零。相同墙钟预算下，Rust 完成的聚合轮数明显更多；Rust 使用持续的 Rayon 线程池，C++ 每轮通过 `std::async` 派发 worker，但本轮没有隔离性能成因。自然时限计数受运行时吞吐和执行轨迹影响，不能视为固定步数差分。此次计数仅适用于所列 fixture、seed、Release profile 与短预算；Debug 默认自然时限仍会提前触发已记录的断言，E5 的更广确定性输入矩阵仍待验收。短预算入口、环境变量追踪和逐轮日志已从 Rust 源码与测试移除。

### E5 第九轮：混合 NaN/Inf 选择器对拍（2026-10-19）

在 E4 锁定的 Rust 1.98.1 与 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb` 下，扩展 `worker_loss_nan_order_reference.rs` 与 C++ `TestWorkerLossSelectorNonFiniteOrdering` 的共同输入矩阵。Rust Debug/Release 各运行定向测试 1 项，均通过。两侧新增四组含 NaN 与正/负无穷的序列，选择索引依次为 `[NaN,+Inf,NaN] → 1`、`[+Inf,NaN,NaN] → 0`、`[NaN,-Inf,+Inf] → 1`、`[+Inf,+Inf,NaN] → 0`；原有有限值与 NaN 三组仍选 `2/0/0`。结果符合 `-Inf < 有限值 < +Inf < NaN`，相等键保留首个 worker。

C++ 使用 MSVC 2019 Release 在 `SparrowCore/build/e5-round9-selector-release` 构建定向 runner；为保证当前测试代码与库实现一致，本次从当前源码构建了 SparrowCore Release 静态库到 `SparrowCore/build/e5-round9-sparrow-lib-release`。运行 `SparrowCoreTests.exe --worker-loss-selector` 并将匹配的 JaguarCore Release `Bin` 目录加入 `PATH` 后，退出码为 0。此轮仅增加 selector 边界覆盖，没有改变生产算法。受控键序列证明两侧排序语义一致，不证明短时自然运行中会出现相同聚合轨迹；真实聚合的计数仍以第七/八轮记录为准，E5 继续部分完成。

### E5 第十轮：确定性混合 NaN/Inf worker 聚合边界用例（2026-10-19）

新增 Rust `mixed_nonfinite_tracker_aggregates_select_the_expected_worker` 与 C++ `TestMixedNonFiniteWorkerResultCollection`。两边都从双放置物布局创建真实 `CollisionTracker`，再构造四个固定 worker tracker 状态：pair loss 分量分别为 `(+Inf,-Inf)`、`(+Inf,0)`、`(-Inf,0)`、`(2,0)`。调用 tracker 自己的 `get_total_weighted_loss` / `TotalWeightedLoss` 汇总后得到 `[NaN,+Inf,-Inf,2]`；Rust 在与 `move_items_multi` 相同的 `OrderedFloat::min_by_key` 选择表达式中选中索引 2，C++ 则把四个固定 worker 结果送进 `Separator::Run` 实际使用的 `WorkerRoundCollector`，后者调用生产 `WorkerLossSelector` 选中索引 2，并累计全部成功结果的计数。

随后 C++ 测试按生产顺序恢复胜出 `solution` 与 `trackerSnapshot`，确认所选 payload 的 `moveCount=3`，恢复后的主 tracker 总 loss、总加权 loss 及 pair 项均为 `-Inf`。Rust 1.98.1 Debug/Release 定向测试各 `2 passed`；MSVC 2019 Release 重建 SparrowCore 与定向 runner 后，运行 `SparrowCoreTests.exe --worker-loss-selector` 退出码 0。测试将固定 worker 结果送入真实生产收集器，但结果由测试构造；它不执行 `RunWorkerRound`/`move_items_multi` 的搜索或异步调度，也不声称给出端到端 worker 移动轨迹。E5 仍部分，短时自然预算统计也仍只适用于各自记录的运行条件。

### E5 第十一轮：真实双 worker 搜索驱动的确定性端到端对拍（2026-10-19）


新增 Rust `optimize_real_exploration_workers_then_compress_matches_reference` 与 C++ `TestTopLevelWorkerMoveCompressionMatchesRustReference` 用例，使用同一 `RustSppCompressWorkerMove.json`、seed=42、width=4.2 和两个重叠放置物作为 warm start。顶层 `optimize` 实际进入双 worker Explore，配置为 `no_improvement_limit=1`、`strike_limit=1`、样本 `1/0/1`，运行到首个 separation result 后停相；Explore 成功并发生 3 次移动，结果收集后的下一次 progress 观测到选后 tracker loss 为 0。随后进入单 worker FailureBased Compression，以 shrink step 0.03 运行真实搜索；首个结果成功并发生 2 次移动，最终两件需求均已放置、布局可行、width `4.073999882`、density `0.654557347`、collision loss 为 0。

Rust Debug/Release 定向目标均为 `3 passed`，新增用例又在两个 profile 各独立重复 3 次，六次均与完整 27 行 trace 一致。Rust 1.98.1 与 Cargo.lock 锁定的 jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb` 未变。Qt 6.5.3 `msvc2019_64` qmake、MSVC 14.44.35228.0 下 SparrowCore Debug/Release 全量 runner 均退出码 0；Release 全量 runner额外复跑一次也为 0。C++ 比较阶段/报告顺序、worker 评估与移动计数、放置身份、终态和 tracker loss；浮点字段使用现有按字段容差。fixture `OptimizeExplorationWorkerCompressionRustReference.tsv` SHA-256 为 `DECAB4C736AD21D28FAC83B004DBAEB62AC088653B91CAD1B68946D7402B94DF`。

这条真实搜索轨迹在固定几何、seed、worker 数和采样配置下重复稳定；它观察到的是有限 tracker loss，没有在真实搜索中产生第十轮的混合 NaN/Inf worker 结果，因此第十轮仍只是非有限键选择与结果收集边界证据。多 worker 任意调度、其他 worker 数和一般实例矩阵不由此推断。E5 继续部分：Rust Debug 默认自然时限仍未完成 Explore，完整确定性输入矩阵仍待验收。

### E5 第十二轮：固定步数复现真实 worker 的混合 NaN/Inf 聚合（2026-10-19）

新增 Rust `fixed_real_worker_rounds_reproduce_mixed_nonfinite_aggregate` 与 fixture `SparrowCore/Tests/Fixtures/OptimizeMixedNonFiniteFixedWorkerRoundsRustReference.tsv`（SHA-256=`854E7B8493F06774027E39DA7420A666EAE2830538DD9C554DED8A0F91D4022A`）。场景复用 `RustSppCompressWorkerMove.json` 中的 2×1 矩形，设条带宽度 3.0、放置平移 `(1.05,0.55)` 与 `(1.55,0.55)`、3 workers、样本 `1/0/1`、seed=4，执行固定 450 轮。每轮都让三个真实 `SeparatorWorker` 从相同 master solution/tracker 出发执行 `load`/`move_items`，用生产相同的 `OrderedFloat(total_weighted_loss).min_by_key` 顺序选择，再恢复胜者并更新 GLS 权重。为逐轮读取 worker key，测试按顺序驱动彼此独立的 worker RNG；它不调用私有 `move_items_multi` 或 Rayon 调度器，因此结论限于相同 worker 输入和选择规则下的固定轮次轨迹。

Release 测试在同一进程独立运行两条完整 450 轮轨迹并比较 fixture，结果位级一致。第449轮三个键均为 NaN并选 worker 0；第450轮首次出现混合键 `[+Inf, NaN, +Inf]`，实际仍选 worker 0。此轮 worker 1 的 raw loss bits 为 `0x407a558d`，低于胜出的 worker 0 的 `0x4085439a`，但它的加权键是 NaN；轮后 master tracker 的 raw loss 为 `0x4085439a`、加权 loss 为 `+Inf`。Rust 1.98.1 Release 定向测试 `1 passed`，新测试文件的 `rustfmt --check` 通过。

C++ 侧使用相同几何、worker 数和样本预算，由真实生产 `Separator::Run` 执行固定 450 轮。Release 临时聚合点记录在 seed=5 的第448、449轮出现 `[+Inf, NaN, NaN]` 并选 worker 0；第450轮为全 NaN并选 worker 0。该运行完成 450 iterations、1621 moves，终态 raw loss bits=`0x3fad792b`（约 `1.35526025`）、weighted loss 为 NaN。永久 C++ 定向 runner `SparrowCoreTests.exe --e5-fixed-nonfinite-worker-rounds` 两次执行同一 seed，终态和移动数完全一致；临时逐轮打印已撤下，当前 SparrowCore Release 静态库和 runner 均从干净源码重建。C++ seed=4 在相同前450轮内未出现混合键（第445–450轮均为 Inf），所以 Rust/C++ 两条记录是各自找到的确定性复现，不是同 seed 的逐轮差分。证据只覆盖本几何、seed、Release profile 与450轮；默认 Debug 与更广输入矩阵仍待验收，E5 保持部分。

### E5 第十三轮：同seed的Rust/C++固定步数轨迹对拍（2026-10-19）

新增 Rust 用例 `e5_same_seed_worker_trajectory_reference.rs::same_seed_fixed_production_worker_rounds_match_reference` 和 fixture `SparrowCore/Tests/Fixtures/OptimizeSameSeedWorkerTrajectoryE5Reference.tsv`（SHA-256=`8D83829CCC16A46F9F1515485B1CEF17C6EB4D71F814E27B290114D88C2855A5`）。输入沿用前轮的两个重叠2×1矩形、条带宽度3.0和放置 `(1.05,0.55)`、`(1.55,0.55)`；Rust/C++ 均使用根seed=5、3 workers、样本 `1/0/1`、Release、固定450步。Rust harness逐轮调用三个公开 `SeparatorWorker::load/move_items`，并复现生产的 `OrderedFloat(total_weighted_loss).min_by_key`、raw-loss best跟踪、GLS权重更新和终态回滚；这样能记录每个worker的key/raw loss/胜出索引，同时不声称覆盖私有 `move_items_multi` 或 Rayon 调度。

C++ 侧通过真实生产 `Separator::Run`、`maxIterations=450` 执行同一输入；监听器在每轮选择后记录主解、raw/weighted tracker loss和两件物品的变换位模式。与Rust fixture的451条状态行（初态加450轮）逐行、逐位完全一致。终态两侧同为450 iterations、1621 moves、76622 evaluations、raw loss bits=`0x3fad792b`、weighted loss bits=`0xffc00000`（NaN）。Rust记录显示第448、449轮worker key为 `[+Inf,NaN,NaN]`、均选择worker 0；第450轮为全NaN、仍选worker 0。此前C++ seed=5生产聚合点的临时记录在相同轮次观察到相同键类别与选择索引；本轮常驻对拍锁定的是每轮生产主状态和终态，不声称C++ fixture包含逐worker key。

验证：Rust 1.98.1 Release定向测试 `1 passed`，新增Rust文件 `rustfmt --check` 通过；MSVC 14.44 Release测试runner重建成功，`--e5-same-seed-worker-trajectory` 定向对拍及SparrowCore完整Release runner均退出码0。该结论限于此几何、seed、worker/sample配置和Release；Rust逐worker harness是顺序调度，Debug与更广输入/调度矩阵仍未验收。E5继续部分，完整 `optimize → compress` 端到端矩阵尚未闭合。

### E5 第十四轮：seed=4固定450步跨语言对拍复核（2026-10-19）

沿用第十三轮输入、3 workers、样本 `1/0/1`、Release和450步，仅将根seed改为4。新增 Rust 用例 `e5_same_seed_worker_trajectory_reference.rs::seed4_fixed_production_worker_rounds_match_reference` 及 fixture `SparrowCore/Tests/Fixtures/OptimizeSameSeedWorkerTrajectorySeed4E5Reference.tsv`（SHA-256=`D1F395D2E136AF22797F19FB0790C5AAD8C1B8CC88292EFB8CB1713B25628619`）。fixture逐轮记录三worker的加权key/raw loss/胜出索引、master状态、变换位模式和终态计数。

与seed=5不同，seed=4没有通过全程对拍。C++生产 `Separator::Run` 的初态至state 448（完成前448轮）与Rust逐位一致；首次差异是第449轮后的state 449。Rust第449轮worker key为 `[NaN,NaN,NaN]`，选择worker 0，raw loss从 `0x407f985c` 降为 `0x4075dc7a`，加权loss为NaN。C++同位置仍是前一轮状态，raw loss为 `0x407f985c`、加权loss为 `+Inf`；state 450仍未移动。C++终态为450 iterations、1637 moves、76562 evaluations、raw loss `0x3fad763f`、weighted loss NaN；Rust fixture的聚合计数为1649 moves、77040 evaluations，raw best也为 `0x3fad763f`。C++完整状态轨迹连续两次采集的SHA-256均为`FE09BB81DE07939C4B84AE645CBDFB905A67CDEEEB06A94B624AA5AA83A0D5E7`，确认该分歧在此环境下稳定复现。

C++ seed=4回归入口现在验证state 0–448与Rust相同，并锁定state 449、450及终态为上述已观察结果；这是一条差异保持检查，不代表两侧通过了seed=4对拍。Rust seed=4/5两个Release fixture测试、C++ seed=4差异检查、seed=5逐轮对拍和SparrowCore完整Release runner均通过，Rust文件 `rustfmt --check` 通过。当前证据只证明seed=5在所测设置下逐轮一致；seed=4于第449轮分歧，差异根因尚未完全裁定，E5及更广seed/调度矩阵仍未收口。

## Sparrow 默认 cold-start fixture（2026-10-17）

`optimizer_cold_start_default_reference.rs` 使用自定义 `PhaseTerminator`：`timeout_at()` 恒为 `None`，`kill()` 只读取 listener 在每阶段首个 separation result 后设置的 stop flag。因此 Rust 参考运行保留默认配置中的 60 秒 compression time limit 作为 shrink-decay horizon，但不会因机器墙钟截断阶段。现有 fixture 已包含该契约说明；本轮重跑输出与 fixture 完全相同，没有改写 fixture 数据。

从仓库根目录分别执行：

```powershell
$env:SPARROW_PRINT_COLD_START_DEFAULT_REFERENCE = '1'
rustup run 1.98.1 cargo test --locked --manifest-path SparrowCore/Tests/RustReference/Cargo.toml --test optimizer_cold_start_default_reference -- --exact optimize_cold_start_with_default_stage_configs_matches_reference --nocapture
rustup run 1.98.1 cargo test --release --locked --manifest-path SparrowCore/Tests/RustReference/Cargo.toml --test optimizer_cold_start_default_reference -- --exact optimize_cold_start_with_default_stage_configs_matches_reference --nocapture
```

Debug 与 Release 均为 `1 passed, 0 failed`。测试将运行时 trace 与 `OptimizeColdStartDefaultRustReference.tsv` 的非注释行逐行比较；两种 profile 都匹配全部 33 行。事件行按 UTF-8、LF 和末尾 LF 规范化后的 SHA-256 为 `86A0AE51F180992DDD93C317506BE61069250E88BAC98FCBAB00EAFA6DB279CE`；fixture 文件 SHA-256 为 `ADCCFE002BAD7FEC206A6057BD400BAF68079361C60CEE1E5A1F803576402B73`。compression 汇总为 647 次 progress callback，首末 iteration 为 0/646，分离结果为 403489 eval、2343 moves、646 iterations。

随后用 Qt 6.5.3 `msvc2019_64` qmake Makefile、Visual Studio 2022 MSVC x64 toolset 14.44.35207 重建并运行 `SparrowCoreTests.exe` Debug 与 Release 全套 runner，两者均退出码 0。该 runner 包含 `TestDefaultColdStartOptimizeMatchesRustReference`。Release runner 输出的四行 evaluator bit 诊断属于其他测试的正常输出；构建有既存的 C4189 未引用局部变量警告，不影响成功状态。
