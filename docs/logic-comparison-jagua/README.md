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
| [40-io.md](records/40-io.md) | `io/{export,ext_repr,import,svg/*}.rs` | B8、B13、B14 | 5 个文件已做静态映射；JSON/SVG 运行验收仍待 B8/B14 |
| [50-bpp.md](records/50-bpp.md) | `probs/bpp/{entities,io}` 实现 | B7、B8、B13、B14 | 7 个文件已静态映射；实例、快照与 IO 对拍仍待 B7/B8/B14 |
| [60-spp.md](records/60-spp.md) | `probs/spp/{entities,io}` 实现 | B6、B7、B8、B13、B14 | 7 个文件已静态映射；状态与 IO 运行对拍仍待 B6/B7/B8/B14 |
| [70-mspp.md](records/70-mspp.md) | `probs/mspp/{entities,io}` 实现 | B6、B7、B8、B13、B14 | 7 个文件已静态映射；问题状态与 IO 运行对拍仍待 B6/B7/B8/B14 |
| [80-geometry.md](records/80-geometry.md) | `geometry/{primitives,fail_fast}` 与几何 helper | B1、B3、B9、B13 | 15 个实现文件已静态映射；B1 首轮确定性几何夹具已在 Debug/Release 通过，逐函数余项与 fail-fast profile 对拍仍待 B1/B3/B9 |
| [SOURCE-INVENTORY.md](records/SOURCE-INVENTORY.md) | `jagua-rs/src/**/*.rs` | B13 | 85 个 pinned 源文件均有记录：24 个模块边界记录，61 个实现/测试文件静态映射 |

## 证据规则

源码行号均指向表中标明的 pinned revision；C++ 行号应在每次实现或复核时按当前工作区更新。运行结论必须附 oracle/fixture、命令和 profile；静态映射不得写成运行等价。E4 当前只闭合 oracle 与确定性 fixture 的首轮基线，仍有 B2 重算和 cold-start fixture 后续项，见 [E4 执行记录](../complex/E4-oracle-fixture-baseline-2026-10-16.md)。

