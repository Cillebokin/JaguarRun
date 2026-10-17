# 17. hazard-filter 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/collision_detection/hazards/filter.rs`（77 行）
**C++**: `JaguarCore/Include/JaguarCore/Collision/HazardFilter.h`（39 行）
**判定**: 核心过滤 callback 结构基本对应；`from_irrelevant_qzones` 在 JaguarCore 无独立构造器。项目按 D5 以 pinned `f5e2b5f` 为行为目标，维护版语义差异见 B4b/R8

## 函数逐条比对

### `trait HazardFilter::is_irrelevant(&self, hkey)`（filter.rs:7-9）
- C++ 对应：`HazardFilter::IsIrrelevant` 两个重载（HazardFilter.h:16-27）
- 判定：结果等价/实现不同
- 证据与说明：Rust 用 trait（静态分发，查询时传 `&impl HazardFilter`）；C++ 用运行时 `std::function`：
  ```cpp
  // HazardFilter.h:12-27
  std::function<bool(const HazardEntity&)> irrelevant;                     // 按实体过滤
  std::function<bool(HazardHandle, const HazardEntity&)> irrelevantHandle; // 按句柄过滤
  inline bool IsIrrelevant(const HazardEntity& entity) const { return irrelevant && irrelevant(entity); }
  inline bool IsIrrelevant(HazardHandle handle, const HazardEntity& entity) const
  {
      if (irrelevantHandle && irrelevantHandle(handle, entity)) return true;
      return IsIrrelevant(entity);
  }
  ```
  差异点：
  1. Rust 的 key 是 `HazKey`（身份）；C++ 的 handle 重载提供身份过滤、entity 重载提供"按实体内容"过滤——Rust 的 `is_irrelevant` 只按 key。C++ 单句柄过滤（Layout.cpp:213-216，见下）即 Rust `impl HazardFilter for HazKey` 的等价物。
  2. Rust 判定"不相关"恒基于 `HazKey`；C++ `IsIrrelevant(handle, entity)` 先查 handle 通道再回落 entity 通道，语义是并集。正常调用链（Layout 的自身过滤）只设置 `irrelevantHandle`，与 Rust 一致。

### `HazKeyFilter` + `from_keys`（filter.rs:11-18）
- C++ 对应：无专门类型；等价物为调用点手工填 lambda（如 `irrelevant = [](entity){...}`）
- 判定：C++ 缺失（作为独立构造器；语义可由 lambda 达成）
- 证据与说明：Rust 从一组 `HazKey` 构造 SecondaryMap 集合式过滤器。C++ 无 key 集合过滤器类型；`CollectPolygonCollisionsUntil` 等查询按需接受任意 `HazardFilter`，调用方可用捕获 vector 的 lambda 复现。行为可复现，缺便捷 API。

### `HazKeyFilter::from_irrelevant_qzones`（filter.rs:20-43）
- C++ 对应：无（全仓无对应构造器；`grep from_irrelevant_qzones` 在 jagua-rs src 内也无调用点，属库公共 API）
- 判定：C++ 缺失
- 证据与说明：Rust 规则：
  ```rust
  // filter.rs:29-39
  hazard_entity: HazardEntity::InferiorQualityZone { quality, .. }
      if quality < required_quality => Some((hkey, ())),  // 只把 quality 低于 required 的低质区标记为 irrelevant
  ```
  即：pinned `f5e2b5f` 把 `quality < required_quality` 的 InferiorQualityZone 标为 irrelevant（过滤掉、视为不参与碰撞），`quality >= required_quality` 的仍为障碍。该谓词与 `container.rs:86` 的“质量编号越大质量越好”、`min_quality` 最低要求及维护版 `QualityZoneFilter` 相反；按 D5，本项目保留锁定谓词做行为对齐，并将意图语义差异登记为 B4b/R8。JaguarCore 引擎本身只有通用过滤 callback，没有专用质量比较器；SparrowCore optimizer 的四处 callback 则都复现 pinned 的 `<` 谓词。

### `impl HazardFilter for HazKey`（filter.rs:51-56）
- C++ 对应：`Layout::IsFeasible` 中手工构造（Layout.cpp:211-216）
- 判定：一致
- 证据与说明：
  ```cpp
  // Layout.cpp:211-216
  HazardFilter filter;
  filter.irrelevantHandle = [ownHazard](HazardHandle handle, const HazardEntity&) {
      return handle == ownHazard;
  };
  ```
  与 Rust `*self == hk`（自身 hazard 不参与自身碰撞检测）语义一致（Rust 侧 layout 的等价调用见 entities/layout.rs 中 `detect_poly_collision(..., &pk_filter)` 一类用法）。

### `NoFilter`（filter.rs:58-66）
- C++ 对应：`struct NoFilter : HazardFilter`（HazardFilter.h:31-38）
- 判定：一致
- 证据与说明：Rust `is_irrelevant` 恒 false；C++ 构造时 `irrelevant = [](const HazardEntity&) { return false; }`。注意形态差异：C++ NoFilter 是子类（is-a，默认参数用 `NoFilter{}` 传值切片为基类，见 CollisionEngine.h:81、86），Rust 是独立类型走泛型。行为等价。

### `impl<T: HazardCollector> HazardFilter for T`（filter.rs:68-77）
- C++ 对应：无 blanket 实现；等价语义由查询内部显式 `collector.Contains(handle)` 承担
- 判定：结果等价/实现不同
- 证据与说明：Rust：collector 中已含的 hazard 视为 irrelevant（避免重复检测）。C++ 在 `CollectPolygonCollisionsUntil`/`CollectSurrogateCollisionsUntil` 里显式检查 `collector.Contains(handle)`（CollisionEngine.cpp:354、389、538、554），把"已收集即跳过"并入查询循环而不是让 collector 兼任 filter。**注意语义合流点**：Rust `collect_collisions_until`（qt_node.rs:194）同样有 `!collector.contains_key(hz.hkey)` 显式检查，两者在该路径上等价；差别只在 Rust 的 trait 让 collector 对象还能被直接当 filter 传给 `collides` 类查询，C++ 无此通道（现有调用点不需要）。

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | trait 静态分发 vs struct+std::function 运行时分发 | 结果等价/实现不同 | C++ 有间接调用开销；`irrelevantHandle`/`irrelevant` 双通道为 Rust 无的细分 | filter.rs:7-9 vs HazardFilter.h:12-27 |
| 2 | `from_irrelevant_qzones`（pinned `quality < required` 区域被过滤） | JaguarCore 无专用质量比较器；SparrowCore callback 有对应谓词 | 按 D5 保留 pinned 行为；与维护版意图契约相反的差异登记为 B4b/R8 | filter.rs:20-43；SparrowCore callbacks 与 B4b 记录 |
| 3 | `HazKeyFilter::from_keys` 集合式过滤器 | C++ 缺失（可用 lambda 复现） | 便捷性缺口 | filter.rs:11-18 |
| 4 | `impl HazardFilter for HazKey` | 一致（Layout.cpp 内联复现） | — | filter.rs:51-56 vs Layout.cpp:211-216 |
| 5 | `NoFilter` | 一致 | — | filter.rs:58-66 vs HazardFilter.h:31-38 |
| 6 | collector 兼任 filter 的 blanket impl | 结果等价/实现不同 | "已收集即跳过"改由查询内 `collector.Contains` 显式承担 | filter.rs:68-77 vs CollisionEngine.cpp:354、389 |

## 结论
过滤语义（忽略哪些 hazard）双侧一致，"检测自身 hazard 时排除自身"（Layout::IsFeasible）与 NoFilter 均逐点对上。形态差异为 trait→std::function、以及 `from_irrelevant_qzones` 质量区过滤器在 C++ 无对应构造器——后者是真实的公共能力缺口，但 jagua-rs 自身 src 内也未见调用，当前不产生行为分叉。

## 补充审计（2026-09-29，逐行复核）

逐行清点 filter.rs（77 行）全部符号：trait HazardFilter::is_irrelevant、HazKeyFilter + from_keys + from_irrelevant_qzones、impl HazardFilter for HazKeyFilter、impl HazardFilter for HazKey、NoFilter、blanket impl<T: HazardCollector>。正文已全覆盖，无【遗漏】函数。补充如下：

### 补-1【验证+行号更新】`irrelevant` entity 通道的实际使用点（新增证据）
正文称"正常调用链（Layout 的自身过滤）只设置 irrelevantHandle"。逐行复核确认当前全仓 HazardFilter 的构造点：
- Layout.cpp:211-216（IsFeasible）：`irrelevantHandle = [ownHazard](h){ return h == ownHazard; }` —— 对应 Rust `impl HazardFilter for HazKey`。
- CollisionEngine.cpp:456、564（CollectPolygonCollisions 无 filter 重载 / CollectSurrogateCollisionsUntil 无 filter 重载）：`NoFilter{}`。
- 测试：TestCollision.cpp:348-351（ignorePlaced 按 kind 过滤）、448-451（exteriorFilter）、678-683（exteriorHandleFilter）——entity 通道与 handle 通道的并集语义（IsIrrelevant(handle,entity) 先 handle 后 entity，HazardFilter.h:22-27）在测试中被显式锁定。
判定：与正文一致，无变化。

### 补-2【浅覆盖→补充】2026-09-29 重写后 filter 在 collect 路径的语义面变化
重写后 C++ collect 路径（CollectEdgeCollisionsUntil/CollectCircleCollisionsUntil/CollectPolygonCollisionsUntil）通过 `report` lambda 应用 filter（CollisionEngine.cpp:498-504、703-709），而 **Rust 的 collect_* 系列根本不接收 filter**（cd_engine.rs:263-321 无 filter 形参；qt_node.rs:149-210 的 collect_collisions_until 也无 filter）。即：C++ collect API 的 filter 能力是 Rust 没有的接口面（当前调用点均传 NoFilter，无行为分叉）。详见记录 22 补-3/补-7。判定：C++ 额外（接口面差异，新增登记）。

### 补-3【验证】`from_irrelevant_qzones` 缺失现状维持
全仓 grep `Irrelevant`（区分大小写）仍仅命中 HazardFilter.h 自身、Layout.cpp:214 的 lambda、TestCollision 的测试过滤器——质量区过滤器无任何构造点。jagua-rs 侧确认 filter.rs:22-43 在 src 内无调用（examples 层可能有，超出比对范围）。判定：维持正文（C++ 缺失，无行为分叉）。
