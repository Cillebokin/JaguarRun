# B14：jagua-rs 输入契约执行

**Rust 基线**：`jagua-rs` revision `f5e2b5ffd14931f17a20ce283a2122e877953ceb`  
**C++ 对照**：`JaguarCore/Source/IO/JsonCodec.cpp`、`JaguarCore/Source/IO/Importer.cpp`  
**关联工作项**：B8、B13、B14、B15  
**验收范围**：输入接受/拒绝及分离值进入实例几何配置；不代表全部 JSON/SVG 往返或几何算法已等价

## 五项契约

| 契约 | pinned Rust 行为 | C++ 执行与回归覆盖 |
|---|---|---|
| orientation 必填 | `jagua-rs/src/io/ext_repr.rs::ExtItem` 要求 `orientation: ExtOrientation`；`ExtOrientation` 又要求 `rotation` | `parseItemValue` 通过 `requiredMember("orientation")` 拒绝缺项；`Importer::ImportItemJson` 测缺 orientation，返回序列化错误。结构化 `ExternalItem` 始终带 `RotationPolicy` 值，`None` 表示固定方向策略，不表示 JSON 字段缺失 |
| 拒绝旧版方向字段 | `ExtItem::deserialize` 拒绝 `allowed_orientations`，也拒绝其 alias `allowed_rotations`、`allowed_reflection_axes`；即使同时提供新旧字段也拒绝 | `parseItemValue` 在方向解析前拒绝这三个字段；`Importer::ImportItemJson` 覆盖新旧字段混用输入 |
| item ID 唯一 | SPP/BPP/MSPP 在按 demand 过滤前检查全部外部 ID，因此 demand 为 0 的记录也参与唯一性校验；ID 类型为 `u64` | `importPositiveItems` 先扫描全部 ID，再跳过零 demand；SPP、BPP、MSPP 均覆盖“正 demand + 同 ID 零 demand”拒绝；SPP JSON 覆盖 `u64::MAX` 接受 |
| item 不允许带孔 | `Importer::import_item` 对 `Polygon.inner` 非空返回错误；容器内孔另行保留为质量 0 区域 | `ImportItemShape` 拒绝带孔 polygon；实例 JSON 回归验证 SPP 导入失败。容器孔保留及碰撞行为已有 `TestIo.cpp::runIoTests` 覆盖 |
| min_item_separation | SPP/BPP/MSPP 字段缺省为 0；`with_min_item_separation` 仅接受有限、非负 f32，正数将 item inflate 与 container deflate 的 offset 设为一半 | `commonInstanceFields` 缺省设 0，JSON `null` 和超出有限 f32 的值被拒；SPP/BPP/MSPP 导入入口统一检查有限非负且可表示为有限 f32。正值均验证 item offset 为半间距、容器/条带 offset 为半间距；负数、NaN、正无穷和 f32 溢出输入均有拒绝回归 |

## 执行实现

- `Importer.cpp::validMinItemSeparation` 将外部 double 输入先纳入 Rust f32 可表示域检查，再供 item、container 与 SPP/BPP/MSPP 实例导入复用；无效值以 `InvalidInput` 返回。
- JSON 数值沿 `numberValue` 量化为 f32，保持现有 `null`/非数值序列化错误出口；问题实例导入继续使用各自现有的 `Result` 错误分类。
- ID 唯一检查保持在 demand 过滤之前，避免零需求项绕过检查。

## 验证状态

已完成定向验证，证据范围如下：

- pinned Rust revision 的三个 LBF 集成用例均通过，命令均以锁定 checkout 的 `lbf/Cargo.toml` 为 manifest：
  - `cargo test --manifest-path C:\Users\36176\.cargo\git\checkouts\jagua-rs-dd06f816230283bf\f5e2b5f\lbf\Cargo.toml --test tests item_holes_are_rejected_but_container_holes_are_preserved`
  - `cargo test --manifest-path C:\Users\36176\.cargo\git\checkouts\jagua-rs-dd06f816230283bf\f5e2b5f\lbf\Cargo.toml --test tests instance_separation_controls_item_and_container_geometry`
  - `cargo test --manifest-path C:\Users\36176\.cargo\git\checkouts\jagua-rs-dd06f816230283bf\f5e2b5f\lbf\Cargo.toml --test tests explicit_rotations_import_solve_and_round_trip`
  这三项验证了带孔 item、容器孔、分离值传播/非法值以及 orientation 必填和旧字段拒绝。
- MSVC 2019 x64 Debug 与 Release 下，JaguarCore DLL 和 `JaguarCoreTests` 工程均构建成功，分别在对应 build 目录运行 `nmake /f Makefile.Debug` 或 `nmake /f Makefile.Release`。随后用临时入口仅调用 `runIoTests()`、`runJsonCodecTests()`，两个配置均以退出码 0 完成；新增 B14 断言位于 `TestJsonCodec.cpp::runJsonCodecTests`。
- 完整 `JaguarCoreTests.exe` 曾启动，但长时间运行无阶段输出，本轮停止了该进程；因此不登记完整测试套件通过。

当前 B14 五项输入契约在所列样例上接受/拒绝一致。此结论不覆盖完整 rotation 数值边界、全资产导入、完整 JSON/SVG 往返或所有几何输入域；这些仍由 B8/B9 和差分工作项继续验收。
