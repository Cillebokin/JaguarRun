# 36. SVG 导出逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/io/svg/layout_to_svg.rs`（487 行）+ `svg_util.rs`（291 行）
**C++**: `JaguarCore/Include/JaguarCore/IO/SvgExport.h` + `JaguarCore/Source/IO/SvgExport.cpp`
**判定**: 固定fixture的几何与渲染子集部分对齐，整份SVG DOM仍不等价。阶段160至176已对齐默认坐标方向、viewBox/线宽、可见标签、item/CD/Surrogate defs/use、路径命令文本、固定BPP quadtree路径、CD开关四组合几何子集、质量区原始/CD路径及碰撞高亮线/圆/区域障碍、min_quality质量边界子集；阶段222至245又补入真实BPP Importer容器路径、CD路径/样式、40×30、20×60与60×20矩形/斜边容器viewBox及描边倍率、斜边凸/凹与单洞/多洞容器默认/CD path数据和共有样式、quality 0及选定quality 2/5 path默认/CD双模式对照、多危险区碰撞线集合及`draw_cd_shapes × highlight_collisions`开关组合。固定SPP path数据、BPP min_quality主题填色与Surrogate pole/pier样式已对拍。C++文档title、scene、class/data诊断属性、collision-debug标记、Surrogate诊断wrapper和可配置描边色、坐标轴按第164/168阶段保留为兼容扩展；容器显式`fill-rule`和关闭CD高亮时空group是否输出仍有DOM差异，碰撞图元额外带有C++诊断`class`，碰撞线还显式写`fill="none"`，广泛BPP布局及部分调试输出尚未覆盖。

**状态说明**：下方“早期逐条比对记录”保留旧差异作为历史证据；当前结论以“差异汇总表”和本文末尾截至阶段245的复核为准。

## 早期逐条比对记录（截至2026-09-29，保留作历史证据）

### 逐条比对（函数/结构体）

### `SvgDrawOptions`（svg_util.rs:13-41）
- C++ 对应：`SvgExportOptions`（SvgExport.h:52-64）。
- 判定：差异（选项集合大幅不同）。
- 证据：Rust 六个开关 `theme / quadtree / surrogate / highlight_collisions / draw_cd_shapes / highlight_cd_shapes`（svg_util.rs:15-27，默认 `quadtree=false, surrogate=false, highlight_collisions=true, draw_cd_shapes=false, highlight_cd_shapes=true`，svg_util.rs:30-41）；C++ 仅 `theme / drawSurrogates / highlightCollisions / includeAxes / title`（h:53-64，默认 `drawSurrogates=false, highlightCollisions=true, includeAxes=true, title="JaguarCore Layout"`）。C++ 缺 `quadtree`、`draw_cd_shapes`、`highlight_cd_shapes` 三个开关（相应图层整体缺失，见下）；C++ 额外 `includeAxes`（坐标轴图层）与 `title`（文档标题）。语义交集仅 surrogate/highlight_collisions 两项。

### `SvgLayoutTheme` + `EARTH_TONES`/`GRAY`（svg_util.rs:43-102）
- C++ 对应：`SvgLayoutTheme`（SvgExport.h:25-49）。
- 判定：差异（字段集与默认值部分一致）。
- 证据：
  - 一致部分（eef0830 补齐）：`stroke_width_multiplier: 2.0` ↔ `strokeWidthMultiplier = 2.0`（svg_util.rs:62 vs h:28）；`container_fill (0xCC,0x82,0x4A)` ↔ `containerFill{0xcc,0x82,0x4a}`（h:30）；`item_fill (0xFF,0xC8,0x79)` ↔ `itemFill{0xff,0xc8,0x79}`（h:32）；`collision_highlight_color (0x00,0xFF,0x00)` ↔ `collisionHighlight{0x00,0xff,0x00}`（h:36）；`qz_fill: [Color; N_QUALITIES=10]` 十色 ↔ `qualityZoneFills` 十色（svg_util.rs:66-77 vs h:42-46，逐值相同：`000000/FF0000/FF5E00/FFA500/C7A900/FFFF00/CBFF00×4`）；`qz_stroke_opac: 0.5` ↔ `qualityZoneStrokeOpacity = 0.5`（h:48）。
  - C++ 额外：`collisionStroke{0x00,0x80,0xff}`（碰撞形状轮廓色）、`surrogateStroke{0x00,0xaa,0x55}`、`axisStroke{0x55,0x55,0x55}`（h:34,38,40）。
  - Rust 额外：`hole_fill`（svg_util.rs:48，C++ 无对应——C++ 不单独绘制 holes）、第二主题 `GRAY`（svg_util.rs:82-101，C++ 无预置主题集合）。
  - Rust 主题可序列化（`Serialize/Deserialize`，svg_util.rs:13,43），C++ 无。

### `Color`（svg_util.rs:127-171）
- C++ 对应：`SvgColor`（SvgExport.h:14-22）+ `color()`（SvgExport.cpp:24-32）。
- 判定：一致（`#RRGGBB` 大写十六进制输出；Rust `Display` `#{:02X}...`（svg_util.rs:130-134）↔ C++ `'#' << uppercase << hex << setw(2)...`（cpp:24-32））。Rust 额外 `From<String>` 解析与 serde（svg_util.rs:136-171），C++ 无。

### `change_brightness`（svg_util.rs:104-112）
- C++ 对应：`halfBrightness`（SvgExport.cpp:34-40，eef0830 新增）。
- 判定：一致（固定 fraction=0.5：`(f32::from(r) * 0.5) as u8` ↔ `static_cast<uint8_t>(float * 0.5F)`；Rust 的通用 fraction 参数 C++ 只用 0.5）。

### `blend_colors`（svg_util.rs:114-125）
- C++ 对应：无（C++ 物品固定用 `itemFill`，不做 min_quality 混色）。
- 判定：C++ 缺失（Rust 对带 min_quality 的物品将 `item_fill` 与 `qz_fill[q]` 各 50% 混合，layout_to_svg.rs:162-165；C++ SvgExport.cpp:196-201 恒用 `options.theme.itemFill`）。

### `original_shape_data`（svg_util.rs:173-183）
- C++ 对应：`ExportLayoutSvg` 内联选择：物品原始形/碰撞形两个 path（SvgExport.cpp:196-208）。
- 判定：差异。Rust 由 `draw_cd_shapes` 开关二选一（原始形或 CD 形）；C++ 无开关，**两个都画**（`item-original` 用原始形 + `item-collision` 用碰撞形虚线叠加），固定输出。

### `simple_polygon_data`（svg_util.rs:185-191）
- C++ 对应：`polygonPath`（SvgExport.cpp:66-83）。
- 判定：基本等价（`M x y L ... Z` 路径；Rust `move_to/line_to/close` ↔ C++ `"M " ... " L " ... " Z"`。数值格式不同：Rust svg crate输出 f32 默认格式，C++ `setprecision(17)` double（cpp:17-22））。

### `s_layout_to_svg`（layout_to_svg.rs:17-24）
- C++ 对应：无独立入口；`ExportLayoutSvg` 接收 `Layout&` 并内部 `layout.Save()`（SvgExport.cpp:172）。
- 判定：基本等价（快照入口由"调用方先 Restore"承担；无行为差异）。

### `layout_to_svg`（layout_to_svg.rs:28-41）
- C++ 对应：`ExportLayoutSvg` 的文档组装（SvgExport.cpp:121-155, 407-408）。
- 判定：差异（viewBox 公式不同）。
- 证据：Rust：`bbox = container.outer_orig.bbox().resize_by(高*0.01, 高*0.01)`，然后 `vbox = bbox.scale(1.1)`（layout_to_svg.rs:48-55, 31），输出 `"{x_min} {y_min} {width} {height}"`；C++：`margin = max(1e-6, largestSide * 0.05)`，`view = bounds ± margin`（cpp:140-145），`largestSide = max(width, height)`。数值上 Rust 的边距以高度为基准（1.1 倍缩放 ≈ 每侧 5% 高度），C++ 以最大边为基准取 5%——方向相似但公式与基准不同，产出 viewBox 数值不同。C++ 额外校验 bounds 有限且非退化（cpp:132-138，`SerializationFailure`）。

### `layout_to_svg_group`（layout_to_svg.rs:45-480，主体）
- C++ 对应：`ExportLayoutSvg` 主体（SvgExport.cpp:159-405）。
- 判定：不等价（结构性重写）。逐图层对照：

**0. 坐标方向与scene翻转（旧差异于第160阶段关闭）**
- 早期C++曾在scene上使用translate/scale翻转；第160阶段移除此变换，Rust与C++现在都直接输出布局坐标。TestSvgExport覆盖viewY非零的viewBox并断言不存在scale(1 -1)；固定seed fixture的路径数据逐条一致。scene节点按第164阶段保留为C++诊断分组，因此它仍是结构差异，但不再改变几何方向。

**1. stroke_width 公式**（layout_to_svg.rs:59-60）
- Rust：`min(bbox.width(), bbox.height()) * 0.001 * theme.stroke_width_multiplier`；C++：`largestSide * 0.0025 * multiplier`（cpp:147-148）。基准（min↔max）、系数（0.001↔0.0025）均不同，所有线宽派生值不同。

**2. 信息标签**（layout_to_svg.rs:62-82）
- Rust：`Text` 元素 `h/w/d/title` 摘要（"h: {:.3} | w: {:.3} | d: {:.3}% | {title}"），置于容器上方，monospace 字体、字号 `min(w,h)*0.025`。C++：无对应可见标签；`<title>` 文档级元素只放 `options.title`（cpp:155）。判定：C++ 缺失（可见标签）。

**3. container 图层**（layout_to_svg.rs:98-121）
- Rust：单 path（原始形或 CD 形二选一）+ bbox title，`fill=container_fill, stroke=black, stroke-width=2*stroke_width`。C++：两个 path——`container-original`（`fill-opacity=0.25`，stroke 用主题色）+ `container-collision`（碰撞形，虚线 `3*sw 2*sw`，`collisionStroke` 色，cpp:160-170）。样式与结构均不同。

**4. quality_zones 图层**（layout_to_svg.rs:123-154）
- C++ 对应：cpp:237-267（eef0830 新增）。
- 判定：基本等价（本轮补齐）。双侧：按质量级**降序**绘制（Rust `iter().rev()` ↔ C++ `quality-- > 0`）、`fill=qz_fill[q]`、`fill-opacity=0.50`、`stroke=change_brightness(fill,0.5)`、`stroke-width=2*sw`、`stroke-opacity=qz_stroke_opac`、`stroke-dasharray=5*sw`、`stroke-linecap/linejoin=round`、title "quality zone, q: {q}"。差异：(a) Rust 遍历 `container.quality_zones`（含空 zone 的 `.rev().flatten()`，跳过 None），C++ 对每个质量级在 zones 列表中 `find_if`，未建 zone 的质量级跳过——由于 C++ Container 只存非空 zone（记录 24），集合语义一致；(b) Rust 绘制的是**原始形**（`shapes_orig`，未经 offset 修改）或 CD 形（开关），C++ `zone->shapes` 为转换后的内部形（导入时 `ConvertToInternal` 产物，记录 24 指出 C++ 不保留 zone 原始形）——带 min_item_separation 时 C++ 的 zone 会显示收缩后的形状；(c) C++ stroke-dasharray 单值 `5*sw` ↔ Rust 同为单值 `5.0*stroke_width`，一致。

**5. items 图层**（layout_to_svg.rs:157-306）
- Rust：**defs/use 复用结构**——`Definitions` 中为每个 item 定义一次 `#item_{external_id}` 组，放置项用 `<Use href="#item_{id}" transform="translate(...) rotate(...)">` 引用（layout_to_svg.rs:159-182, 274-303）；变换取 `int_to_ext_transformation`（外部坐标系，layout_to_svg.rs:280）或 draw_cd_shapes 时的内部变换。C++：**逐放置项展开**——每个 placement 直接输出 `<g id="item_{index}_{externalId}">` 含完整 path，无 defs/use（cpp:172-235）；`item-placement` 组用 `transformAttribute(externalTransform)`（同一 `T_pre ∘ T_int` 公式，cpp:179-181），另附 `data-rotation-degrees/data-translation-x/y` 数据属性与 `item-collision` 虚线叠加 path（cpp:203-208）。判定：差异（复用机制与属性集不同；几何位置等价——同一变换公式）。
- 物品填充：Rust 按 min_quality 混色（见 `blend_colors` 条）；C++ 恒 `itemFill`。差异。
- Rust `fill-opacity=0.5, fill-rule=nonzero, stroke=black` ↔ C++ `fill-opacity=0.7, stroke=itemFill`（cpp:196-201）。样式差异。

**6. surrogate 图层**（layout_to_svg.rs:192-243）
- C++ 对应：cpp:210-233。
- 判定：基本等价（几何一致；样式分级不同）。双侧：取 `shape_cd.surrogate()`，poles 画圆、piers 画线，按内部变换放置（Rust `int_transf` = draw_cd_shapes ? 单位 : pre_transform 逆；C++ `Transformed(internalTransform)` 用放置项内部变换——注意 Rust 在 `draw_cd_shapes=false` 时**逆预变换**后由 Use 的外部变换补回，C++ 直接用内部变换因 path 是碰撞形；两者对代理位置的最终效果一致，均把代理放在绝对位置）。差异：Rust 对 pole 分三级样式（POI：`fill=black, fill-opacity=0.1`；ff pole：实线 `stroke-opacity=0.8`；非 ff：虚线 `stroke-opacity=0.5`，layout_to_svg.rs:195-216），C++ 统一样式（`fill=none, stroke=surrogateStroke`，cpp:99-112）；Rust 的 ff/POI 区分 C++ 缺失。

**7. highlight_cd_shapes 图层**（layout_to_svg.rs:84-95, 246-268, 455-460）
- C++ 对应：无独立虚线高亮图层（容器碰撞形以 `container-collision` 恒绘、物品碰撞形以 `item-collision` 恒绘）。
- 判定：C++ 缺失（作为开关与图层；C++ 以恒开的双 path 形态部分承担了"看见 CD 形"的需求，但无 `highlight_cd_shapes` 开关、无顶点圆点绘制）。

**8. quadtree 图层**（layout_to_svg.rs:309-345 + `quad_tree_data`/`qt_node_data` svg_util.rs:193-248）
- C++ 对应：无。
- 判定：C++ 缺失（三色 quadtree 叶节点绘制：红=Entire、绿=None、描边=Partial，依赖 `CDEngine` 内部结构暴露；C++ `Layout` 不暴露 cde()，记录 26）。

**9. collision 高亮图层**（layout_to_svg.rs:348-453）
- C++ 对应：cpp:269-384。
- 判定：基本等价（几何与主要样式一致；去重机制不同）。
  - 双侧：对每个放置项收集碰撞（Rust `collect_poly_collisions` + 过滤自身；C++ `CollectPolygonCollisions` + handle 比较跳过自身），PlacedItem 碰撞画 POI 连线（`stroke=collision_highlight_color, stroke-opacity=0.75, stroke-width=4*sw, dasharray="4*sw 8*sw", linecap/join=round`；C++ cpp:282-294 `highlightWidth=sw*4`，`dasharray=highlightWidth : highlightWidth*2` 同值）；Exterior 画 POI 圆点（半径 `3*sw`，Rust svg_util.rs:441-445 ↔ C++ cpp:338-344）；Hole/InferiorQualityZone 画到 zone POI 的连线，且 `min_quality` 满足要求的 IQZ 碰撞跳过（Rust layout_to_svg.rs:406-419 ↔ C++ cpp:346-379）。
  - 去重机制差异：Rust 用 `DefaultHasher` 哈希比较（`haz_hash < pi_hash` 才画，layout_to_svg.rs:375-386）；C++ 用 handle 大小（`currentHandleIsGreater`，cpp:313-319）。效果同为"每对只画一条"，但选择哪一侧画不同（哈希序 vs handle 序），线条方向相反（无视觉影响，路径无箭头）。
  - C++ 依赖 `CreateCollisionEngine + Restore` 重建碰撞查询（cpp:271-280），失败返回错误；Rust 直接用 `layout.cde()`。

**10. axes 图层**（C++ cpp:392-405）
- Rust 对应：无。判定：C++ 额外（坐标轴两线，clamp 到 viewBox，`includeAxes` 默认开）。

**11. collision-debug 组**（C++ cpp:386-389）
- Rust 对应：无。判定：C++ 额外（`data-layout-feasible` / `data-enabled` 元数据组）。

### `transform_to_svg`（layout_to_svg.rs:481-487）
- C++ 对应：`transformAttribute`（SvgExport.cpp:85-91）。
- 判定：变换语义基本等价、数字文本不等价（`translate({tx} {ty}), rotate({deg})` 的指令与顺序一致，C++用`ToDegreesRustF32`对齐f32度数换算；Rust直接显示f32，C++将f32结果提升为double后用`setprecision(17)`输出，字符串形态不同，见补充审计8/S7）。

### `data_to_path` / `point` / `circle` / `edge_data` / `aa_rect_data`（svg_util.rs:250-291）
- C++ 对应：`circleElement`（cpp:99-112）、`linePath`（cpp:93-97）按需实现。
- 判定：基本等价（元素级助手；Rust 的通用属性数组机制 ↔ C++ 各自内联属性；`aa_rect_data` 标记 dead_code 无对应）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 坐标方向/scene翻转 | 定向用例已对齐 | 第160阶段移除scene轴向翻转，坐标按Rust方式直接输出；scene作为额外分组保留 | layout_to_svg.rs:28-40 vs SvgExport.cpp:282-288; TestSvgExport.cpp:90-92 |
| 2 | viewBox 公式 | 固定SPP及真实Importer BPP fixture已对齐 | BPP 40×30 fixture为`-2.3300004 -1.8300003 44.66 33.66`；20×60 fixture为`-1.6600003 -3.6600006 23.32 67.32`；60×20矩形与斜边凸四边形fixture均为`-3.2200007 -1.2200003 66.44 22.440002`；更广输入域仍未全量对拍 | layout_to_svg.rs:48-55 vs SvgExport.cpp:263-284 |
| 3 | stroke_width 公式 | 固定SPP及真实Importer BPP fixture已对齐 | BPP 40×30 fixture默认倍率2.0为`0.1224`、倍率3.25为`0.1989`且不改变viewBox；20×60纵向fixture默认倍率2.0为`0.084800005`、倍率3.25为`0.13780001`且不改变viewBox；其他倍率/输入域仍未全量对拍 | layout_to_svg.rs:59-60 vs SvgExport.cpp:286-290 |
| 4 | defs/use 复用与placement | 固定SPP与Surrogate重复placement fixture已对齐（部分） | item、CD形状及Surrogate定义/引用数量和placement变换在对应fixture中一致；C++保留Surrogate诊断wrapper，scene等外层DOM也保留C++扩展 | layout_to_svg.rs:159-303 vs SvgExport.cpp:330-443,543-563 |
| 5 | 可见信息标签（h/w/density/title） | 固定SPP fixture已对齐 | 已输出容器bbox title、placement transform title和h/w/density文档标签；未证明任意布局文本输出全等 | layout_to_svg.rs:62-82 vs SvgExport.cpp:308-320,390-404,688-699 |
| 6 | quadtree图层与开关 | 固定BPP fixture的路径数据和样式已对齐；广泛输出未穷尽 | `SvgDebugOptions::drawQuadtree`输出Entire/Partial/None三类路径；seed=42、64 samples的BPP Release CLI对拍确认三类path `d`、fill、opacity、stroke-width与fill-rule逐项一致；全部调试组合仍未穷尽 | layout_to_svg.rs:309-345, svg_util.rs:193-248 vs SvgExport.cpp:515-552 |
| 7 | highlight_cd_shapes / draw_cd_shapes开关与图层 | 固定BPP间距fixture的四组合几何子集已对齐 | seed=42、64 samples、`min_item_separation=1.0`下，两开关四组合的container/item/CD轮廓、highlight use变换及顶点圆样式一致；draw=true且highlight=true时双方均输出104个顶点圆。C++容器路径显式写`fill-rule=nonzero`而Rust省略；关闭highlight时Rust保留空group而C++省略。质量区、其他布局和全部调试组合未穷尽 | layout_to_svg.rs:84-95,108-146,157-305,455-460 vs SvgExport.cpp:315-390,450-541 |
| 8 | quality_zones 图层 | 固定BPP质量区fixture的原始/CD路径与样式已对齐 | quality=2矩形区、`min_item_separation=1.0`下，`draw_cd_shapes`两状态的路径、填色、fill-opacity文本、描边属性和title均与pinned Rust Release逐字一致；更广质量区形状/布局未穷尽 | layout_to_svg.rs:123-154 vs SvgExport.cpp:475-507 |
| 9 | 主题 EARTH_TONES 对应值 | 一致（eef0830 补齐） | 十色 qz_fill、container/item fill、qz_stroke_opac、multiplier=2.0 逐值相同 | svg_util.rs:61-80 vs SvgExport.h:28-48 |
| 10 | min_quality物品混色 | 固定BPP与EARTH_TONES/GRAY定向用例已对齐 | 带质量要求物品使用主题qz_fill对应等级的逐通道平均色；其他主题/布局范围尚未穷尽 | layout_to_svg.rs:162-165 vs SvgExport.cpp:330-350 |
| 11 | GRAY主题、hole_fill与Color serde | 主题映射字段已对齐；其余接口仍不同 | Gray预设输出已定向核对；Rust hole_fill不参与当前绘制，C++未实现SvgDrawOptions的Serde序列化 | svg_util.rs:82-101,154-171 vs SvgExport.cpp:59-72,SvgExport.h:25-49 |
| 12 | collision 高亮（连线/圆点/zone 跳过规则） | 基本等价 | 样式常量一致；去重机制 hash vs handle（画线方向不同，无视觉影响）；C++ 需重建碰撞引擎 | layout_to_svg.rs:348-453 vs SvgExport.cpp:269-384 |
| 13 | axes 图层、collision-debug 组、data-* 属性、xmlEscape、options 校验 | C++ 额外 | Rust 无对应输出/校验 | SvgExport.cpp:114-118,131-138,386-389,392-405 |
| 14 | 真实Importer非矩形容器path与共有样式 | 固定60×20斜边凸四边形与L形凹多边形fixture已对齐 | 两条container `d`、fill、stroke、stroke-width逐字相同；其他非矩形形状及带洞容器未覆盖 | svg_b8.txt `bpp_svg_oblique_canvas_path_state`/`bpp_svg_concave_canvas_path_state`; CompareProblem.cpp:1673-1710 |
| 15 | BPP洞形容器的container与quality_zones SVG path | 固定单洞与双洞Polygon Bin的默认/CD container path及quality 0/2/5 path选定子集双模式已对齐 | 两个fixture的container组在两种模式下均只含1条外环path，CD模式外环按0.25间距内缩；quality 0组路径数为1/3，quality 2/5组路径数分别为q2=1、q5=0和q2=3、q5=1，顺序、几何与共有样式逐字匹配。不推及其它quality等级、洞形或完整SVG DOM | svg_b8.txt `bpp_svg_hole_canvas_quality_2_5_state`/`bpp_svg_hole_canvas_cd_quality_2_5_state`及multi-hole对应keys、quality-zero与container path keys; CompareProblem.cpp各对应差分断言 |
| 14 | surrogate 图层 | 固定fixture的pole/pier样式与defs/use复用已对齐（保留C++扩展） | POI填充与三类pole透明度/虚线、pier透明度规则按Rust输出；C++描边色仍使用可配置`surrogateStroke`，且保留每placement诊断wrapper | layout_to_svg.rs:192-243,271 vs SvgExport.cpp:190-221,399-443,543-563 |
| 15 | 返回类型 Document vs Result\<string\> | 基本等价 | panic→Result 系；C++ 额外 bounds/主题校验 | layout_to_svg.rs:28 vs SvgExport.cpp:121-129 |

## 结论

截至第168阶段，固定fixture已对齐坐标方向、viewBox/线宽、标签、item/CD/Surrogate defs/use和路径命令文本；BPP带min_quality物品的主题混色符合Rust；Surrogate pole/pier几何、pole分类样式和pier透明度已在同profile BPP fixture上复核。C++继续保留文档title、scene分组、class/data元数据、collision-debug标记、坐标轴、Surrogate诊断wrapper及可配置描边色，所以整份DOM/主题输出仍不全等。BPP广泛布局、任意布局和部分调试组合仍未全量对拍。

### 历史补充审计（2026-09-29）

本节记录的是2026-09-29时的源码审计快照，不代表当前状态。阶段160至166已更新viewBox、stroke_width、标签、固定fixture的defs/use及路径文本、`min_quality`混色、`SvgLayoutTheme::Gray()`和Surrogate pole分类样式。以下涉及这些项目的旧结论均由本文主表及阶段160至166复核更新取代；未被后续阶段覆盖的观察仍保留为历史证据。

对 layout_to_svg.rs（487 行）+ svg_util.rs（291 行）逐行复核（SvgExport.cpp 自 eef0830 后无改动，git log 确认最后修改为 2026-09-28 eef0830；quality_zones 图层与 `qualityZoneFills[10]` 主题已在位，原记录已反映）。重点核对任务关注的"功能性"遗漏：

1. **`blend_colors` 的功能性影响（历史结论，已由阶段165修复）**——Rust 对`min_quality = Some(q)`的物品填充`blend_colors(item_fill, qz_fill[q])`（layout_to_svg.rs:162-165，各分量平均后截断）。当时C++恒用`itemFill`，因此不同质量要求无法从SVG颜色区分。阶段165已实现逐通道平均并以固定BPP fixture及EARTH_TONES/GRAY定向用例复核；当前结论见主表第10项和S1。
2. **`DefaultHasher` 去重的功能性等价（原记录差异 12 已覆盖，补充机制细节）**——Rust `haz_hash < pi_hash` 用 `DefaultHasher`（SipHash-1-3，随机种子）对 `HazardEntity` 哈希（layout_to_svg.rs:375-386）：**每对碰撞去重的选择侧是随机的**（哈希序不定）；C++ 用 handle 的 `(index, generation)` 字典序（cpp:313-317）固定选 handle 大的一侧。效果同为每对一条线；方向随机 vs 固定。无视觉影响（线无箭头）。判定：基本等价（去重目标一致；确定性 C++ 更优）。
3. **`halfBrightness` 对 `change_brightness(color, 0.5)` 的覆盖（原记录已比对，补充调用点差异）**——Rust 唯一调用点是 quality zone 描边（layout_to_svg.rs:129）；C++ 同（cpp:250）。Rust 的通用 fraction 参数（0.5 以外的值）在库内无其他调用点，C++ 固定 0.5 **无功能损失**。判定维持一致。
4. **主题预设`GRAY`（历史结论，预设入口已由阶段165补齐）**——当时C++没有命名预设，但主题字段均可配置，因此可手工构造等价值。阶段165增加`SvgLayoutTheme::Gray()`并以定向断言核对关键字段；Serde接口仍不同，详见主表第11项。
5. **`hole_fill`（svg_util.rs:48,65,86）的"死字段"确认（原记录判"C++ 缺失"，修正影响评估）**——核实 Rust 侧 `hole_fill` 在 layout_to_svg.rs 中**零调用**（holes 以 quality 0 zone 身份绘制，用 `qz_fill[0]` 黑色，不用 hole_fill）。即 hole_fill 是**未使用的主题字段**（serde 可配置但渲染不用）。C++ 不实现**无功能损失**。判定修正：C++ 缺失（字段级）但无行为影响（Rust 死字段）。
6. **quality_zones 绘制顺序边界（补齐）**——Rust `container.quality_zones.iter().rev().flatten()`（layout_to_svg.rs:127）：zones 向量按质量升序存储（import 时 0..N 顺序 push），`.rev()` 从最高质量画起（后画的在下层——SVG 后序元素在上层，即**低质量（q=0 黑）最后画、显示在最上层**）。C++ `for (quality = fills.size(); quality-- > 0;)`（cpp:238-239）从 9 到 0，同序。一致（补齐"高→低"方向证据）。
7. **items defs/use 的 `int_transf` 语义（补充一个易错点）**——Rust `int_transf = draw_cd_shapes ? identity : pre_transform.compose().inverse()`（layout_to_svg.rs:184-190）：非CD模式下item def中的path是原始形，surrogate def中的pole坐标是CD域；C++以碰撞形及放置变换输出代理，最终几何位置一致。2026-09-29时C++未区分POI/ff/普通pole；阶段166已补入三类样式和断言，阶段168补入Surrogate defs/use复用及pier透明度。C++保留主题可配置的描边色和逐placement诊断wrapper；当前扩展见主表第14项。
8. **`transform_to_svg` 数值格式（历史差异，固定fixture已对齐）**——当时C++以double精度格式化变换参数。阶段160至163已改用Rust f32最短文本路径，并在固定SPP fixture中逐字对拍placement变换；任意布局与角度域仍未全量证明。
9. **`SvgDrawOptions` 的 serde 序列化（原记录差异 11 已提，补充用途评估）**——Rust `SvgDrawOptions`/`SvgLayoutTheme` 可 `Serialize/Deserialize`（svg_util.rs:13,43，含 `#[serde(default)]`）：用于从 JSON 配置驱动 SVG 渲染选项。C++ `SvgExportOptions` 为纯 POD 无序列化。若调用方生态（sparrow）用 JSON 传渲染配置，C++ 需自行实现。核实 sparrow 侧未用该序列化路径（仅代码内构造）。判定维持：C++ 缺失（能力级，当前无使用方）。
10. **`quad_tree_data` 的filter参数（历史结论，整层已移植）**——Rust `quad_tree_data(&layout.cde().quadtree, &NoFilter)`（layout_to_svg.rs:310）传`NoFilter`（全量统计）。2026-09-29时C++尚无该层；后续已通过`SvgDebugOptions::drawQuadtree`和`SvgDebugAccess::QuadtreeCells`加入Entire/Partial/None绘制。该层的全组合输出仍未全量对拍，当前状态见主表第6项。
11. **`aa_rect_data`（svg_util.rs:283-291，`#[allow(dead_code)]`）**——Rust 自标注死代码；C++ 无对应。一致（双侧均无行为）。
12. **C++ `polygonPath` 的空多边形守卫（补齐）**——`if (vertices.empty()) return {}`（cpp:71-72）：空 path `d=""` 输出。Rust `simple_polygon_data` 对空多边形 `vertex(0)` 会 panic（索引越界）。C++ 更防御（Rust 侧空多边形在导入时已被 `SPolygon::new` 拒绝，不可达）。判定：C++ 额外防御，正常路径无差异。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | min_quality物品混色 | 固定BPP与两个主题的定向输出已对齐 | item.min_quality存在时按item_fill和qz_fill[q]逐通道平均；BPP CLI固定样例的EARTH_TONES输出及GRAY单测输出一致 | layout_to_svg.rs:162-165、svg_util.rs:115-125 vs SvgExport.cpp:107-120,340-350 |
| S2 | POI/ff/普通pole与pier样式 | 固定BPP Release fixture的几何与透明度规则对齐；pole/pier描边颜色保留C++主题扩展 | POI填充黑色且透明度0.1；POI和fail-fast pole描边透明度0.8；普通pole透明度0.5并使用圆角虚线；pier描边透明度0.8；描边色仍由`surrogateStroke`控制 | layout_to_svg.rs:195-232 vs SvgExport.cpp:190-221,437-440 |
| S3 | hole_fill 为 Rust 死字段 | 修正影响评估 | 渲染不用（holes 走 qz_fill[0]），C++ 不实现无功能损失 | svg_util.rs:48 + layout_to_svg.rs 全文零调用 |
| S4 | GRAY主题 | 已提供且映射字段对齐 | C++ Gray映射字段为stroke 2.5、container D3、item 7A、qz 63、opacity 0.9、collision D00000；Rust hole_fill未参与渲染，Serde入口仍不同 | svg_util.rs:82-101 vs SvgExport.cpp:59-72、SvgExport.h:25-49 |
| S5 | DefaultHasher 去重随机性 | 基本等价（C++ 确定） | 每对一条线；选侧随机 vs handle 字典序 | layout_to_svg.rs:375-386 vs SvgExport.cpp:313-317 |
| S6 | 空多边形 path | C++ 额外防御 | `d=""` vs Rust panic（不可达路径） | svg_util.rs:185-191 vs SvgExport.cpp:71-72 |
| S7 | 变换与路径命令文本 | 定向fixture已逐字对齐 | 坐标/角度使用f32最短文本；transform分隔符及路径命令格式（逗号坐标、小写z）已对齐；仅固定seed四条path d已实测一致 | layout_to_svg.rs:481-487、svg_util.rs:250-260 vs SvgExport.cpp:139-159,193-195 |
| S8 | 根DOM和诊断元数据 | 有意保留的C++扩展 | 文档title、scene分组、class/data属性、collision-debug标记和坐标轴属于当前诊断导出契约，不作为Rust DOM等价证据 | SvgExport.cpp:282-288,294-309,331-349,375-387,448-449,471-472,652-687; docs/compatibility.md:110-120 |
| S9 | Surrogate defs/use复用 | 固定BPP fixture及双placement单测已对齐定义复用 | 每种物品的pole/pier只在defs定义一次，各placed item通过use引用；C++额外保留每placement的class/data诊断wrapper | layout_to_svg.rs:158-243,271-271 vs SvgExport.cpp:399-443,543-563 |
| S10 | 碰撞高亮 | 固定物品重叠、三物品两两重叠、Exterior、Hole、InferiorQualityZone多区域及`min_quality=0..4`边界fixture的开/关与图元样式已对拍；Stage230/231覆盖真实Importer多危险区fixture的关闭与CD组合 | 多zone按quality/index输出；q0孔洞始终碰撞，qz仅在quality小于min_quality时输出；关闭高亮不改变Layout不可行状态，但双方省略collision_lines组；CD形状开关在高亮关闭时仍有效；线样式和Exterior圆心/半径/填色相同；C++图元保留诊断`class`，线还显式写`fill="none"` | layout_to_svg.rs:348-454 vs SvgExport.cpp:611-729; TestSvgExport.cpp collision fixtures |

## 2026-10-27阶段160至165复核更新

- 固定seed SPP输入`LbfSppSmall.json`（seed 123456、64 samples、`ls_frac=0.25`）下，Rust与C++的viewBox逐字相同，均输出4个path、4个use；一个`item_201`定义被两个placement引用，一个`cd_shape_201`定义被两个CD高亮引用。placement变换值逐项一致。
- 第163阶段固定seed SPP对拍中，Rust与C++的4条path d按顺序逐字相同。C++通过共用点坐标格式化器统一多边形、边线、quadtree矩形及axes路径的命令语法；定向单测覆盖多边形、碰撞线和质量区边线。调试图层未在该CLI fixture中启用。
- 第164阶段核对固定seed DOM树和既有诊断契约，决定保留文档title、scene分组、class/data属性、collision-debug状态标记及坐标轴作为C++诊断扩展；compatibility.md、全景稿和差异索引已改正scene不再执行Y轴翻转的说明。保留这些扩展意味着DOM仍不与Rust逐项一致。
- C++物品定义由`Layout::Items()`按唯一物品类型输出到defs；CD路径以原始物品局部坐标定义，再用placement变换引用，避免把轮廓预先写成绝对点。默认渲染的路径/引用数量已与该fixture对齐。
- 阶段162增加Rust格式的container bbox title和placement transform title，并把container CD outline归入highlight_cd_shapes；删除默认输出中重复placement title的item_diagnostics组，统一optionals层分组。上述固定seed下两类title逐字相同。
- 仍有非等价输出：C++保留scene包装、class/data属性和空文档title；路径命令格式已按固定seed fixture对齐；Surrogate开启时仍逐placement展开；BPP广泛布局、其他主题输入和通用调试场景未完成对拍。因此本记录判定仍为部分对齐，不能宣称全量SVG等价。

## 2026-10-27阶段165：BPP min_quality混色与主题输出复核

- 依据Rust layout_to_svg.rs的逐通道0.5混合，min_quality=2在默认EARTH_TONES下期望#FF933C。为LbfBppSmall.json加上该字段后，Rust Debug与C++ Release BPP CLI都成功输出；item fill均为#FF933C，container fill均为#CC824A。
- C++ Gray主题的映射字段与Rust GRAY源码一致。TestSvgExport断言带min_quality物品输出#6E6E6E，并核对Gray容器#D3D3D3、质量区#636363及opacity 0.9、碰撞高亮#D00000。
- min_quality=10在Rust中会索引越界并panic；C++现在返回SerializationFailure以避免越界访问。该错误出口仍有明确差异。
- Debug/Release的JaguarCore与测试runner构建成功；JaguarCoreTests.exe --svg-export-only两配置退出0。旧输出先因缺少混色按预期失败。
- 结论限于固定BPP fixture和已列主题值，不宣称全量BPP输出或整份SVG结构等价。

## 2026-10-27阶段166：Surrogate POI与fail-fast pole调试样式

- **Rust语义**：`poles[0]`是POI；`ff_poles()`是从首位开始的fail-fast前缀。POI用黑色填充、0.1填充透明度和0.8描边透明度；其余fail-fast pole用无填充、0.8描边透明度；普通pole用无填充、0.5描边透明度及`5*stroke_width`圆角虚线。
- **最小实现**：C++复用现有pole顺序与`FailFastPoleCount()`，不改公开结构或ABI；保留原`surrogate-pole` class并增加`data-pole-role`标记区分poi/fail-fast/ordinary，同时保留C++主题中的可配置`surrogateStroke`描边颜色。
- **回归与验证**：先以7个pole、3个fail-fast pole的真实U形物品用例确认旧输出因缺少POI类别断言而失败；随后MSVC 19.29 Debug/Release JaguarCore和测试runner重建成功，`JaguarCoreTests.exe --svg-export-only`两配置均退出0。
- **范围**：关闭POI和fail-fast角色无法区分的缺口；C++描边颜色仍是主题扩展，Surrogate仍逐placement绘制，POI/ff位置及所有调试组合尚未与Rust全量DOM对拍，L4保持部分完成。

## 2026-10-28阶段167至168复核更新

- **同profile基线**：固定BPP U形fixture使用seed=42、64 samples、`ls_frac=0.25`、7个pole和2条pier。Rust Debug CLI与C++ Release CLI不能直接比较pier选择：pinned `piers.rs`的`RAYS_PER_ANGLE`、`N_ANGLES`和网格密度受`debug_assertions`控制。Rust Debug输出的pier与Release不同；改用同revision的Rust Release CLI后，pole与pier的几何输出和C++ Release一致。
- **共享定义**：C++将每种物品的Surrogate pole/pier放进一次`<defs><g id="surrogate_ID">`定义，placement保留`class="surrogate" data-item-id`诊断wrapper并以`<use href="#surrogate_ID">`引用。双placement单测确认定义1份、use和wrapper各2份；Rust不输出该wrapper，属于已保留的C++诊断扩展。
- **pier样式修正**：Rust的fail-fast pier使用`stroke-opacity=0.8`；C++此前漏掉该属性。新增pier样式断言先在旧输出上失败，补上透明度后通过。POI、fail-fast pole、普通pole与pier的几何和透明度规则在固定fixture上相同；C++ `surrogateStroke`仍可配置为`#00AA55`，Rust使用黑色。
- **运行时证据**：MSVC 19.29 JaguarCore和测试runner Debug/Release重建成功，`JaguarCoreTests.exe --svg-export-only`两配置退出0。Rust Release与C++ Release LbfCli均处理该BPP fixture并退出0；解析SVG确认Surrogate defs各1份、use各1个、7个pole、2条pier，pole几何/非颜色样式与pier几何/样式字段一致。
- **范围**：本轮收口固定fixture的Surrogate定义复用、role样式与pier透明度差异；保留C++诊断wrapper和主题色属于既有裁决。此证据不表示整份SVG DOM、广泛BPP输入或全部调试选项已等价，L4仍为部分完成。

## 2026-10-28阶段169复核更新

- **覆盖对象**：使用阶段167的固定BPP U形fixture，seed=42、64 samples、`ls_frac=0.25`，开启quadtree图层，关闭Surrogate和collision highlights；pinned Rust与C++ Release CLI均退出0。
- **差异与修正**：初始对拍发现C++每条非空quadtree path末尾多一个空格，并且Entire/Partial路径缺少`fill-rule="nonzero"`。新增定向测试先在fill-rule断言处失败；随后C++改为仅在子路径间输出空格，并给Entire/Partial补上`nonzero`，None保持无该属性。
- **验证结果**：MSVC 19.29 Release测试runner重建成功，`JaguarCoreTests.exe --svg-export-only`退出0；此前Debug对应测试也通过。复跑两侧Release CLI并解析SVG，Entire/Partial/None三条path的`d`文本逐字一致，fill、fill-opacity、stroke-width和fill-rule均一致，且C++路径无尾随空格。
- **范围**：收口固定fixture的quadtree文本和样式差异；C++保留class/data诊断属性。未覆盖广泛BPP布局、其他Rust profile和全部调试选项，整份SVG DOM仍不等价，L4保持部分完成。

## 2026-10-28阶段170复核更新

- **fixture与矩阵**：在阶段167的BPP U形输入上设置`min_item_separation=1.0`，固定seed=42、64 samples、`ls_frac=0.25`，并关闭quadtree、Surrogate和collision highlights。pinned Rust Release与C++ Release CLI分别运行`draw_cd_shapes`×`highlight_cd_shapes`四种组合，全部退出0并输出1个placed item。
- **几何结果**：正间距使Rust/C++的container与item显示路径在`draw_cd_shapes`开关前后都发生变化；逐组合对比两侧container/item路径、CD轮廓、highlight use的href/transform与描边属性，结果一致。启用CD显示与高亮时两侧都生成104个顶点圆，圆心、半径、填充色和透明度一致。
- **残余DOM差异**：C++基础container path显式写`fill-rule="nonzero"`，Rust省略该属性（SVG默认语义相同）；关闭`highlight_cd_shapes`时Rust仍序列化空`highlight_cd_shapes` group，C++省略空group。两项不改变该fixture的可见几何；保留为DOM差异记录。
- **范围**：本轮核对固定BPP正间距fixture及两个开关的四种组合；没有覆盖质量区、广泛布局或其他调试层组合，L4仍为部分完成。

## 2026-10-28阶段171复核更新

- **fixture与运行**：固定BPP U形输入加入quality=2矩形质量区（`[30,30]`至`[70,70]`）和`min_item_separation=1.0`，seed=42、64 samples、`ls_frac=0.25`。Rust Release与C++ Release CLI分别以`draw_cd_shapes=false/true`运行，两侧四个结果均退出0且各放置1个item。
- **首个差异与修正**：两种开关状态下zone路径`d`、填色、描边宽度/透明度/dasharray、linecap/linejoin及title均相同；唯一逐字差异是Rust输出`fill-opacity="0.50"`、C++输出`0.5`。将既有质量区测试改为精确断言`0.50`，旧实现按预期在该断言失败，再把C++字面文本修正为`0.50`。
- **验证**：MSVC 19.29 JaguarCore及测试runner Debug/Release重建成功；`JaguarCoreTests.exe --svg-export-only`两配置退出0。重建后的C++ Release CLI与pinned Rust Release复跑后，`draw_cd_shapes`两状态的质量区路径与全部所核对属性逐字一致；两侧路径随开关切换，原始区path为29字符，CD区path为1273字符。
- **范围**：本轮只核对一个矩形质量区、一个质量等级及固定BPP正间距样例；多区域、洞、多边形区及广泛布局仍未覆盖，L4继续部分完成。

## 2026-10-28阶段172复核更新

- **fixture与输出**：一组100×100容器、两个10×20矩形分别放置于`(45,50)`和`(53,50)`以产生物品/物品重叠；另一组同容器单物品中心放在`(1,50)`以产生Exterior碰撞。pinned Rust Release的RustOracle模式、JaguarCore MSVC Debug/Release测试runner均生成碰撞高亮开启/关闭SVG。
- **线图元**：开启时每侧均在`collision_lines`中输出一条path，`d="M53,50 L45,50"`完全一致；`stroke="#00FF00"`、`stroke-opacity="0.75"`、`stroke-width="0.81600004"`、`stroke-dasharray="0.81600004 1.6320001"`、round linecap与linejoin逐字段一致。
- **Exterior圆图元**：开启时每侧均输出一个圆，`cx="1"`、`cy="50"`、`r="0.61200005"`和`fill="#00FF00"`逐字段一致。两个fixture关闭高亮后，Rust、C++ Debug和C++ Release均不输出`collision_lines`组。
- **DOM差异**：C++碰撞path保留`class="collision-highlight"`并明确写`fill="none"`；圆同样保留诊断class。Rust对应图元没有class，线也省略fill属性。按既有契约保留诊断元数据，不改生产导出行为。
- **验证**：MSVC 19.29 Debug/Release测试runner重建成功，`JaguarCoreTests.exe --svg-export-only`两配置退出0；RustOracle锁定`f5e2b5f` Release构建并生成四种SVG。线/圆关键属性解析后逐项相等。`cargo fmt --check`另报告`main.rs:1832`的格式差异，与本轮新增段无关；新增代码段已按rustfmt格式调整。
- **范围**：证据覆盖一组物品/物品重叠、一组Exterior碰撞和高亮开关；Hole、InferiorQualityZone、多碰撞对及自定义主题未覆盖，L4仍为部分完成。

## 2026-10-28阶段173复核更新

- **fixture**：复用同一100×100容器和10×20矩形物品；Hole使用quality=0矩形区`[2,2]`至`[4,4]`、物品中心`(5,5)`；InferiorQualityZone使用quality=1矩形区`[60,60]`至`[80,80]`、min_quality=2物品中心`(72,70)`。
- **图元与样式**：pinned Rust Release、JaguarCore MSVC Debug/Release在两类fixture开启高亮时都输出一条path；Hole路径均为`M5,5 L3,3`，InferiorQualityZone路径均为`M72,70 L70,70`。两条线的stroke、opacity、width、dasharray、linecap和linejoin均逐字段一致。关闭高亮后双方均省略`collision_lines`组。
- **验证与差异**：旧C++测试已有quality-zone线端点检查，本阶段增加了碰撞线完整样式、Hole fixture及开关输出检查；RustOracle对拍模式也扩展为生成Hole/QZ两状态SVG。MSVC 19.29 Debug/Release `--svg-export-only`均退出0；pinned Rust Release构建和SVG生成退出0。既有C++诊断class及`fill="none"`差异保持记录。
- **范围**：只覆盖单个Hole和单个低于物品min_quality的quality zone；多个zone、多碰撞线、自定义主题及复杂形状未覆盖，L4仍为部分完成。

## 2026-10-28阶段174复核更新

- **fixture**：同一100×100容器放置三个10×20矩形，中心分别为`(45,50)`、`(49,50)`和`(53,50)`，三对矩形全部重叠；RustOracle对拍入口与C++回归均增加高亮开/关状态。
- **去重结果**：开启时Rust Release、C++ Debug和Release都输出3条collision path，分别代表`45↔49`、`45↔53`、`49↔53`三个无向物品对；将线端点方向归一后集合完全一致，三对各出现一次，没有重复或漏画。逐条线的stroke、opacity、width、dasharray、linecap与linejoin相同。
- **开关与验证**：关闭时三侧均没有`collision_lines`组。MSVC 19.29 Debug/Release测试runner重建成功，`JaguarCoreTests.exe --svg-export-only`均退出0；锁定Rust revision `f5e2b5f`的Release fixture构建和SVG生成退出0。`cargo fmt --check`只报告`main.rs:1832`与本轮无关的格式差异。
- **范围**：仅覆盖3个同形矩形构成的完全两两重叠集合；大规模碰撞集合、混合形状、重复zone hazard与自定义主题仍未覆盖，L4继续部分完成。

## 2026-10-28阶段175复核更新

- **fixture**：单个10×20物品放在`(50,50)`，min_quality=3；同一quality=1包含两个重叠物品的独立矩形区，quality=2再包含一个区；quality=3也放入与物品相交的区域作为边界过滤对照。
- **hazard映射与过滤**：pinned Rust Release和JaguarCore Debug/Release都为quality=1的两个zone及quality=2的zone各画一条线，端点集合为`(50,50)↔(46,46)`、`(50,50)↔(49,46)`、`(50,50)↔(54,53)`；quality=3等于物品min_quality，被双方过滤。三条线的stroke、opacity、width、dasharray、linecap和linejoin相同。
- **开关与验证**：关闭高亮后Rust及C++两配置均无`collision_lines`组。MSVC 19.29 Debug/Release runner构建和`--svg-export-only`通过；锁定Rust revision `f5e2b5f` Release构建及SVG生成通过。`cargo fmt --check`只报告`main.rs:1832`与本轮无关的格式差异。
- **范围**：验证同quality多shape索引、跨quality映射以及等于min_quality的过滤边界；更大zone集合、交叠zone、多形状碰撞与其他质量边界仍待覆盖，L4继续部分完成。

## 2026-10-28阶段176复核更新

- **fixture**：单个10×20物品放在`(50,50)`；quality=0孔洞中心为`(46,50)`，quality=1/2/3的重叠区域中心分别为`(49,50)`、`(52,50)`和`(50,51)`。四个区域彼此交叠且都与物品相交。
- **min_quality边界**：以`min_quality=0..4`逐档运行。Rust Release、C++ Debug和Release输出线数分别为`1、1、2、3、4`；quality=0孔洞在所有档位均保留，quality=1在min_quality≥2时出现，quality=2在≥3时出现，quality=3在=4时出现。三个实现的无向端点集合及各线样式一致。
- **开关与验证**：关闭高亮后三侧每档都省略`collision_lines`组。MSVC 19.29 Debug/Release runner构建及`--svg-export-only`退出0；RustOracle锁定revision `f5e2b5f`的Release构建和十份SVG生成退出0。`cargo fmt --check`仅报告`main.rs:1832`与本轮无关的既有格式差异。
- **范围**：证明此组重叠矩形区和min_quality 0至4边界的路径选择；更高质量值、空区、多组件复杂轮廓和其他几何重叠关系仍未覆盖，L4继续部分完成。

## 2026-10-29阶段177复核更新

- **fixture**：单个10×20物品位于`(50,50)`，`min_quality=9`；quality=7是空shape集合，quality=8含两个分别以`(46,46)`和`(49,46)`为中心的矩形shape，quality=9含一个以`(52,50)`为中心且与物品相交的矩形shape。
- **边界结果**：pinned Rust Release与JaguarCore MSVC Debug/Release都输出两条碰撞高亮线，端点集合为`(50,50)↔(46,46)`和`(50,50)↔(49,46)`；quality=9因等于`min_quality`而过滤，空quality=7没有shape或碰撞线。三侧质量区均按quality 9、8、8绘制，路径几何及fill/stroke样式逐项一致；碰撞线stroke、opacity、width、dasharray、linecap和linejoin一致。关闭高亮后均省略`collision_lines`组。
- **验证与差异**：MSVC 19.29 Debug/Release测试runner构建及`--svg-export-only`通过；RustOracle锁定依赖的Release构建和fixture生成通过；逐节点提取比较确认三侧结果相同。C++继续保留`class`/`data-quality`诊断属性，Rust质量区路径用title携带等级，属性差异不计入几何与样式等价。`cargo fmt --check`只报告`main.rs:1832`既有且与本轮无关的格式差异。
- **范围**：补齐最高有效quality等级过滤、空等级和同等级多shape覆盖；非矩形/复杂轮廓、更多布局关系及整份SVG DOM仍未覆盖，L4继续部分完成。

## 2026-10-29阶段178复核更新

- **fixture**：quality=8使用凹形六边形`(40,40)`、`(60,40)`、`(60,59)`、`(59,59)`、`(59,60)`、`(40,60)`；10×20物品中心位于`(58,50)`且`min_quality=9`。该凹角位于内切圆外，区域POI为`(50,50)`。C++侧用`Container::Create`直接构造内部质量区，不覆盖外部Importer的非矩形区域导入能力。
- **几何与高亮**：pinned Rust Release与JaguarCore MSVC Debug/Release均输出一条quality-zone碰撞线，归一端点为`(50,50)↔(58,50)`。凹形区域path逐字为`M40,40 L60,40 L60,59 L59,59 L59,60 L40,60 z`；区域title、fill/stroke属性及碰撞线stroke/opacity/width/dasharray/linecap/linejoin逐项一致。关闭高亮后三侧都省略`collision_lines`组。
- **验证与差异**：Debug/Release测试runner构建及`--svg-export-only`通过；RustOracle锁定依赖的Release构建和fixture生成通过；对Rust/C++ Debug/C++ Release的collision endpoints、区域path与样式对拍通过。`cargo fmt --check`在本轮新增代码格式调整后只剩`main.rs:1832`既有格式差异。
- **范围**：仅证明该单个正交凹角、多边形质量区和固定物品位置；更广泛凹形、带孔/多轮廓及整份SVG DOM仍未覆盖，L4继续部分完成。

## 2026-10-29阶段179复核更新

- **fixture**：quality=8使用带斜边内凹口的六边形`(40,40)`、`(60,40)`、`(60,58)`、`(58,58)`、`(59,60)`、`(40,60)`；10×20物品中心为`(58,50)`且`min_quality=9`。斜边和凹口均在中心最大内切圆之外，区域POI为`(50,50)`。C++侧直接构造内部Container，Importer入口不在本轮覆盖范围。
- **几何与高亮**：pinned Rust Release与JaguarCore MSVC Debug/Release都输出一条碰撞线，归一端点为`(50,50)↔(58,50)`；凹形区域path逐字为`M40,40 L60,40 L60,58 L58,58 L59,60 L40,60 z`。区域title、fill/stroke属性及碰撞线stroke/opacity/width/dasharray/linecap/linejoin一致；关闭高亮后均省略`collision_lines`组。
- **验证与差异**：MSVC 19.29 Debug/Release runner重建与`--svg-export-only`通过；RustOracle锁定依赖的Release构建和fixture生成通过；三侧的碰撞端点、质量区几何与样式归一比较通过。`cargo fmt --check`只剩`main.rs:1832`既有格式差异。
- **范围**：覆盖一个带单条斜边的正交凹口fixture；一般斜边轮廓、复杂多凹点及Importer入口仍未覆盖，L4继续部分完成。

## 2026-10-29阶段180复核更新

- **fixture**：quality=8按顺序包含一个矩形shape`[44,44]`至`[48,48]`及一个斜边凹形shape`(50,40)`、`(70,40)`、`(70,58)`、`(68,58)`、`(69,60)`、`(50,60)`；10×20物品中心位于`(50,50)`，`min_quality=9`。C++直接构造内部Container，以隔离碰撞shape索引到SVG路径的映射。
- **索引与几何结果**：Rust Release与JaguarCore MSVC Debug/Release均输出且仅输出两条不同碰撞线，分别连接物品POI`(50,50)`与shape 0 POI`(46,46)`、shape 1 POI`(60,50)`；未重复引用同一个shape。两条线的stroke、opacity、width、dasharray、linecap和linejoin相同；质量区的矩形/凹形path及填充、描边属性均相同。关闭高亮后三侧均不输出`collision_lines`组。
- **验证与差异**：MSVC 19.29 Debug/Release runner构建及`--svg-export-only`通过；RustOracle锁定依赖Release生成通过；三侧按无向端点、shape路径及样式归一比较通过。`cargo fmt --check`只剩`main.rs:1832`既有格式差异。
- **范围**：覆盖同一quality下凸形与凹形组件的两个碰撞索引；更多组件数量、复杂轮廓和Importer入口仍未覆盖，L4继续部分完成。

## 2026-10-29阶段181复核更新

- **fixture**：quality=8按顺序有三个shape：不与物品接触的矩形`[5,5]`至`[10,10]`、碰撞矩形`[44,44]`至`[48,48]`、斜边凹形组件`(50,40)`、`(70,40)`、`(70,58)`、`(68,58)`、`(69,60)`、`(50,60)`；物品中心为`(50,50)`、`min_quality=9`。C++直接构造内部Container。
- **索引结果**：Rust Release与JaguarCore MSVC Debug/Release都只输出两条碰撞线，分别指向组件1的POI`(46,46)`和组件2的POI`(60,50)`；组件0不产生碰撞线，前置非碰撞组件没有造成索引偏移或错取。三侧三个quality path、碰撞端点与全部线样式相同；关闭高亮后均不输出`collision_lines`组。
- **验证与差异**：MSVC 19.29 Debug/Release runner构建及`--svg-export-only`通过；RustOracle锁定依赖Release运行通过；三侧按无向端点、组件path和样式归一比较通过。`cargo fmt --check`仍只报告`main.rs:1832`既有格式差异。
- **范围**：验证三个同quality组件中间隔一个非碰撞组件时的`zoneIndex`映射；任意组件数、外部Importer入口与整份SVG DOM仍未覆盖，L4继续部分完成。

## 2026-10-29阶段182复核更新

- **fixture**：quality=8按顺序包含碰撞矩形`[44,44]`至`[48,48]`、远离物品的矩形`[5,5]`至`[10,10]`、斜边凹形组件`(50,40)`、`(70,40)`、`(70,58)`、`(68,58)`、`(69,60)`、`(50,60)`；10×20物品中心为`(50,50)`、`min_quality=9`。C++直接构造内部Container。
- **索引结果**：Rust Release与JaguarCore MSVC Debug/Release都只输出两条线，分别指向shape 0 POI`(46,46)`与shape 2 POI`(60,50)`；中间shape 1有可见区域路径但不生成碰撞线。三侧碰撞线、三个quality-zone path及样式相同；关闭高亮后均无`collision_lines`组。
- **验证与差异**：MSVC 19.29 Debug/Release runner构建及`--svg-export-only`通过；RustOracle锁定依赖Release生成通过；按无向端点、组件路径和样式逐项比较通过。`cargo fmt --check`只剩`main.rs:1832`既有格式差异。
- **范围**：证明单个中间非碰撞组件不会压缩或错置后续`zoneIndex`；不推及任意组件数、Importer入口或整份SVG DOM，L4仍为部分完成。

## 2026-10-29阶段183复核更新

- **fixture**：quality=8包含五个有序shape：远置矩形`[5,5]`至`[10,10]`、碰撞矩形`[44,44]`至`[48,48]`、远置矩形`[80,5]`至`[85,10]`、斜边凹形组件`(50,40)`、`(70,40)`、`(70,58)`、`(68,58)`、`(69,60)`、`(50,60)`、远置矩形`[5,80]`至`[10,85]`；物品中心`(50,50)`、`min_quality=9`。C++直接构造内部Container。
- **索引结果**：Rust Release与JaguarCore MSVC Debug/Release均只输出两条线，指向shape 1 POI`(46,46)`及shape 3 POI`(60,50)`；shape 0、2、4都保留质量区路径但没有碰撞线。五条质量区路径、两条碰撞线端点和样式一致；关闭高亮后三侧均省略`collision_lines`组。
- **验证与差异**：MSVC 19.29 Debug/Release runner构建及`--svg-export-only`通过；RustOracle锁定依赖Release生成通过；按碰撞端点、五个shape路径与样式逐项对拍通过。`cargo fmt --check`只报告`main.rs:1832`既有格式差异。
- **范围**：验证三个非碰撞组件分散在两碰撞组件前后时的固定索引映射；不推及任意组件数量、Importer入口或整份SVG DOM，L4仍为部分完成。

## 2026-10-29阶段184复核更新

- **Importer贯通fixture**：外部容器为100×100矩形，导入quality=8的`ExternalSimplePolygon`凹形轮廓`(40,40)`、`(60,40)`、`(60,58)`、`(58,58)`、`(59,60)`、`(40,60)`；放置10×20、`min_quality=9`物品于中心`(58,50)`。质量区经`Importer::ImportContainer`导入后进入Layout、碰撞检测和SVG导出，没有直接构造内部Container。
- **结果对拍**：Importer接受该无孔简单多边形；放置结果不可行并产生一条质量区碰撞线，端点为`(58,50)↔(50,50)`。Rust Release侧复用阶段179的同几何SVG fixture（不将其视作Rust ExternalRepr Importer入口验收）；Rust Release与JaguarCore MSVC Debug/Release的轮廓path均为`M40,40 L60,40 L60,58 L58,58 L59,60 L40,60 z`；碰撞端点以及fill、fill-opacity、stroke、stroke-width、stroke-opacity、stroke-dasharray、linecap和linejoin一致。关闭高亮后三侧均省略`collision_lines`组。
- **Importer边界与DOM差异**：`ExternalPolygon`变体（包括holes为空的情况）及`ExternalMultiPolygon`仍由质量区Importer拒绝；本 fixture 不声称覆盖带孔或多轮廓质量区。C++继续输出`class="quality-zone"`、`data-quality`和碰撞线`class="collision-highlight"`、`fill="none"`等诊断/显式属性，Rust对应SVG省略部分属性；这些既有DOM扩展不改变本轮核对的几何与共享样式。
- **验证**：MSVC 19.29 JaguarCore测试Runner Debug/Release重建成功，隔离运行目录下`JaguarCoreTests.exe --svg-export-only`均退出0；Rust toolchain 1.98.1及jagua-rs revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`按Cargo.lock/manifest锁定，`cargo run --locked --release`生成成功；解析SVG后对三侧区域路径、碰撞线端点及共同样式属性逐项比较通过。
- **范围**：补齐Importer到SVG的单个凹形简单质量区路径；不覆盖ExternalPolygon/ExternalMultiPolygon质量区输入、多个Importer质量区、一般凹形与整份SVG DOM，B8/L4继续部分完成。

## 2026-10-29阶段185复核更新

- **边界fixture**：沿用阶段184的有效100×100外容器，分别把quality=8区域设为无holes的`ExternalPolygon`、含一个hole的`ExternalPolygon`和含一个polygon的`ExternalMultiPolygon`，逐项调用`Importer::ImportContainer`。
- **C++出口**：两种`ExternalPolygon`输入均以`ErrorCode::UnsupportedShape`和`quality zone polygons are unsupported`失败；`ExternalMultiPolygon`以同一错误码和`quality zone multipolygons are unsupported`失败。拒绝发生在质量区shape分派处，没有生成Container供后续Layout或SVG处理。
- **Rust基线裁决**：锁定revision `f5e2b5f`的`jagua-rs/src/io/import.rs`在quality-zone的`ExtShape::Polygon(_)`与`ExtShape::MultiPolygon(_)`分支使用`unimplemented!()`；Rust运行时出口是panic/trap，并非可直接等同的Importer错误结果。C++保留确定、可检查的`UnsupportedShape`错误作为安全映射，记录为错误机制差异，不宣称panic文本或跨语言异常类型等价。
- **验证**：MSVC 19.29 Debug/Release Runner重建成功，隔离目录下`JaguarCoreTests.exe --svg-export-only`均退出0；错误码和消息由三条独立断言锁定。Rust行为依据Cargo锁定源码分支核查，本轮没有把C++错误文本当成Rust运行时对拍结果。
- **范围**：锁定C++对两种Polygon形态及MultiPolygon的拒绝出口，并裁决与Rust未实现panic的边界；质量区多边形支持、跨语言panic等价及B8其他IO差异仍未完成，B8/L4继续部分完成。

## 2026-10-29阶段222复核更新

- **贯通fixture**：复用Stage217的`bpp_container_minsep_multi_zone_order`，从共享JSON经Rust和C++各自的BPP Importer导入，取首个bin的Container创建空Layout并调用实际SVG导出。输入含`min_item_separation=0.5`、外轮廓、两个polygon hole、显式quality 0，以及交错排列的quality 2/5区域；没有手工重建内部Container。
- **几何对拍**：Rust Debug/Release输出相同的path-state：container组1条path，quality_zones组7条path。依序比较两组中各`d`属性，C++ Debug/Release均与锁定Rust `f5e2b5f`逐字一致；质量区路径次序为quality 5、quality 2的三个组件、quality 0显式区、两个hole轮廓。
- **活动回归与验证**：新增RustOracle `--svg-imported-bpp-parity-only`入口和`svg_b8.txt`固定结果；`CompareProblem.cpp`从同一共享JSON实际导入、导出，并读取该fixture进行Debug/Release比较。MSVC 19.29 Debug/Release测试runner重建成功，`JaguarCoreTests.exe --problem-differential-only`两配置退出0。
- **范围与差异**：本轮只验证该真实Importer路径产生的容器外轮廓及质量区path几何文本和顺序，不比较样式、collision/debug层或整份SVG DOM；C++诊断class等扩展按既有裁决保留。B8与L4仍部分完成。

## 2026-10-29阶段223复核更新

- **覆盖路径**：复用Stage217同一份BPP输入和Stage222的Importer→Layout链路，分别以Rust `draw_cd_shapes=true`与C++ `SvgDebugOptions::drawCdShapes=true`导出碰撞检测形状层。正间距触发容器收缩及质量区/孔洞轮廓扩展，因而覆盖Overlay后的实际多边形，而不只是调试开关状态。
- **几何对拍**：Rust Debug/Release生成同一path-state，含1条容器碰撞轮廓及7条质量区/孔洞碰撞轮廓。C++ MSVC Debug/Release导出的各path `d`文本、path数量及组内顺序均与Rust逐字一致；次序仍为quality 5、quality 2的三个shape、quality 0显式区及两个洞。
- **验证与范围**：新增CD路径固定状态到`svg_b8.txt`，活动比较器在Debug/Release都读取并比较。测试runner重建后，`--problem-differential-only`两配置退出0；`cargo fmt --check`仍仅因8处既有RustOracle格式差异退出1，本轮新增代码不在差异中。仅收该固定Importer fixture的CD路径几何与顺序，不比较样式、其他debug层或整份SVG DOM；L4仍部分完成。

## 2026-10-29阶段224复核更新

- **覆盖内容**：继续使用Stage217的Importer输入及Stage223的`draw_cd_shapes=true`导出。针对container比较fill、stroke、stroke-width；针对quality zone比较fill、fill-opacity、stroke、stroke-width、stroke-opacity、stroke-dasharray、linecap与linejoin，并比较各path所属quality。
- **质量映射**：Rust以path内title读取quality；C++以`data-quality`取值，并逐path断言它与C++ title一致。归一后的quality顺序为`5,2,2,2,0,0,0`，共享样式字段与Rust锁定结果逐字相同。C++ `class`、容器ID等诊断属性保留且不纳入共享样式比较。
- **验证与范围**：Rust Debug/Release各自与`svg_b8.txt`中同一CD style状态逐字匹配；MSVC 19.29 Debug/Release重建后运行`--problem-differential-only`均退出0。`cargo fmt --check`仍因8处既有格式差异退出1，新代码段没有格式差异。证据仅限此固定Importer fixture及列出的路径字段，整份SVG DOM和其他调试层仍未闭合，L4继续部分完成。

## 2026-10-29阶段225复核更新

- **放置项来源与位置**：从阶段217的BPP JSON经双方Importer导入物品和容器，再用导入的第0号物品构造测试Layout，放在`(5.5,5)`使碰撞轮廓命中第1个Polygon hole。Rust的BPP solution importer仍未实现，本轮不把手工`Layout::place_item`描述成BPP解导入验收。
- **SVG对拍**：在默认与`draw_cd_shapes=true`两态比较item原始/CD定义路径的`d`和共有样式、`#item_1`/`#cd_shape_1`引用及变换，以及collision path数量、端点和stroke属性。两态各输出1条相同碰撞高亮线`M5.5,5 L5,5`；默认显示的placement变换为`translate(5 4.5), rotate(0)`，CD显示变换为`translate(5.5 5), rotate(0)`，Rust/C++逐字段匹配。
- **验证与边界**：Rust Debug/Release新增状态均匹配`svg_b8.txt`；JaguarCore MSVC 2019 Debug/Release重建后`--problem-differential-only`退出0，Release断言有效。范围限于此固定导入容器、单个手工放置物品和单个hole碰撞，不覆盖BPP solution导入、一般布局或完整SVG DOM；B8/L4仍部分完成。

## 2026-10-29阶段226复核更新

- **输入与放置**：新增`bpp_svg_quality_minimum_3`共享BPP JSON，沿用Stage217多洞、多质量区容器并将导入物品设为`min_quality=3`。另用原`min_quality=null`物品作控制；三种情况均为测试代码手工放置，不是BPP solution导入。
- **过滤与可行性结果**：min_quality=3物品放入quality=2区时，Layout不可行且输出1条高亮线`M25,5.25 L25.5,5`；无质量门槛控制物品放入quality=5区时同样不可行并输出1条线`M5,13.25 L5.5,13.5`；min_quality=3物品放入quality=5区仍不可行，但因质量达标不输出高亮线。前两种状态的碰撞线stroke、opacity、width、dash、linecap和linejoin一致。
- **对拍与验证**：Rust 1.98.1 Debug/Release输出相同状态并锁入`svg_b8.txt`；JaguarCore MSVC 2019 Debug/Release runner重建后`--problem-differential-only`均退出0。`cargo fmt --check`仍因8处既有格式差异退出1，本轮代码未出现在差异中。
- **范围**：只验证此固定Importer容器、三个手工放置与质量过滤分支，不覆盖BPP solution导入、一般布局或完整SVG DOM；B8/L4仍部分完成。

## 2026-10-29阶段227复核更新

- **真实Importer与场景**：复用`bpp_svg_quality_minimum_3`的BPP JSON和Importer生成的带`min_item_separation=0.5`容器；将`min_quality=3`物品手工放置于`(0.5,15)`。物品原形仍靠近容器内侧，正间距扩张后的碰撞轮廓越过收缩后外边界，触发Exterior hazard。
- **结果对拍**：Rust Debug/Release与JaguarCore MSVC Debug/Release均判定Layout不可行并输出1个Exterior圆，`cx=0.5`、`cy=15`、`r=0.18360001`、`fill=#00FF00`逐字段一致。此物品带`min_quality=3`，但Exterior高亮仍输出，证明质量过滤仅作用于InferiorQualityZone。
- **Debug边界与验证**：完全脱离四叉树根区域的碰撞轮廓会触发Rust源码中的`debug_assert!`及C++对应Debug收缩校验；因此固定fixture选择与根边界相交、可跨Debug/Release运行的位置。Debug/Release差分Runner均退出0，Release断言启用；`cargo fmt --check`仍只报告8处既有格式差异。
- **范围**：只覆盖该固定BPP Importer容器、单物品手工放置及Exterior圆，不覆盖BPP solution导入、一般布局或完整SVG DOM；B8/L4仍部分完成。

## 2026-10-29阶段228复核更新

- **多危险区fixture**：新增`bpp_svg_multi_hazard_min_quality_3`共享BPP输入，物品为18×18、`min_quality=3`，由真实Importer生成容器和物品后手工放置于`(12.5,9.75)`。碰撞轮廓覆盖两个Polygon洞、quality 0、quality 2和quality 5区域，且仍处于可运行的容器与四叉树根范围内。
- **过滤与线集合**：Rust Debug/Release均不可行并输出4条高亮线，分别指向q0中心`(13,5)`、q2中心`(20,5.5)`及两个洞中心`(5,5)`、`(9,5)`；q5中心`(5.5,13.5)`不产生高亮。按端点无方向规范化并排序后，比较完整共享stroke字段。
- **验证与范围**：`svg_b8.txt`固定记录Debug/Release结果；Rust Debug/Release及JaguarCore MSVC Debug/Release的定向runner均退出0。只证明该输入和手工放置的多危险区碰撞集合，不覆盖BPP solution导入、任意布局/危险区数或整份SVG DOM，B8/L4仍部分完成。

## 2026-10-29阶段229复核更新

- **CD组合**：复用阶段228真实Importer生成的BPP多危险区布局，分别以默认选项和`draw_cd_shapes=true`导出SVG；比较归一后的碰撞端点集合及每条线的完整共有stroke字段。
- **结果**：Rust Debug/Release下两种导出的碰撞状态逐字相同，均为4条线：两个Polygon洞、q0与q2保留，q5过滤。C++ MSVC Debug/Release导出状态均匹配`svg_b8.txt`，并断言CD状态与默认状态相同。
- **验证与范围**：锁定Rust Debug/Release oracle与JaguarCore Debug/Release `--problem-differential-only`均退出0。只验证此固定多危险区Layout的碰撞高亮对CD显示开关不敏感，不覆盖CD几何自身或完整SVG DOM，B8/L4仍部分完成。

## 2026-10-29阶段230复核更新

- **关闭行为**：在Stage228真实Importer多危险区Layout上设置`highlight_collisions=false`（C++ `highlightCollisions=false`），Layout仍不可行，但Rust与C++ SVG都不含`collision_lines`组；独立状态为`feasible=false;collisions=0`。
- **差分证据**：新增RustOracle Debug/Release固定行，并由JaguarCore MSVC Debug/Release比较器消费；测试先因缺少Rust参考键按预期失败，补齐Oracle与fixture后两配置runner通过。
- **范围**：仅证明固定多危险区输入下关闭绘制不改变可行性且省略碰撞组，不等同于清除Layout碰撞，也不覆盖一般布局或完整SVG DOM；B8/L4仍部分完成。

## 2026-10-29阶段231复核更新

- **组合开关**：在Stage228多危险区Layout同时设置`draw_cd_shapes=true`和`highlight_collisions=false`。Rust/C++ Debug/Release均保持`feasible=false`，并省略`collision_lines`组；fixture除`feasible=false;collisions=0`外，还逐字锁定`item_1`的CD显示路径。
- **开关独立性**：对比`item_1`路径，组合状态与仅开启CD显示的状态一致，且不同于默认原始形状路径；关闭碰撞高亮没有覆盖或撤销CD显示选项。
- **矩阵与范围**：至此该固定fixture的`draw_cd_shapes × highlight_collisions`四种组合均已覆盖；两侧差分runner通过。该矩阵不覆盖其它debug选项、更多输入或完整SVG DOM，B8/L4仍部分完成。

## 2026-10-29阶段232复核更新

- **真实Importer fixture**：复用`bpp_container_minsep_multi_zone_order`，Importer输出40×30外容器和正间距0.5；对空Layout执行默认SVG导出。
- **画布与描边**：Rust与C++ Debug/Release的根`viewBox`均为`-2.3300004 -1.8300003 44.66 33.66`，container path的`stroke-width`均为`0.1224`，按属性文本逐字比较。
- **验证与范围**：C++断言先因缺少canvas参考键失败；加入Rust Debug/Release输出后，两配置MSVC差分runner通过，Oracle行逐字匹配fixture。该结果只扩展viewBox/基础描边在这一个真实Importer BPP fixture上的证据，不代表全域等价，B8/L4仍部分完成。

## 2026-10-29阶段233复核更新

- **输入与设置**：复用Stage232真实BPP Importer布局（40×30外容器、`min_item_separation=0.5`），仅将SVG主题`stroke_width_multiplier`设为3.25。
- **结果**：Rust和C++ Debug/Release的`viewBox`均保持`-2.3300004 -1.8300003 44.66 33.66`；container `stroke-width`从默认倍率下的`0.1224`变为`0.1989`，两侧属性文本逐字一致。
- **验证与范围**：新增测试先因缺少倍率参考键按预期失败；Rust Debug/Release行加入fixture后，MSVC Debug/Release差分runner均通过，Rust输出与fixture逐字相符。只验证一个非默认正倍率与该固定BPP输入，其他倍率/主题和全SVG等价仍未收口。

## 2026-10-29阶段234复核更新

- **纵向真实Importer fixture**：新增SVG专用`bpp_svg_portrait_canvas`输入，20×60简单多边形容器、`min_item_separation=0.5`、正库存bin，经真实BPP Importer导入后对空Layout导出默认SVG。
- **长宽比与描边**：Rust与C++ Debug/Release根`viewBox`逐字为`-1.6600003 -3.6600006 23.32 67.32`，container path `stroke-width`均为`0.084800005`。与40×30横向fixture形成两种长宽比的固定样例，未覆盖一般输入域。
- **验证与限制**：C++比较器按预期先因缺参考键失败；Rust Debug/Release参考行补齐后Oracle逐字匹配，MSVC Debug/Release差分runner通过。`cargo fmt --check`仍报告8处既有格式差异，本轮新增代码已不在差异中；本次只证明固定fixture状态，不等于全局viewBox/线宽或完整SVG DOM等价。

## 2026-10-29阶段235复核更新

- **纵向倍率复核**：复用`bpp_svg_portrait_canvas`的20×60正间距BPP容器，将`stroke_width_multiplier`从默认2.0改为3.25；比较器同时断言倍率变化前后`viewBox`逐字不变。
- **结果**：Rust Debug/Release与JaguarCore MSVC Debug/Release均输出`viewBox=-1.6600003 -3.6600006 23.32 67.32`；container `stroke-width`从`0.084800005`变为`0.13780001`，与Rust参考文本逐字一致。
- **验证与限制**：先缺少参考行时C++ Debug按预期失败，补入Rust Debug/Release输出后两侧Debug/Release差分runner均通过。该结果补齐纵向fixture的默认/自定义倍率对照，不覆盖其它倍率、主题或全SVG DOM等价。

## 2026-10-29阶段236复核更新

- **宽横向fixture**：新增SVG专用`bpp_svg_wide_canvas`输入，60×20简单多边形BPP容器、`min_item_separation=0.5`和正库存bin；经真实Importer导入后对空Layout执行默认SVG导出。
- **结果与长宽比矩阵**：Rust与C++ Debug/Release根`viewBox`均为`-3.2200007 -1.2200003 66.44 22.440002`，container path `stroke-width`均为`0.08160001`。与40×30及20×60样例共同覆盖三个固定长宽比，逐字匹配Rust参考状态。
- **验证与限制**：比较器先因缺少新参考键按预期失败；Rust Debug/Release与MSVC Debug/Release差分runner补齐后通过。该证据仅适用于这三个固定矩形容器样例，不覆盖任意长宽比/非矩形形状或整份SVG DOM。

## 2026-10-29阶段237复核更新

- **斜边凸容器fixture**：新增SVG专用`bpp_svg_oblique_canvas`输入，包围盒为60×20的斜边凸四边形、`min_item_separation=0.5`，经真实BPP Importer导入后对空Layout执行默认SVG导出。
- **结果**：Rust与C++ Debug/Release的`viewBox`均为`-3.2200007 -1.2200003 66.44 22.440002`，container path `stroke-width`均为`0.08160001`，与同包围盒的60×20矩形fixture逐字一致。
- **验证与限制**：缺少参考键时C++ Debug按预期失败；Rust Debug/Release参考行补齐后，Rust Oracle及MSVC Debug/Release差分runner通过。该比较只验证该凸四边形和SVG画布/描边字段，不代表斜边path几何或一般非矩形容器已全量对齐。

## 2026-10-29阶段238复核更新

- **path与样式比较**：在Stage237同一60×20斜边凸四边形BPP容器上提取container path数据和共有`fill`、`stroke`、`stroke-width`字段，逐字比较Rust与C++输出。
- **结果**：Rust Debug/Release与JaguarCore MSVC Debug/Release均得到`d=M0,0 L55,0 L60,20 L5,20 z;fill=#CC824A;stroke=black;stroke-width=0.08160001`，与同包围盒矩形fixture的视口比较相独立。
- **验证与限制**：C++比较器先因缺少path参考键按预期失败；Rust Debug/Release参考行补齐后，两侧Debug/Release差分runner通过。结论仅适用于此固定凸四边形和共有属性，不覆盖洞、多环、凹多边形或完整SVG DOM。

## 2026-10-29阶段239复核更新

- **凹形容器fixture**：新增SVG专用`bpp_svg_concave_canvas`输入，包围盒60×20的L形凹多边形、`min_item_separation=0.5`，经真实BPP Importer导入并导出默认SVG。
- **path与样式**：Rust Debug/Release与JaguarCore MSVC Debug/Release的container状态逐字为`d=M0,0 L60,0 L60,8 L20,8 L20,20 L0,20 z;fill=#CC824A;stroke=black;stroke-width=0.08160001`。
- **验证与限制**：比较器先因缺少凹形path参考键按预期失败；Rust参考行补齐后，两侧Debug/Release差分runner通过。只证明该L形凹容器输出及共有样式，不覆盖一般凹多边形、带洞/多环或整份SVG DOM。

## 2026-10-29阶段240复核更新

- **复用的洞形输入**：使用已有`bpp_container_minsep_hole_quality_zone` BPP输入，20×20 Polygon Bin含一个矩形内环、正间距0.5和quality zone；经真实Importer导入后对空Layout导出默认SVG。
- **container组结果**：Rust Debug/Release与JaguarCore MSVC Debug/Release均只输出1条container path，外环`d=M0,0 L20,0 L20,20 L0,20 z`；共有样式均为`fill=#CC824A`、`stroke=black`、`stroke-width=0.08160001`。该SVG container组没有输出内环洞path。
- **验证与限制**：比较器先因缺少洞形path参考键按预期失败；加入Rust Debug/Release参考行后，两侧Debug/Release差分runner通过。结论只描述此输入的container组，不推及其它洞形、洞的碰撞语义或整份SVG DOM。

## 2026-10-29阶段241复核更新

- **多洞真实Importer fixture**：复用`bpp_svg_multi_hazard_min_quality_3`，其Polygon Bin包含两个洞及多个quality zone；从放置了物品的真实Importer Layout SVG中独立提取container组路径。
- **path数量、顺序与样式**：Rust与C++ Debug/Release的container组均只有1条外环path，状态为`d=M0,0 L40,0 L40,30 L0,30 z;fill=#CC824A;stroke=black;stroke-width=0.1224`；两个内环没有成为该组路径。
- **验证与限制**：C++比较器先因缺少多洞参考键按预期失败；Rust Debug/Release参考行补齐后，Rust Oracle和MSVC Debug/Release差分runner通过。只验证该双洞BPP输入的container组，不推及任意洞数/形状或整份SVG DOM。

## 2026-10-30阶段242复核更新

- **CD显示输入**：复用Stage240单洞`bpp_container_minsep_hole_quality_zone`空Layout，以及Stage241双洞`bpp_svg_multi_hazard_min_quality_3`真实Importer Layout；后者保留已放置物品，两个案例均以`draw_cd_shapes=true`导出。
- **container path与样式**：Rust Debug/Release及JaguarCore MSVC Debug/Release两侧均只输出1条container外环path。单洞path为`M0.25,19.75 L0.25,0.25 L19.75,0.25 L19.75,19.75 z`，样式`fill=#CC824A`、`stroke=black`、`stroke-width=0.08160001`；双洞path为`M0.25,29.75 L0.25,0.25 L39.75,0.25 L39.75,29.75 z`，样式`fill=#CC824A`、`stroke=black`、`stroke-width=0.1224`。Rust Debug/Release文本一致，C++ Debug/Release逐字匹配；内环仍未作为container组path输出。
- **验证与限制**：C++断言先因新Rust参考行缺失按预期失败；补齐pinned Rust 1.98.1 Debug/Release参考后，MSVC Debug/Release `--problem-differential-only`均通过。结论限这两个固定Importer fixture的container组path与共有样式，不覆盖quality zone洞的CD绘制、其它洞数/形状或整份SVG DOM，B8/L4仍部分完成。

## 2026-10-30阶段243复核更新

- **核对对象**：复用Stage242的单洞与双洞fixture，在`draw_cd_shapes=true`下从`quality_zones`组筛出quality 0 path；依次比较`d`、fill/fill-opacity、stroke/stroke-width/stroke-opacity、dasharray、linecap和linejoin。
- **结果**：pinned Rust Debug/Release状态完全一致；单洞fixture有1条quality 0 path，双洞fixture有3条（显式quality 0区及两个Polygon内环）。C++比较器还核对quality标记与title一致，并对路径顺序和完整共有样式逐字对拍；MSVC Debug/Release均通过。
- **验证与限制**：比较器先因缺少Stage243参考行按预期失败；补齐Rust 1.98.1 Debug/Release状态后两配置均通过。结论仅覆盖这两个fixture在CD模式下的quality 0 path，不覆盖其他quality等级、其他洞形或整份SVG DOM，B8/L4仍部分完成。

## 2026-10-30阶段244复核更新

- **默认模式fixture**：复用Stage243相同的单洞/双洞Layout，以`draw_cd_shapes=false`导出`quality_zones`，只筛选quality 0 paths，并核对路径顺序、`d`、fill/fill-opacity、stroke/stroke-width/stroke-opacity、dasharray、linecap和linejoin。
- **路径状态**：Rust Debug/Release一致，单洞样例有1条原始四边形洞path；双洞样例有3条，顺序为显式quality 0区、Polygon第一个内环、Polygon第二个内环，均为原始四边形path。每条path及共有样式与pinned Rust逐字一致；与Stage243的CD模式path状态不同，CD模式使用扩展后的圆角轮廓。
- **验证与限制**：C++差分先因默认quality 0参考行缺失按预期失败；补齐Rust Debug/Release参考行后，MSVC Debug/Release `--problem-differential-only`均通过。证据仅覆盖这两个fixture的quality 0 path默认/CD双模式，不覆盖其它quality等级、洞形或完整SVG DOM，B8/L4仍部分完成。

## 2026-10-30阶段245复核更新

- **核对范围**：复用单洞/多洞fixture，对`quality_zones`中的quality 2与quality 5 path在默认和CD模式分别取path、顺序及fill/fill-opacity、stroke/stroke-width/stroke-opacity、dasharray、linecap和linejoin；比较器继续核验C++ `data-quality`与title一致。
- **Rust状态**：单洞fixture在两种模式均为q2=1、q5=0；多洞fixture均为q2=3、q5=1，保留group内实际序列化顺序（q5先于q2）。两profile输出逐字相同；默认path与CD偏移后的路径不同。
- **C++验证与限制**：比较器先因Stage245参考行缺失按预期失败；补入pinned Rust Debug/Release参考后，MSVC Debug/Release `--problem-differential-only`均通过。结论限这两个fixture的quality 2/5路径子集，不覆盖其它quality等级、洞形或完整SVG DOM，B8/L4仍部分完成。
