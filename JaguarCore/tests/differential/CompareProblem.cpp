#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string loadReference(const char* fileName)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + fileName})
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>());
    }
    return {};
}

using ReferenceMap = std::map<std::string, std::string>;

ReferenceMap loadProblemReference()
{
    const auto contents = loadReference("problem_types_b7.txt");
    assert(!contents.empty());
    ReferenceMap reference;
    std::istringstream input(contents);
    for (std::string line; std::getline(input, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto separator = line.find('=');
        if (line.empty() || separator == std::string::npos || line[0] == '#')
            continue;
        reference.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    assert(!reference.empty());
    return reference;
}

void expectRustNumbers(const ReferenceMap& reference,
                       const std::string& key,
                       std::initializer_list<double> actual)
{
    const auto found = reference.find(key);
    if (found == reference.end())
        std::fprintf(stderr, "B7 Rust reference is missing key: %s\n", key.c_str());
    assert(found != reference.end());

    std::vector<float> expected;
    std::istringstream input(found->second);
    bool parseValid = true;
    for (std::string part; std::getline(input, part, ',');)
    {
        try
        {
            std::size_t parsed = 0;
            const float value = std::stof(part, &parsed);
            if (parsed != part.size())
            {
                parseValid = false;
                break;
            }
            expected.push_back(value);
        }
        catch (...)
        {
            parseValid = false;
            break;
        }
    }
    if (!parseValid)
        expected.clear();

    if (expected.size() != actual.size())
        std::fprintf(stderr,
                     "B7 reference %s: Rust has %zu values, C++ has %zu\n",
                     key.c_str(), expected.size(), actual.size());
    assert(expected.size() == actual.size());

    std::size_t index = 0;
    for (double actualValue : actual)
    {
        const float actualF32 = static_cast<float>(actualValue);
        if (!jaguar::detail::AlmostEqualRustF32(expected[index], actualF32))
            std::fprintf(stderr,
                         "B7 reference %s[%zu]: Rust %.9g, C++ %.9g\n",
                         key.c_str(), index, expected[index], actualF32);
        assert(jaguar::detail::AlmostEqualRustF32(expected[index], actualF32));
        ++index;
    }
}

void expectRustText(const ReferenceMap& reference,
                    const std::string& key,
                    const std::string& actual)
{
    const auto found = reference.find(key);
    if (found == reference.end())
        std::fprintf(stderr, "B7 Rust reference is missing key: %s\n", key.c_str());
    assert(found != reference.end());
    if (found->second != actual)
        std::fprintf(stderr, "B7 reference %s: Rust %s, C++ %s\n",
                     key.c_str(), found->second.c_str(), actual.c_str());
    assert(found->second == actual);
}

jaguar::Item ImportItem(const jaguar::ExternalDemandItem& external,
                        jaguar::ItemIndex index,
                        const jaguar::Importer& importer)
{
    const auto item = importer.ImportItem(external.item, index);
    assert(item.HasValue());
    return item.Value();
}

jaguar::Container ImportContainer(const jaguar::ExternalContainer& external,
                                   const jaguar::Importer& importer)
{
    const auto container = importer.ImportContainer(external);
    assert(container.HasValue());
    return container.Value();
}
}

void runProblemDifferentialTests()
{
    const auto problemReference = loadProblemReference();
    const jaguar::Importer importer;
    jaguar::CDEConfig cdeConfig{};
    jaguar::ShapeModifyConfig shapeConfig{};

    const auto sppJson = loadReference("spp_instance.json");
    assert(!sppJson.empty());
    const auto sppExternal = jaguar::ParseExternalSPInstanceJson(sppJson);
    assert(sppExternal.HasValue());
    std::vector<jaguar::Item> sppItems;
    std::vector<std::size_t> sppDemands;
    for (std::size_t i = 0; i < sppExternal.Value().items.size(); ++i)
    {
        const auto& externalItem = sppExternal.Value().items[i];
        if (externalItem.demand == 0)
            continue;
        sppItems.push_back(ImportItem(externalItem, sppItems.size(), importer));
        sppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    const auto sppInstance = jaguar::SPInstance::Create(
        std::move(sppItems), std::move(sppDemands),
        sppExternal.Value().stripHeight, cdeConfig, shapeConfig);
    assert(sppInstance.HasValue());
    expectRustNumbers(problemReference, "spp.instance",
                      {static_cast<double>(sppInstance.Value().TotalDemand()),
                       sppInstance.Value().TotalItemArea(),
                       sppInstance.Value().BaseStrip().fixedHeight,
                       sppInstance.Value().BaseStrip().width});
    auto sppProblem = jaguar::SPProblem::Create(sppInstance.Value());
    assert(sppProblem.HasValue());
    assert(sppProblem.Value().RemainingDemand(0) == 2);
    assert(sppProblem.Value().ChangeStripWidth(5.0).HasValue());
    expectRustNumbers(problemReference, "spp.changed", {sppProblem.Value().StripWidth()});
    const auto sppPlaced = sppProblem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(sppPlaced.HasValue());
    expectRustNumbers(problemReference, "spp.placed",
                      {static_cast<double>(sppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(sppProblem.Value().PlacedItemCount()),
                       sppProblem.Value().Density(),
                       sppProblem.Value().StripWidth()});
    const auto sppSolution = sppProblem.Value().Save();
    assert(jaguar::ValidateSPSolution(sppProblem.Value(), sppSolution).HasValue());
    expectRustNumbers(problemReference, "spp.solution",
                      {static_cast<double>(sppSolution.layoutSnapshot.placements.size()),
                       sppSolution.Density(), sppSolution.StripWidth()});
    assert(sppProblem.Value().RemoveItem(sppPlaced.Value()).HasValue());
    expectRustNumbers(problemReference, "spp.removed",
                      {static_cast<double>(sppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(sppProblem.Value().PlacedItemCount()),
                       sppProblem.Value().Density()});
    assert(sppProblem.Value().Restore(sppSolution).HasValue());
    expectRustNumbers(problemReference, "spp.restored",
                      {static_cast<double>(sppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(sppProblem.Value().PlacedItemCount()),
                       sppProblem.Value().Density(),
                       sppProblem.Value().StripWidth()});

    const auto bppJson = loadReference("bpp_instance.json");
    assert(!bppJson.empty());
    const auto bppExternal = jaguar::ParseExternalBPInstanceJson(bppJson);
    assert(bppExternal.HasValue());
    std::vector<jaguar::Item> bppItems;
    std::vector<std::size_t> bppDemands;
    for (std::size_t i = 0; i < bppExternal.Value().items.size(); ++i)
    {
        const auto& externalItem = bppExternal.Value().items[i];
        if (externalItem.demand == 0)
            continue;
        bppItems.push_back(ImportItem(externalItem, bppItems.size(), importer));
        bppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    std::vector<jaguar::Bin> bins;
    for (std::size_t i = 0; i < bppExternal.Value().bins.size(); ++i)
    {
        const auto& externalBin = bppExternal.Value().bins[i];
        if (externalBin.stock == 0)
            continue;
        bins.push_back({i, externalBin.container.id,
                        ImportContainer(externalBin.container, importer),
                        static_cast<std::size_t>(externalBin.stock),
                        externalBin.cost});
    }
    const auto bppInstance = jaguar::BPInstance::Create(
        std::move(bppItems), std::move(bppDemands), std::move(bins));
    assert(bppInstance.HasValue());
    expectRustNumbers(problemReference, "bpp.instance",
                      {static_cast<double>(bppInstance.Value().TotalDemand()),
                       bppInstance.Value().TotalItemArea(),
                       static_cast<double>(bppInstance.Value().BinCount()),
                       static_cast<double>(bppInstance.Value().GetBin(0).stock),
                       static_cast<double>(bppInstance.Value().GetBin(0).cost)});
    auto bppProblem = jaguar::BPProblem::Create(bppInstance.Value());
    assert(bppProblem.HasValue());
    const auto bppFirstPlaced = bppProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(bppFirstPlaced.HasValue());
    expectRustNumbers(problemReference, "bpp.closed",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});
    const auto bppSecondPlaced = bppProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, bppFirstPlaced.Value().layoutIndex, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {3.5, 1.0})});
    assert(bppSecondPlaced.HasValue());
    expectRustNumbers(problemReference, "bpp.open",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});
    const auto bppSolution = bppProblem.Value().Save();
    assert(jaguar::ValidateBPSolution(bppProblem.Value(), bppSolution).HasValue());
    expectRustNumbers(problemReference, "bpp.solution",
                      {static_cast<double>(bppSolution.layouts.size()),
                       static_cast<double>(bppSolution.layouts[0].layoutSnapshot.placements.size()),
                       static_cast<double>(bppSolution.Cost(bppInstance.Value())),
                       bppSolution.Density()});
    assert(bppProblem.Value().RemoveItem(bppSecondPlaced.Value()).HasValue());
    expectRustNumbers(problemReference, "bpp.removed",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});
    const auto bppRestored = bppProblem.Value().Restore(bppSolution);
    assert(bppRestored.HasValue());
    expectRustText(problemReference, "bpp.restore_rekey",
                   bppRestored.Value() ? "true" : "false");
    expectRustNumbers(problemReference, "bpp.restored",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});

    const auto msppJson = loadReference("mspp_instance.json");
    assert(!msppJson.empty());
    const auto msppExternal = jaguar::ParseExternalMSPInstanceJson(msppJson);
    assert(msppExternal.HasValue());
    std::vector<jaguar::Item> msppItems;
    std::vector<std::size_t> msppDemands;
    for (std::size_t i = 0; i < msppExternal.Value().items.size(); ++i)
    {
        const auto& externalItem = msppExternal.Value().items[i];
        if (externalItem.demand == 0)
            continue;
        msppItems.push_back(ImportItem(externalItem, msppItems.size(), importer));
        msppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    const auto msppInstance = jaguar::MSPInstance::Create(
        std::move(msppItems), std::move(msppDemands),
        msppExternal.Value().strips.maxWidth,
        msppExternal.Value().strips.height, cdeConfig, shapeConfig);
    assert(msppInstance.HasValue());
    expectRustNumbers(problemReference, "mspp.instance",
                      {static_cast<double>(msppInstance.Value().TotalDemand()),
                       msppInstance.Value().TotalItemArea(),
                       msppInstance.Value().BaseStrip().maxWidth,
                       msppInstance.Value().BaseStrip().fixedHeight,
                       msppInstance.Value().BaseStrip().width});
    auto msppProblem = jaguar::MSPProblem::Create(msppInstance.Value());
    assert(msppProblem.HasValue());
    assert(msppProblem.Value().RemainingDemand(0) == 1);
    const auto msppLayout = msppProblem.Value().AddLayout(
        msppProblem.Value().Instance().BaseStrip());
    assert(msppLayout.HasValue());
    expectRustNumbers(problemReference, "mspp.layout",
                      {static_cast<double>(msppProblem.Value().LayoutCount()),
                       msppProblem.Value().StripWidth(msppLayout.Value()),
                       msppProblem.Value().TotalStripWidth()});
    const auto msppPlaced = msppProblem.Value().PlaceItem({
        msppLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(msppPlaced.HasValue());
    expectRustNumbers(problemReference, "mspp.placed",
                      {static_cast<double>(msppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(msppProblem.Value().PlacedItemCount()),
                       msppProblem.Value().Density(),
                       msppProblem.Value().TotalStripWidth()});
    assert(msppProblem.Value().ChangeStripWidth(msppLayout.Value(), 8.0).HasValue());
    expectRustNumbers(problemReference, "mspp.changed",
                      {msppProblem.Value().StripWidth(msppLayout.Value()),
                       msppProblem.Value().TotalStripWidth(),
                       msppProblem.Value().Density()});
    assert(msppProblem.Value().FitStrip(msppLayout.Value()).HasValue());
    const auto msppSolution = msppProblem.Value().Save();
    assert(jaguar::ValidateMSPSolution(msppProblem.Value(), msppSolution).HasValue());
    expectRustNumbers(problemReference, "mspp.solution",
                      {static_cast<double>(msppSolution.layouts.size()),
                       msppSolution.TotalStripWidth(), msppSolution.Density()});
    assert(msppProblem.Value().RemoveItem(msppPlaced.Value()).HasValue());
    expectRustNumbers(problemReference, "mspp.removed",
                      {static_cast<double>(msppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(msppProblem.Value().PlacedItemCount()),
                       msppProblem.Value().TotalStripWidth(),
                       msppProblem.Value().Density()});
    const auto msppRestored = msppProblem.Value().Restore(msppSolution);
    assert(msppRestored.HasValue());
    expectRustText(problemReference, "mspp.restore_rekey",
                   msppRestored.Value() ? "true" : "false");
    expectRustNumbers(problemReference, "mspp.restored",
                      {static_cast<double>(msppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(msppProblem.Value().PlacedItemCount()),
                       msppProblem.Value().StripWidth(msppLayout.Value()),
                       msppProblem.Value().TotalStripWidth(),
                       msppProblem.Value().Density()});
}
