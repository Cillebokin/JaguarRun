# sparrow（Rust）与 SparrowCore（C++）代码逻辑全景

## 总览

本文档覆盖两个相关项目：sparrow 是基于 jagua-rs 碰撞引擎的 2D 不规则条带排料启发式求解器（MIT 许可），SparrowCore 是其纯 C++ 移植，目标是与 Rust 参考实现在固定种子下做轨迹级对拍（MPL/MIT 许可边界见 `SparrowCore/docs/SourceMap.md`）。两者共享一条主线：先用 LBF（Left-Bottom-Fill）贪心构造初始可行解，再进入 exploration 阶段（逐步收缩条带宽度，允许不可行状态，靠 separator 修复重叠），最后进入 compression 阶段（在可行前提下压缩）。Separator 通过 CollisionTracker 维护物品对与容器的重叠损失，并以权重（weight）引导移动；采样与坐标下降负责为单个物品寻找低损失位姿。

阅读顺序建议：先读 S1 理解从命令行到 `Optimize` 的入口与配置；再读 S2 把握 exploration、separator 与 compression 的控制流；随后读 S3（sparrow 的采样、评估与量化）或 S5（SparrowCore 对应部分）理解每一步的评分函数；S4 讲 SparrowCore 优化器层与 Rust 的差异；S6 给出工程结构、测试分层与发布门禁。

## 需要特别注意的结论

第一，Separator 的终止条件是 strike 计数而非单纯迭代上限：每轮 strike 结束时若初始损失 initial_strike_loss * 0.98 <= min_loss 则增加 strike，否则清零（sparrow/src/optimizer/separator.rs 约 172–177 行）；外层循环同时受 strike_limit 与 kill() 约束，并在结束时回滚到记录的最小损失解（约 179 行）。第二，exploration 的收缩步长为 current_width * (1.0 - config.shrink_step)（sparrow/src/optimizer/explore.rs 约 195 行）。第三，SparrowCore 测试以固定种子回归为主，容差比较和逐位比较须按字段及测试分别说明。第四，CLI 与两个 WASM C ABI 导出已有异常边界，CLI 异常退出码为 10，WASM 异常返回码为 2。第五，发布清单已纳入 Ctrl-C、Demo 与 C++/Rust 比较；当前待办集中在 oracle fixture 复核、重复大小写目录漂移、路径假设及尚未执行的 Demo 验收。

## 验证说明

本文主体基于源码阅读与静态检查。10 月 16 日的 Windows MSVC 2019 x64 / Qt 5.12.10 Debug、Release 与 CLI 执行记录见移植工作项清单第十四节，记录由用户提供，本次文档修订没有独立复跑。当前结论须区分源码核对、fixture 对照和运行记录：默认 cold-start 的 C++ 测试使用 compression phase deadline override，而 Rust fixture 仍需去除墙钟截止时间后重生成和复核；浏览器 Demo 也尚未运行。
