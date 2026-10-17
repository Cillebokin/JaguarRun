# E4 oracle 与 fixture 基线执行记录（2026-10-16）

## 结论与边界

两处 Rust oracle 已按锁定工具链与 jagua-rs revision 运行。Sparrow RustReference 的 Debug、Release 测试均通过；fixture 生成器连续运行两次得到相同结果，13 个生成文件在逐一核对后全部与工作区 fixture 一致。为匹配生成器输出，已更新 SeparatorRoundRustTrace.tsv 的注释头；C++ 读取器会跳过以 # 开头的注释行。

Jaguar RustOracle 的默认入口与专用入口在 Debug、Release 下均退出码 0。输出揭示了若干既有的 profile 差异，记录在下文。2026-10-17 按锁定配置重算 B2，并新增 B4a filtered-`Entire` 可达性探针复核 Debug/Release 输出。Rust oracle 通过只证明该参考程序成功运行，不代表 JaguarCore/SparrowCore 与 Rust 等价；E4 仍标为“部分”，墙钟驱动的 cold-start fixture 处理待完成。

2026-10-16 的 E4 baseline 运行未执行 C++ 构建或原生测试；2026-10-17 的 B2/B4a 复核各自记录了对应的 C++ 定向 runner。工作区中的构建产物未清理。

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
| JaguarCore/tools/RustOracle/src | 1 | CAB095E7E7EC40EB10654FBCB75B8D9C76C821AE6A14580EC586B0934C605ECD |

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

## 当前可见的 profile 差异与后续项

- 默认输出的 7 行在 Debug/Release 不同：pier_lattice_case 一行，以及 U/L/C 三个形状的 6 组 pier 坐标。现有四个 surrogate_l_c/surrogate_u_shape fixture 的对应数值与 pinned oracle 输出相符，但其注释头仍写 Rust 1.90；目前在 JaguarCore 源码中未找到这些文件的读取者，来源清理与是否保留列入 E7。
- 模型边界探针：Debug 对总需求溢出和超量放置 panic；Release 的 usize 加法回绕，超量放置返回成功并留下剩余量为 usize::MAX 的状态。
- MSPP restore 探针：配置宽度 5、layout 宽度 10 的快照，Debug panic，Release 接受；两者记录的 placed 数均为 1。
- offset 探针：NaN 输入在 Debug panic，在 Release 返回空多边形错误；正负无穷与 collapse 场景在 Release 返回空多边形错误。
- import 探针的 stdout 在两种 profile 相同；stderr 中有被捕获异常的 panic hook 输出。
- collision 和 zero-separation 两个探针的 Debug/Release stdout 完全相同。
- E4 之后仍需处理墙钟截断的 cold-start Rust fixture；B2 的 pinned Debug/Release 复核已于 2026-10-17 记录在下节，oracle 通过不替代跨语言差分结果。
