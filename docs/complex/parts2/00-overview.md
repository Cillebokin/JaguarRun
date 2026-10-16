# sparrow（Rust）与 SparrowCore（C++）代码逻辑全景

## 总览

本文档覆盖两个相关项目：sparrow 是基于 jagua-rs 碰撞引擎的 2D 不规则条带排料启发式求解器（MIT 许可），SparrowCore 是其纯 C++ 移植，目标是与 Rust 参考实现在固定种子下做轨迹级对拍（MPL/MIT 许可边界见 `SparrowCore/docs/SourceMap.md`）。两者共享一条主线：先用 LBF（Left-Bottom-Fill）贪心构造初始可行解，再进入 exploration 阶段（逐步收缩条带宽度，允许不可行状态，靠 separator 修复重叠），最后进入 compression 阶段（在可行前提下压缩）。Separator 通过 CollisionTracker 维护物品对与容器的重叠损失，并以权重（weight）引导移动；采样与坐标下降负责为单个物品寻找低损失位姿。

阅读顺序建议：先读 S1 理解从命令行到 `Optimize` 的入口与配置；再读 S2 把握 exploration、separator 与 compression 的控制流；随后读 S3（sparrow 的采样、评估与量化）或 S5（SparrowCore 对应部分）理解每一步的评分函数；S4 讲 SparrowCore 优化器层与 Rust 的差异；S6 给出工程结构、测试分层与发布门禁。

## 需要特别注意的结论

第一，Separator 的终止条件是"strike 计数"而非单纯的迭代上限：每一轮 strike 结束时若初始损失 `initial_strike_loss * 0.98 <= min_loss` 则增加 strike，否则清零（`sparrow/src/optimizer/separator.rs` 约 172–177 行）；外层循环同时受 `strike_limit` 与 `kill()` 约束，并在结束时回滚到记录的最小损失解（约 179 行）。第二，exploration 的收缩步长为 `current_width * (1.0 - config.shrink_step)`（`sparrow/src/optimizer/explore.rs` 约 195 行）。第三，SparrowCore 的测试以容差级别的固定种子回归为主，只有采样与 RNG 等少数模块做逐位比较，文档中的 "bit-exact" 一词需结合语境理解。第四，SparrowCore 的 C ABI 与 CLI 都没有异常边界保护，`std::logic_error` 类异常会直接穿越 C 边界。第五，多处文档与代码存在路径与大小写不一致，包括 `.pro` 中的 `Include/` 与磁盘上的 `include/`，以及 NestLab 的硬编码路径，在其他环境构建前需先修正。

## 验证说明

本文档基于源码阅读与静态检查完成。尚未在本环境执行 `cargo test`、Qt/MSVC 构建或 `SparrowCoreTests.exe`，因此所有行为性结论（例如轨迹一致性、Bench checksum、Ctrl-C 行为）都标注为"源码推断"，需要实际运行验证。文中关键结论已抽查：separator 的 strike 与回滚逻辑（`separator.rs` 107、172–179 行）、exploration 的收缩步长（`explore.rs` 195 行）、C++ 测试入口的执行顺序（`tests/TestSupport.cpp`）、WASM 的 Demo 桥接依赖（`Demo/App.js` 38–47 行）以及 CLI 与 WASM 中无 try/catch（grep 确认）。
