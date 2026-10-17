# jagua-rs 迁移账本

## 基线与范围

- Rust 基线：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`；工具链为 E4 记录中的 Rust 1.98.1。
- 权威源码：Cargo.lock 对应的 pinned revision。`CodeResource/jagua-rs/jagua-rs/src` 是本地镜像；E4/B0 已记录它与 pinned revision 的 7 个差异文件，遇到差异时以 pinned 源码为准。
- 本账本覆盖 `jagua-rs/src` 下 85 个 Rust 文件，包括 `#[cfg(test)]` 测试模块。`lbf/src` 属 L 线工作项，不计入 B13 的 jagua-rs 源文件数。
- 每条记录至少列出源文件中的函数/类型、重要分支、失败或 panic 出口、JaguarCore 对应符号、差异结论和证据。只登记模块存在或头文件同名，不作为语义等价证据。

## 记录索引

| 记录 | Rust 源范围 | 关联工作项 | 状态 |
|---|---|---|---|
| [00-module-exports.md](records/00-module-exports.md) | `lib.rs` 与 23 个 `mod.rs` | B0、B13 | 模块导出和 feature 边界已登记；实现语义由各 B 项继续核对 |
| [10-utilities-and-assertions.md](records/10-utilities-and-assertions.md) | 浮点比较、NumericPolicy、断言函数与 `qt_traits` debug 检查 | B9、E8、B13 | B9 静态映射和最小边界 fixture 矩阵已记录；运行对拍仍归 B1/B4/B8/E8/B11 |
| [20-entities.md](records/20-entities.md) | `entities/{container,item,layout,placed_item}.rs` | B6、B13 | 4 个文件已做函数、分支、失败出口与 C++ 符号静态映射；运行验收仍归 B6 |
| [30-collision-detection.md](records/30-collision-detection.md) | `collision_detection/{cd_engine,hazards,quadtree}` 实现与测试 | B4、B5、B6、B9、B11、B13 | 10 个文件已静态登记；碰撞数值与差分验收仍待 B4/B11 |
| [40-io.md](records/40-io.md) | `io/{export,ext_repr,import,svg/*}.rs` | B8、B13、B14 | 5 个文件已静态映射；B14 输入契约已执行；完整 JSON/SVG 往返对拍仍归 B8 |
| [50-bpp.md](records/50-bpp.md) | `probs/bpp/{entities,io}` 实现 | B7、B8、B13、B14 | 7 个文件已静态映射；B14 输入契约见 91；实例与快照验收仍归 B7/B8 |
| [60-spp.md](records/60-spp.md) | `probs/spp/{entities,io}` 实现 | B6、B7、B8、B13、B14 | 7 个文件已静态映射；B14 输入契约见 91；状态与完整 IO 对拍仍归 B6/B7/B8 |
| [70-mspp.md](records/70-mspp.md) | `probs/mspp/{entities,io}` 实现 | B6、B7、B8、B13、B14 | 7 个文件已静态映射；B14 输入契约见 91；问题状态与完整 IO 对拍仍归 B6/B7/B8 |
| [80-geometry.md](records/80-geometry.md) | `geometry/{primitives,fail_fast}` 与几何 helper | B1、B3、B9、B13 | 15 个实现文件已静态映射；B1 fixture 含 82 个唯一键，第七轮 Rust Debug/Release oracle 与 C++ 定向 comparator 已通过；第八轮裁决并登记 Rect 退化象限与 SPolygon Rust-only helper 两项差异；B1 仍为部分，其他边界见记录 |
| [90-test-mapping.md](records/90-test-mapping.md) | pinned `jagua-rs/src` 单元测试与 `lbf/tests`；本地 fork 测试另行标识 | B15、R-3 | pinned 27 个测试实例和本地 30 个测试实例均有 C++ 功能测试映射；明确区分映射与覆盖等价，见未覆盖项 |
| [91-input-contracts.md](records/91-input-contracts.md) | `io/ext_repr.rs`、`io/import.rs`、SPP/BPP/MSPP 实例导入 | B14 | 五项输入契约的 C++ 执行与回归记录，验证状态及边界见记录 |
| [SOURCE-INVENTORY.md](records/SOURCE-INVENTORY.md) | `jagua-rs/src/**/*.rs` | B13 | 85 个 pinned 源文件均有记录：24 个模块边界记录，61 个实现/测试文件静态映射 |

## 证据规则

源码行号均指向表中标明的 pinned revision；C++ 行号应在每次实现或复核时按当前工作区更新。运行结论必须附 oracle/fixture、命令和 profile；静态映射不得写成运行等价。E4 的 oracle/fixture 基线、B2 重算和默认 cold-start fixture 已完成；cold-start 33 行 Rust Debug/Release trace 与 fixture 一致，C++ SparrowCore Debug/Release 全套 runner 通过。该基线结论不替代各工作项的跨语言行为验收，详见 [E4 执行记录](../complex/E4-oracle-fixture-baseline-2026-10-16.md)。

