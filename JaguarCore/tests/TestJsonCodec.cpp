#include "JaguarCore/IO/ExternalRepr.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
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

    const auto missingOrientation = jaguar::Importer{}.ImportItemJson(
        R"json({"id": 1, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json",
        0);
    assert(!missingOrientation.HasValue());
    assert(missingOrientation.GetError().code == jaguar::ErrorCode::SerializationFailure);

    const auto legacyOrientation = jaguar::Importer{}.ImportItemJson(
        R"json({"id": 1, "allowed_orientations": [0], "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json",
        0);
    assert(!legacyOrientation.HasValue());
    assert(legacyOrientation.GetError().code == jaguar::ErrorCode::SerializationFailure);

    const auto unknownRotationField = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "orientation": {"rotation": {"mode": "continuous", "locked": true}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!unknownRotationField.HasValue());
    assert(unknownRotationField.GetError().code == jaguar::ErrorCode::SerializationFailure);

    const auto floatingPointId = jaguar::ParseExternalItemJson(
        R"json({"id": 1.0, "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!floatingPointId.HasValue());
    assert(floatingPointId.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    const auto negativeZeroId = jaguar::ParseExternalItemJson(
        R"json({"id": -0, "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!negativeZeroId.HasValue());
    assert(negativeZeroId.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    const auto floatingPointMinQuality = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}, "min_quality": 1.0})json");
    assert(!floatingPointMinQuality.HasValue());
    assert(floatingPointMinQuality.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    const auto duplicateUnknownMetadata = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "dxf": "first.dxf", "dxf": "second.dxf", "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(duplicateUnknownMetadata.HasValue());
    const auto duplicateId = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "id": 2, "orientation": {"rotation": {"mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!duplicateId.HasValue());
    assert(duplicateId.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    const auto duplicateRotationMode = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "orientation": {"rotation": {"mode": "continuous", "mode": "continuous"}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(!duplicateRotationMode.HasValue());
    assert(duplicateRotationMode.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    const auto singlePrecisionAngle = jaguar::ParseExternalItemJson(
        R"json({"id": 1, "orientation": {"rotation": {"mode": "discrete", "angles": [123.456789]}}, "shape": {"type": "simple_polygon", "data": [[0,0],[1,0],[0,1]]}})json");
    assert(singlePrecisionAngle.HasValue());
    assert(singlePrecisionAngle.Value().rotation.anglesDegrees.front() ==
           123.456787109375);

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

    const auto nullSeparation = jaguar::ParseExternalSPInstanceJson(
        R"json({"name":"null-separation","min_item_separation":null,"items":[],"strip_height":1})json");
    assert(!nullSeparation.HasValue());
    assert(nullSeparation.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    const auto outOfRangeJsonSeparation = jaguar::ParseExternalSPInstanceJson(
        R"json({"name":"f32-overflow-separation","min_item_separation":1e39,"items":[],"strip_height":1})json");
    assert(!outOfRangeJsonSeparation.HasValue());
    assert(outOfRangeJsonSeparation.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);

    const auto serializedSpp = jaguar::SerializeExternalSPInstanceJson(
        parsedSpp.Value());
    assert(serializedSpp.HasValue());
    assert(serializedSpp.Value().find("\"min_item_separation\"") !=
           std::string::npos);
    const auto reparsedZeroSeparationSpp =
        jaguar::ParseExternalSPInstanceJson(serializedSpp.Value());
    assert(reparsedZeroSeparationSpp.HasValue());
    assert(reparsedZeroSeparationSpp.Value().minItemSeparation == 0.0);

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
    auto zeroSeparationBpp = parsedBpp.Value();
    zeroSeparationBpp.minItemSeparation = 0.0;
    const auto serializedZeroSeparationBpp =
        jaguar::SerializeExternalBPInstanceJson(zeroSeparationBpp);
    assert(serializedZeroSeparationBpp.HasValue());
    assert(serializedZeroSeparationBpp.Value().find("\"min_item_separation\"") !=
           std::string::npos);
    const auto reparsedZeroSeparationBpp =
        jaguar::ParseExternalBPInstanceJson(serializedZeroSeparationBpp.Value());
    assert(reparsedZeroSeparationBpp.HasValue());
    assert(reparsedZeroSeparationBpp.Value().minItemSeparation == 0.0);

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
    const auto serializedZeroSeparationMspp =
        jaguar::SerializeExternalMSPInstanceJson(parsedMspp.Value());
    assert(serializedZeroSeparationMspp.HasValue());
    assert(serializedZeroSeparationMspp.Value().find("\"min_item_separation\"") !=
           std::string::npos);
    const auto reparsedZeroSeparationMspp =
        jaguar::ParseExternalMSPInstanceJson(
            serializedZeroSeparationMspp.Value());
    assert(reparsedZeroSeparationMspp.HasValue());
    assert(reparsedZeroSeparationMspp.Value().minItemSeparation == 0.0);

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

    const auto maxItemIdSpp = importer.ImportSPInstanceJson(
        R"json({"name":"max-item-id","items":[{"id":18446744073709551615,"demand":1,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"rectangle","data":{"x_min":0,"y_min":0,"width":1,"height":1}}}],"strip_height":10})json");
    assert(maxItemIdSpp.HasValue());
    assert(maxItemIdSpp.Value().GetItem(0).ExternalId() ==
           std::numeric_limits<jaguar::ItemId>::max());

    jaguar::ExternalDemandItem duplicatePositive{};
    duplicatePositive.item.id = 6601;
    duplicatePositive.demand = 1;
    jaguar::ExternalDemandItem duplicateZero = duplicatePositive;
    duplicateZero.demand = 0;
    const std::vector<jaguar::ExternalDemandItem> duplicateItemIds{
        duplicatePositive, duplicateZero};
    jaguar::ExternalSPInstance duplicateIdSpp{};
    duplicateIdSpp.items = duplicateItemIds;
    const auto rejectedDuplicateIdSpp =
        importer.ImportSPInstance(duplicateIdSpp);
    assert(!rejectedDuplicateIdSpp.HasValue());
    assert(rejectedDuplicateIdSpp.GetError().code ==
           jaguar::ErrorCode::InvalidInput);
    jaguar::ExternalBPInstance duplicateIdBpp{};
    duplicateIdBpp.items = duplicateItemIds;
    const auto rejectedDuplicateIdBpp =
        importer.ImportBPInstance(duplicateIdBpp);
    assert(!rejectedDuplicateIdBpp.HasValue());
    assert(rejectedDuplicateIdBpp.GetError().code ==
           jaguar::ErrorCode::InvalidInput);
    jaguar::ExternalMSPInstance duplicateIdMspp{};
    duplicateIdMspp.items = duplicateItemIds;
    const auto rejectedDuplicateIdMspp =
        importer.ImportMSPInstance(duplicateIdMspp);
    assert(!rejectedDuplicateIdMspp.HasValue());
    assert(rejectedDuplicateIdMspp.GetError().code ==
           jaguar::ErrorCode::InvalidInput);

    const auto rejectedHoleItemSpp = importer.ImportSPInstanceJson(
        R"json({"name":"hole-item","items":[{"id":6701,"demand":1,"orientation":{"rotation":{"mode":"discrete","angles":[0]}},"shape":{"type":"polygon","data":{"outer":[[0,0],[10,0],[10,10],[0,10]],"inner":[[[4,4],[6,4],[6,6],[4,6]]]}}}],"strip_height":10})json");
    assert(!rejectedHoleItemSpp.HasValue());
    assert(rejectedHoleItemSpp.GetError().code ==
           jaguar::ErrorCode::UnsupportedShape);

    const auto overflowingDemandSpp = importer.ImportSPInstanceJson(
        R"json({
            "name": "overflowing-demand-spp",
            "items": [
                {"id": 701, "demand": 18446744073709551615,
                 "orientation": {"rotation": {"mode": "discrete", "angles": [0]}},
                 "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 1, "height": 1}}},
                {"id": 702, "demand": 1,
                 "orientation": {"rotation": {"mode": "discrete", "angles": [0]}},
                 "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 1, "height": 1}}}
            ],
            "strip_height": 1
        })json");
    assert(overflowingDemandSpp.HasValue());

    const auto wideRotationInstance = importer.ImportSPInstanceJson(
        R"json({"name":"wide-rotation","items":[{"id":6206,"demand":1,"orientation":{"rotation":{"mode":"continuous"}},"shape":{"type":"rectangle","data":{"x_min":0,"y_min":0,"width":1,"height":1}}}],"strip_height":10})json");
    assert(wideRotationInstance.HasValue());
    jaguar::ExternalSPSolution wideRotationExternal{};
    wideRotationExternal.stripWidth = 10.0;
    wideRotationExternal.layout.placedItems.push_back(
        {6206, {869592.625, {5.0, 5.0}}});
    const auto wideRotationSolution = importer.ImportSPSolution(
        wideRotationInstance.Value(), wideRotationExternal);
    assert(wideRotationSolution.HasValue());
    const float wideRotation = static_cast<float>(wideRotationSolution.Value()
                                                      .layoutSnapshot.placements.front()
                                                      .placedItem.Transform().rotationRad);
    std::uint32_t wideRotationBits = 0;
    std::memcpy(&wideRotationBits, &wideRotation, sizeof(wideRotationBits));
    assert(wideRotationBits == 0xc03afeb4U);
    const auto exportedWideRotationSolution =
        jaguar::ExternalSPSolutionFromSolution(
            wideRotationInstance.Value(), wideRotationSolution.Value());
    assert(exportedWideRotationSolution.HasValue());
    const float exportedWideRotation = static_cast<float>(
        exportedWideRotationSolution.Value()
            .layout.placedItems.front().transformation.rotationDegrees);
    std::uint32_t exportedWideRotationBits = 0;
    std::memcpy(&exportedWideRotationBits, &exportedWideRotation,
                sizeof(exportedWideRotationBits));
    assert(exportedWideRotationBits == 0xc3276815U);

    jaguar::ExternalSPSolution roundtripProbeExternal{};
    roundtripProbeExternal.stripWidth = 10.0;
    const std::uint32_t roundtripProbeInputBits = 0xc1fa7be4U;
    float roundtripProbeInputDegrees = 0.0F;
    std::memcpy(&roundtripProbeInputDegrees, &roundtripProbeInputBits,
                sizeof(roundtripProbeInputDegrees));
    roundtripProbeExternal.layout.placedItems.push_back(
        {6206, {static_cast<double>(roundtripProbeInputDegrees), {5.0, 5.0}}});
    const auto roundtripProbeSolution = importer.ImportSPSolution(
        wideRotationInstance.Value(), roundtripProbeExternal);
    assert(roundtripProbeSolution.HasValue());
    const float roundtripProbeInternalRadians = static_cast<float>(
        roundtripProbeSolution.Value()
            .layoutSnapshot.placements.front()
            .placedItem.Transform()
            .rotationRad);
    std::uint32_t roundtripProbeInternalBits = 0;
    std::memcpy(&roundtripProbeInternalBits, &roundtripProbeInternalRadians,
                sizeof(roundtripProbeInternalBits));
    assert(roundtripProbeInternalBits == 0xbf0be589U);
    const auto& roundtripProbePlaced = roundtripProbeSolution.Value()
                                           .layoutSnapshot.placements.front()
                                           .placedItem;
    const float roundtripProbeInternalX = static_cast<float>(
        roundtripProbePlaced.Transform().translation.x);
    const float roundtripProbeInternalY = static_cast<float>(
        roundtripProbePlaced.Transform().translation.y);
    std::uint32_t roundtripProbeInternalXBits = 0;
    std::uint32_t roundtripProbeInternalYBits = 0;
    std::memcpy(&roundtripProbeInternalXBits, &roundtripProbeInternalX,
                sizeof(roundtripProbeInternalXBits));
    std::memcpy(&roundtripProbeInternalYBits, &roundtripProbeInternalY,
                sizeof(roundtripProbeInternalYBits));
    assert(roundtripProbeInternalXBits == 0x40b5fc10U);
    assert(roundtripProbeInternalYBits == 0x40a55ae2U);
    const auto exportedRoundtripProbe = jaguar::ExternalSPSolutionFromSolution(
        wideRotationInstance.Value(), roundtripProbeSolution.Value());
    assert(exportedRoundtripProbe.HasValue());
    const float roundtripProbeDegrees = static_cast<float>(
        exportedRoundtripProbe.Value()
            .layout.placedItems.front().transformation.rotationDegrees);
    const float roundtripProbeExternalX = static_cast<float>(
        exportedRoundtripProbe.Value()
            .layout.placedItems.front().transformation.translation.x);
    const float roundtripProbeExternalY = static_cast<float>(
        exportedRoundtripProbe.Value()
            .layout.placedItems.front().transformation.translation.y);
    std::uint32_t roundtripProbeBits = 0;
    std::uint32_t roundtripProbeExternalXBits = 0;
    std::uint32_t roundtripProbeExternalYBits = 0;
    std::memcpy(&roundtripProbeBits, &roundtripProbeDegrees,
                sizeof(roundtripProbeBits));
    std::memcpy(&roundtripProbeExternalXBits, &roundtripProbeExternalX,
                sizeof(roundtripProbeExternalXBits));
    std::memcpy(&roundtripProbeExternalYBits, &roundtripProbeExternalY,
                sizeof(roundtripProbeExternalYBits));
    assert(roundtripProbeBits == 0xc1fa7be4U);
    assert(roundtripProbeExternalXBits == 0x40a00000U);
    assert(roundtripProbeExternalYBits == 0x40a00000U);

    jaguar::Layout roundtripProbeLayout(
        roundtripProbeSolution.Value().layoutSnapshot.container);
    assert(roundtripProbeLayout
               .Restore(roundtripProbeSolution.Value().layoutSnapshot)
               .HasValue());
    const auto roundtripProbeSvg = jaguar::ExportLayoutSvg(roundtripProbeLayout);
    assert(roundtripProbeSvg.HasValue());
    const std::string rotationAttribute = "data-rotation-degrees=\"";
    const std::size_t rotationStart =
        roundtripProbeSvg.Value().find(rotationAttribute);
    assert(rotationStart != std::string::npos);
    const std::size_t rotationValueStart = rotationStart + rotationAttribute.size();
    const std::size_t rotationValueEnd =
        roundtripProbeSvg.Value().find('"', rotationValueStart);
    assert(rotationValueEnd != std::string::npos);
    const float roundtripProbeSvgDegrees = std::stof(
        roundtripProbeSvg.Value().substr(
            rotationValueStart, rotationValueEnd - rotationValueStart));
    std::uint32_t roundtripProbeSvgBits = 0;
    std::memcpy(&roundtripProbeSvgBits, &roundtripProbeSvgDegrees,
                sizeof(roundtripProbeSvgBits));
    assert(roundtripProbeSvgBits == 0xc1fa7be4U);

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
    // jagua-rs export is a pure function over the solution struct; a strip
    // width that disagrees with the layout container is exported as-is.
    const auto acceptedSppExport = jaguar::ExternalSPSolutionFromSolution(
        importedSpp.Value(), invalidExportedSppSolution);
    assert(acceptedSppExport.HasValue());
    assert(std::abs(acceptedSppExport.Value().stripWidth -
                    invalidExportedSppSolution.strip.width) < 1.0e-12);
    const auto unknownSppItem = importer.ImportSPSolutionJson(
        importedSpp.Value(),
        R"json({"strip_width": 4, "layout": {"container_id": 0, "placed_items": [{"item_id": 999, "transformation": {"rotation": 0, "translation": [1, 1]}}], "density": 0}, "density": 0, "run_time_sec": 0})json");
    assert(!unknownSppItem.HasValue());
    assert(unknownSppItem.GetError().code == jaguar::ErrorCode::SnapshotMismatch);
    const auto overDemandSpp = importer.ImportSPSolutionJson(
        importedSpp.Value(),
        R"json({"strip_width": 4, "layout": {"container_id": 0, "placed_items": [{"item_id": 201, "transformation": {"rotation": 0, "translation": [1, 1]}}, {"item_id": 201, "transformation": {"rotation": 0, "translation": [3, 1]}}], "density": 0}, "density": 0, "run_time_sec": 0})json");
#ifdef JAGUARCORE_TEST_RELEASE
    assert(overDemandSpp.HasValue());
    assert(overDemandSpp.Value().layoutSnapshot.placements.size() == 2);
#else
    assert(!overDemandSpp.HasValue());
    assert(overDemandSpp.GetError().code == jaguar::ErrorCode::Infeasible);
#endif

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
    assert(!importedBppSolution.HasValue());
    assert(importedBppSolution.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    const auto malformedBppSolution =
        importer.ImportBPSolutionJson(importedBpp.Value(), "{");
    assert(!malformedBppSolution.HasValue());
    assert(malformedBppSolution.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);

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

    auto sparseMsppProblem = jaguar::MSPProblem::Create(importedMspp.Value());
    assert(sparseMsppProblem.HasValue());
    const auto removedMsppLayout = sparseMsppProblem.Value().AddLayout(
        importedMspp.Value().BaseStrip());
    const auto activeMsppLayout = sparseMsppProblem.Value().AddLayout(
        importedMspp.Value().BaseStrip());
    assert(removedMsppLayout.HasValue() && activeMsppLayout.HasValue());
    assert(sparseMsppProblem.Value()
               .RemoveLayout(removedMsppLayout.Value())
               .HasValue());
    const auto sparseMsppExport = jaguar::ExternalMSPSolutionFromSolution(
        importedMspp.Value(), sparseMsppProblem.Value().Save());
    assert(sparseMsppExport.HasValue());
    assert(sparseMsppExport.Value().layouts.size() == 1);
    assert(sparseMsppExport.Value().containers.size() == 1);
    assert(sparseMsppExport.Value().layouts.front().containerId == 0);
    assert(sparseMsppExport.Value().containers.front().id == 0);

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
    assert(std::abs(separatedSpp.Value().GetItem(0)
                        .CollisionShape().BoundingBox().Width() - 4.0) <
           1.0e-9);

    const auto rejectedNegativeSeparation = importer.ImportSPInstanceJson(
        R"json({"name":"negative-separation","min_item_separation":-1,"items":[],"strip_height":1})json");
    assert(!rejectedNegativeSeparation.HasValue());
    assert(rejectedNegativeSeparation.GetError().code ==
           jaguar::ErrorCode::InvalidInput);
    jaguar::ExternalSPInstance nanSeparation{};
    nanSeparation.minItemSeparation =
        std::numeric_limits<double>::quiet_NaN();
    const auto rejectedNanSeparation = importer.ImportSPInstance(nanSeparation);
    assert(!rejectedNanSeparation.HasValue());
    assert(rejectedNanSeparation.GetError().code ==
           jaguar::ErrorCode::InvalidInput);
    jaguar::ExternalSPInstance infiniteSeparation{};
    infiniteSeparation.minItemSeparation =
        std::numeric_limits<double>::infinity();
    const auto rejectedInfiniteSeparation =
        importer.ImportSPInstance(infiniteSeparation);
    assert(!rejectedInfiniteSeparation.HasValue());
    assert(rejectedInfiniteSeparation.GetError().code ==
           jaguar::ErrorCode::InvalidInput);

    jaguar::ExternalBPInstance invalidSeparationBpp{};
    invalidSeparationBpp.minItemSeparation = -1.0;
    const auto rejectedNegativeSeparationBpp =
        importer.ImportBPInstance(invalidSeparationBpp);
    assert(!rejectedNegativeSeparationBpp.HasValue());
    assert(rejectedNegativeSeparationBpp.GetError().code ==
           jaguar::ErrorCode::InvalidInput);
    jaguar::ExternalMSPInstance invalidSeparationMspp{};
    invalidSeparationMspp.minItemSeparation = -1.0;
    const auto rejectedNegativeSeparationMspp =
        importer.ImportMSPInstance(invalidSeparationMspp);
    assert(!rejectedNegativeSeparationMspp.HasValue());
    assert(rejectedNegativeSeparationMspp.GetError().code ==
           jaguar::ErrorCode::InvalidInput);

    jaguar::ExternalSPInstance outOfRangeSeparation{};
    outOfRangeSeparation.name = "f32-overflow-separation";
    outOfRangeSeparation.minItemSeparation = 1.0e39;
    jaguar::ExternalItem outOfRangeSeparationItem{};
    outOfRangeSeparationItem.id = 602;
    outOfRangeSeparationItem.rotation = jaguar::RotationPolicy::None();
    outOfRangeSeparationItem.shape = jaguar::ExternalShape::Rectangle(
        0.0, 0.0, 2.0, 2.0);
    outOfRangeSeparation.items.push_back({outOfRangeSeparationItem, 1});
    outOfRangeSeparation.stripHeight = 1.0e40;
    const auto rejectedOutOfRangeSeparation =
        importer.ImportSPInstance(outOfRangeSeparation);
    assert(!rejectedOutOfRangeSeparation.HasValue());
    assert(rejectedOutOfRangeSeparation.GetError().code ==
           jaguar::ErrorCode::InvalidInput);
    assert(rejectedOutOfRangeSeparation.GetError().message ==
           "minimum item separation must be finite and nonnegative");

    jaguar::ExternalItem separatedItem{};
    separatedItem.id = 6801;
    separatedItem.rotation = jaguar::RotationPolicy::Discrete({0.0});
    separatedItem.shape = jaguar::ExternalShape::Rectangle(0.0, 0.0, 2.0, 2.0);
    jaguar::ExternalDemandItem separatedDemand{separatedItem, 1};
    jaguar::ExternalBPInstance separatedBppInput{};
    separatedBppInput.minItemSeparation = 2.0;
    separatedBppInput.items.push_back(separatedDemand);
    jaguar::ExternalBin separatedBin{};
    separatedBin.container.id = 6802;
    separatedBin.container.shape = jaguar::ExternalShape::Rectangle(
        0.0, 0.0, 10.0, 10.0);
    separatedBin.stock = 1;
    separatedBppInput.bins.push_back(separatedBin);
    const auto separatedBpp = importer.ImportBPInstance(separatedBppInput);
    assert(separatedBpp.HasValue());
    assert(std::abs(separatedBpp.Value().GetItem(0)
                        .CollisionShape().BoundingBox().Width() - 4.0) <
           1.0e-9);
    assert(separatedBpp.Value().GetBin(0)
               .container.GetOriginalShape().modifyConfig.offset.has_value());
    assert(std::abs(*separatedBpp.Value().GetBin(0)
                         .container.GetOriginalShape().modifyConfig.offset - 1.0) <
           1.0e-12);

    jaguar::ExternalMSPInstance separatedMsppInput{};
    separatedMsppInput.minItemSeparation = 2.0;
    separatedMsppInput.items.push_back(separatedDemand);
    separatedMsppInput.strips = {4.0, 10.0};
    const auto separatedMspp = importer.ImportMSPInstance(separatedMsppInput);
    assert(separatedMspp.HasValue());
    assert(std::abs(separatedMspp.Value().GetItem(0)
                        .CollisionShape().BoundingBox().Width() - 4.0) <
           1.0e-9);
    assert(separatedMspp.Value().BaseStrip().shapeModifyConfig.offset.has_value());
    assert(std::abs(*separatedMspp.Value().BaseStrip().shapeModifyConfig.offset - 1.0) <
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
    assert(!importedMsppSolution.HasValue());
    assert(importedMsppSolution.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    const auto malformedMsppSolution =
        importer.ImportMSPSolutionJson(importedMspp.Value(), "{");
    assert(!malformedMsppSolution.HasValue());
    assert(malformedMsppSolution.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    const auto mismatchedMspp = importer.ImportMSPSolutionJson(
        importedMspp.Value(),
        R"json({"layouts": [], "containers": [{"id": 0, "shape": {"type": "rectangle", "data": {"x_min": 0, "y_min": 0, "width": 10, "height": 5}}}], "density": 0, "run_time_sec": 0})json");
    assert(!mismatchedMspp.HasValue());
    assert(mismatchedMspp.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);

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
