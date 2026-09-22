#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"

#include <cassert>
#include <cmath>

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
    const auto item = jaguar::Item::Create(index, 1000 + index,
                                           std::move(original), surrogateConfig);
    assert(item.HasValue());
    return item.Value();
}
}

void runSppTests()
{
    const auto first = makeItem(0, 2.0, 2.0);
    const auto second = makeItem(1, 1.0, 2.0);
    jaguar::CDEConfig cdeConfig{};
    jaguar::ShapeModifyConfig shapeConfig{};

    const auto instance = jaguar::SPInstance::Create(
        {first, second}, {2, 1}, 4.0, cdeConfig, shapeConfig);
    assert(instance.HasValue());
    assert(std::abs(instance.Value().TotalItemArea() - 10.0) < 1.0e-12);
    assert(std::abs(instance.Value().BaseStrip().width - 2.5) < 1.0e-12);

    auto problem = jaguar::SPProblem::Create(instance.Value());
    assert(problem.HasValue());
    assert(problem.Value().RemainingDemand(0) == 2);
    assert(problem.Value().RemainingDemand(1) == 1);

    const auto firstPlacement = problem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {0.5, 1.0})});
    assert(firstPlacement.HasValue());
    assert(problem.Value().RemainingDemand(0) == 1);

    const auto secondPlacement = problem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {3.0, 1.0})});
    assert(secondPlacement.HasValue());
    assert(problem.Value().RemainingDemand(0) == 0);
    const auto overDemand = problem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {6.0, 1.0})});
    assert(!overDemand.HasValue());
    assert(overDemand.GetError().code == jaguar::ErrorCode::Infeasible);

    const auto changedWidth = problem.Value().ChangeStripWidth(10.0);
    assert(changedWidth.HasValue());
    assert(std::abs(problem.Value().StripWidth() - 10.0) < 1.0e-12);
    assert(problem.Value().RemainingDemand(0) == 0);

    const auto fitted = problem.Value().FitStrip();
    assert(fitted.HasValue());
    assert(problem.Value().StripWidth() < 10.0);
    assert(problem.Value().StripWidth() > 4.0);

    const auto removed = problem.Value().RemoveItem(secondPlacement.Value());
    assert(removed.HasValue());
    assert(removed.Value().itemIndex == 0);
    assert(problem.Value().RemainingDemand(0) == 1);

    const auto solution = problem.Value().Save();
    assert(jaguar::ValidateSPSolution(problem.Value(), solution).HasValue());
    auto invalidSolution = solution;
    invalidSolution.strip.width += 1.0;
    assert(!jaguar::ValidateSPSolution(problem.Value(), invalidSolution).HasValue());
    auto invalidStripConfig = solution;
    invalidStripConfig.strip.cdeConfig.cdThreshold = 1;
    assert(!problem.Value().Restore(invalidStripConfig).HasValue());
    assert(!jaguar::ValidateSPSolution(problem.Value(), invalidStripConfig).HasValue());
    const auto thirdPlacement = problem.Value().PlaceItem({
        1, jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0})});
    assert(thirdPlacement.HasValue());
    assert(problem.Value().Restore(solution).HasValue());
    assert(problem.Value().RemainingDemand(0) == 1);
    assert(problem.Value().RemainingDemand(1) == 1);
    assert(problem.Value().PlacedItemCount() == 1);

    const auto invalidWidth = problem.Value().ChangeStripWidth(0.0);
    assert(!invalidWidth.HasValue());
    assert(invalidWidth.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto emptyInstance = jaguar::SPInstance::Create(
        {}, {}, 4.0, cdeConfig, shapeConfig);
    assert(!emptyInstance.HasValue());
    assert(emptyInstance.GetError().code == jaguar::ErrorCode::InvalidInput);
}
