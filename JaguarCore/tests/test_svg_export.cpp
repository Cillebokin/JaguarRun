#include "JaguarCore/io/importer.h"
#include "JaguarCore/io/json_codec.h"
#include "JaguarCore/io/svg_export.h"
#include "JaguarCore/assertions.h"

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
    itemInput.shape = jaguar::ExternalShape::rectangle(0.0, 0.0, 10.0, 20.0);
    itemInput.rotation = jaguar::RotationPolicy::none();
    const auto item = importer.importItem(itemInput, 0);
    assert(item.hasValue());

    jaguar::ExternalContainer containerInput{};
    containerInput.id = 9100;
    containerInput.shape = jaguar::ExternalShape::rectangle(0.0, 0.0, 100.0, 100.0);
    const auto container = importer.importContainer(containerInput);
    assert(container.hasValue());

    jaguar::Layout layout(container.value());
    const auto placed = layout.placeItem(
        item.value(),
        jaguar::Transform2d::fromRotationTranslation(
            0.25 * std::acos(-1.0), {50.0, 50.0}));
    assert(placed.hasValue());
    const auto before = layout.save();

    const auto externalLayout = jaguar::externalLayoutFromLayout(layout);
    assert(externalLayout.hasValue());
    assert(externalLayout.value().containerId == 9100);
    assert(externalLayout.value().placedItems.size() == 1);
    assert(externalLayout.value().placedItems.front().itemId == 9001);
    assert(std::abs(externalLayout.value().density - layout.density()) < 1.0e-12);
    const auto externalLayoutJson =
        jaguar::serializeExternalLayoutJson(externalLayout.value());
    assert(externalLayoutJson.hasValue());
    assert(jaguar::parseExternalLayoutJson(externalLayoutJson.value()).hasValue());

    jaguar::SvgExportOptions options{};
    options.drawSurrogates = true;
    options.highlightCollisions = true;
    const auto svg = jaguar::exportLayoutSvg(layout, options);
    assert(svg.hasValue());
    assert(svg.value().find("<svg") != std::string::npos);
    assert(svg.value().find("viewBox=") != std::string::npos);
    assert(svg.value().find("scale(1 -1)") != std::string::npos);
    assert(svg.value().find("container-original") != std::string::npos);
    assert(svg.value().find("container-collision") != std::string::npos);
    assert(svg.value().find("item-original") != std::string::npos);
    assert(svg.value().find("item-collision") != std::string::npos);
    assert(svg.value().find("data-item-id=\"9001\"") != std::string::npos);
    assert(svg.value().find("data-rotation-degrees=") != std::string::npos);
    assert(svg.value().find("translate(") != std::string::npos);
    assert(svg.value().find("rotate(") != std::string::npos);
    assert(svg.value().find("surrogate") != std::string::npos);
    std::ofstream svgFile("JaguarCoreSvgTest.svg", std::ios::binary);
    assert(svgFile);
    svgFile << svg.value();
    svgFile.close();
    assert(jaguar::validateLayoutSnapshot(layout, before).hasValue());

    jaguar::SvgExportOptions invalidOptions{};
    invalidOptions.theme.strokeWidthMultiplier = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = jaguar::exportLayoutSvg(layout, invalidOptions);
    assert(!invalid.hasValue());
    assert(invalid.error().code == jaguar::ErrorCode::SerializationFailure);
    assert(jaguar::validateLayoutSnapshot(layout, before).hasValue());
}
