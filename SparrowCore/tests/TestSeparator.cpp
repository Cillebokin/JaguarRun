#include "SparrowCore/Optimizer/OptimizationTerminator.h"
#include "SparrowCore/Optimizer/Separator.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Problems/Spp.h"

#include <chrono>
#include <ostream>
#include <utility>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }
    output << "SEPARATOR TEST FAILURE: " << message << '\n';
    return false;
}

jaguar::Item MakeItem(jaguar::ItemIndex index)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{3U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    const auto item = jaguar::Item::Create(
        index,
        static_cast<jaguar::ItemId>(2000U + index),
        {polygon.Value(),
         jaguar::Transform2d::Identity(),
         jaguar::ShapeModifyMode::Inflate,
         {}},
        surrogateConfig);
    return item.Value();
}
}

bool RunSeparatorTests(std::ostream& output)
{
    const auto instance = jaguar::SPInstance::Create(
        {MakeItem(0U), MakeItem(1U)},
        {1U, 1U},
        4.0,
        jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    if (!Expect(instance.HasValue(), output, "separator instance creation failed"))
    {
        return false;
    }
    auto problemResult = jaguar::SPProblem::Create(instance.Value());
    if (!Expect(problemResult.HasValue(), output, "separator problem creation failed"))
    {
        return false;
    }
    jaguar::SPProblem problem = std::move(problemResult.Value());
    if (!Expect(problem.ChangeStripWidth(8.0).HasValue(),
                output,
                "separator strip resize failed"))
    {
        return false;
    }
    if (!Expect(problem.PlaceItem({0U, {0.0, {0.0, 0.0}}}).HasValue()
                    && problem.PlaceItem({1U, {0.0, {0.0, 0.0}}}).HasValue(),
                output,
                "separator overlapping setup failed"))
    {
        return false;
    }
    const jaguar::SPSolution initial = problem.Save();
    sparrow::SeparatorConfig separatorConfig;
    separatorConfig.strikeLimit = 10U;
    separatorConfig.noImprovementLimit = 20U;
    separatorConfig.nContainerSamples = 256U;
    separatorConfig.nFocussedSamples = 64U;
    separatorConfig.nCoordinateDescents = 1U;

    auto separatorResult = sparrow::Separator::Create(std::move(problem), 42U);
    if (!Expect(separatorResult.HasValue(), output, "separator creation failed"))
    {
        return false;
    }
    sparrow::Separator separator = std::move(separatorResult.Value());
    const float initialLoss = separator.Tracker().TotalLoss();
    sparrow::OptimizationTerminator terminator({
        20U,
        std::chrono::milliseconds{2000}});
    const auto result = separator.Run(separatorConfig, terminator);
    if (!Expect(result.HasValue(), output, "separator run failed"))
    {
        return false;
    }
    return Expect(separator.Tracker().TotalLoss() <= initialLoss
                      && result.Value().solution.layoutSnapshot.placements.size()
                             == initial.layoutSnapshot.placements.size(),
                  output,
                  "separator must preserve placements and never worsen loss");
}
