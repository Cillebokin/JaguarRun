# JaguarCore 安全变换与数值策略设计

日期：2026-09-21

## 范围

本次只完善 JaguarCore 本体，不处理 NestLab、DXF、`Exp_Entity`、lbf 或 WASM。

目标是修复两个已确认的核心问题：

1. 非法或变换失败时，几何 API 不应绕过 `Result` 直接取值。
2. `NumericPolicy` 不应只是测试用声明，至少要成为公共几何默认阈值和多边形有效性判定的实际来源。

## 方案

### 变换错误传播

- 为 `Transform2d` 增加 `isFinite()`，检查旋转和平移分量。
- 将 `SimplePolygon::transformed()` 改为返回 `Result<SimplePolygon>`。
- 对非有限变换参数返回 `ErrorCode::InvalidInput`。
- 对变换后产生非有限点、零面积或非法多边形的情况，直接传播 `SimplePolygon::create()` 的错误。
- `PlacedItem::create()` 和 `OriginalShape::convertToInternal()` 必须检查并传播变换结果。
- 更新现有调用方和测试；不保留一个会在非法输入下抛出异常的旧公共路径。

### NumericPolicy 接入

- 为 `NumericPolicy` 增加可复用的默认阈值访问方式。
- `SimplePolygon::create()` 接收可选的 `NumericPolicy`，面积有效性使用 `areaTolerance`。
- `Edge2d`、`SimplePolygon`、SoA 几何接口的默认比较容差引用 `NumericPolicy` 的默认值。
- 导入器的连续重复点清理和布局/快照的通用相等比较使用同一默认策略。
- 仅迁移通用公共阈值；直线骨架、偏移算法等内部稳定性 epsilon 保持局部命名，避免本次任务引入未经验证的算法重调参。

## 错误处理和边界条件

- `Transform2d::identity()` 仍保持零初始化且有效。
- `NaN`、正负无穷和变换后溢出均返回 `InvalidInput` 或底层多边形错误。
- 变换成功后，已有 surrogate 继续按现有规则复制变换结果。
- 不改变合法输入的顶点顺序、面积、质心和 surrogate 语义。

## 测试策略

先增加失败测试，再实现代码：

1. 直接对 `SimplePolygon` 使用非有限变换，断言返回 `InvalidInput`。
2. 通过 `Layout::placeItem()` 使用非有限变换，断言失败且布局状态未改变。
3. 通过 `OriginalShape::convertToInternal()` 使用非有限预变换，断言错误传播。
4. 验证自定义 `NumericPolicy::areaTolerance` 能影响极小面积多边形的有效性。
5. 保留并运行现有几何、布局、差分和问题模型测试。

## 非目标

- 不增加 DXF 解析器或 NestLab 适配代码。
- 不实现 jagua-rs 的 lbf、WASM 或完整 API 兼容层。
- 不创建稳定 C ABI，也不调整现有 MSVC 包结构。

## 验证

修改完成后运行 Debug/Release × AVX2/Scalar 四种 JaguarCore 测试，并运行现有 `VerifyPackage.ps1`。只有四种测试和包校验均成功，才能宣称本次修改完成。
