#include "JaguarCore/problems/mspp.h"
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
    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::create(index, 3000 + index,
                                           std::move(original), config);
    assert(item.hasValue());
    return item.value();
}
}

void runMsppTests()
{
    const auto item0 = makeItem(0, 2.0, 2.0);
    const auto item1 = makeItem(1, 1.0, 2.0);
    jaguar::CDEConfig cdeConfig{};
    jaguar::ShapeModifyConfig shapeConfig{};
    const auto instance = jaguar::MSPInstance::create(
        {item0, item1}, {2, 1}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(instance.hasValue());
    assert(instance.value().baseStrip().width == 10.0);

    auto problem = jaguar::MSPProblem::create(instance.value());
    assert(problem.hasValue());
    const auto firstLayout = problem.value().addLayout({
        10.0, 5.0, 5.0, cdeConfig, shapeConfig});
    const auto secondLayout = problem.value().addLayout({
        10.0, 5.0, 6.0, cdeConfig, shapeConfig});
    assert(firstLayout.hasValue());
    assert(secondLayout.hasValue());
    assert(problem.value().layoutCount() == 2);
    assert(problem.value().totalStripWidth() == 11.0);

    const auto first = problem.value().placeItem({
        firstLayout.value(), 0,
        jaguar::Transform2d::fromRotationTranslation(0.0, {1.0, 1.0})});
    const auto second = problem.value().placeItem({
        secondLayout.value(), 0,
        jaguar::Transform2d::fromRotationTranslation(0.0, {1.0, 1.0})});
    const auto third = problem.value().placeItem({
        secondLayout.value(), 1,
        jaguar::Transform2d::fromRotationTranslation(0.0, {4.0, 1.0})});
    assert(first.hasValue() && second.hasValue() && third.hasValue());
    assert(problem.value().remainingDemand(0) == 0);
    assert(problem.value().remainingDemand(1) == 0);

    const auto tooWide = problem.value().changeStripWidth(firstLayout.value(), 11.0);
    assert(!tooWide.hasValue());
    assert(tooWide.error().code == jaguar::ErrorCode::InvalidInput);

    assert(problem.value().changeStripWidth(firstLayout.value(), 8.0).hasValue());
    assert(problem.value().stripWidth(firstLayout.value()) == 8.0);
    assert(problem.value().fitStrip(firstLayout.value()).hasValue());
    assert(problem.value().stripWidth(firstLayout.value()) < 8.0);

    const auto snapshot = problem.value().save();
    assert(jaguar::validateMSPSolution(problem.value(), snapshot).hasValue());

    auto mismatchedStripContainer = snapshot;
    assert(!mismatchedStripContainer.layouts.empty());
    mismatchedStripContainer.layouts.front().strip.width *= 0.5;
    assert(mismatchedStripContainer.layouts.front().strip.width > 0.0);
    assert(!problem.value().restore(mismatchedStripContainer).hasValue());
    assert(problem.value().layoutCount() == 2);
    assert(problem.value().placedItemCount() == 3);

    auto mismatchedStripSpec = snapshot;
    mismatchedStripSpec.layouts.front().strip.maxWidth = 20.0;
    assert(!problem.value().restore(mismatchedStripSpec).hasValue());
    assert(problem.value().layoutCount() == 2);
    assert(problem.value().placedItemCount() == 3);

    auto mismatchedStripConfig = snapshot;
    mismatchedStripConfig.layouts.front().strip.cdeConfig.cdThreshold = 1;
    assert(!problem.value().restore(mismatchedStripConfig).hasValue());
    assert(problem.value().layoutCount() == 2);
    assert(problem.value().placedItemCount() == 3);

    assert(problem.value().removeItem(second.value()).hasValue());
    assert(problem.value().remainingDemand(0) == 1);
    assert(problem.value().removeLayout(firstLayout.value()).hasValue());
    assert(problem.value().layoutCount() == 1);
    assert(problem.value().restore(snapshot).hasValue());
    assert(problem.value().layoutCount() == 2);
    assert(problem.value().placedItemCount() == 3);
    assert(problem.value().remainingDemand(0) == 0);
    assert(problem.value().remainingDemand(1) == 0);
    assert(problem.value().totalStripWidth() > 0.0);
    assert(problem.value().density() > 0.0);

    const auto invalidInstance = jaguar::MSPInstance::create(
        {}, {}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(!invalidInstance.hasValue());
    assert(invalidInstance.error().code == jaguar::ErrorCode::InvalidInput);
}
