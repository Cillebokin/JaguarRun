#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/NumericPolicy.h"
#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <utility>

namespace
{
jaguar::SimplePolygon Rectangle(double width, double height)
{
    const auto shape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(shape.HasValue());
    return shape.Value();
}

jaguar::Item makeItem()
{
    jaguar::OriginalShape original{
        Rectangle(2.0, 2.0), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(0, 9101, std::move(original), config);
    assert(item.HasValue());
    return item.Value();
}

jaguar::Container makeContainer()
{
    jaguar::OriginalShape original{
        Rectangle(10.0, 10.0), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    const auto container = jaguar::Container::Create(9102, std::move(original), {}, {});
    assert(container.HasValue());
    return container.Value();
}
}

void runBoundaryValidationTests()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    const jaguar::NumericPolicy invalidPolicy{nan, 1.0e-7, 1.0e-9, 1.0e-3};
    const auto invalidPolicyPolygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}, invalidPolicy);
    assert(!invalidPolicyPolygon.HasValue());
    assert(invalidPolicyPolygon.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto invalidRectangle = jaguar::Rect2d::Create(0.0, 0.0, inf, 1.0);
    assert(!invalidRectangle.HasValue());

    const auto overflowPolygon = jaguar::SimplePolygon::Create({
        {1.0e308, 1.0e308}, {-1.0e308, 1.0e308}, {-1.0e308, -1.0e308}});
    assert(!overflowPolygon.HasValue());
    assert(overflowPolygon.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto sourceShape = Rectangle(2.0, 2.0);
    jaguar::SurrogateConfig invalidRatio{};
    invalidRatio.failFastPoleAreaRatio = nan;
    const auto invalidRatioResult = jaguar::Surrogate::Create(sourceShape, invalidRatio);
    assert(!invalidRatioResult.HasValue());
    assert(invalidRatioResult.GetError().code == jaguar::ErrorCode::InvalidInput);

    jaguar::SurrogateConfig invalidThreshold{};
    invalidThreshold.poleLimits = {{{1, inf}, {0, 0.0}, {0, 0.0}}};
    const auto invalidThresholdResult =
        jaguar::Surrogate::Create(sourceShape, invalidThreshold);
    assert(!invalidThresholdResult.HasValue());
    assert(invalidThresholdResult.GetError().code == jaguar::ErrorCode::InvalidInput);

    jaguar::SurrogateConfig zeroPiers{};
    const auto zeroPiersResult = jaguar::Surrogate::Create(sourceShape, zeroPiers);
    assert(zeroPiersResult.HasValue());

    jaguar::OriginalShape rotationShape{
        sourceShape, jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    const auto invalidRotation = jaguar::Item::Create(
        0, 9103, std::move(rotationShape), {}, std::nullopt,
        {jaguar::RotationMode::Discrete, {nan}});
    assert(!invalidRotation.HasValue());
    assert(invalidRotation.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto invalidStrip = jaguar::Strip::Create(nan, {}, {}, 10.0);
    assert(!invalidStrip.HasValue());
    assert(invalidStrip.GetError().code == jaguar::ErrorCode::InvalidInput);
    const auto invalidMultiStrip =
        jaguar::MultiStrip::Create(10.0, 5.0, {}, {}, inf);
    assert(!invalidMultiStrip.HasValue());
    assert(invalidMultiStrip.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto item = makeItem();
    const auto invalidSpp = jaguar::SPInstance::Create(
        {item}, {1}, nan, {}, {});
    assert(!invalidSpp.HasValue());
    assert(invalidSpp.GetError().code == jaguar::ErrorCode::InvalidInput);
    const auto invalidMspp = jaguar::MSPInstance::Create(
        {item}, {1}, nan, 5.0, {}, {});
    assert(!invalidMspp.HasValue());
    assert(invalidMspp.GetError().code == jaguar::ErrorCode::InvalidInput);
    const auto invalidBpp = jaguar::BPInstance::Create({}, {}, {});
    assert(!invalidBpp.HasValue());
    assert(invalidBpp.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto root = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(root.HasValue());
    jaguar::CollisionEngine engine(root.Value(), {});
    const auto invalidBatch = engine.DetectPolygonCollisionsParallel(
        {}, jaguar::NoFilter{}, {0, 0});
    assert(!invalidBatch.HasValue());
    assert(invalidBatch.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto hazardShape = std::make_shared<const jaguar::SimplePolygon>(sourceShape);
    const auto registered = engine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 0, 0, 0}, hazardShape, true});
    assert(registered.HasValue());
    const auto query = Rectangle(1.0, 1.0);
    const bool baselineCollision = engine.DetectPolygonCollision(query, jaguar::NoFilter{});
    const auto snapshot = engine.Save();
    assert(snapshot.dynamicHazards.size() == 1);

    auto nullShapeSnapshot = snapshot;
    nullShapeSnapshot.dynamicHazards.front().shape.reset();
    const auto nullShapeRestore = engine.Restore(nullShapeSnapshot);
    assert(!nullShapeRestore.HasValue());
    assert(nullShapeRestore.GetError().code == jaguar::ErrorCode::InvalidInput);
    assert(engine.DetectPolygonCollision(query, jaguar::NoFilter{}) == baselineCollision);

    auto staticSnapshot = snapshot;
    staticSnapshot.dynamicHazards.front().dynamic = false;
    const auto staticRestore = engine.Restore(staticSnapshot);
    assert(!staticRestore.HasValue());
    assert(staticRestore.GetError().code == jaguar::ErrorCode::SnapshotMismatch);
    assert(engine.DetectPolygonCollision(query, jaguar::NoFilter{}) == baselineCollision);

    assert(engine.DeregisterHazard(registered.Value()).HasValue());
    const auto staleDeregister = engine.DeregisterHazard(registered.Value());
    assert(!staleDeregister.HasValue());
    assert(staleDeregister.GetError().code == jaguar::ErrorCode::InvalidInput);

    jaguar::Layout layout(makeContainer());
    const auto placed = layout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    assert(placed.HasValue());
    const std::size_t placedCount = layout.PlacedItemCount();
    const double density = layout.Density();
    const bool feasible = layout.IsFeasible();
    const auto layoutSnapshot = layout.Save();
    const auto rightmost = layout.RightmostCollisionX();
    const auto invalidPlacement = layout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(nan, {0.0, 0.0}));
    assert(!invalidPlacement.HasValue());
    assert(invalidPlacement.GetError().code == jaguar::ErrorCode::InvalidInput);
    const auto invalidRemoval = layout.RemoveItem({
        std::numeric_limits<std::size_t>::max(), 0});
    assert(!invalidRemoval.HasValue());
    assert(invalidRemoval.GetError().code == jaguar::ErrorCode::InvalidInput);
    assert(layout.PlacedItemCount() == placedCount);
    assert(layout.Density() == density);
    assert(layout.IsFeasible() == feasible);
    assert(layout.Save().placements.size() == layoutSnapshot.placements.size());
    assert(layout.Save().cdeSnapshot.dynamicHazards.size() ==
           layoutSnapshot.cdeSnapshot.dynamicHazards.size());
    assert(layout.RightmostCollisionX() == rightmost);

    // Fixed seed and exactly 256 cases keep this boundary run reproducible.
    std::mt19937_64 generator(0x4A4752434F5245ULL);
    std::uniform_int_distribution<std::size_t> vertexCountDistribution(3, 12);
    std::uniform_real_distribution<double> coordinateDistribution(-1000.0, 1000.0);
    const double pi = std::acos(-1.0);
    for (std::size_t caseIndex = 0; caseIndex < 256; ++caseIndex)
    {
        std::vector<jaguar::Point2d> points;
        const std::size_t vertexCount = vertexCountDistribution(generator);
        points.reserve(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            points.push_back({coordinateDistribution(generator),
                              coordinateDistribution(generator)});
        }

        jaguar::Point2d center{};
        for (const jaguar::Point2d& point : points)
        {
            center.x += point.x;
            center.y += point.y;
        }
        center.x /= static_cast<double>(points.size());
        center.y /= static_cast<double>(points.size());
        std::sort(points.begin(), points.end(), [center](const auto& first, const auto& second) {
            return std::atan2(first.y - center.y, first.x - center.x) <
                   std::atan2(second.y - center.y, second.x - center.x);
        });

        const auto polygon = jaguar::SimplePolygon::Create(std::move(points));
        if (!polygon.HasValue())
            continue;

        const jaguar::SimplePolygon& value = polygon.Value();
        for (const jaguar::Point2d& point : value.Vertices())
            assert(point.IsFinite());
        assert(std::isfinite(value.BoundingBox().xMin));
        assert(std::isfinite(value.BoundingBox().yMin));
        assert(std::isfinite(value.BoundingBox().xMax));
        assert(std::isfinite(value.BoundingBox().yMax));
        assert(value.Centroid().IsFinite());
        assert(std::isfinite(value.Area()));
        assert(std::isfinite(value.Diameter()));

        const auto transformed = value.Transformed(
            jaguar::Transform2d::FromRotationTranslation(
                static_cast<double>(caseIndex % 16) * pi / 16.0,
                {static_cast<double>(caseIndex) * 0.25,
                 -static_cast<double>(caseIndex) * 0.125}));
        assert(transformed.HasValue());

        const auto surrogate = jaguar::Surrogate::Create(transformed.Value(), {});
        if (!surrogate.HasValue())
        {
            assert(surrogate.GetError().code == jaguar::ErrorCode::InvalidPolygon);
            continue;
        }
        for (const jaguar::Circle2d& pole : surrogate.Value().Poles())
        {
            assert(pole.center.IsFinite());
            assert(std::isfinite(pole.radius));
        }
        for (const jaguar::Edge2d& pier : surrogate.Value().Piers())
        {
            assert(pier.start.IsFinite());
            assert(pier.end.IsFinite());
        }
    }
}
