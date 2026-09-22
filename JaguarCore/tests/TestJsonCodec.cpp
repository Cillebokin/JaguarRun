#include "JaguarCore/IO/ExternalRepr.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
std::string loadFixture(const char* fileName)
{
    for (const std::string& path : {
             std::string("..\\jagua-rs\\assets\\") + fileName,
             std::string("jagua-rs\\assets\\") + fileName,
             std::string("CodeResource\\jagua-rs\\assets\\") + fileName,
             std::string("..\\JaguarRun\\CodeResource\\jagua-rs\\assets\\")
                 + fileName})
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
    const auto parsedItem = jaguar::ParseExternalItemJson(baseItemJson());
    assert(parsedItem.HasValue());
    assert(parsedItem.Value().id == 42);
    assert(parsedItem.Value().minQuality == 2);
    assert(parsedItem.Value().rotation.kind == jaguar::RotationPolicyKind::Discrete);
    assert(parsedItem.Value().rotation.anglesDegrees.size() == 4);
    const auto* polygon = std::get_if<jaguar::ExternalPolygon>(
        &parsedItem.Value().shape.value);
    assert(polygon != nullptr);
    assert(polygon->outer.size() == 4);
    assert(polygon->holes.empty());
    (void)polygon;

    jaguar::JsonCodecOptions prettyOptions{};
    prettyOptions.pretty = true;
    const auto serializedItem = jaguar::SerializeExternalItemJson(
        parsedItem.Value(), prettyOptions);
    assert(serializedItem.HasValue());
    assert(serializedItem.Value().find("\"orientation\"") != std::string::npos);
    assert(serializedItem.Value().find("\"allowed_orientations\"") == std::string::npos);
    assert(serializedItem.Value().find("\"inner\"") != std::string::npos);

    const auto reparsedItem = jaguar::ParseExternalItemJson(serializedItem.Value());
    assert(reparsedItem.HasValue());
    assert(reparsedItem.Value().id == parsedItem.Value().id);
    assert(reparsedItem.Value().minQuality == parsedItem.Value().minQuality);
    assert(reparsedItem.Value().rotation.anglesDegrees.size() == 4);

    jaguar::ExternalContainer container{
        800,
        jaguar::ExternalShape::Polygon(
            {{0.0, 0.0}, {20.0, 0.0}, {20.0, 10.0}, {0.0, 10.0}},
            {{{4.0, 2.0}, {6.0, 2.0}, {6.0, 4.0}, {4.0, 4.0}}}),
        {{1, jaguar::ExternalShape::Rectangle(10.0, 0.0, 5.0, 2.0)}}};
    const auto serializedContainer =
        jaguar::SerializeExternalContainerJson(container);
    assert(serializedContainer.HasValue());
    const auto reparsedContainer =
        jaguar::ParseExternalContainerJson(serializedContainer.Value());
    assert(reparsedContainer.HasValue());
    assert(reparsedContainer.Value().id == 800);
    assert(reparsedContainer.Value().zones.size() == 1);

    const auto missingOrientation = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!missingOrientation.HasValue());
    assert(missingOrientation.GetError().code == jaguar::ErrorCode::SerializationFailure);

    const auto legacyOrientation = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "allowed_orientations": [0], "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!legacyOrientation.HasValue());
    assert(legacyOrientation.GetError().code == jaguar::ErrorCode::SerializationFailure);

    const auto unknownRotationField = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "orientation": {"rotation": {"mode": "continuous", "locked": true}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!unknownRotationField.HasValue());
    assert(unknownRotationField.GetError().code == jaguar::ErrorCode::SerializationFailure);

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
    const auto parsedSpp = jaguar::ParseExternalSPInstanceJson(sppJson);
    assert(parsedSpp.HasValue());
    assert(parsedSpp.Value().name == "small-spp");
    assert(parsedSpp.Value().minItemSeparation == 0.0);
    assert(parsedSpp.Value().items.size() == 1);
    assert(parsedSpp.Value().items.front().demand == 3);
    assert(parsedSpp.Value().stripHeight == 10.0);

    const auto serializedSpp = jaguar::SerializeExternalSPInstanceJson(
        parsedSpp.Value());
    assert(serializedSpp.HasValue());
    assert(jaguar::ParseExternalSPInstanceJson(serializedSpp.Value()).HasValue());

    const auto parsedBpp = jaguar::ParseExternalBPInstanceJson(R"json({
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
    assert(parsedBpp.HasValue());
    assert(parsedBpp.Value().bins.size() == 1);
    assert(parsedBpp.Value().bins.front().stock == 2);
    assert(parsedBpp.Value().bins.front().cost == 11);

    const auto parsedMspp = jaguar::ParseExternalMSPInstanceJson(R"json({
        "name": "small-mspp",
        "items": [{
            "id": 7,
            "demand": 1,
            "orientation": {"rotation": {"mode": "continuous"}},
            "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}
        }],
        "strips": {"height": 20, "max_width": 30}
    })json");
    assert(parsedMspp.HasValue());
    assert(parsedMspp.Value().strips.height == 20.0);
    assert(parsedMspp.Value().strips.maxWidth == 30.0);

    jaguar::Importer importer;
    const auto importedSpp = importer.ImportSPInstanceJson(
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
    assert(importedSpp.HasValue());
    assert(importedSpp.Value().ItemCount() == 1);
    assert(!importedSpp.Value().BaseStrip().shapeModifyConfig.offset.has_value());
    const auto importedSppSolution = importer.ImportSPSolutionJson(
        importedSpp.Value(),
        R"json({
            "strip_width": 4,
            "layout": {"container_id": 0, "placed_items": [{
                "item_id": 201,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.25},
            "density": 0.25,
            "run_time_sec": 0
        })json");
    assert(importedSppSolution.HasValue());
    assert(importedSppSolution.Value().layoutSnapshot.placements.size() == 1);
    const auto exportedSpp = jaguar::ExternalSPSolutionFromSolution(
        importedSpp.Value(), importedSppSolution.Value());
    assert(exportedSpp.HasValue());
    assert(exportedSpp.Value().layout.placedItems.size() == 1);
    const auto exportedSppJson = jaguar::SerializeExternalSPSolutionJson(
        exportedSpp.Value());
    assert(exportedSppJson.HasValue());
    const auto reparsedSppSolution = jaguar::ParseExternalSPSolutionJson(
        exportedSppJson.Value());
    assert(reparsedSppSolution.HasValue());
    const auto reimportedSppSolution = importer.ImportSPSolution(
        importedSpp.Value(), reparsedSppSolution.Value());
    assert(reimportedSppSolution.HasValue());
    assert(reimportedSppSolution.Value().layoutSnapshot.placements.size() ==
           importedSppSolution.Value().layoutSnapshot.placements.size());
    assert(std::abs(reimportedSppSolution.Value().strip.width -
                    importedSppSolution.Value().strip.width) < 1.0e-12);
    assert(std::abs(reimportedSppSolution.Value().Density() -
                    importedSppSolution.Value().Density()) < 1.0e-12);
    auto invalidExportedSppSolution = importedSppSolution.Value();
    invalidExportedSppSolution.strip.width += 1.0;
    const auto rejectedSppExport = jaguar::ExternalSPSolutionFromSolution(
        importedSpp.Value(), invalidExportedSppSolution);
    assert(!rejectedSppExport.HasValue());
    const auto unknownSppItem = importer.ImportSPSolutionJson(
        importedSpp.Value(),
        R"json({"strip_width": 4, "layout": {"container_id": 0, "placed_items": [{"item_id": 999, "transformation": {"rotation": 0, "translation": [1, 1]}}], "density": 0}, "density": 0, "run_time_sec": 0})json");
    assert(!unknownSppItem.HasValue());
    assert(unknownSppItem.GetError().code == jaguar::ErrorCode::SnapshotMismatch);
    const auto overDemandSpp = importer.ImportSPSolutionJson(
        importedSpp.Value(),
        R"json({"strip_width": 4, "layout": {"container_id": 0, "placed_items": [{"item_id": 201, "transformation": {"rotation": 0, "translation": [1, 1]}}, {"item_id": 201, "transformation": {"rotation": 0, "translation": [3, 1]}}], "density": 0}, "density": 0, "run_time_sec": 0})json");
    assert(!overDemandSpp.HasValue());
    assert(overDemandSpp.GetError().code == jaguar::ErrorCode::Infeasible);

    const auto importedBpp = importer.ImportBPInstanceJson(
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
    assert(importedBpp.HasValue());
    const auto importedBppSolution = importer.ImportBPSolutionJson(
        importedBpp.Value(),
        R"json({
            "cost": 10,
            "layouts": [{"container_id": 401, "placed_items": [{
                "item_id": 301,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(importedBppSolution.HasValue());
    assert(importedBppSolution.Value().layouts.size() == 1);
    assert(importedBppSolution.Value().layouts.front().layoutSnapshot.placements.size() == 1);
    const auto exportedBpp = jaguar::ExternalBPSolutionFromSolution(
        importedBpp.Value(), importedBppSolution.Value());
    assert(exportedBpp.HasValue());
    assert(exportedBpp.Value().layouts.size() == 1);
    const auto exportedBppJson = jaguar::SerializeExternalBPSolutionJson(
        exportedBpp.Value());
    assert(exportedBppJson.HasValue());
    const auto reparsedBppSolution = jaguar::ParseExternalBPSolutionJson(
        exportedBppJson.Value());
    assert(reparsedBppSolution.HasValue());
    const auto reimportedBppSolution = importer.ImportBPSolution(
        importedBpp.Value(), reparsedBppSolution.Value());
    assert(reimportedBppSolution.HasValue());
    assert(reimportedBppSolution.Value().layouts.size() ==
           importedBppSolution.Value().layouts.size());
    assert(reimportedBppSolution.Value().Cost(importedBpp.Value()) ==
           importedBppSolution.Value().Cost(importedBpp.Value()));
    assert(std::abs(reimportedBppSolution.Value().Density() -
                    importedBppSolution.Value().Density()) < 1.0e-12);
    auto invalidExportedBppSolution = importedBppSolution.Value();
    invalidExportedBppSolution.layouts.front().binIndex = 99;
    const auto rejectedBppExport = jaguar::ExternalBPSolutionFromSolution(
        importedBpp.Value(), invalidExportedBppSolution);
    assert(!rejectedBppExport.HasValue());

    const auto importedMspp = importer.ImportMSPInstanceJson(
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
    assert(importedMspp.HasValue());
    assert(!importedMspp.Value().BaseStrip().shapeModifyConfig.offset.has_value());

    const auto separatedSpp = importer.ImportSPInstanceJson(
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
    assert(separatedSpp.HasValue());
    assert(separatedSpp.Value().BaseStrip().shapeModifyConfig.offset.has_value());
    assert(std::abs(*separatedSpp.Value().BaseStrip().shapeModifyConfig.offset - 1.0) <
           1.0e-12);
    const auto importedMsppSolution = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
        R"json({
            "layouts": [{"container_id": 0, "placed_items": [{
                "item_id": 501,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(importedMsppSolution.HasValue());
    assert(importedMsppSolution.Value().layouts.size() == 1);
    assert(importedMsppSolution.Value().layouts.front().layoutSnapshot.placements.size() == 1);

    const auto importedMsppWithEmptyLayout = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
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
    assert(importedMsppWithEmptyLayout.HasValue());
    assert(importedMsppWithEmptyLayout.Value().layouts.size() == 2);
    assert(importedMsppWithEmptyLayout.Value().layouts.front().layoutSnapshot.placements.empty());
    assert(importedMsppWithEmptyLayout.Value().layouts.back().layoutSnapshot.placements.size() == 1);

    const auto mismatchedMsppHeight = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
        R"json({
            "layouts": [{"container_id": 0, "placed_items": [{
                "item_id": 501,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 6}}}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(!mismatchedMsppHeight.HasValue());
    assert(mismatchedMsppHeight.GetError().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto mismatchedMsppOrigin = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
        R"json({
            "layouts": [{"container_id": 0, "placed_items": [{
                "item_id": 501,
                "transformation": {"rotation": 0, "translation": [1, 1]}
            }], "density": 0.1}],
            "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 1, "y_min": 0, "width": 10, "height": 5}}}],
            "density": 0.1,
            "run_time_sec": 0
        })json");
    assert(!mismatchedMsppOrigin.HasValue());
    assert(mismatchedMsppOrigin.GetError().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto duplicateMsppContainerIds = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
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
    assert(!duplicateMsppContainerIds.HasValue());
    assert(duplicateMsppContainerIds.GetError().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto duplicateMsppLayoutIds = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
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
    assert(!duplicateMsppLayoutIds.HasValue());
    assert(duplicateMsppLayoutIds.GetError().code == jaguar::ErrorCode::SnapshotMismatch);

    const auto exportedMspp = jaguar::ExternalMSPSolutionFromSolution(
        importedMspp.Value(), importedMsppSolution.Value());
    assert(exportedMspp.HasValue());
    assert(exportedMspp.Value().layouts.size() == 1);
    assert(exportedMspp.Value().containers.size() == 1);
    const auto exportedMsppJson = jaguar::SerializeExternalMSPSolutionJson(
        exportedMspp.Value());
    assert(exportedMsppJson.HasValue());
    const auto reparsedMsppSolution = jaguar::ParseExternalMSPSolutionJson(
        exportedMsppJson.Value());
    assert(reparsedMsppSolution.HasValue());
    const auto reimportedMsppSolution = importer.ImportMSPSolution(
        importedMspp.Value(), reparsedMsppSolution.Value());
    assert(reimportedMsppSolution.HasValue());
    assert(reimportedMsppSolution.Value().layouts.size() ==
           importedMsppSolution.Value().layouts.size());
    assert(std::abs(reimportedMsppSolution.Value().TotalStripWidth() -
                    importedMsppSolution.Value().TotalStripWidth()) < 1.0e-12);
    assert(std::abs(reimportedMsppSolution.Value().Density() -
                    importedMsppSolution.Value().Density()) < 1.0e-12);
    auto invalidExportedMsppSolution = importedMsppSolution.Value();
    invalidExportedMsppSolution.layouts.front().layoutSnapshot.placements.front()
        .placedItem = importedBppSolution.Value().layouts.front()
                          .layoutSnapshot.placements.front().placedItem;
    const auto rejectedMsppExport = jaguar::ExternalMSPSolutionFromSolution(
        importedMspp.Value(), invalidExportedMsppSolution);
    assert(!rejectedMsppExport.HasValue());
    const auto mismatchedMspp = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
        R"json({"layouts": [], "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}], "density": 0, "run_time_sec": 0})json");
    assert(!mismatchedMspp.HasValue());
    assert(mismatchedMspp.GetError().code == jaguar::ErrorCode::SnapshotMismatch);

    const std::string swimFixture = loadFixture("swim.json");
    assert(!swimFixture.empty());
    const auto parsedSwim = jaguar::ParseExternalSPInstanceJson(swimFixture);
    assert(parsedSwim.HasValue());
    assert(!parsedSwim.Value().items.empty());

    const std::string baldacciFixture = loadFixture("baldacci1.json");
    assert(!baldacciFixture.empty());
    const auto parsedBaldacci = jaguar::ParseExternalBPInstanceJson(baldacciFixture);
    assert(parsedBaldacci.HasValue());
    assert(!parsedBaldacci.Value().items.empty());
    assert(!parsedBaldacci.Value().bins.empty());

    const jaguar::ExternalLayout layout{
        9,
        {{7, {15.0, {2.0, 3.0}}}},
        0.25};
    const auto serializedLayout = jaguar::SerializeExternalLayoutJson(layout);
    assert(serializedLayout.HasValue());
    const auto reparsedLayout = jaguar::ParseExternalLayoutJson(serializedLayout.Value());
    assert(reparsedLayout.HasValue());
    assert(reparsedLayout.Value().containerId == 9);
    assert(reparsedLayout.Value().placedItems.size() == 1);
    assert(std::abs(reparsedLayout.Value().placedItems.front().transformation.rotationDegrees -
                    15.0) < 1.0e-12);

    const jaguar::ExternalSPSolution spSolution{12.0, layout, 0.75, 4};
    const auto spSolutionJson = jaguar::SerializeExternalSPSolutionJson(spSolution);
    assert(spSolutionJson.HasValue());
    assert(jaguar::ParseExternalSPSolutionJson(spSolutionJson.Value()).HasValue());

    const jaguar::ExternalBPSolution bpSolution{17, {layout}, 0.8, 5};
    const auto bpSolutionJson = jaguar::SerializeExternalBPSolutionJson(bpSolution);
    assert(bpSolutionJson.HasValue());
    assert(jaguar::ParseExternalBPSolutionJson(bpSolutionJson.Value()).HasValue());

    const jaguar::ExternalMSPSolution mspSolution{
        {layout}, {container}, 0.9, 6};
    const auto mspSolutionJson =
        jaguar::SerializeExternalMSPSolutionJson(mspSolution);
    assert(mspSolutionJson.HasValue());
    assert(jaguar::ParseExternalMSPSolutionJson(mspSolutionJson.Value()).HasValue());
}
