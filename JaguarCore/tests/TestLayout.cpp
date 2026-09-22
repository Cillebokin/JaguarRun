#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Assertions.h"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>
#include <vector>

namespace
{
jaguar::SimplePolygon Rectangle(double xMin,
                                double yMin,
                                double xMax,
                                double yMax)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(polygon.HasValue());
    return polygon.Value();
}

jaguar::Container makeContainer(jaguar::ContainerId id,
                                double width,
                                std::vector<jaguar::QualityZone> zones = {})
{
    jaguar::OriginalShape outer{
        Rectangle(0.0, 0.0, width, 10.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    jaguar::CDEConfig cdeConfig{};
    cdeConfig.quadtreeDepth = 4;
    cdeConfig.cdThreshold = 8;
    const auto container = jaguar::Container::Create(
        id, std::move(outer), std::move(zones), cdeConfig);
    assert(container.HasValue());
    return container.Value();
}

jaguar::Item makeItem(std::optional<std::size_t> minQuality = std::nullopt)
{
    jaguar::OriginalShape original{
        Rectangle(0.0, 0.0, 2.0, 2.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(0, 1001, std::move(original),
                                           surrogateConfig, minQuality);
    assert(item.HasValue());
    return item.Value();
}
}

void runLayoutTests()
{
    const jaguar::Item item = makeItem();
    assert(std::abs(item.Area() - 4.0) < 1.0e-12);
    assert(item.GetOriginalShape().shape.VertexCount() == 4);
    assert(item.CollisionShape().GetSurrogate() != nullptr);

    jaguar::Layout layout(makeContainer(10, 10.0));
    const auto invalidPlacement = layout.PlaceItem(
        item,
        jaguar::Transform2d::FromRotationTranslation(
            std::numeric_limits<double>::quiet_NaN(), {2.0, 2.0}));
    assert(!invalidPlacement.HasValue());
    assert(invalidPlacement.GetError().code == jaguar::ErrorCode::InvalidInput);
    assert(layout.PlacedItemCount() == 0);

    const auto placed = layout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {2.0, 2.0}));
    assert(placed.HasValue());
    assert(layout.PlacedItemCount() == 1);
    assert(layout.IsFeasible());
    assert(layout.Density() > 0.0);

    auto snapshot = layout.Save();
    assert(jaguar::ValidateLayout(layout).HasValue());
    assert(jaguar::ValidateLayoutSnapshot(layout, snapshot).HasValue());
    assert(snapshot.cdeSnapshot.dynamicHazards.size() == 1);
    auto mismatchedHazardSnapshot = snapshot;
    mismatchedHazardSnapshot.cdeSnapshot.dynamicHazards[0].entity.itemIndex = 99;
    assert(!layout.Restore(mismatchedHazardSnapshot).HasValue());
    assert(!jaguar::ValidateLayoutSnapshot(layout, mismatchedHazardSnapshot).HasValue());
    assert(layout.PlacedItemCount() == 1);
    assert(layout.IsFeasible());

    auto geometryMismatchedSnapshot = snapshot;
    geometryMismatchedSnapshot.cdeSnapshot.dynamicHazards[0].shape =
        std::make_shared<const jaguar::SimplePolygon>(Rectangle(8.0, 8.0, 9.0, 9.0));
    assert(!layout.Restore(geometryMismatchedSnapshot).HasValue());
    auto dynamicFlagMismatchedSnapshot = snapshot;
    dynamicFlagMismatchedSnapshot.cdeSnapshot.dynamicHazards[0].dynamic = false;
    assert(!layout.Restore(dynamicFlagMismatchedSnapshot).HasValue());

    const auto second = layout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 2.0}));
    assert(second.HasValue());
    assert(layout.PlacedItemCount() == 2);
    assert(layout.Restore(snapshot).HasValue());
    assert(layout.PlacedItemCount() == 1);
    assert(layout.IsFeasible());

    const auto outside = layout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {20.0, 2.0}));
    assert(outside.HasValue());
    assert(!layout.IsFeasible());
    assert(layout.RemoveItem(outside.Value()).HasValue());
    assert(layout.IsFeasible());
    const auto invalidRemove = layout.RemoveItem({999, 1});
    assert(!invalidRemove.HasValue());
    assert(invalidRemove.GetError().code == jaguar::ErrorCode::InvalidInput);

    const jaguar::QualityZone hole{0, {Rectangle(4.0, 4.0, 6.0, 6.0)}};
    jaguar::Layout holedLayout(makeContainer(11, 10.0, {hole}));
    const auto inHole = holedLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {4.5, 4.5}));
    assert(inHole.HasValue());
    assert(!holedLayout.IsFeasible());

    const jaguar::QualityZone qualityOne{1, {Rectangle(0.0, 0.0, 4.0, 4.0)}};
    const jaguar::QualityZone qualityTwo{2, {Rectangle(6.0, 0.0, 10.0, 4.0)}};
    const jaguar::Item qualityItem = makeItem(2);
    jaguar::Layout qualityOneLayout(
        makeContainer(13, 10.0, {qualityOne, qualityTwo}));
    const auto acceptableQualityPlacement = qualityOneLayout.PlaceItem(
        qualityItem, jaguar::Transform2d::FromRotationTranslation(0.0, {6.5, 0.5}));
    assert(acceptableQualityPlacement.HasValue());
    // Rust filters zones with quality < min_quality. A quality-2 zone is
    // therefore still a collision for an item requiring quality 2.
    assert(!qualityOneLayout.IsFeasible());

    jaguar::Layout qualityTwoLayout(
        makeContainer(13, 10.0, {qualityOne, qualityTwo}));
    const auto rejectedQualityPlacement = qualityTwoLayout.PlaceItem(
        qualityItem, jaguar::Transform2d::FromRotationTranslation(0.0, {0.5, 0.5}));
    assert(rejectedQualityPlacement.HasValue());
    assert(qualityTwoLayout.IsFeasible());

    const std::vector<std::optional<std::size_t>> requiredQualities{
        std::nullopt, 0, 1, 2, 3};
    const std::vector<bool> qualityOneExpected{
        false, false, false, true, true};
    const std::vector<bool> qualityTwoExpected{
        false, false, false, false, true};
    for (std::size_t i = 0; i < requiredQualities.size(); ++i)
    {
        const jaguar::Item matrixItem = makeItem(requiredQualities[i]);

        jaguar::Layout q1Layout(
            makeContainer(130 + i, 10.0, {qualityOne, qualityTwo}));
        assert(q1Layout.PlaceItem(
                   matrixItem,
                   jaguar::Transform2d::FromRotationTranslation(
                       0.0, {1.5, 1.5}))
                   .HasValue());
        assert(q1Layout.IsFeasible() == qualityOneExpected[i]);

        jaguar::Layout q2Layout(
            makeContainer(140 + i, 10.0, {qualityOne, qualityTwo}));
        assert(q2Layout.PlaceItem(
                   matrixItem,
                   jaguar::Transform2d::FromRotationTranslation(
                       0.0, {7.5, 1.5}))
                   .HasValue());
        assert(q2Layout.IsFeasible() == qualityTwoExpected[i]);

        jaguar::Layout holeMatrixLayout(
            makeContainer(150 + i, 10.0, {hole, qualityOne, qualityTwo}));
        assert(holeMatrixLayout.PlaceItem(
                   matrixItem,
                   jaguar::Transform2d::FromRotationTranslation(
                       0.0, {4.5, 4.5}))
                   .HasValue());
        assert(!holeMatrixLayout.IsFeasible());
    }

    auto assertRebuildEquivalent = [](jaguar::Layout& current) {
        const auto state = current.Save();
        jaguar::Layout rebuilt(state.container);
        assert(rebuilt.Restore(state).HasValue());
        assert(rebuilt.PlacedItemCount() == current.PlacedItemCount());
        assert(std::abs(rebuilt.Density() - current.Density()) < 1.0e-12);
        assert(rebuilt.IsFeasible() == current.IsFeasible());
        const auto rebuiltState = rebuilt.Save();
        assert(rebuiltState.cdeSnapshot.dynamicHazards.size() ==
               state.cdeSnapshot.dynamicHazards.size());
    };

    jaguar::Layout randomLayout(makeContainer(14, 20.0));
    std::vector<jaguar::PlacedItemHandle> activeHandles;
    std::mt19937 random(0x4A475243u);
    std::uniform_real_distribution<double> position(-2.0, 18.0);
    for (std::size_t step = 0; step < 1000; ++step)
    {
        const std::size_t operation = random() % 3;
        if (activeHandles.empty() || operation == 0)
        {
            const auto placedRandom = randomLayout.PlaceItem(
                item,
                jaguar::Transform2d::FromRotationTranslation(
                    (random() % 4) * (std::acos(-1.0) * 0.5),
                    {position(random), position(random)}));
            assert(placedRandom.HasValue());
            activeHandles.push_back(placedRandom.Value());
        }
        else if (operation == 1)
        {
            const std::size_t index = random() % activeHandles.size();
            assert(randomLayout.RemoveItem(activeHandles[index]).HasValue());
            activeHandles.erase(activeHandles.begin() + static_cast<std::ptrdiff_t>(index));
        }
        else
        {
            const auto state = randomLayout.Save();
            assert(randomLayout.Restore(state).HasValue());
        }
        assertRebuildEquivalent(randomLayout);
    }

    // Larger feasible Layout state: keep placements on a deterministic grid
    // so the test stresses Slot/CDE/snapshot cardinality without turning
    // collision rejection into the random variable.
    jaguar::Layout largerLayout(makeContainer(15, 128.0));
    std::vector<std::optional<jaguar::PlacedItemHandle>> gridHandles(64);
    std::mt19937_64 largerRandom(0x4C41594F55545F4FULL);
    for (std::size_t step = 0; step < 512; ++step)
    {
        std::vector<std::size_t> freeCells;
        std::vector<std::size_t> occupiedCells;
        for (std::size_t cell = 0; cell < gridHandles.size(); ++cell)
        {
            (gridHandles[cell].has_value() ? occupiedCells : freeCells)
                .push_back(cell);
        }

        const std::size_t operation = largerRandom() % 3;
        if ((operation == 0 || occupiedCells.empty()) && !freeCells.empty())
        {
            const std::size_t cell = freeCells[largerRandom() % freeCells.size()];
            const double x = 1.0 + static_cast<double>(cell % 32) * 4.0;
            const double y = 1.0 + static_cast<double>(cell / 32) * 4.0;
            const auto placedGrid = largerLayout.PlaceItem(
                item, jaguar::Transform2d::FromRotationTranslation(0.0, {x, y}));
            assert(placedGrid.HasValue());
            gridHandles[cell] = placedGrid.Value();
        }
        else if (operation == 1 && !occupiedCells.empty())
        {
            const std::size_t cell =
                occupiedCells[largerRandom() % occupiedCells.size()];
            assert(largerLayout.RemoveItem(*gridHandles[cell]).HasValue());
            gridHandles[cell].reset();
        }
        else
        {
            const auto state = largerLayout.Save();
            assert(largerLayout.Restore(state).HasValue());
        }

        const auto state = largerLayout.Save();
        assert(state.cdeSnapshot.dynamicHazards.size() ==
               largerLayout.PlacedItemCount());
        assert(jaguar::ValidateLayout(largerLayout).HasValue());
        assert(jaguar::ValidateLayoutSnapshot(largerLayout, state).HasValue());
        assert(largerLayout.IsFeasible());
        if (step % 64 == 0)
        {
            jaguar::Layout rebuilt(state.container);
            assert(rebuilt.Restore(state).HasValue());
            assert(rebuilt.PlacedItemCount() == largerLayout.PlacedItemCount());
            assert(rebuilt.IsFeasible() == largerLayout.IsFeasible());
            assert(std::abs(rebuilt.Density() - largerLayout.Density()) < 1.0e-12);
        }
    }

    const auto oldSnapshot = layout.Save();
    assert(layout.SwapContainer(makeContainer(12, 20.0)).HasValue());
    assert(layout.IsFeasible());
    const auto mismatch = layout.Restore(oldSnapshot);
    assert(!mismatch.HasValue());
    assert(mismatch.GetError().code == jaguar::ErrorCode::SnapshotMismatch);
}
