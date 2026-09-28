#include "JaguarCore/Problems/Bpp.h"
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
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(index, 2000 + index,
                                           std::move(original), surrogateConfig);
    assert(item.HasValue());
    return item.Value();
}

jaguar::Container makeContainer(jaguar::ContainerId id,
                                double width,
                                double height = 6.0)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(polygon.HasValue());
    jaguar::OriginalShape original{
        polygon.Value(), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    jaguar::CDEConfig config{};
    const auto container = jaguar::Container::Create(id, std::move(original), {}, config);
    assert(container.HasValue());
    return container.Value();
}
}

void runBppTests()
{
    const auto item0 = makeItem(0, 2.0, 2.0);
    const auto item1 = makeItem(1, 1.0, 2.0);
    std::vector<jaguar::Bin> bins;
    bins.push_back({0, 5001, makeContainer(5001, 6.0), 1, 10});
    bins.push_back({1, 5002, makeContainer(5002, 10.0), 1, 25});

    const auto instance = jaguar::BPInstance::Create(
        {item0, item1}, {2, 1}, std::move(bins));
    assert(instance.HasValue());
    assert(instance.Value().TotalDemand() == 3);
    assert(instance.Value().TotalItemArea() == 10.0);
    assert(instance.Value().ItemIndexFromExternalId(2000).has_value());
    assert(instance.Value().ItemIndexFromExternalId(2000).value() == 0);
    assert(!instance.Value().ItemIndexFromExternalId(9999).has_value());

    const auto f32AreaInstance = jaguar::BPInstance::Create(
        {makeItem(0, 4096.0, 4096.0), makeItem(1, 1.0, 1.0),
         makeItem(2, 1.0, 1.0)},
        {1, 1, 1}, {});
    assert(f32AreaInstance.HasValue());
    // Rust rounds after each f32 addition: 2^24 + 1 + 1 remains 2^24.
    assert(f32AreaInstance.Value().TotalItemArea() == 16777216.0);

    auto problem = jaguar::BPProblem::Create(instance.Value());
    assert(problem.HasValue());
    assert(std::isnan(problem.Value().Density()));
    assert(std::isnan(problem.Value().Save().Density()));

    const auto first = problem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0})});
    assert(first.HasValue());
    assert(layoutKeySlot(first.Value().layoutIndex) == 1);
    assert(layoutKeyGeneration(first.Value().layoutIndex) == 1);
    assert(problem.Value().RemainingBinStock(0) == 0);
    assert(problem.Value().RemainingDemand(0) == 1);
    assert(problem.Value().BinCost() == 10);

    const auto second = problem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, first.Value().layoutIndex, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {2.0, 0.0})});
    assert(second.HasValue());
    assert(problem.Value().RemainingDemand(0) == 0);
    assert(problem.Value().PlacedItemCount() == 2);

    const auto unavailable = problem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 1,
        jaguar::Transform2d::Identity()});
    assert(!unavailable.HasValue());
    assert(unavailable.GetError().code == jaguar::ErrorCode::Infeasible);

    const auto third = problem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 1, 1,
        jaguar::Transform2d::Identity()});
    assert(third.HasValue());
    assert(problem.Value().RemainingBinStock(1) == 0);
    assert(problem.Value().BinCost() == 35);

    const auto snapshot = problem.Value().Save();
    assert(jaguar::ValidateBPSolution(problem.Value(), snapshot).HasValue());
    const auto removedLast = problem.Value().RemoveItem(third.Value());
    assert(removedLast.HasValue());
    assert(removedLast.Value().targetKind == jaguar::LayoutTargetKind::Closed);
    assert(removedLast.Value().binIndex == 1);
    assert(problem.Value().RemainingBinStock(1) == 1);
    assert(problem.Value().BinCost() == 10);

    const auto changedKeyRestore = problem.Value().Restore(snapshot);
    assert(changedKeyRestore.HasValue());
    assert(changedKeyRestore.Value());
    assert(problem.Value().RemainingBinStock(1) == 0);
    assert(problem.Value().PlacedItemCount() == 3);
    assert(problem.Value().BinCost() == 35);
    assert(problem.Value().Density() > 0.0);
    const auto stableSnapshot = problem.Value().Save();
    const auto stableKeyRestore = problem.Value().Restore(stableSnapshot);
    assert(stableKeyRestore.HasValue());
    assert(!stableKeyRestore.Value());

    auto staleHandleProblem = jaguar::BPProblem::Create(
        jaguar::BPInstance::Create(
            {item0}, {2},
            {{0, 9050, makeContainer(9050, 10.0, 5.0), 2, 1}}).Value());
    assert(staleHandleProblem.HasValue());
    const auto oldLayoutItem = staleHandleProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::Identity()});
    assert(oldLayoutItem.HasValue());
    const auto oldLayoutSnapshot = staleHandleProblem.Value().Save();
    assert(staleHandleProblem.Value().RemoveLayout(
        oldLayoutItem.Value().layoutIndex).HasValue());
    const auto replacementLayoutItem = staleHandleProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::Identity()});
    assert(replacementLayoutItem.HasValue());
    const auto staleSnapshotRestore = staleHandleProblem.Value().Restore(
        oldLayoutSnapshot);
    assert(staleSnapshotRestore.HasValue());
    assert(staleSnapshotRestore.Value());
    const auto staleLayoutPlacement = staleHandleProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, oldLayoutItem.Value().layoutIndex,
        0, 0, jaguar::Transform2d::FromRotationTranslation(0.0, {2.0, 0.0})});
    assert(!staleLayoutPlacement.HasValue());
    assert(staleLayoutPlacement.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto restoreOrderInstance = jaguar::BPInstance::Create(
        {makeItem(0, 1.0, 1.0)}, {2},
        {{0, 9060, makeContainer(9060, 2.0, 2.0), 1, 10},
         {1, 9061, makeContainer(9061, 3.0, 2.0), 1, 25}});
    assert(restoreOrderInstance.HasValue());
    auto restoreOrderProblem =
        jaguar::BPProblem::Create(restoreOrderInstance.Value());
    assert(restoreOrderProblem.HasValue());
    const auto restoreOrderFirst = restoreOrderProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::Identity()});
    const auto restoreOrderSecond = restoreOrderProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 1, 0,
        jaguar::Transform2d::Identity()});
    assert(restoreOrderFirst.HasValue() && restoreOrderSecond.HasValue());
    auto restoreOrderSnapshot = restoreOrderProblem.Value().Save();
    assert(restoreOrderSnapshot.layouts.size() == 2);
    assert(restoreOrderProblem.Value().RemoveLayout(
        restoreOrderFirst.Value().layoutIndex).HasValue());
    assert(restoreOrderProblem.Value().RemoveLayout(
        restoreOrderSecond.Value().layoutIndex).HasValue());
    std::reverse(restoreOrderSnapshot.layouts.begin(),
                 restoreOrderSnapshot.layouts.end());
    assert(restoreOrderProblem.Value().Restore(restoreOrderSnapshot).HasValue());
    const auto restoredOrderSnapshot = restoreOrderProblem.Value().Save();
    assert(restoredOrderSnapshot.layouts.size() == 2);
    // Rust's SecondaryMap restores in ascending key order: the second saved
    // layout occupies the first freed slot after the LIFO slot reuse.
    assert(restoredOrderSnapshot.layouts[0].binIndex == 1);
    assert(restoredOrderSnapshot.layouts[1].binIndex == 0);

    const auto largeItem = makeItem(0, 4096.0, 4096.0);
    const auto unitItem = makeItem(1, 1.0, 1.0);
    std::vector<jaguar::Bin> densityBins;
    densityBins.push_back(
        {0, 5100, makeContainer(5100, 8192.0, 4096.0), 1, 1});
    const auto densityInstance = jaguar::BPInstance::Create(
        {largeItem, unitItem}, {1, 2}, std::move(densityBins));
    assert(densityInstance.HasValue());
    auto densityProblem = jaguar::BPProblem::Create(densityInstance.Value());
    assert(densityProblem.HasValue());
    const auto densityLarge = densityProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0})});
    assert(densityLarge.HasValue());
    assert(densityProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, densityLarge.Value().layoutIndex, 0, 1,
        jaguar::Transform2d::FromRotationTranslation(0.0, {5000.0, 100.0})})
               .HasValue());
    assert(densityProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, densityLarge.Value().layoutIndex, 0, 1,
        jaguar::Transform2d::FromRotationTranslation(0.0, {6000.0, 100.0})})
               .HasValue());
    // Rust f32 aggregation rounds 2^24 + 1 + 1 to 2^24.
    assert(densityProblem.Value().Density() == 0.5);
    auto densitySolution = densityProblem.Value().Save();
    assert(densitySolution.Density() == 0.5);
    std::reverse(densitySolution.layouts[0].layoutSnapshot.placements.begin(),
                 densitySolution.layouts[0].layoutSnapshot.placements.end());
    assert(densitySolution.Density() == 0.5);

    const auto overDemand = problem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, first.Value().layoutIndex, 0, 0,
        jaguar::Transform2d::Identity()});
    assert(!overDemand.HasValue());
    assert(overDemand.GetError().code == jaguar::ErrorCode::Infeasible);

    assert(problem.Value().RemoveLayout(first.Value().layoutIndex).HasValue());
    assert(problem.Value().RemainingDemand(0) == 2);
    assert(problem.Value().RemainingBinStock(0) == 1);
    assert(problem.Value().PlacedItemCount() == 1);
    assert(problem.Value().BinCost() == 25);

    const auto churnItem = makeItem(0, 1.0, 1.0);
    std::vector<jaguar::Bin> churnBins{
        {0, 5400, makeContainer(5400, 1.0, 1.0), 4, 1},
        {1, 5401, makeContainer(5401, 16777216.0, 1.0), 1, 1}};
    const auto churnInstance = jaguar::BPInstance::Create(
        {churnItem}, {4}, std::move(churnBins));
    assert(churnInstance.HasValue());
    auto churnProblem = jaguar::BPProblem::Create(churnInstance.Value());
    assert(churnProblem.HasValue());
    const auto churnFirst = churnProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::Identity()});
    const auto churnSecond = churnProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::Identity()});
    const auto churnLarge = churnProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 1, 0,
        jaguar::Transform2d::Identity()});
    assert(churnFirst.HasValue() && churnSecond.HasValue() && churnLarge.HasValue());
    assert(churnProblem.Value().RemoveLayout(churnFirst.Value().layoutIndex).HasValue());
    assert(churnProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::Identity()}).HasValue());
    // Rust slot order sums the two unit areas before the 2^24 container area.
    const float rustOrderedDensity = 3.0F / (2.0F + 16777216.0F);
    assert(churnProblem.Value().Density() ==
           static_cast<double>(rustOrderedDensity));
    auto churnSolution = churnProblem.Value().Save();
    assert(churnSolution.Density() == static_cast<double>(rustOrderedDensity));
    std::reverse(churnSolution.layouts.begin(), churnSolution.layouts.end());
    assert(churnSolution.Density() == static_cast<double>(rustOrderedDensity));

    const auto emptyInstance = jaguar::BPInstance::Create({}, {}, {});
    assert(emptyInstance.HasValue());
    assert(emptyInstance.Value().ItemCount() == 0);
    assert(emptyInstance.Value().BinCount() == 0);
    assert(emptyInstance.Value().TotalDemand() == 0);
    assert(jaguar::BPProblem::Create(emptyInstance.Value()).HasValue());

    std::vector<jaguar::Bin> duplicateExternalIds;
    duplicateExternalIds.push_back({0, 5200, makeContainer(5200, 4.0), 1, 1});
    duplicateExternalIds.push_back({1, 5200, makeContainer(5200, 5.0), 1, 2});
    const auto duplicateExternalIdInstance = jaguar::BPInstance::Create(
        {}, {}, std::move(duplicateExternalIds));
    assert(duplicateExternalIdInstance.HasValue());
    assert(duplicateExternalIdInstance.Value().BinCount() == 2);

    const auto zeroDemandInstance = jaguar::BPInstance::Create(
        {item0}, {0}, {{0, 5300, makeContainer(5300, 4.0), 1, 1}});
    assert(zeroDemandInstance.HasValue());
    assert(zeroDemandInstance.Value().ItemIndexFromExternalId(2000).has_value());
    assert(zeroDemandInstance.Value().ItemIndexFromExternalId(2000).value() == 0);
    assert(zeroDemandInstance.Value().TotalDemand() == 0);
    const auto zeroDemandProblem = jaguar::BPProblem::Create(zeroDemandInstance.Value());
    assert(zeroDemandProblem.HasValue());
    assert(zeroDemandProblem.Value().RemainingDemand(0) == 0);
    assert(std::isnan(zeroDemandProblem.Value().Density()));
}
