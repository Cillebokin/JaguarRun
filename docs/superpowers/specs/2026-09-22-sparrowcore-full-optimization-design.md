# SparrowCore 完整优化流程设计

## 目标

在不破坏 JaguarCore 核心边界的前提下，将当前 SparrowCore 的 LBF 初始构造能力扩展为完整的 Sparrow 两阶段优化流程，并提供可验证的 C++ 原生、CLI、WASM 和浏览器演示入口。

## 范围

本次包含：

1. 恢复并验证 SparrowCore 测试基线；
2. 完成 Sparrow 风格 LBFBuilder；
3. 实现碰撞量化、PairMatrix、CollisionTracker 和分离评估；
4. 实现 Separator 与 Worker；
5. 实现 Exploration；
6. 实现 Compression；
7. 实现 CLI、WASM JSON 接口和浏览器 Demo；
8. 按《代码规范.docx》规范化 SparrowCore 的源码目录、文件名、函数名、枚举值、变量名、include 和 qmake 工程。

不包含 NestLab 生产流程适配，也不修改 JaguarCore 的几何算法或公共 ABI，除非编译期确实需要新增已经批准的只读/快照接口。

## 架构

```text
JaguarCore
  └─ 几何、碰撞、Layout、SPP/BPP、外部表示和验证

SparrowCore
  ├─ LBF 初始构造
  ├─ Sampling / Coordinate Descent
  ├─ Collision Quantification
  ├─ Separator / Worker
  ├─ Exploration
  └─ Compression

SparrowCli / SparrowWasm / SparrowDemo
  └─ 只通过 SparrowCore 公共接口工作
```

依赖方向始终为 `SparrowCore -> JaguarCore`，上层入口不得访问 JaguarCore 私有头文件。

## 公共接口

SparrowCore 的公共函数统一使用 UpperCamelCase，例如 `SolveSppLbf`、`SearchLbf`、`Optimize`、`Version`。公共类型使用 UpperCamelCase，成员变量遵循类成员 `m_` + UpperCamel，结构数据成员使用 lowerCamel。布尔变量使用 `is`、`has` 或 `can` 表达状态。

源代码目录和文件名首字母大写：`Include`、`Source`、`Tests`、`Bench`、`Docs` 以及 `Optimizer`、`Sampling`、`Random`、`Eval`、`Support`、`IO` 等模块目录。旧的小写路径不保留兼容别名，因为 SparrowCore 尚未发布稳定 ABI。

## 线程模型

Separator 是单个优化任务的状态所有者。每个 Worker 使用独立的候选变换和评估上下文，不跨线程共享可变 Layout。候选结果通过值返回，主 Separator 在单线程状态机中提交接受、回滚和 CollisionTracker 更新。终止器只提供原子读取，不持有 Layout 引用。

## 错误处理

公共接口继续使用 JaguarCore 兼容的 `Result<T>`。输入配置、实例、宽度上限、采样预算和恢复状态错误返回 `InvalidInput`；几何上没有可行解时返回 `Infeasible`；WASM/CLI 边界将错误转换为结构化 JSON，不使用异常作为控制流。

## 验证策略

- 固定 seed 验证 RNG、采样、loss、搜索历史和指标可重复；
- SPP/BPP 输出必须经过 JaguarCore validator；
- Rust/Sparrow 对照只比较明确的行为、不变量、指标和容差，不要求随机搜索位置逐点一致；
- 每个阶段都有独立测试；
- Debug/Release、Scalar/AVX2（若使用）和 WASM 构建分别验收；
- 完整 Sparrow 流程至少验证 LBF 初始解、Separator、Exploration、Compression、终止和最终 JSON/SVG 输出。

## 发布边界

第一阶段保留静态库构建，CLI 和 WASM 作为独立目标。WASM 不依赖 Qt，不暴露 C++ STL ABI，以 JSON 输入和 JSON/SVG 输出作为边界。SparrowCore 的 DLL/包部署在原生算法通过完整验收后单独处理。
