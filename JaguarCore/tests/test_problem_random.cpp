#include "JaguarCore/assertions.h"
#include "JaguarCore/io/importer.h"
#include "JaguarCore/io/json_codec.h"
#include "JaguarCore/problems/bpp.h"
#include "JaguarCore/problems/mspp.h"
#include "JaguarCore/problems/spp.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace
{
constexpr double kPi = 3.141592653589793238462643383279502884;

jaguar::Item makeItem(jaguar::ItemIndex index,
                      jaguar::ItemId externalId,
                      double width,
                      double height)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(polygon.hasValue());
    jaguar::OriginalShape original{
        polygon.value(), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Inflate, {}};
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto item = jaguar::Item::create(
        index, externalId, std::move(original), surrogateConfig);
    assert(item.hasValue());
    return item.value();
}

jaguar::Container makeContainer(jaguar::ContainerId id,
                                double width,
                                double height)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}});
    assert(polygon.hasValue());
    jaguar::OriginalShape original{
        polygon.value(), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Deflate, {}};
    const auto container = jaguar::Container::create(
        id, std::move(original), {}, jaguar::CDEConfig{});
    assert(container.hasValue());
    return container.value();
}

jaguar::Transform2d randomTransform(std::mt19937_64& random,
                                    std::uniform_real_distribution<double>& position)
{
    const double rotation = static_cast<double>(random() % 4) * kPi * 0.5;
    return jaguar::Transform2d::fromRotationTranslation(
        rotation, {position(random), position(random)});
}

void assertSppRebuildEquivalent(const jaguar::SPProblem& problem,
                                const jaguar::SPInstance& instance)
{
    const jaguar::SPSolution state = problem.save();
    assert(jaguar::validateSPSolution(problem, state).hasValue());

    auto rebuilt = jaguar::SPProblem::create(instance);
    assert(rebuilt.hasValue());
    assert(rebuilt.value().restore(state).hasValue());
    assert(jaguar::validateSPSolution(rebuilt.value(), state).hasValue());
    assert(rebuilt.value().placedItemCount() == problem.placedItemCount());
    assert(std::abs(rebuilt.value().stripWidth() - problem.stripWidth()) < 1.0e-9);
    assert(std::abs(rebuilt.value().density() - problem.density()) < 1.0e-12);
    for (std::size_t i = 0; i < instance.itemCount(); ++i)
        assert(rebuilt.value().remainingDemand(i) == problem.remainingDemand(i));
}

void assertBppRebuildEquivalent(const jaguar::BPProblem& problem,
                                const jaguar::BPInstance& instance)
{
    const jaguar::BPSolution state = problem.save();
    assert(jaguar::validateBPSolution(problem, state).hasValue());

    auto rebuilt = jaguar::BPProblem::create(instance);
    assert(rebuilt.hasValue());
    assert(rebuilt.value().restore(state).hasValue());
    assert(jaguar::validateBPSolution(rebuilt.value(), state).hasValue());
    assert(rebuilt.value().placedItemCount() == problem.placedItemCount());
    assert(rebuilt.value().openLayoutCount() == problem.openLayoutCount());
    assert(rebuilt.value().binCost() == problem.binCost());
    assert(std::abs(rebuilt.value().density() - problem.density()) < 1.0e-12);
    for (std::size_t i = 0; i < instance.itemCount(); ++i)
        assert(rebuilt.value().remainingDemand(i) == problem.remainingDemand(i));
    for (std::size_t i = 0; i < instance.binCount(); ++i)
        assert(rebuilt.value().remainingBinStock(i) == problem.remainingBinStock(i));
}

void assertMsppRebuildEquivalent(const jaguar::MSPProblem& problem,
                                 const jaguar::MSPInstance& instance)
{
    const jaguar::MSPSolution state = problem.save();
    assert(jaguar::validateMSPSolution(problem, state).hasValue());

    auto rebuilt = jaguar::MSPProblem::create(instance);
    assert(rebuilt.hasValue());
    assert(rebuilt.value().restore(state).hasValue());
    assert(jaguar::validateMSPSolution(rebuilt.value(), state).hasValue());
    assert(rebuilt.value().placedItemCount() == problem.placedItemCount());
    assert(rebuilt.value().layoutCount() == problem.layoutCount());
    assert(std::abs(rebuilt.value().totalStripWidth() -
                    problem.totalStripWidth()) < 1.0e-9);
    assert(std::abs(rebuilt.value().density() - problem.density()) < 1.0e-12);
    for (std::size_t i = 0; i < instance.itemCount(); ++i)
        assert(rebuilt.value().remainingDemand(i) == problem.remainingDemand(i));
}

void assertSppJsonCheckpoint(const jaguar::SPProblem& problem)
{
    const jaguar::SPInstance& instance = problem.instance();
    const jaguar::SPSolution solution = problem.save();
    const auto external = jaguar::externalSPSolutionFromSolution(instance, solution);
    assert(external.hasValue());
    const auto json = jaguar::serializeExternalSPSolutionJson(external.value());
    assert(json.hasValue());
    const auto parsed = jaguar::parseExternalSPSolutionJson(json.value());
    assert(parsed.hasValue());
    const auto imported = jaguar::Importer{}.importSPSolution(
        instance, parsed.value());
    const auto importedFromJson = jaguar::Importer{}.importSPSolutionJson(
        instance, json.value());
    assert(imported.hasValue());
    assert(importedFromJson.hasValue());
    assert(imported.value().layoutSnapshot.placements.size() ==
           solution.layoutSnapshot.placements.size());
    assert(importedFromJson.value().layoutSnapshot.placements.size() ==
           solution.layoutSnapshot.placements.size());
    assert(std::abs(imported.value().stripWidth() - solution.stripWidth()) <
           1.0e-9);
    assert(std::abs(importedFromJson.value().stripWidth() - solution.stripWidth()) <
           1.0e-9);

    auto rebuilt = jaguar::SPProblem::create(instance);
    assert(rebuilt.hasValue());
    assert(rebuilt.value().restore(importedFromJson.value()).hasValue());
    assert(jaguar::validateSPSolution(rebuilt.value(), importedFromJson.value()).hasValue());
    assert(rebuilt.value().placedItemCount() == problem.placedItemCount());
    assert(std::abs(rebuilt.value().density() - problem.density()) < 1.0e-12);
}

void assertBppJsonCheckpoint(const jaguar::BPProblem& problem)
{
    const jaguar::BPInstance& instance = problem.instance();
    const jaguar::BPSolution solution = problem.save();
    const auto external = jaguar::externalBPSolutionFromSolution(instance, solution);
    assert(external.hasValue());
    const auto json = jaguar::serializeExternalBPSolutionJson(external.value());
    assert(json.hasValue());
    const auto parsed = jaguar::parseExternalBPSolutionJson(json.value());
    assert(parsed.hasValue());
    const auto imported = jaguar::Importer{}.importBPSolution(
        instance, parsed.value());
    const auto importedFromJson = jaguar::Importer{}.importBPSolutionJson(
        instance, json.value());
    assert(imported.hasValue());
    assert(importedFromJson.hasValue());
    assert(imported.value().layouts.size() == solution.layouts.size());
    assert(importedFromJson.value().layouts.size() == solution.layouts.size());
    assert(imported.value().cost(instance) == solution.cost(instance));
    assert(importedFromJson.value().cost(instance) == solution.cost(instance));

    auto rebuilt = jaguar::BPProblem::create(instance);
    assert(rebuilt.hasValue());
    assert(rebuilt.value().restore(importedFromJson.value()).hasValue());
    assert(jaguar::validateBPSolution(rebuilt.value(), importedFromJson.value()).hasValue());
    assert(rebuilt.value().placedItemCount() == problem.placedItemCount());
    assert(rebuilt.value().binCost() == problem.binCost());
    assert(std::abs(rebuilt.value().density() - problem.density()) < 1.0e-12);
}

void assertMsppJsonCheckpoint(const jaguar::MSPProblem& problem)
{
    const jaguar::MSPInstance& instance = problem.instance();
    const jaguar::MSPSolution solution = problem.save();
    const auto external = jaguar::externalMSPSolutionFromSolution(instance, solution);
    assert(external.hasValue());
    const auto json = jaguar::serializeExternalMSPSolutionJson(external.value());
    assert(json.hasValue());
    const auto parsed = jaguar::parseExternalMSPSolutionJson(json.value());
    assert(parsed.hasValue());
    const auto imported = jaguar::Importer{}.importMSPSolution(
        instance, parsed.value());
    const auto importedFromJson = jaguar::Importer{}.importMSPSolutionJson(
        instance, json.value());
    assert(imported.hasValue());
    assert(importedFromJson.hasValue());
    assert(imported.value().layouts.size() == solution.layouts.size());
    assert(importedFromJson.value().layouts.size() == solution.layouts.size());
    assert(std::abs(imported.value().totalStripWidth() -
                    solution.totalStripWidth()) < 1.0e-9);
    assert(std::abs(importedFromJson.value().totalStripWidth() -
                    solution.totalStripWidth()) < 1.0e-9);

    auto rebuilt = jaguar::MSPProblem::create(instance);
    assert(rebuilt.hasValue());
    assert(rebuilt.value().restore(importedFromJson.value()).hasValue());
    assert(jaguar::validateMSPSolution(rebuilt.value(), importedFromJson.value()).hasValue());
    assert(rebuilt.value().placedItemCount() == problem.placedItemCount());
    assert(std::abs(rebuilt.value().density() - problem.density()) < 1.0e-12);
}

void runRandomSpp()
{
    const std::vector<jaguar::Item> items{
        makeItem(0, 4100, 2.0, 2.0), makeItem(1, 4101, 1.0, 2.0)};
    const auto instanceResult = jaguar::SPInstance::create(
        items, {12, 12}, 10.0, jaguar::CDEConfig{}, jaguar::ShapeModifyConfig{});
    assert(instanceResult.hasValue());
    const jaguar::SPInstance instance = instanceResult.value();
    auto problem = jaguar::SPProblem::create(instance);
    assert(problem.hasValue());

    std::mt19937_64 random(0x5350505F52414E44ULL);
    std::uniform_real_distribution<double> position(0.0, 8.0);
    for (std::size_t step = 0; step < 1000; ++step)
    {
        const auto current = problem.value().save();
        const std::size_t operation = random() % 5;
        if (operation == 0 || current.layoutSnapshot.placements.empty())
        {
            std::vector<std::size_t> available;
            for (std::size_t i = 0; i < instance.itemCount(); ++i)
            {
                if (problem.value().remainingDemand(i) > 0)
                    available.push_back(i);
            }
            if (!available.empty())
            {
                const std::size_t itemIndex =
                    available[random() % available.size()];
                assert(problem.value().placeItem({
                           itemIndex, randomTransform(random, position)})
                           .hasValue());
            }
        }
        else if (operation == 1)
        {
            const auto& placements = current.layoutSnapshot.placements;
            assert(problem.value().removeItem(
                       placements[random() % placements.size()].handle)
                       .hasValue());
            (void)placements;
        }
        else if (operation == 2)
        {
            assert(problem.value().restore(current).hasValue());
        }
        else if (operation == 3)
        {
            const double width = 6.0 + static_cast<double>(random() % 120) / 10.0;
            assert(problem.value().changeStripWidth(width).hasValue());
        }
        else if (!current.layoutSnapshot.placements.empty())
        {
            const auto fitted = problem.value().fitStrip();
            assert(fitted.hasValue());
        }

        assertSppRebuildEquivalent(problem.value(), instance);
        if (step % 250 == 0 && problem.value().placedItemCount() > 0)
            assertSppJsonCheckpoint(problem.value());
    }
}

void runRandomBpp()
{
    const std::vector<jaguar::Item> items{
        makeItem(0, 4200, 2.0, 2.0), makeItem(1, 4201, 1.0, 2.0)};
    std::vector<jaguar::Bin> bins{
        {0, 5200, makeContainer(5200, 12.0, 10.0), 5, 10},
        {1, 5201, makeContainer(5201, 14.0, 10.0), 5, 25}};
    const auto instanceResult = jaguar::BPInstance::create(
        items, {16, 16}, std::move(bins));
    assert(instanceResult.hasValue());
    const jaguar::BPInstance instance = instanceResult.value();
    auto problem = jaguar::BPProblem::create(instance);
    assert(problem.hasValue());

    std::mt19937_64 random(0x4250505F52414E44ULL);
    std::uniform_real_distribution<double> position(0.0, 9.0);
    for (std::size_t step = 0; step < 1000; ++step)
    {
        const auto current = problem.value().save();
        const std::size_t operation = random() % 5;
        std::vector<std::size_t> availableItems;
        for (std::size_t i = 0; i < instance.itemCount(); ++i)
        {
            if (problem.value().remainingDemand(i) > 0)
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
                for (std::size_t i = 0; i < instance.binCount(); ++i)
                {
                    if (problem.value().remainingBinStock(i) > 0)
                        availableBins.push_back(i);
                }
                if (!availableBins.empty())
                {
                    const std::size_t binIndex =
                        availableBins[random() % availableBins.size()];
                    assert(problem.value().placeItem({
                               jaguar::LayoutTargetKind::Closed, 0, binIndex,
                               itemIndex, randomTransform(random, position)})
                               .hasValue());
                }
            }
            else
            {
                const auto& selectedLayout =
                    current.layouts[random() % current.layouts.size()];
                assert(problem.value().placeItem({
                           jaguar::LayoutTargetKind::Open,
                           selectedLayout.layoutIndex, selectedLayout.binIndex,
                           itemIndex,
                           randomTransform(random, position)})
                           .hasValue());
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
                assert(problem.value().removeItem(
                           candidates[random() % candidates.size()])
                           .hasValue());
            }
        }
        else if (operation == 2)
        {
            assert(problem.value().restore(current).hasValue());
        }

        assertBppRebuildEquivalent(problem.value(), instance);
        if (step % 250 == 0 && problem.value().placedItemCount() > 0)
            assertBppJsonCheckpoint(problem.value());
    }
}

void runRandomMspp()
{
    const std::vector<jaguar::Item> items{
        makeItem(0, 4300, 2.0, 2.0), makeItem(1, 4301, 1.0, 2.0)};
    const auto instanceResult = jaguar::MSPInstance::create(
        items, {16, 16}, 14.0, 10.0, jaguar::CDEConfig{},
        jaguar::ShapeModifyConfig{});
    assert(instanceResult.hasValue());
    const jaguar::MSPInstance instance = instanceResult.value();
    auto problem = jaguar::MSPProblem::create(instance);
    assert(problem.hasValue());

    std::mt19937_64 random(0x4D53505F52414E44ULL);
    std::uniform_real_distribution<double> position(0.0, 2.0);
    for (std::size_t step = 0; step < 1000; ++step)
    {
        const auto current = problem.value().save();
        const std::size_t operation = random() % 7;
        std::vector<std::size_t> availableItems;
        for (std::size_t i = 0; i < instance.itemCount(); ++i)
        {
            if (problem.value().remainingDemand(i) > 0)
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
                const auto added = problem.value().addLayout({
                    14.0, 10.0, width, jaguar::CDEConfig{},
                    jaguar::ShapeModifyConfig{}});
                assert(added.hasValue());
                layoutIndex = added.value();
            }
            else
            {
                layoutIndex =
                    current.layouts[random() % current.layouts.size()].layoutIndex;
            }
            const std::size_t itemIndex =
                availableItems[random() % availableItems.size()];
            assert(problem.value().placeItem({
                       layoutIndex, itemIndex, randomTransform(random, position)})
                       .hasValue());
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
                assert(problem.value().removeItem(
                           candidates[random() % candidates.size()])
                           .hasValue());
            }
        }
        else if (operation == 2)
        {
            assert(problem.value().restore(current).hasValue());
        }
        else if (operation == 3 && !current.layouts.empty())
        {
            const std::size_t layoutIndex =
                current.layouts[random() % current.layouts.size()].layoutIndex;
            const double width =
                4.0 + static_cast<double>(random() % 100) / 10.0;
            assert(problem.value().changeStripWidth(layoutIndex, width).hasValue());
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
                assert(problem.value().fitStrip(layoutIndex).hasValue());
            }
        }
        else if (operation == 5)
        {
            const double width =
                4.0 + static_cast<double>(random() % 100) / 10.0;
            assert(problem.value().addLayout({
                       14.0, 10.0, width, jaguar::CDEConfig{},
                       jaguar::ShapeModifyConfig{}})
                       .hasValue());
        }
        else if (operation == 6 && !current.layouts.empty())
        {
            const std::size_t layoutIndex =
                current.layouts[random() % current.layouts.size()].layoutIndex;
            assert(problem.value().removeLayout(layoutIndex).hasValue());
        }

        assertMsppRebuildEquivalent(problem.value(), instance);
        if (step % 250 == 0 && problem.value().placedItemCount() > 0)
            assertMsppJsonCheckpoint(problem.value());
    }
}
}

void runProblemRandomStateTests()
{
    runRandomSpp();
    runRandomBpp();
    runRandomMspp();
}
