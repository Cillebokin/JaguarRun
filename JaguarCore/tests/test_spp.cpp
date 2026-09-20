#include "JaguarCore/problems/spp.h"
#include "JaguarCore/assertions.h"

#include <cassert>
#include <cmath>

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
    const auto item = jaguar::Item::create(index, 1000 + index,
                                           std::move(original), surrogateConfig);
    assert(item.hasValue());
    return item.value();
}
}

void runSppTests()
{
    const auto first = makeItem(0, 2.0, 2.0);
    const auto second = makeItem(1, 1.0, 2.0);
    jaguar::CDEConfig cdeConfig{};
    jaguar::ShapeModifyConfig shapeConfig{};

    const auto instance = jaguar::SPInstance::create(
        {first, second}, {2, 1}, 4.0, cdeConfig, shapeConfig);
    assert(instance.hasValue());
    assert(std::abs(instance.value().totalItemArea() - 10.0) < 1.0e-12);
    assert(std::abs(instance.value().baseStrip().width - 2.5) < 1.0e-12);

    auto problem = jaguar::SPProblem::create(instance.value());
    assert(problem.hasValue());
    assert(problem.value().remainingDemand(0) == 2);
    assert(problem.value().remainingDemand(1) == 1);

    const auto firstPlacement = problem.value().placeItem({
        0, jaguar::Transform2d::fromRotationTranslation(0.0, {0.5, 1.0})});
    assert(firstPlacement.hasValue());
    assert(problem.value().remainingDemand(0) == 1);

    const auto secondPlacement = problem.value().placeItem({
        0, jaguar::Transform2d::fromRotationTranslation(0.0, {3.0, 1.0})});
    assert(secondPlacement.hasValue());
    assert(problem.value().remainingDemand(0) == 0);
    const auto overDemand = problem.value().placeItem({
        0, jaguar::Transform2d::fromRotationTranslation(0.0, {6.0, 1.0})});
    assert(!overDemand.hasValue());
    assert(overDemand.error().code == jaguar::ErrorCode::Infeasible);

    const auto changedWidth = problem.value().changeStripWidth(10.0);
    assert(changedWidth.hasValue());
    assert(std::abs(problem.value().stripWidth() - 10.0) < 1.0e-12);
    assert(problem.value().remainingDemand(0) == 0);

    const auto fitted = problem.value().fitStrip();
    assert(fitted.hasValue());
    assert(problem.value().stripWidth() < 10.0);
    assert(problem.value().stripWidth() > 4.0);

    const auto removed = problem.value().removeItem(secondPlacement.value());
    assert(removed.hasValue());
    assert(removed.value().itemIndex == 0);
    assert(problem.value().remainingDemand(0) == 1);

    const auto solution = problem.value().save();
    assert(jaguar::validateSPSolution(problem.value(), solution).hasValue());
    auto invalidSolution = solution;
    invalidSolution.strip.width += 1.0;
    assert(!jaguar::validateSPSolution(problem.value(), invalidSolution).hasValue());
    auto invalidStripConfig = solution;
    invalidStripConfig.strip.cdeConfig.cdThreshold = 1;
    assert(!problem.value().restore(invalidStripConfig).hasValue());
    assert(!jaguar::validateSPSolution(problem.value(), invalidStripConfig).hasValue());
    const auto thirdPlacement = problem.value().placeItem({
        1, jaguar::Transform2d::fromRotationTranslation(0.0, {0.0, 0.0})});
    assert(thirdPlacement.hasValue());
    assert(problem.value().restore(solution).hasValue());
    assert(problem.value().remainingDemand(0) == 1);
    assert(problem.value().remainingDemand(1) == 1);
    assert(problem.value().placedItemCount() == 1);

    const auto invalidWidth = problem.value().changeStripWidth(0.0);
    assert(!invalidWidth.hasValue());
    assert(invalidWidth.error().code == jaguar::ErrorCode::InvalidInput);

    const auto emptyInstance = jaguar::SPInstance::create(
        {}, {}, 4.0, cdeConfig, shapeConfig);
    assert(!emptyInstance.hasValue());
    assert(emptyInstance.error().code == jaguar::ErrorCode::InvalidInput);
}
