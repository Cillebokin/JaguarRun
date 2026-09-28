#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"
#include "JaguarCore/Assertions.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>

void runSvgExportTests()
{
    jaguar::Importer importer;
    jaguar::ExternalItem itemInput{};
    itemInput.id = 9001;
    itemInput.shape = jaguar::ExternalShape::Rectangle(0.0, 0.0, 10.0, 20.0);
    itemInput.rotation = jaguar::RotationPolicy::None();
    const auto item = importer.ImportItem(itemInput, 0);
    assert(item.HasValue());

    jaguar::ExternalContainer containerInput{};
    containerInput.id = 9100;
    containerInput.shape = jaguar::ExternalShape::Rectangle(0.0, 0.0, 100.0, 100.0);
    const auto container = importer.ImportContainer(containerInput);
    assert(container.HasValue());

    jaguar::Layout layout(container.Value());
    const auto placed = layout.PlaceItem(
        item.Value(),
        jaguar::Transform2d::FromRotationTranslation(
            0.25 * std::acos(-1.0), {50.0, 50.0}));
    assert(placed.HasValue());
    const auto before = layout.Save();

    const auto externalLayout = jaguar::ExternalLayoutFromLayout(layout);
    assert(externalLayout.HasValue());
    assert(externalLayout.Value().containerId == 9100);
    assert(externalLayout.Value().placedItems.size() == 1);
    assert(externalLayout.Value().placedItems.front().itemId == 9001);
    assert(std::abs(externalLayout.Value().density - layout.Density()) < 1.0e-12);
    const auto externalLayoutJson =
        jaguar::SerializeExternalLayoutJson(externalLayout.Value());
    assert(externalLayoutJson.HasValue());
    assert(jaguar::ParseExternalLayoutJson(externalLayoutJson.Value()).HasValue());

    jaguar::SvgExportOptions options{};
    options.drawSurrogates = true;
    options.highlightCollisions = true;
    const auto svg = jaguar::ExportLayoutSvg(layout, options);
    assert(svg.HasValue());
    assert(svg.Value().find("<svg") != std::string::npos);
    assert(svg.Value().find("viewBox=") != std::string::npos);
    assert(svg.Value().find("scale(1 -1)") != std::string::npos);
    assert(svg.Value().find("container-original") != std::string::npos);
    assert(svg.Value().find("container-collision") != std::string::npos);
    assert(svg.Value().find("item-original") != std::string::npos);
    assert(svg.Value().find("item-collision") != std::string::npos);
    assert(svg.Value().find("data-item-id=\"9001\"") != std::string::npos);
    assert(svg.Value().find("data-rotation-degrees=") != std::string::npos);
    assert(svg.Value().find("translate(") != std::string::npos);
    assert(svg.Value().find("rotate(") != std::string::npos);
    assert(svg.Value().find("surrogate") != std::string::npos);
    std::ofstream svgFile("JaguarCoreSvgTest.svg", std::ios::binary);
    assert(svgFile);
    svgFile << svg.Value();
    svgFile.close();
    assert(jaguar::ValidateLayoutSnapshot(layout, before).HasValue());

    jaguar::Layout collisionLayout(container.Value());
    const auto collisionFirst = collisionLayout.PlaceItem(
        item.Value(), jaguar::Transform2d::FromRotationTranslation(
                          0.0, {45.0, 50.0}));
    const auto collisionSecond = collisionLayout.PlaceItem(
        item.Value(), jaguar::Transform2d::FromRotationTranslation(
                          0.0, {53.0, 50.0}));
    assert(collisionFirst.HasValue());
    assert(collisionSecond.HasValue());
    assert(!collisionLayout.IsFeasible());

    const auto collisionSvg = jaguar::ExportLayoutSvg(collisionLayout);
    assert(collisionSvg.HasValue());
    assert(collisionSvg.Value().find("class=\"collision-highlight\"") !=
           std::string::npos);
    const bool collisionLineForward =
        collisionSvg.Value().find("d=\"M 45 50 L 53 50\"") !=
        std::string::npos;
    const bool collisionLineReverse =
        collisionSvg.Value().find("d=\"M 53 50 L 45 50\"") !=
        std::string::npos;
    assert(collisionLineForward || collisionLineReverse);

    jaguar::SvgExportOptions noCollisionHighlights{};
    noCollisionHighlights.highlightCollisions = false;
    const auto noCollisionHighlightSvg =
        jaguar::ExportLayoutSvg(collisionLayout, noCollisionHighlights);
    assert(noCollisionHighlightSvg.HasValue());
    assert(noCollisionHighlightSvg.Value().find(
               "class=\"collision-highlight\"") == std::string::npos);

    jaguar::ExternalContainer qualityContainerInput{};
    qualityContainerInput.id = 9200;
    qualityContainerInput.shape = jaguar::ExternalShape::Rectangle(
        0.0, 0.0, 100.0, 100.0);
    qualityContainerInput.zones.push_back(
        {1, jaguar::ExternalShape::Rectangle(60.0, 60.0, 20.0, 20.0)});
    const auto qualityContainer = importer.ImportContainer(qualityContainerInput);
    assert(qualityContainer.HasValue());

    jaguar::ExternalItem qualityItemInput = itemInput;
    qualityItemInput.minQuality = 2;
    const auto qualityItem = importer.ImportItem(qualityItemInput, 1);
    assert(qualityItem.HasValue());
    jaguar::Layout qualityLayout(qualityContainer.Value());
    const auto qualityPlaced = qualityLayout.PlaceItem(
        qualityItem.Value(), jaguar::Transform2d::FromRotationTranslation(
                                 0.0, {72.0, 70.0}));
    assert(qualityPlaced.HasValue());
    const auto qualitySvg = jaguar::ExportLayoutSvg(qualityLayout);
    assert(qualitySvg.HasValue());
    assert(qualitySvg.Value().find("d=\"M 72 70 L 70 70\"") !=
               std::string::npos ||
           qualitySvg.Value().find("d=\"M 70 70 L 72 70\"") !=
               std::string::npos);

    qualityItemInput.minQuality = 1;
    const auto acceptedQualityItem = importer.ImportItem(qualityItemInput, 2);
    assert(acceptedQualityItem.HasValue());
    jaguar::Layout acceptedQualityLayout(qualityContainer.Value());
    const auto acceptedQualityPlaced = acceptedQualityLayout.PlaceItem(
        acceptedQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {72.0, 70.0}));
    assert(acceptedQualityPlaced.HasValue());
    const auto acceptedQualitySvg =
        jaguar::ExportLayoutSvg(acceptedQualityLayout);
    assert(acceptedQualitySvg.HasValue());
    assert(acceptedQualitySvg.Value().find(
               "class=\"collision-highlight\"") == std::string::npos);

    jaguar::SvgExportOptions invalidOptions{};
    invalidOptions.theme.strokeWidthMultiplier = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = jaguar::ExportLayoutSvg(layout, invalidOptions);
    assert(!invalid.HasValue());
    assert(invalid.GetError().code == jaguar::ErrorCode::SerializationFailure);
    assert(jaguar::ValidateLayoutSnapshot(layout, before).HasValue());
}
