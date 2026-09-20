#include "JaguarCore/geometry/original_shape.h"
#include "JaguarCore/geometry/shape_modification.h"

#include <cassert>
#include <cmath>

void runShapeModificationTests()
{
    const auto source = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}});
    assert(source.hasValue());

    const jaguar::Transform2d translation =
        jaguar::Transform2d::fromRotationTranslation(0.0, {10.0, 20.0});
    const jaguar::SimplePolygon moved = source.value().transformed(translation);
    assert(std::abs(moved.boundingBox().xMin - 10.0) < 1.0e-12);
    assert(std::abs(moved.boundingBox().yMin - 20.0) < 1.0e-12);
    assert(std::abs(moved.area() - source.value().area()) < 1.0e-12);

    jaguar::OriginalShape original{
        source.value(), translation, jaguar::ShapeModifyMode::Inflate, {}};
    const auto internal = original.convertToInternal();
    assert(internal.hasValue());
    assert(std::abs(internal.value().boundingBox().xMin - 10.0) < 1.0e-12);
    assert(std::abs(internal.value().boundingBox().yMin - 20.0) < 1.0e-12);

    const auto redundant = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(redundant.hasValue());
    const auto simplified = jaguar::simplifyShape(
        redundant.value(), jaguar::ShapeModifyMode::Inflate, 0.0);
    assert(simplified.hasValue());
    assert(simplified.value().vertexCount() == 4);

    const auto inflated = jaguar::offsetShape(
        source.value(), jaguar::ShapeModifyMode::Inflate, 1.0);
    assert(inflated.hasValue());
    assert(inflated.value().vertexCount() == 68);
    assert(std::abs(inflated.value().area() - 15.1364769) < 1.0e-3);

    const auto deflated = jaguar::offsetShape(
        source.value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(deflated.hasValue());
    assert(std::abs(deflated.value().area() - 2.25) < 1.0e-9);

    const auto narrowNotch = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 9.9}, {4.0, 9.9}, {4.0, 10.0}, {0.0, 10.0}});
    assert(narrowNotch.hasValue());
    const auto rustNotchInflated = jaguar::offsetShape(
        narrowNotch.value(), jaguar::ShapeModifyMode::Inflate, 0.25);
    assert(rustNotchInflated.hasValue());
    assert(rustNotchInflated.value().vertexCount() == 104);
    assert(std::abs(rustNotchInflated.value().area() - 110.019035339355) <
           1.0e-3);
    const auto closed = jaguar::closeNarrowConcavities(
        narrowNotch.value(), jaguar::ShapeModifyMode::Inflate, {0.2, 0.01});
    assert(closed.vertexCount() < narrowNotch.value().vertexCount());

    jaguar::ShapeModifyConfig narrowOnlyConfig{};
    narrowOnlyConfig.narrowConcavityCutoff = std::make_pair(0.2, 0.01);
    jaguar::OriginalShape narrowOnly{
        narrowNotch.value(), jaguar::Transform2d::identity(),
        jaguar::ShapeModifyMode::Inflate, narrowOnlyConfig};
    const auto unchangedWithoutSimplification = narrowOnly.convertToInternal();
    assert(unchangedWithoutSimplification.hasValue());
    assert(unchangedWithoutSimplification.value().vertexCount() ==
           narrowNotch.value().vertexCount());

    const auto collapsed = jaguar::offsetShape(
        source.value(), jaguar::ShapeModifyMode::Deflate, 2.0);
    assert(!collapsed.hasValue());
    assert(collapsed.error().code == jaguar::ErrorCode::OffsetFailed);

    const auto splitCandidate = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {2.0, 1.0}, {0.0, 4.0}});
    assert(splitCandidate.hasValue());
    const auto splitOffset = jaguar::offsetShape(
        splitCandidate.value(), jaguar::ShapeModifyMode::Deflate, 0.45);
    assert(splitOffset.hasValue());
    // Rust geo-buffer straight-skeleton oracle: this inward offset splits
    // into two components and jagua-rs keeps the first outer component.
    assert(splitOffset.value().vertexCount() == 3);
    assert(std::abs(splitOffset.value().area() - 1.419689416885) < 1.0e-6);
    assert(std::abs(splitOffset.value().vertices()[0].x - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.value().vertices()[0].y - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.value().vertices()[1].x - 1.825834035873) <
           1.0e-6);
    assert(std::abs(splitOffset.value().vertices()[1].y - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.value().vertices()[2].x - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.value().vertices()[2].y - 2.513751029968) <
           1.0e-6);

    const auto splitComponents = jaguar::offsetShapes(
        splitCandidate.value(), jaguar::ShapeModifyMode::Deflate, 0.45);
    assert(splitComponents.hasValue());
    assert(splitComponents.value().size() == 2);
    const auto& firstSplitComponent = splitComponents.value()[0];
    assert(firstSplitComponent.vertexCount() == 3);
    assert(std::abs(firstSplitComponent.area() - 1.419689345789) < 1.0e-6);
    assert(std::abs(firstSplitComponent.boundingBox().xMin - 0.45) <
           1.0e-6);
    assert(std::abs(firstSplitComponent.boundingBox().yMin - 0.45) <
           1.0e-6);
    assert(std::abs(firstSplitComponent.boundingBox().xMax -
                    1.825833975347) < 1.0e-6);
    assert(std::abs(firstSplitComponent.boundingBox().yMax -
                    2.513750963021) < 1.0e-6);

    const auto& secondSplitComponent = splitComponents.value()[1];
    assert(secondSplitComponent.vertexCount() == 3);
    assert(std::abs(secondSplitComponent.area() - 1.419689345789) < 1.0e-6);
    assert(std::abs(secondSplitComponent.boundingBox().xMin -
                    2.174166024653) < 1.0e-6);
    assert(std::abs(secondSplitComponent.boundingBox().yMin - 0.45) <
           1.0e-6);
    assert(std::abs(secondSplitComponent.boundingBox().xMax - 3.55) <
           1.0e-6);
    assert(std::abs(secondSplitComponent.boundingBox().yMax -
                    2.513750963021) < 1.0e-6);

    const auto emptyComponents = jaguar::offsetShapes(
        source.value(), jaguar::ShapeModifyMode::Deflate, 2.0);
    assert(emptyComponents.hasValue());
    assert(emptyComponents.value().empty());

    const auto lShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {6.0, 0.0}, {6.0, 2.0},
        {2.0, 2.0}, {2.0, 6.0}, {0.0, 6.0}});
    assert(lShape.hasValue());
    const auto lInflated = jaguar::offsetShape(
        lShape.value(), jaguar::ShapeModifyMode::Inflate, 0.25);
    assert(lInflated.hasValue());
    assert(lInflated.value().area() > lShape.value().area());
    assert(jaguar::shapeModificationValid(
        lShape.value(), lInflated.value(), jaguar::ShapeModifyMode::Inflate));

    const auto lDeflated = jaguar::offsetShape(
        lShape.value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(lDeflated.hasValue());
    assert(lDeflated.value().area() < lShape.value().area());
    assert(jaguar::shapeModificationValid(
        lShape.value(), lDeflated.value(), jaguar::ShapeModifyMode::Deflate));

    const auto lDeflatedComponents = jaguar::offsetShapes(
        lShape.value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(lDeflatedComponents.hasValue());
    assert(lDeflatedComponents.value().size() == 1);
    const auto& lDeflatedComponent = lDeflatedComponents.value().front();
    assert(lDeflatedComponent.vertexCount() == 22);
    // The fallback uses C++ double geometry; Rust's oracle rounds the
    // exported polygon through f32 before constructing its SPolygon.
    assert(std::abs(lDeflatedComponent.area() - 14.263492548395) < 2.0e-6);
    assert(std::abs(lDeflatedComponent.boundingBox().xMin - 0.25) <
           1.0e-6);
    assert(std::abs(lDeflatedComponent.boundingBox().yMin - 0.25) <
           1.0e-6);
    assert(std::abs(lDeflatedComponent.boundingBox().xMax - 5.75) <
           1.0e-6);
    assert(std::abs(lDeflatedComponent.boundingBox().yMax - 5.75) <
           1.0e-6);

    const auto simplifiedInflated = jaguar::simplifyShape(
        narrowNotch.value(), jaguar::ShapeModifyMode::Inflate, 0.02);
    assert(simplifiedInflated.hasValue());
    assert(jaguar::shapeModificationValid(
        narrowNotch.value(), simplifiedInflated.value(),
        jaguar::ShapeModifyMode::Inflate));

    const auto simplifiedDeflated = jaguar::simplifyShape(
        narrowNotch.value(), jaguar::ShapeModifyMode::Deflate, 0.02);
    assert(simplifiedDeflated.hasValue());
    assert(jaguar::shapeModificationValid(
        narrowNotch.value(), simplifiedDeflated.value(),
        jaguar::ShapeModifyMode::Deflate));
}
