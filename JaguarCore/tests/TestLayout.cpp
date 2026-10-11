#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Assertions.h"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <limits>
#include <random>
#include <stdexcept>
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
    jaguar::OriginalShape emptyQualityZoneOuter{
        Rectangle(0.0, 0.0, 4.0, 4.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto emptyQualityZoneContainer = jaguar::Container::Create(
        18, std::move(emptyQualityZoneOuter),
        {jaguar::QualityZone{1, {}}}, jaguar::CDEConfig{});
    assert(emptyQualityZoneContainer.HasValue());

    jaguar::OriginalShape emptyDiscreteRotationShape{
        Rectangle(0.0, 0.0, 1.0, 1.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    jaguar::SurrogateConfig emptyDiscreteRotationSurrogate{};
    emptyDiscreteRotationSurrogate.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto emptyDiscreteRotationItem = jaguar::Item::Create(
        20, 1020, std::move(emptyDiscreteRotationShape),
        emptyDiscreteRotationSurrogate, std::nullopt,
        {jaguar::RotationMode::Discrete, {}});
    assert(emptyDiscreteRotationItem.HasValue());
    assert(emptyDiscreteRotationItem.Value().AllowedRotation().mode ==
           jaguar::RotationMode::Discrete);
    assert(emptyDiscreteRotationItem.Value().AllowedRotation().radians.empty());

    jaguar::OriginalShape roundedRotationShape{
        Rectangle(0.0, 0.0, 1.0, 1.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    const auto roundedRotationItem = jaguar::Item::Create(
        21, 1021, std::move(roundedRotationShape),
        emptyDiscreteRotationSurrogate, std::nullopt,
        {jaguar::RotationMode::Discrete, {1.00000006}});
    assert(roundedRotationItem.HasValue());
    assert(roundedRotationItem.Value().AllowedRotation().radians.size() == 1);
    assert(roundedRotationItem.Value().AllowedRotation().radians[0] ==
           static_cast<double>(static_cast<float>(1.00000006)));

    jaguar::OriginalShape precisionBoundsShape{
        Rectangle(-16777216.0, 0.0, 1.0, 2.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    jaguar::CDEConfig precisionBoundsConfig{};
    const auto precisionBoundsContainer = jaguar::Container::Create(
        17, std::move(precisionBoundsShape), {}, precisionBoundsConfig);
    assert(precisionBoundsContainer.HasValue());
    // Rust computes bbox width and square inflation in f32.
    assert(precisionBoundsContainer.Value().Bounds().yMin == -8388607.0);
    assert(precisionBoundsContainer.Value().Bounds().yMax == 8388609.0);

    const jaguar::Item item = makeItem();
    assert(std::abs(item.Area() - 4.0) < 1.0e-12);
    assert(item.GetOriginalShape().shape.VertexCount() == 4);
    assert(item.CollisionShape().GetSurrogate() != nullptr);

    auto makeIndexedItem = [](jaguar::ItemIndex index, jaguar::ItemId externalId) {
        jaguar::OriginalShape original{
            Rectangle(0.0, 0.0, 2.0, 2.0),
            jaguar::Transform2d::Identity(),
            jaguar::ShapeModifyMode::Inflate,
            {}};
        jaguar::SurrogateConfig config{};
        config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
        const auto created = jaguar::Item::Create(
            index, externalId, std::move(original), config);
        assert(created.HasValue());
        return created.Value();
    };
    jaguar::Layout distinctItemLayout(makeContainer(22, 30.0));
    assert(distinctItemLayout.Items().empty());
    const auto itemIndex5First = makeIndexedItem(5, 1005);
    const auto itemIndex6 = makeIndexedItem(6, 1006);
    const auto itemIndex5Later = makeIndexedItem(5, 1505);
    const auto itemIndex5FirstHandle = distinctItemLayout.PlaceItem(
        itemIndex5First,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    const auto itemIndex6Handle = distinctItemLayout.PlaceItem(
        itemIndex6,
        jaguar::Transform2d::FromRotationTranslation(0.0, {8.0, 1.0}));
    const auto itemIndex5LaterHandle = distinctItemLayout.PlaceItem(
        itemIndex5Later,
        jaguar::Transform2d::FromRotationTranslation(0.0, {15.0, 1.0}));
    assert(itemIndex5FirstHandle.HasValue() && itemIndex6Handle.HasValue() &&
           itemIndex5LaterHandle.HasValue());
    const auto distinctItems = distinctItemLayout.Items();
    assert(distinctItems.size() == 2);
    assert(distinctItems[0].get().Index() == 5);
    assert(distinctItems[0].get().ExternalId() == 1005);
    assert(distinctItems[1].get().Index() == 6);
    assert(distinctItems[1].get().ExternalId() == 1006);
    assert(distinctItemLayout.RemoveItem(itemIndex5FirstHandle.Value()).HasValue());
    const auto distinctItemsAfterRemoval = distinctItemLayout.Items();
    assert(distinctItemsAfterRemoval.size() == 2);
    assert(distinctItemsAfterRemoval[0].get().Index() == 6);
    assert(distinctItemsAfterRemoval[1].get().Index() == 5);
    assert(distinctItemsAfterRemoval[1].get().ExternalId() == 1505);

    auto makeDensityItem = [](jaguar::ItemIndex index, double side) {
        jaguar::OriginalShape original{
            Rectangle(0.0, 0.0, side, side),
            jaguar::Transform2d::Identity(),
            jaguar::ShapeModifyMode::Inflate,
            {}};
        jaguar::SurrogateConfig config{};
        config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
        const auto created = jaguar::Item::Create(
            index, 1100 + index, std::move(original), config);
        assert(created.HasValue());
        return created.Value();
    };
    jaguar::OriginalShape densityContainerShape{
        Rectangle(0.0, 0.0, 8192.0, 4096.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    jaguar::CDEConfig densityCdeConfig{};
    densityCdeConfig.quadtreeDepth = 4;
    densityCdeConfig.cdThreshold = 8;
    const auto densityContainer = jaguar::Container::Create(
        16, std::move(densityContainerShape), {}, densityCdeConfig);
    assert(densityContainer.HasValue());
    jaguar::Layout densityLayout(densityContainer.Value());
    const auto largeDensityItem = makeDensityItem(0, 4096.0);
    const auto unitDensityItem1 = makeDensityItem(1, 1.0);
    const auto unitDensityItem2 = makeDensityItem(2, 1.0);
    assert(densityLayout.PlaceItem(
               largeDensityItem,
               jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0}))
               .HasValue());
    assert(densityLayout.PlaceItem(
               unitDensityItem1,
               jaguar::Transform2d::FromRotationTranslation(0.0, {5000.0, 100.0}))
               .HasValue());
    assert(densityLayout.PlaceItem(
               unitDensityItem2,
               jaguar::Transform2d::FromRotationTranslation(0.0, {6000.0, 100.0}))
               .HasValue());
    // Rust accumulates these f32 areas in slot order: 2^24 + 1 + 1 rounds to 2^24.
    assert(densityLayout.Density() == 0.5);

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
    assert(!jaguar::ValidateLayoutSnapshot(layout, mismatchedHazardSnapshot).HasValue());
    jaguar::Layout mismatchedHazardLayout(layout.GetContainer());
#ifdef JAGUARCORE_TEST_RELEASE
    assert(mismatchedHazardLayout.Restore(mismatchedHazardSnapshot).HasValue());
#else
    bool mismatchedHazardThrew = false;
    try
    {
        (void)mismatchedHazardLayout.Restore(mismatchedHazardSnapshot);
    }
    catch (const std::logic_error&)
    {
        mismatchedHazardThrew = true;
    }
    assert(mismatchedHazardThrew);
#endif
    assert(mismatchedHazardLayout.PlacedItemCount() == 1);

    auto geometryMismatchedSnapshot = snapshot;
    geometryMismatchedSnapshot.cdeSnapshot.dynamicHazards[0].shape =
        std::make_shared<const jaguar::SimplePolygon>(Rectangle(8.0, 8.0, 9.0, 9.0));
    jaguar::Layout geometryMismatchedLayout(layout.GetContainer());
#ifdef JAGUARCORE_TEST_RELEASE
    assert(geometryMismatchedLayout.Restore(geometryMismatchedSnapshot).HasValue());
#else
    bool geometryMismatchThrew = false;
    try
    {
        (void)geometryMismatchedLayout.Restore(geometryMismatchedSnapshot);
    }
    catch (const std::logic_error&)
    {
        geometryMismatchThrew = true;
    }
    assert(geometryMismatchThrew);
#endif
    assert(geometryMismatchedLayout.PlacedItemCount() == 1);

    auto dynamicFlagMismatchedSnapshot = snapshot;
    dynamicFlagMismatchedSnapshot.cdeSnapshot.dynamicHazards[0].dynamic = false;
    jaguar::Layout dynamicFlagMismatchedLayout(layout.GetContainer());
#ifdef JAGUARCORE_TEST_RELEASE
    assert(dynamicFlagMismatchedLayout
               .Restore(dynamicFlagMismatchedSnapshot)
               .HasValue());
    assert(dynamicFlagMismatchedLayout.PlacedItemCount() == 1);
    assert(dynamicFlagMismatchedLayout.Save().cdeSnapshot.dynamicHazards.empty());
#else
    bool dynamicFlagMismatchThrew = false;
    try
    {
        (void)dynamicFlagMismatchedLayout.Restore(dynamicFlagMismatchedSnapshot);
    }
    catch (const std::logic_error&)
    {
        dynamicFlagMismatchThrew = true;
    }
    assert(dynamicFlagMismatchThrew);
    assert(dynamicFlagMismatchedLayout.PlacedItemCount() == 1);
#endif

    const auto second = layout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 2.0}));
    assert(second.HasValue());
    assert(layout.PlacedItemCount() == 2);
    const auto twoPlacementSnapshot = layout.Save();
    assert(twoPlacementSnapshot.cdeSnapshot.dynamicHazards.size() == 2);
    const auto& firstHazard =
        twoPlacementSnapshot.cdeSnapshot.dynamicHazards[0];
    const auto& secondHazard =
        twoPlacementSnapshot.cdeSnapshot.dynamicHazards[1];
    assert(!(firstHazard.entity == secondHazard.entity));
    assert(firstHazard.entity.placedItemHandle ==
           twoPlacementSnapshot.placements[0].handle);
    assert(secondHazard.entity.placedItemHandle ==
           twoPlacementSnapshot.placements[1].handle);
    assert(firstHazard.entity.transform.translation.x == 2.0);
    assert(secondHazard.entity.transform.translation.x == 5.0);
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
    assert(!qualityOneLayout.IsFeasible());

    jaguar::Layout qualityTwoLayout(
        makeContainer(13, 10.0, {qualityOne, qualityTwo}));
    const auto rejectedQualityPlacement = qualityTwoLayout.PlaceItem(
        qualityItem, jaguar::Transform2d::FromRotationTranslation(0.0, {0.5, 0.5}));
    assert(rejectedQualityPlacement.HasValue());
    assert(!qualityTwoLayout.IsFeasible());

    const std::vector<std::optional<std::size_t>> requiredQualities{
        std::nullopt, 0, 1, 2, 3};
    const std::vector<bool> qualityOneExpected{
        false, false, false, false, false};
    const std::vector<bool> qualityTwoExpected{
        false, false, false, false, false};
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

    jaguar::Layout slotMapReuseLayout(makeContainer(16, 20.0));
    const auto firstSlotHandle = slotMapReuseLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    const auto secondSlotHandle = slotMapReuseLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 1.0}));
    assert(firstSlotHandle.HasValue() && secondSlotHandle.HasValue());
    assert(firstSlotHandle.Value().index == 1);
    assert(firstSlotHandle.Value().generation == 1);
    assert(slotMapReuseLayout.RemoveItem(firstSlotHandle.Value()).HasValue());
    assert(slotMapReuseLayout.RemoveItem(secondSlotHandle.Value()).HasValue());
    const auto reusedMostRecentSlot = slotMapReuseLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {9.0, 1.0}));
    assert(reusedMostRecentSlot.HasValue());
    assert(reusedMostRecentSlot.Value().index == secondSlotHandle.Value().index);
    assert(reusedMostRecentSlot.Value().generation ==
           secondSlotHandle.Value().generation + 2);

    const auto slotMapReuseSnapshot = slotMapReuseLayout.Save();
    assert(slotMapReuseLayout.RemoveItem(reusedMostRecentSlot.Value()).HasValue());
    jaguar::Layout restoredSlotMapReuseLayout(slotMapReuseSnapshot.container);
    assert(restoredSlotMapReuseLayout.Restore(slotMapReuseSnapshot).HasValue());
    const auto reusedAfterRestore = restoredSlotMapReuseLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {13.0, 1.0}));
    assert(reusedAfterRestore.HasValue());
    assert(reusedAfterRestore.Value().index == firstSlotHandle.Value().index);
    assert(reusedAfterRestore.Value().generation ==
           firstSlotHandle.Value().generation + 2);
    assert(!restoredSlotMapReuseLayout.RemoveItem(
                firstSlotHandle.Value())
                .HasValue());
    assert(!restoredSlotMapReuseLayout.RemoveItem(
                secondSlotHandle.Value())
                .HasValue());
    assert(restoredSlotMapReuseLayout.RemoveItem(
               reusedMostRecentSlot.Value())
               .HasValue());

    jaguar::Layout cdeRestoreLayout(makeContainer(18, 20.0));
    const auto cdeRestoreHandleA = cdeRestoreLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    const auto cdeRestoreHandleB = cdeRestoreLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 1.0}));
    assert(cdeRestoreHandleA.HasValue() && cdeRestoreHandleB.HasValue());
    const auto cdeRestoreSnapshot = cdeRestoreLayout.Save();
    assert(cdeRestoreLayout.RemoveItem(cdeRestoreHandleA.Value()).HasValue());
    assert(cdeRestoreLayout.RemoveItem(cdeRestoreHandleB.Value()).HasValue());
    const auto cdeRestoreReplacement = cdeRestoreLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {9.0, 1.0}));
    assert(cdeRestoreReplacement.HasValue());
    assert(cdeRestoreLayout.Restore(cdeRestoreSnapshot).HasValue());
    assert(jaguar::ValidateLayoutSnapshot(cdeRestoreLayout,
                                          cdeRestoreSnapshot).HasValue());
    assert(cdeRestoreLayout.RemoveItem(cdeRestoreHandleA.Value()).HasValue());
    assert(cdeRestoreLayout.RemoveItem(cdeRestoreHandleB.Value()).HasValue());

    jaguar::Layout swapAfterRestoreLayout(makeContainer(19, 20.0));
    const auto swapRestoreFirst = swapAfterRestoreLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    const auto swapRestoreSecond = swapAfterRestoreLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 1.0}));
    assert(swapRestoreFirst.HasValue() && swapRestoreSecond.HasValue());
    const auto swapRestoreSnapshot = swapAfterRestoreLayout.Save();
    assert(swapAfterRestoreLayout.RemoveItem(swapRestoreFirst.Value()).HasValue());
    assert(swapAfterRestoreLayout.RemoveItem(swapRestoreSecond.Value()).HasValue());
    assert(swapAfterRestoreLayout
               .PlaceItem(item,
                          jaguar::Transform2d::FromRotationTranslation(
                              0.0, {9.0, 1.0}))
               .HasValue());
    assert(swapAfterRestoreLayout.Restore(swapRestoreSnapshot).HasValue());
    assert(swapAfterRestoreLayout.SwapContainer(makeContainer(20, 21.0)).HasValue());
    const auto hazardsAfterSwapRestore =
        swapAfterRestoreLayout.Save().cdeSnapshot.dynamicHazards;
    assert(hazardsAfterSwapRestore.size() == 2);
    // jagua-rs re-registers in the restored CDE snapshot order (second, first).
    assert(hazardsAfterSwapRestore[0].entity.placedItemHandle ==
           swapRestoreSecond.Value());
    assert(hazardsAfterSwapRestore[1].entity.placedItemHandle ==
           swapRestoreFirst.Value());

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
