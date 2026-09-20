#include "JaguarCore/problems/bpp.h"
#include "JaguarCore/problems/mspp.h"
#include "JaguarCore/problems/spp.h"
#include "JaguarCore/assertions.h"
#include "JaguarCore/io/importer.h"
#include "JaguarCore/io/json_codec.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
std::string loadReference(const char* fileName)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\tests\\fixtures\\rust_reference\\") + fileName,
             std::string("JaguarCore\\tests\\fixtures\\rust_reference\\") + fileName})
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>());
    }
    return {};
}

jaguar::Item importItem(const jaguar::ExternalDemandItem& external,
                        jaguar::ItemIndex index,
                        const jaguar::Importer& importer)
{
    const auto item = importer.importItem(external.item, index);
    assert(item.hasValue());
    return item.value();
}

jaguar::Container importContainer(const jaguar::ExternalContainer& external,
                                   const jaguar::Importer& importer)
{
    const auto container = importer.importContainer(external);
    assert(container.hasValue());
    return container.value();
}
}

void runProblemDifferentialTests()
{
    const jaguar::Importer importer;
    jaguar::CDEConfig cdeConfig{};
    jaguar::ShapeModifyConfig shapeConfig{};

    const auto sppJson = loadReference("spp_instance.json");
    assert(!sppJson.empty());
    const auto sppExternal = jaguar::parseExternalSPInstanceJson(sppJson);
    assert(sppExternal.hasValue());
    std::vector<jaguar::Item> sppItems;
    std::vector<std::size_t> sppDemands;
    for (std::size_t i = 0; i < sppExternal.value().items.size(); ++i)
    {
        const auto& externalItem = sppExternal.value().items[i];
        if (externalItem.demand == 0)
            continue;
        sppItems.push_back(importItem(externalItem, sppItems.size(), importer));
        sppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    const auto sppInstance = jaguar::SPInstance::create(
        std::move(sppItems), std::move(sppDemands),
        sppExternal.value().stripHeight, cdeConfig, shapeConfig);
    assert(sppInstance.hasValue());
    assert(sppInstance.value().totalDemand() == 2);
    assert(std::abs(sppInstance.value().totalItemArea() - 8.0) < 1.0e-9);
    assert(std::abs(sppInstance.value().baseStrip().width - 2.0) < 1.0e-9);
    auto sppProblem = jaguar::SPProblem::create(sppInstance.value());
    assert(sppProblem.hasValue());
    assert(sppProblem.value().remainingDemand(0) == 2);
    const auto sppPlaced = sppProblem.value().placeItem({
        0, jaguar::Transform2d::fromRotationTranslation(0.0, {1.0, 1.0})});
    assert(sppPlaced.hasValue());
    assert(sppProblem.value().remainingDemand(0) == 1);
    const auto sppSolution = sppProblem.value().save();
    assert(jaguar::validateSPSolution(sppProblem.value(), sppSolution).hasValue());
    assert(sppSolution.layoutSnapshot.placements.size() == 1);
    assert(sppSolution.layoutSnapshot.cdeSnapshot.dynamicHazards.size() == 1);
    assert(std::abs(sppSolution.density() - 0.5) < 1.0e-9);
    assert(sppProblem.value().removeItem(sppPlaced.value()).hasValue());
    assert(sppProblem.value().restore(sppSolution).hasValue());
    assert(sppProblem.value().remainingDemand(0) == 1);

    const auto bppJson = loadReference("bpp_instance.json");
    assert(!bppJson.empty());
    const auto bppExternal = jaguar::parseExternalBPInstanceJson(bppJson);
    assert(bppExternal.hasValue());
    std::vector<jaguar::Item> bppItems;
    std::vector<std::size_t> bppDemands;
    for (std::size_t i = 0; i < bppExternal.value().items.size(); ++i)
    {
        const auto& externalItem = bppExternal.value().items[i];
        if (externalItem.demand == 0)
            continue;
        bppItems.push_back(importItem(externalItem, bppItems.size(), importer));
        bppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    std::vector<jaguar::Bin> bins;
    for (std::size_t i = 0; i < bppExternal.value().bins.size(); ++i)
    {
        const auto& externalBin = bppExternal.value().bins[i];
        if (externalBin.stock == 0)
            continue;
        bins.push_back({i, externalBin.container.id,
                        importContainer(externalBin.container, importer),
                        static_cast<std::size_t>(externalBin.stock),
                        externalBin.cost});
    }
    const auto bppInstance = jaguar::BPInstance::create(
        std::move(bppItems), std::move(bppDemands), std::move(bins));
    assert(bppInstance.hasValue());
    auto bppProblem = jaguar::BPProblem::create(bppInstance.value());
    assert(bppProblem.hasValue());
    assert(bppProblem.value().remainingDemand(0) == 1);
    assert(bppProblem.value().remainingBinStock(0) == 1);
    const auto bppPlaced = bppProblem.value().placeItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::fromRotationTranslation(0.0, {1.0, 1.0})});
    assert(bppPlaced.hasValue());
    assert(bppProblem.value().remainingDemand(0) == 0);
    assert(bppProblem.value().remainingBinStock(0) == 0);
    const auto bppSolution = bppProblem.value().save();
    assert(jaguar::validateBPSolution(bppProblem.value(), bppSolution).hasValue());
    assert(bppSolution.cost(bppInstance.value()) == 10);
    assert(bppSolution.density() > 0.0);
    assert(bppProblem.value().removeItem(bppPlaced.value()).hasValue());
    assert(bppProblem.value().restore(bppSolution).hasValue());
    assert(bppProblem.value().remainingDemand(0) == 0);

    const auto msppJson = loadReference("mspp_instance.json");
    assert(!msppJson.empty());
    const auto msppExternal = jaguar::parseExternalMSPInstanceJson(msppJson);
    assert(msppExternal.hasValue());
    std::vector<jaguar::Item> msppItems;
    std::vector<std::size_t> msppDemands;
    for (std::size_t i = 0; i < msppExternal.value().items.size(); ++i)
    {
        const auto& externalItem = msppExternal.value().items[i];
        if (externalItem.demand == 0)
            continue;
        msppItems.push_back(importItem(externalItem, msppItems.size(), importer));
        msppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    const auto msppInstance = jaguar::MSPInstance::create(
        std::move(msppItems), std::move(msppDemands),
        msppExternal.value().strips.maxWidth,
        msppExternal.value().strips.height, cdeConfig, shapeConfig);
    assert(msppInstance.hasValue());
    auto msppProblem = jaguar::MSPProblem::create(msppInstance.value());
    assert(msppProblem.hasValue());
    assert(msppProblem.value().remainingDemand(0) == 1);
    assert(std::abs(msppProblem.value().instance().baseStrip().maxWidth - 10.0) <
           1.0e-9);
    const auto msppLayout = msppProblem.value().addLayout(
        msppProblem.value().instance().baseStrip());
    assert(msppLayout.hasValue());
    const auto msppPlaced = msppProblem.value().placeItem({
        msppLayout.value(), 0,
        jaguar::Transform2d::fromRotationTranslation(0.0, {1.0, 1.0})});
    assert(msppPlaced.hasValue());
    assert(msppProblem.value().remainingDemand(0) == 0);
    const auto msppSolution = msppProblem.value().save();
    assert(jaguar::validateMSPSolution(msppProblem.value(), msppSolution).hasValue());
    assert(std::abs(msppSolution.totalStripWidth() - 10.0) < 1.0e-9);
    assert(msppSolution.density() > 0.0);
    assert(msppProblem.value().removeItem(msppPlaced.value()).hasValue());
    assert(msppProblem.value().restore(msppSolution).hasValue());
    assert(msppProblem.value().remainingDemand(0) == 0);
}
