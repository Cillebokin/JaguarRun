#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace
{
constexpr std::size_t LAYOUT_KEY_PART_BITS = sizeof(std::size_t) * 4U;
constexpr std::size_t LAYOUT_KEY_PART_MASK =
    std::numeric_limits<std::size_t>::max() >> LAYOUT_KEY_PART_BITS;

std::size_t layoutKeySlot(std::size_t key)
{
    return key & LAYOUT_KEY_PART_MASK;
}

std::size_t layoutKeyGeneration(std::size_t key)
{
    return key >> LAYOUT_KEY_PART_BITS;
}

jaguar::Item makeItem(jaguar::ItemIndex index, double width, double height)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(polygon.HasValue());
    jaguar::OriginalShape original{
        polygon.Value(), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(index, 3000 + index,
                                           std::move(original), config);
    assert(item.HasValue());
    return item.Value();
}
}

void runMsppTests()
{
    const auto item0 = makeItem(0, 2.0, 2.0);
    const auto item1 = makeItem(1, 1.0, 2.0);
    jaguar::CDEConfig cdeConfig{};
    jaguar::ShapeModifyConfig shapeConfig{};
    const auto infiniteMaxWidthStrip = jaguar::MultiStrip::Create(
        std::numeric_limits<double>::infinity(), 5.0, cdeConfig,
        shapeConfig, 10.0);
    assert(infiniteMaxWidthStrip.HasValue());
    assert(std::isinf(infiniteMaxWidthStrip.Value().maxWidth));
    auto infiniteWidthStrip = infiniteMaxWidthStrip.Value();
    assert(infiniteWidthStrip.SetWidth(
        std::numeric_limits<double>::infinity()).HasValue());
    assert(std::isinf(infiniteWidthStrip.width));
    const auto instance = jaguar::MSPInstance::Create(
        {item0, item1}, {2, 1}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(instance.HasValue());
    assert(instance.Value().TotalDemand() == 3);
    assert(instance.Value().TotalItemArea() == 10.0);
    assert(instance.Value().ItemIndexFromExternalId(3000).has_value());
    assert(instance.Value().ItemIndexFromExternalId(3000).value() == 0);
    assert(!instance.Value().ItemIndexFromExternalId(9999).has_value());
    assert(instance.Value().BaseStrip().width == 10.0);

    const auto f32AreaInstance = jaguar::MSPInstance::Create(
        {makeItem(0, 4096.0, 4096.0), makeItem(1, 1.0, 1.0),
         makeItem(2, 1.0, 1.0)},
        {1, 1, 1}, 1.0, 1.0, cdeConfig, shapeConfig);
    assert(f32AreaInstance.HasValue());
    // Rust rounds after each f32 addition: 2^24 + 1 + 1 remains 2^24.
    assert(f32AreaInstance.Value().TotalItemArea() == 16777216.0);

    const auto narrowBaseStrip = jaguar::MultiStrip::Create(
        10.0, 5.0, cdeConfig, shapeConfig, 4.0);
    assert(narrowBaseStrip.HasValue());
    const auto narrowBaseInstance = jaguar::MSPInstance::Create(
        {item0}, {1}, narrowBaseStrip.Value());
    assert(narrowBaseInstance.HasValue());
    assert(narrowBaseInstance.Value().BaseStrip().maxWidth == 10.0);
    assert(narrowBaseInstance.Value().BaseStrip().width == 4.0);

    auto problem = jaguar::MSPProblem::Create(instance.Value());
    assert(problem.HasValue());
    assert(std::isnan(problem.Value().Density()));
    assert(std::isnan(problem.Value().Save().Density()));
    const auto firstLayout = problem.Value().AddLayout({
        10.0, 5.0, 5.0, cdeConfig, shapeConfig});
    const auto secondLayout = problem.Value().AddLayout({
        10.0, 5.0, 6.0, cdeConfig, shapeConfig});
    assert(firstLayout.HasValue());
    assert(secondLayout.HasValue());
    assert(layoutKeySlot(firstLayout.Value()) == 1);
    assert(layoutKeyGeneration(firstLayout.Value()) == 1);
    assert(layoutKeySlot(secondLayout.Value()) == 2);
    assert(layoutKeyGeneration(secondLayout.Value()) == 1);
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().TotalStripWidth() == 11.0);

    const auto first = problem.Value().PlaceItem({
        firstLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    const auto second = problem.Value().PlaceItem({
        secondLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    const auto third = problem.Value().PlaceItem({
        secondLayout.Value(), 1,
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 1.0})});
    assert(first.HasValue() && second.HasValue() && third.HasValue());
    assert(problem.Value().RemainingDemand(0) == 0);
    assert(problem.Value().RemainingDemand(1) == 0);

    const auto tooWide = problem.Value().ChangeStripWidth(firstLayout.Value(), 11.0);
    assert(!tooWide.HasValue());
    assert(tooWide.GetError().code == jaguar::ErrorCode::InvalidInput);

    assert(problem.Value().ChangeStripWidth(firstLayout.Value(), 8.0).HasValue());
    assert(problem.Value().StripWidth(firstLayout.Value()) == 8.0);
    assert(problem.Value().FitStrip(firstLayout.Value()).HasValue());
    assert(problem.Value().StripWidth(firstLayout.Value()) < 8.0);

    const auto snapshot = problem.Value().Save();
    assert(jaguar::ValidateMSPSolution(problem.Value(), snapshot).HasValue());

    auto mismatchedStripContainer = snapshot;
    assert(!mismatchedStripContainer.layouts.empty());
    mismatchedStripContainer.layouts.front().strip.width *= 0.5;
    assert(mismatchedStripContainer.layouts.front().strip.width > 0.0);
    assert(!problem.Value().Restore(mismatchedStripContainer).HasValue());
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().PlacedItemCount() == 3);

    auto mismatchedStripSpec = snapshot;
    mismatchedStripSpec.layouts.front().strip.maxWidth = 20.0;
    assert(problem.Value().Restore(mismatchedStripSpec).HasValue());
    assert(problem.Value().Save().layouts.front().strip.maxWidth == 20.0);
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().PlacedItemCount() == 3);

    auto mismatchedStripConfig = snapshot;
    mismatchedStripConfig.layouts.front().strip.cdeConfig.cdThreshold = 1;
    assert(problem.Value().Restore(mismatchedStripConfig).HasValue());
    assert(problem.Value().Save().layouts.front().strip.cdeConfig.cdThreshold ==
           1);
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().PlacedItemCount() == 3);

    assert(problem.Value().RemoveItem(second.Value()).HasValue());
    assert(problem.Value().RemainingDemand(0) == 1);
    assert(problem.Value().RemoveLayout(firstLayout.Value()).HasValue());
    assert(problem.Value().LayoutCount() == 1);
    const auto changedKeyRestore = problem.Value().Restore(snapshot);
    assert(changedKeyRestore.HasValue());
    assert(changedKeyRestore.Value());
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().PlacedItemCount() == 3);
    assert(problem.Value().RemainingDemand(0) == 0);
    assert(problem.Value().RemainingDemand(1) == 0);
    assert(problem.Value().TotalStripWidth() > 0.0);
    assert(problem.Value().Density() > 0.0);

    const auto staleHandleInstance = jaguar::MSPInstance::Create(
        {item0}, {2}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(staleHandleInstance.HasValue());
    auto staleHandleProblem = jaguar::MSPProblem::Create(
        staleHandleInstance.Value());
    assert(staleHandleProblem.HasValue());
    const auto oldLayout = staleHandleProblem.Value().AddLayout(
        {10.0, 5.0, 10.0, cdeConfig, shapeConfig});
    assert(oldLayout.HasValue());
    assert(staleHandleProblem.Value().PlaceItem({
        oldLayout.Value(), 0, jaguar::Transform2d::Identity()}).HasValue());
    const auto oldLayoutSnapshot = staleHandleProblem.Value().Save();
    assert(staleHandleProblem.Value().RemoveLayout(oldLayout.Value()).HasValue());
    const auto replacementLayout = staleHandleProblem.Value().AddLayout(
        {10.0, 5.0, 10.0, cdeConfig, shapeConfig});
    assert(replacementLayout.HasValue());
    assert(staleHandleProblem.Value().PlaceItem({
        replacementLayout.Value(), 0, jaguar::Transform2d::Identity()}).HasValue());
    const auto staleSnapshotRestore = staleHandleProblem.Value().Restore(
        oldLayoutSnapshot);
    assert(staleSnapshotRestore.HasValue());
    assert(staleSnapshotRestore.Value());
    const auto staleLayoutPlacement = staleHandleProblem.Value().PlaceItem({
        oldLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {2.0, 0.0})});
    assert(!staleLayoutPlacement.HasValue());
    assert(staleLayoutPlacement.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto restoreOrderInstance = jaguar::MSPInstance::Create(
        {item0}, {2}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(restoreOrderInstance.HasValue());
    auto restoreOrderProblem =
        jaguar::MSPProblem::Create(restoreOrderInstance.Value());
    assert(restoreOrderProblem.HasValue());
    assert(restoreOrderProblem.Value().AddLayout(
        {10.0, 5.0, 4.0, cdeConfig, shapeConfig}).HasValue());
    assert(restoreOrderProblem.Value().AddLayout(
        {10.0, 5.0, 6.0, cdeConfig, shapeConfig}).HasValue());
    auto restoreOrderSnapshot = restoreOrderProblem.Value().Save();
    assert(restoreOrderSnapshot.layouts.size() == 2);
    assert(restoreOrderProblem.Value().RemoveLayout(
        restoreOrderSnapshot.layouts[0].layoutIndex).HasValue());
    assert(restoreOrderProblem.Value().RemoveLayout(
        restoreOrderSnapshot.layouts[1].layoutIndex).HasValue());
    std::reverse(restoreOrderSnapshot.layouts.begin(),
                 restoreOrderSnapshot.layouts.end());
    assert(restoreOrderProblem.Value().Restore(restoreOrderSnapshot).HasValue());
    const auto restoredOrderSnapshot = restoreOrderProblem.Value().Save();
    assert(restoredOrderSnapshot.layouts.size() == 2);
    // Rust's SecondaryMap restores snapshots in ascending key order regardless
    // of the order in which a C++ vector happens to present them.
    assert(restoredOrderSnapshot.layouts[0].strip.width == 6.0);
    assert(restoredOrderSnapshot.layouts[1].strip.width == 4.0);

    const auto largeDensityItem = makeItem(0, 4096.0, 4096.0);
    const auto unitDensityItem = makeItem(1, 1.0, 1.0);
    const auto densityInstance = jaguar::MSPInstance::Create(
        {largeDensityItem, unitDensityItem}, {1, 2}, 8192.0, 4096.0,
        cdeConfig, shapeConfig);
    assert(densityInstance.HasValue());
    auto densityProblem = jaguar::MSPProblem::Create(densityInstance.Value());
    assert(densityProblem.HasValue());
    const auto densityLayout = densityProblem.Value().AddLayout(
        {8192.0, 4096.0, 8192.0, cdeConfig, shapeConfig});
    assert(densityLayout.HasValue());
    assert(densityProblem.Value().PlaceItem({
        densityLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0})})
               .HasValue());
    assert(densityProblem.Value().PlaceItem({
        densityLayout.Value(), 1,
        jaguar::Transform2d::FromRotationTranslation(0.0, {5000.0, 100.0})})
               .HasValue());
    assert(densityProblem.Value().PlaceItem({
        densityLayout.Value(), 1,
        jaguar::Transform2d::FromRotationTranslation(0.0, {6000.0, 100.0})})
               .HasValue());
    // Rust f32 aggregation rounds 2^24 + 1 + 1 to 2^24.
    assert(densityProblem.Value().Density() == 0.5);
    auto densitySolution = densityProblem.Value().Save();
    assert(densitySolution.Density() == 0.5);
    std::reverse(densitySolution.layouts[0].layoutSnapshot.placements.begin(),
                 densitySolution.layouts[0].layoutSnapshot.placements.end());
    assert(densitySolution.Density() == 0.5);

    const auto stripWidthItem = makeItem(0, 1.0, 1.0);
    const auto stripWidthInstance = jaguar::MSPInstance::Create(
        {stripWidthItem}, {1}, 16777216.0, 1.0, cdeConfig, shapeConfig);
    assert(stripWidthInstance.HasValue());
    auto stripWidthProblem =
        jaguar::MSPProblem::Create(stripWidthInstance.Value());
    assert(stripWidthProblem.HasValue());
    assert(stripWidthProblem.Value().AddLayout(
        {16777216.0, 1.0, 16777216.0, cdeConfig, shapeConfig})
               .HasValue());
    assert(stripWidthProblem.Value().AddLayout(
        {16777216.0, 1.0, 1.0, cdeConfig, shapeConfig})
               .HasValue());
    assert(stripWidthProblem.Value().AddLayout(
        {16777216.0, 1.0, 1.0, cdeConfig, shapeConfig})
               .HasValue());
    // Rust sums the widths as f32, so each trailing 1 rounds away at 2^24.
    assert(stripWidthProblem.Value().TotalStripWidth() == 16777216.0);
    auto stripWidthSolution = stripWidthProblem.Value().Save();
    assert(stripWidthSolution.TotalStripWidth() == 16777216.0);
    std::reverse(stripWidthSolution.layouts.begin(),
                 stripWidthSolution.layouts.end());
    assert(stripWidthSolution.TotalStripWidth() == 16777216.0);

    const auto churnInstance = jaguar::MSPInstance::Create(
        {stripWidthItem}, {1}, 16777216.0, 1.0, cdeConfig, shapeConfig);
    assert(churnInstance.HasValue());
    auto churnProblem = jaguar::MSPProblem::Create(churnInstance.Value());
    assert(churnProblem.HasValue());
    const auto removedLayout = churnProblem.Value().AddLayout(
        {16777216.0, 1.0, 1.0, cdeConfig, shapeConfig});
    assert(removedLayout.HasValue());
    const auto secondChurnLayout = churnProblem.Value().AddLayout(
        {16777216.0, 1.0, 1.0, cdeConfig, shapeConfig});
    assert(secondChurnLayout.HasValue());
    const auto largeChurnLayout = churnProblem.Value().AddLayout(
        {16777216.0, 1.0, 16777216.0, cdeConfig, shapeConfig});
    assert(largeChurnLayout.HasValue());
    assert(churnProblem.Value().RemoveLayout(removedLayout.Value()).HasValue());
    const auto reusedChurnLayout = churnProblem.Value().AddLayout(
        {16777216.0, 1.0, 1.0, cdeConfig, shapeConfig});
    assert(reusedChurnLayout.HasValue());
    // Rust reuses the freed slot, so the two small widths add before 2^24.
    assert(churnProblem.Value().TotalStripWidth() == 16777218.0);
    assert(churnProblem.Value().PlaceItem({
        reusedChurnLayout.Value(), 0, jaguar::Transform2d::Identity()})
               .HasValue());
    const float rustOrderedChurnDensity =
        1.0F / (2.0F + 16777216.0F);
    auto churnSnapshot = churnProblem.Value().Save();
    assert(churnSnapshot.Density() ==
           static_cast<double>(rustOrderedChurnDensity));
    std::reverse(churnSnapshot.layouts.begin(), churnSnapshot.layouts.end());
    assert(churnSnapshot.Density() ==
           static_cast<double>(rustOrderedChurnDensity));

    const auto overMaxInstance = jaguar::MSPInstance::Create(
        {stripWidthItem}, {1}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(overMaxInstance.HasValue());
    auto overMaxProblem = jaguar::MSPProblem::Create(overMaxInstance.Value());
    assert(overMaxProblem.HasValue());
    const auto overMaxLayout = overMaxProblem.Value().AddLayout(
        {10.0, 5.0, 11.0, cdeConfig, shapeConfig});
    assert(overMaxLayout.HasValue());
    assert(overMaxProblem.Value().StripWidth(overMaxLayout.Value()) == 11.0);
    const auto underMaxLayout = overMaxProblem.Value().AddLayout(
        {10.0, 5.0, 9.0, cdeConfig, shapeConfig});
    const auto maxWidthLayout = overMaxProblem.Value().AddLayout(
        {10.0, 5.0, 10.0, cdeConfig, shapeConfig});
    assert(underMaxLayout.HasValue() && maxWidthLayout.HasValue());
    const auto extendableStrips = overMaxProblem.Value().ExtendableStrips();
    assert(extendableStrips.size() == 1);
    assert(extendableStrips.front() == underMaxLayout.Value());
    assert(!overMaxProblem.Value()
                .ChangeStripWidth(overMaxLayout.Value(), 11.0)
                .HasValue());
    const auto overMaxSnapshot = overMaxProblem.Value().Save();
    auto restoredOverMaxProblem =
        jaguar::MSPProblem::Create(overMaxInstance.Value());
    assert(restoredOverMaxProblem.HasValue());
    assert(restoredOverMaxProblem.Value().Restore(overMaxSnapshot).HasValue());
    assert(restoredOverMaxProblem.Value().StripWidth(overMaxLayout.Value()) ==
           11.0);

    jaguar::ShapeModifyConfig fitShapeConfig = shapeConfig;
    fitShapeConfig.offset = 0.1;
    const auto fitInstance = jaguar::MSPInstance::Create(
        {makeItem(0, 2.0, 1.0)}, {1}, 10.0, 4.0, cdeConfig,
        fitShapeConfig);
    assert(fitInstance.HasValue());
    auto fitProblem = jaguar::MSPProblem::Create(fitInstance.Value());
    assert(fitProblem.HasValue());
    const auto fitLayout = fitProblem.Value().AddLayout(
        {10.0, 4.0, 10.0, cdeConfig, fitShapeConfig});
    assert(fitLayout.HasValue());
    assert(fitProblem.Value().PlaceItem({
        fitLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.25, 1.0})})
               .HasValue());
    assert(fitProblem.Value().FitStrip(fitLayout.Value()).HasValue());
    assert(std::abs(fitProblem.Value().StripWidth(fitLayout.Value()) -
                    static_cast<double>(3.350032329559326F)) < 1.0e-12);

    const auto customStripInstance = jaguar::MSPInstance::Create(
        {item0}, {1}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(customStripInstance.HasValue());
    auto customStripProblem =
        jaguar::MSPProblem::Create(customStripInstance.Value());
    assert(customStripProblem.HasValue());
    const auto customStripLayout = customStripProblem.Value().AddLayout(
        {20.0, 5.0, 4.0, cdeConfig, shapeConfig});
    assert(customStripLayout.HasValue());
    const auto customStripSolution = customStripProblem.Value().Save();
    auto customStripRestoredProblem =
        jaguar::MSPProblem::Create(customStripInstance.Value());
    assert(customStripRestoredProblem.HasValue());
    assert(customStripRestoredProblem.Value().Restore(customStripSolution)
               .HasValue());
    const auto customStripRestored = customStripRestoredProblem.Value().Save();
    assert(customStripRestored.layouts.size() == 1);
    assert(customStripRestored.layouts.front().strip.maxWidth == 20.0);

    const auto emptyInstance = jaguar::MSPInstance::Create(
        {}, {}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(emptyInstance.HasValue());
    assert(emptyInstance.Value().ItemCount() == 0);
    auto emptyProblem = jaguar::MSPProblem::Create(emptyInstance.Value());
    assert(emptyProblem.HasValue());
    assert(std::isnan(emptyProblem.Value().Density()));

    const auto zeroDemandInstance = jaguar::MSPInstance::Create(
        {stripWidthItem}, {0}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(zeroDemandInstance.HasValue());
    assert(zeroDemandInstance.Value().ItemIndexFromExternalId(3000).has_value());
    assert(zeroDemandInstance.Value().ItemIndexFromExternalId(3000).value() == 0);
    assert(zeroDemandInstance.Value().ItemCount() == 1);
    assert(zeroDemandInstance.Value().ItemDemand(0) == 0);
    auto zeroDemandProblem = jaguar::MSPProblem::Create(zeroDemandInstance.Value());
    assert(zeroDemandProblem.HasValue());
    assert(zeroDemandProblem.Value().RemainingDemand(0) == 0);
    assert(std::isnan(zeroDemandProblem.Value().Density()));

#ifdef JAGUARCORE_TEST_RELEASE
    const auto overDemandInstance = jaguar::MSPInstance::Create(
        {makeItem(0, 2.0, 2.0)}, {1}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(overDemandInstance.HasValue());
    auto overDemandProblem = jaguar::MSPProblem::Create(overDemandInstance.Value());
    assert(overDemandProblem.HasValue());
    const auto overDemandLayout = overDemandProblem.Value().AddLayout(
        {10.0, 5.0, 10.0, cdeConfig, shapeConfig});
    assert(overDemandLayout.HasValue());
    assert(overDemandProblem.Value().PlaceItem({
        overDemandLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})})
               .HasValue());
    const auto overDemandPlacement = overDemandProblem.Value().PlaceItem({
        overDemandLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 1.0})});
    assert(overDemandPlacement.HasValue());
    assert(overDemandProblem.Value().RemainingDemand(0) ==
           std::numeric_limits<std::size_t>::max());
    assert(overDemandProblem.Value().PlacedItemCount() == 2);
#endif
}
