# entities：Item、Container、PlacedItem 与 Layout

**Rust 基线**：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`  
**C++ 对照**：`JaguarCore/Include/JaguarCore/Entities/*`、`JaguarCore/Source/Entities/*`  
**关联工作项**：B6、B13  
**证据类型**：pinned Rust 与当前 C++ 源码静态核对；本记录不构成运行对拍结论

## `jagua-rs/src/entities/container.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `Container` (15–24) | 保存外轮廓的原始形状与碰撞形状、按质量等级索引的固定长度 `[Option<InferiorQualityZone>; 10]`，以及空布局碰撞引擎基态 `base_cde`；几何与引擎通过 `Arc` 共享 | `Entities/Container.h::Container`；C++ 私有 `Data` 同时保存 id、原/碰撞外轮廓、质量区、CDE 配置、方形 bounds 与静态 hazards（`Source/Entities/Container.cpp:34–47`）。Rust 不保存外部容器 id，质量区容器布局也不同 |
| `Container::new` (27–71) | 先将原始外轮廓转内部多边形（32，转换失败以 `anyhow::Result` 返回）；再拒绝重复 quality（34–37）及 `quality >= 10`（38–44）；校验通过后按 quality 写入定长数组（45–52）；初始 CDE 加入 `Exterior` 和所有质量区 hazards（54–63）。外层转换之后，数组索引由前置范围检查保护 | `Container::Create`（`Source/Entities/Container.cpp:50–104`）检查唯一等级和 `<10`，但通过 `Result<Container>` 返回 `InvalidInput`；接收已转换成 `SimplePolygon` 的 `QualityZone`。C++ 创建方形 bounds 后注册静态 hazards；`CreateCollisionEngine` 在 hazard 注册失败时返回空指针（137–146），Rust `new` 直接保存已建的 `Arc<CDEngine>`。Rust 区域 shape 同时保存 `shapes_orig` 与 `shapes_cd`，C++ `QualityZone` 只持有碰撞多边形 |
| `Container::area` (73–77) | 返回原始外轮廓面积，不扣孔洞或低质量区 | `Container::Area`（107–110）同为原外形毛面积；返回类型 Rust `f32`、C++ `double` |
| `N_QUALITIES` (80–81) | 质量等级槽位数固定为 10 | C++ `Container.cpp` 匿名命名空间中的 `MAX_QUALITIES = 10`（11），数值一致、符号非公开 |
| `InferiorQualityZone` (85–92) | 保存等级、原始形状 `shapes_orig` 与碰撞形状 `shapes_cd`；`quality == 0` 的语义由注释规定为 hole | C++ `Container.h::QualityZone` 只包含 quality 与 `vector<SimplePolygon>`；质量 0 转为 `HazardKind::Hole` 的映射在 `Container.cpp:85–97` |
| `InferiorQualityZone::new` (95–112) | `quality >= 10` 触发 `assert!` 并 panic（96–99），而形状转换错误通过 `Result` 返回（100–110）；转换前后形状按输入顺序成对保存 | C++ `Container::Create` 把越界等级作为普通 `InvalidInput` 返回；其 API 接收已经表示为内部多边形的 zone，没有同形的“保留原始区轮廓并转换”构造器 |
| `to_hazards` (114–126) | 按 `shapes_cd` 顺序生成 hazard；每区的 `idx` 是该区内枚举下标；等级 0 生成 `Hole`，其余生成带 quality 的 `InferiorQualityZone`；均为静态 hazard（`false`） | 对应 `Container::Create` 的静态 hazard 构造（`Container.cpp:85–99`）。Rust iterator 惰性生成，C++ 在容器数据构造阶段立即填充 `staticHazards` |
| `InferiorQualityZone::area` (128–131) | 对原始区形状面积求和 | C++ `QualityZone` 没有面积方法；若调用侧需要同一结果，需显式遍历 polygon，尚无同名公开接口 |

## `jagua-rs/src/entities/item.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `Item` (12–27) | 公共字段包括实例内 `idx`、外部 id、原始/碰撞形状、旋转范围、可选最低质量与 surrogate 配置；原始和碰撞形状以 `Arc` 共享 | `Entities/Item.h::Item` 提供同类状态及 `Index/ExternalId/GetOriginalShape/CollisionShape/GetSurrogateConfig/MinQuality/AllowedRotation` 查询；C++ 使用值成员 |
| `Item::new` (30–53) | 接收调用者提供的索引、id、形状、旋转、min quality 和 surrogate 配置；先保存原形状，再 `convert_to_internal`，继而 `generate_surrogate`；两个几何步骤的错误均向上传播，成功后保存内外形状及其他属性。此构造器本身不检查 `min_quality` 与容器等级的一致性，也不校验离散角数或旋转范围 | `Item::Create`（`Source/Entities/Item.cpp:41–70`）新增旋转可表示性校验（None/Continuous 要求 radians 为空；Discrete 不校验角数，9–21），失败返回 `InvalidInput`；将离散角舍入到 f32（54–55）后做与 Rust 对应的形状转换和 surrogate 生成。C++ 同样未在实体构造处校验 `minQuality` |
| `Item::area` (55–58) | 返回原始形状面积 | `Item::Area`（`Item.cpp:107–110`）同为原始形状面积；类型为 `f32` 对 `double` |

## `jagua-rs/src/entities/placed_item.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `PItemKey` (11–14) | `slotmap::new_key_type!` 生成 Layout 内唯一 key；key 随 slotmap 槽位代数管理 | C++ `PlacedItemHandle` 是 `HazardHandle` 别名；Layout 自己维护槽位与 generation（`Entities/Layout.h:12,70–80`），不是 Rust `SlotMap` 类型导出 |
| `PlacedItem` (18–25) | 持有 `Arc<Item>`、原样保存的 `DTransformation` 和变换后 `Arc<SPolygon>` | `Entities/PlacedItem.h::PlacedItem` 保存 `shared_ptr<const Item>`、`Transform2d` 和碰撞形状；类字段为私有并经 getter 暴露 |
| `PlacedItem::new` (29–39) | 对 `d_transf` 调 `compose()` 得到可应用变换，再对 item 的碰撞形状执行 `transform_clone`；新对象 `Arc::clone(item)`，不复制 Item 数据。函数签名无 `Result`，这里无显式失败分支 | `PlacedItem::Create`（`Source/Entities/PlacedItem.cpp:16–31`）先把角度与平移分量舍入到 f32，再调用 `TransformedRustF32`；几何错误作为 `Result` 返回。成功时 `make_shared<const Item>(item)` 复制 Item，与 Rust 仅增加 Arc 引用不同；保存的变换为舍入后的 C++ 变换 |

## `jagua-rs/src/entities/layout.rs`

| Rust 声明/行 | 逻辑、分支与失败出口 | C++ 对应与静态差异 |
|---|---|---|
| `Layout::new` (27–34) | 克隆 container 的 `base_cde` 作为可变布局引擎，创建带 key 的空 `SlotMap` | `Layout::Layout`（`Source/Entities/Layout.cpp:55–61`）调用 `Container::CreateCollisionEngine` 并创建哨兵槽位；Rust 对共享基态做 clone，C++ 新建引擎并注册静态 hazards |
| `from_snapshot` (37–43) | 先用 snapshot container 建 Layout，再调用 `restore`；restore 失败会由 `expect` panic，预期为快照共享同一 static base | C++ 无同形静态工厂；调用方显式创建 `Layout` 后调 `Restore`，错误通过 `Result<void>` 返回 |
| `swap_container` (46–55) | 保存 CDE 快照，把当前 container 和引擎换成新 container 的 base clone，再逐个重新注册旧快照的动态 hazards；函数不返回错误，也没有显式回滚分支 | `Layout::SwapContainer`（`Layout.cpp:160–201`）先构建新 CDE，逐个校验旧动态 hazard 对应有效放置句柄并注册；失败返回错误且在提交新容器前保留旧状态。C++ 还将新 CDE hazard handles 回填 slot。Rust 无返回错误合同，C++ 是更显式的失败传播/事务提交结构 |
| `save` (59–65) | clone container、placed slotmap 和 CDE snapshot | `Layout::Save`（`Layout.cpp:243–260`）记录 placement、CDE snapshot、槽代数与 free-slot 顺序；C++ 快照显式保存额外句柄分配元数据 |
| `restore` (74–89) | 首先用 `Arc::ptr_eq` 比较双方 `base_cde`；不一致立即返回 `ContainerMismatch`，文档承诺此时 Layout 不变。匹配后依序替换 container、slotmap、恢复 CDE，再跑两条 `debug_assert!`：重建 quadtree 相等及 snapshot 与 Layout 相等 | C++ `Layout::Restore`（`Layout.cpp:262–443`）以 `Container::SameBase` 做身份检查，并额外验证 sentinel、placement/free-slot 唯一性和 generation 合法性。CDE 恢复失败通过 `Result` 返回；提交 container/slots 后才调 CDE Restore（388–397），失败时可能留下部分恢复状态。动态 hazards 不匹配在 Debug 抛 `logic_error`，Release 仍返回成功（438–442）。Rust 此函数没有上述显式快照元数据校验或可恢复 CDE 错误出口 |
| `place_item` (93–105) | 先将新 `PlacedItem` 插入 SlotMap，借 key 构造 dynamic hazard 并注册；debug 下检查 quadtree；返回 key。API 无 `Result`，shape 转换/注册没有显式错误分支 | `Layout::PlaceItem`（`Layout.cpp:108–137`）可因 CDE 缺失、PlacedItem 构造或 hazard 注册失败返回错误；注册失败会撤销 slot 并推进 generation、放回 free list（125–132）。C++ 对失败与回滚显式化 |
| `remove_item` (108–120) | slotmap remove 返回 `None` 时 `expect` panic（112）；有效时按 entity 注销 hazard，Debug 检查 quadtree 后返回被移除对象 | `Layout::RemoveItem`（`Layout.cpp:139–158`）无效句柄返回 `InvalidInput`；注销失败先返回错误且不清 slot；成功后清 slot、推进 generation 并回收 index |
| `is_empty`、`placed_item_area`、`density` (124–142) | 空性来自 SlotMap；面积按放置项原始 area 累加，density 为放置面积除以容器毛面积，类型均为 `f32` | C++ 对应 `PlacedItemCount/PlacedItemArea/Density`（`Layout.cpp:226–241,450–456`）；面积也以 `float` 累加、容器面积先转 `float` 后相除再转 `double` |
| `items` (145–150) | 按 SlotMap 遍历顺序映射 Item 引用，并按 `item.idx` 去重 | `Layout::Items`（`Layout.cpp:458–478`）按槽位索引扫描并按 `ItemIndex` 去重；两者都是首次出现顺序，但存储枚举次序由不同容器实现，仍需 B6 fixture 确认跨实现顺序 |
| `cde` (154–156) | 只读暴露布局碰撞引擎引用 | C++ 没有 Layout 的公开 CDE getter；可从其它接口操作或另行创建容器 CDE |
| `is_feasible` (160–168) | 遍历每个放置项，用 `haz_key_from_pi_key` 查自身 hazard；若缺映射则 `expect` panic；否则排除自身后执行多边形碰撞查询，任一碰撞返回 false | `Layout::IsFeasible`（`Layout.cpp:203–224`）CDE 为空时返回 false；用 `cdeHandle` 过滤自身，碰撞即 false。Rust 缺 map 属 panic，C++ 对 null engine 属普通 false |
| `ContainerMismatch` (171–174) | 零字段错误类型，用于不同 static base 的 restore 失败 | C++ 映射到 `ErrorCode::SnapshotMismatch`（`Layout.cpp:264–269`），错误载荷还包含消息 |
| `LayoutSnapshot` 与查询 (179–203) | 快照包含 container、完整 `SlotMap` 与 `CDESnapshot`；`density`/`placed_item_area` 重用 Layout 的原始面积逻辑 | C++ `LayoutSnapshot`（`Entities/Layout.h:17–34`）以 placements、CDE snapshot、slot generations、free-slot 顺序表达；`PlacedItemArea`（`Layout.cpp:35–53`）先按 handle `(index,generation)` 排序后以 float 累加。Rust 按 SlotMap `values()` 顺序累加，浮点求和次序的跨实现等价尚未由本记录证明 |

## B6 结论与未覆盖验证

- 四个 Rust 文件均已逐项登记；这只是 pinned-source 静态对照，不代表生命周期与 restore 已完成验收。
- Rust `Arc::ptr_eq(base_cde)` 与 C++ `Container::SameBase` 都以共享静态容器数据身份判同源；两者的实例创建/复制路径与快照可达路径仍需由 B6 用例证明相同。
- 已确认的接口级差异包括 Rust 无错误返回的 place/remove/swap 路径中存在 `expect` 或隐式假设，C++ 多数改为 `Result`；C++ snapshot 还保存 slot generation/free-list 元数据。
- 后续 B6 应重点覆盖跨同源/异源容器 restore、无效/过期 handle、hazard 注册失败回滚、snapshot 往返后 handle 复用顺序、`Items()` 去重顺序与不可行布局检查。当前记录未运行测试。

## 证据位置

- pinned Rust：`entities/container.rs:13–132`、`entities/item.rs:10–59`、`entities/placed_item.rs:11–39`、`entities/layout.rs:12–203`。
- C++：`JaguarCore/Source/Entities/Container.cpp:11–151`、`Item.cpp:9–110`、`PlacedItem.cpp:16–56`、`Layout.cpp:35–490`；公开声明见 `JaguarCore/Include/JaguarCore/Entities/{Container,Item,PlacedItem,Layout}.h`。
- 现有测试入口：`JaguarCore/Tests/TestLayout.cpp`、`JaguarCore/Tests/TestBoundary.cpp`；列出测试位置仅表示代码存在，不代表本轮执行或已满足 B6 验收。
