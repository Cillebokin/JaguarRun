#include "JaguarCore/io/importer.h"
#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/entities/layout.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/rect.h"
#include "JaguarCore/geometry/simple_polygon.h"
#include "JaguarCore/geometry/shape_modification.h"

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
             std::string("..\\JaguarCore\\tests\\fixtures\\rust_reference\\") + fileName,
             std::string("JaguarCore\\tests\\fixtures\\rust_reference\\") + fileName})
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
    const auto rectangle = jaguar::Rect2d::create(-2.0, -1.0, 3.0, 4.0);
    assert(rectangle.hasValue());
    assert(std::abs(rectangle.value().width() - 5.0) < 1.0e-12);
    assert(std::abs(rectangle.value().height() - 5.0) < 1.0e-12);
    assert(std::abs(rectangle.value().area() - 25.0) < 1.0e-12);

    const auto edge = jaguar::Edge2d::create({0.0, 0.0}, {3.0, 4.0});
    assert(edge.hasValue());
    assert(std::abs(edge.value().length() - 5.0) < 1.0e-12);

    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 3.0}, {0.0, 3.0}});
    assert(polygon.hasValue());
    assert(std::abs(polygon.value().area() - 12.0) < 1.0e-12);
    assert(std::abs(polygon.value().centroid().x - 2.0) < 1.0e-12);
    assert(std::abs(polygon.value().centroid().y - 1.5) < 1.0e-12);

    const auto offsetSource = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}});
    assert(offsetSource.hasValue());
    const auto inflated = jaguar::offsetShape(
        offsetSource.value(), jaguar::ShapeModifyMode::Inflate, 1.0);
    assert(inflated.hasValue());
    assert(inflated.value().vertexCount() == 68);
    assert(std::abs(inflated.value().area() - 15.1364769) < 1.0e-3);
    const auto deflated = jaguar::offsetShape(
        offsetSource.value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(deflated.hasValue());
    assert(std::abs(deflated.value().area() - 2.25) < 1.0e-9);

    const auto concave = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 9.9}, {4.0, 9.9}, {4.0, 10.0}, {0.0, 10.0}});
    assert(concave.hasValue());
    const auto closed = jaguar::closeNarrowConcavities(
        concave.value(), jaguar::ShapeModifyMode::Inflate, {0.2, 0.01});
    assert(closed.vertexCount() < concave.value().vertexCount());
    assert(jaguar::shapeModificationValid(
        concave.value(), closed, jaguar::ShapeModifyMode::Inflate));
    const auto simplifiedInflate = jaguar::simplifyShape(
        concave.value(), jaguar::ShapeModifyMode::Inflate, 0.02);
    assert(simplifiedInflate.hasValue());
    assert(simplifiedInflate.value().vertexCount() <= concave.value().vertexCount());
    assert(jaguar::shapeModificationValid(
        concave.value(), simplifiedInflate.value(),
        jaguar::ShapeModifyMode::Inflate));
    const auto simplifiedDeflate = jaguar::simplifyShape(
        concave.value(), jaguar::ShapeModifyMode::Deflate, 0.02);
    assert(simplifiedDeflate.hasValue());
    assert(simplifiedDeflate.value().vertexCount() <= concave.value().vertexCount());
    assert(jaguar::shapeModificationValid(
        concave.value(), simplifiedDeflate.value(),
        jaguar::ShapeModifyMode::Deflate));

    const std::string fixture = loadReference("geometry_item.json");
    assert(!fixture.empty());

    const jaguar::Importer importer;
    const auto imported = importer.importItemJson(fixture, 4);
    assert(imported.hasValue());

    // Expected values come from the Rust f32 import semantics. The rotation
    // comparison uses a wider cross-language tolerance because JaguarCore
    // stores angles as double while jagua-rs stores them as f32.
    const jaguar::Item& item = imported.value();
    assert(item.externalId() == 101);
    assert(item.index() == 4);
    assert(std::abs(item.area() - 200.0) < 1.0e-9);
    assert(std::abs(item.collisionShape().centroid().x) < 1.0e-9);
    assert(std::abs(item.collisionShape().centroid().y) < 1.0e-9);
    assert(std::abs(item.collisionShape().boundingBox().xMin + 5.0) < 1.0e-9);
    assert(std::abs(item.collisionShape().boundingBox().xMax - 5.0) < 1.0e-9);
    assert(std::abs(item.collisionShape().boundingBox().yMin + 10.0) < 1.0e-9);
    assert(std::abs(item.collisionShape().boundingBox().yMax - 10.0) < 1.0e-9);
    assert(item.allowedRotation().mode == jaguar::RotationMode::Discrete);
    assert(item.allowedRotation().radians.size() == 2);
    assert(std::abs(item.allowedRotation().radians[0]) < 1.0e-9);
    assert(std::abs(item.allowedRotation().radians[1] - 1.5 * std::acos(-1.0)) <
           1.0e-6);

    jaguar::ExternalItem boundaryItem{};
    boundaryItem.id = 102;
    boundaryItem.shape = jaguar::ExternalShape::rectangle(0.0, 0.0, 2.0, 2.0);
    boundaryItem.rotation = jaguar::RotationPolicy::discrete(
        {-0.0, 360.0, -90.0, 270.0});
    const auto boundaryImport = importer.importItem(boundaryItem, 5);
    assert(boundaryImport.hasValue());
    assert(boundaryImport.value().allowedRotation().radians.size() == 2);
    assert(std::abs(boundaryImport.value().allowedRotation().radians[0]) < 1.0e-12);
    assert(std::abs(boundaryImport.value().allowedRotation().radians[1] -
                    1.5 * std::acos(-1.0)) < 1.0e-12);

    jaguar::ExternalItem steppedItem = boundaryItem;
    steppedItem.rotation = jaguar::RotationPolicy::stepped(120.0);
    const auto steppedImport = importer.importItem(steppedItem, 6);
    assert(steppedImport.hasValue());
    assert(steppedImport.value().allowedRotation().radians.size() == 3);

    jaguar::ImporterConfig separationConfig{};
    separationConfig.minItemSeparation = 2.0;
    const jaguar::Importer separatedImporter(separationConfig);
    const auto separated = separatedImporter.importItem(boundaryItem, 7);
    assert(separated.hasValue());
    assert(std::abs(separated.value().collisionShape().boundingBox().width() - 4.0) <
           1.0e-9);

    jaguar::ExternalItem nearlyDuplicate = boundaryItem;
    nearlyDuplicate.shape = jaguar::ExternalShape::simplePolygon({
        {0.0, 0.0}, {1.0, 0.0}, {1.0 + 1.0e-10, 0.0},
        {1.0, 1.0}, {0.0, 1.0}});
    const auto cleaned = importer.importItem(nearlyDuplicate, 8);
    assert(cleaned.hasValue());
    assert(cleaned.value().originalShape().shape.vertexCount() == 4);
    (void)item;
}

namespace
{
 jaguar::SimplePolygon rectanglePolygon(
    double xMin, double yMin, double xMax, double yMax)
{
    const auto rectangle = jaguar::SimplePolygon::create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(rectangle.hasValue());
    return rectangle.value();
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
    const auto bounds = jaguar::Rect2d::create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.hasValue());
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    jaguar::CollisionEngine engine(bounds.value(), config);

    const auto dynamic = engine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 1, 0, 0},
        rectangleShape(4.0, 4.0, 6.0, 6.0), true});
    assert(dynamic.hasValue());
    const auto colliding = jaguar::SimplePolygon::create({
        {5.0, 5.0}, {7.0, 5.0}, {7.0, 7.0}, {5.0, 7.0}});
    assert(colliding.hasValue());
    assert(engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));
    const auto collisions = engine.collectPolygonCollisions(colliding.value());
    assert(collisions.size() == 1);
    assert(collisions.front().kind == jaguar::HazardKind::PlacedItem);

    const auto exterior = engine.registerHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0},
        rectangleShape(2.0, 2.0, 8.0, 8.0), false});
    assert(exterior.hasValue());
    const auto outside = jaguar::SimplePolygon::create({
        {0.5, 0.5}, {1.0, 0.5}, {1.0, 1.0}, {0.5, 1.0}});
    assert(outside.hasValue());
    assert(engine.detectPolygonCollision(outside.value(), jaguar::NoFilter{}));
    const auto outsideCollisions = engine.collectPolygonCollisions(outside.value());
    assert(outsideCollisions.size() == 1);
    assert(outsideCollisions.front().kind == jaguar::HazardKind::Exterior);

    jaguar::HazardFilter filter{};
    filter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Exterior;
    };
    assert(!engine.detectPolygonCollision(outside.value(), filter));

    const auto snapshot = engine.save();
    assert(engine.deregisterHazard(dynamic.value()).hasValue());
    assert(!engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));
    assert(engine.restore(snapshot).hasValue());
    assert(engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));

    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    surrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto surrogateShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(surrogateShape.hasValue());
    const auto surrogate = jaguar::Surrogate::create(
        surrogateShape.value(), surrogateConfig);
    assert(surrogate.hasValue());
    jaguar::CollisionEngine surrogateEngine(bounds.value(), config);
    assert(surrogateEngine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 2, 0, 0},
        rectangleShape(4.0, 4.0, 6.0, 6.0), true}).hasValue());
    assert(surrogateEngine.detectSurrogateCollision(
        surrogate.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {4.0, 4.0}),
        jaguar::NoFilter{}));
    assert(!surrogateEngine.detectSurrogateCollision(
        surrogate.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {0.0, 0.0}),
        jaguar::NoFilter{}));

    jaguar::CDEConfig containerConfig{};
    const auto container = jaguar::Container::create(
        7001,
        {rectanglePolygon(0.0, 0.0, 10.0, 10.0),
         jaguar::Transform2d::identity(), jaguar::ShapeModifyMode::Deflate, {}},
        {{0, {rectanglePolygon(4.0, 4.0, 6.0, 6.0)}},
         {1, {rectanglePolygon(0.0, 0.0, 3.0, 3.0)}},
         {2, {rectanglePolygon(7.0, 0.0, 10.0, 3.0)}}},
        containerConfig);
    assert(container.hasValue());
    const auto staticEngine = container.value().createCollisionEngine();
    assert(staticEngine != nullptr);

    const auto holeProbe = jaguar::SimplePolygon::create({
        {4.5, 4.5}, {5.5, 4.5}, {5.5, 5.5}, {4.5, 5.5}});
    const auto qualityOneProbe = jaguar::SimplePolygon::create({
        {1.0, 1.0}, {2.0, 1.0}, {2.0, 2.0}, {1.0, 2.0}});
    const auto qualityTwoProbe = jaguar::SimplePolygon::create({
        {8.0, 1.0}, {9.0, 1.0}, {9.0, 2.0}, {8.0, 2.0}});
    assert(holeProbe.hasValue() && qualityOneProbe.hasValue() &&
           qualityTwoProbe.hasValue());
    const auto holeHazards = staticEngine->collectPolygonCollisions(
        holeProbe.value());
    assert(holeHazards.size() == 1);
    assert(holeHazards.front().kind == jaguar::HazardKind::Hole);
    const auto qualityOneHazards = staticEngine->collectPolygonCollisions(
        qualityOneProbe.value());
    assert(qualityOneHazards.size() == 1);
    assert(qualityOneHazards.front().kind == jaguar::HazardKind::InferiorQualityZone);
    assert(qualityOneHazards.front().quality == 1);
    const auto qualityTwoHazards = staticEngine->collectPolygonCollisions(
        qualityTwoProbe.value());
    assert(qualityTwoHazards.size() == 1);
    assert(qualityTwoHazards.front().kind == jaguar::HazardKind::InferiorQualityZone);
    assert(qualityTwoHazards.front().quality == 2);

    jaguar::ExternalItem qualityItem{};
    qualityItem.id = 7002;
    qualityItem.rotation = jaguar::RotationPolicy::none();
    qualityItem.shape = jaguar::ExternalShape::rectangle(0.0, 0.0, 2.0, 2.0);
    qualityItem.minQuality = 2;
    const jaguar::Importer qualityImporter;
    const auto importedQualityItem = qualityImporter.importItem(qualityItem, 0);
    assert(importedQualityItem.hasValue());

    jaguar::Layout holeLayout(container.value());
    assert(holeLayout.placeItem(
        importedQualityItem.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {5.0, 5.0})).hasValue());
    assert(!holeLayout.isFeasible());

    jaguar::Layout inferiorLayout(container.value());
    assert(inferiorLayout.placeItem(
        importedQualityItem.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {1.5, 1.5})).hasValue());
    // Match jagua-rs HazKeyFilter::from_irrelevant_qzones: quality values
    // below min_quality are filtered from collision queries.
    assert(inferiorLayout.isFeasible());

    jaguar::Layout acceptableLayout(container.value());
    assert(acceptableLayout.placeItem(
        importedQualityItem.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {8.5, 1.5})).hasValue());
    assert(!acceptableLayout.isFeasible());

    jaguar::SurrogateConfig qualitySurrogateConfig{};
    qualitySurrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    qualitySurrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto qualitySurrogateShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(qualitySurrogateShape.hasValue());
    const auto qualitySurrogate = jaguar::Surrogate::create(
        qualitySurrogateShape.value(), qualitySurrogateConfig);
    assert(qualitySurrogate.hasValue());
    jaguar::HazardFilter qualitySurrogateFilter{};
    qualitySurrogateFilter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::InferiorQualityZone &&
               entity.quality < 2;
    };
    assert(!staticEngine->detectSurrogateCollision(
        qualitySurrogate.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {1.0, 1.0}),
        qualitySurrogateFilter));
    assert(staticEngine->detectSurrogateCollision(
        qualitySurrogate.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {8.0, 1.0}),
        qualitySurrogateFilter));
}
