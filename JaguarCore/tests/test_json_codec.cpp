#include "JaguarCore/io/external_repr.h"
#include "JaguarCore/io/importer.h"
#include "JaguarCore/io/json_codec.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
std::string loadFixture(const char* fileName)
{
    for (const std::string& path : {std::string("..\\jagua-rs\\assets\\") + fileName,
                                    std::string("jagua-rs\\assets\\") + fileName})
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            continue;
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    }
    return {};
}
}

namespace
{
const char* baseItemJson()
{
    return R"json({
        "id": 42,
        "dxf": "metadata/is_allowed.json",
        "orientation": {"rotation": {"mode": "discrete", "angles": [-90, 270, 0, 360]}},
        "shape": {"type": "polygon", "data": {
            "outer": [[0, 0], [10, 0], [10, 10], [0, 10]],
            "inner": []
        }},
        "min_quality": 2
    })json";
}
}

void runJsonCodecTests()
{
    const auto parsedItem = jaguar::parseExternalItemJson(baseItemJson());
    assert(parsedItem.hasValue());
    assert(parsedItem.value().id == 42);
    assert(parsedItem.value().minQuality == 2);
    assert(parsedItem.value().rotation.kind == jaguar::RotationPolicyKind::Discrete);
    assert(parsedItem.value().rotation.anglesDegrees.size() == 4);
    const auto* polygon = std::get_if<jaguar::ExternalPolygon>(
        &parsedItem.value().shape.value);
    assert(polygon != nullptr);
    assert(polygon->outer.size() == 4);
    assert(polygon->holes.empty());
    (void)polygon;

    jaguar::JsonCodecOptions prettyOptions{};
    prettyOptions.pretty = true;
    const auto serializedItem = jaguar::serializeExternalItemJson(
        parsedItem.value(), prettyOptions);
    assert(serializedItem.hasValue());
    assert(serializedItem.value().find("\"orientation\"") != std::string::npos);
    assert(serializedItem.value().find("\"allowed_orientations\"") == std::string::npos);
    assert(serializedItem.value().find("\"inner\"") != std::string::npos);

    const auto reparsedItem = jaguar::parseExternalItemJson(serializedItem.value());
    assert(reparsedItem.hasValue());
    assert(reparsedItem.value().id == parsedItem.value().id);
    assert(reparsedItem.value().minQuality == parsedItem.value().minQuality);
    assert(reparsedItem.value().rotation.anglesDegrees.size() == 4);

    jaguar::ExternalContainer container{
        800,
        jaguar::ExternalShape::polygon(
            {{0.0, 0.0}, {20.0, 0.0}, {20.0, 10.0}, {0.0, 10.0}},
            {{{4.0, 2.0}, {6.0, 2.0}, {6.0, 4.0}, {4.0, 4.0}}}),
        {{1, jaguar::ExternalShape::rectangle(10.0, 0.0, 5.0, 2.0)}}};
    const auto serializedContainer =
        jaguar::serializeExternalContainerJson(container);
    assert(serializedContainer.hasValue());
    const auto reparsedContainer =
        jaguar::parseExternalContainerJson(serializedContainer.value());
    assert(reparsedContainer.hasValue());
    assert(reparsedContainer.value().id == 800);
    assert(reparsedContainer.value().zones.size() == 1);

    const auto missingOrientation = jaguar::parseExternalItemJson(
        R"json({"id": 1, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!missingOrientation.hasValue());
    assert(missingOrientation.error().code == jaguar::ErrorCode::SerializationFailure);

    const auto legacyOrientation = jaguar::parseExternalItemJson(
        R"json({"id": 1, "allowed_orientations": [0], "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!legacyOrientation.hasValue());
    assert(legacyOrientation.error().code == jaguar::ErrorCode::SerializationFailure);

    const auto unknownRotationField = jaguar::parseExternalItemJson(
        R"json({"id": 1, "orientation": {"rotation": {"mode": "continuous", "locked": true}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!unknownRotationField.hasValue());
    assert(unknownRotationField.error().code == jaguar::ErrorCode::SerializationFailure);

    const char* sppJson = R"json({
        "name": "small-spp",
        "items": [{
            "id": 7,
            "demand": 3,
            "orientation": {"rotation": {"mode": "stepped", "step": 90}},
            "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 2, "height": 4}}
        }],
        "strip_height": 10
    })json";
    const auto parsedSpp = jaguar::parseExternalSPInstanceJson(sppJson);
    assert(parsedSpp.hasValue());
    assert(parsedSpp.value().name == "small-spp");
    assert(parsedSpp.value().minItemSeparation == 0.0);
    assert(parsedSpp.value().items.size() == 1);
    assert(parsedSpp.value().items.front().demand == 3);
    assert(parsedSpp.value().stripHeight == 10.0);

    const auto serializedSpp = jaguar::serializeExternalSPInstanceJson(
        parsedSpp.value());
    assert(serializedSpp.hasValue());
    assert(jaguar::parseExternalSPInstanceJson(serializedSpp.value()).hasValue());

    const auto parsedBpp = jaguar::parseExternalBPInstanceJson(R"json({
        "name": "small-bpp",
        "min_item_separation": 1.5,
        "items": [{
            "id": 7,
            "demand": 1,
            "orientation": {"rotation": {"mode": "discrete", "angles": [0]}},
            "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}
        }],
        "bins": [{
            "id": 9,
            "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 20}},
            "stock": 2,
            "cost": 11
        }]
    })json");
    assert(parsedBpp.hasValue());
    assert(parsedBpp.value().bins.size() == 1);
    assert(parsedBpp.value().bins.front().stock == 2);
    assert(parsedBpp.value().bins.front().cost == 11);

    const auto parsedMspp = jaguar::parseExternalMSPInstanceJson(R"json({
        "name": "small-mspp",
        "items": [{
            "id": 7,
            "demand": 1,
            "orientation": {"rotation": {"mode": "continuous"}},
            "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}
        }],
        "strips": {"height": 20, "max_width": 30}
    })json");
    assert(parsedMspp.hasValue());
    assert(parsedMspp.value().strips.height == 20.0);
    assert(parsedMspp.value().strips.maxWidth == 30.0);

    jaguar::Importer importer;
    const auto importedSpp = importer.importSPInstanceJson(
        R"json({
            "name": "one-spp",
            "items": [{
                "id": 201,
                "demand": 1,
                "orientation": {"rotation": {"mode": "discrete", "angles": [0]}},
                "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 2, "height": 2}}
            }],
            "strip_height": 4
        })json");
    assert(importedSpp.hasValue());
    assert(importedSpp.value().itemCount() == 1);
    assert(!importedSpp.value().baseStrip().shapeModifyConfig.offset.has_value());
    const auto importedSppSolution = importer.importSPSolutionJson(
        importedSpp.value(),
        R"json({
            "strip_width": 4,
            "layout": {"container_id": 0, "placed_items": [{
                "item_id": 201,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.25},
            "density": 0.25,
            "run_time_sec": 0
        })json");
    assert(importedSppSolution.hasValue());
    assert(importedSppSolution.value().layoutSnapshot.placements.size() == 1);
    const auto exportedSpp = jaguar::externalSPSolutionFromSolution(
        importedSpp.value(), importedSppSolution.value());
    assert(exportedSpp.hasValue());
    assert(exportedSpp.value().layout.placedItems.size() == 1);
    const auto exportedSppJson = jaguar::serializeExternalSPSolutionJson(
        exportedSpp.value());
    assert(exportedSppJson.hasValue());
    const auto reparsedSppSolution = jaguar::parseExternalSPSolutionJson(
        exportedSppJson.value());
    assert(reparsedSppSolution.hasValue());
    const auto reimportedSppSolution = importer.importSPSolution(
        importedSpp.value(), reparsedSppSolution.value());
    assert(reimportedSppSolution.hasValue());
    assert(reimportedSppSolution.value().layoutSnapshot.placements.size() ==
           importedSppSolution.value().layoutSnapshot.placements.size());
    assert(std::abs(reimportedSppSolution.value().strip.width -
                    importedSppSolution.value().strip.width) < 1.0e-12);
    assert(std::abs(reimportedSppSolution.value().density() -
                    importedSppSolution.value().density()) < 1.0e-12);
    auto invalidExportedSppSolution = importedSppSolution.value();
    invalidExportedSppSolution.strip.width += 1.0;
    const auto rejectedSppExport = jaguar::externalSPSolutionFromSolution(
        importedSpp.value(), invalidExportedSppSolution);
    assert(!rejectedSppExport.hasValue());
    const auto unknownSppItem = importer.importSPSolutionJson(
        importedSpp.value(),
        R"json({"strip_width": 4, "layout": {"container_id": 0, "placed_items": [{"item_id": 999, "transformation": {"rotation": 0, "translation": [1, 1]}}], "density": 0}, "density": 0, "run_time_sec": 0})json");
    assert(!unknownSppItem.hasValue());
    assert(unknownSppItem.error().code == jaguar::ErrorCode::SnapshotMismatch);
    const auto overDemandSpp = importer.importSPSolutionJson(
        importedSpp.value(),
        R"json({"strip_width": 4, "layout": {"container_id": 0, "placed_items": [{"item_id": 201, "transformation": {"rotation": 0, "translation": [1, 1]}}, {"item_id": 201, "transformation": {"rotation": 0, "translation": [3, 1]}}], "density": 0}, "density": 0, "run_time_sec": 0})json");
    assert(!overDemandSpp.hasValue());
    assert(overDemandSpp.error().code == jaguar::ErrorCode::Infeasible);

    const auto importedBpp = importer.importBPInstanceJson(
        R"json({
            "name": "one-bpp",
            "items": [{
                "id": 301,
                "demand": 1,
                "orientation": {"rotation": {"mode": "discrete", "angles": [0]}},
                "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 2, "height": 2}}
            }],
            "bins": [{
                "id": 401,
                "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 6, "height": 6}},
                "stock": 1,
                "cost": 10
            }]
        })json");
    assert(importedBpp.hasValue());
    const auto importedBppSolution = importer.importBPSolutionJson(
        importedBpp.value(),
        R"json({
            "cost": 10,
            "layouts": [{"container_id": 401, "placed_items": [{
                "item_id": 301,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(importedBppSolution.hasValue());
    assert(importedBppSolution.value().layouts.size() == 1);
    assert(importedBppSolution.value().layouts.front().layoutSnapshot.placements.size() == 1);
    const auto exportedBpp = jaguar::externalBPSolutionFromSolution(
        importedBpp.value(), importedBppSolution.value());
    assert(exportedBpp.hasValue());
    assert(exportedBpp.value().layouts.size() == 1);
    const auto exportedBppJson = jaguar::serializeExternalBPSolutionJson(
        exportedBpp.value());
    assert(exportedBppJson.hasValue());
    const auto reparsedBppSolution = jaguar::parseExternalBPSolutionJson(
        exportedBppJson.value());
    assert(reparsedBppSolution.hasValue());
    const auto reimportedBppSolution = importer.importBPSolution(
        importedBpp.value(), reparsedBppSolution.value());
    assert(reimportedBppSolution.hasValue());
    assert(reimportedBppSolution.value().layouts.size() ==
           importedBppSolution.value().layouts.size());
    assert(reimportedBppSolution.value().cost(importedBpp.value()) ==
           importedBppSolution.value().cost(importedBpp.value()));
    assert(std::abs(reimportedBppSolution.value().density() -
                    importedBppSolution.value().density()) < 1.0e-12);
    auto invalidExportedBppSolution = importedBppSolution.value();
    invalidExportedBppSolution.layouts.front().binIndex = 99;
    const auto rejectedBppExport = jaguar::externalBPSolutionFromSolution(
        importedBpp.value(), invalidExportedBppSolution);
    assert(!rejectedBppExport.hasValue());

    const auto importedMspp = importer.importMSPInstanceJson(
        R"json({
            "name": "one-mspp",
            "items": [{
                "id": 501,
                "demand": 1,
                "orientation": {"rotation": {"mode": "discrete", "angles": [0]}},
                "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 2, "height": 2}}
            }],
            "strips": {"height": 5, "max_width": 10}
        })json");
    assert(importedMspp.hasValue());
    assert(!importedMspp.value().baseStrip().shapeModifyConfig.offset.has_value());

    const auto separatedSpp = importer.importSPInstanceJson(
        R"json({
            "name": "separated-spp",
            "min_item_separation": 2,
            "items": [{
                "id": 601,
                "demand": 1,
                "orientation": {"rotation": {"mode": "discrete", "angles": [0]}},
                "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 2, "height": 2}}
            }],
            "strip_height": 4
        })json");
    assert(separatedSpp.hasValue());
    assert(separatedSpp.value().baseStrip().shapeModifyConfig.offset.has_value());
    assert(std::abs(*separatedSpp.value().baseStrip().shapeModifyConfig.offset - 1.0) <
           1.0e-12);
    const auto importedMsppSolution = importer.importMSPSolutionJson(
        importedMspp.value(),
        R"json({
            "layouts": [{"container_id": 0, "placed_items": [{
                "item_id": 501,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(importedMsppSolution.hasValue());
    assert(importedMsppSolution.value().layouts.size() == 1);
    assert(importedMsppSolution.value().layouts.front().layoutSnapshot.placements.size() == 1);

    const auto importedMsppWithEmptyLayout = importer.importMSPSolutionJson(
        importedMspp.value(),
        R"json({
            "layouts": [
                {"container_id": 0, "placed_items": [], "density": 0},
                {"container_id": 1, "placed_items": [{
                    "item_id": 501,
                    "transformation": {"rotation": 0, "translation": [1, 1]}
                }], "density": 0.1}
            ],
            "containers": [
                {"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}},
                {"id": 1, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}
            ],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(importedMsppWithEmptyLayout.hasValue());
    assert(importedMsppWithEmptyLayout.value().layouts.size() == 2);
    assert(importedMsppWithEmptyLayout.value().layouts.front().layoutSnapshot.placements.empty());
    assert(importedMsppWithEmptyLayout.value().layouts.back().layoutSnapshot.placements.size() == 1);

    const auto mismatchedMsppHeight = importer.importMSPSolutionJson(
        importedMspp.value(),
        R"json({
            "layouts": [{"container_id": 0, "placed_items": [{
                "item_id": 501,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 6}}}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(!mismatchedMsppHeight.hasValue());
    assert(mismatchedMsppHeight.error().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto mismatchedMsppOrigin = importer.importMSPSolutionJson(
        importedMspp.value(),
        R"json({
            "layouts": [{"container_id": 0, "placed_items": [{
                "item_id": 501,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 1, "y_min": 0, "width": 10, "height": 5}}}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(!mismatchedMsppOrigin.hasValue());
    assert(mismatchedMsppOrigin.error().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto duplicateMsppContainerIds = importer.importMSPSolutionJson(
        importedMspp.value(),
        R"json({
            "layouts": [
                {"container_id": 0, "placed_items": [{"item_id": 501, "transformation": {"rotation": 0, "translation": [1, 1]}}], "density": 0.1},
                {"container_id": 0, "placed_items": [{"item_id": 501, "transformation": {"rotation": 0, "translation": [1, 1]}}], "density": 0.1}
            ],
            "containers": [
                {"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}},
                {"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}
            ],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(!duplicateMsppContainerIds.hasValue());
    assert(duplicateMsppContainerIds.error().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto duplicateMsppLayoutIds = importer.importMSPSolutionJson(
        importedMspp.value(),
        R"json({
            "layouts": [
                {"container_id": 0, "placed_items": [{"item_id": 501, "transformation": {"rotation": 0, "translation": [1, 1]}}], "density": 0.1},
                {"container_id": 0, "placed_items": [{"item_id": 501, "transformation": {"rotation": 0, "translation": [2, 1]}}], "density": 0.1}
            ],
            "containers": [
                {"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}},
                {"id": 1, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}
            ],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(!duplicateMsppLayoutIds.hasValue());
    assert(duplicateMsppLayoutIds.error().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto exportedMspp = jaguar::externalMSPSolutionFromSolution(
        importedMspp.value(), importedMsppSolution.value());
    assert(exportedMspp.hasValue());
    assert(exportedMspp.value().layouts.size() == 1);
    assert(exportedMspp.value().containers.size() == 1);
    const auto exportedMsppJson = jaguar::serializeExternalMSPSolutionJson(
        exportedMspp.value());
    assert(exportedMsppJson.hasValue());
    const auto reparsedMsppSolution = jaguar::parseExternalMSPSolutionJson(
        exportedMsppJson.value());
    assert(reparsedMsppSolution.hasValue());
    const auto reimportedMsppSolution = importer.importMSPSolution(
        importedMspp.value(), reparsedMsppSolution.value());
    assert(reimportedMsppSolution.hasValue());
    assert(reimportedMsppSolution.value().layouts.size() ==
           importedMsppSolution.value().layouts.size());
    assert(std::abs(reimportedMsppSolution.value().totalStripWidth() -
                    importedMsppSolution.value().totalStripWidth()) < 1.0e-12);
    assert(std::abs(reimportedMsppSolution.value().density() -
                    importedMsppSolution.value().density()) < 1.0e-12);
    auto invalidExportedMsppSolution = importedMsppSolution.value();
    invalidExportedMsppSolution.layouts.front().layoutSnapshot.placements.front()
        .placedItem = importedBppSolution.value().layouts.front()
                          .layoutSnapshot.placements.front().placedItem;
    const auto rejectedMsppExport = jaguar::externalMSPSolutionFromSolution(
        importedMspp.value(), invalidExportedMsppSolution);
    assert(!rejectedMsppExport.hasValue());
    const auto mismatchedMspp = importer.importMSPSolutionJson(
        importedMspp.value(),
        R"json({"layouts": [], "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}], "density": 0, "run_time_sec": 0})json");
    assert(!mismatchedMspp.hasValue());
    assert(mismatchedMspp.error().code == jaguar::ErrorCode::SnapshotMismatch);

    const std::string swimFixture = loadFixture("swim.json");
    assert(!swimFixture.empty());
    const auto parsedSwim = jaguar::parseExternalSPInstanceJson(swimFixture);
    assert(parsedSwim.hasValue());
    assert(!parsedSwim.value().items.empty());

    const std::string baldacciFixture = loadFixture("baldacci1.json");
    assert(!baldacciFixture.empty());
    const auto parsedBaldacci = jaguar::parseExternalBPInstanceJson(baldacciFixture);
    assert(parsedBaldacci.hasValue());
    assert(!parsedBaldacci.value().items.empty());
    assert(!parsedBaldacci.value().bins.empty());

    const jaguar::ExternalLayout layout{
        9,
        {{7, {15.0, {2.0, 3.0}}}},
        0.25};
    const auto serializedLayout = jaguar::serializeExternalLayoutJson(layout);
    assert(serializedLayout.hasValue());
    const auto reparsedLayout = jaguar::parseExternalLayoutJson(serializedLayout.value());
    assert(reparsedLayout.hasValue());
    assert(reparsedLayout.value().containerId == 9);
    assert(reparsedLayout.value().placedItems.size() == 1);
    assert(std::abs(reparsedLayout.value().placedItems.front().transformation.rotationDegrees -
                    15.0) < 1.0e-12);

    const jaguar::ExternalSPSolution spSolution{12.0, layout, 0.75, 4};
    const auto spSolutionJson = jaguar::serializeExternalSPSolutionJson(spSolution);
    assert(spSolutionJson.hasValue());
    assert(jaguar::parseExternalSPSolutionJson(spSolutionJson.value()).hasValue());

    const jaguar::ExternalBPSolution bpSolution{17, {layout}, 0.8, 5};
    const auto bpSolutionJson = jaguar::serializeExternalBPSolutionJson(bpSolution);
    assert(bpSolutionJson.hasValue());
    assert(jaguar::parseExternalBPSolutionJson(bpSolutionJson.value()).hasValue());

    const jaguar::ExternalMSPSolution mspSolution{
        {layout}, {container}, 0.9, 6};
    const auto mspSolutionJson =
        jaguar::serializeExternalMSPSolutionJson(mspSolution);
    assert(mspSolutionJson.hasValue());
    assert(jaguar::parseExternalMSPSolutionJson(mspSolutionJson.value()).hasValue());
}
