#pragma once

#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/IO/ExternalRepr.h"
#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"

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

    JAGUARCORE_API Result<Item> ImportItem(const ExternalItem& external,
                                            ItemIndex internalIndex) const;
    JAGUARCORE_API Result<Container> ImportContainer(
        const ExternalContainer& external) const;

    JAGUARCORE_API Result<Item> ImportItemJson(const std::string& json,
                                               ItemIndex internalIndex) const;
    JAGUARCORE_API Result<Container> ImportContainerJson(
        const std::string& json) const;

    JAGUARCORE_API Result<SPInstance> ImportSPInstance(
        const ExternalSPInstance& external) const;
    JAGUARCORE_API Result<SPSolution> ImportSPSolution(
        const SPInstance& instance, const ExternalSPSolution& external) const;
    JAGUARCORE_API Result<SPInstance> ImportSPInstanceJson(
        const std::string& json) const;
    JAGUARCORE_API Result<SPSolution> ImportSPSolutionJson(
        const SPInstance& instance, const std::string& json) const;

    JAGUARCORE_API Result<BPInstance> ImportBPInstance(
        const ExternalBPInstance& external) const;
    JAGUARCORE_API Result<BPSolution> ImportBPSolution(
        const BPInstance& instance, const ExternalBPSolution& external) const;
    JAGUARCORE_API Result<BPInstance> ImportBPInstanceJson(
        const std::string& json) const;
    JAGUARCORE_API Result<BPSolution> ImportBPSolutionJson(
        const BPInstance& instance, const std::string& json) const;

    JAGUARCORE_API Result<MSPInstance> ImportMSPInstance(
        const ExternalMSPInstance& external) const;
    JAGUARCORE_API Result<MSPSolution> ImportMSPSolution(
        const MSPInstance& instance, const ExternalMSPSolution& external) const;
    JAGUARCORE_API Result<MSPInstance> ImportMSPInstanceJson(
        const std::string& json) const;
    JAGUARCORE_API Result<MSPSolution> ImportMSPSolutionJson(
        const MSPInstance& instance, const std::string& json) const;

private:
    Result<SimplePolygon> ImportItemShape(const ExternalShape& shape) const;
    Result<SimplePolygon> ImportContainerOuter(const ExternalShape& shape) const;
    Result<SimplePolygon> ImportZoneShape(const ExternalShape& shape) const;
    Result<RotationRange> ImportRotation(const RotationPolicy& rotation) const;
    Result<SimplePolygon> ImportSimplePolygon(const std::vector<Point2d>& points) const;
    ShapeModifyConfig ShapeConfig() const;

    ImporterConfig m_Config;
};
}
