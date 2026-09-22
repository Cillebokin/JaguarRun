#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Assertions.h"

#include <cassert>

namespace
{
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

jaguar::Container makeContainer(jaguar::ContainerId id, double width)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, 6.0}, {0.0, 6.0}});
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
    auto problem = jaguar::BPProblem::Create(instance.Value());
    assert(problem.HasValue());

    const auto first = problem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0})});
    assert(first.HasValue());
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

    assert(problem.Value().Restore(snapshot).HasValue());
    assert(problem.Value().RemainingBinStock(1) == 0);
    assert(problem.Value().PlacedItemCount() == 3);
    assert(problem.Value().BinCost() == 35);
    assert(problem.Value().Density() > 0.0);

    const auto overDemand = problem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, first.Value().layoutIndex, 0, 0,
        jaguar::Transform2d::Identity()});
    assert(!overDemand.HasValue());
    assert(overDemand.GetError().code == jaguar::ErrorCode::Infeasible);

    const auto emptyInstance = jaguar::BPInstance::Create({}, {}, {});
    assert(!emptyInstance.HasValue());
    assert(emptyInstance.GetError().code == jaguar::ErrorCode::InvalidInput);
}
