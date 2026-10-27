#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"
#include "JaguarCore/Assertions.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
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
    options.includeAxes = false;
    const auto svg = jaguar::ExportLayoutSvg(layout, options);
    assert(svg.HasValue());
    assert(svg.Value().find("<svg") != std::string::npos);
    assert(svg.Value().find("viewBox=") != std::string::npos);
    assert(svg.Value().find("<defs>") != std::string::npos);
    assert(svg.Value().find("<g id=\"item_9001\">") != std::string::npos);
    assert(svg.Value().find("href=\"#item_9001\"") != std::string::npos);
    const auto assertRustViewBox = [](const std::string& svgText,
                                      double expectedX,
                                      double expectedY,
                                      double expectedWidth,
                                      double expectedHeight) {
        const std::size_t attributeStart = svgText.find("viewBox=\"");
        assert(attributeStart != std::string::npos);
        const std::size_t valueStart = attributeStart + std::string("viewBox=\"").size();
        const std::size_t valueEnd = svgText.find('\"', valueStart);
        assert(valueEnd != std::string::npos);
        std::istringstream values(svgText.substr(valueStart, valueEnd - valueStart));
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        assert(static_cast<bool>(values >> x >> y >> width >> height));
        assert(std::abs(x - expectedX) < 1.0e-4);
        assert(std::abs(y - expectedY) < 1.0e-4);
        assert(std::abs(width - expectedWidth) < 1.0e-4);
        assert(std::abs(height - expectedHeight) < 1.0e-4);
    };
    assertRustViewBox(svg.Value(), -6.1, -6.1, 112.2, 112.2);
    assert(svg.Value().find("<g id=\"scene\">") != std::string::npos);
    assert(svg.Value().find("scale(1 -1)") == std::string::npos);
    assert(svg.Value().find("container-original") != std::string::npos);
    assert(svg.Value().find("container-collision") != std::string::npos);
    assert(svg.Value().find("item-original") != std::string::npos);
    assert(svg.Value().find("item-collision") != std::string::npos);
    assert(svg.Value().find("data-item-id=\"9001\"") != std::string::npos);
    assert(svg.Value().find("data-rotation-degrees=") != std::string::npos);
    assert(svg.Value().find("data-translation-x=\"53.535534\"") !=
           std::string::npos);
    assert(svg.Value().find("53.535534137747391") == std::string::npos);
    assert(svg.Value().find("translate(") != std::string::npos);
    assert(svg.Value().find("rotate(") != std::string::npos);
    assert(svg.Value().find("surrogate") != std::string::npos);
    const std::size_t itemOriginalStart =
        svg.Value().find("class=\"item-original\"");
    assert(itemOriginalStart != std::string::npos);
    const std::size_t itemOriginalEnd = svg.Value().find("/>", itemOriginalStart);
    assert(itemOriginalEnd != std::string::npos);
    assert(svg.Value().substr(itemOriginalStart,
                              itemOriginalEnd - itemOriginalStart)
               .find("stroke-width=\"0.20400001") != std::string::npos);
    const std::string itemOriginal = svg.Value().substr(
        itemOriginalStart, itemOriginalEnd - itemOriginalStart);
    assert(itemOriginal.find("fill-opacity=\"0.5\"") != std::string::npos);
    assert(itemOriginal.find("stroke=\"black\"") != std::string::npos);
    const std::size_t containerOriginalStart =
        svg.Value().find("class=\"container-original\"");
    assert(containerOriginalStart != std::string::npos);
    const std::size_t containerOriginalEnd =
        svg.Value().find("/>", containerOriginalStart);
    assert(containerOriginalEnd != std::string::npos);
    assert(svg.Value().substr(containerOriginalStart,
                              containerOriginalEnd - containerOriginalStart)
               .find("stroke-width=\"0.40800002") != std::string::npos);
    const std::string containerOriginal = svg.Value().substr(
        containerOriginalStart, containerOriginalEnd - containerOriginalStart);
    assert(containerOriginal.find("fill-opacity=") == std::string::npos);
    assert(containerOriginal.find("stroke=\"black\"") != std::string::npos);
    const std::size_t containerCollisionStart =
        svg.Value().find("class=\"container-collision\"");
    assert(containerCollisionStart != std::string::npos);
    const std::size_t containerCollisionEnd =
        svg.Value().find("/>", containerCollisionStart);
    assert(containerCollisionEnd != std::string::npos);
    const std::string containerCollision = svg.Value().substr(
        containerCollisionStart,
        containerCollisionEnd - containerCollisionStart);
    assert(containerCollision.find("stroke=\"black\"") != std::string::npos);
    assert(containerCollision.find("stroke-opacity=\"0.3\"") !=
           std::string::npos);
    assert(containerCollision.find("stroke-width=\"0.102000006\"") !=
           std::string::npos);
    assert(containerCollision.find(
               "stroke-dasharray=\"0.20400001 0.40800002\"") !=
           std::string::npos);
    assert(svg.Value().find(
               "h: 100.000 | w: 100.000 | d: 2.000% | JaguarCore Layout") !=
           std::string::npos);

    jaguar::ExternalContainer nonSquareContainerInput{};
    nonSquareContainerInput.id = 9101;
    nonSquareContainerInput.shape =
        jaguar::ExternalShape::Rectangle(0.0, 0.0, 100.0, 50.0);
    const auto nonSquareContainer = importer.ImportContainer(nonSquareContainerInput);
    assert(nonSquareContainer.HasValue());
    const jaguar::Layout nonSquareLayout(nonSquareContainer.Value());
    const auto nonSquareSvg = jaguar::ExportLayoutSvg(nonSquareLayout);
    assert(nonSquareSvg.HasValue());
    assertRustViewBox(nonSquareSvg.Value(), -5.55, -3.05, 111.1, 56.1);
    const std::size_t nonSquareContainerStart =
        nonSquareSvg.Value().find("class=\"container-original\"");
    assert(nonSquareContainerStart != std::string::npos);
    const std::size_t nonSquareContainerEnd =
        nonSquareSvg.Value().find("/>", nonSquareContainerStart);
    assert(nonSquareContainerEnd != std::string::npos);
    assert(nonSquareSvg.Value()
               .substr(nonSquareContainerStart,
                       nonSquareContainerEnd - nonSquareContainerStart)
               .find("stroke-width=\"0.20400001") != std::string::npos);

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
    const auto countOccurrences = [](const std::string& value,
                                     const std::string& needle) {
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = value.find(needle, position)) != std::string::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    };
    assert(countOccurrences(collisionSvg.Value(), "<g id=\"item_9001\">") == 1);
    assert(countOccurrences(collisionSvg.Value(), "href=\"#item_9001\"") == 2);
    assert(collisionSvg.Value().find("class=\"collision-highlight\"") !=
           std::string::npos);
    const bool collisionLineForward =
        collisionSvg.Value().find("d=\"M 45 50 L 53 50\"") !=
        std::string::npos;
    const bool collisionLineReverse =
        collisionSvg.Value().find("d=\"M 53 50 L 45 50\"") !=
        std::string::npos;
    assert(collisionLineForward || collisionLineReverse);

    jaguar::SvgDebugOptions debugOptions{};
    debugOptions.drawQuadtree = true;
    debugOptions.drawCdShapes = true;
    debugOptions.highlightCdShapes = false;
    const auto debugSvg = jaguar::ExportLayoutSvg(
        layout, options, debugOptions);
    assert(debugSvg.HasValue());
    assert(debugSvg.Value().find("id=\"quadtree\"") != std::string::npos);
    assert(debugSvg.Value().find("class=\"quadtree-cell") != std::string::npos);
    assert(debugSvg.Value().find("class=\"container-cd-display\"") !=
           std::string::npos);
    assert(debugSvg.Value().find("class=\"item-cd-display\"") !=
           std::string::npos);
    assert(debugSvg.Value().find("class=\"container-cd-highlight\"") ==
           std::string::npos);
    assert(debugSvg.Value().find("class=\"item-cd-highlight\"") ==
           std::string::npos);

    debugOptions.highlightCdShapes = true;
    const auto highlightedDebugSvg = jaguar::ExportLayoutSvg(
        layout, options, debugOptions);
    assert(highlightedDebugSvg.HasValue());
    assert(highlightedDebugSvg.Value().find(
               "class=\"item-cd-vertex\"") != std::string::npos);
    assert(highlightedDebugSvg.Value().find(
               "fill=\"cyan\" fill-opacity=\"0.8\"") != std::string::npos);

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
    assert(qualitySvg.Value().find("id=\"quality_zones\"") !=
           std::string::npos);
    assert(qualitySvg.Value().find(
               "class=\"quality-zone\" data-quality=\"1\"") !=
           std::string::npos);
    assert(qualitySvg.Value().find(
               "fill=\"#FF0000\" fill-opacity=\"0.5\"") !=
           std::string::npos);
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

    jaguar::ImporterConfig zoneOffsetConfig{};
    zoneOffsetConfig.minItemSeparation = 2.0;
    jaguar::Importer zoneOffsetImporter(zoneOffsetConfig);
    const auto offsetQualityContainer =
        zoneOffsetImporter.ImportContainer(qualityContainerInput);
    assert(offsetQualityContainer.HasValue());
    const auto directQualityContainer = jaguar::Container::Create(
        offsetQualityContainer.Value().Id(),
        offsetQualityContainer.Value().GetOriginalShape(),
        offsetQualityContainer.Value().QualityZones(), jaguar::CDEConfig{});
    assert(directQualityContainer.HasValue());
    assert(&directQualityContainer.Value().QualityZones() ==
           &directQualityContainer.Value().OriginalQualityZones());
    jaguar::Layout offsetQualityLayout(offsetQualityContainer.Value());
    jaguar::SvgDebugOptions drawCollisionZones{};
    drawCollisionZones.drawCdShapes = true;
    drawCollisionZones.highlightCdShapes = false;
    const auto originalZoneSvg = jaguar::ExportLayoutSvg(
        offsetQualityLayout, jaguar::SvgExportOptions{},
        jaguar::SvgDebugOptions{});
    const auto collisionZoneSvg = jaguar::ExportLayoutSvg(
        offsetQualityLayout, jaguar::SvgExportOptions{}, drawCollisionZones);
    assert(originalZoneSvg.HasValue());
    assert(collisionZoneSvg.HasValue());
    const auto qualityZonePath = [](const std::string& value) {
        const std::size_t zone = value.find("class=\"quality-zone\"");
        assert(zone != std::string::npos);
        const std::size_t pathStart = value.find("d=\"", zone);
        assert(pathStart != std::string::npos);
        const std::size_t pathValueStart = pathStart + 3U;
        const std::size_t pathEnd = value.find('"', pathValueStart);
        assert(pathEnd != std::string::npos);
        return value.substr(pathValueStart, pathEnd - pathValueStart);
    };
    assert(qualityZonePath(originalZoneSvg.Value()) !=
           qualityZonePath(collisionZoneSvg.Value()));

    jaguar::SvgExportOptions zeroStrokeWidthOptions{};
    zeroStrokeWidthOptions.theme.strokeWidthMultiplier = 0.0;
    const auto zeroStrokeWidthSvg =
        jaguar::ExportLayoutSvg(layout, zeroStrokeWidthOptions);
    assert(zeroStrokeWidthSvg.HasValue());
    assert(zeroStrokeWidthSvg.Value().find("stroke-width=\"0\"") !=
           std::string::npos);

    jaguar::SvgExportOptions negativeStrokeWidthOptions{};
    negativeStrokeWidthOptions.theme.strokeWidthMultiplier = -1.25;
    const auto negativeStrokeWidthSvg =
        jaguar::ExportLayoutSvg(layout, negativeStrokeWidthOptions);
    assert(negativeStrokeWidthSvg.HasValue());
    assert(negativeStrokeWidthSvg.Value().find("stroke-width=\"-0.12750001\"") !=
           std::string::npos);

    jaguar::SvgExportOptions invalidOptions{};
    invalidOptions.theme.strokeWidthMultiplier = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = jaguar::ExportLayoutSvg(layout, invalidOptions);
    assert(!invalid.HasValue());
    assert(invalid.GetError().code == jaguar::ErrorCode::SerializationFailure);
    assert(jaguar::ValidateLayoutSnapshot(layout, before).HasValue());
}
