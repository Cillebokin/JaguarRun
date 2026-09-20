#include "JaguarCore/entities/container.h"
#include "JaguarCore/entities/item.h"
#include "JaguarCore/entities/layout.h"
#include "JaguarCore/assertions.h"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <random>
#include <vector>

namespace
{
jaguar::SimplePolygon rectangle(double xMin,
                                double yMin,
                                double xMax,
                                double yMax)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(polygon.hasValue());
    return polygon.value();
}

jaguar::Container makeContainer(jaguar::ContainerId id,
                                double width,
                                std::vector<jaguar::QualityZone> zones = {})
{
    jaguar::OriginalShape outer{
        rectangle(0.0, 0.0, width, 10.0),
        jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    jaguar::CDEConfig cdeConfig{};
    cdeConfig.quadtreeDepth = 4;
    cdeConfig.cdThreshold = 8;
    const auto container = jaguar::Container::create(
        id, std::move(outer), std::move(zones), cdeConfig);
    assert(container.hasValue());
    return container.value();
}

jaguar::Item makeItem(std::optional<std::size_t> minQuality = std::nullopt)
{
    jaguar::OriginalShape original{
        rectangle(0.0, 0.0, 2.0, 2.0),
        jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::create(0, 1001, std::move(original),
                                           surrogateConfig, minQuality);
    assert(item.hasValue());
    return item.value();
}
}

void runLayoutTests()
{
    const jaguar::Item item = makeItem();
    assert(std::abs(item.area() - 4.0) < 1.0e-12);
    assert(item.originalShape().shape.vertexCount() == 4);
    assert(item.collisionShape().surrogate() != nullptr);

    jaguar::Layout layout(makeContainer(10, 10.0));
    const auto placed = layout.placeItem(
        item, jaguar::Transform2d::fromRotationTranslation(0.0, {2.0, 2.0}));
    assert(placed.hasValue());
    assert(layout.placedItemCount() == 1);
    assert(layout.isFeasible());
    assert(layout.density() > 0.0);

    auto snapshot = layout.save();
    assert(jaguar::validateLayout(layout).hasValue());
    assert(jaguar::validateLayoutSnapshot(layout, snapshot).hasValue());
    assert(snapshot.cdeSnapshot.dynamicHazards.size() == 1);
    auto mismatchedHazardSnapshot = snapshot;
    mismatchedHazardSnapshot.cdeSnapshot.dynamicHazards[0].entity.itemIndex = 99;
    assert(!layout.restore(mismatchedHazardSnapshot).hasValue());
    assert(!jaguar::validateLayoutSnapshot(layout, mismatchedHazardSnapshot).hasValue());
    assert(layout.placedItemCount() == 1);
    assert(layout.isFeasible());

    auto geometryMismatchedSnapshot = snapshot;
    geometryMismatchedSnapshot.cdeSnapshot.dynamicHazards[0].shape =
        std::make_shared<const jaguar::SimplePolygon>(rectangle(8.0, 8.0, 9.0, 9.0));
    assert(!layout.restore(geometryMismatchedSnapshot).hasValue());
    auto dynamicFlagMismatchedSnapshot = snapshot;
    dynamicFlagMismatchedSnapshot.cdeSnapshot.dynamicHazards[0].dynamic = false;
    assert(!layout.restore(dynamicFlagMismatchedSnapshot).hasValue());

    const auto second = layout.placeItem(
        item, jaguar::Transform2d::fromRotationTranslation(0.0, {5.0, 2.0}));
    assert(second.hasValue());
    assert(layout.placedItemCount() == 2);
    assert(layout.restore(snapshot).hasValue());
    assert(layout.placedItemCount() == 1);
    assert(layout.isFeasible());

    const auto outside = layout.placeItem(
        item, jaguar::Transform2d::fromRotationTranslation(0.0, {20.0, 2.0}));
    assert(outside.hasValue());
    assert(!layout.isFeasible());
    assert(layout.removeItem(outside.value()).hasValue());
    assert(layout.isFeasible());
    const auto invalidRemove = layout.removeItem({999, 1});
    assert(!invalidRemove.hasValue());
    assert(invalidRemove.error().code == jaguar::ErrorCode::InvalidInput);

    const jaguar::QualityZone hole{0, {rectangle(4.0, 4.0, 6.0, 6.0)}};
    jaguar::Layout holedLayout(makeContainer(11, 10.0, {hole}));
    const auto inHole = holedLayout.placeItem(
        item, jaguar::Transform2d::fromRotationTranslation(0.0, {4.5, 4.5}));
    assert(inHole.hasValue());
    assert(!holedLayout.isFeasible());

    const jaguar::QualityZone qualityOne{1, {rectangle(0.0, 0.0, 4.0, 4.0)}};
    const jaguar::QualityZone qualityTwo{2, {rectangle(6.0, 0.0, 10.0, 4.0)}};
    const jaguar::Item qualityItem = makeItem(2);
    jaguar::Layout qualityOneLayout(
        makeContainer(13, 10.0, {qualityOne, qualityTwo}));
    const auto acceptableQualityPlacement = qualityOneLayout.placeItem(
        qualityItem, jaguar::Transform2d::fromRotationTranslation(0.0, {6.5, 0.5}));
    assert(acceptableQualityPlacement.hasValue());
    // Rust filters zones with quality < min_quality. A quality-2 zone is
    // therefore still a collision for an item requiring quality 2.
    assert(!qualityOneLayout.isFeasible());

    jaguar::Layout qualityTwoLayout(
        makeContainer(13, 10.0, {qualityOne, qualityTwo}));
    const auto rejectedQualityPlacement = qualityTwoLayout.placeItem(
        qualityItem, jaguar::Transform2d::fromRotationTranslation(0.0, {0.5, 0.5}));
    assert(rejectedQualityPlacement.hasValue());
    assert(qualityTwoLayout.isFeasible());

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
        assert(q1Layout.placeItem(
                   matrixItem,
                   jaguar::Transform2d::fromRotationTranslation(
                       0.0, {1.5, 1.5}))
                   .hasValue());
        assert(q1Layout.isFeasible() == qualityOneExpected[i]);

        jaguar::Layout q2Layout(
            makeContainer(140 + i, 10.0, {qualityOne, qualityTwo}));
        assert(q2Layout.placeItem(
                   matrixItem,
                   jaguar::Transform2d::fromRotationTranslation(
                       0.0, {7.5, 1.5}))
                   .hasValue());
        assert(q2Layout.isFeasible() == qualityTwoExpected[i]);

        jaguar::Layout holeMatrixLayout(
            makeContainer(150 + i, 10.0, {hole, qualityOne, qualityTwo}));
        assert(holeMatrixLayout.placeItem(
                   matrixItem,
                   jaguar::Transform2d::fromRotationTranslation(
                       0.0, {4.5, 4.5}))
                   .hasValue());
        assert(!holeMatrixLayout.isFeasible());
    }

    auto assertRebuildEquivalent = [](jaguar::Layout& current) {
        const auto state = current.save();
        jaguar::Layout rebuilt(state.container);
        assert(rebuilt.restore(state).hasValue());
        assert(rebuilt.placedItemCount() == current.placedItemCount());
        assert(std::abs(rebuilt.density() - current.density()) < 1.0e-12);
        assert(rebuilt.isFeasible() == current.isFeasible());
        const auto rebuiltState = rebuilt.save();
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
            const auto placedRandom = randomLayout.placeItem(
                item,
                jaguar::Transform2d::fromRotationTranslation(
                    (random() % 4) * (std::acos(-1.0) * 0.5),
                    {position(random), position(random)}));
            assert(placedRandom.hasValue());
            activeHandles.push_back(placedRandom.value());
        }
        else if (operation == 1)
        {
            const std::size_t index = random() % activeHandles.size();
            assert(randomLayout.removeItem(activeHandles[index]).hasValue());
            activeHandles.erase(activeHandles.begin() + static_cast<std::ptrdiff_t>(index));
        }
        else
        {
            const auto state = randomLayout.save();
            assert(randomLayout.restore(state).hasValue());
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
            const auto placedGrid = largerLayout.placeItem(
                item, jaguar::Transform2d::fromRotationTranslation(0.0, {x, y}));
            assert(placedGrid.hasValue());
            gridHandles[cell] = placedGrid.value();
        }
        else if (operation == 1 && !occupiedCells.empty())
        {
            const std::size_t cell =
                occupiedCells[largerRandom() % occupiedCells.size()];
            assert(largerLayout.removeItem(*gridHandles[cell]).hasValue());
            gridHandles[cell].reset();
        }
        else
        {
            const auto state = largerLayout.save();
            assert(largerLayout.restore(state).hasValue());
        }

        const auto state = largerLayout.save();
        assert(state.cdeSnapshot.dynamicHazards.size() ==
               largerLayout.placedItemCount());
        assert(jaguar::validateLayout(largerLayout).hasValue());
        assert(jaguar::validateLayoutSnapshot(largerLayout, state).hasValue());
        assert(largerLayout.isFeasible());
        if (step % 64 == 0)
        {
            jaguar::Layout rebuilt(state.container);
            assert(rebuilt.restore(state).hasValue());
            assert(rebuilt.placedItemCount() == largerLayout.placedItemCount());
            assert(rebuilt.isFeasible() == largerLayout.isFeasible());
            assert(std::abs(rebuilt.density() - largerLayout.density()) < 1.0e-12);
        }
    }

    const auto oldSnapshot = layout.save();
    assert(layout.swapContainer(makeContainer(12, 20.0)).hasValue());
    assert(layout.isFeasible());
    const auto mismatch = layout.restore(oldSnapshot);
    assert(!mismatch.hasValue());
    assert(mismatch.error().code == jaguar::ErrorCode::SnapshotMismatch);
}
