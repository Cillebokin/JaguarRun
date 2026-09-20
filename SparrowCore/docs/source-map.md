# SparrowCore LBF 源码映射与许可边界

## 1. 文档目的

本文是 SparrowCore LBF 纯 C++ 移植的来源登记表。它记录参考实现、目标文件、保留的行为、不保证逐点相同的部分，以及许可证边界。本文不授权将 Rust 源码逐行复制到 C++；实现必须重新编写，并通过行为/不变量/指标差分验证。

参考目录：

```text
D:\MyFiles\Gits_Xj\NestLab\NestLab\jagua-rs\lbf
D:\MyFiles\Gits_Xj\NestLab\NestLab\sparrow
D:\MyFiles\Gits_Xj\NestLab\NestLab\JaguarCore
```

## 2. 许可证和来源

| 来源 | 许可证/用途 | SparrowCore 处理 |
|---|---|---|
| `jagua-rs` / `jagua-rs/lbf` | MPL-2.0；几何、CDE、SPP/BPP API 与 LBF reference behavior | 只参考公开 API、算法行为、字段和测试不变量；不复制实现文本；如后续复制受许可保护的代码片段，必须保留 MPL-2.0 notice 并做发布审查 |
| `sparrow` | MIT；Sparrow LBFBuilder、采样、评估、坐标下降和优化阶段参考 | 只参考算法论文、公开行为和模块职责；不复制 Rust 实现文本；如后续复制受许可保护的代码片段，必须保留 MIT notice |
| `JaguarCore` | 本地项目模块；纯 C++ 几何、CDE、Layout、问题模型和 I/O | 只通过 `JaguarCore/include` public headers 和既有链接边界使用；不包含 JaguarCore private headers |

SparrowCore 第一版只生成 static library，不进行 DLL 发布、CRT/ABI 发布和部署；这些属于主计划明确排除的 Task21。

## 3. jagua-rs/lbf 映射

| 参考文件 | 目标文件 | 保留语义 |
|---|---|---|
| `src/config.rs` | `include/SparrowCore/lbf_config.h` | CDE 配置、简化配置、seed、sample budget、local-search fraction |
| `src/samplers/rotation_distr.rs` | `src/sampling/rotation_distribution.cpp` | None/Discrete/Continuous 旋转策略 |
| `src/samplers/uniform_rect_sampler.rs` | `src/sampling/uniform_rect_sampler.cpp` | bbox 内 x/y/rotation 均匀采样 |
| `src/samplers/ls_sampler.rs` | `src/sampling/local_search_sampler.cpp` | 平移/旋转正态采样、均值移动、指数衰减 |
| `src/opt/loss.rs` | `src/optimizer/lbf_loss.cpp` | `10*x_max+y_max`，采样 bbox 的 x_max tightening |
| `src/opt/search.rs` | `src/optimizer/lbf_search.cpp` | Uniform → Surrogate → exact polygon → Local Search |
| `src/opt/lbf_spp.rs` | `src/optimizer/spp_lbf_optimizer.cpp` | item order、demand、无位置时扩宽、fitStrip |
| `src/opt/lbf_bpp.rs` | `src/optimizer/bpp_lbf_optimizer.cpp` | open layout 优先、stock bin 次之、库存/成本 |
| `src/util/assertions.rs` | `src/support/lbf_assertions.cpp` | strip width sanity check |
| `src/io/output.rs` | `src/io/lbf_output.cpp` | external solution/config/metrics 输出边界 |

### jagua-rs/lbf 的已知差异

- Rust LBF 参考实现使用 `SmallRng`；SparrowCore 不把 `SmallRng` 的内部序列当作跨语言 API，采用独立、文档化的 `SparrowRng` 并比较 raw primitive/metrics。
- Rust reference 的 `f32` 几何进入 JaguarCore 时转为 `double`；比较时记录转换点并使用明确容差。
- Rust `SPProblem`/`BPProblem` 的内部 private 状态不进入 SparrowCore；使用 JaguarCore public value types 重建结果。
- Rust BPP 空 Layout 由问题模型关闭；MSPP 空 Layout 是合法 solution state，须保留其 JSON round-trip 语义。

## 4. Sparrow 映射

| 参考文件 | 目标文件 | 第一版范围 |
|---|---|---|
| `src/optimizer/lbf.rs` | `src/optimizer/sparrow_lbf_builder.cpp` | initial construction、按 convex-hull-area×diameter 排序、width ceiling |
| `src/sample/search.rs` | `src/sampling/best_samples.cpp` / `src/optimizer/lbf_search.cpp` | container/focussed samples、BestSamples、两阶段 refinement |
| `src/sample/coord_descent.rs` | `src/sampling/coordinate_descent.cpp` | horizontal/vertical/diagonal/wiggle candidate 与 step update |
| `src/sample/uniform_sampler.rs` | `src/sampling/uniform_rect_sampler.cpp` | item bbox 合法化采样 |
| `src/eval/sample_eval.rs` | `src/optimizer/sample_eval.cpp` | Invalid/Collision/Clear 三态排序 |
| `src/eval/lbf_evaluator.rs` | `src/eval/sparrow_lbf_evaluator.cpp` | `10*(POI.x+bbox.x)+POI.y+bbox.y` |
| `src/consts.rs` | `include/SparrowCore/lbf_config.h` | sample/step constants |

## 5. 明确不在 Task1/第一版 LBF 内

以下模块不在本计划的 LBF 初始实现内：

- `sparrow/src/optimizer/explore.rs`；
- `sparrow/src/optimizer/compress.rs`；
- `sparrow/src/optimizer/separator.rs` 和 `worker.rs`；
- `sparrow/src/quantify/*` CollisionTracker、PairMatrix、Overlap Proxy 和 SIMD 量化；
- TUI、live SVG viewer、CLI、DXF/SVG 输入转换；
- NestLab 适配、生产排料替换、DLL 部署和 ABI 验收。

这些内容只能在 SparrowCore LBF initial construction 完成、Rust/C++ metrics differential 稳定后另开计划。

## 6. Fixture 资产

Task1 固定资产：

- `tests/fixtures/lbf_spp_small.json`：2 个 2×2 矩形裁片，固定条带高度 4，demand=2；
- `tests/fixtures/lbf_bpp_small.json`：1 个 2×2 矩形裁片，6×6 Bin，stock=1，cost=10；
- `tests/fixtures/lbf_config_seed42.json`：四叉树深度 4、阈值 32、64 samples、`ls_frac=0.25`、seed=42 的参考配置；
- `tests/fixtures/lbf_seed42_reference.tsv`：SparrowRng raw/float primitive 的字段定义和后续 seed=42 oracle 入口；
- `tests/fixtures/lbf_metrics_reference.tsv`：LBF reference 的 sample count、placed、remaining、width、density、cost 字段定义；SPP 的 cost=0，BPP 的 width=0 表示该指标不适用。

两个 JSON fixture 使用 JaguarCore 已验证的 snake_case external representation：每个 Item 都有 `orientation.rotation`，不含 holes、legacy orientation 字段、NaN 或 Infinity。

## 7. 验证策略

Task1 只验证资产和映射，不声称 LBF 算法已经实现：

```text
JSON syntax/type/finite-number check
  ↓
JaguarCore parser boundary check
  ↓
Rust lbf reference invocation (when generating metrics)
  ↓
later SparrowCore differential comparator
```

Task1 不修改 JaguarCore 生产代码，也不要求当前 C++ 能消费 SparrowCore fixture；消费 fixture 的代码分别由 Task2、Task8、Task9 和 Task10 实现。
