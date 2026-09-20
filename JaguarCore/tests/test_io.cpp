#include "JaguarCore/io/external_repr.h"
#include "JaguarCore/io/importer.h"

#include <cassert>
#include <cmath>
#include <string>

namespace
{
jaguar::ExternalShape rectangle(double xMin,
                                double yMin,
                                double width,
                                double height)
{
    return jaguar::ExternalShape::rectangle(xMin, yMin, width, height);
}

jaguar::Importer makeImporter(double separation = 0.0)
{
    jaguar::ImporterConfig config{};
    config.minItemSeparation = separation;
    return jaguar::Importer(config);
}
}

void runIoTests()
{
    jaguar::ExternalItem itemInput{};
    itemInput.id = 7001;
    itemInput.shape = rectangle(10.0, 20.0, 2.0, 4.0);
    itemInput.rotation = jaguar::RotationPolicy::discrete({-90.0, 270.0, 0.0, 360.0});
    itemInput.minQuality = 2;

    const auto importedItem = makeImporter().importItem(itemInput, 3);
    assert(importedItem.hasValue());
    assert(importedItem.value().index() == 3);
    assert(importedItem.value().externalId() == 7001);
    assert(importedItem.value().minQuality() == 2);
    assert(importedItem.value().allowedRotation().mode == jaguar::RotationMode::Discrete);
    assert(importedItem.value().allowedRotation().radians.size() == 2);
    assert(std::abs(importedItem.value().collisionShape().centroid().x) < 1.0e-9);
    assert(std::abs(importedItem.value().collisionShape().centroid().y) < 1.0e-9);

    jaguar::ExternalItem stepped = itemInput;
    stepped.rotation = jaguar::RotationPolicy::stepped(90.0);
    const auto steppedItem = makeImporter().importItem(stepped, 4);
    assert(steppedItem.hasValue());
    assert(steppedItem.value().allowedRotation().radians.size() == 4);

    jaguar::ExternalItem continuous = itemInput;
    continuous.rotation = jaguar::RotationPolicy::continuous();
    const auto continuousItem = makeImporter().importItem(continuous, 5);
    assert(continuousItem.hasValue());
    assert(continuousItem.value().allowedRotation().mode ==
           jaguar::RotationMode::Continuous);

    jaguar::ExternalItem invalidRotation = itemInput;
    invalidRotation.rotation = jaguar::RotationPolicy::discrete({});
    const auto invalidRotationResult = makeImporter().importItem(invalidRotation, 6);
    assert(!invalidRotationResult.hasValue());
    assert(invalidRotationResult.error().code == jaguar::ErrorCode::InvalidInput);

    jaguar::ExternalItem invalidStep = itemInput;
    invalidStep.rotation = jaguar::RotationPolicy::stepped(7.0);
    const auto invalidStepResult = makeImporter().importItem(invalidStep, 7);
    assert(!invalidStepResult.hasValue());
    assert(invalidStepResult.error().code == jaguar::ErrorCode::InvalidInput);

    const auto hole = std::vector<jaguar::Point2d>{
        {4.0, 4.0}, {6.0, 4.0}, {6.0, 6.0}, {4.0, 6.0}};
    jaguar::ExternalItem itemWithHole = itemInput;
    itemWithHole.shape = jaguar::ExternalShape::polygon(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, {hole});
    const auto holeItemResult = makeImporter().importItem(itemWithHole, 8);
    assert(!holeItemResult.hasValue());
    assert(holeItemResult.error().code == jaguar::ErrorCode::UnsupportedShape);

    jaguar::ExternalContainer containerInput{};
    containerInput.id = 8001;
    containerInput.shape = jaguar::ExternalShape::polygon(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, {hole});
    containerInput.zones.push_back({1, rectangle(6.0, 0.0, 4.0, 4.0)});
    const auto importedContainer = makeImporter().importContainer(containerInput);
    assert(importedContainer.hasValue());
    assert(importedContainer.value().id() == 8001);

    const auto containerEngine = importedContainer.value().createCollisionEngine();
    assert(containerEngine != nullptr);
    const auto holeProbe = jaguar::SimplePolygon::create({
        {4.5, 4.5}, {5.5, 4.5}, {5.5, 5.5}, {4.5, 5.5}});
    assert(holeProbe.hasValue());
    assert(containerEngine->detectPolygonCollision(holeProbe.value(), jaguar::NoFilter{}));

    const auto outsideOriginalHole = jaguar::SimplePolygon::create({
        {3.2, 4.5}, {3.8, 4.5}, {3.8, 5.5}, {3.2, 5.5}});
    assert(outsideOriginalHole.hasValue());
    assert(!containerEngine->detectPolygonCollision(
        outsideOriginalHole.value(), jaguar::NoFilter{}));

    const auto separatedContainer = makeImporter(2.0).importContainer(containerInput);
    assert(separatedContainer.hasValue());
    const auto separatedContainerEngine =
        separatedContainer.value().createCollisionEngine();
    assert(separatedContainerEngine != nullptr);
    assert(separatedContainerEngine->detectPolygonCollision(
        outsideOriginalHole.value(), jaguar::NoFilter{}));

    const auto nearOriginalOuter = jaguar::SimplePolygon::create({
        {0.2, 0.2}, {0.8, 0.2}, {0.8, 0.8}, {0.2, 0.8}});
    assert(nearOriginalOuter.hasValue());
    assert(!containerEngine->detectPolygonCollision(
        nearOriginalOuter.value(), jaguar::NoFilter{}));
    assert(separatedContainerEngine->detectPolygonCollision(
        nearOriginalOuter.value(), jaguar::NoFilter{}));

    const auto separatedItem = makeImporter(2.0).importItem(
        {9001, jaguar::RotationPolicy::none(), rectangle(0.0, 0.0, 2.0, 2.0), std::nullopt},
        9);
    assert(separatedItem.hasValue());
    assert(std::abs(separatedItem.value().collisionShape().boundingBox().width() - 4.0) <
           1.0e-9);

    const std::string itemJson = R"json({
        "id": 9101,
        "orientation": {"rotation": {"mode": "discrete", "angles": [0, 180]}},
        "shape": {"type": "rectangle", "data": {
            "x_min": 0, "y_min": 0, "width": 4, "height": 2
        }},
        "min_quality": 1
    })json";
    const auto importedItemJson = makeImporter().importItemJson(itemJson, 12);
    assert(importedItemJson.hasValue());
    assert(importedItemJson.value().index() == 12);
    assert(importedItemJson.value().externalId() == 9101);
    assert(importedItemJson.value().minQuality() == 1);

    const std::string containerJson = R"json({
        "id": 9201,
        "shape": {"type": "polygon", "data": {
            "outer": [[0,0],[20,0],[20,10],[0,10]],
            "inner": [[[4,2],[6,2],[6,4],[4,4]]]
        }}
    })json";
    const auto importedContainerJson =
        makeImporter().importContainerJson(containerJson);
    assert(importedContainerJson.hasValue());
    assert(importedContainerJson.value().id() == 9201);

    const auto invalidJson = makeImporter().importItemJson("{", 13);
    assert(!invalidJson.hasValue());
    assert(invalidJson.error().code == jaguar::ErrorCode::SerializationFailure);
}
