#pragma once

#include "JaguarCore/entities/layout.h"
#include "JaguarCore/export.h"
#include "JaguarCore/result.h"

#include <cstdint>
#include <string>

namespace jaguar
{
struct SvgColor
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

struct SvgLayoutTheme
{
    double strokeWidthMultiplier = 2.0;
    SvgColor containerFill{0xcc, 0x82, 0x4a};
    SvgColor itemFill{0xff, 0xc8, 0x79};
    SvgColor collisionStroke{0x00, 0x80, 0xff};
    SvgColor surrogateStroke{0x00, 0xaa, 0x55};
    SvgColor axisStroke{0x55, 0x55, 0x55};
};

struct SvgExportOptions
{
    SvgLayoutTheme theme{};
    bool drawSurrogates = false;
    bool highlightCollisions = true;
    bool includeAxes = true;
    std::string title = "JaguarCore Layout";
};

JAGUARCORE_API Result<std::string> exportLayoutSvg(
    const Layout& layout,
    const SvgExportOptions& options = {});
}
