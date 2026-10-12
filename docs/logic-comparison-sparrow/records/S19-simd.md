# S19. quantify/simd（circles SoA + SIMD 量化通道） 逐文件逻辑比对

**Rust**: `CodeResource/sparrow/src/quantify/simd/mod.rs`（46 行）+ `simd/circles_soa.rs`（38 行）+ `simd/overlap_proxy_simd.rs`（110 行），另涉及启用侧 `src/eval/collision_loss.rs` 的 `#[cfg(feature = "simd")]` 分支（74-88 行）与 `src/lib.rs:1`（`feature(portable_simd)`）
**C++**: SparrowCore 侧**无独立的量化 SoA/SIMD 通道**（`SparrowCore/Include|Source` 全树无 Soa/Simd/Avx 文件，grep 验证为空）；量化走 `Source/Quantify/OverlapProxy.cpp` 的标量双循环。几何侧的 SoA/SIMD 在 JaguarCore：`JaguarCore/Source/Collision/SoaGeometry.h`（77 行）+ `SoaGeometry.cpp`（235 行）+ `SoaGeometryAvx2.cpp`（52 行）+ `SoaGeometryAvx2Fallback.cpp`（11 行），服务于 `CollisionEngine` 的精确碰撞检测，与 quantify 的 pole 代理计算是两套互不相交的数据通路。
**判定**: 默认路径结果对应/实现不同（Rust SIMD 是 opt-in、默认关闭；SIMD reduce 与标量累加顺序不同，源码 debug_assert 只要求相对误差在 1e-3 内；C++ 量化恒走标量。Rust SIMD 最大 ULP 差异未测量，有界提前退出变体在 C++ 无对应）

---

## 背景核对（先确认 Rust 侧 SIMD 的启用状态）

- `simd` 是 Cargo.toml:49 的 **opt-in feature**（`simd = []`），`default` 不含它（Cargo.toml `[features]` 段无 default 项）。`lib.rs:1` `#![cfg_attr(feature = "simd", feature(portable_simd))]` 才打开 nightly 的 `std::simd`。
- 启用点仅两处：`eval/collision_loss.rs:2-7`（`quantify_collision_poly_poly` 与 SIMD 版二选一）与 `simd/mod.rs` 自身。`bench.rs`、`tests/tests.rs` 均无 simd 引用（grep 验证空）。故 **Rust 默认构建走标量路径**，SIMD 是可选加速。
- C++ 侧：SparrowCore 全树 grep `Soa|SOA|Simd|SIMD|Avx|AVX` 无命中（任务背景预期成立）；JaguarCore 的命中全部位于 `Source/Collision/`（SoaGeometry*、CollisionEngine）与构建/测试文件。即 C++ 的 quantify 没有任何向量化版本。

## 函数逐条比对（以 Rust 为纲）

### `CirclesSoA` 结构 + `new`（circles_soa.rs:6-21）
- Rust：`#[repr(align(32))]` 令 `CirclesSoA` 结构体类型按 32 字节对齐，含三个 `Vec<f32>`（x/y/r）SoA 字段。当前 SIMD 主循环宽度是 `SIMD_WIDTH=4`（`Simd<f32, 4>`，overlap_proxy_simd.rs:11-15），量化代码没有 AVX intrinsic；该结构体对齐属性本身也不能证明三个 `Vec` 的堆缓冲区按 32 字节对齐。原记录将其解释为 8-lane AVX 载入准备没有源码依据，现予更正。
- C++ 对应：**无**。量化侧不存在 pole 的 SoA 结构——`Surrogate::Poles()` 返回 `vector<Circle2d>`（AoS，double 存储，Surrogate.h:60,85），`CalculateOverlapProxy` 直接嵌套遍历（OverlapProxy.cpp:115-117）。
- 判定：【C++缺失】（数据结构层面；标量路径无需它）
- 证据：circles_soa.rs:6-21 vs OverlapProxy.cpp:115-117、Surrogate.h:60。

### `CirclesSoA::load`（circles_soa.rs:22-37）
- Rust：resize 三个数组后 `izip!` 拷入 x/y/r；调用方 `collision_loss.rs:41` 每次 `reload` 时载入**候选形状（moving item）的 poles**（注意方向：`poles_soa.load(&_shape.surrogate().poles)`，_shape 是被评估的候选；p2 即它，见下条）。
- C++ 对应：无（标量路径无需预拷贝）。
- 判定：【C++缺失】
- 证据：circles_soa.rs:22-37、collision_loss.rs:37-42 vs OverlapProxy.cpp:115-117。

### `poles_overlap_area_proxy_simd`（overlap_proxy_simd.rs:19-22）
- Rust：无界版薄封装（`max_unscaled_overlap = INFINITY` 后 unwrap）。C++ 无对应。【C++缺失】
- 证据：overlap_proxy_simd.rs:19-22。

### `poles_overlap_area_proxy_simd_bounded`（overlap_proxy_simd.rs:24-110）— 核心循环
- Rust 逻辑：对 sp1 每个 pole：
  - SIMD 块（`SIMD_WIDTH=4`，f32xN）：`pd = r1+r2 - sqrt(dx²+dy²)`；`pd >= eps` 掩码选择 `pd` 或 `eps²/(2eps-pd)`；`min_r = simd_min(r1,r2)`；`reduce_sum` 累加（:50-73）。
  - 标量尾巴：逐 pole 与标量版公式完全一致（:75-92）。
  - **有界提前退出**：每个 p1 处理完后检查 `total_overlap > max_unscaled_overlap` 则返回 None（:94-96）。
  - 最终 `*= PI`；debug_assert 与标量 `overlap_area_proxy` 相对误差 1e-3 内一致（:99-109）。
- C++ 对应：`CalculateOverlapProxy`（OverlapProxy.cpp:108-135）——**纯标量双循环**，无向量化、无块/尾巴划分、无有界退出。逐公式比对（pd/decay/min_r/PI）已在 S17 记录为逐字一致。
- 数值等价性分析：
  1. 块内各 lane 执行相同公式，但 `reduce_sum` 的 4-lane 树状归约与标量顺序累加的结合顺序不同，可能产生浮点差异。Rust debug_assert 使用约 1e-3 相对误差阈值；这不是 ULP 上界，当前没有测量最大 ULP 偏差。
  2. 尾巴处理与标量版一致。
  3. 因此只能得出 SIMD 版在该内部断言容差内近似标量版；没有证据给出固定 ULP 上界。C++ 标量实现与 Rust 默认标量路径按 S17 的有限 fixtures/容差证据核对，不能据此推导 C++ 与 Rust SIMD 逐位或 ULP 等价。
  4. 有界退出（early-return None）是**性能/控制流**优化：`add` 在 `collision_loss.rs:51-53` 收到 None 时返回 true 提前终止 hazard 遍历——影响的是评估耗时，不是最终损失值（上界比较逻辑在 S11/S12 已录：C++ 在 SeparationEvaluator::Evaluate 事后比较 cpp:188-193）。
- 判定：【差异】（C++ 无 SIMD/有界变体；Rust 内部只验证约 1e-3 相对误差，最大 ULP 偏差未测，不能声称 ULP 级等价；控制流为 Rust opt-in 路径）
- 证据：overlap_proxy_simd.rs:24-110 vs OverlapProxy.cpp:108-135；collision_loss.rs:44-56。

### `quantify_collision_poly_poly_simd`（simd/mod.rs:13-16）
- Rust：无界薄封装。C++ 无对应（标量 `CalculatePolygonCollisionLoss` 即最终路径）。【C++缺失】（入口封装层面）
- 证据：simd/mod.rs:13-16。

### `quantify_collision_poly_poly_simd_bounded`（simd/mod.rs:18-46）
- Rust：计算 `epsilon = max(d1,d2)*0.01`、`penalty`、由 `max_loss` 反推 `max_unscaled_overlap = (max_loss/penalty)² - eps²) / PI`（负则 None，:28-35），调 SIMD bounded 版再加 `eps²`、`sqrt * penalty`（:37-45）。公式与标量 `quantify_collision_poly_poly`（mod.rs:15-25）同构（+eps²、sqrt、penalty），仅多上界。
- C++ 对应：`CalculatePolygonCollisionLoss`（OverlapProxy.cpp:137-183）同公式无上界（见 S17）。**上界换算公式 C++ 无对应**——对应功能（评估短路）由 SeparationEvaluator 层的 upperBound 事后比较承担（S11 差异#1/S12 已录）。
- 判定：【差异】（有界换算与提前退出无 C++ 对应；最终值等价）
- 证据：simd/mod.rs:18-46 vs OverlapProxy.cpp:137-183、SeparationEvaluator.cpp:182-200。

### 与 JaguarCore 侧 SoA/AVX2 的关系（任务指定核对项）
- **两套 SoA 互不相交**：
  1. Rust `CirclesSoA`（quantify）：f32 的**圆(pole)数组** x/y/r，用于**损失量化**（overlap proxy 数值计算），仅 `feature="simd"` 时存在。
  2. C++ JaguarCore `PolygonSoA`（SoaGeometry.h:23-77）：**多边形边**的 SoA（m_StartX/StartY/EndX/EndY/EdgeMinX/MaxX/MinY/MaxY，全 **double**），用于**精确碰撞检测**（`intersectsBoundary`/`Contains`/`Intersects`，服务于 CollisionEngine 的 `CollectPolygonCollisionsUntil`），带 `SoaExecutionMode`（Scalar/Auto/Avx2）运行时选择（SoaGeometry.h:21-30）与 AVX2 探测（avx2Supported）。
- AVX2 实现（SoaGeometryAvx2.cpp:5-52）：`__m256d`（4×double）对边包围盒做 batch 相交预筛，命中 lane 再做**标量精确边相交**（:28-34），尾巴标量（:37-51）——"向量预筛 + 标量精算"模式；Rust quantify SIMD 是"整条公式向量化"。二者结构不同、服务目标不同。
- 对接点：C++ tracker 的 `BuildFromLayout`/`EvaluateCandidateLoss(layout,...)` 调 JaguarCore `CollectPolygonCollisionsUntil`（CollisionTracker.cpp:833-837,660-664）→ 该引擎内部经 PolygonSoA/AVX2 做**检测**；检测命中的 hazard 再由 SparrowCore `CalculatePolygonCollisionLoss` 做**标量量化**。即：C++ 把 Rust 中"检测（jagua-rs CDE）+ 量化（标量或 SIMD）"的两段中，向量化的加速度放在了检测段（几何侧 AVX2），量化段恒为标量。Rust 的 simd feature 恰相反（检测段为 jagua-rs 标量/其内部结构，量化段可 SIMD）。
- 数值上：PolygonSoA 的 AVX2 预筛只影响**是否命中**的判定速度与顺序（预筛逻辑与标量等价：比较条件一致），量化数值不受影响；故两套 SoA 的存在不破坏 quantify 层的数值等价性。
- 判定：【差异】（架构分工不同；结果等价）
- 证据：SoaGeometry.h:23-77、SoaGeometryAvx2.cpp:5-52、CollisionTracker.cpp:833-837 vs circles_soa.rs:6-38、overlap_proxy_simd.rs:24-110。

### 精度路径（任务指定核对项）
- Rust SIMD：全程 f32（`Simd<f32,4>`、`Vec<f32>`）。C++ 量化标量路径：存储 double（Circle2d）→ cast float 运算（S17 差异#9 已录 RustF32 双轨）。两者运算精度同为 f32，无双精度分歧。
- JaguarCore SoaGeometry 侧为 **double**（`__m256d`、`vector<double>`），但那是几何检测精度（与 jagua-rs 检测侧的对照属 JaguarCore↔jagua-rs 比对范围），不进 quantify 数值链。
- 判定：【一致】（quantify 数值链两侧均 f32 运算）
- 证据：circles_soa.rs:9-11、overlap_proxy_simd.rs:12-15 vs OverlapProxy.cpp:42-50、SoaGeometry.h:68-75。

---

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | CirclesSoA（f32 x/y/r SoA + load）及 SIMD 量化通道在 C++ 无对应物——C++ quantify 恒走标量双循环 | 【C++缺失】 | Rust simd 为 opt-in、默认构建走标量；SIMD debug_assert 只要求相对误差约 1e-3，最大 ULP 差异未测 | Cargo.toml:49、lib.rs:1、circles_soa.rs:6-37 vs OverlapProxy.cpp:108-135 |
| 2 | 有界变体（`*_simd_bounded` 的 max_unscaled_overlap 反推 + 每 pole 提前退出 None）在 C++ 无对应 | 【差异】 | 短路是性能优化；C++ 以 SeparationEvaluator 的 upperBound 事后比较等价实现（S11/S12 已录） | simd/mod.rs:28-43、overlap_proxy_simd.rs:94-96、collision_loss.rs:51-53 vs SeparationEvaluator.cpp:194-199 |
| 3 | SIMD 块 `reduce_sum` 树状求和 vs 标量顺序累加 | 【差异】 | 归约顺序不同；Rust debug_assert 的相对误差阈值约 1e-3，未建立 ULP 上界 | overlap_proxy_simd.rs:72,99-106 vs OverlapProxy.cpp:114-131 |
| 4 | SIMD 标量尾巴公式与标量版/C++ 逐字一致 | 【一致】 | — | overlap_proxy_simd.rs:75-92 vs overlap_proxy.rs:13-21 |
| 5 | `quantify_collision_poly_poly_simd_bounded` 的 epsilon/penalty/+eps²/sqrt 公式与标量版同构，C++ CalculatePolygonCollisionLoss 同公式（S17 已录） | 【一致】 | — | simd/mod.rs:25,43-45 vs OverlapProxy.cpp:147-177 |
| 6 | JaguarCore PolygonSoA/AVX2（double、边 SoA、向量预筛+标量精算、SoaExecutionMode 运行时切换）与 Rust CirclesSoA（f32、pole SoA、公式向量化）是两套不相交结构 | 【差异】 | C++ 把向量化放在检测段（CollisionEngine→CollectPolygonCollisionsUntil 内部），量化段标量；Rust simd feature 反之。检测结果等价，量化数值不受影响 | SoaGeometry.h:23-77、SoaGeometryAvx2.cpp:5-52、CollisionTracker.cpp:833-837 vs circles_soa.rs:6-38 |
| 7 | fixtures 覆盖 Rust 默认标量路径；C++ 标量路径用对应 reference/regression 数据核对 | 【证据范围】 | 浮点 loss/weight 用容差；Rust SIMD feature 没有 parity fixture，C++ 自参考也不证明跨语言 parity | fixtures 目录 + Cargo.toml features vs OverlapProxy.cpp |
| 8 | `CirclesSoA` 类型的 32 字节对齐属性被旧记录误释为 8-lane AVX 用途 | 【文档更正】 | 源码只证明结构体类型对齐；活动 SIMD 宽度为 4 lanes，未证明 Vec 堆缓冲区对齐或 AVX 用途 | circles_soa.rs:6-12; overlap_proxy_simd.rs:11-15 |

## 结论

Rust 的 quantify SIMD 通道（CirclesSoA + portable_simd + 有界提前退出）默认关闭；C++ 量化恒走标量。SIMD 内部 debug_assert 仅给出约 1e-3 相对误差容差，最大 ULP 差异未测量，因此只能把默认标量路径作为当前移植 parity 基线，不能宣称 SIMD 数值等价。JaguarCore 的 PolygonSoA/AVX2 服务几何碰撞检测，与 quantify pole SIMD 是不同数据通路。若未来在 C++ 复刻量化 SIMD，还需保留/重新设计有界退出；SeparationEvaluator 的事后 upperBound 比较覆盖的是决策层，未复现 pole 循环的早停性能。

### 补充审计（2026-09-29）
1. **【核对确认：Rust 侧文件与行数】** simd/mod.rs 46 行（2 入口封装 + bounded 主体）、circles_soa.rs 38 行、overlap_proxy_simd.rs 110 行——三个文件的每个函数（new/load、poles_overlap_area_proxy_simd、poles_overlap_area_proxy_simd_bounded、quantify_collision_poly_poly_simd、quantify_collision_poly_poly_simd_bounded）主文均有条目，无遗漏。
2. **【浅覆盖补全：SIMD_WIDTH=4 与 tail 处理的边界】** 主文记了块宽 4 与标量尾巴；补记细节：`chunks = p2.x.len() / SIMD_WIDTH`（整块数）、尾巴从 `remaining_idx = chunks*SIMD_WIDTH` 起逐 pole 标量处理（overlap_proxy_simd.rs:53-92），且**有界检查在每个 p1 的块+尾巴全部完成后**（:94-96）而非块间——即上界退出粒度是"每 pole1 行"。C++ 无对应物判定不变。
3. **【浅覆盖补全：bounded 上界换算公式方向】** simd/mod.rs:28-35 `max_unscaled_overlap = (max_loss/penalty)² - eps²) / PI`（loss = sqrt(overlap_proxy)*penalty 且 overlap_proxy 含 +eps²，故逆推先减 eps² 再除 π）；`overlap < 0 → None`（上界小于 eps² 的最小可表达 loss 时直接放弃）。该换算与 S12 补充审计第 1 条的 `max_loss/weight` 预除界是两层不同的界（外层权重界→内层未加权重叠界）。C++ 无对应，判定不变。
4. **【核对确认：C++ 侧无 SoA/Simd/Avx 命中】** 现场重验 SparrowCore 全树 grep 无命中（量化侧）；JaguarCore 侧 SoaGeometry* 仍在 Collision/ 目录服务检测，主文分工判定维持。
