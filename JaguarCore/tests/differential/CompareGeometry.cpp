#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/ShapeModification.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace
{
std::string loadReference(const char* fileName)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + fileName})
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>());
    }
    return {};
}
}

void runGeometryDifferentialTests()
{
    const auto rectangle = jaguar::Rect2d::Create(-2.0, -1.0, 3.0, 4.0);
    assert(rectangle.HasValue());
    assert(std::abs(rectangle.Value().Width() - 5.0) < 1.0e-12);
    assert(std::abs(rectangle.Value().Height() - 5.0) < 1.0e-12);
    assert(std::abs(rectangle.Value().Area() - 25.0) < 1.0e-12);

    const auto edge = jaguar::Edge2d::Create({0.0, 0.0}, {3.0, 4.0});
    assert(edge.HasValue());
    assert(std::abs(edge.Value().Length() - 5.0) < 1.0e-12);

    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 3.0}, {0.0, 3.0}});
    assert(polygon.HasValue());
    assert(std::abs(polygon.Value().Area() - 12.0) < 1.0e-12);
    assert(std::abs(polygon.Value().Centroid().x - 2.0) < 1.0e-12);
    assert(std::abs(polygon.Value().Centroid().y - 1.5) < 1.0e-12);

    const auto offsetSource = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}});
    assert(offsetSource.HasValue());
    const auto inflated = jaguar::OffsetShape(
        offsetSource.Value(), jaguar::ShapeModifyMode::Inflate, 1.0);
    assert(inflated.HasValue());
    assert(inflated.Value().VertexCount() == 68);
    assert(std::abs(inflated.Value().Area() - 15.1364769) < 1.0e-3);
    const auto deflated = jaguar::OffsetShape(
        offsetSource.Value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(deflated.HasValue());
    assert(std::abs(deflated.Value().Area() - 2.25) < 1.0e-9);

    const auto concave = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 9.9}, {4.0, 9.9}, {4.0, 10.0}, {0.0, 10.0}});
    assert(concave.HasValue());
    const auto closed = jaguar::CloseNarrowConcavities(
        concave.Value(), jaguar::ShapeModifyMode::Inflate, {0.2, 0.01});
    assert(closed.VertexCount() < concave.Value().VertexCount());
    assert(jaguar::ShapeModificationValid(
        concave.Value(), closed, jaguar::ShapeModifyMode::Inflate));
    const auto simplifiedInflate = jaguar::SimplifyShape(
        concave.Value(), jaguar::ShapeModifyMode::Inflate, 0.02);
    assert(simplifiedInflate.HasValue());
    assert(simplifiedInflate.Value().VertexCount() <= concave.Value().VertexCount());
    assert(jaguar::ShapeModificationValid(
        concave.Value(), simplifiedInflate.Value(),
        jaguar::ShapeModifyMode::Inflate));
    const auto simplifiedDeflate = jaguar::SimplifyShape(
        concave.Value(), jaguar::ShapeModifyMode::Deflate, 0.02);
    assert(simplifiedDeflate.HasValue());
    assert(simplifiedDeflate.Value().VertexCount() <= concave.Value().VertexCount());
    assert(jaguar::ShapeModificationValid(
        concave.Value(), simplifiedDeflate.Value(),
        jaguar::ShapeModifyMode::Deflate));

    const std::string fixture = loadReference("geometry_item.json");
    assert(!fixture.empty());

    const jaguar::Importer importer;
    const auto imported = importer.ImportItemJson(fixture, 4);
    assert(imported.HasValue());

    // Expected values come from the Rust f32 import and rotation semantics.
    const jaguar::Item& item = imported.Value();
    assert(item.ExternalId() == 101);
    assert(item.Index() == 4);
    assert(std::abs(item.Area() - 200.0) < 1.0e-9);
    assert(std::abs(item.CollisionShape().Centroid().x) < 1.0e-9);
    assert(std::abs(item.CollisionShape().Centroid().y) < 1.0e-9);
    assert(std::abs(item.CollisionShape().BoundingBox().xMin + 5.0) < 1.0e-9);
    assert(std::abs(item.CollisionShape().BoundingBox().xMax - 5.0) < 1.0e-9);
    assert(std::abs(item.CollisionShape().BoundingBox().yMin + 10.0) < 1.0e-9);
    assert(std::abs(item.CollisionShape().BoundingBox().yMax - 10.0) < 1.0e-9);
    assert(item.AllowedRotation().mode == jaguar::RotationMode::Discrete);
    assert(item.AllowedRotation().radians.size() == 2);
    assert(std::abs(item.AllowedRotation().radians[0]) < 1.0e-9);
    assert(std::abs(item.AllowedRotation().radians[1] - 1.5 * std::acos(-1.0)) <
           1.0e-6);

    jaguar::ExternalItem boundaryItem{};
    boundaryItem.id = 102;
    boundaryItem.shape = jaguar::ExternalShape::Rectangle(0.0, 0.0, 2.0, 2.0);
    boundaryItem.rotation = jaguar::RotationPolicy::Discrete(
        {-0.0, 360.0, -90.0, 270.0});
    const auto boundaryImport = importer.ImportItem(boundaryItem, 5);
    assert(boundaryImport.HasValue());
    assert(boundaryImport.Value().AllowedRotation().radians.size() == 2);
    assert(std::abs(boundaryImport.Value().AllowedRotation().radians[0]) < 1.0e-12);
    const double rustStoredRotation = static_cast<double>(
        static_cast<float>(1.5 * std::acos(-1.0)));
    assert(boundaryImport.Value().AllowedRotation().radians[1] == rustStoredRotation);

    jaguar::ExternalItem steppedItem = boundaryItem;
    steppedItem.rotation = jaguar::RotationPolicy::Stepped(120.0);
    const auto steppedImport = importer.ImportItem(steppedItem, 6);
    assert(steppedImport.HasValue());
    assert(steppedImport.Value().AllowedRotation().radians.size() == 3);

    jaguar::ImporterConfig separationConfig{};
    separationConfig.minItemSeparation = 2.0;
    const jaguar::Importer separatedImporter(separationConfig);
    const auto separated = separatedImporter.ImportItem(boundaryItem, 7);
    assert(separated.HasValue());
    assert(std::abs(separated.Value().CollisionShape().BoundingBox().Width() - 4.0) <
           1.0e-9);

    jaguar::ExternalItem nearlyDuplicate = boundaryItem;
    nearlyDuplicate.shape = jaguar::ExternalShape::SimplePolygon({
        {0.0, 0.0}, {1.0, 0.0}, {1.0 + 1.0e-10, 0.0},
        {1.0, 1.0}, {0.0, 1.0}});
    const auto cleaned = importer.ImportItem(nearlyDuplicate, 8);
    assert(cleaned.HasValue());
    assert(cleaned.Value().GetOriginalShape().shape.VertexCount() == 4);
    (void)item;
}

namespace
{
 jaguar::SimplePolygon rectanglePolygon(
    double xMin, double yMin, double xMax, double yMax)
{
    const auto rectangle = jaguar::SimplePolygon::Create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(rectangle.HasValue());
    return rectangle.Value();
}

std::shared_ptr<const jaguar::SimplePolygon> rectangleShape(
    double xMin, double yMin, double xMax, double yMax)
{
    return std::make_shared<const jaguar::SimplePolygon>(
        rectanglePolygon(xMin, yMin, xMax, yMax));
}
}

void runCollisionDifferentialTests()
{
    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.HasValue());
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    jaguar::CollisionEngine engine(bounds.Value(), config);

    const auto dynamic = engine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 1, 0, 0},
        rectangleShape(4.0, 4.0, 6.0, 6.0), true});
    assert(dynamic.HasValue());
    const auto colliding = jaguar::SimplePolygon::Create({
        {5.0, 5.0}, {7.0, 5.0}, {7.0, 7.0}, {5.0, 7.0}});
    assert(colliding.HasValue());
    assert(engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));
    const auto collisions = engine.CollectPolygonCollisions(colliding.Value());
    assert(collisions.size() == 1);
    assert(collisions.front().kind == jaguar::HazardKind::PlacedItem);

    const auto exterior = engine.RegisterHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0},
        rectangleShape(2.0, 2.0, 8.0, 8.0), false});
    assert(exterior.HasValue());
    const auto outside = jaguar::SimplePolygon::Create({
        {0.5, 0.5}, {1.0, 0.5}, {1.0, 1.0}, {0.5, 1.0}});
    assert(outside.HasValue());
    assert(engine.DetectPolygonCollision(outside.Value(), jaguar::NoFilter{}));
    const auto outsideCollisions = engine.CollectPolygonCollisions(outside.Value());
    assert(outsideCollisions.size() == 1);
    assert(outsideCollisions.front().kind == jaguar::HazardKind::Exterior);

    jaguar::HazardFilter filter{};
    filter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Exterior;
    };
    assert(!engine.DetectPolygonCollision(outside.Value(), filter));

    const auto snapshot = engine.Save();
    assert(engine.DeregisterHazard(dynamic.Value()).HasValue());
    assert(!engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));
    assert(engine.Restore(snapshot).HasValue());
    assert(engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));

    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    surrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto surrogateShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(surrogateShape.HasValue());
    const auto surrogate = jaguar::Surrogate::Create(
        surrogateShape.Value(), surrogateConfig);
    assert(surrogate.HasValue());
    jaguar::CollisionEngine surrogateEngine(bounds.Value(), config);
    assert(surrogateEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 2, 0, 0},
        rectangleShape(4.0, 4.0, 6.0, 6.0), true}).HasValue());
    const auto surrogateCollision = surrogateEngine.DetectSurrogateCollision(
        surrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 4.0}),
        jaguar::NoFilter{});
    assert(surrogateCollision.HasValue());
    assert(surrogateCollision.Value());
    const auto surrogateNoCollision = surrogateEngine.DetectSurrogateCollision(
        surrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0}),
        jaguar::NoFilter{});
    assert(surrogateNoCollision.HasValue());
    assert(!surrogateNoCollision.Value());

    jaguar::CDEConfig containerConfig{};
    const auto container = jaguar::Container::Create(
        7001,
        {rectanglePolygon(0.0, 0.0, 10.0, 10.0),
         jaguar::Transform2d::Identity(), jaguar::ShapeModifyMode::Deflate, {}},
        {{0, {rectanglePolygon(4.0, 4.0, 6.0, 6.0)}},
         {1, {rectanglePolygon(0.0, 0.0, 3.0, 3.0)}},
         {2, {rectanglePolygon(7.0, 0.0, 10.0, 3.0)}}},
        containerConfig);
    assert(container.HasValue());
    const auto staticEngine = container.Value().CreateCollisionEngine();
    assert(staticEngine != nullptr);

    const auto holeProbe = jaguar::SimplePolygon::Create({
        {4.5, 4.5}, {5.5, 4.5}, {5.5, 5.5}, {4.5, 5.5}});
    const auto qualityOneProbe = jaguar::SimplePolygon::Create({
        {1.0, 1.0}, {2.0, 1.0}, {2.0, 2.0}, {1.0, 2.0}});
    const auto qualityTwoProbe = jaguar::SimplePolygon::Create({
        {8.0, 1.0}, {9.0, 1.0}, {9.0, 2.0}, {8.0, 2.0}});
    assert(holeProbe.HasValue() && qualityOneProbe.HasValue() &&
           qualityTwoProbe.HasValue());
    const auto holeHazards = staticEngine->CollectPolygonCollisions(
        holeProbe.Value());
    assert(holeHazards.size() == 1);
    assert(holeHazards.front().kind == jaguar::HazardKind::Hole);
    const auto qualityOneHazards = staticEngine->CollectPolygonCollisions(
        qualityOneProbe.Value());
    assert(qualityOneHazards.size() == 1);
    assert(qualityOneHazards.front().kind == jaguar::HazardKind::InferiorQualityZone);
    assert(qualityOneHazards.front().quality == 1);
    const auto qualityTwoHazards = staticEngine->CollectPolygonCollisions(
        qualityTwoProbe.Value());
    assert(qualityTwoHazards.size() == 1);
    assert(qualityTwoHazards.front().kind == jaguar::HazardKind::InferiorQualityZone);
    assert(qualityTwoHazards.front().quality == 2);

    jaguar::ExternalItem qualityItem{};
    qualityItem.id = 7002;
    qualityItem.rotation = jaguar::RotationPolicy::None();
    qualityItem.shape = jaguar::ExternalShape::Rectangle(0.0, 0.0, 2.0, 2.0);
    qualityItem.minQuality = 2;
    const jaguar::Importer qualityImporter;
    const auto importedQualityItem = qualityImporter.ImportItem(qualityItem, 0);
    assert(importedQualityItem.HasValue());

    jaguar::Layout holeLayout(container.Value());
    assert(holeLayout.PlaceItem(
        importedQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 5.0})).HasValue());
    assert(!holeLayout.IsFeasible());

    jaguar::Layout inferiorLayout(container.Value());
    assert(inferiorLayout.PlaceItem(
        importedQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.5, 1.5})).HasValue());
    // jagua-rs Layout::is_feasible filters the item's own hazard only, so an
    // inferior-quality zone remains a collision for this placed item.
    assert(!inferiorLayout.IsFeasible());

    jaguar::Layout acceptableLayout(container.Value());
    assert(acceptableLayout.PlaceItem(
        importedQualityItem.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {8.5, 1.5})).HasValue());
    assert(!acceptableLayout.IsFeasible());

    jaguar::SurrogateConfig qualitySurrogateConfig{};
    qualitySurrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    qualitySurrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto qualitySurrogateShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(qualitySurrogateShape.HasValue());
    const auto qualitySurrogate = jaguar::Surrogate::Create(
        qualitySurrogateShape.Value(), qualitySurrogateConfig);
    assert(qualitySurrogate.HasValue());
    jaguar::HazardFilter qualitySurrogateFilter{};
    qualitySurrogateFilter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::InferiorQualityZone &&
               entity.quality < 2;
    };
    const auto staticNoCollision = staticEngine->DetectSurrogateCollision(
        qualitySurrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}),
        qualitySurrogateFilter);
    assert(staticNoCollision.HasValue());
    assert(!staticNoCollision.Value());
    const auto staticCollision = staticEngine->DetectSurrogateCollision(
        qualitySurrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {8.0, 1.0}),
        qualitySurrogateFilter);
    assert(staticCollision.HasValue());
    assert(staticCollision.Value());
}
