#include "JaguarCore/Problems/MSpp.h"
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
    const auto instance = jaguar::MSPInstance::Create(
        {item0, item1}, {2, 1}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(instance.HasValue());
    assert(instance.Value().BaseStrip().width == 10.0);

    auto problem = jaguar::MSPProblem::Create(instance.Value());
    assert(problem.HasValue());
    const auto firstLayout = problem.Value().AddLayout({
        10.0, 5.0, 5.0, cdeConfig, shapeConfig});
    const auto secondLayout = problem.Value().AddLayout({
        10.0, 5.0, 6.0, cdeConfig, shapeConfig});
    assert(firstLayout.HasValue());
    assert(secondLayout.HasValue());
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
    assert(!problem.Value().Restore(mismatchedStripSpec).HasValue());
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().PlacedItemCount() == 3);

    auto mismatchedStripConfig = snapshot;
    mismatchedStripConfig.layouts.front().strip.cdeConfig.cdThreshold = 1;
    assert(!problem.Value().Restore(mismatchedStripConfig).HasValue());
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().PlacedItemCount() == 3);

    assert(problem.Value().RemoveItem(second.Value()).HasValue());
    assert(problem.Value().RemainingDemand(0) == 1);
    assert(problem.Value().RemoveLayout(firstLayout.Value()).HasValue());
    assert(problem.Value().LayoutCount() == 1);
    assert(problem.Value().Restore(snapshot).HasValue());
    assert(problem.Value().LayoutCount() == 2);
    assert(problem.Value().PlacedItemCount() == 3);
    assert(problem.Value().RemainingDemand(0) == 0);
    assert(problem.Value().RemainingDemand(1) == 0);
    assert(problem.Value().TotalStripWidth() > 0.0);
    assert(problem.Value().Density() > 0.0);

    const auto invalidInstance = jaguar::MSPInstance::Create(
        {}, {}, 10.0, 5.0, cdeConfig, shapeConfig);
    assert(!invalidInstance.HasValue());
    assert(invalidInstance.GetError().code == jaguar::ErrorCode::InvalidInput);
}
