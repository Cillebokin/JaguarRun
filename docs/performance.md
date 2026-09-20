# JaguarCore 性能基线

Task 19 的基线程序位于 `JaguarCore/tests/bench`，通过独立的 `JaguarCoreBench.pro` 构建，不进入 JaguarCore DLL。

## 基准项

- `bench_geometry.cpp`
  - 多边形边、面积、包含查询；
  - 重复刚体变换；
  - 简化处理。
- `bench_collision.cpp`
  - 线性遍历多边形碰撞；
  - 四叉树 CDE 查询；
  - Surrogate fail-fast 查询。
  - 批量标量查询与批量并行查询。
  - SoA 边界粗筛的 Scalar/Auto/Avx2 三种模式；
  - 每个查询同时比较 brute-force 与 CDE 结果；结果不一致时程序失败。
- `bench_layout.cpp`
  - 预缓存变换轮廓与重复重建轮廓；
  - Layout 放置/移除；
  - Snapshot restore。

## 输出

程序输出简单的 CSV 风格行：

```text
collision,brute_ms,123.456
collision,quadtree_ms,12.345
collision,surrogate_ms,4.321
collision,soa_scalar_ms,1.234
collision,soa_auto_ms,0.987
collision,soa_avx2_ms,0.765
```

Debug 与 Release 必须分别记录。单次耗时不是跨机器结论；比较时应保持编译器、架构、顶点数、Hazard 数、查询数和容差一致。

## SIMD/SoA 实现边界

Task 19 的 SIMD 实现只覆盖 SoA 边界粗筛，不改变精确几何语义：

- `PolygonSoA` 保存边端点和边 bbox 的 SoA 数组；
- AVX2 每次处理 4 条边 bbox，候选命中后仍调用标量 `Edge2d::intersects()`；
- 不修改公共 `SimplePolygon` ABI；
- `SoaExecutionMode::Scalar` 强制标量，`Auto` 仅在运行时 AVX2 可用且模块包含 AVX2 对象时使用 AVX2，`Avx2` 在不可用时安全回退到标量；
- `soa_geometry_avx2.cpp` 通过 qmake `AVX2_SOURCES` 单独使用 `/arch:AVX2`，普通 JaguarCore 源文件不继承该编译选项；
- 使用 `CONFIG+=jaguarcore_no_avx2` 可构建纯标量 DLL，便于旧 CPU、部署兼容和差分验证。

这项优化只保证结果一致性和候选粗筛加速，不等同于整个碰撞系统或排料优化器的线性加速保证。

## 2026-09-18 本机验证

环境：MSVC2019 x64、Debug/Release、JaguarCoreBench `all`；碰撞场景 144 个动态矩形 Hazard、2000 个查询；Layout 场景 36 个放置位置。

| 配置 | Geometry primitive | Geometry transform | Collision brute | Collision quadtree | Collision surrogate | Batch scalar | Batch parallel | SoA Scalar | SoA Auto | SoA Avx2 | Layout cached | Layout rebuild | Layout place/remove | Layout restore |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Debug + AVX2 | 1186.356 ms | 41112.059 ms | 13.069 ms | 77.513 ms | 46.268 ms | 1205.091 ms | 848.104 ms | 10.767 ms | 13.210 ms | 12.221 ms | 0.339 ms | 1467.114 ms | 22742.771 ms | 7738.338 ms |
| Debug + Scalar | 1208.659 ms | 41552.058 ms | 8.307 ms | 52.266 ms | 36.044 ms | 1249.532 ms | 848.411 ms | 9.005 ms | 7.588 ms | 10.931 ms | 1.000 ms | 1421.343 ms | 23097.154 ms | 7586.237 ms |
| Release + AVX2 | 91.778 ms | 3272.584 ms | 1.834 ms | 4.354 ms | 2.625 ms | 105.161 ms | 32.380 ms | 2.285 ms | 1.724 ms | 1.473 ms | 0.282 ms | 82.059 ms | 212.035 ms | 665.588 ms |
| Release + Scalar | 87.371 ms | 3645.508 ms | 1.183 ms | 3.376 ms | 3.401 ms | 90.976 ms | 30.652 ms | 1.437 ms | 1.433 ms | 1.417 ms | 0.225 ms | 71.819 ms | 231.199 ms | 696.768 ms |

Collision correctness check: after the virtual-root candidate filtering and collector changes, all four configurations again reported `brute_hits=1271`, `quadtree_hits=1271`, `surrogate_hits=792`, and `soa_hits=32500`. `soa_avx2_supported` was `1` for AVX2 builds on this machine and `0` for scalar builds. The Surrogate count is a fail-fast screening count and is intentionally not treated as a complete collision result. These numbers are single-machine measurements, not cross-machine performance claims; Debug timings are especially affected by diagnostics and should not be compared with Release as an optimization ratio.

The same four configurations also ran the complete `JaguarCoreTests` executable with exit code `0`; Release test assertions remained active through `/UNDEBUG` and the forced `test_release_asserts.h` include.

## 构建与验证命令

AVX2 默认构建使用 qmake 的独立 `AVX2_SOURCES` 编译规则。纯标量对照构建使用：

```text
qmake JaguarCore.pro CONFIG+=jaguarcore_no_avx2
```

验证要求：Debug/Release 各运行一次 JaguarCoreTests，并运行 `JaguarCoreBench all`；启动测试和基准时应从 NestLab 工程根目录运行，以便加载 JSON fixture。
