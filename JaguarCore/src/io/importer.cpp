#include "JaguarCore/io/importer.h"

#include "JaguarCore/io/json_codec.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace jaguar
{
namespace
{
constexpr std::size_t kMaxAngles = 65536;
constexpr std::size_t kMaxQualities = 10;

Result<SimplePolygon> invalidShape(const char* message)
{
    return Result<SimplePolygon>::fromError(
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
        return Result<std::size_t>::fromError(
            {ErrorCode::InvalidInput, std::string(field) + " exceeds size_t"});
    }
    return Result<std::size_t>::fromValue(static_cast<std::size_t>(value));
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
        !external.translation.isFinite())
    {
        return Result<Transform2d>::fromError(
            {ErrorCode::SnapshotMismatch,
             "external solution contains a non-finite transformation"});
    }
    constexpr double pi = 3.141592653589793238462643383279502884;
    const Transform2d externalTransform = Transform2d::fromRotationTranslation(
        external.rotationDegrees * pi / 180.0, external.translation);
    return Result<Transform2d>::fromValue(
        original.preTransform.inverse().followedBy(externalTransform));
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
            return Result<std::vector<Item>>::fromError(
                {ErrorCode::InvalidInput, "external item IDs must be unique"});
        }
    }

    for (const ExternalDemandItem& external : externalItems)
    {
        if (external.demand == 0)
            continue;
        const auto demand = toSize(external.demand, "item demand");
        if (!demand.hasValue())
            return Result<std::vector<Item>>::fromError(demand.error());
        const auto imported = importer.importItem(external.item, items.size());
        if (!imported.hasValue())
            return Result<std::vector<Item>>::fromError(imported.error());
        items.push_back(imported.value());
        demands.push_back(demand.value());
    }
    if (items.empty())
    {
        return Result<std::vector<Item>>::fromError(
            {ErrorCode::InvalidInput, "instance must have positive item demand"});
    }
    return Result<std::vector<Item>>::fromValue(std::move(items));
}

template <typename Instance>
const Item* findItemByExternalId(const Instance& instance,
                                 ItemId id,
                                 ItemIndex& index) noexcept
{
    for (std::size_t i = 0; i < instance.itemCount(); ++i)
    {
        if (instance.item(i).externalId() == id)
        {
            index = i;
            return &instance.item(i);
        }
    }
    return nullptr;
}

Result<ExternalRectangle> rectangleData(const ExternalShape& shape)
{
    const auto* rectangle = std::get_if<ExternalRectangle>(&shape.value);
    if (!rectangle || !finiteRectangle(*rectangle))
    {
        return Result<ExternalRectangle>::fromError(
            {ErrorCode::SnapshotMismatch,
             "MSP solution containers must be finite rectangles"});
    }
    return Result<ExternalRectangle>::fromValue(*rectangle);
}

bool near(double lhs, double rhs) noexcept
{
    return std::abs(lhs - rhs) <= 1.0e-9;
}
}

Importer::Importer(ImporterConfig config)
    : m_Config(std::move(config))
{
}

ShapeModifyConfig Importer::shapeConfig() const
{
    ShapeModifyConfig config = m_Config.shapeModifyConfig;
    if (m_Config.minItemSeparation > 0.0)
        config.offset = m_Config.minItemSeparation * 0.5;
    return config;
}

Result<SimplePolygon> Importer::importSimplePolygon(
    const std::vector<Point2d>& inputPoints) const
{
    std::vector<Point2d> points = inputPoints;
    if (points.size() > 1 && points.front() == points.back())
        points.pop_back();

    std::vector<Point2d> cleaned;
    cleaned.reserve(points.size());
    for (const Point2d& point : points)
    {
        if (cleaned.empty() || cleaned.back().squaredDistanceTo(point) > 1.0e-18)
            cleaned.push_back(point);
    }
    if (cleaned.size() > 1 && cleaned.front() == cleaned.back())
        cleaned.pop_back();

    return SimplePolygon::create(std::move(cleaned));
}

Result<SimplePolygon> Importer::importItemShape(const ExternalShape& shape) const
{
    if (const auto* rectangle = std::get_if<ExternalRectangle>(&shape.value))
    {
        if (!finiteRectangle(*rectangle))
        {
            return Result<SimplePolygon>::fromError(
                {ErrorCode::InvalidInput, "invalid external rectangle"});
        }
        return SimplePolygon::create({
            {rectangle->xMin, rectangle->yMin},
            {rectangle->xMin + rectangle->width, rectangle->yMin},
            {rectangle->xMin + rectangle->width,
             rectangle->yMin + rectangle->height},
            {rectangle->xMin, rectangle->yMin + rectangle->height}});
    }
    if (const auto* polygon = std::get_if<ExternalSimplePolygon>(&shape.value))
        return importSimplePolygon(polygon->points);
    if (const auto* polygon = std::get_if<ExternalPolygon>(&shape.value))
    {
        if (!polygon->holes.empty())
            return invalidShape("item polygons with holes are unsupported");
        return importSimplePolygon(polygon->outer);
    }
    return invalidShape("item multipolygons are unsupported");
}

Result<SimplePolygon> Importer::importContainerOuter(const ExternalShape& shape) const
{
    if (const auto* rectangle = std::get_if<ExternalRectangle>(&shape.value))
    {
        if (!finiteRectangle(*rectangle))
        {
            return Result<SimplePolygon>::fromError(
                {ErrorCode::InvalidInput, "invalid external rectangle"});
        }
        return SimplePolygon::create({
            {rectangle->xMin, rectangle->yMin},
            {rectangle->xMin + rectangle->width, rectangle->yMin},
            {rectangle->xMin + rectangle->width,
             rectangle->yMin + rectangle->height},
            {rectangle->xMin, rectangle->yMin + rectangle->height}});
    }
    if (const auto* polygon = std::get_if<ExternalSimplePolygon>(&shape.value))
        return importSimplePolygon(polygon->points);
    if (const auto* polygon = std::get_if<ExternalPolygon>(&shape.value))
        return importSimplePolygon(polygon->outer);
    return invalidShape("container multipolygons are unsupported");
}

Result<SimplePolygon> Importer::importZoneShape(const ExternalShape& shape) const
{
    if (const auto* polygon = std::get_if<ExternalPolygon>(&shape.value))
    {
        if (!polygon->holes.empty())
            return invalidShape("quality zones with holes are unsupported");
    }
    if (std::holds_alternative<ExternalMultiPolygon>(shape.value))
        return invalidShape("quality zone multipolygons are unsupported");
    return importItemShape(shape);
}

Result<RotationRange> Importer::importRotation(const RotationPolicy& rotation) const
{
    if (rotation.kind == RotationPolicyKind::Continuous)
        return Result<RotationRange>::fromValue(
            RotationRange{RotationMode::Continuous, {}});
    if (rotation.kind == RotationPolicyKind::None)
        return Result<RotationRange>::fromValue(
            RotationRange{RotationMode::None, {}});

    std::vector<double> degrees;
    if (rotation.kind == RotationPolicyKind::Discrete)
    {
        if (rotation.anglesDegrees.empty() ||
            rotation.anglesDegrees.size() > kMaxAngles)
        {
            return Result<RotationRange>::fromError(
                {ErrorCode::InvalidInput,
                 "discrete rotation requires 1..=65536 angles"});
        }
        degrees.reserve(rotation.anglesDegrees.size());
        for (double angle : rotation.anglesDegrees)
        {
            if (!std::isfinite(angle))
            {
                return Result<RotationRange>::fromError(
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
            return Result<RotationRange>::fromError(
                {ErrorCode::InvalidInput,
                 "rotation step must be finite and in (0, 360]"});
        }
        const double count = std::round(360.0 / step);
        const double tolerance = 360.0 * std::numeric_limits<float>::epsilon();
        if (count < 1.0 || count > static_cast<double>(kMaxAngles) ||
            std::abs(count * step - 360.0) > tolerance)
        {
            return Result<RotationRange>::fromError(
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
        return Result<RotationRange>::fromValue(
            RotationRange{RotationMode::None, {}});

    const double pi = std::acos(-1.0);
    std::vector<double> radians;
    radians.reserve(degrees.size());
    for (double angle : degrees)
        radians.push_back(angle * pi / 180.0);
    return Result<RotationRange>::fromValue(
        RotationRange{RotationMode::Discrete, std::move(radians)});
}

JAGUARCORE_API Result<Item> Importer::importItem(
    const ExternalItem& external,
    ItemIndex internalIndex) const
{
    if (!std::isfinite(m_Config.minItemSeparation) ||
        m_Config.minItemSeparation < 0.0)
    {
        return Result<Item>::fromError(
            {ErrorCode::InvalidInput, "minimum item separation must be finite and nonnegative"});
    }

    auto shape = importItemShape(external.shape);
    if (!shape.hasValue())
        return Result<Item>::fromError(shape.error());
    auto rotation = importRotation(external.rotation);
    if (!rotation.hasValue())
        return Result<Item>::fromError(rotation.error());

    const Point2d center = shape.value().centroid();
    OriginalShape original{
        std::move(shape.value()),
        Transform2d::fromRotationTranslation(0.0, {-center.x, -center.y}),
        ShapeModifyMode::Inflate,
        shapeConfig()};
    return Item::create(internalIndex, external.id, std::move(original),
                        m_Config.cdeConfig.itemSurrogateConfig,
                        external.minQuality, std::move(rotation.value()));
}

JAGUARCORE_API Result<Container> Importer::importContainer(
    const ExternalContainer& external) const
{
    if (!std::isfinite(m_Config.minItemSeparation) ||
        m_Config.minItemSeparation < 0.0)
    {
        return Result<Container>::fromError(
            {ErrorCode::InvalidInput, "minimum item separation must be finite and nonnegative"});
    }

    auto outer = importContainerOuter(external.shape);
    if (!outer.hasValue())
        return Result<Container>::fromError(outer.error());

    std::vector<std::vector<SimplePolygon>> grouped(kMaxQualities);
    if (const auto* polygon = std::get_if<ExternalPolygon>(&external.shape.value))
    {
        for (const auto& hole : polygon->holes)
        {
            auto holeShape = importSimplePolygon(hole);
            if (!holeShape.hasValue())
                return Result<Container>::fromError(holeShape.error());
            OriginalShape holeOriginal{
                std::move(holeShape.value()), Transform2d::identity(),
                ShapeModifyMode::Inflate, shapeConfig()};
            auto converted = holeOriginal.convertToInternal();
            if (!converted.hasValue())
                return Result<Container>::fromError(converted.error());
            grouped[0].push_back(std::move(converted.value()));
        }
    }

    for (const ExternalQualityZone& zone : external.zones)
    {
        if (zone.quality >= kMaxQualities)
        {
            return Result<Container>::fromError(
                {ErrorCode::InvalidInput, "quality zone is outside the supported range"});
        }
        auto zoneShape = importZoneShape(zone.shape);
        if (!zoneShape.hasValue())
            return Result<Container>::fromError(zoneShape.error());
        OriginalShape zoneOriginal{
            std::move(zoneShape.value()), Transform2d::identity(),
            ShapeModifyMode::Inflate, shapeConfig()};
        auto converted = zoneOriginal.convertToInternal();
        if (!converted.hasValue())
            return Result<Container>::fromError(converted.error());
        grouped[zone.quality].push_back(std::move(converted.value()));
    }

    std::vector<QualityZone> zones;
    for (std::size_t quality = 0; quality < grouped.size(); ++quality)
    {
        if (!grouped[quality].empty())
            zones.push_back({quality, std::move(grouped[quality])});
    }

    OriginalShape originalOuter{
        std::move(outer.value()), Transform2d::identity(),
        ShapeModifyMode::Deflate, shapeConfig()};
    return Container::create(external.id, std::move(originalOuter),
                             std::move(zones), m_Config.cdeConfig);
}

JAGUARCORE_API Result<Item> Importer::importItemJson(
    const std::string& json, ItemIndex internalIndex) const
{
    const auto parsed = parseExternalItemJson(json);
    if (!parsed.hasValue())
        return Result<Item>::fromError(parsed.error());
    return importItem(parsed.value(), internalIndex);
}

JAGUARCORE_API Result<Container> Importer::importContainerJson(
    const std::string& json) const
{
    const auto parsed = parseExternalContainerJson(json);
    if (!parsed.hasValue())
        return Result<Container>::fromError(parsed.error());
    return importContainer(parsed.value());
}

JAGUARCORE_API Result<SPInstance> Importer::importSPInstance(
    const ExternalSPInstance& external) const
{
    if (!std::isfinite(external.minItemSeparation) ||
        external.minItemSeparation < 0.0)
    {
        return Result<SPInstance>::fromError(
            {ErrorCode::InvalidInput,
             "minimum item separation must be finite and nonnegative"});
    }
    const Importer scoped(withSeparation(m_Config, external.minItemSeparation));
    std::vector<std::size_t> demands;
    const auto items = importPositiveItems(scoped, external.items, demands);
    if (!items.hasValue())
        return Result<SPInstance>::fromError(items.error());
    return SPInstance::create(items.value(), std::move(demands),
                              external.stripHeight, m_Config.cdeConfig,
                              problemShapeConfig(scoped.shapeConfig().offset));
}

JAGUARCORE_API Result<SPSolution> Importer::importSPSolution(
    const SPInstance& instance, const ExternalSPSolution& external) const
{
    if (!std::isfinite(external.stripWidth) || external.stripWidth <= 0.0)
    {
        return Result<SPSolution>::fromError(
            {ErrorCode::SnapshotMismatch, "SP solution strip width is invalid"});
    }
    auto problem = SPProblem::create(instance);
    if (!problem.hasValue())
        return Result<SPSolution>::fromError(problem.error());
    auto& value = problem.value();
    const auto changed = value.changeStripWidth(external.stripWidth);
    if (!changed.hasValue())
        return Result<SPSolution>::fromError(changed.error());
    for (const ExternalPlacedItem& placed : external.layout.placedItems)
    {
        ItemIndex itemIndex = 0;
        const Item* item = findItemByExternalId(instance, placed.itemId, itemIndex);
        if (!item)
        {
            return Result<SPSolution>::fromError(
                {ErrorCode::SnapshotMismatch, "SP solution references an unknown item ID"});
        }
        const auto transform = externalToInternal(
            placed.transformation, item->originalShape());
        if (!transform.hasValue())
            return Result<SPSolution>::fromError(transform.error());
        const auto result = value.placeItem({itemIndex, transform.value()});
        if (!result.hasValue())
            return Result<SPSolution>::fromError(result.error());
    }
    return Result<SPSolution>::fromValue(value.save());
}

JAGUARCORE_API Result<SPInstance> Importer::importSPInstanceJson(
    const std::string& json) const
{
    const auto parsed = parseExternalSPInstanceJson(json);
    if (!parsed.hasValue())
        return Result<SPInstance>::fromError(parsed.error());
    return importSPInstance(parsed.value());
}

JAGUARCORE_API Result<SPSolution> Importer::importSPSolutionJson(
    const SPInstance& instance, const std::string& json) const
{
    const auto parsed = parseExternalSPSolutionJson(json);
    if (!parsed.hasValue())
        return Result<SPSolution>::fromError(parsed.error());
    return importSPSolution(instance, parsed.value());
}

JAGUARCORE_API Result<BPInstance> Importer::importBPInstance(
    const ExternalBPInstance& external) const
{
    if (!std::isfinite(external.minItemSeparation) ||
        external.minItemSeparation < 0.0)
    {
        return Result<BPInstance>::fromError(
            {ErrorCode::InvalidInput,
             "minimum item separation must be finite and nonnegative"});
    }
    const Importer scoped(withSeparation(m_Config, external.minItemSeparation));
    std::vector<std::size_t> demands;
    const auto items = importPositiveItems(scoped, external.items, demands);
    if (!items.hasValue())
        return Result<BPInstance>::fromError(items.error());

    std::set<ContainerId> ids;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < external.bins.size(); ++i)
    {
        if (!ids.insert(external.bins[i].container.id).second)
        {
            return Result<BPInstance>::fromError(
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
        if (!stock.hasValue())
            return Result<BPInstance>::fromError(stock.error());
        const auto container = scoped.importContainer(externalBin.container);
        if (!container.hasValue())
            return Result<BPInstance>::fromError(container.error());
        bins.push_back(Bin{bins.size(), externalBin.container.id,
                           container.value(), stock.value(), externalBin.cost});
    }
    if (bins.empty())
    {
        return Result<BPInstance>::fromError(
            {ErrorCode::InvalidInput, "instance must have positive bin stock"});
    }
    return BPInstance::create(items.value(), std::move(demands), std::move(bins));
}

JAGUARCORE_API Result<BPSolution> Importer::importBPSolution(
    const BPInstance& instance, const ExternalBPSolution& external) const
{
    auto problem = BPProblem::create(instance);
    if (!problem.hasValue())
        return Result<BPSolution>::fromError(problem.error());
    auto& value = problem.value();
    for (const ExternalLayout& layout : external.layouts)
    {
        std::size_t binIndex = instance.binCount();
        for (std::size_t i = 0; i < instance.binCount(); ++i)
        {
            if (instance.bin(i).externalId == layout.containerId)
            {
                binIndex = i;
                break;
            }
        }
        if (binIndex >= instance.binCount() || layout.placedItems.empty())
        {
            return Result<BPSolution>::fromError(
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
                return Result<BPSolution>::fromError(
                    {ErrorCode::SnapshotMismatch,
                     "BP solution references an unknown item ID"});
            }
            const auto transform = externalToInternal(
                placed.transformation, item->originalShape());
            if (!transform.hasValue())
                return Result<BPSolution>::fromError(transform.error());
            const auto result = value.placeItem({
                i == 0 ? LayoutTargetKind::Closed : LayoutTargetKind::Open,
                layoutIndex, binIndex, itemIndex, transform.value()});
            if (!result.hasValue())
                return Result<BPSolution>::fromError(result.error());
            if (i == 0)
                layoutIndex = result.value().layoutIndex;
        }
    }
    return Result<BPSolution>::fromValue(value.save());
}

JAGUARCORE_API Result<BPInstance> Importer::importBPInstanceJson(
    const std::string& json) const
{
    const auto parsed = parseExternalBPInstanceJson(json);
    if (!parsed.hasValue())
        return Result<BPInstance>::fromError(parsed.error());
    return importBPInstance(parsed.value());
}

JAGUARCORE_API Result<BPSolution> Importer::importBPSolutionJson(
    const BPInstance& instance, const std::string& json) const
{
    const auto parsed = parseExternalBPSolutionJson(json);
    if (!parsed.hasValue())
        return Result<BPSolution>::fromError(parsed.error());
    return importBPSolution(instance, parsed.value());
}

JAGUARCORE_API Result<MSPInstance> Importer::importMSPInstance(
    const ExternalMSPInstance& external) const
{
    if (!std::isfinite(external.minItemSeparation) ||
        external.minItemSeparation < 0.0)
    {
        return Result<MSPInstance>::fromError(
            {ErrorCode::InvalidInput,
             "minimum item separation must be finite and nonnegative"});
    }
    const Importer scoped(withSeparation(m_Config, external.minItemSeparation));
    std::vector<std::size_t> demands;
    const auto items = importPositiveItems(scoped, external.items, demands);
    if (!items.hasValue())
        return Result<MSPInstance>::fromError(items.error());
    return MSPInstance::create(items.value(), std::move(demands),
                               external.strips.maxWidth, external.strips.height,
                               m_Config.cdeConfig,
                               problemShapeConfig(scoped.shapeConfig().offset));
}

JAGUARCORE_API Result<MSPSolution> Importer::importMSPSolution(
    const MSPInstance& instance, const ExternalMSPSolution& external) const
{
    if (external.layouts.size() != external.containers.size())
    {
        return Result<MSPSolution>::fromError(
            {ErrorCode::SnapshotMismatch,
             "MSP solution layouts and containers must have equal counts"});
    }
    std::set<ContainerId> containerIds;
    for (const ExternalContainer& container : external.containers)
    {
        if (!containerIds.insert(container.id).second)
        {
            return Result<MSPSolution>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "MSP solution container IDs must be unique"});
        }
    }
    auto problem = MSPProblem::create(instance);
    if (!problem.hasValue())
        return Result<MSPSolution>::fromError(problem.error());
    auto& value = problem.value();
    std::set<ContainerId> layoutContainerIds;
    for (const ExternalLayout& layout : external.layouts)
    {
        if (!layoutContainerIds.insert(layout.containerId).second)
        {
            return Result<MSPSolution>::fromError(
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
            return Result<MSPSolution>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "MSP solution references an invalid layout"});
        }
        const auto rectangle = rectangleData(containerIt->shape);
        if (!rectangle.hasValue())
            return Result<MSPSolution>::fromError(rectangle.error());
        if (!near(rectangle.value().xMin, 0.0) ||
            !near(rectangle.value().yMin, 0.0) ||
            !near(rectangle.value().height,
                  instance.baseStrip().fixedHeight) ||
            rectangle.value().width > instance.baseStrip().maxWidth + 1.0e-9)
        {
            return Result<MSPSolution>::fromError(
                {ErrorCode::SnapshotMismatch,
                 "MSP solution container is incompatible with the instance strip"});
        }
        const auto strip = MultiStrip::create(
            instance.baseStrip().maxWidth, rectangle.value().height,
            instance.baseStrip().cdeConfig, instance.baseStrip().shapeModifyConfig,
            rectangle.value().width);
        if (!strip.hasValue())
            return Result<MSPSolution>::fromError(strip.error());
        const auto added = value.addLayout(strip.value());
        if (!added.hasValue())
            return Result<MSPSolution>::fromError(added.error());
        for (const ExternalPlacedItem& placed : layout.placedItems)
        {
            ItemIndex itemIndex = 0;
            const Item* item = findItemByExternalId(instance, placed.itemId, itemIndex);
            if (!item)
            {
                return Result<MSPSolution>::fromError(
                    {ErrorCode::SnapshotMismatch,
                     "MSP solution references an unknown item ID"});
            }
            const auto transform = externalToInternal(
                placed.transformation, item->originalShape());
            if (!transform.hasValue())
                return Result<MSPSolution>::fromError(transform.error());
            const auto result = value.placeItem({added.value(), itemIndex,
                                                 transform.value()});
            if (!result.hasValue())
                return Result<MSPSolution>::fromError(result.error());
        }
    }
    return Result<MSPSolution>::fromValue(value.save());
}

JAGUARCORE_API Result<MSPInstance> Importer::importMSPInstanceJson(
    const std::string& json) const
{
    const auto parsed = parseExternalMSPInstanceJson(json);
    if (!parsed.hasValue())
        return Result<MSPInstance>::fromError(parsed.error());
    return importMSPInstance(parsed.value());
}

JAGUARCORE_API Result<MSPSolution> Importer::importMSPSolutionJson(
    const MSPInstance& instance, const std::string& json) const
{
    const auto parsed = parseExternalMSPSolutionJson(json);
    if (!parsed.hasValue())
        return Result<MSPSolution>::fromError(parsed.error());
    return importMSPSolution(instance, parsed.value());
}
}
