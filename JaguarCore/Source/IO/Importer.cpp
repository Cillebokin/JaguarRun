#include "JaguarCore/IO/Importer.h"

#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace jaguar
{
namespace
{
constexpr std::size_t MAX_ANGLES = 65536;
constexpr std::size_t MAX_QUALITIES = 10;

std::uint32_t orderedFloatBits(float value) noexcept
{
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x80000000U) != 0U)
        return ~bits + 1U;
    return bits | 0x80000000U;
}

bool rustFpaEqual(float first, float second) noexcept
{
    if (first == second)
        return true;
    if (!std::isfinite(first) || !std::isfinite(second))
        return false;
    if (std::abs(first - second) <= std::numeric_limits<float>::epsilon())
        return true;

    const std::uint32_t firstBits = orderedFloatBits(first);
    const std::uint32_t secondBits = orderedFloatBits(second);
    const std::uint32_t difference = firstBits > secondBits
        ? firstBits - secondBits
        : secondBits - firstBits;
    return difference <= 4U;
}

bool rustFpaEqual(const Point2d& first, const Point2d& second) noexcept
{
    return rustFpaEqual(static_cast<float>(first.x),
                        static_cast<float>(second.x)) &&
           rustFpaEqual(static_cast<float>(first.y),
                        static_cast<float>(second.y));
}

Result<SimplePolygon> invalidShape(const char* message)
{
    return Result<SimplePolygon>::FromError(
        {ErrorCode::UnsupportedShape, message});
}

bool normalizedRectangleBounds(const ExternalRectangle& rectangle,
                               Rect2d& bounds) noexcept
{
    const float xMin = static_cast<float>(rectangle.xMin);
    const float yMin = static_cast<float>(rectangle.yMin);
    const float width = static_cast<float>(rectangle.width);
    const float height = static_cast<float>(rectangle.height);
    if (!std::isfinite(xMin) || !std::isfinite(yMin) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0F || height <= 0.0F)
        return false;

    const float xMax = xMin + width;
    const float yMax = yMin + height;
    if (!std::isfinite(xMax) || !std::isfinite(yMax) ||
        !(xMin < xMax && yMin < yMax))
        return false;

    bounds = Rect2d{static_cast<double>(xMin), static_cast<double>(yMin),
                    static_cast<double>(xMax), static_cast<double>(yMax)};
    return true;
}

bool finiteRectangle(const ExternalRectangle& rectangle) noexcept
{
    Rect2d bounds{};
    return normalizedRectangleBounds(rectangle, bounds);
}

float normalizeDegreesRustF32(float degrees) noexcept
{
    float normalized = std::fmod(degrees, 360.0F);
    if (normalized < 0.0F)
        normalized += 360.0F;
    return std::fmod(normalized, 360.0F);
}

double radiansRustF32(float degrees) noexcept
{
    constexpr float pi = 3.14159265358979323846F;
    return static_cast<double>(degrees * (pi / 180.0F));
}

Result<std::size_t> toSize(std::uint64_t value, const char* field)
{
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        return Result<std::size_t>::FromError(
            {ErrorCode::InvalidInput, std::string(field) + " exceeds size_t"});
    }
    return Result<std::size_t>::FromValue(static_cast<std::size_t>(value));
}

bool validMinItemSeparation(double separation) noexcept
{
    return std::isfinite(separation) && separation >= 0.0 &&
           std::isfinite(static_cast<float>(separation));
}

ImporterConfig withSeparation(const ImporterConfig& source, double separation)
{
    ImporterConfig result = source;
    result.minItemSeparation = separation;
    result.shapeModifyConfig.offset =
        separation > 0.0 ? std::optional<double>(separation * 0.5)
                         : std::nullopt;
    return result;
}

ShapeModifyConfig problemShapeConfig(std::optional<double> offset)
{
    ShapeModifyConfig config{};
    config.offset = offset;
    return config;
}

Result<Transform2d> externalToInternal(const ExternalTransformation& external,
                                       const OriginalShape& original)
{
    const float rotationDegrees = static_cast<float>(external.rotationDegrees);
    const float translationX = static_cast<float>(external.translation.x);
    const float translationY = static_cast<float>(external.translation.y);
    if (!std::isfinite(rotationDegrees) || !std::isfinite(translationX) ||
        !std::isfinite(translationY))
    {
        return Result<Transform2d>::FromError(
            {ErrorCode::SnapshotMismatch,
             "external solution contains a non-finite transformation"});
    }
    constexpr float pi = 3.14159265358979323846F;
    const float rotationRadians = rotationDegrees * (pi / 180.0F);
    const Transform2d externalTransform = Transform2d::FromRotationTranslation(
        static_cast<double>(rotationRadians),
        {static_cast<double>(translationX), static_cast<double>(translationY)});
    return Result<Transform2d>::FromValue(
        original.preTransform.InverseRustF32().FollowedByRustF32(externalTransform));
}

Result<std::vector<Item>> importPositiveItems(
    const Importer& importer,
    const std::vector<ExternalDemandItem>& externalItems,
    std::vector<std::size_t>& demands)
{
    std::set<ItemId> ids;
    std::vector<Item> items;
    demands.clear();
    for (const ExternalDemandItem& external : externalItems)
    {
        if (!ids.insert(external.item.id).second)
        {
            return Result<std::vector<Item>>::FromError(
                {ErrorCode::InvalidInput, "external item IDs must be unique"});
        }
    }

    for (const ExternalDemandItem& external : externalItems)
    {
        if (external.demand == 0)
            continue;
        const auto demand = toSize(external.demand, "item demand");
        if (!demand.HasValue())
            return Result<std::vector<Item>>::FromError(demand.GetError());
        const auto imported = importer.ImportItem(external.item, items.size());
        if (!imported.HasValue())
            return Result<std::vector<Item>>::FromError(imported.GetError());
        items.push_back(imported.Value());
        demands.push_back(demand.Value());
    }
    if (items.empty())
    {
        return Result<std::vector<Item>>::FromError(
            {ErrorCode::InvalidInput, "instance must have positive item demand"});
    }
    return Result<std::vector<Item>>::FromValue(std::move(items));
}

template <typename Instance>
const Item* findItemByExternalId(const Instance& instance,
                                 ItemId id,
                                 ItemIndex& index) noexcept
{
    for (std::size_t i = 0; i < instance.ItemCount(); ++i)
    {
        if (instance.GetItem(i).ExternalId() == id)
        {
            index = i;
            return &instance.GetItem(i);
        }
    }
    return nullptr;
}

Result<ExternalRectangle> rectangleData(const ExternalShape& shape)
{
    const auto* rectangle = std::get_if<ExternalRectangle>(&shape.value);
    if (!rectangle || !finiteRectangle(*rectangle))
    {
        return Result<ExternalRectangle>::FromError(
            {ErrorCode::SnapshotMismatch,
             "MSP solution containers must be finite rectangles"});
    }
    return Result<ExternalRectangle>::FromValue(*rectangle);
}

bool near(double lhs, double rhs) noexcept
{
    return NumericPolicy{}.NearlyEqual(lhs, rhs);
}
}

Importer::Importer(ImporterConfig config)
    : m_Config(std::move(config))
{
}

ShapeModifyConfig Importer::ShapeConfig() const
{
    ShapeModifyConfig config = m_Config.shapeModifyConfig;
    if (m_Config.minItemSeparation > 0.0)
        config.offset = m_Config.minItemSeparation * 0.5;
    return config;
}

Result<SimplePolygon> Importer::ImportSimplePolygon(
    const std::vector<Point2d>& inputPoints) const
{
    std::vector<Point2d> points = inputPoints;
    for (Point2d& point : points)
    {
        point.x = static_cast<double>(static_cast<float>(point.x));
        point.y = static_cast<double>(static_cast<float>(point.y));
    }
    if (points.size() > 1 && points.front() == points.back())
        points.pop_back();

    std::vector<std::size_t> duplicateVertices;
    duplicateVertices.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1U) % points.size();
        if (rustFpaEqual(points[i], points[next]))
            duplicateVertices.push_back(i);
    }
    std::sort(duplicateVertices.rbegin(), duplicateVertices.rend());
    for (const std::size_t index : duplicateVertices)
    {
        if (index < points.size())
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
    }

    return SimplePolygon::Create(std::move(points));
}

Result<SimplePolygon> Importer::ImportItemShape(const ExternalShape& shape) const
{
    if (const auto* rectangle = std::get_if<ExternalRectangle>(&shape.value))
    {
        Rect2d bounds{};
        if (!normalizedRectangleBounds(*rectangle, bounds))
        {
            return Result<SimplePolygon>::FromError(
                {ErrorCode::InvalidInput, "invalid external rectangle"});
        }
        return SimplePolygon::Create({
            {bounds.xMin, bounds.yMin},
            {bounds.xMax, bounds.yMin},
            {bounds.xMax, bounds.yMax},
            {bounds.xMin, bounds.yMax}});
    }
    if (const auto* polygon = std::get_if<ExternalSimplePolygon>(&shape.value))
        return ImportSimplePolygon(polygon->points);
    if (const auto* polygon = std::get_if<ExternalPolygon>(&shape.value))
    {
        if (!polygon->holes.empty())
            return invalidShape("item polygons with holes are unsupported");
        return ImportSimplePolygon(polygon->outer);
    }
    return invalidShape("item multipolygons are unsupported");
}

Result<SimplePolygon> Importer::ImportContainerOuter(const ExternalShape& shape) const
{
    if (const auto* rectangle = std::get_if<ExternalRectangle>(&shape.value))
    {
        Rect2d bounds{};
        if (!normalizedRectangleBounds(*rectangle, bounds))
        {
            return Result<SimplePolygon>::FromError(
                {ErrorCode::InvalidInput, "invalid external rectangle"});
        }
        return SimplePolygon::Create({
            {bounds.xMin, bounds.yMin},
            {bounds.xMax, bounds.yMin},
            {bounds.xMax, bounds.yMax},
            {bounds.xMin, bounds.yMax}});
    }
    if (const auto* polygon = std::get_if<ExternalSimplePolygon>(&shape.value))
        return ImportSimplePolygon(polygon->points);
    if (const auto* polygon = std::get_if<ExternalPolygon>(&shape.value))
        return ImportSimplePolygon(polygon->outer);
    return invalidShape("container multipolygons are unsupported");
}

Result<SimplePolygon> Importer::ImportZoneShape(const ExternalShape& shape) const
{
    if (std::holds_alternative<ExternalPolygon>(shape.value))
        return invalidShape("quality zone polygons are unsupported");
    if (std::holds_alternative<ExternalMultiPolygon>(shape.value))
        return invalidShape("quality zone multipolygons are unsupported");
    return ImportItemShape(shape);
}

Result<RotationRange> Importer::ImportRotation(const RotationPolicy& rotation) const
{
    if (rotation.kind == RotationPolicyKind::Continuous)
        return Result<RotationRange>::FromValue(
            RotationRange{RotationMode::Continuous, {}});
    if (rotation.kind == RotationPolicyKind::None)
        return Result<RotationRange>::FromValue(
            RotationRange{RotationMode::None, {}});

    std::vector<float> degrees;
    if (rotation.kind == RotationPolicyKind::Discrete)
    {
        if (rotation.anglesDegrees.empty() ||
            rotation.anglesDegrees.size() > MAX_ANGLES)
        {
            return Result<RotationRange>::FromError(
                {ErrorCode::InvalidInput,
                 "discrete rotation requires 1..=65536 angles"});
        }
        degrees.reserve(rotation.anglesDegrees.size());
        for (double angle : rotation.anglesDegrees)
        {
            const float angleF32 = static_cast<float>(angle);
            if (!std::isfinite(angleF32))
            {
                return Result<RotationRange>::FromError(
                    {ErrorCode::InvalidInput, "rotation angles must be finite"});
            }
            degrees.push_back(normalizeDegreesRustF32(angleF32));
        }
    }
    else
    {
        const float stepF32 = static_cast<float>(rotation.stepDegrees);
        if (!std::isfinite(stepF32) || stepF32 <= 0.0F || stepF32 > 360.0F)
        {
            return Result<RotationRange>::FromError(
                {ErrorCode::InvalidInput,
                 "rotation step must be finite and in (0, 360]"});
        }
        const double step = static_cast<double>(stepF32);
        const double count = std::round(360.0 / step);
        const double tolerance = 360.0 * std::numeric_limits<float>::epsilon();
        if (count < 1.0 || count > static_cast<double>(MAX_ANGLES) ||
            std::abs(count * step - 360.0) > tolerance)
        {
            return Result<RotationRange>::FromError(
                {ErrorCode::InvalidInput,
                 "rotation step must divide 360 into at most 65536 angles"});
        }
        degrees.reserve(static_cast<std::size_t>(count));
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
        {
            degrees.push_back(static_cast<float>(
                360.0 * static_cast<double>(i) / count));
        }
    }

    std::sort(degrees.begin(), degrees.end());
    degrees.erase(std::unique(degrees.begin(), degrees.end()), degrees.end());
    if (degrees.size() == 1 && degrees.front() == 0.0F)
        return Result<RotationRange>::FromValue(
            RotationRange{RotationMode::None, {}});

    std::vector<double> radians;
    radians.reserve(degrees.size());
    for (float angle : degrees)
        radians.push_back(radiansRustF32(angle));
    return Result<RotationRange>::FromValue(
        RotationRange{RotationMode::Discrete, std::move(radians)});
}

JAGUARCORE_API Result<Item> Importer::ImportItem(
    const ExternalItem& external,
    ItemIndex internalIndex) const
{
    if (!validMinItemSeparation(m_Config.minItemSeparation))
    {
        return Result<Item>::FromError(
            {ErrorCode::InvalidInput, "minimum item separation must be finite and nonnegative"});
    }

    auto shape = ImportItemShape(external.shape);
    if (!shape.HasValue())
        return Result<Item>::FromError(shape.GetError());
    auto rotation = ImportRotation(external.rotation);
    if (!rotation.HasValue())
        return Result<Item>::FromError(rotation.GetError());

    const Point2d center = shape.Value().Centroid();
    OriginalShape original{
        std::move(shape.Value()),
        Transform2d::FromRotationTranslation(0.0, {-center.x, -center.y}),
        ShapeModifyMode::Inflate,
        ShapeConfig()};
    return Item::Create(internalIndex, external.id, std::move(original),
                        m_Config.cdeConfig.itemSurrogateConfig,
                        external.minQuality, std::move(rotation.Value()));
}

JAGUARCORE_API Result<Container> Importer::ImportContainer(
    const ExternalContainer& external) const
{
    if (!validMinItemSeparation(m_Config.minItemSeparation))
    {
        return Result<Container>::FromError(
            {ErrorCode::InvalidInput, "minimum item separation must be finite and nonnegative"});
    }

    auto outer = ImportContainerOuter(external.shape);
    if (!outer.HasValue())
        return Result<Container>::FromError(outer.GetError());

    std::vector<std::vector<SimplePolygon>> grouped(MAX_QUALITIES);
    std::vector<SimplePolygon> holes;
    if (const auto* polygon = std::get_if<ExternalPolygon>(&external.shape.value))
    {
        for (const auto& hole : polygon->holes)
        {
            auto holeShape = ImportSimplePolygon(hole);
            if (!holeShape.HasValue())
                return Result<Container>::FromError(holeShape.GetError());
            holes.push_back(std::move(holeShape.Value()));
        }
    }

    for (const ExternalQualityZone& zone : external.zones)
    {
        if (zone.quality >= MAX_QUALITIES)
        {
            return Result<Container>::FromError(
                {ErrorCode::InvalidInput, "quality zone is outside the supported range"});
        }
        auto zoneShape = ImportZoneShape(zone.shape);
        if (!zoneShape.HasValue())
            return Result<Container>::FromError(zoneShape.GetError());
        grouped[zone.quality].push_back(std::move(zoneShape.Value()));
    }

    for (SimplePolygon& hole : holes)
        grouped[0].push_back(std::move(hole));

    std::vector<QualityZone> zones;
    for (std::size_t quality = 0; quality < grouped.size(); ++quality)
    {
        if (!grouped[quality].empty())
        {
            std::vector<SimplePolygon> convertedShapes;
            convertedShapes.reserve(grouped[quality].size());
            for (SimplePolygon& shape : grouped[quality])
            {
                OriginalShape zoneOriginal{
                    std::move(shape), Transform2d::Identity(),
                    ShapeModifyMode::Inflate, ShapeConfig()};
                auto converted = zoneOriginal.ConvertToInternal();
                if (!converted.HasValue())
                    return Result<Container>::FromError(converted.GetError());
                convertedShapes.push_back(std::move(converted.Value()));
            }
            zones.push_back({quality, std::move(convertedShapes)});
        }
    }

    OriginalShape originalOuter{
        std::move(outer.Value()), Transform2d::Identity(),
        ShapeModifyMode::Deflate, ShapeConfig()};
    return Container::Create(external.id, std::move(originalOuter),
                             std::move(zones), m_Config.cdeConfig);
}

JAGUARCORE_API Result<Item> Importer::ImportItemJson(
    const std::string& json, ItemIndex internalIndex) const
{
    const auto parsed = ParseExternalItemJson(json);
    if (!parsed.HasValue())
        return Result<Item>::FromError(parsed.GetError());
    return ImportItem(parsed.Value(), internalIndex);
}

JAGUARCORE_API Result<Container> Importer::ImportContainerJson(
    const std::string& json) const
{
    const auto parsed = ParseExternalContainerJson(json);
    if (!parsed.HasValue())
        return Result<Container>::FromError(parsed.GetError());
    return ImportContainer(parsed.Value());
}

JAGUARCORE_API Result<SPInstance> Importer::ImportSPInstance(
    const ExternalSPInstance& external) const
{
    if (!validMinItemSeparation(external.minItemSeparation))
    {
        return Result<SPInstance>::FromError(
            {ErrorCode::InvalidInput,
             "minimum item separation must be finite and nonnegative"});
    }
    const Importer scoped(withSeparation(m_Config, external.minItemSeparation));
    std::vector<std::size_t> demands;
    const auto items = importPositiveItems(scoped, external.items, demands);
    if (!items.HasValue())
        return Result<SPInstance>::FromError(items.GetError());
    return SPInstance::Create(items.Value(), std::move(demands),
                              external.stripHeight, m_Config.cdeConfig,
                              problemShapeConfig(scoped.ShapeConfig().offset));
}

JAGUARCORE_API Result<SPSolution> Importer::ImportSPSolution(
    const SPInstance& instance, const ExternalSPSolution& external) const
{
    if (!std::isfinite(external.stripWidth) || external.stripWidth <= 0.0)
    {
        return Result<SPSolution>::FromError(
            {ErrorCode::SnapshotMismatch, "SP solution strip width is invalid"});
    }
    auto problem = SPProblem::Create(instance);
    if (!problem.HasValue())
        return Result<SPSolution>::FromError(problem.GetError());
    auto& value = problem.Value();
    const auto changed = value.ChangeStripWidth(external.stripWidth);
    if (!changed.HasValue())
        return Result<SPSolution>::FromError(changed.GetError());
    for (const ExternalPlacedItem& placed : external.layout.placedItems)
    {
        ItemIndex itemIndex = 0;
        const Item* item = findItemByExternalId(instance, placed.itemId, itemIndex);
        if (!item)
        {
            return Result<SPSolution>::FromError(
                {ErrorCode::SnapshotMismatch, "SP solution references an unknown item ID"});
        }
        const auto transform = externalToInternal(
            placed.transformation, item->GetOriginalShape());
        if (!transform.HasValue())
            return Result<SPSolution>::FromError(transform.GetError());
        const auto result = value.PlaceItem({itemIndex, transform.Value()});
        if (!result.HasValue())
            return Result<SPSolution>::FromError(result.GetError());
    }
    return Result<SPSolution>::FromValue(value.Save());
}

JAGUARCORE_API Result<SPInstance> Importer::ImportSPInstanceJson(
    const std::string& json) const
{
    const auto parsed = ParseExternalSPInstanceJson(json);
    if (!parsed.HasValue())
        return Result<SPInstance>::FromError(parsed.GetError());
    return ImportSPInstance(parsed.Value());
}

JAGUARCORE_API Result<SPSolution> Importer::ImportSPSolutionJson(
    const SPInstance& instance, const std::string& json) const
{
    const auto parsed = ParseExternalSPSolutionJson(json);
    if (!parsed.HasValue())
        return Result<SPSolution>::FromError(parsed.GetError());
    return ImportSPSolution(instance, parsed.Value());
}

JAGUARCORE_API Result<BPInstance> Importer::ImportBPInstance(
    const ExternalBPInstance& external) const
{
    if (!validMinItemSeparation(external.minItemSeparation))
    {
        return Result<BPInstance>::FromError(
            {ErrorCode::InvalidInput,
             "minimum item separation must be finite and nonnegative"});
    }
    const Importer scoped(withSeparation(m_Config, external.minItemSeparation));
    std::vector<std::size_t> demands;
    const auto items = importPositiveItems(scoped, external.items, demands);
    if (!items.HasValue())
        return Result<BPInstance>::FromError(items.GetError());

    std::set<ContainerId> ids;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < external.bins.size(); ++i)
    {
        if (!ids.insert(external.bins[i].container.id).second)
        {
            return Result<BPInstance>::FromError(
                {ErrorCode::InvalidInput, "external bin IDs must be unique"});
        }
        order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&external](std::size_t lhs, std::size_t rhs) {
        return external.bins[lhs].container.id < external.bins[rhs].container.id;
    });

    std::vector<Bin> bins;
    for (const std::size_t sourceIndex : order)
    {
        const ExternalBin& externalBin = external.bins[sourceIndex];
        if (externalBin.stock == 0)
            continue;
        const auto stock = toSize(externalBin.stock, "bin stock");
        if (!stock.HasValue())
            return Result<BPInstance>::FromError(stock.GetError());
        const auto container = scoped.ImportContainer(externalBin.container);
        if (!container.HasValue())
            return Result<BPInstance>::FromError(container.GetError());
        bins.push_back(Bin{bins.size(), externalBin.container.id,
                           container.Value(), stock.Value(), externalBin.cost});
    }
    if (bins.empty())
    {
        return Result<BPInstance>::FromError(
            {ErrorCode::InvalidInput, "instance must have positive bin stock"});
    }
    return BPInstance::Create(items.Value(), std::move(demands), std::move(bins));
}

JAGUARCORE_API Result<BPSolution> Importer::ImportBPSolution(
    const BPInstance&, const ExternalBPSolution&) const
{
    return Result<BPSolution>::FromError(
        {ErrorCode::UnsupportedOperation,
         "BPP solution import is not implemented in the reference crate"});
}
JAGUARCORE_API Result<BPInstance> Importer::ImportBPInstanceJson(
    const std::string& json) const
{
    const auto parsed = ParseExternalBPInstanceJson(json);
    if (!parsed.HasValue())
        return Result<BPInstance>::FromError(parsed.GetError());
    return ImportBPInstance(parsed.Value());
}

JAGUARCORE_API Result<BPSolution> Importer::ImportBPSolutionJson(
    const BPInstance& instance, const std::string& json) const
{
    const auto parsed = ParseExternalBPSolutionJson(json);
    if (!parsed.HasValue())
        return Result<BPSolution>::FromError(parsed.GetError());
    return ImportBPSolution(instance, parsed.Value());
}

JAGUARCORE_API Result<MSPInstance> Importer::ImportMSPInstance(
    const ExternalMSPInstance& external) const
{
    if (!validMinItemSeparation(external.minItemSeparation))
    {
        return Result<MSPInstance>::FromError(
            {ErrorCode::InvalidInput,
             "minimum item separation must be finite and nonnegative"});
    }
    const Importer scoped(withSeparation(m_Config, external.minItemSeparation));
    std::vector<std::size_t> demands;
    const auto items = importPositiveItems(scoped, external.items, demands);
    if (!items.HasValue())
        return Result<MSPInstance>::FromError(items.GetError());
    return MSPInstance::Create(items.Value(), std::move(demands),
                               external.strips.maxWidth, external.strips.height,
                               m_Config.cdeConfig,
                               problemShapeConfig(scoped.ShapeConfig().offset));
}

JAGUARCORE_API Result<MSPSolution> Importer::ImportMSPSolution(
    const MSPInstance&, const ExternalMSPSolution&) const
{
    return Result<MSPSolution>::FromError(
        {ErrorCode::UnsupportedOperation,
         "MSPP solution import is not implemented in the reference crate"});
}
JAGUARCORE_API Result<MSPInstance> Importer::ImportMSPInstanceJson(
    const std::string& json) const
{
    const auto parsed = ParseExternalMSPInstanceJson(json);
    if (!parsed.HasValue())
        return Result<MSPInstance>::FromError(parsed.GetError());
    return ImportMSPInstance(parsed.Value());
}

JAGUARCORE_API Result<MSPSolution> Importer::ImportMSPSolutionJson(
    const MSPInstance& instance, const std::string& json) const
{
    const auto parsed = ParseExternalMSPSolutionJson(json);
    if (!parsed.HasValue())
        return Result<MSPSolution>::FromError(parsed.GetError());
    return ImportMSPSolution(instance, parsed.Value());
}
}
