#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

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

    jaguar::ShapeModifyConfig fitShapeConfig = shapeConfig;
    fitShapeConfig.offset = 0.1;
    const auto fitInstance = jaguar::SPInstance::Create(
        {makeItem(0, 2.0, 1.0)}, {1}, 4.0, cdeConfig, fitShapeConfig);
    assert(fitInstance.HasValue());
    auto fitProblem = jaguar::SPProblem::Create(fitInstance.Value());
    assert(fitProblem.HasValue());
    assert(fitProblem.Value().ChangeStripWidth(10.0).HasValue());
    assert(fitProblem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {1.25, 1.0})})
               .HasValue());
    assert(fitProblem.Value().FitStrip().HasValue());
    assert(std::abs(fitProblem.Value().StripWidth() -
                    static_cast<double>(3.350032329559326F)) < 1.0e-12);

    auto precisionProblem = jaguar::SPProblem::Create(fitInstance.Value());
    assert(precisionProblem.HasValue());
    const auto precisionPlacement = precisionProblem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {1.1, 1.0})});
    assert(precisionPlacement.HasValue());
    const auto precisionRemoved =
        precisionProblem.Value().RemoveItem(precisionPlacement.Value());
    assert(precisionRemoved.HasValue());
    assert(precisionRemoved.Value().transform.translation.x ==
           static_cast<double>(static_cast<float>(1.1)));

    const auto instance = jaguar::SPInstance::Create(
        {first, second}, {2, 1}, 4.0, cdeConfig, shapeConfig);
    assert(instance.HasValue());
    assert(instance.Value().ItemIndexFromExternalId(1000).has_value());
    assert(instance.Value().ItemIndexFromExternalId(1000).value() == 0);
    assert(!instance.Value().ItemIndexFromExternalId(9999).has_value());
    assert(std::abs(instance.Value().TotalItemArea() - 10.0) < 1.0e-12);
    assert(std::abs(instance.Value().BaseStrip().width - 2.5) < 1.0e-12);

    const auto restoreStrip = jaguar::Strip::Create(
        20.0, cdeConfig, shapeConfig, 10.0);
    assert(restoreStrip.HasValue());
    const auto restoreInstance = jaguar::SPInstance::Create(
        {makeItem(0, 1.0, 1.0), makeItem(1, 1.0, 1.0),
         makeItem(2, 1.0, 1.0)},
        {1, 1, 1}, restoreStrip.Value());
    assert(restoreInstance.HasValue());
    auto restoreProblem = jaguar::SPProblem::Create(restoreInstance.Value());
    assert(restoreProblem.HasValue());
    const auto restoreItemA = restoreProblem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    const auto restoreItemB = restoreProblem.Value().PlaceItem({
        1, jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 1.0})});
    assert(restoreItemA.HasValue());
    assert(restoreItemB.HasValue());
    const auto restoreSnapshot = restoreProblem.Value().Save();
    assert(restoreProblem.Value().RemoveItem(restoreItemA.Value()).HasValue());
    assert(restoreProblem.Value().RemoveItem(restoreItemB.Value()).HasValue());
    assert(restoreProblem.Value().PlaceItem({
        2, jaguar::Transform2d::FromRotationTranslation(0.0, {9.0, 1.0})})
               .HasValue());
    assert(restoreProblem.Value().Restore(restoreSnapshot).HasValue());
    const auto restoredSnapshot = restoreProblem.Value().Save();
    assert(restoredSnapshot.layoutSnapshot.cdeSnapshot.dynamicHazards.size() == 2);
    assert(restoredSnapshot.layoutSnapshot.cdeSnapshot.dynamicHazards[0]
               .entity.itemIndex == 1);
    assert(restoredSnapshot.layoutSnapshot.cdeSnapshot.dynamicHazards[1]
               .entity.itemIndex == 0);

    const auto largeAreaItem = makeItem(0, 4096.0, 4096.0);
    const auto smallAreaItem1 = makeItem(1, 1.0, 1.0);
    const auto smallAreaItem2 = makeItem(2, 1.0, 1.0);
    const auto f32AreaInstance = jaguar::SPInstance::Create(
        {largeAreaItem, smallAreaItem1, smallAreaItem2}, {1, 1, 1}, 2.0,
        cdeConfig, shapeConfig);
    assert(f32AreaInstance.HasValue());
    // Rust accumulates item areas in f32 before deriving the base strip width.
    assert(f32AreaInstance.Value().TotalItemArea() == 16777216.0);
    assert(f32AreaInstance.Value().BaseStrip().width == 8388608.0);

    const auto f32DensityStrip = jaguar::Strip::Create(
        4096.0, cdeConfig, shapeConfig, 8192.0);
    assert(f32DensityStrip.HasValue());
    const auto f32DensityInstance = jaguar::SPInstance::Create(
        {largeAreaItem, smallAreaItem1, smallAreaItem2}, {1, 1, 1},
        f32DensityStrip.Value());
    assert(f32DensityInstance.HasValue());
    auto f32DensityProblem = jaguar::SPProblem::Create(f32DensityInstance.Value());
    assert(f32DensityProblem.HasValue());
    assert(f32DensityProblem.Value().PlaceItem({
        0, jaguar::Transform2d::Identity()}).HasValue());
    assert(f32DensityProblem.Value().PlaceItem({
        1, jaguar::Transform2d::FromRotationTranslation(0.0, {5000.0, 100.0})})
               .HasValue());
    assert(f32DensityProblem.Value().PlaceItem({
        2, jaguar::Transform2d::FromRotationTranslation(0.0, {6000.0, 100.0})})
               .HasValue());
    auto f32DensitySolution = f32DensityProblem.Value().Save();
    assert(f32DensitySolution.Density() == 0.5);
    std::reverse(f32DensitySolution.layoutSnapshot.placements.begin(),
                 f32DensitySolution.layoutSnapshot.placements.end());
    assert(f32DensitySolution.Density() == 0.5);

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
#ifdef JAGUARCORE_TEST_RELEASE
    assert(overDemand.HasValue());
    assert(problem.Value().RemainingDemand(0) ==
           std::numeric_limits<std::size_t>::max());
    assert(problem.Value().RemoveItem(overDemand.Value()).HasValue());
    assert(problem.Value().RemainingDemand(0) == 0);
#else
    assert(!overDemand.HasValue());
    assert(overDemand.GetError().code == jaguar::ErrorCode::Infeasible);
#endif

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
    // jagua-rs restore() accepts a snapshot with a different strip
    // configuration and adopts it as the new problem state.
    assert(problem.Value().Restore(invalidStripConfig).HasValue());
    assert(problem.Value().Save().strip.cdeConfig.cdThreshold == 1);
    assert(problem.Value().StripWidth() ==
           invalidStripConfig.strip.width);
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

    const auto explicitStrip = jaguar::Strip::Create(
        4.0, cdeConfig, shapeConfig, 1.0);
    assert(explicitStrip.HasValue());
    const auto emptyExplicitInstance = jaguar::SPInstance::Create(
        {}, {}, explicitStrip.Value());
    assert(emptyExplicitInstance.HasValue());
    assert(emptyExplicitInstance.Value().ItemCount() == 0);
    assert(emptyExplicitInstance.Value().TotalDemand() == 0);
    auto emptyExplicitProblem =
        jaguar::SPProblem::Create(emptyExplicitInstance.Value());
    assert(emptyExplicitProblem.HasValue());
    assert(emptyExplicitProblem.Value().Density() == 0.0);

    const auto zeroDemandInstance = jaguar::SPInstance::Create(
        {first}, {0}, explicitStrip.Value());
    assert(zeroDemandInstance.HasValue());
    assert(zeroDemandInstance.Value().ItemIndexFromExternalId(1000).has_value());
    assert(zeroDemandInstance.Value().ItemIndexFromExternalId(1000).value() == 0);
    assert(zeroDemandInstance.Value().TotalDemand() == 0);
    assert(zeroDemandInstance.Value().TotalItemArea() == 0.0);
    auto zeroDemandProblem = jaguar::SPProblem::Create(zeroDemandInstance.Value());
    assert(zeroDemandProblem.HasValue());
    assert(zeroDemandProblem.Value().RemainingDemand(0) == 0);
    assert(zeroDemandProblem.Value().Density() == 0.0);
}
