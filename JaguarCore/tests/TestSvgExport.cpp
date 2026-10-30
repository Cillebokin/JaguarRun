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
    assert(svg.Value().find("<g id=\"container\">") != std::string::npos);
    assert(svg.Value().find(
               "d=\"M0,0 L100,0 L100,100 L0,100 z\"") !=
           std::string::npos);
    assert(svg.Value().find(
               "<title>container, bbox: [x_min: 0.000, y_min: 0.000, x_max: 100.000, y_max: 100.000]</title>") !=
           std::string::npos);
    assert(svg.Value().find(
               "<title>item, id: 9001, transf: [r: 45.000°, t: (53.536, 39.393)]</title>") !=
           std::string::npos);
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

    jaguar::ImporterConfig surrogateImporterConfig{};
    surrogateImporterConfig.cdeConfig.itemSurrogateConfig.poleLimits =
        {{{8, 0.0}, {4, 0.75}, {2, 0.9}}};
    surrogateImporterConfig.cdeConfig.itemSurrogateConfig.failFastPoleAreaRatio =
        0.5;
    surrogateImporterConfig.cdeConfig.itemSurrogateConfig.failFastPiers = 2;
    jaguar::Importer surrogateImporter(surrogateImporterConfig);
    jaguar::ExternalItem surrogateItemInput{};
    surrogateItemInput.id = 9002;
    surrogateItemInput.shape = jaguar::ExternalShape::SimplePolygon({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 4.0}, {4.0, 4.0}, {4.0, 10.0}, {0.0, 10.0}});
    const auto surrogateItem = surrogateImporter.ImportItem(surrogateItemInput, 1);
    assert(surrogateItem.HasValue());
    const jaguar::Surrogate* itemSurrogate =
        surrogateItem.Value().CollisionShape().GetSurrogate();
    assert(itemSurrogate != nullptr);
    assert(itemSurrogate->Poles().size() == 7);
    assert(itemSurrogate->FailFastPoleCount() == 3);

    jaguar::Layout surrogateLayout(container.Value());
    assert(surrogateLayout.PlaceItem(
        surrogateItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}))
               .HasValue());
    jaguar::SvgExportOptions surrogateOptions{};
    surrogateOptions.drawSurrogates = true;
    const auto surrogateSvg =
        jaguar::ExportLayoutSvg(surrogateLayout, surrogateOptions);
    assert(surrogateSvg.HasValue());
    assert(surrogateSvg.Value().find(
               "class=\"surrogate-pole\" data-pole-role=\"poi\"") !=
           std::string::npos);
    const auto assertSurrogatePoleStyle = [&surrogateSvg](
                                              const char* poleRole,
                                              const char* expectedStyle) {
        const std::string roleAttribute =
            std::string("data-pole-role=\"") + poleRole + "\"";
        const std::size_t start = surrogateSvg.Value().find(roleAttribute);
        assert(start != std::string::npos);
        const std::size_t end = surrogateSvg.Value().find("/>", start);
        assert(end != std::string::npos);
        assert(surrogateSvg.Value().substr(start, end - start).find(
                   expectedStyle) != std::string::npos);
    };
    assertSurrogatePoleStyle(
        "poi",
        "fill=\"black\" fill-opacity=\"0.1\" stroke=\"#00AA55\"");
    assertSurrogatePoleStyle(
        "fail-fast",
        "fill=\"none\" stroke=\"#00AA55\"");
    assertSurrogatePoleStyle("poi", "stroke-opacity=\"0.8\"");
    assertSurrogatePoleStyle(
        "fail-fast", "stroke-opacity=\"0.8\"");
    assertSurrogatePoleStyle(
        "ordinary",
        "stroke-opacity=\"0.5\" stroke-dasharray=\"");
    assert(surrogateSvg.Value().find(
               "stroke-linecap=\"round\" stroke-linejoin=\"round\"") !=
           std::string::npos);
    const auto countSurrogateRole = [&surrogateSvg](const char* poleRole) {
        const std::string roleAttribute =
            std::string("data-pole-role=\"") + poleRole + "\"";
        std::size_t count = 0U;
        std::size_t position = 0U;
        while ((position = surrogateSvg.Value().find(roleAttribute, position)) !=
               std::string::npos)
        {
            ++count;
            position += roleAttribute.size();
        }
        return count;
    };
    assert(countSurrogateRole("poi") == 1U);
    assert(countSurrogateRole("fail-fast") == 2U);
    assert(countSurrogateRole("ordinary") == 4U);
    std::size_t surrogatePierCount = 0U;
    std::size_t surrogatePierPosition = 0U;
    while ((surrogatePierPosition = surrogateSvg.Value().find(
                "<path class=\"surrogate-pier\"",
                surrogatePierPosition)) != std::string::npos)
    {
        const std::size_t pierEnd =
            surrogateSvg.Value().find("/>", surrogatePierPosition);
        assert(pierEnd != std::string::npos);
        assert(surrogateSvg.Value()
                   .substr(surrogatePierPosition,
                           pierEnd - surrogatePierPosition)
                   .find("stroke-opacity=\"0.8\"") != std::string::npos);
        ++surrogatePierCount;
        surrogatePierPosition = pierEnd + 2U;
    }
    assert(surrogatePierCount == 2U);

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
    const auto assertCollisionLineStyles = [&countOccurrences](
                                              const std::string& svgText,
                                              std::size_t expectedCount) {
        assert(countOccurrences(svgText,
                                "<path class=\"collision-highlight\"") ==
               expectedCount);
        const std::string prefix = "<path class=\"collision-highlight\"";
        std::size_t position = 0;
        while ((position = svgText.find(prefix, position)) !=
               std::string::npos)
        {
            const std::size_t end = svgText.find("/>\n", position);
            assert(end != std::string::npos);
            const std::string element =
                svgText.substr(position, end - position);
            assert(element.find("stroke=\"#00FF00\"") !=
                   std::string::npos);
            assert(element.find("stroke-opacity=\"0.75\"") !=
                   std::string::npos);
            assert(element.find("stroke-width=\"0.81600004\"") !=
                   std::string::npos);
            assert(element.find(
                       "stroke-dasharray=\"0.81600004 1.6320001\"") !=
                   std::string::npos);
            assert(element.find("stroke-linecap=\"round\"") !=
                   std::string::npos);
            assert(element.find("stroke-linejoin=\"round\"") !=
                   std::string::npos);
            position = end + 3U;
        }
    };
    assert(countOccurrences(collisionSvg.Value(),
                            "<g id=\"collision_lines\">") == 1);
    assert(countOccurrences(collisionSvg.Value(), "<g id=\"item_9001\">") == 1);
    assert(countOccurrences(collisionSvg.Value(), "href=\"#item_9001\"") == 2);
    assert(countOccurrences(collisionSvg.Value(), "<g id=\"cd_shape_9001\">") == 1);
    assert(countOccurrences(collisionSvg.Value(), "href=\"#cd_shape_9001\"") == 2);
    assert(collisionSvg.Value().find("class=\"collision-highlight\"") !=
           std::string::npos);

    jaguar::SvgExportOptions repeatedSurrogateOptions{};
    repeatedSurrogateOptions.drawSurrogates = true;
    repeatedSurrogateOptions.highlightCollisions = false;
    const auto repeatedSurrogateSvg = jaguar::ExportLayoutSvg(
        collisionLayout, repeatedSurrogateOptions);
    assert(repeatedSurrogateSvg.HasValue());
    assert(countOccurrences(repeatedSurrogateSvg.Value(),
                            "<g id=\"surrogate_9001\">") == 1);
    assert(countOccurrences(repeatedSurrogateSvg.Value(),
                            "class=\"surrogate-pole\"") == 2);
    assert(countOccurrences(repeatedSurrogateSvg.Value(),
                            "href=\"#surrogate_9001\"") == 2);
    assert(countOccurrences(repeatedSurrogateSvg.Value(),
                            "class=\"surrogate\" data-item-id=\"9001\"") == 2);

    const bool collisionLineForward =
        collisionSvg.Value().find("d=\"M45,50 L53,50\"") !=
        std::string::npos;
    const bool collisionLineReverse =
        collisionSvg.Value().find("d=\"M53,50 L45,50\"") !=
        std::string::npos;
    assert(collisionLineForward || collisionLineReverse);
    assertCollisionLineStyles(collisionSvg.Value(), 1);

    jaguar::SvgDebugOptions debugOptions{};
    debugOptions.drawQuadtree = true;
    debugOptions.drawCdShapes = true;
    debugOptions.highlightCdShapes = false;
    const auto debugSvg = jaguar::ExportLayoutSvg(
        layout, options, debugOptions);
    assert(debugSvg.HasValue());
    assert(debugSvg.Value().find("id=\"quadtree\"") != std::string::npos);
    assert(debugSvg.Value().find("class=\"quadtree-cell") != std::string::npos);
    const auto assertQuadtreePath = [&debugSvg](const char* presence,
                                                bool hasFillRule) {
        const std::string className =
            std::string("class=\"quadtree-cell quadtree-") + presence + "\"";
        const std::size_t pathStart =
            debugSvg.Value().find(className);
        assert(pathStart != std::string::npos);
        const std::size_t pathEnd = debugSvg.Value().find("/>", pathStart);
        assert(pathEnd != std::string::npos);
        const std::string element = debugSvg.Value().substr(
            pathStart, pathEnd - pathStart);
        const bool hasNonzeroFillRule =
            element.find("fill-rule=\"nonzero\"") != std::string::npos;
        assert(hasNonzeroFillRule == hasFillRule);
        const std::size_t dataStart = element.find("d=\"");
        assert(dataStart != std::string::npos);
        const std::size_t valueStart = dataStart + 3U;
        const std::size_t valueEnd = element.find('"', valueStart);
        assert(valueEnd != std::string::npos);
        const std::string pathData =
            element.substr(valueStart, valueEnd - valueStart);
        assert(pathData.empty() || pathData.back() != ' ');
    };
    assertQuadtreePath("entire", true);
    assertQuadtreePath("partial", true);
    assertQuadtreePath("none", false);
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
               "<g id=\"collision_lines\">") == std::string::npos);
    assert(noCollisionHighlightSvg.Value().find(
               "class=\"collision-highlight\"") == std::string::npos);

    std::ofstream collisionSvgFile("JaguarCoreCollisionHighlightTest.svg",
                                   std::ios::binary);
    assert(collisionSvgFile);
    collisionSvgFile << collisionSvg.Value();
    collisionSvgFile.close();
    std::ofstream noCollisionSvgFile(
        "JaguarCoreCollisionHighlightDisabledTest.svg", std::ios::binary);
    assert(noCollisionSvgFile);
    noCollisionSvgFile << noCollisionHighlightSvg.Value();

    jaguar::Layout multiCollisionLayout(container.Value());
    const auto multiCollisionFirst = multiCollisionLayout.PlaceItem(
        item.Value(), jaguar::Transform2d::FromRotationTranslation(
                          0.0, {45.0, 50.0}));
    const auto multiCollisionSecond = multiCollisionLayout.PlaceItem(
        item.Value(), jaguar::Transform2d::FromRotationTranslation(
                          0.0, {49.0, 50.0}));
    const auto multiCollisionThird = multiCollisionLayout.PlaceItem(
        item.Value(), jaguar::Transform2d::FromRotationTranslation(
                          0.0, {53.0, 50.0}));
    assert(multiCollisionFirst.HasValue());
    assert(multiCollisionSecond.HasValue());
    assert(multiCollisionThird.HasValue());
    assert(!multiCollisionLayout.IsFeasible());
    const auto multiCollisionSvg =
        jaguar::ExportLayoutSvg(multiCollisionLayout);
    assert(multiCollisionSvg.HasValue());
    assertCollisionLineStyles(multiCollisionSvg.Value(), 3);
    const auto hasCollisionPair = [&multiCollisionSvg](const char* forward,
                                                       const char* reverse) {
        return multiCollisionSvg.Value().find(forward) != std::string::npos ||
               multiCollisionSvg.Value().find(reverse) != std::string::npos;
    };
    assert(hasCollisionPair("d=\"M49,50 L45,50\"",
                            "d=\"M45,50 L49,50\""));
    assert(hasCollisionPair("d=\"M53,50 L45,50\"",
                            "d=\"M45,50 L53,50\""));
    assert(hasCollisionPair("d=\"M53,50 L49,50\"",
                            "d=\"M49,50 L53,50\""));
    const auto multiCollisionDisabledSvg =
        jaguar::ExportLayoutSvg(multiCollisionLayout, noCollisionHighlights);
    assert(multiCollisionDisabledSvg.HasValue());
    assert(multiCollisionDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream multiCollisionSvgFile(
        "JaguarCoreMultiCollisionHighlightTest.svg", std::ios::binary);
    assert(multiCollisionSvgFile);
    multiCollisionSvgFile << multiCollisionSvg.Value();
    std::ofstream multiCollisionDisabledSvgFile(
        "JaguarCoreMultiCollisionHighlightDisabledTest.svg",
        std::ios::binary);
    assert(multiCollisionDisabledSvgFile);
    multiCollisionDisabledSvgFile << multiCollisionDisabledSvg.Value();

    jaguar::Layout exteriorCollisionLayout(container.Value());
    const auto exteriorCollisionPlaced = exteriorCollisionLayout.PlaceItem(
        item.Value(), jaguar::Transform2d::FromRotationTranslation(
                          0.0, {1.0, 50.0}));
    assert(exteriorCollisionPlaced.HasValue());
    assert(!exteriorCollisionLayout.IsFeasible());
    const auto exteriorCollisionSvg =
        jaguar::ExportLayoutSvg(exteriorCollisionLayout);
    assert(exteriorCollisionSvg.HasValue());
    assert(countOccurrences(exteriorCollisionSvg.Value(),
                            "<circle class=\"collision-highlight\"") == 1);
    const std::size_t exteriorCircleStart = exteriorCollisionSvg.Value().find(
        "<circle class=\"collision-highlight\"");
    const std::size_t exteriorCircleEnd = exteriorCollisionSvg.Value().find(
        "/>\n", exteriorCircleStart);
    assert(exteriorCircleEnd != std::string::npos);
    const std::string exteriorCircleElement =
        exteriorCollisionSvg.Value().substr(
            exteriorCircleStart, exteriorCircleEnd - exteriorCircleStart);
    assert(exteriorCircleElement.find("cx=\"1\"") != std::string::npos);
    assert(exteriorCircleElement.find("cy=\"50\"") != std::string::npos);
    assert(exteriorCircleElement.find("r=\"0.61200005\"") !=
           std::string::npos);
    assert(exteriorCircleElement.find("fill=\"#00FF00\"") !=
           std::string::npos);
    const auto exteriorCollisionDisabledSvg =
        jaguar::ExportLayoutSvg(exteriorCollisionLayout, noCollisionHighlights);
    assert(exteriorCollisionDisabledSvg.HasValue());
    assert(exteriorCollisionDisabledSvg.Value().find(
               "class=\"collision-highlight\"") == std::string::npos);
    std::ofstream exteriorCollisionSvgFile("JaguarCoreExteriorHighlightTest.svg",
                                           std::ios::binary);
    assert(exteriorCollisionSvgFile);
    exteriorCollisionSvgFile << exteriorCollisionSvg.Value();
    std::ofstream exteriorCollisionDisabledSvgFile(
        "JaguarCoreExteriorHighlightDisabledTest.svg", std::ios::binary);
    assert(exteriorCollisionDisabledSvgFile);
    exteriorCollisionDisabledSvgFile << exteriorCollisionDisabledSvg.Value();

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
    assert(!qualityLayout.IsFeasible());
    const auto qualitySvg = jaguar::ExportLayoutSvg(qualityLayout);
    assert(qualitySvg.HasValue());
    assertCollisionLineStyles(qualitySvg.Value(), 1);
    assert(qualitySvg.Value().find("fill=\"#FF933C\"") !=
           std::string::npos);
    assert(qualitySvg.Value().find("id=\"quality_zones\"") !=
           std::string::npos);
    assert(qualitySvg.Value().find(
               "class=\"quality-zone\" data-quality=\"1\"") !=
           std::string::npos);
    assert(qualitySvg.Value().find(
               "fill=\"#FF0000\" fill-opacity=\"0.50\"") !=
           std::string::npos);
    assert(qualitySvg.Value().find("d=\"M72,70 L70,70\"") !=
               std::string::npos ||
           qualitySvg.Value().find("d=\"M70,70 L72,70\"") !=
               std::string::npos);
    const auto qualityCollisionDisabledSvg =
        jaguar::ExportLayoutSvg(qualityLayout, noCollisionHighlights);
    assert(qualityCollisionDisabledSvg.HasValue());
    assert(qualityCollisionDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream qualityCollisionSvgFile(
        "JaguarCoreInferiorQualityZoneHighlightTest.svg", std::ios::binary);
    assert(qualityCollisionSvgFile);
    qualityCollisionSvgFile << qualitySvg.Value();
    std::ofstream qualityCollisionDisabledSvgFile(
        "JaguarCoreInferiorQualityZoneHighlightDisabledTest.svg",
        std::ios::binary);
    assert(qualityCollisionDisabledSvgFile);
    qualityCollisionDisabledSvgFile << qualityCollisionDisabledSvg.Value();

    jaguar::ExternalContainer holeContainerInput{};
    holeContainerInput.id = 9300;
    holeContainerInput.shape = jaguar::ExternalShape::Rectangle(
        0.0, 0.0, 100.0, 100.0);
    holeContainerInput.zones.push_back(
        {0, jaguar::ExternalShape::Rectangle(2.0, 2.0, 2.0, 2.0)});
    const auto holeContainer = importer.ImportContainer(holeContainerInput);
    assert(holeContainer.HasValue());
    const auto holeItem = importer.ImportItem(itemInput, 4);
    assert(holeItem.HasValue());
    jaguar::Layout holeLayout(holeContainer.Value());
    const auto holePlaced = holeLayout.PlaceItem(
        holeItem.Value(), jaguar::Transform2d::FromRotationTranslation(
                              0.0, {5.0, 5.0}));
    assert(holePlaced.HasValue());
    assert(!holeLayout.IsFeasible());
    const auto holeSvg = jaguar::ExportLayoutSvg(holeLayout);
    assert(holeSvg.HasValue());
    assertCollisionLineStyles(holeSvg.Value(), 1);
    assert(holeSvg.Value().find("d=\"M5,5 L3,3\"") != std::string::npos ||
           holeSvg.Value().find("d=\"M3,3 L5,5\"") != std::string::npos);
    const auto holeCollisionDisabledSvg =
        jaguar::ExportLayoutSvg(holeLayout, noCollisionHighlights);
    assert(holeCollisionDisabledSvg.HasValue());
    assert(holeCollisionDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream holeCollisionSvgFile("JaguarCoreHoleHighlightTest.svg",
                                      std::ios::binary);
    assert(holeCollisionSvgFile);
    holeCollisionSvgFile << holeSvg.Value();
    std::ofstream holeCollisionDisabledSvgFile(
        "JaguarCoreHoleHighlightDisabledTest.svg", std::ios::binary);
    assert(holeCollisionDisabledSvgFile);
    holeCollisionDisabledSvgFile << holeCollisionDisabledSvg.Value();

    jaguar::ExternalContainer multipleQualityZonesInput{};
    multipleQualityZonesInput.id = 9400;
    multipleQualityZonesInput.shape = jaguar::ExternalShape::Rectangle(
        0.0, 0.0, 100.0, 100.0);
    multipleQualityZonesInput.zones.push_back(
        {1, jaguar::ExternalShape::Rectangle(45.0, 45.0, 2.0, 2.0)});
    multipleQualityZonesInput.zones.push_back(
        {1, jaguar::ExternalShape::Rectangle(48.0, 45.0, 2.0, 2.0)});
    multipleQualityZonesInput.zones.push_back(
        {2, jaguar::ExternalShape::Rectangle(53.0, 52.0, 2.0, 2.0)});
    multipleQualityZonesInput.zones.push_back(
        {3, jaguar::ExternalShape::Rectangle(45.0, 52.0, 2.0, 2.0)});
    const auto multipleQualityZonesContainer =
        importer.ImportContainer(multipleQualityZonesInput);
    assert(multipleQualityZonesContainer.HasValue());
    jaguar::ExternalItem multipleQualityItemInput = itemInput;
    multipleQualityItemInput.minQuality = 3;
    const auto multipleQualityItem =
        importer.ImportItem(multipleQualityItemInput, 5);
    assert(multipleQualityItem.HasValue());
    jaguar::Layout multipleQualityLayout(multipleQualityZonesContainer.Value());
    const auto multipleQualityPlaced = multipleQualityLayout.PlaceItem(
        multipleQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}));
    assert(multipleQualityPlaced.HasValue());
    assert(!multipleQualityLayout.IsFeasible());
    const auto multipleQualitySvg =
        jaguar::ExportLayoutSvg(multipleQualityLayout);
    assert(multipleQualitySvg.HasValue());
    assertCollisionLineStyles(multipleQualitySvg.Value(), 3);
    const auto hasQualityCollisionLine = [&multipleQualitySvg](
                                             const char* forward,
                                             const char* reverse) {
        return multipleQualitySvg.Value().find(forward) != std::string::npos ||
               multipleQualitySvg.Value().find(reverse) != std::string::npos;
    };
    assert(hasQualityCollisionLine("d=\"M50,50 L46,46\"",
                                   "d=\"M46,46 L50,50\""));
    assert(hasQualityCollisionLine("d=\"M50,50 L49,46\"",
                                   "d=\"M49,46 L50,50\""));
    assert(hasQualityCollisionLine("d=\"M50,50 L54,53\"",
                                   "d=\"M54,53 L50,50\""));
    assert(multipleQualitySvg.Value().find("d=\"M50,50 L46,53\"") ==
           std::string::npos);
    const auto multipleQualityDisabledSvg = jaguar::ExportLayoutSvg(
        multipleQualityLayout, noCollisionHighlights);
    assert(multipleQualityDisabledSvg.HasValue());
    assert(multipleQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream multipleQualitySvgFile(
        "JaguarCoreMultipleQualityZonesHighlightTest.svg", std::ios::binary);
    assert(multipleQualitySvgFile);
    multipleQualitySvgFile << multipleQualitySvg.Value();
    std::ofstream multipleQualityDisabledSvgFile(
        "JaguarCoreMultipleQualityZonesHighlightDisabledTest.svg",
        std::ios::binary);
    assert(multipleQualityDisabledSvgFile);
    multipleQualityDisabledSvgFile << multipleQualityDisabledSvg.Value();

    jaguar::ExternalContainer qualityBoundaryInput{};
    qualityBoundaryInput.id = 9500;
    qualityBoundaryInput.shape = jaguar::ExternalShape::Rectangle(
        0.0, 0.0, 100.0, 100.0);
    qualityBoundaryInput.zones.push_back(
        {0, jaguar::ExternalShape::Rectangle(44.0, 48.0, 4.0, 4.0)});
    qualityBoundaryInput.zones.push_back(
        {1, jaguar::ExternalShape::Rectangle(47.0, 48.0, 4.0, 4.0)});
    qualityBoundaryInput.zones.push_back(
        {2, jaguar::ExternalShape::Rectangle(50.0, 48.0, 4.0, 4.0)});
    qualityBoundaryInput.zones.push_back(
        {3, jaguar::ExternalShape::Rectangle(48.0, 49.0, 4.0, 4.0)});
    const auto qualityBoundaryContainer =
        importer.ImportContainer(qualityBoundaryInput);
    assert(qualityBoundaryContainer.HasValue());
    const char* expectedBoundaryLines[][2] = {
        {"d=\"M50,50 L46,50\"", "d=\"M46,50 L50,50\""},
        {"d=\"M50,50 L49,50\"", "d=\"M49,50 L50,50\""},
        {"d=\"M50,50 L52,50\"", "d=\"M52,50 L50,50\""},
        {"d=\"M50,50 L50,51\"", "d=\"M50,51 L50,50\""}};
    const std::size_t expectedBoundaryCounts[] = {1, 1, 2, 3, 4};
    for (std::size_t minQuality = 0; minQuality <= 4; ++minQuality)
    {
        jaguar::ExternalItem boundaryItemInput = itemInput;
        boundaryItemInput.minQuality = minQuality;
        const auto boundaryItem = importer.ImportItem(
            boundaryItemInput, static_cast<jaguar::ItemIndex>(6 + minQuality));
        assert(boundaryItem.HasValue());
        jaguar::Layout boundaryLayout(qualityBoundaryContainer.Value());
        const auto boundaryPlaced = boundaryLayout.PlaceItem(
            boundaryItem.Value(),
            jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}));
        assert(boundaryPlaced.HasValue());
        assert(!boundaryLayout.IsFeasible());
        const auto boundarySvg = jaguar::ExportLayoutSvg(boundaryLayout);
        assert(boundarySvg.HasValue());
        const std::size_t expectedCount = expectedBoundaryCounts[minQuality];
        assertCollisionLineStyles(boundarySvg.Value(), expectedCount);
        for (std::size_t line = 0; line < expectedCount; ++line)
        {
            assert(boundarySvg.Value().find(expectedBoundaryLines[line][0]) !=
                       std::string::npos ||
                   boundarySvg.Value().find(expectedBoundaryLines[line][1]) !=
                       std::string::npos);
        }
        const auto boundaryDisabledSvg =
            jaguar::ExportLayoutSvg(boundaryLayout, noCollisionHighlights);
        assert(boundaryDisabledSvg.HasValue());
        assert(boundaryDisabledSvg.Value().find(
                   "id=\"collision_lines\"") == std::string::npos);
        const std::string stem = "JaguarCoreQualityBoundaryMin" +
                                 std::to_string(minQuality);
        std::ofstream boundarySvgFile(stem + "Enabled.svg", std::ios::binary);
        assert(boundarySvgFile);
        boundarySvgFile << boundarySvg.Value();
        std::ofstream boundaryDisabledSvgFile(
            stem + "Disabled.svg", std::ios::binary);
        assert(boundaryDisabledSvgFile);
        boundaryDisabledSvgFile << boundaryDisabledSvg.Value();
    }

    const auto makeQualityRectangle = [](double xMin,
                                         double yMin,
                                         double xMax,
                                         double yMax) {
        const auto shape = jaguar::SimplePolygon::Create(
            {{xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
        assert(shape.HasValue());
        return shape.Value();
    };
    std::vector<jaguar::QualityZone> maximumQualityZones{
        {7, {}},
        {8, {makeQualityRectangle(44.0, 44.0, 48.0, 48.0),
             makeQualityRectangle(47.0, 44.0, 51.0, 48.0)}},
        {9, {makeQualityRectangle(50.0, 48.0, 54.0, 52.0)}}};
    jaguar::OriginalShape maximumQualityOuter{
        makeQualityRectangle(0.0, 0.0, 100.0, 100.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto maximumQualityContainer = jaguar::Container::Create(
        9600, std::move(maximumQualityOuter), std::move(maximumQualityZones),
        jaguar::CDEConfig{});
    assert(maximumQualityContainer.HasValue());
    jaguar::ExternalItem maximumQualityItemInput = itemInput;
    maximumQualityItemInput.minQuality = 9;
    const auto maximumQualityItem =
        importer.ImportItem(maximumQualityItemInput, 11);
    assert(maximumQualityItem.HasValue());
    jaguar::Layout maximumQualityLayout(maximumQualityContainer.Value());
    const auto maximumQualityPlaced = maximumQualityLayout.PlaceItem(
        maximumQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}));
    assert(maximumQualityPlaced.HasValue());
    const auto maximumQualitySvg =
        jaguar::ExportLayoutSvg(maximumQualityLayout);
    assert(maximumQualitySvg.HasValue());
    assertCollisionLineStyles(maximumQualitySvg.Value(), 2);
    assert(maximumQualitySvg.Value().find("d=\"M50,50 L46,46\"") !=
               std::string::npos ||
           maximumQualitySvg.Value().find("d=\"M46,46 L50,50\"") !=
               std::string::npos);
    assert(maximumQualitySvg.Value().find("d=\"M50,50 L49,46\"") !=
               std::string::npos ||
           maximumQualitySvg.Value().find("d=\"M49,46 L50,50\"") !=
               std::string::npos);
    assert(maximumQualitySvg.Value().find("d=\"M50,50 L52,50\"") ==
           std::string::npos);
    assert(maximumQualitySvg.Value().find("data-quality=\"7\"") ==
           std::string::npos);
    assert(countOccurrences(maximumQualitySvg.Value(),
                            "data-quality=\"8\"") == 2);
    assert(countOccurrences(maximumQualitySvg.Value(),
                            "data-quality=\"9\"") == 1);
    const auto maximumQualityDisabledSvg = jaguar::ExportLayoutSvg(
        maximumQualityLayout, noCollisionHighlights);
    assert(maximumQualityDisabledSvg.HasValue());
    assert(maximumQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream maximumQualitySvgFile(
        "JaguarCoreQualityBoundaryMax9Enabled.svg", std::ios::binary);
    assert(maximumQualitySvgFile);
    maximumQualitySvgFile << maximumQualitySvg.Value();
    std::ofstream maximumQualityDisabledSvgFile(
        "JaguarCoreQualityBoundaryMax9Disabled.svg", std::ios::binary);
    assert(maximumQualityDisabledSvgFile);
    maximumQualityDisabledSvgFile << maximumQualityDisabledSvg.Value();

    const auto concaveQualityShape = jaguar::SimplePolygon::Create(
        {{40.0, 40.0}, {60.0, 40.0}, {60.0, 59.0},
         {59.0, 59.0}, {59.0, 60.0}, {40.0, 60.0}});
    assert(concaveQualityShape.HasValue());
    jaguar::OriginalShape concaveQualityOuter{
        makeQualityRectangle(0.0, 0.0, 100.0, 100.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto concaveQualityContainer = jaguar::Container::Create(
        9601, std::move(concaveQualityOuter),
        {{8, {concaveQualityShape.Value()}}}, jaguar::CDEConfig{});
    assert(concaveQualityContainer.HasValue());
    jaguar::Layout concaveQualityLayout(concaveQualityContainer.Value());
    const auto concaveQualityPlaced = concaveQualityLayout.PlaceItem(
        maximumQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {58.0, 50.0}));
    assert(concaveQualityPlaced.HasValue());
    const auto concaveQualitySvg =
        jaguar::ExportLayoutSvg(concaveQualityLayout);
    assert(concaveQualitySvg.HasValue());
    assertCollisionLineStyles(concaveQualitySvg.Value(), 1);
    assert(concaveQualitySvg.Value().find("d=\"M58,50 L50,50\"") !=
               std::string::npos ||
           concaveQualitySvg.Value().find("d=\"M50,50 L58,50\"") !=
               std::string::npos);
    assert(concaveQualitySvg.Value().find(
               "M40,40 L60,40 L60,59 L59,59 L59,60 L40,60 z") !=
           std::string::npos);
    const auto concaveQualityDisabledSvg = jaguar::ExportLayoutSvg(
        concaveQualityLayout, noCollisionHighlights);
    assert(concaveQualityDisabledSvg.HasValue());
    assert(concaveQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream concaveQualitySvgFile(
        "JaguarCoreConcaveQualityZoneCollisionEnabled.svg", std::ios::binary);
    assert(concaveQualitySvgFile);
    concaveQualitySvgFile << concaveQualitySvg.Value();
    std::ofstream concaveQualityDisabledSvgFile(
        "JaguarCoreConcaveQualityZoneCollisionDisabled.svg", std::ios::binary);
    assert(concaveQualityDisabledSvgFile);
    concaveQualityDisabledSvgFile << concaveQualityDisabledSvg.Value();

    const auto slantedConcaveQualityShape = jaguar::SimplePolygon::Create(
        {{40.0, 40.0}, {60.0, 40.0}, {60.0, 58.0},
         {58.0, 58.0}, {59.0, 60.0}, {40.0, 60.0}});
    assert(slantedConcaveQualityShape.HasValue());
    jaguar::OriginalShape slantedConcaveQualityOuter{
        makeQualityRectangle(0.0, 0.0, 100.0, 100.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto slantedConcaveQualityContainer = jaguar::Container::Create(
        9602, std::move(slantedConcaveQualityOuter),
        {{8, {slantedConcaveQualityShape.Value()}}}, jaguar::CDEConfig{});
    assert(slantedConcaveQualityContainer.HasValue());
    jaguar::Layout slantedConcaveQualityLayout(
        slantedConcaveQualityContainer.Value());
    const auto slantedConcaveQualityPlaced =
        slantedConcaveQualityLayout.PlaceItem(
            maximumQualityItem.Value(),
            jaguar::Transform2d::FromRotationTranslation(0.0, {58.0, 50.0}));
    assert(slantedConcaveQualityPlaced.HasValue());
    const auto slantedConcaveQualitySvg =
        jaguar::ExportLayoutSvg(slantedConcaveQualityLayout);
    assert(slantedConcaveQualitySvg.HasValue());
    assertCollisionLineStyles(slantedConcaveQualitySvg.Value(), 1);
    assert(slantedConcaveQualitySvg.Value().find("d=\"M58,50 L50,50\"") !=
               std::string::npos ||
           slantedConcaveQualitySvg.Value().find("d=\"M50,50 L58,50\"") !=
               std::string::npos);
    assert(slantedConcaveQualitySvg.Value().find(
               "M40,40 L60,40 L60,58 L58,58 L59,60 L40,60 z") !=
           std::string::npos);
    const auto slantedConcaveQualityDisabledSvg = jaguar::ExportLayoutSvg(
        slantedConcaveQualityLayout, noCollisionHighlights);
    assert(slantedConcaveQualityDisabledSvg.HasValue());
    assert(slantedConcaveQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream slantedConcaveQualitySvgFile(
        "JaguarCoreSlantedConcaveQualityZoneCollisionEnabled.svg",
        std::ios::binary);
    assert(slantedConcaveQualitySvgFile);
    slantedConcaveQualitySvgFile << slantedConcaveQualitySvg.Value();
    std::ofstream slantedConcaveQualityDisabledSvgFile(
        "JaguarCoreSlantedConcaveQualityZoneCollisionDisabled.svg",
        std::ios::binary);
    assert(slantedConcaveQualityDisabledSvgFile);
    slantedConcaveQualityDisabledSvgFile << slantedConcaveQualityDisabledSvg.Value();

    const auto indexedConcaveQualityShape = jaguar::SimplePolygon::Create(
        {{50.0, 40.0}, {70.0, 40.0}, {70.0, 58.0},
         {68.0, 58.0}, {69.0, 60.0}, {50.0, 60.0}});
    assert(indexedConcaveQualityShape.HasValue());
    jaguar::OriginalShape indexedQualityOuter{
        makeQualityRectangle(0.0, 0.0, 100.0, 100.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto indexedQualityContainer = jaguar::Container::Create(
        9603, std::move(indexedQualityOuter),
        {{8, {makeQualityRectangle(44.0, 44.0, 48.0, 48.0),
              indexedConcaveQualityShape.Value()}}},
        jaguar::CDEConfig{});
    assert(indexedQualityContainer.HasValue());
    jaguar::Layout indexedQualityLayout(indexedQualityContainer.Value());
    const auto indexedQualityPlaced = indexedQualityLayout.PlaceItem(
        maximumQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}));
    assert(indexedQualityPlaced.HasValue());
    const auto indexedQualitySvg = jaguar::ExportLayoutSvg(indexedQualityLayout);
    assert(indexedQualitySvg.HasValue());
    assertCollisionLineStyles(indexedQualitySvg.Value(), 2);
    assert(indexedQualitySvg.Value().find("d=\"M50,50 L46,46\"") !=
               std::string::npos ||
           indexedQualitySvg.Value().find("d=\"M46,46 L50,50\"") !=
               std::string::npos);
    assert(indexedQualitySvg.Value().find("d=\"M50,50 L60,50\"") !=
               std::string::npos ||
           indexedQualitySvg.Value().find("d=\"M60,50 L50,50\"") !=
               std::string::npos);
    assert(countOccurrences(indexedQualitySvg.Value(),
                            "data-quality=\"8\"") == 2);
    assert(indexedQualitySvg.Value().find(
               "M44,44 L48,44 L48,48 L44,48 z") != std::string::npos);
    assert(indexedQualitySvg.Value().find(
               "M50,40 L70,40 L70,58 L68,58 L69,60 L50,60 z") !=
           std::string::npos);
    const auto indexedQualityDisabledSvg = jaguar::ExportLayoutSvg(
        indexedQualityLayout, noCollisionHighlights);
    assert(indexedQualityDisabledSvg.HasValue());
    assert(indexedQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream indexedQualitySvgFile(
        "JaguarCoreIndexedQualityZoneShapesEnabled.svg", std::ios::binary);
    assert(indexedQualitySvgFile);
    indexedQualitySvgFile << indexedQualitySvg.Value();
    std::ofstream indexedQualityDisabledSvgFile(
        "JaguarCoreIndexedQualityZoneShapesDisabled.svg", std::ios::binary);
    assert(indexedQualityDisabledSvgFile);
    indexedQualityDisabledSvgFile << indexedQualityDisabledSvg.Value();

    const auto offsetConcaveQualityShape = jaguar::SimplePolygon::Create(
        {{50.0, 40.0}, {70.0, 40.0}, {70.0, 58.0},
         {68.0, 58.0}, {69.0, 60.0}, {50.0, 60.0}});
    assert(offsetConcaveQualityShape.HasValue());
    jaguar::OriginalShape offsetIndexedQualityOuter{
        makeQualityRectangle(0.0, 0.0, 100.0, 100.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto offsetIndexedQualityContainer = jaguar::Container::Create(
        9604, std::move(offsetIndexedQualityOuter),
        {{8, {makeQualityRectangle(5.0, 5.0, 10.0, 10.0),
              makeQualityRectangle(44.0, 44.0, 48.0, 48.0),
              offsetConcaveQualityShape.Value()}}},
        jaguar::CDEConfig{});
    assert(offsetIndexedQualityContainer.HasValue());
    jaguar::Layout offsetIndexedQualityLayout(
        offsetIndexedQualityContainer.Value());
    const auto offsetIndexedQualityPlaced =
        offsetIndexedQualityLayout.PlaceItem(
            maximumQualityItem.Value(),
            jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}));
    assert(offsetIndexedQualityPlaced.HasValue());
    const auto offsetIndexedQualitySvg =
        jaguar::ExportLayoutSvg(offsetIndexedQualityLayout);
    assert(offsetIndexedQualitySvg.HasValue());
    assertCollisionLineStyles(offsetIndexedQualitySvg.Value(), 2);
    assert(offsetIndexedQualitySvg.Value().find("d=\"M50,50 L46,46\"") !=
               std::string::npos ||
           offsetIndexedQualitySvg.Value().find("d=\"M46,46 L50,50\"") !=
               std::string::npos);
    assert(offsetIndexedQualitySvg.Value().find("d=\"M50,50 L60,50\"") !=
               std::string::npos ||
           offsetIndexedQualitySvg.Value().find("d=\"M60,50 L50,50\"") !=
               std::string::npos);
    assert(offsetIndexedQualitySvg.Value().find("M5,5 L10,5 L10,10 L5,10 z") !=
           std::string::npos);
    assert(offsetIndexedQualitySvg.Value().find(
               "M44,44 L48,44 L48,48 L44,48 z") != std::string::npos);
    assert(offsetIndexedQualitySvg.Value().find(
               "M50,40 L70,40 L70,58 L68,58 L69,60 L50,60 z") !=
           std::string::npos);
    assert(countOccurrences(offsetIndexedQualitySvg.Value(),
                            "data-quality=\"8\"") == 3);
    const auto offsetIndexedQualityDisabledSvg = jaguar::ExportLayoutSvg(
        offsetIndexedQualityLayout, noCollisionHighlights);
    assert(offsetIndexedQualityDisabledSvg.HasValue());
    assert(offsetIndexedQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream offsetIndexedQualitySvgFile(
        "JaguarCoreOffsetIndexedQualityZoneShapesEnabled.svg",
        std::ios::binary);
    assert(offsetIndexedQualitySvgFile);
    offsetIndexedQualitySvgFile << offsetIndexedQualitySvg.Value();
    std::ofstream offsetIndexedQualityDisabledSvgFile(
        "JaguarCoreOffsetIndexedQualityZoneShapesDisabled.svg",
        std::ios::binary);
    assert(offsetIndexedQualityDisabledSvgFile);
    offsetIndexedQualityDisabledSvgFile << offsetIndexedQualityDisabledSvg.Value();

    jaguar::OriginalShape middleIndexedQualityOuter{
        makeQualityRectangle(0.0, 0.0, 100.0, 100.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto middleIndexedQualityContainer = jaguar::Container::Create(
        9605, std::move(middleIndexedQualityOuter),
        {{8, {makeQualityRectangle(44.0, 44.0, 48.0, 48.0),
              makeQualityRectangle(5.0, 5.0, 10.0, 10.0),
              indexedConcaveQualityShape.Value()}}},
        jaguar::CDEConfig{});
    assert(middleIndexedQualityContainer.HasValue());
    jaguar::Layout middleIndexedQualityLayout(
        middleIndexedQualityContainer.Value());
    const auto middleIndexedQualityPlaced =
        middleIndexedQualityLayout.PlaceItem(
            maximumQualityItem.Value(),
            jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}));
    assert(middleIndexedQualityPlaced.HasValue());
    const auto middleIndexedQualitySvg =
        jaguar::ExportLayoutSvg(middleIndexedQualityLayout);
    assert(middleIndexedQualitySvg.HasValue());
    assertCollisionLineStyles(middleIndexedQualitySvg.Value(), 2);
    assert(middleIndexedQualitySvg.Value().find("d=\"M50,50 L46,46\"") !=
               std::string::npos ||
           middleIndexedQualitySvg.Value().find("d=\"M46,46 L50,50\"") !=
               std::string::npos);
    assert(middleIndexedQualitySvg.Value().find("d=\"M50,50 L60,50\"") !=
               std::string::npos ||
           middleIndexedQualitySvg.Value().find("d=\"M60,50 L50,50\"") !=
               std::string::npos);
    assert(middleIndexedQualitySvg.Value().find("M44,44 L48,44 L48,48 L44,48 z") !=
           std::string::npos);
    assert(middleIndexedQualitySvg.Value().find("M5,5 L10,5 L10,10 L5,10 z") !=
           std::string::npos);
    assert(middleIndexedQualitySvg.Value().find(
               "M50,40 L70,40 L70,58 L68,58 L69,60 L50,60 z") !=
           std::string::npos);
    assert(countOccurrences(middleIndexedQualitySvg.Value(),
                            "data-quality=\"8\"") == 3);
    const auto middleIndexedQualityDisabledSvg = jaguar::ExportLayoutSvg(
        middleIndexedQualityLayout, noCollisionHighlights);
    assert(middleIndexedQualityDisabledSvg.HasValue());
    assert(middleIndexedQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream middleIndexedQualitySvgFile(
        "JaguarCoreMiddleIndexedQualityZoneShapesEnabled.svg",
        std::ios::binary);
    assert(middleIndexedQualitySvgFile);
    middleIndexedQualitySvgFile << middleIndexedQualitySvg.Value();
    std::ofstream middleIndexedQualityDisabledSvgFile(
        "JaguarCoreMiddleIndexedQualityZoneShapesDisabled.svg",
        std::ios::binary);
    assert(middleIndexedQualityDisabledSvgFile);
    middleIndexedQualityDisabledSvgFile << middleIndexedQualityDisabledSvg.Value();

    jaguar::OriginalShape sparseIndexedQualityOuter{
        makeQualityRectangle(0.0, 0.0, 100.0, 100.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    const auto sparseIndexedQualityContainer = jaguar::Container::Create(
        9606, std::move(sparseIndexedQualityOuter),
        {{8, {makeQualityRectangle(5.0, 5.0, 10.0, 10.0),
              makeQualityRectangle(44.0, 44.0, 48.0, 48.0),
              makeQualityRectangle(80.0, 5.0, 85.0, 10.0),
              indexedConcaveQualityShape.Value(),
              makeQualityRectangle(5.0, 80.0, 10.0, 85.0)}}},
        jaguar::CDEConfig{});
    assert(sparseIndexedQualityContainer.HasValue());
    jaguar::Layout sparseIndexedQualityLayout(
        sparseIndexedQualityContainer.Value());
    const auto sparseIndexedQualityPlaced = sparseIndexedQualityLayout.PlaceItem(
        maximumQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {50.0, 50.0}));
    assert(sparseIndexedQualityPlaced.HasValue());
    const auto sparseIndexedQualitySvg =
        jaguar::ExportLayoutSvg(sparseIndexedQualityLayout);
    assert(sparseIndexedQualitySvg.HasValue());
    assertCollisionLineStyles(sparseIndexedQualitySvg.Value(), 2);
    assert(sparseIndexedQualitySvg.Value().find("d=\"M50,50 L46,46\"") !=
               std::string::npos ||
           sparseIndexedQualitySvg.Value().find("d=\"M46,46 L50,50\"") !=
               std::string::npos);
    assert(sparseIndexedQualitySvg.Value().find("d=\"M50,50 L60,50\"") !=
               std::string::npos ||
           sparseIndexedQualitySvg.Value().find("d=\"M60,50 L50,50\"") !=
               std::string::npos);
    assert(countOccurrences(sparseIndexedQualitySvg.Value(),
                            "data-quality=\"8\"") == 5);
    assert(sparseIndexedQualitySvg.Value().find("M5,5 L10,5 L10,10 L5,10 z") !=
           std::string::npos);
    assert(sparseIndexedQualitySvg.Value().find(
               "M44,44 L48,44 L48,48 L44,48 z") != std::string::npos);
    assert(sparseIndexedQualitySvg.Value().find("M80,5 L85,5 L85,10 L80,10 z") !=
           std::string::npos);
    assert(sparseIndexedQualitySvg.Value().find(
               "M50,40 L70,40 L70,58 L68,58 L69,60 L50,60 z") !=
           std::string::npos);
    assert(sparseIndexedQualitySvg.Value().find("M5,80 L10,80 L10,85 L5,85 z") !=
           std::string::npos);
    const auto sparseIndexedQualityDisabledSvg = jaguar::ExportLayoutSvg(
        sparseIndexedQualityLayout, noCollisionHighlights);
    assert(sparseIndexedQualityDisabledSvg.HasValue());
    assert(sparseIndexedQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream sparseIndexedQualitySvgFile(
        "JaguarCoreSparseIndexedQualityZoneShapesEnabled.svg",
        std::ios::binary);
    assert(sparseIndexedQualitySvgFile);
    sparseIndexedQualitySvgFile << sparseIndexedQualitySvg.Value();
    std::ofstream sparseIndexedQualityDisabledSvgFile(
        "JaguarCoreSparseIndexedQualityZoneShapesDisabled.svg",
        std::ios::binary);
    assert(sparseIndexedQualityDisabledSvgFile);
    sparseIndexedQualityDisabledSvgFile << sparseIndexedQualityDisabledSvg.Value();

    jaguar::ExternalContainer importedSimplePolygonQualityContainerInput{};
    importedSimplePolygonQualityContainerInput.id = 9608;
    importedSimplePolygonQualityContainerInput.shape =
        jaguar::ExternalShape::Rectangle(0.0, 0.0, 100.0, 100.0);
    importedSimplePolygonQualityContainerInput.zones.push_back(
        {8, jaguar::ExternalShape::SimplePolygon(
                {{40.0, 40.0}, {60.0, 40.0}, {60.0, 58.0},
                 {58.0, 58.0}, {59.0, 60.0}, {40.0, 60.0}})});
    const auto importedSimplePolygonQualityContainer =
        importer.ImportContainer(importedSimplePolygonQualityContainerInput);
    assert(importedSimplePolygonQualityContainer.HasValue());
    jaguar::Layout importedSimplePolygonQualityLayout(
        importedSimplePolygonQualityContainer.Value());
    const auto importedSimplePolygonQualityPlaced =
        importedSimplePolygonQualityLayout.PlaceItem(
            maximumQualityItem.Value(),
            jaguar::Transform2d::FromRotationTranslation(0.0, {58.0, 50.0}));
    assert(importedSimplePolygonQualityPlaced.HasValue());
    assert(!importedSimplePolygonQualityLayout.IsFeasible());
    const auto importedSimplePolygonQualitySvg =
        jaguar::ExportLayoutSvg(importedSimplePolygonQualityLayout);
    assert(importedSimplePolygonQualitySvg.HasValue());
    assertCollisionLineStyles(importedSimplePolygonQualitySvg.Value(), 1);
    assert(importedSimplePolygonQualitySvg.Value().find(
               "d=\"M58,50 L50,50\"") != std::string::npos ||
           importedSimplePolygonQualitySvg.Value().find(
               "d=\"M50,50 L58,50\"") != std::string::npos);
    assert(importedSimplePolygonQualitySvg.Value().find(
               "class=\"quality-zone\" data-quality=\"8\"") !=
           std::string::npos);
    assert(importedSimplePolygonQualitySvg.Value().find(
               "M40,40 L60,40 L60,58 L58,58 L59,60 L40,60 z") !=
           std::string::npos);
    const auto importedSimplePolygonQualityDisabledSvg =
        jaguar::ExportLayoutSvg(importedSimplePolygonQualityLayout,
                                noCollisionHighlights);
    assert(importedSimplePolygonQualityDisabledSvg.HasValue());
    assert(importedSimplePolygonQualityDisabledSvg.Value().find(
               "id=\"collision_lines\"") == std::string::npos);
    std::ofstream importedSimplePolygonQualitySvgFile(
        "JaguarCoreImportedSimplePolygonQualityZoneCollisionEnabled.svg",
        std::ios::binary);
    assert(importedSimplePolygonQualitySvgFile);
    importedSimplePolygonQualitySvgFile <<
        importedSimplePolygonQualitySvg.Value();
    std::ofstream importedSimplePolygonQualityDisabledSvgFile(
        "JaguarCoreImportedSimplePolygonQualityZoneCollisionDisabled.svg",
        std::ios::binary);
    assert(importedSimplePolygonQualityDisabledSvgFile);
    importedSimplePolygonQualityDisabledSvgFile <<
        importedSimplePolygonQualityDisabledSvg.Value();

    const auto assertUnsupportedImportedQualityShape =
        [&importer, &importedSimplePolygonQualityContainerInput](
            const jaguar::ExternalShape& shape, const char* expectedMessage) {
            jaguar::ExternalContainer unsupportedInput =
                importedSimplePolygonQualityContainerInput;
            unsupportedInput.id = 9609;
            unsupportedInput.zones.clear();
            unsupportedInput.zones.push_back({8, shape});
            const auto result = importer.ImportContainer(unsupportedInput);
            assert(!result.HasValue());
            assert(result.GetError().code ==
                   jaguar::ErrorCode::UnsupportedShape);
            assert(result.GetError().message == expectedMessage);
        };
    const std::vector<jaguar::Point2d> rejectedQualityZoneOuter{
        {40.0, 40.0}, {60.0, 40.0}, {60.0, 60.0}, {40.0, 60.0}};
    const std::vector<jaguar::Point2d> rejectedQualityZoneHole{
        {45.0, 45.0}, {45.0, 50.0}, {50.0, 50.0}, {50.0, 45.0}};
    assertUnsupportedImportedQualityShape(
        jaguar::ExternalShape::Polygon(rejectedQualityZoneOuter),
        "quality zone polygons are unsupported");
    assertUnsupportedImportedQualityShape(
        jaguar::ExternalShape::Polygon(rejectedQualityZoneOuter,
                                       {rejectedQualityZoneHole}),
        "quality zone polygons are unsupported");
    assertUnsupportedImportedQualityShape(
        jaguar::ExternalShape::MultiPolygon(
            {jaguar::ExternalPolygon{rejectedQualityZoneOuter, {}}}),
        "quality zone multipolygons are unsupported");

    jaguar::SvgExportOptions grayThemeOptions{};
    grayThemeOptions.theme = jaguar::SvgLayoutTheme::Gray();
    grayThemeOptions.includeAxes = false;
    const auto grayQualitySvg =
        jaguar::ExportLayoutSvg(qualityLayout, grayThemeOptions);
    assert(grayQualitySvg.HasValue());
    assert(grayQualitySvg.Value().find("fill=\"#6E6E6E\"") !=
           std::string::npos);
    assert(grayQualitySvg.Value().find("fill=\"#D3D3D3\"") !=
           std::string::npos);
    assert(grayQualitySvg.Value().find(
               "fill=\"#636363\" fill-opacity=\"0.50\"") !=
           std::string::npos);
    assert(grayQualitySvg.Value().find("stroke-opacity=\"0.9\"") !=
           std::string::npos);
    assert(grayQualitySvg.Value().find("stroke=\"#D00000\"") !=
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

    qualityItemInput.minQuality = 10;
    const auto unsupportedQualityItem = importer.ImportItem(qualityItemInput, 3);
    assert(unsupportedQualityItem.HasValue());
    jaguar::Layout unsupportedQualityLayout(qualityContainer.Value());
    const auto unsupportedQualityPlaced = unsupportedQualityLayout.PlaceItem(
        unsupportedQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {72.0, 70.0}));
    assert(unsupportedQualityPlaced.HasValue());
    const auto unsupportedQualitySvg =
        jaguar::ExportLayoutSvg(unsupportedQualityLayout);
    assert(!unsupportedQualitySvg.HasValue());
    assert(unsupportedQualitySvg.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);

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
