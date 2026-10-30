#include "JaguarCore/IO/SvgExport.h"

#include "JaguarCore/FailFast/Surrogate.h"
#include "JaguarCore/Geometry/RustF32.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace jaguar
{
struct SvgDebugAccess
{
    struct QuadtreeCell
    {
        Rect2d bounds;
        HazardPresence presence = HazardPresence::None;
    };

    static std::vector<QuadtreeCell> QuadtreeCells(const Layout& layout)
    {
        std::vector<QuadtreeCell> cells;
        if (layout.m_Cde)
            AppendQuadtreeCells(layout.m_Cde->m_QuadTree.m_Root, cells);
        return cells;
    }

private:
    static void AppendQuadtreeCells(const QuadTree::Node& node,
                                    std::vector<QuadtreeCell>& cells)
    {
        const bool hasHazards = !node.hazards.empty();
        const bool hasChildren = std::any_of(
            node.children.begin(), node.children.end(),
            [](const auto& child) { return static_cast<bool>(child); });
        if (hasChildren && hasHazards)
        {
            for (const auto& child : node.children)
            {
                if (child)
                    AppendQuadtreeCells(*child, cells);
            }
            return;
        }

        cells.push_back({node.bounds,
                         hasHazards ? node.hazards.front().presence
                                    : HazardPresence::None});
    }
};

SvgLayoutTheme SvgLayoutTheme::Gray() noexcept
{
    SvgLayoutTheme theme{};
    theme.strokeWidthMultiplier = 2.5;
    theme.containerFill = {0xd3, 0xd3, 0xd3};
    theme.itemFill = {0x7a, 0x7a, 0x7a};
    theme.collisionStroke = {0x00, 0x00, 0x00};
    theme.collisionHighlight = {0xd0, 0x00, 0x00};
    theme.surrogateStroke = {0x00, 0x00, 0x00};
    const SvgColor gray{0x63, 0x63, 0x63};
    theme.qualityZoneFills.fill(gray);
    theme.qualityZoneStrokeOpacity = 0.9;
    return theme;
}

namespace
{
std::string number(double value)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}

std::string number(float value)
{
    char buffer[64]{};
    const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (formatted.ec != std::errc{})
        return number(static_cast<double>(value));
    return std::string(buffer, formatted.ptr);
}

std::string geometryNumber(double value)
{
    return number(static_cast<float>(value));
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

SvgColor halfBrightness(const SvgColor& value) noexcept
{
    return {
        static_cast<std::uint8_t>(static_cast<float>(value.red) * 0.5F),
        static_cast<std::uint8_t>(static_cast<float>(value.green) * 0.5F),
        static_cast<std::uint8_t>(static_cast<float>(value.blue) * 0.5F)};
}

SvgColor blendColors(const SvgColor& first, const SvgColor& second) noexcept
{
    const auto blend = [](std::uint8_t lhs, std::uint8_t rhs) {
        return static_cast<std::uint8_t>(
            (static_cast<unsigned int>(lhs) + rhs) / 2U);
    };
    return {blend(first.red, second.red), blend(first.green, second.green),
            blend(first.blue, second.blue)};
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

Point2d Apply(const Transform2d& transform, const Point2d& point) noexcept
{
    return transform.Apply(point);
}

std::string pathPoint(const Point2d& point)
{
    return geometryNumber(point.x) + ',' + geometryNumber(point.y);
}

std::string polygonPath(const SimplePolygon& polygon,
                        const Transform2d& transform = Transform2d::Identity())
{
    std::ostringstream path;
    const auto& vertices = polygon.Vertices();
    if (vertices.empty())
        return {};

    const Point2d first = Apply(transform, vertices.front());
    path << "M" << pathPoint(first);
    for (std::size_t i = 1; i < vertices.size(); ++i)
    {
        const Point2d point = Apply(transform, vertices[i]);
        path << " L" << pathPoint(point);
    }
    path << " z";
    return path.str();
}

std::string transformAttribute(const Transform2d& transform)
{
    return "translate(" + geometryNumber(transform.translation.x) + " " +
           geometryNumber(transform.translation.y) + "), rotate(" +
           number(detail::ToDegreesRustF32(transform.rotationRad)) + ")";
}

std::string fixedThree(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

std::string rustTransformLabel(const Transform2d& transform)
{
    return "r: " + fixedThree(detail::ToDegreesRustF32(transform.rotationRad)) +
           "°, t: (" + fixedThree(static_cast<float>(transform.translation.x)) +
           ", " + fixedThree(static_cast<float>(transform.translation.y)) + ")";
}

std::string rustContainerTitle(const Rect2d& bounds)
{
    return "container, bbox: [x_min: " +
           fixedThree(static_cast<float>(bounds.xMin)) + ", y_min: " +
           fixedThree(static_cast<float>(bounds.yMin)) + ", x_max: " +
           fixedThree(static_cast<float>(bounds.xMax)) + ", y_max: " +
           fixedThree(static_cast<float>(bounds.yMax)) + "]";
}

std::string linePath(const Point2d& start, const Point2d& end)
{
    return "M" + pathPoint(start) + " L" + pathPoint(end);
}

std::string surrogatePoleElement(const Circle2d& circle,
                                 const Transform2d& transform,
                                 const char* role,
                                 const std::string& stroke,
                                 float strokeWidth,
                                 bool isPoi,
                                 bool isFailFast)
{
    const Point2d center = Apply(transform, circle.center);
    std::ostringstream output;
    output << "<circle class=\"surrogate-pole\" data-pole-role=\""
           << role << "\" cx=\""
           << geometryNumber(center.x) << "\" cy=\""
           << geometryNumber(center.y) << "\" r=\""
           << geometryNumber(circle.radius) << "\" fill=\""
           << (isPoi ? "black" : "none") << "\"";
    if (isPoi)
        output << " fill-opacity=\"0.1\"";
    output << " stroke=\"" << stroke << "\" stroke-width=\""
           << number(strokeWidth) << "\" stroke-opacity=\""
           << number(isPoi || isFailFast ? 0.8F : 0.5F) << "\"";
    if (!isPoi && !isFailFast)
    {
        output << " stroke-dasharray=\"" << number(strokeWidth * 5.0F)
               << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"";
    }
    output << "/>";
    return output.str();
}

bool validOptions(const SvgExportOptions& options) noexcept
{
    return std::isfinite(options.theme.strokeWidthMultiplier) &&
           std::isfinite(static_cast<float>(options.theme.strokeWidthMultiplier));
}
}

JAGUARCORE_API Result<std::string> ExportLayoutSvg(
    const Layout& layout, const SvgExportOptions& options)
{
    return ExportLayoutSvg(layout, options, SvgDebugOptions{});
}

JAGUARCORE_API Result<std::string> ExportLayoutSvg(
    const Layout& layout,
    const SvgExportOptions& options,
    const SvgDebugOptions& debugOptions)
{
    if (!validOptions(options))
    {
        return Result<std::string>::FromError(
            {ErrorCode::SerializationFailure,
             "SVG stroke width multiplier must be finite"});
    }

    const Rect2d bounds = layout.GetContainer().Bounds();
    if (!std::isfinite(bounds.xMin) || !std::isfinite(bounds.yMin) ||
        !std::isfinite(bounds.xMax) || !std::isfinite(bounds.yMax) ||
        bounds.xMin >= bounds.xMax || bounds.yMin >= bounds.yMax)
    {
        return Result<std::string>::FromError(
            {ErrorCode::SerializationFailure, "layout bounds are invalid for SVG export"});
    }

    const Rect2d& originalContainerBounds =
        layout.GetContainer().GetOriginalShape().shape.BoundingBox();
    const float originalContainerHeight =
        static_cast<float>(originalContainerBounds.HeightRustF32());
    const float strokeBoundsResize = originalContainerHeight * 0.01F;
    const auto strokeBounds = originalContainerBounds.ResizeByRustF32(
        strokeBoundsResize, strokeBoundsResize);
    if (!strokeBounds.has_value())
    {
        return Result<std::string>::FromError(
            {ErrorCode::SerializationFailure,
             "layout bounds are invalid for SVG stroke width"});
    }
    const float scaleFactorMinusOne = 1.1F - 1.0F;
    const float viewResizeX =
        static_cast<float>(strokeBounds->WidthRustF32()) * scaleFactorMinusOne /
        2.0F;
    const float viewResizeY =
        static_cast<float>(strokeBounds->HeightRustF32()) * scaleFactorMinusOne /
        2.0F;
    const float viewX = static_cast<float>(strokeBounds->xMin) - viewResizeX;
    const float viewY = static_cast<float>(strokeBounds->yMin) - viewResizeY;
    const float viewXMax = static_cast<float>(strokeBounds->xMax) + viewResizeX;
    const float viewYMax = static_cast<float>(strokeBounds->yMax) + viewResizeY;
    const float viewWidth = viewXMax - viewX;
    const float viewHeight = viewYMax - viewY;
    const float minimumStrokeSide = detail::MinRustF32(
        static_cast<float>(strokeBounds->WidthRustF32()),
        static_cast<float>(strokeBounds->HeightRustF32()));
    const float strokeBase = minimumStrokeSide * 0.001F;
    const float strokeWidth =
        strokeBase * static_cast<float>(options.theme.strokeWidthMultiplier);

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\""
        << number(viewX) << ' ' << number(viewY) << ' ' << number(viewWidth) << ' '
        << number(viewHeight) << "\">\n";
    svg << "  <title>" << xmlEscape(options.title) << "</title>\n";
    svg << "  <g id=\"scene\">\n";

    const Container& container = layout.GetContainer();
    svg << "    <g id=\"container\">\n";
    if (debugOptions.drawCdShapes)
    {
        svg << "    <path class=\"container-cd-display\" data-container-id=\""
            << container.Id() << "\" d=\"" << polygonPath(container.CollisionShape())
            << "\" fill=\"" << color(options.theme.containerFill)
            << "\" stroke=\"black"
            << "\" stroke-width=\"" << number(strokeWidth * 2.0F)
            << "\" fill-rule=\"nonzero\"/>\n";
    }
    else
    {
        svg << "    <path class=\"container-original\" data-container-id=\""
            << container.Id() << "\" d=\""
            << polygonPath(container.GetOriginalShape().shape)
            << "\" fill=\"" << color(options.theme.containerFill)
            << "\" stroke=\"black"
            << "\" stroke-width=\"" << number(strokeWidth * 2.0F)
            << "\" fill-rule=\"nonzero\"/>\n";
    }
    svg << "      <title>" << rustContainerTitle(originalContainerBounds)
        << "</title>\n"
        << "    </g>\n";

    const LayoutSnapshot snapshot = layout.Save();
    const auto uniqueItems = layout.Items();
    svg << "    <g id=\"items\">\n      <defs>\n";
    for (const auto& itemReference : uniqueItems)
    {
        const Item& item = itemReference.get();
        const Transform2d localTransform = debugOptions.drawCdShapes
            ? Transform2d::Identity()
            : item.GetOriginalShape().preTransform.InverseRustF32();
        const SimplePolygon& displayShape = debugOptions.drawCdShapes
            ? item.CollisionShape()
            : item.GetOriginalShape().shape;
        const char* displayClass = debugOptions.drawCdShapes
            ? "item-cd-display"
            : "item-original";
        SvgColor itemFill = options.theme.itemFill;
        if (const std::optional<std::size_t> minQuality = item.MinQuality();
            minQuality.has_value())
        {
            if (*minQuality >= options.theme.qualityZoneFills.size())
            {
                return Result<std::string>::FromError(
                    {ErrorCode::SerializationFailure,
                     "item minimum quality is outside the SVG theme range"});
            }
            itemFill = blendColors(
                itemFill, options.theme.qualityZoneFills[*minQuality]);
        }
        svg << "        <g id=\"item_" << item.ExternalId() << "\">\n"
            << "          <path class=\"" << displayClass << "\" d=\""
            << polygonPath(displayShape) << "\" fill=\""
            << color(itemFill)
            << "\" fill-opacity=\"0.5\" fill-rule=\"nonzero\" stroke=\"black\" stroke-width=\""
            << number(strokeWidth) << "\"/>\n"
            << "        </g>\n";
        if (debugOptions.highlightCdShapes)
        {
            const char* collisionClass = debugOptions.drawCdShapes
                ? "item-cd-highlight"
                : "item-collision";
            svg << "        <g id=\"cd_shape_" << item.ExternalId() << "\">\n"
                << "          <path class=\"" << collisionClass << "\" d=\""
                << polygonPath(item.CollisionShape(), localTransform)
                << "\" fill=\"none\" stroke=\"black\" stroke-width=\""
                << number(strokeWidth * 0.5F)
                << "\" stroke-opacity=\"0.3\" stroke-dasharray=\""
                << number(strokeWidth) << ' ' << number(strokeWidth * 2.0F)
                << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
            if (debugOptions.drawCdShapes)
            {
                for (const Point2d& vertex : item.CollisionShape().Vertices())
                {
                    svg << "          <circle class=\"item-cd-vertex\" cx=\""
                        << geometryNumber(vertex.x) << "\" cy=\""
                        << geometryNumber(vertex.y)
                        << "\" r=\"" << number(strokeWidth * 0.5F)
                        << "\" fill=\"cyan\" fill-opacity=\"0.8\"/>\n";
                }
            }
            svg << "        </g>\n";
        }
        if (options.drawSurrogates)
        {
            const Surrogate* itemSurrogate =
                item.CollisionShape().GetSurrogate();
            if (itemSurrogate != nullptr)
            {
                const Transform2d surrogateLocalTransform =
                    debugOptions.drawCdShapes
                        ? Transform2d::Identity()
                        : item.GetOriginalShape().preTransform.InverseRustF32();
                const auto transformed =
                    itemSurrogate->TransformedRustF32(surrogateLocalTransform);
                if (!transformed.HasValue())
                    return Result<std::string>::FromError(
                        transformed.GetError());

                svg << "        <g id=\"surrogate_" << item.ExternalId()
                    << "\">\n";
                const std::vector<Circle2d>& poles =
                    transformed.Value().Poles();
                const std::size_t failFastPoleCount =
                    transformed.Value().FailFastPoleCount();
                for (std::size_t index = 0; index < poles.size(); ++index)
                {
                    const bool isPoi = index == 0U;
                    const bool isFailFast = index < failFastPoleCount;
                    const char* poleRole = isPoi
                        ? "poi"
                        : (isFailFast ? "fail-fast" : "ordinary");
                    svg << "          "
                        << surrogatePoleElement(
                               poles[index], Transform2d::Identity(), poleRole,
                               color(options.theme.surrogateStroke), strokeWidth,
                               isPoi, isFailFast)
                        << "\n";
                }
                for (const Edge2d& pier : transformed.Value().Piers())
                {
                    svg << "          <path class=\"surrogate-pier\" d=\""
                        << linePath(pier.start, pier.end)
                        << "\" fill=\"none\" stroke=\""
                        << color(options.theme.surrogateStroke)
                        << "\" stroke-width=\"" << number(strokeWidth)
                        << "\" stroke-opacity=\"0.8\"/>\n";
                }
                svg << "        </g>\n";
            }
        }
    }
    svg << "      </defs>\n";
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        const PlacedItem& placedItem = placement.placedItem;
        const Item& item = placedItem.GetItem();
        const Transform2d externalTransform =
            item.GetOriginalShape().preTransform.FollowedByRustF32(
                placedItem.Transform());
        const Transform2d displayTransform = debugOptions.drawCdShapes
            ? placedItem.Transform()
            : externalTransform;
        svg << "      <use href=\"#item_" << item.ExternalId()
            << "\" transform=\"" << transformAttribute(displayTransform)
            << "\" data-item-id=\"" << item.ExternalId()
            << "\" data-rotation-degrees=\""
            << number(detail::ToDegreesRustF32(externalTransform.rotationRad))
            << "\" data-translation-x=\""
            << geometryNumber(externalTransform.translation.x)
            << "\" data-translation-y=\""
            << geometryNumber(externalTransform.translation.y) << "\">\n"
            << "        <title>item, id: " << item.ExternalId()
            << ", transf: [" << rustTransformLabel(displayTransform) << "]"
            << "</title>\n"
            << "      </use>\n";
    }
    svg << "    </g>\n";
    svg << "    <g id=\"quality_zones\">\n";
    const std::vector<QualityZone>& qualityZones = debugOptions.drawCdShapes
        ? container.QualityZones()
        : container.OriginalQualityZones();
    for (std::size_t quality = options.theme.qualityZoneFills.size();
         quality-- > 0;)
    {
        const auto zone = std::find_if(
            qualityZones.begin(), qualityZones.end(),
            [quality](const QualityZone& candidate) {
                return candidate.quality == quality;
            });
        if (zone == qualityZones.end())
            continue;

        const SvgColor fill = options.theme.qualityZoneFills[quality];
        const SvgColor stroke = halfBrightness(fill);
        for (const SimplePolygon& shape : zone->shapes)
        {
            svg << "      <path class=\"quality-zone\" data-quality=\""
                << quality << "\" d=\"" << polygonPath(shape)
                << "\" fill=\"" << color(fill)
                << "\" fill-opacity=\"0.50\" stroke=\"" << color(stroke)
                << "\" stroke-width=\"" << number(strokeWidth * 2.0F)
                << "\" stroke-opacity=\""
                << number(static_cast<float>(
                       options.theme.qualityZoneStrokeOpacity))
                << "\" stroke-dasharray=\"" << number(strokeWidth * 5.0F)
                << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
                << "        <title>quality zone, q: " << quality
                << "</title>\n"
                << "      </path>\n";
        }
    }
    svg << "    </g>\n";

    svg << "    <g id=\"optionals\">\n";
    if (debugOptions.highlightCdShapes)
    {
        svg << "    <g id=\"highlight_cd_shapes\">\n";
        for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        {
            const PlacedItem& placedItem = placement.placedItem;
            const Item& item = placedItem.GetItem();
            const Transform2d externalTransform =
                item.GetOriginalShape().preTransform.FollowedByRustF32(
                    placedItem.Transform());
            const Transform2d displayTransform = debugOptions.drawCdShapes
                ? placedItem.Transform()
                : externalTransform;
            svg << "      <use href=\"#cd_shape_" << item.ExternalId()
                << "\" transform=\"" << transformAttribute(displayTransform)
                << "\"/>\n";
        }
        const char* containerCollisionClass = debugOptions.drawCdShapes
            ? "container-cd-highlight"
            : "container-collision";
        svg << "      <path class=\"" << containerCollisionClass
            << "\" data-container-id=\"" << container.Id()
            << "\" d=\"" << polygonPath(container.CollisionShape())
            << "\" fill=\"none\" stroke=\"black\" stroke-width=\""
            << number(strokeWidth * 0.5F)
            << "\" stroke-opacity=\"0.3\" stroke-dasharray=\""
            << number(strokeWidth) << ' ' << number(strokeWidth * 2.0F)
            << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        svg << "    </g>\n";
    }

    svg << "      <g id=\"surrogates\">\n";
    if (options.drawSurrogates)
    {
        for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        {
            const PlacedItem& placedItem = placement.placedItem;
            const Item& item = placedItem.GetItem();
            if (item.CollisionShape().GetSurrogate() == nullptr)
                continue;
            const Transform2d externalTransform =
                item.GetOriginalShape().preTransform.FollowedByRustF32(
                    placedItem.Transform());
            const Transform2d displayTransform = debugOptions.drawCdShapes
                ? placedItem.Transform()
                : externalTransform;
            svg << "        <g class=\"surrogate\" data-item-id=\""
                << item.ExternalId() << "\">\n";
            svg << "          <use href=\"#surrogate_" << item.ExternalId()
                << "\" transform=\"" << transformAttribute(displayTransform)
                << "\"/>\n"
                << "        </g>\n";
        }
    }
    svg << "      </g>\n";

    if (debugOptions.drawQuadtree)
    {
        const std::vector<SvgDebugAccess::QuadtreeCell> cells =
            SvgDebugAccess::QuadtreeCells(layout);
        std::array<std::ostringstream, 3> paths;
        std::array<bool, 3> hasPath{};
        for (const SvgDebugAccess::QuadtreeCell& cell : cells)
        {
            std::size_t index = 0U;
            switch (cell.presence)
            {
            case HazardPresence::Entire: index = 0U; break;
            case HazardPresence::Partial: index = 1U; break;
            case HazardPresence::None: index = 2U; break;
            }
            const Rect2d& rect = cell.bounds;
            if (hasPath[index])
                paths[index] << ' ';
            paths[index] << "M" << geometryNumber(rect.xMin) << ','
                         << geometryNumber(rect.yMin) << " L"
                         << geometryNumber(rect.xMax) << ','
                         << geometryNumber(rect.yMin) << " L"
                         << geometryNumber(rect.xMax) << ','
                         << geometryNumber(rect.yMax) << " L"
                         << geometryNumber(rect.xMin) << ','
                         << geometryNumber(rect.yMax) << " z";
            hasPath[index] = true;
        }
        svg << "    <g id=\"quadtree\">\n"
            << "      <path class=\"quadtree-cell quadtree-entire\" d=\""
            << paths[0].str()
            << "\" fill=\"red\" fill-rule=\"nonzero\" stroke=\"black\" stroke-width=\""
            << number(strokeWidth * 0.25F) << "\" fill-opacity=\"0.6\"/>\n"
            << "      <path class=\"quadtree-cell quadtree-partial\" d=\""
            << paths[1].str()
            << "\" fill=\"none\" fill-rule=\"nonzero\" stroke=\"black\" stroke-width=\""
            << number(strokeWidth * 0.25F) << "\" fill-opacity=\"0.3\"/>\n"
            << "      <path class=\"quadtree-cell quadtree-none\" d=\""
            << paths[2].str()
            << "\" fill=\"green\" stroke=\"black\" stroke-width=\""
            << number(strokeWidth * 0.25F) << "\" fill-opacity=\"0.6\"/>\n"
            << "    </g>\n";
    }

    if (options.highlightCollisions)
    {
        CollisionEnginePtr collisionEngine = container.CreateCollisionEngine();
        if (!collisionEngine)
        {
            return Result<std::string>::FromError(
                {ErrorCode::CollisionEngineFailure,
                 "could not create collision engine for SVG export"});
        }
        const Result<void> restored = collisionEngine->Restore(snapshot.cdeSnapshot);
        if (!restored.HasValue())
            return Result<std::string>::FromError(restored.GetError());

        const float highlightWidth = strokeWidth * 4.0F;
        const auto appendHighlightLine = [&svg, &options, highlightWidth](
                                            const Point2d& start,
                                            const Point2d& end) {
            svg << "      <path class=\"collision-highlight\" d=\""
                << linePath(start, end) << "\" fill=\"none\" stroke=\""
                << color(options.theme.collisionHighlight)
                << "\" stroke-opacity=\"0.75\" stroke-width=\""
                << number(highlightWidth) << "\" stroke-dasharray=\""
                << number(highlightWidth) << ' '
                << number(highlightWidth * 2.0F)
                << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        };

        svg << "    <g id=\"collision_lines\">\n";
        for (const LayoutPlacementSnapshot& placement : snapshot.placements)
        {
            const SimplePolygon& placedShape =
                placement.placedItem.CollisionShape();
            const Point2d placedCenter =
                placedShape.PointOfInaccessibility().center;
            const auto collisions =
                collisionEngine->CollectPolygonCollisions(placedShape);
            for (const HazardEntity& collision : collisions)
            {
                switch (collision.kind)
                {
                case HazardKind::PlacedItem:
                {
                    if (collision.placedItemHandle == placement.handle)
                        break;
                    const bool currentHandleIsGreater =
                        placement.handle.index > collision.placedItemHandle.index ||
                        (placement.handle.index == collision.placedItemHandle.index &&
                         placement.handle.generation >
                             collision.placedItemHandle.generation);
                    if (!currentHandleIsGreater)
                        break;

                    const auto other = std::find_if(
                        snapshot.placements.begin(), snapshot.placements.end(),
                        [&collision](const LayoutPlacementSnapshot& candidate) {
                            return candidate.handle == collision.placedItemHandle;
                        });
                    if (other == snapshot.placements.end())
                    {
                        return Result<std::string>::FromError(
                            {ErrorCode::SnapshotMismatch,
                             "collision references a missing placement during SVG export"});
                    }
                    appendHighlightLine(
                        placedCenter,
                        other->placedItem.CollisionShape()
                            .PointOfInaccessibility().center);
                    break;
                }
                case HazardKind::Exterior:
                    svg << "      <circle class=\"collision-highlight\" cx=\""
                        << geometryNumber(placedCenter.x) << "\" cy=\""
                        << geometryNumber(placedCenter.y) << "\" r=\""
                        << number(strokeWidth * 3.0F) << "\" fill=\""
                        << color(options.theme.collisionHighlight)
                        << "\"/>\n";
                    break;
                case HazardKind::Hole:
                case HazardKind::InferiorQualityZone:
                {
                    const std::size_t quality = collision.kind == HazardKind::Hole
                        ? 0U
                        : collision.quality;
                    const std::optional<std::size_t> requiredQuality =
                        placement.placedItem.GetItem().MinQuality();
                    if (collision.kind == HazardKind::InferiorQualityZone &&
                        requiredQuality.has_value() &&
                        quality >= *requiredQuality)
                    {
                        break;
                    }

                    const auto zone = std::find_if(
                        container.QualityZones().begin(),
                        container.QualityZones().end(),
                        [quality](const QualityZone& candidate) {
                            return candidate.quality == quality;
                        });
                    if (zone == container.QualityZones().end() ||
                        collision.zoneIndex >= zone->shapes.size())
                    {
                        return Result<std::string>::FromError(
                            {ErrorCode::SnapshotMismatch,
                             "collision references a missing quality zone during SVG export"});
                    }
                    appendHighlightLine(
                        placedCenter,
                        zone->shapes[collision.zoneIndex]
                            .PointOfInaccessibility().center);
                    break;
                }
                }
            }
        }
        svg << "    </g>\n";
    }

    svg << "    </g>\n";

    const bool feasible = layout.IsFeasible();
    svg << "    <g id=\"collision-debug\" data-layout-feasible=\""
        << (feasible ? "true" : "false") << "\" data-enabled=\""
        << (options.highlightCollisions ? "true" : "false") << "\"/>\n";
    svg << "  </g>\n";

    const float originalWidth =
        static_cast<float>(originalContainerBounds.WidthRustF32());
    const float originalHeight =
        static_cast<float>(originalContainerBounds.HeightRustF32());
    const float labelSide = detail::MinRustF32(originalWidth, originalHeight);
    const float labelY = static_cast<float>(originalContainerBounds.yMin) -
                         (0.5F * 0.025F) * labelSide;
    std::ostringstream label;
    label << std::fixed << std::setprecision(3)
          << "h: " << originalHeight << " | w: " << originalWidth << " | d: "
          << (static_cast<float>(layout.Density()) * 100.0F) << "% | "
          << options.title;
    svg << "  <text font-family=\"monospace\" font-size=\""
        << number(labelSide * 0.025F) << "\" font-weight=\"500\" x=\""
        << number(static_cast<float>(originalContainerBounds.xMin))
        << "\" y=\"" << number(labelY) << "\">\n"
        << xmlEscape(label.str()) << "\n"
        << "  </text>\n";

    if (options.includeAxes)
    {
        const float xAxisY = std::clamp(0.0F, viewY, viewY + viewHeight);
        const float yAxisX = std::clamp(0.0F, viewX, viewX + viewWidth);
        svg << "  <g id=\"axes\" stroke=\""
            << color(options.theme.axisStroke) << "\" stroke-width=\""
            << number(strokeWidth * 0.75F) << "\" fill=\"none\">\n";
        svg << "    <path d=\"M" << pathPoint({viewX, xAxisY})
            << " L" << pathPoint({viewX + viewWidth, xAxisY}) << "\"/>\n";
        svg << "    <path d=\"M" << pathPoint({yAxisX, viewY})
            << " L" << pathPoint({yAxisX, viewY + viewHeight}) << "\"/>\n";
        svg << "  </g>\n";
    }

    svg << "</svg>\n";
    return Result<std::string>::FromValue(svg.str());
}
}
