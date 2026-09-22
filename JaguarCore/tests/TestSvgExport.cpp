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

    jaguar::SvgExportOptions invalidOptions{};
    invalidOptions.theme.strokeWidthMultiplier = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = jaguar::ExportLayoutSvg(layout, invalidOptions);
    assert(!invalid.HasValue());
    assert(invalid.GetError().code == jaguar::ErrorCode::SerializationFailure);
    assert(jaguar::ValidateLayoutSnapshot(layout, before).HasValue());
}
