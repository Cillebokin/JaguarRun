# 18. hazard-collector 逐文件逻辑比对
**Rust**: `CodeResource/jagua-rs/jagua-rs/src/collision_detection/hazards/collector.rs`（146 行）
**C++**: `JaguarCore/Include/JaguarCore/Collision/HazardCollector.h`（35 行）+ `JaguarCore/Source/Collision/Hazards/Collector.cpp`（69 行）
**判定**: 基本等价（收集语义一致；数据结构 SecondaryMap→vector，64 位 bloom 快筛在 C++ 无对应）

## 函数逐条比对

### `trait HazardCollector`（collector.rs:7-42，含默认方法）
- C++ 对应：`class HazardCollector`（HazardCollector.h:10-34）
- 判定：结果等价/实现不同
- 证据与说明：Rust 是 trait + 默认方法；C++ 是具体类（vector 存储）。接口一一对应：

| Rust（行号） | C++（行号） | 判定 |
|---|---|---|
| `contains_key(hkey)`（8） | `Contains(handle)`（Collector.cpp:7-11） | 一致（见下条性能差异） |
| `contains_entity(entity)`（10-12，默认 `iter().any`） | `ContainsEntity(entity)`（Collector.cpp:13-18，`any_of`） | 一致 |
| `insert(hkey, entity)`（14） | `Insert(handle, entity)`（Collector.cpp:25-34） | 一致（Rust SecondaryMap::insert 覆盖旧值；C++ 先 find，找到则覆盖 `it->second = entity`（Collector.cpp:32-33），未找到 emplace_back——覆盖语义一致） |
| `remove_by_key(hkey)`（16） | `Remove(handle)`（Collector.cpp:36-41） | 一致（C++ erase-remove 全部匹配项；Rust SecondaryMap remove 单键，键唯一故等价） |
| `remove_by_entity(entity)`（18-25，默认线性找 + panic） | `RemoveEntity(entity)`（Collector.cpp:43-53） | 差异（错误通道）：Rust `.expect("HazardEntity not found in collector")` panic；C++ 找不到返回 false 不抛错 |
| `is_empty()`（27-29） | `Empty()`（Collector.cpp:60-63） | 一致 |
| `len()`（31） | `Size()`（Collector.cpp:55-58） | 一致 |
| `iter()`（33） | `Entries()`（Collector.cpp:65-68） | 一致（Rust 迭代器 vs C++ 返回 vector 引用） |
| `keys()`/`entities()`（35-41，默认实现） | 无独立 API（调用方遍历 Entries） | C++ 缺失（便捷性） |

- Rust 的 trait 约束 `HazardCollector: HazardFilter`（collector.rs:7）在 C++ 侧不存在（记录 17 差异 #6）。

### `BasicHazardCollector` 结构（collector.rs:44-53）
- C++ 对应：`HazardCollector::m_Entries`（HazardCollector.h:33）
- 判定：结果等价/实现不同
- 证据与说明：Rust 用 `SecondaryMap<HazKey, HazardEntity>`（键→实体映射，O(1) 查找）；C++ 用 `std::vector<std::pair<HazardHandle, HazardEntity>>`（线性查找）。**64 位 bloom 快筛 `detected_key_bits: u64`（collector.rs:48-53）在 C++ 无对应**：
  ```rust
  // collector.rs:48-53
  /// Lossy negative filter for `detected`. The low six bits of each key select one of these 64
  /// bits. An unset bit proves that the key is absent; a set bit is only a possible match and is
  /// verified in `detected`. Bits stay set after removal because several keys may share a bit.
  /// Stale bits only cause an extra map lookup.
  detected_key_bits: u64,
  ```
  这是 Rust 侧 `contains_key` 的 O(1) 预筛优化。C++ `Contains` 直接线性扫（Collector.cpp:9-10），无任何预筛。行为（返回值）等价，性能随收集量线性增长。

### `BasicHazardCollector::new/with_capacity/clear`（collector.rs:55-72）
- C++ 对应：默认构造 + `Clear()`（Collector.cpp:20-23）
- 判定：一致（clear 同步清位图；C++ 无位图故仅 vector.clear）

### `contains_key`（collector.rs:74-77）
- C++ 对应：`Contains`（Collector.cpp:7-11）
- 判定：结果等价/实现不同
- 证据与说明：
  ```rust
  // collector.rs:75-77
  self.detected_key_bits & Self::key_bit(hkey) != 0 && self.detected.contains_key(hkey)
  ```
  位图先验否证（bit=0 必不存在），bit=1 再查 SecondaryMap 确证。C++ 线性 `any_of` 按 handle 相等。两者返回值一致，复杂度不同（O(1) vs O(n)）。

### `insert`（collector.rs:79-82）/ `remove`（collector.rs:84-86）/ `retain`（collector.rs:88-90）
- C++ 对应：`Insert`（Collector.cpp:25-34）/ `Remove`（Collector.cpp:36-41）/ 无 retain
- 判定：insert/remove 一致；retain 为 C++ 缺失
- 证据与说明：Rust `insert` 先置位图位（`detected_key_bits |= key_bit`，collector.rs:80）；`remove` **不**清位（注释 collector.rs:50-51 明示 stale bit 只引起一次多余查找）。C++ Insert 的去重+覆盖见上表。`retain`（按谓词保留）在 C++ 无对应（现有调用链不需要）。

### `is_empty/len/iter`（collector.rs:92-104）+ `key_bit`（collector.rs:106-108）
- C++ 对应：`Empty/Size/Entries`（Collector.cpp:55-68）/ 无
- 判定：一致 / key_bit 为 C++ 缺失（bloom 机制的一部分）
- 证据与说明：`key_bit = 1 << (hkey.data().as_ffi() & 63)`（collector.rs:107），取 slotmap key 低位 mod 64。C++ 无。

### `impl HazardCollector for BasicHazardCollector`（collector.rs:117-137）+ `IntoIterator`（collector.rs:139-146）
- C++ 对应：即类本身（无 trait 分离）
- 判定：一致（形态差异）

## 差异汇总表

| # | 项 | 判定 | 影响 | 证据 |
|---|---|---|---|---|
| 1 | 64 位 bloom 快筛（`detected_key_bits`/`key_bit`） | C++ 缺失 | Contains/Insert O(1) 预筛丢失，C++ 线性扫描，收集量大时性能劣化；无行为差异 | collector.rs:48-53、74-82、106-108 vs Collector.cpp:7-11 |
| 2 | SecondaryMap vs vector 存储 | 结果等价/实现不同 | C++ Contains/Insert 均 O(n)（Insert 内含 find_if） | collector.rs:46-47 vs Collector.cpp:25-34 |
| 3 | `remove_by_entity` 找不到时 panic vs 返回 false | 差异（错误通道） | Rust 调试期崩溃 vs C++ 静默 false；现有 C++ 调用链未依赖返回值 | collector.rs:18-25 vs Collector.cpp:43-53 |
| 4 | `retain` 谓词保留 | C++ 缺失 | API 缺口，无调用方 | collector.rs:88-90 |
| 5 | trait（可多实现）vs 具体类 | 结果等价/实现不同 | Rust 允许自定义 collector（如联合 collector）；C++ 单一实现 | collector.rs:7 vs HazardCollector.h:10 |
| 6 | `keys()`/`entities()` 便捷迭代 | C++ 缺失（Entries 可替代） | 便捷性 | collector.rs:35-41 |

## 结论
收集器的可观测行为（insert 去重覆盖、contains、remove、clear、size/empty、entries 遍历）双侧一致。核心差异是性能层面：Rust 的 SecondaryMap + 64 位 bloom 快筛在 C++ 中退化为裸 vector 线性扫描；另外 Rust `remove_by_entity` 的 panic 在 C++ 变为返回 false，属错误通道差异。
