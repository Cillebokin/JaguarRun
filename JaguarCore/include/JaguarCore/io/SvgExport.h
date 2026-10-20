#pragma once

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Export.h"
#include "JaguarCore/Result.h"

#include <array>
#include <cstdint>
#include <string>

namespace jaguar
{
// SVG 颜色
struct SvgColor
{
    // 红色分量
    std::uint8_t red = 0;
    // 绿色分量
    std::uint8_t green = 0;
    // 蓝色分量
    std::uint8_t blue = 0;
};

// SVG 布局主题
struct SvgLayoutTheme
{
    // Rust jagua-rs 的 GRAY 主题
    static JAGUARCORE_API SvgLayoutTheme Gray() noexcept;

    // 线宽缩放倍数
    double strokeWidthMultiplier = 2.0;
    // 容器填充色
    SvgColor containerFill{0xcc, 0x82, 0x4a};
    // 物品填充色
    SvgColor itemFill{0xff, 0xc8, 0x79};
    // 碰撞轮廓色
    SvgColor collisionStroke{0x00, 0x80, 0xff};
    // 碰撞高亮色
    SvgColor collisionHighlight{0x00, 0xff, 0x00};
    // 快速碰撞代理轮廓色
    SvgColor surrogateStroke{0x00, 0xaa, 0x55};
    // 坐标轴轮廓色
    SvgColor axisStroke{0x55, 0x55, 0x55};
    // 各质量等级区域填充色
    std::array<SvgColor, 10> qualityZoneFills{{
        {0x00, 0x00, 0x00}, {0xff, 0x00, 0x00}, {0xff, 0x5e, 0x00},
        {0xff, 0xa5, 0x00}, {0xc7, 0xa9, 0x00}, {0xff, 0xff, 0x00},
        {0xcb, 0xff, 0x00}, {0xcb, 0xff, 0x00}, {0xcb, 0xff, 0x00},
        {0xcb, 0xff, 0x00}}};
    // 质量区域轮廓透明度
    double qualityZoneStrokeOpacity = 0.5;
};

// SVG 布局导出选项
struct SvgExportOptions
{
    // SVG 布局主题
    SvgLayoutTheme theme{};
    // 是否绘制快速碰撞代理
    bool drawSurrogates = false;
    // 是否高亮碰撞区域
    bool highlightCollisions = true;
    // 是否包含坐标轴
    bool includeAxes = true;
    // SVG 文档标题
    std::string title = "JaguarCore Layout";
};

// 将布局导出为 SVG
JAGUARCORE_API Result<std::string> ExportLayoutSvg(
    const Layout& layout,
    const SvgExportOptions& options = {});
}
