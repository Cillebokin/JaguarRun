#include "JaguarCore/Assertions.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace
{
constexpr double PI = 3.141592653589793238462643383279502884;

jaguar::Item makeItem(jaguar::ItemIndex index,
                      jaguar::ItemId externalId,
                      double width,
                      double height)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(polygon.HasValue());
    jaguar::OriginalShape original{
        polygon.Value(), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::Create(
        index, externalId, std::move(original), surrogateConfig);
    assert(item.HasValue());
    return item.Value();
}

jaguar::Container makeContainer(jaguar::ContainerId id,
                                double width,
                                double height)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(polygon.HasValue());
    jaguar::OriginalShape original{
        polygon.Value(), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    const auto container = jaguar::Container::Create(
        id, std::move(original), {}, jaguar::CDEConfig{});
    assert(container.HasValue());
    return container.Value();
}

jaguar::Transform2d randomTransform(std::mt19937_64& random,
                                    std::uniform_real_distribution<double>& position)
{
    const double rotation = static_cast<double>(random() % 4) * PI * 0.5;
    return jaguar::Transform2d::FromRotationTranslation(
        rotation, {position(random), position(random)});
}

void assertSppRebuildEquivalent(const jaguar::SPProblem& problem,
                                const jaguar::SPInstance& instance)
{
    const jaguar::SPSolution state = problem.Save();
    assert(jaguar::ValidateSPSolution(problem, state).HasValue());

    auto rebuilt = jaguar::SPProblem::Create(instance);
    assert(rebuilt.HasValue());
    assert(rebuilt.Value().Restore(state).HasValue());
    assert(jaguar::ValidateSPSolution(rebuilt.Value(), state).HasValue());
    assert(rebuilt.Value().PlacedItemCount() == problem.PlacedItemCount());
    assert(std::abs(rebuilt.Value().StripWidth() - problem.StripWidth()) < 1.0e-9);
    assert(std::abs(rebuilt.Value().Density() - problem.Density()) < 1.0e-12);
    for (std::size_t i = 0; i < instance.ItemCount(); ++i)
        assert(rebuilt.Value().RemainingDemand(i) == problem.RemainingDemand(i));
}

void assertBppRebuildEquivalent(const jaguar::BPProblem& problem,
                                const jaguar::BPInstance& instance)
{
    const jaguar::BPSolution state = problem.Save();
    assert(jaguar::ValidateBPSolution(problem, state).HasValue());

    auto rebuilt = jaguar::BPProblem::Create(instance);
    assert(rebuilt.HasValue());
    assert(rebuilt.Value().Restore(state).HasValue());
    assert(jaguar::ValidateBPSolution(rebuilt.Value(), state).HasValue());
    assert(rebuilt.Value().PlacedItemCount() == problem.PlacedItemCount());
    assert(rebuilt.Value().OpenLayoutCount() == problem.OpenLayoutCount());
    assert(rebuilt.Value().BinCost() == problem.BinCost());
    assert(std::abs(rebuilt.Value().Density() - problem.Density()) < 1.0e-12);
    for (std::size_t i = 0; i < instance.ItemCount(); ++i)
        assert(rebuilt.Value().RemainingDemand(i) == problem.RemainingDemand(i));
    for (std::size_t i = 0; i < instance.BinCount(); ++i)
        assert(rebuilt.Value().RemainingBinStock(i) == problem.RemainingBinStock(i));
}

void assertMsppRebuildEquivalent(const jaguar::MSPProblem& problem,
                                 const jaguar::MSPInstance& instance)
{
    const jaguar::MSPSolution state = problem.Save();
    assert(jaguar::ValidateMSPSolution(problem, state).HasValue());

    auto rebuilt = jaguar::MSPProblem::Create(instance);
    assert(rebuilt.HasValue());
    assert(rebuilt.Value().Restore(state).HasValue());
    assert(jaguar::ValidateMSPSolution(rebuilt.Value(), state).HasValue());
    assert(rebuilt.Value().PlacedItemCount() == problem.PlacedItemCount());
    assert(rebuilt.Value().LayoutCount() == problem.LayoutCount());
    assert(std::abs(rebuilt.Value().TotalStripWidth() -
                    problem.TotalStripWidth()) < 1.0e-9);
    assert(std::abs(rebuilt.Value().Density() - problem.Density()) < 1.0e-12);
    for (std::size_t i = 0; i < instance.ItemCount(); ++i)
        assert(rebuilt.Value().RemainingDemand(i) == problem.RemainingDemand(i));
}

void assertSppJsonCheckpoint(const jaguar::SPProblem& problem)
{
    const jaguar::SPInstance& instance = problem.Instance();
    const jaguar::SPSolution solution = problem.Save();
    const auto external = jaguar::ExternalSPSolutionFromSolution(instance, solution);
    assert(external.HasValue());
    const auto json = jaguar::SerializeExternalSPSolutionJson(external.Value());
    assert(json.HasValue());
    const auto parsed = jaguar::ParseExternalSPSolutionJson(json.Value());
    assert(parsed.HasValue());
    const auto imported = jaguar::Importer{}.ImportSPSolution(
        instance, parsed.Value());
    const auto importedFromJson = jaguar::Importer{}.ImportSPSolutionJson(
        instance, json.Value());
    assert(imported.HasValue());
    assert(importedFromJson.HasValue());
    assert(imported.Value().layoutSnapshot.placements.size() ==
           solution.layoutSnapshot.placements.size());
    assert(importedFromJson.Value().layoutSnapshot.placements.size() ==
           solution.layoutSnapshot.placements.size());
    assert(std::abs(imported.Value().StripWidth() - solution.StripWidth()) <
           1.0e-9);
    assert(std::abs(importedFromJson.Value().StripWidth() - solution.StripWidth()) <
           1.0e-9);

    auto rebuilt = jaguar::SPProblem::Create(instance);
    assert(rebuilt.HasValue());
    assert(rebuilt.Value().Restore(importedFromJson.Value()).HasValue());
    assert(jaguar::ValidateSPSolution(rebuilt.Value(), importedFromJson.Value()).HasValue());
    assert(rebuilt.Value().PlacedItemCount() == problem.PlacedItemCount());
    assert(std::abs(rebuilt.Value().Density() - problem.Density()) < 1.0e-12);
}

void assertBppJsonCheckpoint(const jaguar::BPProblem& problem)
{
    const jaguar::BPInstance& instance = problem.Instance();
    const jaguar::BPSolution solution = problem.Save();
    const auto external = jaguar::ExternalBPSolutionFromSolution(instance, solution);
    assert(external.HasValue());
    const auto json = jaguar::SerializeExternalBPSolutionJson(external.Value());
    assert(json.HasValue());
    const auto parsed = jaguar::ParseExternalBPSolutionJson(json.Value());
    assert(parsed.HasValue());
    const auto imported = jaguar::Importer{}.ImportBPSolution(
        instance, parsed.Value());
    const auto importedFromJson = jaguar::Importer{}.ImportBPSolutionJson(
        instance, json.Value());
    assert(imported.HasValue());
    assert(importedFromJson.HasValue());
    assert(imported.Value().layouts.size() == solution.layouts.size());
    assert(importedFromJson.Value().layouts.size() == solution.layouts.size());
    assert(imported.Value().Cost(instance) == solution.Cost(instance));
    assert(importedFromJson.Value().Cost(instance) == solution.Cost(instance));

    auto rebuilt = jaguar::BPProblem::Create(instance);
    assert(rebuilt.HasValue());
    assert(rebuilt.Value().Restore(importedFromJson.Value()).HasValue());
    assert(jaguar::ValidateBPSolution(rebuilt.Value(), importedFromJson.Value()).HasValue());
    assert(rebuilt.Value().PlacedItemCount() == problem.PlacedItemCount());
    assert(rebuilt.Value().BinCost() == problem.BinCost());
    assert(std::abs(rebuilt.Value().Density() - problem.Density()) < 1.0e-12);
}

void assertMsppJsonCheckpoint(const jaguar::MSPProblem& problem)
{
    const jaguar::MSPInstance& instance = problem.Instance();
    const jaguar::MSPSolution solution = problem.Save();
    const auto external = jaguar::ExternalMSPSolutionFromSolution(instance, solution);
    assert(external.HasValue());
    const auto json = jaguar::SerializeExternalMSPSolutionJson(external.Value());
    assert(json.HasValue());
    const auto parsed = jaguar::ParseExternalMSPSolutionJson(json.Value());
    assert(parsed.HasValue());
    const auto imported = jaguar::Importer{}.ImportMSPSolution(
        instance, parsed.Value());
    const auto importedFromJson = jaguar::Importer{}.ImportMSPSolutionJson(
        instance, json.Value());
    assert(imported.HasValue());
    assert(importedFromJson.HasValue());
    assert(imported.Value().layouts.size() == solution.layouts.size());
    assert(importedFromJson.Value().layouts.size() == solution.layouts.size());
    assert(std::abs(imported.Value().TotalStripWidth() -
                    solution.TotalStripWidth()) < 1.0e-9);
    assert(std::abs(importedFromJson.Value().TotalStripWidth() -
                    solution.TotalStripWidth()) < 1.0e-9);

    auto rebuilt = jaguar::MSPProblem::Create(instance);
    assert(rebuilt.HasValue());
    assert(rebuilt.Value().Restore(importedFromJson.Value()).HasValue());
    assert(jaguar::ValidateMSPSolution(rebuilt.Value(), importedFromJson.Value()).HasValue());
    assert(rebuilt.Value().PlacedItemCount() == problem.PlacedItemCount());
    assert(std::abs(rebuilt.Value().Density() - problem.Density()) < 1.0e-12);
}

void runRandomSpp()
{
    const std::vector<jaguar::Item> items{
        makeItem(0, 4100, 2.0, 2.0), makeItem(1, 4101, 1.0, 2.0)};
    const auto instanceResult = jaguar::SPInstance::Create(
        items, {12, 12}, 10.0, jaguar::CDEConfig{}, jaguar::ShapeModifyConfig{});
    assert(instanceResult.HasValue());
    const jaguar::SPInstance instance = instanceResult.Value();
    auto problem = jaguar::SPProblem::Create(instance);
    assert(problem.HasValue());

    std::mt19937_64 random(0x5350505F52414E44ULL);
    std::uniform_real_distribution<double> position(0.0, 8.0);
    for (std::size_t step = 0; step < 1000; ++step)
    {
        const auto current = problem.Value().Save();
        const std::size_t operation = random() % 5;
        if (operation == 0 || current.layoutSnapshot.placements.empty())
        {
            std::vector<std::size_t> available;
            for (std::size_t i = 0; i < instance.ItemCount(); ++i)
            {
                if (problem.Value().RemainingDemand(i) > 0)
                    available.push_back(i);
            }
            if (!available.empty())
            {
                const std::size_t itemIndex =
                    available[random() % available.size()];
                assert(problem.Value().PlaceItem({
                           itemIndex, randomTransform(random, position)})
                           .HasValue());
            }
        }
        else if (operation == 1)
        {
            const auto& placements = current.layoutSnapshot.placements;
            assert(problem.Value().RemoveItem(
                       placements[random() % placements.size()].handle)
                       .HasValue());
            (void)placements;
        }
        else if (operation == 2)
        {
            assert(problem.Value().Restore(current).HasValue());
        }
        else if (operation == 3)
        {
            const double width = 6.0 + static_cast<double>(random() % 120) / 10.0;
            assert(problem.Value().ChangeStripWidth(width).HasValue());
        }
        else if (!current.layoutSnapshot.placements.empty())
        {
            const auto fitted = problem.Value().FitStrip();
            assert(fitted.HasValue());
        }

        assertSppRebuildEquivalent(problem.Value(), instance);
        if (step % 250 == 0 && problem.Value().PlacedItemCount() > 0)
            assertSppJsonCheckpoint(problem.Value());
    }
}

void runRandomBpp()
{
    const std::vector<jaguar::Item> items{
        makeItem(0, 4200, 2.0, 2.0), makeItem(1, 4201, 1.0, 2.0)};
    std::vector<jaguar::Bin> bins{
        {0, 5200, makeContainer(5200, 12.0, 10.0), 5, 10},
        {1, 5201, makeContainer(5201, 14.0, 10.0), 5, 25}};
    const auto instanceResult = jaguar::BPInstance::Create(
        items, {16, 16}, std::move(bins));
    assert(instanceResult.HasValue());
    const jaguar::BPInstance instance = instanceResult.Value();
    auto problem = jaguar::BPProblem::Create(instance);
    assert(problem.HasValue());

    std::mt19937_64 random(0x4250505F52414E44ULL);
    std::uniform_real_distribution<double> position(0.0, 9.0);
    for (std::size_t step = 0; step < 1000; ++step)
    {
        const auto current = problem.Value().Save();
        const std::size_t operation = random() % 5;
        std::vector<std::size_t> availableItems;
        for (std::size_t i = 0; i < instance.ItemCount(); ++i)
        {
            if (problem.Value().RemainingDemand(i) > 0)
                availableItems.push_back(i);
        }
        if ((operation == 0 || current.layouts.empty()) &&
            !availableItems.empty())
        {
            const std::size_t itemIndex =
                availableItems[random() % availableItems.size()];
            const bool preferClosed = current.layouts.empty() || (random() % 2 == 0);
            if (preferClosed)
            {
                std::vector<std::size_t> availableBins;
                for (std::size_t i = 0; i < instance.BinCount(); ++i)
                {
                    if (problem.Value().RemainingBinStock(i) > 0)
                        availableBins.push_back(i);
                }
                if (!availableBins.empty())
                {
                    const std::size_t binIndex =
                        availableBins[random() % availableBins.size()];
                    assert(problem.Value().PlaceItem({
                               jaguar::LayoutTargetKind::Closed, 0, binIndex,
                               itemIndex, randomTransform(random, position)})
                               .HasValue());
                }
            }
            else
            {
                const auto& selectedLayout =
                    current.layouts[random() % current.layouts.size()];
                assert(problem.Value().PlaceItem({
                           jaguar::LayoutTargetKind::Open,
                           selectedLayout.layoutIndex, selectedLayout.binIndex,
                           itemIndex,
                           randomTransform(random, position)})
                           .HasValue());
                (void)selectedLayout;
            }
        }
        else if (operation == 1 && !current.layouts.empty())
        {
            std::vector<jaguar::BPPlacedHandle> candidates;
            for (const auto& layout : current.layouts)
            {
                for (const auto& placement : layout.layoutSnapshot.placements)
                    candidates.push_back({layout.layoutIndex, placement.handle});
            }
            if (!candidates.empty())
            {
                assert(problem.Value().RemoveItem(
                           candidates[random() % candidates.size()])
                           .HasValue());
            }
        }
        else if (operation == 2)
        {
            assert(problem.Value().Restore(current).HasValue());
        }

        assertBppRebuildEquivalent(problem.Value(), instance);
        if (step % 250 == 0 && problem.Value().PlacedItemCount() > 0)
            assertBppJsonCheckpoint(problem.Value());
    }
}

void runRandomMspp()
{
    const std::vector<jaguar::Item> items{
        makeItem(0, 4300, 2.0, 2.0), makeItem(1, 4301, 1.0, 2.0)};
    const auto instanceResult = jaguar::MSPInstance::Create(
        items, {16, 16}, 14.0, 10.0, jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    assert(instanceResult.HasValue());
    const jaguar::MSPInstance instance = instanceResult.Value();
    auto problem = jaguar::MSPProblem::Create(instance);
    assert(problem.HasValue());

    std::mt19937_64 random(0x4D53505F52414E44ULL);
    std::uniform_real_distribution<double> position(0.0, 2.0);
    for (std::size_t step = 0; step < 1000; ++step)
    {
        const auto current = problem.Value().Save();
        const std::size_t operation = random() % 7;
        std::vector<std::size_t> availableItems;
        for (std::size_t i = 0; i < instance.ItemCount(); ++i)
        {
            if (problem.Value().RemainingDemand(i) > 0)
                availableItems.push_back(i);
        }

        if ((operation == 0 || current.layouts.empty()) &&
            !availableItems.empty())
        {
            std::size_t layoutIndex = 0;
            if (current.layouts.empty() || random() % 2 == 0)
            {
                const double width =
                    4.0 + static_cast<double>(random() % 100) / 10.0;
                const auto added = problem.Value().AddLayout({
                    14.0, 10.0, width, jaguar::CDEConfig{},
                    jaguar::ShapeModifyConfig{}});
                assert(added.HasValue());
                layoutIndex = added.Value();
            }
            else
            {
                layoutIndex =
                    current.layouts[random() % current.layouts.size()].layoutIndex;
            }
            const std::size_t itemIndex =
                availableItems[random() % availableItems.size()];
            assert(problem.Value().PlaceItem({
                       layoutIndex, itemIndex, randomTransform(random, position)})
                       .HasValue());
        }
        else if (operation == 1 && !current.layouts.empty())
        {
            std::vector<jaguar::MSPPlacedHandle> candidates;
            for (const auto& layout : current.layouts)
            {
                for (const auto& placement : layout.layoutSnapshot.placements)
                    candidates.push_back({layout.layoutIndex, placement.handle});
            }
            if (!candidates.empty())
            {
                assert(problem.Value().RemoveItem(
                           candidates[random() % candidates.size()])
                           .HasValue());
            }
        }
        else if (operation == 2)
        {
            assert(problem.Value().Restore(current).HasValue());
        }
        else if (operation == 3 && !current.layouts.empty())
        {
            const std::size_t layoutIndex =
                current.layouts[random() % current.layouts.size()].layoutIndex;
            const double width =
                4.0 + static_cast<double>(random() % 100) / 10.0;
            assert(problem.Value().ChangeStripWidth(layoutIndex, width).HasValue());
        }
        else if (operation == 4 && !current.layouts.empty())
        {
            std::vector<std::size_t> nonEmptyLayouts;
            for (const auto& layout : current.layouts)
            {
                if (!layout.layoutSnapshot.placements.empty())
                    nonEmptyLayouts.push_back(layout.layoutIndex);
            }
            if (!nonEmptyLayouts.empty())
            {
                const std::size_t layoutIndex =
                    nonEmptyLayouts[random() % nonEmptyLayouts.size()];
                assert(problem.Value().FitStrip(layoutIndex).HasValue());
            }
        }
        else if (operation == 5)
        {
            const double width =
                4.0 + static_cast<double>(random() % 100) / 10.0;
            assert(problem.Value().AddLayout({
                       14.0, 10.0, width, jaguar::CDEConfig{},
                       jaguar::ShapeModifyConfig{}})
                       .HasValue());
        }
        else if (operation == 6 && !current.layouts.empty())
        {
            const std::size_t layoutIndex =
                current.layouts[random() % current.layouts.size()].layoutIndex;
            assert(problem.Value().RemoveLayout(layoutIndex).HasValue());
        }

        assertMsppRebuildEquivalent(problem.Value(), instance);
        if (step % 250 == 0 && problem.Value().PlacedItemCount() > 0)
            assertMsppJsonCheckpoint(problem.Value());
    }
}
}

void runProblemRandomStateTests()
{
    runRandomSpp();
    runRandomBpp();
    runRandomMspp();
}
