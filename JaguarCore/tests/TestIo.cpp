#include "JaguarCore/IO/ExternalRepr.h"
#include "JaguarCore/IO/Importer.h"

#include <cassert>
#include <cmath>
#include <string>

namespace
{
jaguar::ExternalShape Rectangle(double xMin,
                                double yMin,
                                double width,
                                double height)
{
    return jaguar::ExternalShape::Rectangle(xMin, yMin, width, height);
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
    itemInput.shape = Rectangle(10.0, 20.0, 2.0, 4.0);
    itemInput.rotation = jaguar::RotationPolicy::Discrete({-90.0, 270.0, 0.0, 360.0});
    itemInput.minQuality = 2;

    const auto importedItem = makeImporter().ImportItem(itemInput, 3);
    assert(importedItem.HasValue());
    assert(importedItem.Value().Index() == 3);
    assert(importedItem.Value().ExternalId() == 7001);
    assert(importedItem.Value().MinQuality() == 2);
    assert(importedItem.Value().AllowedRotation().mode == jaguar::RotationMode::Discrete);
    assert(importedItem.Value().AllowedRotation().radians.size() == 2);
    assert(std::abs(importedItem.Value().CollisionShape().Centroid().x) < 1.0e-9);
    assert(std::abs(importedItem.Value().CollisionShape().Centroid().y) < 1.0e-9);

    jaguar::ExternalItem stepped = itemInput;
    stepped.rotation = jaguar::RotationPolicy::Stepped(90.0);
    const auto steppedItem = makeImporter().ImportItem(stepped, 4);
    assert(steppedItem.HasValue());
    assert(steppedItem.Value().AllowedRotation().radians.size() == 4);

    jaguar::ExternalItem continuous = itemInput;
    continuous.rotation = jaguar::RotationPolicy::Continuous();
    const auto continuousItem = makeImporter().ImportItem(continuous, 5);
    assert(continuousItem.HasValue());
    assert(continuousItem.Value().AllowedRotation().mode ==
           jaguar::RotationMode::Continuous);

    jaguar::ExternalItem invalidRotation = itemInput;
    invalidRotation.rotation = jaguar::RotationPolicy::Discrete({});
    const auto invalidRotationResult = makeImporter().ImportItem(invalidRotation, 6);
    assert(!invalidRotationResult.HasValue());
    assert(invalidRotationResult.GetError().code == jaguar::ErrorCode::InvalidInput);

    jaguar::ExternalItem invalidStep = itemInput;
    invalidStep.rotation = jaguar::RotationPolicy::Stepped(7.0);
    const auto invalidStepResult = makeImporter().ImportItem(invalidStep, 7);
    assert(!invalidStepResult.HasValue());
    assert(invalidStepResult.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto hole = std::vector<jaguar::Point2d>{
        {4.0, 4.0}, {6.0, 4.0}, {6.0, 6.0}, {4.0, 6.0}};
    jaguar::ExternalItem itemWithHole = itemInput;
    itemWithHole.shape = jaguar::ExternalShape::Polygon(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, {hole});
    const auto holeItemResult = makeImporter().ImportItem(itemWithHole, 8);
    assert(!holeItemResult.HasValue());
    assert(holeItemResult.GetError().code == jaguar::ErrorCode::UnsupportedShape);

    jaguar::ExternalContainer containerInput{};
    containerInput.id = 8001;
    containerInput.shape = jaguar::ExternalShape::Polygon(
        {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}}, {hole});
    containerInput.zones.push_back({1, Rectangle(6.0, 0.0, 4.0, 4.0)});
    const auto importedContainer = makeImporter().ImportContainer(containerInput);
    assert(importedContainer.HasValue());
    assert(importedContainer.Value().Id() == 8001);

    const auto containerEngine = importedContainer.Value().CreateCollisionEngine();
    assert(containerEngine != nullptr);
    const auto holeProbe = jaguar::SimplePolygon::Create({
        {4.5, 4.5}, {5.5, 4.5}, {5.5, 5.5}, {4.5, 5.5}});
    assert(holeProbe.HasValue());
    assert(containerEngine->DetectPolygonCollision(holeProbe.Value(), jaguar::NoFilter{}));

    const auto outsideOriginalHole = jaguar::SimplePolygon::Create({
        {3.2, 4.5}, {3.8, 4.5}, {3.8, 5.5}, {3.2, 5.5}});
    assert(outsideOriginalHole.HasValue());
    assert(!containerEngine->DetectPolygonCollision(
        outsideOriginalHole.Value(), jaguar::NoFilter{}));

    const auto separatedContainer = makeImporter(2.0).ImportContainer(containerInput);
    assert(separatedContainer.HasValue());
    const auto separatedContainerEngine =
        separatedContainer.Value().CreateCollisionEngine();
    assert(separatedContainerEngine != nullptr);
    assert(separatedContainerEngine->DetectPolygonCollision(
        outsideOriginalHole.Value(), jaguar::NoFilter{}));

    const auto nearOriginalOuter = jaguar::SimplePolygon::Create({
        {0.2, 0.2}, {0.8, 0.2}, {0.8, 0.8}, {0.2, 0.8}});
    assert(nearOriginalOuter.HasValue());
    assert(!containerEngine->DetectPolygonCollision(
        nearOriginalOuter.Value(), jaguar::NoFilter{}));
    assert(separatedContainerEngine->DetectPolygonCollision(
        nearOriginalOuter.Value(), jaguar::NoFilter{}));

    const auto separatedItem = makeImporter(2.0).ImportItem(
        {9001, jaguar::RotationPolicy::None(), Rectangle(0.0, 0.0, 2.0, 2.0), std::nullopt},
        9);
    assert(separatedItem.HasValue());
    assert(std::abs(separatedItem.Value().CollisionShape().BoundingBox().Width() - 4.0) <
           1.0e-9);

    const std::string itemJson = R"json({
        "id": 9101,
        "orientation": {"rotation": {"mode": "discrete", "angles": [0, 180]}},
        "shape": {"type": "rectangle", "data": {
            "x_min": 0, "y_min": 0, "width": 4, "height": 2
        }},
        "min_quality": 1
    })json";
    const auto importedItemJson = makeImporter().ImportItemJson(itemJson, 12);
    assert(importedItemJson.HasValue());
    assert(importedItemJson.Value().Index() == 12);
    assert(importedItemJson.Value().ExternalId() == 9101);
    assert(importedItemJson.Value().MinQuality() == 1);

    const std::string containerJson = R"json({
        "id": 9201,
        "shape": {"type": "polygon", "data": {
            "outer": [[0,0],[20,0],[20,10],[0,10]],
            "inner": [[[4,2],[6,2],[6,4],[4,4]]]
        }}
    })json";
    const auto importedContainerJson =
        makeImporter().ImportContainerJson(containerJson);
    assert(importedContainerJson.HasValue());
    assert(importedContainerJson.Value().Id() == 9201);

    const auto invalidJson = makeImporter().ImportItemJson("{", 13);
    assert(!invalidJson.HasValue());
    assert(invalidJson.GetError().code == jaguar::ErrorCode::SerializationFailure);
}
