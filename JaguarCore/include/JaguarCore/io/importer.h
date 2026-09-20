#pragma once

#include "JaguarCore/entities/container.h"
#include "JaguarCore/entities/item.h"
#include "JaguarCore/io/external_repr.h"
#include "JaguarCore/problems/bpp.h"
#include "JaguarCore/problems/mspp.h"
#include "JaguarCore/problems/spp.h"

#include <string>

namespace jaguar
{
struct ImporterConfig
{
    CDEConfig cdeConfig{};
    ShapeModifyConfig shapeModifyConfig{};
    double minItemSeparation = 0.0;
};

class Importer
{
public:
    explicit JAGUARCORE_API Importer(ImporterConfig config = {});

    JAGUARCORE_API Result<Item> importItem(const ExternalItem& external,
                                            ItemIndex internalIndex) const;
    JAGUARCORE_API Result<Container> importContainer(
        const ExternalContainer& external) const;

    JAGUARCORE_API Result<Item> importItemJson(const std::string& json,
                                               ItemIndex internalIndex) const;
    JAGUARCORE_API Result<Container> importContainerJson(
        const std::string& json) const;

    JAGUARCORE_API Result<SPInstance> importSPInstance(
        const ExternalSPInstance& external) const;
    JAGUARCORE_API Result<SPSolution> importSPSolution(
        const SPInstance& instance, const ExternalSPSolution& external) const;
    JAGUARCORE_API Result<SPInstance> importSPInstanceJson(
        const std::string& json) const;
    JAGUARCORE_API Result<SPSolution> importSPSolutionJson(
        const SPInstance& instance, const std::string& json) const;

    JAGUARCORE_API Result<BPInstance> importBPInstance(
        const ExternalBPInstance& external) const;
    JAGUARCORE_API Result<BPSolution> importBPSolution(
        const BPInstance& instance, const ExternalBPSolution& external) const;
    JAGUARCORE_API Result<BPInstance> importBPInstanceJson(
        const std::string& json) const;
    JAGUARCORE_API Result<BPSolution> importBPSolutionJson(
        const BPInstance& instance, const std::string& json) const;

    JAGUARCORE_API Result<MSPInstance> importMSPInstance(
        const ExternalMSPInstance& external) const;
    JAGUARCORE_API Result<MSPSolution> importMSPSolution(
        const MSPInstance& instance, const ExternalMSPSolution& external) const;
    JAGUARCORE_API Result<MSPInstance> importMSPInstanceJson(
        const std::string& json) const;
    JAGUARCORE_API Result<MSPSolution> importMSPSolutionJson(
        const MSPInstance& instance, const std::string& json) const;

private:
    Result<SimplePolygon> importItemShape(const ExternalShape& shape) const;
    Result<SimplePolygon> importContainerOuter(const ExternalShape& shape) const;
    Result<SimplePolygon> importZoneShape(const ExternalShape& shape) const;
    Result<RotationRange> importRotation(const RotationPolicy& rotation) const;
    Result<SimplePolygon> importSimplePolygon(const std::vector<Point2d>& points) const;
    ShapeModifyConfig shapeConfig() const;

    ImporterConfig m_Config;
};
}
