#include "JaguarCore/io/svg_export.h"

#include "JaguarCore/fail_fast/surrogate.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace jaguar
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279502884;

std::string number(double value)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string color(const SvgColor& value)
{
    std::ostringstream stream;
    stream << '#' << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << static_cast<int>(value.red)
           << std::setw(2) << static_cast<int>(value.green)
           << std::setw(2) << static_cast<int>(value.blue);
    return stream.str();
}

std::string xmlEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value)
    {
        switch (character)
        {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

Point2d apply(const Transform2d& transform, const Point2d& point) noexcept
{
    return transform.apply(point);
}

std::string polygonPath(const SimplePolygon& polygon,
                        const Transform2d& transform = Transform2d::identity())
{
    std::ostringstream path;
    const auto& vertices = polygon.vertices();
    if (vertices.empty())
        return {};

    const Point2d first = apply(transform, vertices.front());
    path << "M " << number(first.x) << ' ' << number(first.y);
    for (std::size_t i = 1; i < vertices.size(); ++i)
    {
        const Point2d point = apply(transform, vertices[i]);
        path << " L " << number(point.x) << ' ' << number(point.y);
    }
    path << " Z";
    return path.str();
}

std::string transformAttribute(const Transform2d& transform)
{
    return "translate(" + number(transform.translation.x) + " " +
           number(transform.translation.y) + ") rotate(" +
           number(transform.rotationRad * 180.0 / kPi) + ")";
}

std::string linePath(const Point2d& start, const Point2d& end)
{
    return "M " + number(start.x) + ' ' + number(start.y) + " L " +
           number(end.x) + ' ' + number(end.y);
}

std::string circleElement(const Circle2d& circle,
                          const Transform2d& transform,
                          const char* className,
                          const std::string& stroke,
                          double strokeWidth)
{
    const Point2d center = apply(transform, circle.center);
    std::ostringstream output;
    output << "<circle class=\"" << className << "\" cx=\"" << number(center.x)
           << "\" cy=\"" << number(center.y) << "\" r=\"" << number(circle.radius)
           << "\" fill=\"none\" stroke=\"" << stroke << "\" stroke-width=\""
           << number(strokeWidth) << "\"/>";
    return output.str();
}

bool validOptions(const SvgExportOptions& options) noexcept
{
    return std::isfinite(options.theme.strokeWidthMultiplier) &&
           options.theme.strokeWidthMultiplier > 0.0;
}
}

JAGUARCORE_API Result<std::string> exportLayoutSvg(
    const Layout& layout, const SvgExportOptions& options)
{
    if (!validOptions(options))
    {
        return Result<std::string>::fromError(
            {ErrorCode::SerializationFailure,
             "SVG stroke width multiplier must be finite and positive"});
    }

    const Rect2d bounds = layout.container().bounds();
    if (!std::isfinite(bounds.xMin) || !std::isfinite(bounds.yMin) ||
        !std::isfinite(bounds.xMax) || !std::isfinite(bounds.yMax) ||
        bounds.xMin >= bounds.xMax || bounds.yMin >= bounds.yMax)
    {
        return Result<std::string>::fromError(
            {ErrorCode::SerializationFailure, "layout bounds are invalid for SVG export"});
    }

    const double largestSide = std::max(bounds.width(), bounds.height());
    const double margin = std::max(1.0e-6, largestSide * 0.05);
    const double viewX = bounds.xMin - margin;
    const double viewY = bounds.yMin - margin;
    const double viewWidth = bounds.width() + margin * 2.0;
    const double viewHeight = bounds.height() + margin * 2.0;
    const double flipTranslation = viewY + viewHeight;
    const double strokeWidth = largestSide * 0.0025 *
                                options.theme.strokeWidthMultiplier;

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\""
        << number(viewX) << ' ' << number(viewY) << ' ' << number(viewWidth) << ' '
        << number(viewHeight) << "\">\n";
    svg << "  <title>" << xmlEscape(options.title) << "</title>\n";
    svg << "  <g id=\"scene\" transform=\"translate(0 "
        << number(flipTranslation) << ") scale(1 -1)\">\n";

    const Container& container = layout.container();
    svg << "    <path class=\"container-original\" data-container-id=\""
        << container.id() << "\" d=\"" << polygonPath(container.originalShape().shape)
        << "\" fill=\"" << color(options.theme.containerFill)
        << "\" fill-opacity=\"0.25\" stroke=\"" << color(options.theme.containerFill)
        << "\" stroke-width=\"" << number(strokeWidth) << "\" fill-rule=\"evenodd\"/>\n";
    svg << "    <path class=\"container-collision\" data-container-id=\""
        << container.id() << "\" d=\"" << polygonPath(container.collisionShape())
        << "\" fill=\"none\" stroke=\"" << color(options.theme.collisionStroke)
        << "\" stroke-width=\"" << number(strokeWidth)
        << "\" stroke-dasharray=\"" << number(strokeWidth * 3.0) << ' '
        << number(strokeWidth * 2.0) << "\"/>\n";

    const LayoutSnapshot snapshot = layout.save();
    for (std::size_t placementIndex = 0;
         placementIndex < snapshot.placements.size(); ++placementIndex)
    {
        const LayoutPlacementSnapshot& placement = snapshot.placements[placementIndex];
        const PlacedItem& placedItem = placement.placedItem;
        const Item& item = placedItem.item();
        const Transform2d externalTransform =
            item.originalShape().preTransform.followedBy(placedItem.transform());
        const Transform2d internalTransform = placedItem.transform();

        svg << "    <g id=\"item_" << placementIndex << "_" << item.externalId()
            << "\" data-item-id=\"" << item.externalId()
            << "\" data-rotation-degrees=\""
            << number(externalTransform.rotationRad * 180.0 / kPi)
            << "\" data-translation-x=\"" << number(externalTransform.translation.x)
            << "\" data-translation-y=\"" << number(externalTransform.translation.y)
            << "\">\n";
        svg << "      <title>item id: " << item.externalId() << ", transform: "
            << xmlEscape(transformAttribute(externalTransform)) << "</title>\n";
        svg << "      <g class=\"item-placement\" transform=\""
            << transformAttribute(externalTransform) << "\">\n";
        svg << "        <path class=\"item-original\" d=\""
            << polygonPath(item.originalShape().shape) << "\" fill=\""
            << color(options.theme.itemFill)
            << "\" fill-opacity=\"0.7\" stroke=\""
            << color(options.theme.itemFill) << "\" stroke-width=\""
            << number(strokeWidth) << "\"/>\n";
        svg << "      </g>\n";
        svg << "      <path class=\"item-collision\" d=\""
            << polygonPath(item.collisionShape(), internalTransform) << "\" fill=\"none\""
            << " stroke=\"" << color(options.theme.collisionStroke)
            << "\" stroke-width=\"" << number(strokeWidth)
            << "\" stroke-dasharray=\"" << number(strokeWidth * 2.0) << ' '
            << number(strokeWidth) << "\"/>\n";

        if (options.drawSurrogates && item.collisionShape().surrogate())
        {
            const Surrogate transformed =
                item.collisionShape().surrogate()->transformed(internalTransform);
            svg << "      <g class=\"surrogate\" data-item-id=\""
                << item.externalId() << "\">\n";
            for (const Circle2d& pole : transformed.poles())
            {
                svg << "        "
                    << circleElement(pole, Transform2d::identity(), "surrogate-pole",
                                     color(options.theme.surrogateStroke), strokeWidth)
                    << "\n";
            }
            for (const Edge2d& pier : transformed.piers())
            {
                svg << "        <path class=\"surrogate-pier\" d=\""
                    << linePath(pier.start, pier.end) << "\" fill=\"none\" stroke=\""
                    << color(options.theme.surrogateStroke) << "\" stroke-width=\""
                    << number(strokeWidth) << "\"/>\n";
            }
            svg << "      </g>\n";
        }
        svg << "    </g>\n";
    }

    const bool feasible = layout.isFeasible();
    svg << "    <g id=\"collision-debug\" data-layout-feasible=\""
        << (feasible ? "true" : "false") << "\" data-enabled=\""
        << (options.highlightCollisions ? "true" : "false") << "\"/>\n";
    svg << "  </g>\n";

    if (options.includeAxes)
    {
        const double xAxisY = std::clamp(0.0, viewY, viewY + viewHeight);
        const double yAxisX = std::clamp(0.0, viewX, viewX + viewWidth);
        svg << "  <g id=\"axes\" transform=\"translate(0 "
            << number(flipTranslation) << ") scale(1 -1)\" stroke=\""
            << color(options.theme.axisStroke) << "\" stroke-width=\""
            << number(strokeWidth * 0.75) << "\" fill=\"none\">\n";
        svg << "    <path d=\"M " << number(viewX) << ' ' << number(xAxisY)
            << " L " << number(viewX + viewWidth) << ' ' << number(xAxisY) << "\"/>\n";
        svg << "    <path d=\"M " << number(yAxisX) << ' ' << number(viewY)
            << " L " << number(yAxisX) << ' ' << number(viewY + viewHeight) << "\"/>\n";
        svg << "  </g>\n";
    }

    svg << "</svg>\n";
    return Result<std::string>::fromValue(svg.str());
}
}
