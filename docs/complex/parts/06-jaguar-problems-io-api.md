# JaguarCore 问题模型、IO 与公开 API 代码逻辑说明

## 一、范围与阅读方法

本文覆盖 `JaguarCore/Source/Problems`（SPP、BPP、MSPP）、`Source/IO` 与 `include/JaguarCore/io`，以及 `Source/Support/Version.cpp`、`include/JaguarCore/export.h`、`include/JaguarCore/result.h` 与 `Source/Support/Result.cpp`。Rust 对照实现位于 `CodeResource/jagua-rs/jagua-rs/src/probs/` 与 `src/io/`。阅读时以实现体为准，头文件中的签名只作为入口索引。行号均指向当前工作区文件，如有改动需重新核对。

整体分层可以概括为三层。最底层是 `Entities` 中的 `Layout`、`Item`、`Container` 等几何实体，负责碰撞引擎（CDE）与变换。中间层是三类 Problem，它们持有 Instance 并维护"剩余需求、剩余库存、已放置布局"等状态。最外层是 `Importer`（外部表示到内部 Instance 与 Solution 的转换）、`JsonCodec`（JSON 文本与 `ExternalRepr` 之间的编解码）与 `SvgExport`（渲染）。

## 二、三类 Problem 的状态机

SPP 的状态由 `SPProblem`（`include/JaguarCore/problems/spp.h`，156 行）持有。构造函数（`Source/Problems/SPP/Problem.cpp` 第 9–17 行）从 Instance 初始化 `RemainingDemand`。`PlaceItem`（第 30–53 行）在 NDEBUG 未定义时才检查剩余需求是否为正（第 38–44 行），随后在第 50 行递减。`RemoveItem`（第 55–71 行）先移除布局、再检查实例索引，若索引非法则已经移除的布局不会回滚，这是一个非原子操作。`ChangeStripWidth`（第 73–90 行）复制 Strip 后调用 `SwapContainer`。`FitStrip`（第 92–111 行）在第 104 行使用 1.00001 的乘数加偏移来收紧宽度。`Save`（第 113–116 行）输出 `SPSolution`。`Restore`（第 118–158 行）优先尝试恢复布局，否则按 `SameBase` 重建，然后恢复 Strip 并重新计算需求；第 146 行在调试模式下抛出 `std::out_of_range`，第 149 行在调试模式下抛出 `std::underflow_error`。`RemainingDemand` 对非法索引返回 0（第 160–163 行）。

Rust 的 `SPProblem::place_item` 没有需求检查，C++ 仅在调试模式检查，因此发布版本中需求可能下溢，这一点与 Rust 在 release 下按补码回绕的行为接近，但 C++ 无法得到与 Rust 一致的错误报告。Rust 的 `fit_strip` 使用 `unwrap` 取最大值，C++ 的对应实现更宽松。Strip 的 `SetWidth` 与 `Create` 见 `Source/Problems/SPP/Strip.cpp` 第 181–214 行，`fixedHeight` 与宽度只检查 `!(x > 0.0)`，因此无穷大宽度会通过检查，这是一个需要注意的边界。

BPP 的状态由 `BPProblem`（`include/JaguarCore/problems/bpp.h`，201 行）与 `Source/Problems/BPP/BppProblem.cpp`（516 行）实现。布局存放在 `LayoutSlot` 数组中，每个槽位包含 `optional<Layout>`、`binIndex` 与 `generation`。槽位 0 是哑元，`generation` 初始为 0。`AllocateLayoutSlot`（第 114–130 行）复用空闲槽并把 generation 设为 `generation | 1`，使其为奇数表示占用；`ReleaseLayoutSlot`（第 132–144 行）则把 generation 置为偶数。这种设计模拟 Rust 的 `SlotMap`，其对外的布局键由 `kLayoutKeyPartBits = 32`（第 14–38 行，64 位平台）编码槽位与代数。`OpenLayout`（第 146–164 行）先检查库存再递减库存。`PlaceItem`（第 166–221 行）在目标为 `Closed` 时先打开布局，并在第 196–200 行与第 209–213 行提供回滚；需求检查只在调试模式下（第 174–180 行）；需求递减位于第 217 行。`RemoveItem`（第 223–258 行）先删除布局，再检查实例索引（第 238–242 行），布局为空时自动关闭（第 248–255 行），并返回 `Closed` 的 `layoutIndex = 0`。`RemoveLayout`（第 260–291 行）先校验所有物品，再恢复需求与库存，最后释放槽位，这是 BPP 中唯一做到先验证再修改的路径。

`Restore`（第 307–454 行）的顺序是：第 329–368 行做快照校验（包括 `SameBase` 与 `sameItemDefinition`），第 367 行递减库存，第 372–376 行按槽位排序准备数据，第 378–390 行尝试保留原键，第 392–446 行为无法保留的布局分配新槽位，第 448–453 行提交，并返回 `layoutKeysChanged` 标志。Rust 中 BPP 的 restore 通过 `insert` 生成新键，因此键会变化，C++ 尽量保留键，这是一个行为差异，调用方不应依赖键在 restore 后不变，也不应依赖其变化。`RemainingBinStock`（第 461–465 行）与 `BinCost`（第 501–510 行）分别返回库存与成本。`Density`（第 486–499 行）在没有任何布局时执行 0/0，结果为 NaN，这一点应当在调用方处理。`BppSolution.cpp` 的 `Density`（第 37–49 行）按槽位顺序累加，`Cost`（第 51–60 行）忽略越界的 `binIndex`，静默跳过错误数据。

MSPP 的状态由 `MSPProblem`（`include/JaguarCore/problems/mspp.h`，211 行）与 `Source/Problems/MSPP/MspProblem.cpp`（512 行）实现。每条 `MultiStrip` 包含 `maxWidth`、`fixedHeight` 与当前 `width`。`AddLayout`（第 142–157 行）对布局做规范化，但不检查它是否与实例的基础定义相符。`RemoveLayout`（第 159–181 行）存在非原子问题：第 176 行在循环中已经递增需求，而第 173 行的后续校验可能提前返回，导致部分修改已经生效。`PlaceItem`（第 183–212 行）在第 191–197 行做调试模式的需求检查。`RemoveItem`（第 214–236 行）同样先删布局后检查索引。`ChangeStripWidth`（第 238–260 行）原地调用 `SwapContainer`。`FitStrip`（第 262–286 行）在第 279 行使用与 SPP 相同的 1.00001 乘数。`Restore`（第 302–437 行）没有对快照中的 Strip 做校验，键保留逻辑与 BPP 类似，提交位于第 432–436 行。`ExtendableStrips`（第 470–480 行）、`TotalStripWidth`（第 482–491 行）与 `Density`（第 493–506 行）均使用 float 运算，`Density` 在无布局时同样产生 NaN。MSPP 的 `MspStrip::Create`（`Source/Problems/MSPP/MspStrip.cpp` 第 5–32 行）没有检查宽度不超过 `maxWidth`，而 `SetWidth`（第 34–50 行）在第 38 行检查该条件，但错误信息写的是"exceeds f32 range"，与实际含义不符。Rust 中 `Strip::set_width` 对此使用断言。`MspSolution.cpp` 的 `Density`（第 101–113 行）使用的是 Strip 高度乘宽度，而不是容器面积，与 SPP 的定义不同。

三类 Problem 的 `Create` 都有 `Source/Problems/*/...Instance.cpp` 中的前置校验，但 MSPP 实例创建中没有任何正需求检查。调试期的 `DebugAssert*Solution`（位于 `Source/Support/Assertions.cpp` 约第 387 行及其 BP、MSP 对应函数）包裹在 `#ifndef NDEBUG` 中，每次修改后都会调用 `Save()` 并校验，因此调试模式下每次变更的代价为 O(n)，性能分析时应当使用发布构建。

## 三、Importer 的映射、校验与错误

`Importer`（`Source/IO/Importer.cpp`，714 行；接口在 `include/JaguarCore/io/importer.h`，102 行）的主要流程是：先把外部形状转为内部多边形，再施加中心化预变换与形状膨胀，最后交给 Instance 工厂。`withSeparation`（第 114–122 行）把最小物品间距转换为偏移量 `separation / 2`，这是每侧各让出一半间距的做法。`externalToInternal`（第 131–151 行）先应用预变换的逆变换，再应用外部变换，全部以 f32 计算以与 Rust 对齐。

`rustFpaEqual`（第 21–53 行）按 ULP 比较浮点数，用于判断相邻重复顶点。`normalizedRectangleBounds`（第 61–82 行）处理矩形边界。`normalizeDegreesRustF32`（第 90–96 行）与 `radiansRustF32`（第 98–102 行）分别规范化角度与转换弧度，均采用 f32 语义。

`importPositiveItems`（第 153–189 行）是物品导入的入口校验：第 158–168 行检查所有物品（包括需求为零的物品）的 ID 唯一性，第 170–182 行跳过需求为零的物品，第 183–187 行在最终为空时报错。也就是说，ID 唯一性检查先于零需求过滤，这意味着一个零需求物品与另一个同 ID 物品仍会产生冲突错误。

形状导入分为几个函数。`ImportSimplePolygon`（第 238–266 行）做 f32 舍入，闭合 Strip 的首尾顶点，并通过 `rustFpaEqual` 去掉连续重复顶点。这一步与 Rust 的 `eliminate_degenerate_vertices` 对应，但 C++ 中对退化情况的处理是否完全一致，需要对照测试样例确认。`ImportItemShape`（第 268–293 行）与 `ImportContainerOuter`（第 295–316 行）分别处理物品与容器外轮廓，`ImportZoneShape`（第 318–325 行）处理质量区。

旋转导入（`ImportRotation`，第 327–397 行）允许的最大角度数为 65536。离散模式（Stepped）的步长必须能整除 360。角度列表会排序并去重。若只有单个角度 0，会被折叠为 `None`，即不允许旋转。这一折叠是隐式行为，调用方若期望保留显式的 `[0.0]` 列表，会得到不同的内部表示。

`ImportItem`（第 399–426 行）执行中心化（第 417–420 行）、按 `ShapeModifyConfig` 做 Inflate，然后调用 `Item::Create`。`ImportContainer`（第 428–497 行）中，`MAX_QUALITIES` 为 10（第 442、457 行），与 Rust 的 `N_QUALITIES` 一致（Rust 位于 `entities/container.rs:81`）。孔洞会合并到 `grouped[0]`（第 468–469 行），这意味着多个外轮廓或多个孔洞的归属规则是隐式的，依赖输入的排列顺序。质量区通过 `ConvertToInternal`（第 480–486 行）转换，最后由 `Container::Create` 构造（第 495 行）。

Instance 层的导入：`ImportSPInstance`（第 517–535 行）调用 `SPInstance::Create` 并传入 `stripHeight`。`ImportSPSolution`（第 537–570 行）依次执行 `ChangeStripWidth`、通过 `findItemByExternalId`（线性扫描）查找物品、`externalToInternal`、`PlaceItem` 与 `Save`。线性查找在大规模实例上的代价为 O(n²)。`ImportBPInstance`（第 590–642 行）检查 ID 唯一性（第 606–614 行），按 ID 排序（第 617–619 行），跳过库存为零的 bin（第 625–626 行），并把 bin 重新编号（第 633–634 行），这一重编号意味着外部 bin 索引与内部索引可能不一致。若结果为空则报错（第 636–640 行）。`ImportMSPInstance`（第 669–688 行）以 `maxWidth` 与高度调用 `MSPInstance::Create`。`ImportBPSolution`（第 644–650 行）与 `ImportMSPSolution`（第 690–696 行）直接返回 `UnsupportedOperation`，Rust 中对应的 `import_solution` 也是 `unimplemented!()`，因此两侧都没有 BP 与 MSP 解决方案导入能力。

## 四、JsonCodec 协议与版本策略

`JsonCodec`（`Source/IO/JsonCodec.cpp`，1977 行；接口在 `include/JaguarCore/io/JsonCodec.h`，120 行）包含一个手写的递归下降解析器（第 79–475 行）。解析器没有深度限制（第 142–182 行的递归处理），因此极深嵌套的输入可能导致栈溢出。数字通过 `strtod`（第 453、463 行）转换，并在 `ExternalRepr` 的 float 字段上做截断（第 728、738 行），这意味着 double 精度会在导入时丢失。非有限数被拒绝（第 461–466 行）。末尾多余数据被拒绝（第 92–94 行）。

解析错误统一包装为 `SerializationFailure`（第 627–631 行与第 646–651 行），这是唯一的错误码，调用方无法区分语法错误与语义错误。`JsonWriter`（第 477–625 行）输出的键按字典序排列，使用 `setprecision(17)`（第 563 行）保证浮点往返，并在第 561–562 行拒绝非有限数。

字段名与枚举约定集中在几个区段：旋转模式在第 1007–1124 行，形状类型在第 873–1005 行，物品字段在第 1178–1187 行，实例与解决方案在第 1453–1735 行附近。`None` 旋转被写为 `{"mode":"discrete","angles":[0.0]}`（第 1095–1101 行），因此写出后再读入会得到单角度离散模式，而不是原始的 `None`，这与 Importer 中的折叠规则形成对称但不完全互逆的关系。

版本策略方面，JSON 中没有 schema version 字段，编解码不检查版本号。版本信息只能通过 `Version.cpp` 的 `JaguarCoreApiVersion()` 获取，其值硬编码为 `2U`。因此协议演进时没有兼容性闸门，升级前后必须依靠外部约定。

## 五、SvgExport 渲染流水线

`ExportLayoutSvg`（`Source/IO/SvgExport.cpp`，410 行；接口在 `include/JaguarCore/io/SvgExport.h` 第 67–69 行，返回 `Result<std::string>`）的流水线大致为：计算视口（第 114–145 行），构造 flip 平移（第 146 行），生成场景组（第 156–157 行），绘制容器、布局与碰撞高亮，最后输出坐标轴（第 396–397 行）。

第 146 行的 flipTranslation 计算为 viewY + viewHeight，而第 156–157 行的变换写为 translate(0 F) scale(1 -1)。按公式推导，当 viewY 不为 0 时可能产生视口垂直偏移；这是静态推导的疑点，尚未通过渲染结果确认。应以 viewY 不为 0 的输入运行 SVG 渲染回归后再判定。每个布局的 id 在第 184 行生成。碰撞高亮（第 269–384 行）通过重建一个 CollisionEngine 来判定冲突，因此导出一次带高亮的 SVG 代价较大。

错误路径方面，`compatibility.md` 第 120 行声称 SVG 导出只返回 `SerializationFailure`，但代码还会返回 `CollisionEngineFailure` 与 `SnapshotMismatch`，文档与实现不一致。

## 六、公开 API 列表与典型调用顺序

对外入口主要包括：Importer 的 `ImportItem`、`ImportContainer`、`ImportItemJson`、`ImportContainerJson`、`ImportSPInstance`、`ImportSPSolution` 及其 JSON 版本，`ImportBPInstance`、`ImportBPSolution` 及其 JSON 版本，`ImportMSPInstance`、`ImportMSPSolution` 及其 JSON 版本。JsonCodec 提供 `Parse*Json` 与 `Serialize*Json` 成对接口，以及从内部对象生成 `External*` 的 `External*FromSolution` 与 `ExternalLayoutFromLayout`。渲染入口为 `ExportLayoutSvg`。版本接口为 `Version()`、`JaguarCoreVersion()` 与 `JaguarCoreApiVersion()`，版本号分别硬编码为 `0.1.0-dev` 与 `2U`，Windows 的 PE 版本号 `0.1.0.0` 写在 `JaguarCore.pro:18` 与 `Package/JaguarCorePackage.pri:16` 中。

典型调用顺序为：先用 `ImporterConfig`（包含 `cdeConfig`、`shapeModifyConfig` 与 `minItemSeparation`）调用 `ImportItem` 与 `ImportContainer` 生成内部对象，或直接调用 `ImportSPInstanceJson` 等一步到位的接口；然后用 Instance 创建 `SPProblem`、`BPProblem` 或 `MSPProblem`；接着用 `PlaceItem` 放置物品，必要时调用 `RemoveItem`、`ChangeStripWidth`、`FitStrip`；通过 `Save` 得到解决方案快照；最后用 `ExportLayoutSvg` 渲染，或用 `Serialize*Json` 输出。若需要从外部解决方案恢复，应调用 `ImportSPSolution`，它内部会完成 ChangeStripWidth 与 PlaceItem 的回放。注意 BPP 与 MSPP 的解决方案导入目前不可用。

`Result<T>` 定义于 `include/JaguarCore/result.h`（约 90 行）。`ErrorCode` 共 14 个取值（第 10–26 行），`Error` 结构（第 29–33 行），`Result<T>` 基于 `std::variant` 实现（第 41–57 行），在成功状态下调用 `GetError` 会抛出 `bad_variant_access`。`Result<void>` 见第 87–88 行。`Source/Support/Result.cpp` 只有一行 include。与 Rust 的 panic 与 anyhow 不同，C++ 的失败路径以值返回为主，但部分内部断言仍然依赖异常。

export.h 在 Windows 下根据 JAGUARCORE_BUILD 选择 dllexport 或 dllimport，在 Linux 下展开为空。头文件引用使用大写目录名，而仓库中 Include/ 与 include/ 两套目录都存在，且对应头文件在两套路径下均可找到。当前不能据此断言 Linux 构建失败；应比较两套副本，确认权威目录以及 qmake/CMake 的 include root 是否一致。大小写副本漂移仍是维护风险。

## 七、差分测试与 Rust Oracle 机制

差分测试位于 JaguarCore/Tests/Differential 与 JaguarCore/Tests/differential 两个大小写不同的目录中；JaguarCoreTests.pro 当前引用大写 Differential/ 路径，且该目录存在。目录大小写本身不足以证明 Linux 构建失败，需核对两份副本内容并指定权威目录。测试比较 C++ 结果与字面量及 tests/fixtures/RustReference 下的快照，不会在每次运行时调用 Rust oracle。CompareGeometry 面积使用 1e-3 与 1e-9 两档，CompareOptimizer 密度容差为 5e-3。CompareProblem.cpp 第 17–31 行的 Windows 反斜杠路径仍可能阻碍 Linux 读取 fixtures；tests/differential/README.md 的运行说明需与工程当前状态复核。

`JaguarCore/Tools/RustOracle/src/main.rs` 是Rust差分oracle；其 `Cargo.toml` 通过Git revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb` 直接依赖 jagua-rs，并启用 spp、bpp、mspp 特性。Manifest还单独声明 `geo-buffer = "0.2"`，用于原始buffer组件诊断；这不等同于锁定jagua-rs revision的 `offset_shape` 实现。`main()` 根据命令行标志分派：`--restore-boundary-parity-only`、`--model-boundary-parity-only`、`--offset-boundary-parity-only`、`--import-boundary-parity-only`、`--zero-separation-parity-only`、`--collision-parity-only`，若均不匹配则输出固定场景的 key=value 文本。`tools/RustOracle/last_oracle_output.txt` 是陈旧且不完整的输出，不能作为当前基准。因此，当前差分机制是"人工生成的快照 + 字面量"，并不是在线对比，Rust 与 C++ 行为一旦分叉，测试不会自动发现，除非有人重新运行oracle并更新fixtures。

## 八、Rust 差异、可疑点与陷阱汇总

以下是当前源码审阅中记录的主要风险，除已有运行证据者外仍应作为待复现项：发布版需求下溢；SPP/MSPP 的非原子移除；Restore 对 Strip 校验不足；BP/MSP 解决方案导入缺失；无布局 Density 的 NaN；JSON float 截断和版本字段缺失；SVG viewY 非零时可能出现的垂直偏移（公式推导，待渲染回归）；导入器隐含约束；MSPP Create/SetWidth 边界不一致；错误码契约。质量区语义、错误码和 SVG 行为需结合产品契约及最小回归用例确认；大小写问题按重复目录副本漂移追踪。

以上条目来自源码检查，需依证据等级和复现情况安排优先级。公式推导或实现差异应先补最小输入与回归测试，再定性为运行缺陷。跨平台方面，Include/include 与 Tests/Differential/differential 两套目录同时存在；先核对内容和工程引用，避免把目录大小写差异直接写成确定的构建失败。文中引用的兼容性文档、测试目录和源码路径也应按当前仓库布局逐条复核。
