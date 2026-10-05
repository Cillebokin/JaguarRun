# 36. SVG 导出逐文件逻辑比对

**Rust**: `CodeResource/jagua-rs/jagua-rs/src/io/svg/layout_to_svg.rs`（487 行）+ `svg_util.rs`（291 行）
**C++**: `JaguarCore/Include/JaguarCore/io/SvgExport.h`（70 行）+ `JaguarCore/Source/IO/SvgExport.cpp`（410 行）
**判定**: 不等价（有意重写、非兼容输出：C++ 做 y-flip 翻转坐标系、viewBox/stroke 公式不同、无 defs/use 复用结构、无可见标签与 quadtree 图层、主题体系不同；eef0830 后 quality_zones 图层与 `qualityZoneFills[10]` 主题已补齐，与 Rust 的差距显著缩小但仍非逐属性兼容）

## 逐条比对（函数/结构体）

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

**0. y-flip / 坐标系**（Rust 无 ↔ C++ cpp:156-157）
- C++ 全场景包裹 `<g id="scene" transform="translate(0 flipTranslation) scale(1 -1)">`，把数学坐标系翻成 SVG 的 y 向下坐标系；`flipTranslation = viewY + viewHeight`（cpp:146）。Rust 直接使用原始坐标（y 向上，SVG 查看器中呈倒置——jagua-rs 有意为之）。**根本性输出差异：同一布局两库的 SVG 互为镜像。**

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
- 判定：一致（`translate({tx} {ty}), rotate({deg})` 格式；C++ 用 `ToDegreesRustF32` 对齐 f32 度数换算）。

### `data_to_path` / `point` / `circle` / `edge_data` / `aa_rect_data`（svg_util.rs:250-291）
- C++ 对应：`circleElement`（cpp:99-112）、`linePath`（cpp:93-97）按需实现。
- 判定：基本等价（元素级助手；Rust 的通用属性数组机制 ↔ C++ 各自内联属性；`aa_rect_data` 标记 dead_code 无对应）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | y-flip 坐标系翻转 | 差异（根本） | C++ 输出与 Rust 互为垂直镜像（C++ 视觉正向） | 无 vs SvgExport.cpp:146,156-157 |
| 2 | viewBox 公式 | 差异 | Rust `bbox.resize_by(1%高).scale(1.1)` vs C++ `margin=5%*max(w,h)`，数值不同 | layout_to_svg.rs:48-55,31 vs SvgExport.cpp:140-145 |
| 3 | stroke_width 公式 | 差异 | `min(w,h)*0.001*mult` vs `max(w,h)*0.0025*mult`，所有线宽不同 | layout_to_svg.rs:59-60 vs SvgExport.cpp:147-148 |
| 4 | defs/use 复用 vs 逐项展开 | 差异 | 输出结构不同；文件大小与 item_{id} 引用语义不同；几何位置一致（同 `T_pre∘T_int` 公式） | layout_to_svg.rs:159-303 vs SvgExport.cpp:172-235 |
| 5 | 可见信息标签（h/w/density/title） | C++ 缺失 | C++ 仅文档级 `<title>` | layout_to_svg.rs:62-82 vs SvgExport.cpp:155 |
| 6 | quadtree 图层 + 开关 | C++ 缺失 | 三色叶节点绘制整体缺失 | layout_to_svg.rs:309-345, svg_util.rs:193-248 vs 无 |
| 7 | highlight_cd_shapes / draw_cd_shapes 开关与图层 | C++ 缺失（部分承担） | C++ 恒绘碰撞形双 path，无开关、无顶点圆点 | layout_to_svg.rs:84-95,246-268 vs SvgExport.cpp:165-170,203-208 |
| 8 | quality_zones 图层 | 一致（eef0830 补齐） | 降序、配色、dasharray、title 双侧对齐；C++ 画内部形（zone 原始形未保留，记录 24） | layout_to_svg.rs:123-154 vs SvgExport.cpp:237-267 |
| 9 | 主题 EARTH_TONES 对应值 | 一致（eef0830 补齐） | 十色 qz_fill、container/item fill、qz_stroke_opac、multiplier=2.0 逐值相同 | svg_util.rs:61-80 vs SvgExport.h:28-48 |
| 10 | min_quality 物品混色（blend_colors） | C++ 缺失 | C++ 恒 itemFill | layout_to_svg.rs:162-165 vs SvgExport.cpp:196-201 |
| 11 | GRAY 主题、hole_fill、Color serde | C++ 缺失 | 主题不可配置序列化 | svg_util.rs:82-101,48,154-171 vs 无 |
| 12 | collision 高亮（连线/圆点/zone 跳过规则） | 基本等价 | 样式常量一致；去重机制 hash vs handle（画线方向不同，无视觉影响）；C++ 需重建碰撞引擎 | layout_to_svg.rs:348-453 vs SvgExport.cpp:269-384 |
| 13 | axes 图层、collision-debug 组、data-* 属性、xmlEscape、options 校验 | C++ 额外 | Rust 无对应输出/校验 | SvgExport.cpp:114-118,131-138,386-389,392-405 |
| 14 | surrogate 图层 | 基本等价 | 几何一致；Rust POI/ff/非 ff 三级样式 vs C++ 统一样式 | layout_to_svg.rs:192-243 vs SvgExport.cpp:210-233 |
| 15 | 返回类型 Document vs Result\<string\> | 基本等价 | panic→Result 系；C++ 额外 bounds/主题校验 | layout_to_svg.rs:28 vs SvgExport.cpp:121-129 |

## 结论

C++ 的 SVG 导出是一次有意的重写而非逐属性移植：y-flip、viewBox/stroke 公式、逐项展开（无 defs/use）共同决定了输出与 Rust 不兼容（同一布局渲染为镜像、尺寸参数不同）。功能覆盖上，eef0830 补齐 quality_zones 图层与十色主题后，容器/物品/质量区/代理/碰撞高亮五类核心图层均已存在且样式常量大量对齐；仍缺 quadtree 图层、可见信息标签、`draw_cd_shapes`/`highlight_cd_shapes` 开关、min_quality 混色与 GRAY 主题，另新增 axes 与调试元数据为 C++ 特有。

### 补充审计（2026-09-29）

对 layout_to_svg.rs（487 行）+ svg_util.rs（291 行）逐行复核（SvgExport.cpp 自 eef0830 后无改动，git log 确认最后修改为 2026-09-28 eef0830；quality_zones 图层与 `qualityZoneFills[10]` 主题已在位，原记录已反映）。重点核对任务关注的"功能性"遗漏：

1. **`blend_colors` 的功能性影响再确认（原记录差异 10 判定"C++ 缺失"成立，补充量化）**——Rust 对 `min_quality = Some(q)` 的物品填充 `blend_colors(item_fill, qz_fill[q])`（layout_to_svg.rs:162-165，各分量 `(c1+c2)/2` 后 `as u8` 截断）。C++ 恒 `itemFill`（SvgExport.cpp:196-201）。**这是视觉语义差异而非纯样式**：Rust 的带质量要求物品颜色随 q 变化（q=1 → FF6400 系混合色、q=9 → E56300 系混合色），C++ 全部同色——用户无法从 C++ SVG 区分"有质量要求的物品"。判定维持：C++ 缺失（功能性视觉信息丢失）。
2. **`DefaultHasher` 去重的功能性等价（原记录差异 12 已覆盖，补充机制细节）**——Rust `haz_hash < pi_hash` 用 `DefaultHasher`（SipHash-1-3，随机种子）对 `HazardEntity` 哈希（layout_to_svg.rs:375-386）：**每对碰撞去重的选择侧是随机的**（哈希序不定）；C++ 用 handle 的 `(index, generation)` 字典序（cpp:313-317）固定选 handle 大的一侧。效果同为每对一条线；方向随机 vs 固定。无视觉影响（线无箭头）。判定：基本等价（去重目标一致；确定性 C++ 更优）。
3. **`halfBrightness` 对 `change_brightness(color, 0.5)` 的覆盖（原记录已比对，补充调用点差异）**——Rust 唯一调用点是 quality zone 描边（layout_to_svg.rs:129）；C++ 同（cpp:250）。Rust 的通用 fraction 参数（0.5 以外的值）在库内无其他调用点，C++ 固定 0.5 **无功能损失**。判定维持一致。
4. **主题预设 `GRAY`（svg_util.rs:82-101）功能性评估（原记录差异 11 判"C++ 缺失"成立，补充影响面）**——GRAY 是库内第二预设（`stroke_width_multiplier=2.5`、灰阶配色、`qz_stroke_opac=0.9`），Rust 调用方可通过 `SvgDrawOptions::theme` 选择；C++ `SvgLayoutTheme` 无命名预设但**字段全部可配置**（值初始化默认即 EARTH_TONES 等价值）——调用方可手工填出 GRAY 等价主题。判定细化：**预设常量缺失、能力不缺失**（主题结构体可表达）。
5. **`hole_fill`（svg_util.rs:48,65,86）的"死字段"确认（原记录判"C++ 缺失"，修正影响评估）**——核实 Rust 侧 `hole_fill` 在 layout_to_svg.rs 中**零调用**（holes 以 quality 0 zone 身份绘制，用 `qz_fill[0]` 黑色，不用 hole_fill）。即 hole_fill 是**未使用的主题字段**（serde 可配置但渲染不用）。C++ 不实现**无功能损失**。判定修正：C++ 缺失（字段级）但无行为影响（Rust 死字段）。
6. **quality_zones 绘制顺序边界（补齐）**——Rust `container.quality_zones.iter().rev().flatten()`（layout_to_svg.rs:127）：zones 向量按质量升序存储（import 时 0..N 顺序 push），`.rev()` 从最高质量画起（后画的在下层——SVG 后序元素在上层，即**低质量（q=0 黑）最后画、显示在最上层**）。C++ `for (quality = fills.size(); quality-- > 0;)`（cpp:238-239）从 9 到 0，同序。一致（补齐"高→低"方向证据）。
7. **items defs/use 的 `int_transf` 语义（原记录第 5/6 层已覆盖，补齐一个易错点）**——Rust `int_transf = draw_cd_shapes ? identity : pre_transform.compose().inverse()`（layout_to_svg.rs:184-190）：非 CD 模式下 item def 中的 path 是**原始形**、需先逆中心化才能配合 Use 的**外部**变换；surrogate def 中的 pole 坐标是 CD 域的、同样用 `int_transf` 逆中心化。C++ 无 defs 结构，path 直接用 `polygonPath(shape, internalTransform)`（cpp:203-208）以内部变换放置碰撞形。**最终几何位置双侧一致**（原记录 79 行已分析），此处补充：Rust 的 `poi_style` 黑色 POI 圆（fill-opacity 0.1）与 ff/非 ff 三级样式在 C++ 全部统一为 `surrogateStroke` 空心圆（cpp:99-112,218-224）——**POI（首位 pole）在 C++ 中无任何视觉区分**，调试时可辨识性下降。判定：差异（功能性调试信息丢失，非纯样式——Rust 的 POI 标记用于人工核对 pole-of-inaccessibility 位置）。
8. **`transform_to_svg` 数值格式（补齐）**——Rust `format!("translate({tx} {ty}), rotate({r})")`：`{tx}` 默认 Display 格式（f32 最短表示）；C++ `number()` `setprecision(17)`（cpp:17-22）。数值精度不同（双侧解析后坐标一致，字符串形态不同）。属既有"数值格式"差异族（原记录 simple_polygon_data 条已提）。
9. **`SvgDrawOptions` 的 serde 序列化（原记录差异 11 已提，补充用途评估）**——Rust `SvgDrawOptions`/`SvgLayoutTheme` 可 `Serialize/Deserialize`（svg_util.rs:13,43，含 `#[serde(default)]`）：用于从 JSON 配置驱动 SVG 渲染选项。C++ `SvgExportOptions` 为纯 POD 无序列化。若调用方生态（sparrow）用 JSON 传渲染配置，C++ 需自行实现。核实 sparrow 侧未用该序列化路径（仅代码内构造）。判定维持：C++ 缺失（能力级，当前无使用方）。
10. **`quad_tree_data` 的 filter 参数（遗漏的细节）**——Rust `quad_tree_data(&layout.cde().quadtree, &NoFilter)`（layout_to_svg.rs:310）传 `NoFilter`（全量统计）；C++ 整层缺失（原记录差异 6）。无修正。
11. **`aa_rect_data`（svg_util.rs:283-291，`#[allow(dead_code)]`）**——Rust 自标注死代码；C++ 无对应。一致（双侧均无行为）。
12. **C++ `polygonPath` 的空多边形守卫（补齐）**——`if (vertices.empty()) return {}`（cpp:71-72）：空 path `d=""` 输出。Rust `simple_polygon_data` 对空多边形 `vertex(0)` 会 panic（索引越界）。C++ 更防御（Rust 侧空多边形在导入时已被 `SPolygon::new` 拒绝，不可达）。判定：C++ 额外防御，正常路径无差异。

补充差异汇总：

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| S1 | min_quality 物品混色（功能性） | C++ 缺失 | 带质量要求物品与普通物品在 C++ SVG 中不可视觉区分 | layout_to_svg.rs:162-165 vs SvgExport.cpp:196-201 |
| S2 | POI 视觉标记（黑圆/ff 分级） | C++ 缺失（调试信息） | POI 位置不可人工核对；ff/非 ff pole 不可区分 | layout_to_svg.rs:195-216 vs SvgExport.cpp:99-112,218-224 |
| S3 | hole_fill 为 Rust 死字段 | 修正影响评估 | 渲染不用（holes 走 qz_fill[0]），C++ 不实现无功能损失 | svg_util.rs:48 + layout_to_svg.rs 全文零调用 |
| S4 | GRAY 预设 | 字段级缺失、能力不缺失 | 主题结构体可手工表达 | svg_util.rs:82-101 vs SvgExport.h:25-49 |
| S5 | DefaultHasher 去重随机性 | 基本等价（C++ 确定） | 每对一条线；选侧随机 vs handle 字典序 | layout_to_svg.rs:375-386 vs SvgExport.cpp:313-317 |
| S6 | 空多边形 path | C++ 额外防御 | `d=""` vs Rust panic（不可达路径） | svg_util.rs:185-191 vs SvgExport.cpp:71-72 |
| S7 | 变换串数值格式 | 差异（字符串形态） | f32 Display vs setprecision(17) | layout_to_svg.rs:486 vs SvgExport.cpp:17-22,85-91 |
