#include "JaguarCore/IO/Importer.h"

#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/NumericPolicy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace jaguar
{
namespace
{
constexpr std::size_t MAX_ANGLES = 65536;
constexpr std::size_t MAX_QUALITIES = 10;

Result<SimplePolygon> invalidShape(const char* message)
{
    return Result<SimplePolygon>::FromError(
        {ErrorCode::UnsupportedShape, message});
}

bool finiteRectangle(const ExternalRectangle& rectangle) noexcept
{
    return std::isfinite(rectangle.xMin) && std::isfinite(rectangle.yMin) &&
           std::isfinite(rectangle.width) && std::isfinite(rectangle.height) &&
           rectangle.width > 0.0 && rectangle.height > 0.0 &&
           std::isfinite(rectangle.xMin + rectangle.width) &&
           std::isfinite(rectangle.yMin + rectangle.height);
}

double normalizeDegrees(double degrees) noexcept
{
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    if (normalized == 360.0)
        normalized = 0.0;
    return normalized;
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
    if (!std::isfinite(external.rotationDegrees) ||
        !external.translation.IsFinite())
    {
        return Result<Transform2d>::FromError(
            {ErrorCode::SnapshotMismatch,
             "external solution contains a non-finite transformation"});
    }
    constexpr double pi = 3.141592653589793238462643383279502884;
    const Transform2d externalTransform = Transform2d::FromRotationTranslation(
        external.rotationDegrees * pi / 180.0, external.translation);
    return Result<Transform2d>::FromValue(
        original.preTransform.Inverse().FollowedBy(externalTransform));
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
    if (points.size() > 1 && points.front() == points.back())
        points.pop_back();

    std::vector<Point2d> cleaned;
    cleaned.reserve(points.size());
    const NumericPolicy policy{};
    const double toleranceSquared =
        policy.equalityTolerance * policy.equalityTolerance;
    for (const Point2d& point : points)
    {
        if (cleaned.empty() ||
            cleaned.back().SquaredDistanceTo(point) > toleranceSquared)
            cleaned.push_back(point);
    }
    if (cleaned.size() > 1 && cleaned.front() == cleaned.back())
        cleaned.pop_back();

    return SimplePolygon::Create(std::move(cleaned));
}

Result<SimplePolygon> Importer::ImportItemShape(const ExternalShape& shape) const
{
    if (const auto* rectangle = std::get_if<ExternalRectangle>(&shape.value))
    {
        if (!finiteRectangle(*rectangle))
        {
            return Result<SimplePolygon>::FromError(
                {ErrorCode::InvalidInput, "invalid external rectangle"});
        }
        return SimplePolygon::Create({
            {rectangle->xMin, rectangle->yMin},
            {rectangle->xMin + rectangle->width, rectangle->yMin},
            {rectangle->xMin + rectangle->width,
             rectangle->yMin + rectangle->height},
            {rectangle->xMin, rectangle->yMin + rectangle->height}});
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
        if (!finiteRectangle(*rectangle))
        {
            return Result<SimplePolygon>::FromError(
                {ErrorCode::InvalidInput, "invalid external rectangle"});
        }
        return SimplePolygon::Create({
            {rectangle->xMin, rectangle->yMin},
            {rectangle->xMin + rectangle->width, rectangle->yMin},
            {rectangle->xMin + rectangle->width,
             rectangle->yMin + rectangle->height},
            {rectangle->xMin, rectangle->yMin + rectangle->height}});
    }
    if (const auto* polygon = std::get_if<ExternalSimplePolygon>(&shape.value))
        return ImportSimplePolygon(polygon->points);
    if (const auto* polygon = std::get_if<ExternalPolygon>(&shape.value))
        return ImportSimplePolygon(polygon->outer);
    return invalidShape("container multipolygons are unsupported");
}

Result<SimplePolygon> Importer::ImportZoneShape(const ExternalShape& shape) const
{
    if (const auto* polygon = std::get_if<ExternalPolygon>(&shape.value))
    {
        if (!polygon->holes.empty())
            return invalidShape("quality zones with holes are unsupported");
    }
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

    std::vector<double> degrees;
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
            if (!std::isfinite(angle))
            {
                return Result<RotationRange>::FromError(
                    {ErrorCode::InvalidInput, "rotation angles must be finite"});
            }
            degrees.push_back(normalizeDegrees(angle));
        }
    }
    else
    {
        const double step = rotation.stepDegrees;
        if (!std::isfinite(step) || step <= 0.0 || step > 360.0)
        {
            return Result<RotationRange>::FromError(
                {ErrorCode::InvalidInput,
                 "rotation step must be finite and in (0, 360]"});
        }
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
            degrees.push_back(360.0 * static_cast<double>(i) / count);
        }
    }

    std::sort(degrees.begin(), degrees.end());
    degrees.erase(std::unique(degrees.begin(), degrees.end()), degrees.end());
    if (degrees.size() == 1 && degrees.front() == 0.0)
        return Result<RotationRange>::FromValue(
            RotationRange{RotationMode::None, {}});

    const double pi = std::acos(-1.0);
    std::vector<double> radians;
    radians.reserve(degrees.size());
    for (double angle : degrees)
        radians.push_back(angle * pi / 180.0);
    return Result<RotationRange>::FromValue(
        RotationRange{RotationMode::Discrete, std::move(radians)});
}

JAGUARCORE_API Result<Item> Importer::ImportItem(
    const ExternalItem& external,
    ItemIndex internalIndex) const
{
    if (!std::isfinite(m_Config.minItemSeparation) ||
        m_Config.minItemSeparation < 0.0)
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
    if (!std::isfinite(m_Config.minItemSeparation) ||
        m_Config.minItemSeparation < 0.0)
    {
        return Result<Container>::FromError(
            {ErrorCode::InvalidInput, "minimum item separation must be finite and nonnegative"});
    }

    auto outer = ImportContainerOuter(external.shape);
    if (!outer.HasValue())
        return Result<Container>::FromError(outer.GetError());

    std::vector<std::vector<SimplePolygon>> grouped(MAX_QUALITIES);
    if (const auto* polygon = std::get_if<ExternalPolygon>(&external.shape.value))
    {
        for (const auto& hole : polygon->holes)
        {
            auto holeShape = ImportSimplePolygon(hole);
            if (!holeShape.HasValue())
                return Result<Container>::FromError(holeShape.GetError());
            OriginalShape holeOriginal{
                std::move(holeShape.Value()), Transform2d::Identity(),
                ShapeModifyMode::Inflate, ShapeConfig()};
            auto converted = holeOriginal.ConvertToInternal();
            if (!converted.HasValue())
                return Result<Container>::FromError(converted.GetError());
            grouped[0].push_back(std::move(converted.Value()));
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
        OriginalShape zoneOriginal{
            std::move(zoneShape.Value()), Transform2d::Identity(),
            ShapeModifyMode::Inflate, ShapeConfig()};
        auto converted = zoneOriginal.ConvertToInternal();
        if (!converted.HasValue())
            return Result<Container>::FromError(converted.GetError());
        grouped[zone.quality].push_back(std::move(converted.Value()));
    }

    std::vector<QualityZone> zones;
    for (std::size_t quality = 0; quality < grouped.size(); ++quality)
    {
        if (!grouped[quality].empty())
            zones.push_back({quality, std::move(grouped[quality])});
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
    if (!std::isfinite(external.minItemSeparation) ||
        external.minItemSeparation < 0.0)
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
    if (!std::isfinite(external.minItemSeparation) ||
        external.minItemSeparation < 0.0)
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
    const BPInstance& instance, const ExternalBPSolution& external) const
{
    auto problem = BPProblem::Create(instance);
    if (!problem.HasValue())
        return Result<BPSolution>::FromError(problem.GetError());
    auto& value = problem.Value();
    for (const ExternalLayout& layout : external.layouts)
    {
        std::size_t binIndex = instance.BinCount();
        for (std::size_t i = 0; i < instance.BinCount(); ++i)
        {
            if (instance.GetBin(i).externalId == layout.containerId)
            {
                binIndex = i;
                break;
            }
        }
        if (binIndex >= instance.BinCount() || layout.placedItems.empty())
        {
            return Result<BPSolution>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "BP solution references an invalid or empty layout"});
        }

        std::size_t layoutIndex = 0;
        for (std::size_t i = 0; i < layout.placedItems.size(); ++i)
        {
            const ExternalPlacedItem& placed = layout.placedItems[i];
            ItemIndex itemIndex = 0;
            const Item* item = findItemByExternalId(instance, placed.itemId, itemIndex);
            if (!item)
            {
                return Result<BPSolution>::FromError(
                    {ErrorCode::SnapshotMismatch,
                     "BP solution references an unknown item ID"});
            }
            const auto transform = externalToInternal(
                placed.transformation, item->GetOriginalShape());
            if (!transform.HasValue())
                return Result<BPSolution>::FromError(transform.GetError());
            const auto result = value.PlaceItem({
                i == 0 ? LayoutTargetKind::Closed : LayoutTargetKind::Open,
                layoutIndex, binIndex, itemIndex, transform.Value()});
            if (!result.HasValue())
                return Result<BPSolution>::FromError(result.GetError());
            if (i == 0)
                layoutIndex = result.Value().layoutIndex;
        }
    }
    return Result<BPSolution>::FromValue(value.Save());
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
    if (!std::isfinite(external.minItemSeparation) ||
        external.minItemSeparation < 0.0)
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
    const MSPInstance& instance, const ExternalMSPSolution& external) const
{
    if (external.layouts.size() != external.containers.size())
    {
        return Result<MSPSolution>::FromError(
            {ErrorCode::SnapshotMismatch,
             "MSP solution layouts and containers must have equal counts"});
    }
    std::set<ContainerId> containerIds;
    for (const ExternalContainer& container : external.containers)
    {
        if (!containerIds.insert(container.id).second)
        {
            return Result<MSPSolution>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "MSP solution container IDs must be unique"});
        }
    }
    auto problem = MSPProblem::Create(instance);
    if (!problem.HasValue())
        return Result<MSPSolution>::FromError(problem.GetError());
    auto& value = problem.Value();
    std::set<ContainerId> layoutContainerIds;
    for (const ExternalLayout& layout : external.layouts)
    {
        if (!layoutContainerIds.insert(layout.containerId).second)
        {
            return Result<MSPSolution>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "MSP solution layouts must reference unique container IDs"});
        }
        const auto containerIt = std::find_if(
            external.containers.begin(), external.containers.end(),
            [&layout](const ExternalContainer& container) {
                return container.id == layout.containerId;
            });
        if (containerIt == external.containers.end())
        {
            return Result<MSPSolution>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "MSP solution references an invalid layout"});
        }
        const auto rectangle = rectangleData(containerIt->shape);
        if (!rectangle.HasValue())
            return Result<MSPSolution>::FromError(rectangle.GetError());
        if (!near(rectangle.Value().xMin, 0.0) ||
            !near(rectangle.Value().yMin, 0.0) ||
            !near(rectangle.Value().height,
                  instance.BaseStrip().fixedHeight) ||
            rectangle.Value().width > instance.BaseStrip().maxWidth + 1.0e-9)
        {
            return Result<MSPSolution>::FromError(
                {ErrorCode::SnapshotMismatch,
                 "MSP solution container is incompatible with the instance strip"});
        }
        const auto strip = MultiStrip::Create(
            instance.BaseStrip().maxWidth, rectangle.Value().height,
            instance.BaseStrip().cdeConfig, instance.BaseStrip().shapeModifyConfig,
            rectangle.Value().width);
        if (!strip.HasValue())
            return Result<MSPSolution>::FromError(strip.GetError());
        const auto added = value.AddLayout(strip.Value());
        if (!added.HasValue())
            return Result<MSPSolution>::FromError(added.GetError());
        for (const ExternalPlacedItem& placed : layout.placedItems)
        {
            ItemIndex itemIndex = 0;
            const Item* item = findItemByExternalId(instance, placed.itemId, itemIndex);
            if (!item)
            {
                return Result<MSPSolution>::FromError(
                    {ErrorCode::SnapshotMismatch,
                     "MSP solution references an unknown item ID"});
            }
            const auto transform = externalToInternal(
                placed.transformation, item->GetOriginalShape());
            if (!transform.HasValue())
                return Result<MSPSolution>::FromError(transform.GetError());
            const auto result = value.PlaceItem({added.Value(), itemIndex,
                                                 transform.Value()});
            if (!result.HasValue())
                return Result<MSPSolution>::FromError(result.GetError());
        }
    }
    return Result<MSPSolution>::FromValue(value.Save());
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
