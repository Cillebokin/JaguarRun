# 34. import（导入器）逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/io/import.rs`（311 行）
**C++**: `JaguarCore/Include/JaguarCore/io/importer.h`（102 行）+ `JaguarCore/Source/IO/Importer.cpp`（714 行）
**判定**: 基本等价（转换语义一致；非连续重复顶点的显式检查位置和诊断不同，但两侧都会拒绝；`assert!`/`unimplemented!`→`Result` 错误；C++ 额外含 probs 层导入与 JSON 入口——probs 部分在记录 37）

> 范围说明：本记录覆盖 Rust `io/import.rs` 全文（entities 层导入：`Importer::new`/`with_min_item_separation`/`import_item`/`import_container` + 自由函数 `import_rotation`/`import_simple_polygon`/`centering_transformation`/`ext_to_int_transformation`/`eliminate_degenerate_vertices`）。C++ `Importer.cpp` 中 `ImportSPInstance`/`ImportBPInstance`/`ImportMSPInstance`/各 solution 导入及助手 `importPositiveItems`/`withSeparation`/`externalToInternal` 的 solution 用法属于 probs 层，在记录 37 比对。

## 逐条比对（函数/结构体）

### `struct Importer` + `Importer::new`（Rust 17-43 行）
- C++ 对应：`importer.h:15-30` 的 `ImporterConfig` + `Importer` 构造函数（Importer.cpp:225-228）。
- 判定：基本等价（配置形态不同）。
- 证据：Rust `Importer { shape_modify_config, cde_config }`，`new(cde_config, simplify_tolerance, narrow_concavity_cutoff)` 构造 `ShapeModifyConfig { offset: None, simplify_tolerance, narrow_concavity_cutoff }`（import.rs:35-42）；C++ `ImporterConfig { cdeConfig, shapeModifyConfig, minItemSeparation }`（importer.h:15-23），`ShapeModifyConfig` 整体由调用方给定（含 offset 字段，`withSeparation` 会覆写）。差异：Rust 构造时强制 `offset: None`（offset 只能经 `with_min_item_separation` 设置）；C++ 允许调用方直接在 `shapeModifyConfig.offset` 传入初始值，但 `ShapeConfig()`（cpp:230-236）在 `minItemSeparation > 0` 时会覆盖之——`minItemSeparation == 0` 时 C++ 保留调用方 offset，Rust 无此路径（`with_min_item_separation(0.0)` 置 `None`）。属 C++ 额外自由度。

### `with_min_item_separation`（Rust 45-54 行）
- C++ 对应：`withSeparation`（Importer.cpp:114-122，匿名命名空间）+ 各 `Import*Instance` 入口处的校验（cpp:520-525 等）。
- 判定：一致。
- 证据：Rust `ensure!(separation.is_finite() && separation >= 0.0, "min_item_separation must be finite and nonnegative")`，`offset = (separation > 0.0).then_some(separation / 2.0)`（import.rs:48-52）↔ C++ `withSeparation` 同式 `separation > 0.0 ? separation * 0.5 : nullopt`（cpp:118-120），有限非负校验在各 instance 入口（cpp:403-408 `ImportItem`、cpp:431-436 `ImportContainer`、cpp:520-525/593-598/672-677 各 instance）。C++ 采用"入口处建 scoped Importer"而非 Rust 的链式 builder，语义等价。

### `import_item`（Rust 56-105 行）
- C++ 对应：`Importer::ImportItem`（Importer.cpp:399-426）+ 私有 `ImportItemShape`（cpp:268-293）+ `ImportRotation`（cpp:327-397）。
- 判定：基本等价（逐分支一致；差异见下）。

分支对照：
- **Rectangle**：Rust `Rect::try_new(*x_min, *y_min, x_min + width, y_min + height)?` → `SPolygon::from(rect)`（import.rs:62-70）↔ C++ `normalizedRectangleBounds`（cpp:61-82，f32 域校验有限性/正宽高/xMin<xMax）+ 逆时针四点 `SimplePolygon::Create`（cpp:278-282）。一致（含 f32 收窄路径：cpp:64-67 先 `static_cast<float>`）。
- **SimplePolygon**：`import_simple_polygon(esp)?` ↔ `ImportSimplePolygon(polygon->points)`（cpp:285）。
- **Polygon（带孔）**：Rust `ensure!(ep.inner.is_empty(), "item {} has unsupported holes")`（import.rs:73-77）↔ C++ `invalidShape("item polygons with holes are unsupported")`（cpp:288-289）。一致（错误消息措辞不同，Rust 带物品 id）。
- **MultiPolygon**：`bail!("No support for multipolygons yet")` ↔ `invalidShape("item multipolygons are unsupported")`（cpp:292）。一致。
- **OriginalShape 组装**：Rust `pre_transform: centering_transformation(&shape)`（中心平移到原点）、`modify_mode: Inflate`（import.rs:84-89）↔ C++ `Transform2d::FromRotationTranslation(0.0, {-center.x, -center.y})`（cpp:417-420，`center = shape.Centroid()`）+ `ShapeModifyMode::Inflate`。一致。
- **Item 创建**：Rust `Item::new(internal_id, ext_item.id, original_shape, allowed_orientations, base_quality, cde_config.item_surrogate_config)`（import.rs:96-104）↔ C++ `Item::Create(internalIndex, external.id, std::move(original), m_Config.cdeConfig.itemSurrogateConfig, external.minQuality, std::move(rotation.Value()))`（cpp:423-425）。参数顺序不同但语义一致。
- **差异（校验时点）**：Rust 在形状导入完成后再 `import_rotation`（import.rs:94）；C++ 先 `ImportItemShape` 再 `ImportRotation`（cpp:410-415）——顺序一致。但 C++ `ImportItem` 入口先做 `minItemSeparation` 校验（cpp:403-408），Rust 该校验只在 `with_min_item_separation`。轻微差异：直接调用 `ImportItem` 时 C++ 会拒绝非法 separation 配置，Rust 无此检查（配置在 builder 时已定）。

### `import_container`（Rust 107-197 行）
- C++ 对应：`Importer::ImportContainer`（Importer.cpp:428-497）+ `ImportContainerOuter`（cpp:295-316）+ `ImportZoneShape`（cpp:318-325）。
- 判定：基本等价（逐分支一致；`assert!`→`Result`；zone 质量上限常量来源不同）。

分支对照：
- **外轮廓**：Rectangle/SimplePolygon/Polygon(取 outer，**忽略 holes 不报错**)/MultiPolygon(bail)（import.rs:114-126）↔ C++ `ImportContainerOuter` 同四分支（cpp:297-315），Polygon 分支同样 `ImportSimplePolygon(polygon->outer)` 不检查 holes（cpp:313-314）。一致。Rust `pre_transform: DTransformation::empty()`、`Deflate` ↔ C++ `Transform2d::Identity()`、`Deflate`（cpp:492-494）。一致。
- **holes**：Rust 仅 `ExtShape::Polygon(jp)` 分支导入 `jp.inner`（import.rs:135-147），Rectangle/SimplePolygon → `vec![]`，MultiPolygon → `unimplemented!` ↔ C++ `std::get_if<ExternalPolygon>` 才收集 holes（cpp:444-453），否则空；MultiPolygon 已在外轮廓分支报错，不可达。一致。
- **zones 数量断言**：Rust `assert!(zones.iter().all(|z| z.quality < N_QUALITIES), ...)`（import.rs:108-111）↔ C++ `if (zone.quality >= MAX_QUALITIES) return ... "quality zone is outside the supported range"`（cpp:457-461，`MAX_QUALITIES = 10`，cpp:19）。基本等价（assert→Result 错误；`N_QUALITIES` 为编译期常量 10，`MAX_QUALITIES` 为 cpp 内字面常量 10，值一致但来源不同）。
- **zone 形状**：Rust Rectangle/SimplePolygon 可用，Polygon → `unimplemented!(...)`、MultiPolygon → `unimplemented!`（import.rs:155-169）↔ C++ `ImportZoneShape`：`ExternalPolygon` → `invalidShape("quality zone polygons are unsupported")`、`ExternalMultiPolygon` → 同类错误，其余复用 `ImportItemShape`（cpp:318-325）。一致（unimplemented→UnsupportedShape 错误码）。
- **holes→quality 0 合并**：Rust `shapes_inferior_qzones[0].extend(holes)`（import.rs:175-176）↔ C++ `for (SimplePolygon& hole : holes) grouped[0].push_back(std::move(hole));`（cpp:468-469）。一致。
- **空 zone 丢弃**：Rust 对所有 `q in 0..N_QUALITIES` 生成 `InferiorQualityZone`（含空 shape 列表，`InferiorQualityZone::new(q, vec![])` 是否报错取决于内部；空 zone 由 `Container::new` 处理）↔ C++ `if (!grouped[quality].empty())` 才建 zone（cpp:474）。需注意：Rust 对空 zone 同样调用 `InferiorQualityZone::new`，若其内部允许空列表则行为一致；C++ 显式跳过空质量级。形式差异，最终容器 zone 集合语义一致（空 zone 无形状无作用）。另：Rust zone 内形状经 `OriginalShape{Inflate}` 包装修改后交给 `InferiorQualityZone::new`（import.rs:183-193）↔ C++ `zoneOriginal.ConvertToInternal()` 后收集（cpp:480-487），一致。
- **Container 创建**：Rust `Container::new(original_outer, quality_zones, self.cde_config)`（不含外部 id —— Rust Container 无 id 字段）↔ C++ `Container::Create(external.id, ...)`（cpp:495-496）。C++ 额外保留外部 id（供 BPP 导出 container_id 使用，见记录 35），Rust 导出时由调用方传 id。设计差异，非行为错误。

### `import_rotation`（Rust 200-245 行）
- C++ 对应：`Importer::ImportRotation`（Importer.cpp:327-397）+ 助手 `normalizeDegreesRustF32`（cpp:90-96）/`radiansRustF32`（cpp:98-102）。
- 判定：一致（含 MAX_ANGLES 65536、stepped 展开公式、`[0.0]→None` 归一化）。
- 证据：
  - `MAX_ANGLES: usize = 65_536`（import.rs:202）↔ `constexpr std::size_t MAX_ANGLES = 65536;`（cpp:18）。
  - **Continuous**：直接返回 `RotationRange::Continuous`（import.rs:204）↔ cpp:329-331。C++ 额外处理 `kind == None` → `RotationMode::None`（cpp:332-334），对应 Rust 侧不存在的外部输入（表示层哨兵，见记录 33）。
  - **Discrete**：非空且 ≤65536（`"discrete rotation requires 1..=65536 angles"`，import.rs:206-209 ↔ cpp:339-345 同文案）；全部有限（import.rs:210-213 ↔ cpp:347-354）；归一化 `angle.rem_euclid(360.0) % 360.0`（import.rs:216）↔ `normalizeDegreesRustF32`：`fmod(degrees,360)`，负则 +360，再 `fmod(...,360)`（cpp:90-96）。rem_euclid 与该三步等价（负数 rem_euclid 得正，再 %360 处理 360.0 边界）。一致。
  - **Stepped**：有限且 ∈(0,360]（import.rs:220-223 ↔ cpp:360-366）；`count = (360.0/step).round()`（f64），`count <= 65536 && |count*step - 360| <= 360*f32::EPSILON`（import.rs:224-230 ↔ cpp:367-376，`tolerance = 360.0 * std::numeric_limits<float>::epsilon()`）；展开 `i * 360.0 / count` 逐项算避免累积误差（import.rs:231-233 ↔ cpp:377-382）。逐字一致。C++ 额外 `count < 1.0` 拒绝（cpp:370）——Rust 中 `count` 为 0 时 `(0..0)` 生成空 angles，随后落入 discrete 分支吗？不：Rust stepped 生成空 vec 后 `angles.sort/dedup`，`angles == [0.0]` 为 false（空），返回 `Discrete(vec![])`；但 `count < 1` 意味着 step > 360 已被 (0,360] 上界排除，仅 step 恰为 360 时 count=1，故双侧该分支实际不可达，无行为差异。
  - **排序去重 + [0.0]→None**：`angles.sort_by(f32::total_cmp); angles.dedup(); if angles == [0.0] { None } else { Discrete(to_radians) }`（import.rs:236-244）↔ `std::sort` + `std::unique`（cpp:385-386）+ `degrees.size()==1 && degrees.front()==0.0F → None`（cpp:387-389）+ `radiansRustF32`（cpp:391-394）。一致。注：Rust `dedup` 只去相邻重复（sort 后等价 std::unique）；Rust `total_cmp` 对 NaN 排序——但 finite 校验已排除 NaN。
  - **度→弧度**：Rust `f32::to_radians()`（= `self * (π/180)` f32 域）↔ C++ `degrees * (pi / 180.0F)` 后升 double（cpp:98-102）。一致。

### `import_simple_polygon`（Rust 247-260 行）
- C++ 对应：`Importer::ImportSimplePolygon`（Importer.cpp:223-251）。
- 判定：有限输入下的去闭合、连续重复点消除和后续构造路径一致；非有限顶点的整体接受/失败契约未证。
- 证据：共同的步骤：
  1. 首尾相同去尾点：`points.len() > 1 && points[0] == points[len-1]` → pop（import.rs:250-252）↔ `points.size() > 1 && points.front() == points.back()` → pop_back（cpp:232-233）。
  2. 连续重复顶点消除：`eliminate_degenerate_vertices(&mut points)`（import.rs:254）↔ cpp:235-248 内联同算法（rustFpaEqual 判等、倒序删除）。
  3. `SPolygon::new(points)` ↔ `SimplePolygon::Create(std::move(points))`（cpp:250）。
  - Rust 在第 2 步后用 `unique()` 显式拒绝非连续重复，并返回 `Simple polygon has non-consecutive duplicate vertices`（import.rs:256-258）。C++ 没有同一层检查，但随后 `SimplePolygon::Create` 会在自交检查前逐对比较所有顶点（SimplePolygon.cpp:217-223），所以同一重复顶点也会被拒绝。对 `[A,B,A,C]` 等已核输入，两侧都是失败；差异是失败位置和诊断（Rust `anyhow` 文案 vs C++ `InvalidPolygon` / `simple polygon contains duplicate points`），不是该重复点场景的接受边界。Rust oracle 与 C++ `ImportItem` 回归用例确认这一点。
  - 另：C++ 入口先把每个坐标 `double→float→double` 收窄（cpp:227-231），模拟 Rust f32 输入域；Rust 天然 f32。一致。

### `centering_transformation`（Rust 262-267 行）
- C++ 对应：`ImportItem` 内联 `Transform2d::FromRotationTranslation(0.0, {-center.x, -center.y})`（Importer.cpp:417-420）。
- 判定：一致（无独立公共函数；`shape.centroid()` ↔ `shape.Centroid()`，见记录 05）。

### `ext_to_int_transformation`（Rust 269-285 行）
- C++ 对应：`externalToInternal`（Importer.cpp:131-151，匿名命名空间）。
- 判定：公式相同；非有限输入两侧均失败，失败通道不同。
- 证据：Rust：
```rust
Transformation::empty()
    .transform(&pre_transf.compose().inverse())
    .transform_from_decomposed(ext_transf)
    .decompose()
```
（import.rs:281-285，注释"1. undo pre-transform, 2. do the absolute transformation"）↔ C++ `original.preTransform.InverseRustF32().FollowedByRustF32(externalTransform)`（cpp:150）。矩阵语义：`T_ext ∘ T_pre⁻¹`（先逆预变换再施加外部变换），有限输入下公式相同。类型化C++ `ImportSPSolution`检查f32收窄后的非有限角度/平移并返回 `SnapshotMismatch`（cpp:116-135）；Rust不做同层校验：NaN在`DTransformation::new`触发panic，Inf旋转在矩阵构造中触发panic。对Inf平移，静态追到`ImportSPSolution`的完整类型化调用链：`DTransformation::new`允许Inf通过；随后`ext_to_int_transformation`调用`transform_from_decomposed`/`rotate_translate`，`dot_prod`计算旋转矩阵时包含`Inf * 0`，结果NaN由`NotNan`运算拒绝并触发panic，发生在`prob.place_item`之前。因此类型化Rust `import_solution`不会将该情况作为`Result`错误返回。该边界也可由JSON输入触发：当前锁定依赖未启用`serde_json/float_roundtrip`，有限JSON数值`3.5e38`先解析为有限f64，再经serde的f32 visitor窄化为`+Inf`并进入Rust导入panic路径；C++ `strtod`虽读到有限double，但`numberValue`随即收窄为f32并检查有限性，JSON入口在Importer之前返回`SerializationFailure`（JsonCodec.cpp:460-467,716-727,1270-1287；Importer.cpp:563-569）。因此同一JSON输入两侧失败阶段与错误通道不同；若通过类型化C++ `ExternalSPSolution`直接传入该有限double，则Importer返回`SnapshotMismatch`。Rust panic点、JSON可达性和C++解析出口已有本轮定向运行证据，详细结果见测试记录；既有NaN/Inf角度与形状fixture仍不能外推为所有非有限输入的统一结果。

### `eliminate_degenerate_vertices`（Rust 287-311 行）
- C++ 对应：`Importer::ImportSimplePolygon` 内联段（Importer.cpp:235-248）。
- 判定：一致。
- 证据：双侧算法：遍历 i，j=(i+1)%n，`approx_eq!(f32, p_i.0, p_j.0) && approx_eq!(f32, p_i.1, p_j.1)` 标记 i（import.rs:290-297）↔ `rustFpaEqual(points[i], points[next])`（cpp:237-241，分量级包装，直接委托公共比较器；非有限边界见记录38）；倒序排序索引后逐个 `points.remove(index)`（import.rs:300-310）↔ `std::sort(duplicateVertices.rbegin()...)` + `points.erase(...)`（cpp:243-248）。C++ 缺 Rust 的 `debug!` 日志输出（cpp 无日志设施差异）。Rust `index < points.len()` 守卫 ↔ C++ `if (index < points.size())`（cpp:246）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 非连续重复顶点检查 | 接受边界一致，诊断不同 | 两侧均拒绝；Rust 在 importer 层给出专用错误，C++ 由 `SimplePolygon::Create` 返回 `InvalidPolygon` | import.rs:256-258 vs Importer.cpp:235-250 + SimplePolygon.cpp:217-223 |
| 2 | `assert!`（zone 质量上限）→ `Result` 错误 | 基本等价 | C++ 所有构建下返回 `InvalidInput`；Rust 仅 debug 构建中止。`N_QUALITIES=10` ↔ `MAX_QUALITIES=10` 值一致但常量来源不同 | import.rs:108-111 vs Importer.cpp:19,457-461 |
| 3 | `unimplemented!`（zone Polygon/MultiPolygon、container MultiPolygon）→ `UnsupportedShape` 错误 | 基本等价 | panic 通道改 Result 通道；消息文案不同 | import.rs:145,165,168 vs Importer.cpp:315,320-325 |
| 4 | `with_min_item_separation` 形态：链式 builder vs 入口 scoped 导入器 | 一致 | 校验（有限非负）与 offset=separation/2 公式双侧一致；C++ `ShapeConfig()` 在 separation==0 时保留调用方 offset，Rust 总是置 None（C++ 额外自由度） | import.rs:45-54 vs Importer.cpp:114-122,230-236 |
| 5 | Container 外部 id | C++ 额外 | Rust `Container::new` 无 id；C++ `Container::Create(external.id, ...)` 保留（供 BPP 导出用） | import.rs:196 vs Importer.cpp:495-496 |
| 6 | `ext_to_int_transformation` 非有限变换 | 定向运行确认同一JSON输入Rust解析后panic、C++解析时返回`SerializationFailure`；类型化入口出口也不同 | C++ JSON的`numberValue`将有限double窄化为f32并在非有限时拒绝；类型化C++ Importer返回 `SnapshotMismatch`。Rust serde默认解析`3.5e38`为f32 Inf，再于矩阵乘法中由`Inf * 0 → NaN`触发`NotNan` panic，早于放置项 | import.rs:274-285、spp/io/import.rs:63-79、ext_repr.rs:171-184、transformation.rs:56-57,146-193、serde_json 1.0.151 vs JsonCodec.cpp:460-467,716-727,1270-1287、Importer.cpp:116-135,519-551；Rust Debug/Release oracle exit 0；MSVC14.29 C++ Release全量runner exit 0，覆盖`TestIo`/`TestJsonCodec` |
| 7 | `import_rotation` 全流程（65536 上限、stepped 展开、[0.0]→None、rem_euclid 归一化、f32 弧度换算） | 一致 | 错误消息文案逐一相同；`rem_euclid(360)%360` ↔ `fmod+fmod` 三步等价 | import.rs:200-245 vs Importer.cpp:327-397 |
| 8 | item/container 形状四分支 + Inflate/Deflate + 中心化 pre_transform | 一致 | Rectangle f32 域校验、holes 拒绝（item）/忽略（container outer）、MultiPolygon 拒绝均一致 | import.rs:60-133 vs Importer.cpp:268-316 |
| 9 | holes→quality 0 合并 | 一致 | `shapes_inferior_qzones[0].extend(holes)` ↔ `grouped[0].push_back` | import.rs:175-176 vs Importer.cpp:468-469 |
| 10 | `eliminate_degenerate_vertices` | 一致 | rustFpaEqual 判等 + 倒序删除，含 `index < len` 守卫；C++ 无 debug 日志 | import.rs:287-311 vs Importer.cpp:235-248 |
| 11 | min_item_separation 校验时点 | 差异（轻微） | C++ 在 `ImportItem`/`ImportContainer`/各 instance 入口重复校验；Rust 只在 builder。直接 API 调用时 C++ 更严格 | import.rs:47-51 vs Importer.cpp:403-408,431-436 |
| 12 | JSON 直入口（`ImportItemJson` 等） | C++ 额外 | 组合 `ParseExternalItemJson` + `ImportItem`；Rust 侧由 serde+调用方组合承担 | importer.h:39-44 vs 无 |
| 13 | 非有限多边形整体构造契约 | 部分实测；整体契约未证 | Rust 1.98.1 Debug/Release对现有NaN/+Inf fixture均panic；2026-10-22以MSVC14.29重建当前JaguarCore Debug/Release库及runner，Release全量runner exit 0。Debug全量runner exit 3已定位到`runCollisionTests()`注册`coveringShape`（`TestCollision.cpp:61-64`）时触发`Quadtree.cpp:423-426`的“无Partial子项”断言；该形状覆盖整个根节点，子象限均为`Entire`。单独运行现有`runIoTests()`的Debug runner exit 0，两个`InvalidInput`断言通过；未覆盖所有NaN/Inf输入 | SimplePolygon.cpp:203-213 vs simple_polygon.rs:41-74,120-141；Quadtree.cpp:423-426；TestCollision.cpp:61-64；TestIo.cpp:249-264 |
| 14 | JSON rotation f32溢出（`rotation: 3.5e38`） | 定向运行确认Rust Debug/Release解析为f32 Inf后在`cos(Inf)→NaN`处panic；C++ JSON入口返回`SerializationFailure`；C++类型化`rotationDegrees=3.5e38`入口返回`SnapshotMismatch` | Rust `DTransformation::new`允许Inf，旋转矩阵构造时`NotNan`拒绝NaN；C++ JSON `numberValue`在Importer前拒绝f32溢出，类型化入口在`externalToInternal`拒绝非有限值 | d_transformation.rs:20-26、transformation.rs:146-193、serde_json 1.0.151、JsonCodec.cpp:460-467,716-727,1270-1287、Importer.cpp:116-135；Rust Debug/Release oracle exit 0；MSVC14.29 C++ Release全量runner exit 0，覆盖`TestIo`/`TestJsonCodec` |
| 15 | JSON负向f32溢出（rotation/translation=`-3.5e38`） | Rust Debug/Release解析为`-Inf`并在导入中panic；C++ JSON入口返回`SerializationFailure`；类型化C++入口返回`SnapshotMismatch` | Rust用例同时断言负号保留；平移在矩阵乘法中产生NaN，旋转在`cos(-Inf)`产生NaN | RustOracle `--import-boundary-parity-only`、TestIo.cpp、TestJsonCodec.cpp；Rust Debug/Release exit 0；MSVC14.29 C++ Release全量runner exit 0 |
| 16 | JSON最大有限f32边界（`3.4028234663852886e38`） | Rust将rotation和translation解析为`f32::MAX`；C++ JSON解析成功，两个字段收窄后等于`float::max()` | 只证明serde/JsonCodec的解析与f32窄化边界；不证明该变换进入完整Importer后一定可放置 | RustOracle `--import-boundary-parity-only`、TestJsonCodec.cpp；Rust Debug/Release exit 0；MSVC14.29 C++ Release全量runner exit 0 |
| 17 | f32最大有限值到Inf的舍入临界点 | 中点前一个f64值`340282356779733623858607532500980858880`解析为±`f32::MAX`；中点`340282356779733661637539395458142568448`解析为±Inf。C++接受前值，正负中点均返回`SerializationFailure`。正向前驱的完整Importer结果另按rotation/translation x分别核对 | 中点解析仍只覆盖JSON解析和f64→f32舍入；完整Importer仅覆盖正向前驱及本记录固定的2×2 SPP实例，不外推负向前驱或其他几何 | Rust serde raw bits：前值`7f7fffff`/`ff7fffff`，中点`7f800000`/`ff800000`；Rust Debug/Release `--import-boundary-parity-only` exit 0；MSVC14.29 C++ Release全量runner exit 0；rotation几何raw bits一致，translation x分别为Rust panic/C++ `InvalidInput` |

## 结论

有限输入下导入管线的转换语义（形状四分支、Inflate/Deflate修改模式、中心化预变换、旋转展开与65536上限、stepped容差判定、`[0.0]→None`归一、连续重复顶点消除、holes合并quality 0、preTransform逆变换合成公式）在已核路径上保持一致。此前把“非连续重复顶点”列为接受边界缺口不准确：C++下游`SimplePolygon::Create`已拒绝已核重复顶点输入；两侧在该场景只差错误诊断。Rust 1.98.1、锁定jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`的`--import-boundary-parity-only`在Debug/Release均对现有`nan_coordinate`和`infinite_coordinate` fixture输出panic：前者落在直径的`NotNan` unwrap，后者落在包围盒`Rect::try_new` unwrap。2026-10-22以MSVC14.29重建当前JaguarCore Debug/Release库及runner；Release全量runner exit 0，Debug全量runner exit 3已定位到Importer测试之前的`runCollisionTests()`：注册覆盖根节点的`coveringShape`时命中Quadtree“无Partial子项”Debug断言（`TestCollision.cpp:61-64`、`Quadtree.cpp:423-426`），并非Importer失败。单独运行现有`runIoTests()`的Debug runner exit 0，确认`TestIo.cpp:249-264`两个坐标输入均返回`InvalidInput`。RustReference Debug测试在另一边界fixture上捕获了`qt_hazard.rs:113`同类断言；这只确认共享断言条件，不证明与C++覆盖根节点输入逐点相同。本轮新增同一JSON数值`3.5e38`的Rust/C++定向比较：Rust Debug/Release将其解析成f32 Inf并在导入中panic；C++ Release JSON入口在解析阶段返回`SerializationFailure`；类型化C++ Importer入口返回`SnapshotMismatch`。现有fixture仍不能代表全部NaN/Inf输入，完整多边形契约仍未证。非有限多边形构造契约和非有限变换失败通道是两个独立边界：前者源码可确认C++拒绝残留非有限顶点，后者此处新增了单值运行证据；其余差异为错误通道、C++保留容器外部id和额外API能力。

### 补充审计（2026-09-29）

对 import.rs 311 行逐行复核（含辅助函数全量），原记录遗漏/需修正各点：

1. **`import_item` 中 `debug!("[IMPORT] starting item {:?}", ext_item.id)`（import.rs:58，遗漏）**——Rust 导入每个物品时打日志；C++ 无日志输出。判定：C++ 缺失（日志，无行为影响）。同理 `eliminate_degenerate_vertices` 内 `debug!` 已在原记录差异表 10 提及。
2. **`import_container` zones 断言的**短路顺序**（浅覆盖→补齐）**——Rust `assert!` 在函数最前（import.rs:108-111），**先于**形状导入；C++ `zone.quality >= MAX_QUALITIES` 检查在 zone 循环内（cpp:457-461），即先导入外轮廓与 holes、再在首个非法 zone 处失败。若输入同时含非法外轮廓与非法 zone，Rust 报 zone 断言（debug 下 panic）、C++ 报外轮廓错误。失败优先级不同，接受边界相同（两侧最终都拒绝）。判定：差异（轻微，错误次序）。
3. **`import_rotation` Stepped 分支 `count < 1.0` 的可达性再核验（原记录 54 行已分析，补充 f32 收窄细节）**——Rust `step: f32`（`ExtRotation::Stepped { step: f32 }`）；C++ `RotationPolicy::stepDegrees` 为 double，`ImportRotation` 先 `static_cast<float>` 再校验（cpp:360）。`(0,360]` 上界排除 step>360；step→360⁻ 时 `360/step > 1`，round 后 count≥1；step=360 时 count=1。`count<1` 仅在 step>360（已被拒）出现，双侧不可达，原分析成立。另注意 C++ 展开式 `360.0 * i / count`（cpp:380-381）与 Rust `f64::from(i) * 360.0 / count`（import.rs:232）运算次序相同（先乘后除），逐位一致。
4. **`import_container` 空 zone 语义（原记录 44 行的判定补充 C++ InferiorQualityZone 空列表行为）**——Rust 对每个 `q in 0..N_QUALITIES`（含空）调用 `InferiorQualityZone::new(q, vec![])`；C++ `if (!grouped[quality].empty())` 才建（cpp:474）。原记录已指出"最终容器 zone 集合语义一致"。补充：Rust `InferiorQualityZone::new` 对空 shapes 是否成功由 entities 层决定（若失败则 Rust 直接报错而 C++ 静默跳过）；查 Rust `container.rs` 中 `InferiorQualityZone::new` 允许空列表（POI 计算在空列表时返回单位元）——该点属记录 24 范畴，本记录交叉引用。双侧最终行为一致。
5. **`import_simple_polygon` 的非连续重复检查在 Rust 是 `itertools::unique()`（浅覆盖→补齐算法细节）**——`points.len() != points.iter().unique().count()`（import.rs:256）：`unique()` 按**首次出现序**迭代去重，与排序无关；即检查"任意顶点值出现超过一次"。C++ 侧等价检查由 `SimplePolygon::Create` 的逐对比较承担（原记录已比对），此处仅补 Rust 侧语义（原记录未说明 `unique()` 的 O(n²) 全对语义）。
6. **`Importer::new` 的 `#[must_use]` 与 `with_min_item_separation` 返回 `Result<Self>`（遗漏的 API 形态细节）**——Rust builder 链式（`with_min_item_separation` 消耗 self 返回 `Result<Self>`）；C++ `ImporterConfig` 值拷贝 + `withSeparation` 自由函数返回新 config（cpp:114-122），无 builder 链。原记录差异 4 已覆盖语义，此处补充：C++ `withSeparation` 位于匿名命名空间，非公共 API——外部调用方无法复现"带 separation 的配置派生"，必须走 `Import*Instance` 入口。判定：差异（API 可达性，C++ 收窄）。
7. **`ext_to_int_transformation` 参数为 `&DTransformation` 而非 `&ExtTransformation`（遗漏的签名细节）**——Rust 该函数接**内部**分解变换类型（调用方 spp/import.rs:72-74 先 `DTransformation::from(ext)` 转换）；C++ `externalToInternal` 接 `ExternalTransformation`（cpp:131-134）内部做度→弧度。即 Rust 把"度→弧度"放在 From 转换（ext_repr.rs:180-184），C++ 放在 externalToInternal 内部——归属位置不同，数值路径一致（均 f32 域 `deg * (π/180)`）。
8. **`import_item` 的 `Item::new` 参数 `base_quality: Option<usize>`（浅覆盖→补齐字段名）**——原记录 31 行写"参数顺序不同但语义一致"；补充证据：C++ `Item::Create(internalIndex, external.id, original, itemSurrogateConfig, external.minQuality, rotation)`（cpp:423-425）中 `minQuality` 位置对应 Rust `base_quality`（import.rs:101），两侧均为 `Option/optional`。一致。
9. **`import_container` 的 holes 导入失败传播（遗漏）**——Rust `json_holes.iter().map(import_simple_polygon).collect::<Result<Vec<_>>>()?`（import.rs:139-143）任一 hole 失败即整体失败；C++ 同（cpp:446-452 逐 hole `ImportSimplePolygon` 失败即返回）。一致（补齐证据）。
10. **`import_item` Rectangle 分支的 `Rect::try_new` 错误传播（补齐 C++ 分支细节）**——Rust `Rect::try_new(*x_min, *y_min, x_min + width, y_min + height)?`：`x_min + width` 在 **f32 域**计算（Rust f32 类型）；C++ `normalizedRectangleBounds`（cpp:61-82）同样先收窄到 float 再 `xMin + width`（cpp:73-74）。原记录 26 行已提"f32 域校验"，此处补充加法也在 f32 域（非 double），逐位一致。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | `import_container` 断言/zone 校验时点 | 差异（轻微） | Rust zone 断言最先（debug panic）；C++ 外轮廓先行、zone 校验在循环内。失败优先级不同，接受边界相同 | import.rs:108-111 vs Importer.cpp:438-461 |
| S2 | `debug!` 日志（item 起始/退化顶点） | C++ 缺失 | 无行为影响 | import.rs:58,304-307 vs 无 |
| S3 | `withSeparation` 为匿名命名空间私有 | 差异（API 可达性） | 外部调用方无法构造派生配置，必须走 instance 入口 | import.rs:47-54 vs Importer.cpp:114-122 |
| S4 | `unique()` 全对去重语义（非相邻） | 补齐说明 | Rust 显式 O(n²) 检查 ↔ C++ SimplePolygon::Create 逐对比较 | import.rs:256 vs SimplePolygon.cpp:217-223 |
| S5 | Rectangle 分支 f32 域加法 | 一致（补齐证据） | `x_min + width` 双侧均在 f32 域计算 | import.rs:68 vs Importer.cpp:73-74 |

### E1 补充复核（2026-10-22）

- `eliminate_degenerate_vertices` 的 C++ 分量比较已移除非有限输入拦截，直接委托 `AlmostEqualRustF32`；因此 `f32::MAX/+Inf`、邻近 NaN 位模式等边界与 pinned `approx_eq!` 使用同一谓词。此处只说明连续顶点消除，不代表多边形对非有限值的整体接受/失败契约一致。
- C++ 随后的 `SimplePolygon::Create` 在重复点检查前拒绝残留非有限点；Rust `SPolygon::new` 没有同位置的 finite 扫描，后续几何计算仍可能报错或触发 panic。完整多边形构造边界继续单列，不从相邻比较一致推断端到端成功/失败等价。
- f64溢出中点前驱现已通过匹配的完整SPP实例进入`ImportSPSolutionJson`：巨大有限rotation在Rust/C++生成相同的变换与碰撞形状raw bits；相同值放入translation x后，f32精度使2×2形状的x包围盒折叠，Rust于`SPolygon::transform` panic，C++在`Rect2d::Create`返回`InvalidInput`。这是完整导入链的失败出口差异，详见[SPP solution IO记录](37-prob-io.md)和[Transformation记录](06-transformation.md)。
- `PlacedItem`有限几何补充对拍使用同一SPP实例（item id 201、2×2矩形、strip height 4），外部rotation=`37.25°`、translation=`[4.25,-1.75]`。Rust 1.98.1 / jagua-rs `f5e2b5ffd14931f17a20ce283a2122e877953ceb` Debug与Release输出逐raw-bit相同；C++ `TestJsonCodec`对完整Importer结果逐raw-bit断言，MSVC14.29 Debug隔离runner与Release全量runner均通过。变换=`3f266f42,408e1a48,beb28958`；顶点依序为`40880000,bfe00000`、`40baf1b2,bf0a16e9`、`40943490,3f86bb54`、`404285bb,be21c9bc`；bbox=`404285bb,bfe00000,40baf1b2,3f86bb54`；(collision area, diameter, centroid x/y, original Item area)=`40800000,403504f3,408e1a47,beb28953,40800000`；POI=`408e1a48,beb28958,3f800000`。surrogate存在，包含2个pole、0个pier、hull indices `[0,1,2,3]`、hull area `40800000`；pole依序为`408e1a48,beb28958,3f800000`及`4093280b,3f4fce76,3e2f40c8`。本结果只闭合一个有限输入和默认空配置生成的surrogate形态，不外推到其他变换、形状或surrogate配置。
