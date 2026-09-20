#include "JaguarCore/problems/bpp.h"
#include "JaguarCore/assertions.h"

#include <cassert>

namespace
{
jaguar::Item makeItem(jaguar::ItemIndex index, double width, double height)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(polygon.hasValue());
    jaguar::OriginalShape original{
        polygon.value(), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::create(index, 2000 + index,
                                           std::move(original), surrogateConfig);
    assert(item.hasValue());
    return item.value();
}

jaguar::Container makeContainer(jaguar::ContainerId id, double width)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, 6.0}, {0.0, 6.0}});
    assert(polygon.hasValue());
    jaguar::OriginalShape original{
        polygon.value(), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    jaguar::CDEConfig config{};
    const auto container = jaguar::Container::create(id, std::move(original), {}, config);
    assert(container.hasValue());
    return container.value();
}
}

void runBppTests()
{
    const auto item0 = makeItem(0, 2.0, 2.0);
    const auto item1 = makeItem(1, 1.0, 2.0);
    std::vector<jaguar::Bin> bins;
    bins.push_back({0, 5001, makeContainer(5001, 6.0), 1, 10});
    bins.push_back({1, 5002, makeContainer(5002, 10.0), 1, 25});

    const auto instance = jaguar::BPInstance::create(
        {item0, item1}, {2, 1}, std::move(bins));
    assert(instance.hasValue());
    auto problem = jaguar::BPProblem::create(instance.value());
    assert(problem.hasValue());

    const auto first = problem.value().placeItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::fromRotationTranslation(0.0, {0.0, 0.0})});
    assert(first.hasValue());
    assert(problem.value().remainingBinStock(0) == 0);
    assert(problem.value().remainingDemand(0) == 1);
    assert(problem.value().binCost() == 10);

    const auto second = problem.value().placeItem({
        jaguar::LayoutTargetKind::Open, first.value().layoutIndex, 0, 0,
        jaguar::Transform2d::fromRotationTranslation(0.0, {2.0, 0.0})});
    assert(second.hasValue());
    assert(problem.value().remainingDemand(0) == 0);
    assert(problem.value().placedItemCount() == 2);

    const auto unavailable = problem.value().placeItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 1,
        jaguar::Transform2d::identity()});
    assert(!unavailable.hasValue());
    assert(unavailable.error().code == jaguar::ErrorCode::Infeasible);

    const auto third = problem.value().placeItem({
        jaguar::LayoutTargetKind::Closed, 0, 1, 1,
        jaguar::Transform2d::identity()});
    assert(third.hasValue());
    assert(problem.value().remainingBinStock(1) == 0);
    assert(problem.value().binCost() == 35);

    const auto snapshot = problem.value().save();
    assert(jaguar::validateBPSolution(problem.value(), snapshot).hasValue());
    const auto removedLast = problem.value().removeItem(third.value());
    assert(removedLast.hasValue());
    assert(removedLast.value().targetKind == jaguar::LayoutTargetKind::Closed);
    assert(removedLast.value().binIndex == 1);
    assert(problem.value().remainingBinStock(1) == 1);
    assert(problem.value().binCost() == 10);

    assert(problem.value().restore(snapshot).hasValue());
    assert(problem.value().remainingBinStock(1) == 0);
    assert(problem.value().placedItemCount() == 3);
    assert(problem.value().binCost() == 35);
    assert(problem.value().density() > 0.0);

    const auto overDemand = problem.value().placeItem({
        jaguar::LayoutTargetKind::Open, first.value().layoutIndex, 0, 0,
        jaguar::Transform2d::identity()});
    assert(!overDemand.hasValue());
    assert(overDemand.error().code == jaguar::ErrorCode::Infeasible);

    const auto emptyInstance = jaguar::BPInstance::create({}, {}, {});
    assert(!emptyInstance.hasValue());
    assert(emptyInstance.error().code == jaguar::ErrorCode::InvalidInput);
}
