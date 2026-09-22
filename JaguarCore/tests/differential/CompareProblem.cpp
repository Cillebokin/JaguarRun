#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"

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
    assert(sppInstance.Value().TotalDemand() == 2);
    assert(std::abs(sppInstance.Value().TotalItemArea() - 8.0) < 1.0e-9);
    assert(std::abs(sppInstance.Value().BaseStrip().width - 2.0) < 1.0e-9);
    auto sppProblem = jaguar::SPProblem::Create(sppInstance.Value());
    assert(sppProblem.HasValue());
    assert(sppProblem.Value().RemainingDemand(0) == 2);
    const auto sppPlaced = sppProblem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(sppPlaced.HasValue());
    assert(sppProblem.Value().RemainingDemand(0) == 1);
    const auto sppSolution = sppProblem.Value().Save();
    assert(jaguar::ValidateSPSolution(sppProblem.Value(), sppSolution).HasValue());
    assert(sppSolution.layoutSnapshot.placements.size() == 1);
    assert(sppSolution.layoutSnapshot.cdeSnapshot.dynamicHazards.size() == 1);
    assert(std::abs(sppSolution.Density() - 0.5) < 1.0e-9);
    assert(sppProblem.Value().RemoveItem(sppPlaced.Value()).HasValue());
    assert(sppProblem.Value().Restore(sppSolution).HasValue());
    assert(sppProblem.Value().RemainingDemand(0) == 1);

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
    auto bppProblem = jaguar::BPProblem::Create(bppInstance.Value());
    assert(bppProblem.HasValue());
    assert(bppProblem.Value().RemainingDemand(0) == 1);
    assert(bppProblem.Value().RemainingBinStock(0) == 1);
    const auto bppPlaced = bppProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(bppPlaced.HasValue());
    assert(bppProblem.Value().RemainingDemand(0) == 0);
    assert(bppProblem.Value().RemainingBinStock(0) == 0);
    const auto bppSolution = bppProblem.Value().Save();
    assert(jaguar::ValidateBPSolution(bppProblem.Value(), bppSolution).HasValue());
    assert(bppSolution.Cost(bppInstance.Value()) == 10);
    assert(bppSolution.Density() > 0.0);
    assert(bppProblem.Value().RemoveItem(bppPlaced.Value()).HasValue());
    assert(bppProblem.Value().Restore(bppSolution).HasValue());
    assert(bppProblem.Value().RemainingDemand(0) == 0);

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
    auto msppProblem = jaguar::MSPProblem::Create(msppInstance.Value());
    assert(msppProblem.HasValue());
    assert(msppProblem.Value().RemainingDemand(0) == 1);
    assert(std::abs(msppProblem.Value().Instance().BaseStrip().maxWidth - 10.0) <
           1.0e-9);
    const auto msppLayout = msppProblem.Value().AddLayout(
        msppProblem.Value().Instance().BaseStrip());
    assert(msppLayout.HasValue());
    const auto msppPlaced = msppProblem.Value().PlaceItem({
        msppLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(msppPlaced.HasValue());
    assert(msppProblem.Value().RemainingDemand(0) == 0);
    const auto msppSolution = msppProblem.Value().Save();
    assert(jaguar::ValidateMSPSolution(msppProblem.Value(), msppSolution).HasValue());
    assert(std::abs(msppSolution.TotalStripWidth() - 10.0) < 1.0e-9);
    assert(msppSolution.Density() > 0.0);
    assert(msppProblem.Value().RemoveItem(msppPlaced.Value()).HasValue());
    assert(msppProblem.Value().Restore(msppSolution).HasValue());
    assert(msppProblem.Value().RemainingDemand(0) == 0);
}
