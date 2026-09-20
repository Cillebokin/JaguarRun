#include "JaguarCore/io/json_codec.h"
#include "JaguarCore/io/importer.h"
#include "JaguarCore/problems/spp.h"
#include "JaguarCore/assertions.h"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
std::string loadFile(const char* name)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\tests\\fixtures\\rust_reference\\") + name,
             std::string("JaguarCore\\tests\\fixtures\\rust_reference\\") + name})
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    return {};
}

std::vector<jaguar::Item> importItems(
    const jaguar::ExternalSPInstance& external, const jaguar::Importer& importer,
    std::vector<std::size_t>& demands)
{
    std::vector<jaguar::Item> items;
    for (const auto& demandItem : external.items)
    {
        if (demandItem.demand == 0)
            continue;
        const auto item = importer.importItem(demandItem.item, items.size());
        assert(item.hasValue());
        items.push_back(item.value());
        demands.push_back(static_cast<std::size_t>(demandItem.demand));
    }
    return items;
}

jaguar::Transform2d externalToInternal(
    const jaguar::ExternalTransformation& external,
    const jaguar::Item& item)
{
    const double radians =
        external.rotationDegrees * 3.14159265358979323846 / 180.0;
    const jaguar::Point2d center = item.originalShape().shape.centroid();
    const double sine = std::sin(radians);
    const double cosine = std::cos(radians);
    const jaguar::Point2d rotatedCenter{
        cosine * center.x - sine * center.y,
        sine * center.x + cosine * center.y};
    return jaguar::Transform2d::fromRotationTranslation(
        radians,
        {external.translation.x + rotatedCenter.x,
         external.translation.y + rotatedCenter.y});
}
}

void runOptimizerDifferentialTests()
{
    const auto instanceJson = loadFile("optimizer_spp_instance.json");
    const auto configJson = loadFile("optimizer_lbf_config.json");
    assert(!instanceJson.empty() && !configJson.empty());
    (void)configJson;
    const auto external = jaguar::parseExternalSPInstanceJson(instanceJson);
    assert(external.hasValue());

    jaguar::ImporterConfig importerConfig{};
    importerConfig.cdeConfig.quadtreeDepth = 4;
    importerConfig.cdeConfig.cdThreshold = 32;
    importerConfig.shapeModifyConfig.simplifyTolerance = std::nullopt;
    const jaguar::Importer importer(importerConfig);
    std::vector<std::size_t> demands;
    auto items = importItems(external.value(), importer, demands);
    const auto instance = jaguar::SPInstance::create(
        std::move(items), std::move(demands), external.value().stripHeight,
        importerConfig.cdeConfig, importerConfig.shapeModifyConfig);
    assert(instance.hasValue());

    // The Rust LBF output is consumed as an external solution fixture when
    // generated. This direct baseline keeps the acceptance contract explicit:
    // JaguarCore validates external placements and metrics, but does not claim
    // to reproduce LBF's optimizer path.
    const auto outputJson = loadFile("lbf_spp\\sol_optimizer_spp_instance.json");
    assert(!outputJson.empty());
    const auto root = jaguar::parseExternalSPSolutionJson(outputJson);
    assert(!root.hasValue());
    (void)root;
    const auto solutionMarker = outputJson.find("\"solution\"");
    assert(solutionMarker != std::string::npos);
    const auto solutionStart = outputJson.find('{', solutionMarker);
    assert(solutionStart != std::string::npos);
    std::size_t depth = 0;
    std::size_t solutionEnd = std::string::npos;
    for (std::size_t i = solutionStart; i < outputJson.size(); ++i)
    {
        if (outputJson[i] == '{')
            ++depth;
        else if (outputJson[i] == '}' && --depth == 0)
        {
            solutionEnd = i + 1;
            break;
        }
    }
    assert(solutionEnd != std::string::npos);
    const auto solutionJson = outputJson.substr(solutionStart, solutionEnd - solutionStart);
    const auto output = jaguar::parseExternalSPSolutionJson(solutionJson);
    assert(output.hasValue());
    assert(std::isfinite(output.value().stripWidth));
    assert(output.value().stripWidth > 0.0);
    assert(std::isfinite(output.value().density));
    assert(output.value().density >= 0.0);
    assert(output.value().density <= 1.0 + 1.0e-6);

    auto problem = jaguar::SPProblem::create(instance.value());
    assert(problem.hasValue());
    assert(problem.value().changeStripWidth(output.value().stripWidth).hasValue());
    for (const auto& placed : output.value().layout.placedItems)
    {
        const auto itemIt = std::find_if(
            external.value().items.begin(), external.value().items.end(),
            [&placed](const auto& candidate) {
                return candidate.item.id == placed.itemId;
            });
        assert(itemIt != external.value().items.end());
        const auto itemIndex = static_cast<jaguar::ItemIndex>(
            std::distance(external.value().items.begin(), itemIt));
        assert(itemIndex < problem.value().instance().itemCount());
        const jaguar::Transform2d transform = externalToInternal(
            placed.transformation, problem.value().instance().item(itemIndex));
        const auto placedResult = problem.value().placeItem({itemIndex, transform});
        assert(placedResult.hasValue());
    }
    assert(problem.value().save().layoutSnapshot.placements.size() ==
           output.value().layout.placedItems.size());
    assert(problem.value().save().layoutSnapshot.cdeSnapshot.dynamicHazards.size() ==
           output.value().layout.placedItems.size());
    const auto rebuilt = problem.value().save();
    assert(jaguar::validateSPSolution(problem.value(), rebuilt).hasValue());
    assert(std::abs(rebuilt.density() - output.value().density) < 5.0e-3);
}
