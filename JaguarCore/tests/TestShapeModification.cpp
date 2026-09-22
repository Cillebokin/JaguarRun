#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Geometry/ShapeModification.h"

#include <cassert>
#include <cmath>
#include <limits>

void runShapeModificationTests()
{
    const auto source = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}});
    assert(source.HasValue());

    const jaguar::Transform2d translation =
        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 20.0});
    const auto moved = source.Value().Transformed(translation);
    assert(moved.HasValue());
    assert(std::abs(moved.Value().BoundingBox().xMin - 10.0) < 1.0e-12);
    assert(std::abs(moved.Value().BoundingBox().yMin - 20.0) < 1.0e-12);
    assert(std::abs(moved.Value().Area() - source.Value().Area()) < 1.0e-12);

    jaguar::OriginalShape original{
        source.Value(), translation, jaguar::ShapeModifyMode::Inflate, {}};
    const auto internal = original.ConvertToInternal();
    assert(internal.HasValue());
    assert(std::abs(internal.Value().BoundingBox().xMin - 10.0) < 1.0e-12);
    assert(std::abs(internal.Value().BoundingBox().yMin - 20.0) < 1.0e-12);

    jaguar::OriginalShape invalidOriginal{
        source.Value(),
        jaguar::Transform2d::FromRotationTranslation(
            0.0, {0.0, std::numeric_limits<double>::infinity()}),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    const auto invalidInternal = invalidOriginal.ConvertToInternal();
    assert(!invalidInternal.HasValue());
    assert(invalidInternal.GetError().code == jaguar::ErrorCode::InvalidInput);

    const auto redundant = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(redundant.HasValue());
    const auto simplified = jaguar::SimplifyShape(
        redundant.Value(), jaguar::ShapeModifyMode::Inflate, 0.0);
    assert(simplified.HasValue());
    assert(simplified.Value().VertexCount() == 4);

    const auto inflated = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Inflate, 1.0);
    assert(inflated.HasValue());
    assert(inflated.Value().VertexCount() == 68);
    assert(std::abs(inflated.Value().Area() - 15.1364769) < 1.0e-3);

    const auto deflated = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(deflated.HasValue());
    assert(std::abs(deflated.Value().Area() - 2.25) < 1.0e-9);

    const auto narrowNotch = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 9.9}, {4.0, 9.9}, {4.0, 10.0}, {0.0, 10.0}});
    assert(narrowNotch.HasValue());
    const auto rustNotchInflated = jaguar::OffsetShape(
        narrowNotch.Value(), jaguar::ShapeModifyMode::Inflate, 0.25);
    assert(rustNotchInflated.HasValue());
    assert(rustNotchInflated.Value().VertexCount() == 104);
    assert(std::abs(rustNotchInflated.Value().Area() - 110.019035339355) <
           1.0e-3);
    const auto closed = jaguar::CloseNarrowConcavities(
        narrowNotch.Value(), jaguar::ShapeModifyMode::Inflate, {0.2, 0.01});
    assert(closed.VertexCount() < narrowNotch.Value().VertexCount());

    jaguar::ShapeModifyConfig narrowOnlyConfig{};
    narrowOnlyConfig.narrowConcavityCutoff = std::make_pair(0.2, 0.01);
    jaguar::OriginalShape narrowOnly{
        narrowNotch.Value(), jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate, narrowOnlyConfig};
    const auto unchangedWithoutSimplification = narrowOnly.ConvertToInternal();
    assert(unchangedWithoutSimplification.HasValue());
    assert(unchangedWithoutSimplification.Value().VertexCount() ==
           narrowNotch.Value().VertexCount());

    const auto collapsed = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Deflate, 2.0);
    assert(!collapsed.HasValue());
    assert(collapsed.GetError().code == jaguar::ErrorCode::OffsetFailed);

    const auto splitCandidate = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {2.0, 1.0}, {0.0, 4.0}});
    assert(splitCandidate.HasValue());
    const auto splitOffset = jaguar::OffsetShape(
        splitCandidate.Value(), jaguar::ShapeModifyMode::Deflate, 0.45);
    assert(splitOffset.HasValue());
    // Rust geo-buffer straight-skeleton oracle: this inward offset splits
    // into two components and jagua-rs keeps the first outer component.
    assert(splitOffset.Value().VertexCount() == 3);
    assert(std::abs(splitOffset.Value().Area() - 1.419689416885) < 1.0e-6);
    assert(std::abs(splitOffset.Value().Vertices()[0].x - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.Value().Vertices()[0].y - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.Value().Vertices()[1].x - 1.825834035873) <
           1.0e-6);
    assert(std::abs(splitOffset.Value().Vertices()[1].y - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.Value().Vertices()[2].x - 0.449999988079) <
           1.0e-6);
    assert(std::abs(splitOffset.Value().Vertices()[2].y - 2.513751029968) <
           1.0e-6);

    const auto splitComponents = jaguar::OffsetShapes(
        splitCandidate.Value(), jaguar::ShapeModifyMode::Deflate, 0.45);
    assert(splitComponents.HasValue());
    assert(splitComponents.Value().size() == 2);
    const auto& firstSplitComponent = splitComponents.Value()[0];
    assert(firstSplitComponent.VertexCount() == 3);
    assert(std::abs(firstSplitComponent.Area() - 1.419689345789) < 1.0e-6);
    assert(std::abs(firstSplitComponent.BoundingBox().xMin - 0.45) <
           1.0e-6);
    assert(std::abs(firstSplitComponent.BoundingBox().yMin - 0.45) <
           1.0e-6);
    assert(std::abs(firstSplitComponent.BoundingBox().xMax -
                    1.825833975347) < 1.0e-6);
    assert(std::abs(firstSplitComponent.BoundingBox().yMax -
                    2.513750963021) < 1.0e-6);

    const auto& secondSplitComponent = splitComponents.Value()[1];
    assert(secondSplitComponent.VertexCount() == 3);
    assert(std::abs(secondSplitComponent.Area() - 1.419689345789) < 1.0e-6);
    assert(std::abs(secondSplitComponent.BoundingBox().xMin -
                    2.174166024653) < 1.0e-6);
    assert(std::abs(secondSplitComponent.BoundingBox().yMin - 0.45) <
           1.0e-6);
    assert(std::abs(secondSplitComponent.BoundingBox().xMax - 3.55) <
           1.0e-6);
    assert(std::abs(secondSplitComponent.BoundingBox().yMax -
                    2.513750963021) < 1.0e-6);

    const auto emptyComponents = jaguar::OffsetShapes(
        source.Value(), jaguar::ShapeModifyMode::Deflate, 2.0);
    assert(emptyComponents.HasValue());
    assert(emptyComponents.Value().empty());

    const auto lShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {6.0, 0.0}, {6.0, 2.0},
        {2.0, 2.0}, {2.0, 6.0}, {0.0, 6.0}});
    assert(lShape.HasValue());
    const auto lInflated = jaguar::OffsetShape(
        lShape.Value(), jaguar::ShapeModifyMode::Inflate, 0.25);
    assert(lInflated.HasValue());
    assert(lInflated.Value().Area() > lShape.Value().Area());
    assert(jaguar::ShapeModificationValid(
        lShape.Value(), lInflated.Value(), jaguar::ShapeModifyMode::Inflate));

    const auto lDeflated = jaguar::OffsetShape(
        lShape.Value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(lDeflated.HasValue());
    assert(lDeflated.Value().Area() < lShape.Value().Area());
    assert(jaguar::ShapeModificationValid(
        lShape.Value(), lDeflated.Value(), jaguar::ShapeModifyMode::Deflate));

    const auto lDeflatedComponents = jaguar::OffsetShapes(
        lShape.Value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(lDeflatedComponents.HasValue());
    assert(lDeflatedComponents.Value().size() == 1);
    const auto& lDeflatedComponent = lDeflatedComponents.Value().front();
    assert(lDeflatedComponent.VertexCount() == 22);
    // The fallback uses C++ double geometry; Rust's oracle rounds the
    // exported polygon through f32 before constructing its SPolygon.
    assert(std::abs(lDeflatedComponent.Area() - 14.263492548395) < 2.0e-6);
    assert(std::abs(lDeflatedComponent.BoundingBox().xMin - 0.25) <
           1.0e-6);
    assert(std::abs(lDeflatedComponent.BoundingBox().yMin - 0.25) <
           1.0e-6);
    assert(std::abs(lDeflatedComponent.BoundingBox().xMax - 5.75) <
           1.0e-6);
    assert(std::abs(lDeflatedComponent.BoundingBox().yMax - 5.75) <
           1.0e-6);

    const auto simplifiedInflated = jaguar::SimplifyShape(
        narrowNotch.Value(), jaguar::ShapeModifyMode::Inflate, 0.02);
    assert(simplifiedInflated.HasValue());
    assert(jaguar::ShapeModificationValid(
        narrowNotch.Value(), simplifiedInflated.Value(),
        jaguar::ShapeModifyMode::Inflate));

    const auto simplifiedDeflated = jaguar::SimplifyShape(
        narrowNotch.Value(), jaguar::ShapeModifyMode::Deflate, 0.02);
    assert(simplifiedDeflated.HasValue());
    assert(jaguar::ShapeModificationValid(
        narrowNotch.Value(), simplifiedDeflated.Value(),
        jaguar::ShapeModifyMode::Deflate));
}
