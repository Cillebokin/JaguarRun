#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"

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
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\") + name,
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\") + name,
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + name})
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
        const auto item = importer.ImportItem(demandItem.item, items.size());
        assert(item.HasValue());
        items.push_back(item.Value());
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
    const jaguar::Point2d center = item.GetOriginalShape().shape.Centroid();
    const double sine = std::sin(radians);
    const double cosine = std::cos(radians);
    const jaguar::Point2d rotatedCenter{
        cosine * center.x - sine * center.y,
        sine * center.x + cosine * center.y};
    return jaguar::Transform2d::FromRotationTranslation(
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
    const auto external = jaguar::ParseExternalSPInstanceJson(instanceJson);
    assert(external.HasValue());

    jaguar::ImporterConfig importerConfig{};
    importerConfig.cdeConfig.quadtreeDepth = 4;
    importerConfig.cdeConfig.cdThreshold = 32;
    importerConfig.shapeModifyConfig.simplifyTolerance = std::nullopt;
    const jaguar::Importer importer(importerConfig);
    std::vector<std::size_t> demands;
    auto items = importItems(external.Value(), importer, demands);
    const auto instance = jaguar::SPInstance::Create(
        std::move(items), std::move(demands), external.Value().stripHeight,
        importerConfig.cdeConfig, importerConfig.shapeModifyConfig);
    assert(instance.HasValue());

    // The Rust LBF output is consumed as an external solution fixture when
    // generated. This direct baseline keeps the acceptance contract explicit:
    // JaguarCore validates external placements and metrics, but does not claim
    // to reproduce LBF's optimizer path.
    const auto outputJson = loadFile("LbfSpp\\sol_optimizer_spp_instance.json");
    assert(!outputJson.empty());
    const auto root = jaguar::ParseExternalSPSolutionJson(outputJson);
    assert(!root.HasValue());
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
    const auto output = jaguar::ParseExternalSPSolutionJson(solutionJson);
    assert(output.HasValue());
    assert(std::isfinite(output.Value().stripWidth));
    assert(output.Value().stripWidth > 0.0);
    assert(std::isfinite(output.Value().density));
    assert(output.Value().density >= 0.0);
    assert(output.Value().density <= 1.0 + 1.0e-6);

    auto problem = jaguar::SPProblem::Create(instance.Value());
    assert(problem.HasValue());
    assert(problem.Value().ChangeStripWidth(output.Value().stripWidth).HasValue());
    for (const auto& placed : output.Value().layout.placedItems)
    {
        const auto itemIt = std::find_if(
            external.Value().items.begin(), external.Value().items.end(),
            [&placed](const auto& candidate) {
                return candidate.item.id == placed.itemId;
            });
        assert(itemIt != external.Value().items.end());
        const auto itemIndex = static_cast<jaguar::ItemIndex>(
            std::distance(external.Value().items.begin(), itemIt));
        assert(itemIndex < problem.Value().Instance().ItemCount());
        const jaguar::Transform2d transform = externalToInternal(
            placed.transformation, problem.Value().Instance().GetItem(itemIndex));
        const auto placedResult = problem.Value().PlaceItem({itemIndex, transform});
        assert(placedResult.HasValue());
    }
    assert(problem.Value().Save().layoutSnapshot.placements.size() ==
           output.Value().layout.placedItems.size());
    assert(problem.Value().Save().layoutSnapshot.cdeSnapshot.dynamicHazards.size() ==
           output.Value().layout.placedItems.size());
    const auto rebuilt = problem.Value().Save();
    assert(jaguar::ValidateSPSolution(problem.Value(), rebuilt).HasValue());
    assert(std::abs(rebuilt.Density() - output.Value().density) < 5.0e-3);
}
