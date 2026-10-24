#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Geometry/ShapeModification.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

void runShapeModificationTests()
{
    const auto source = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}});
    assert(source.HasValue());

    const auto validationBounds = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {3.0, 0.0}, {3.0, 3.0}, {0.0, 3.0}});
    assert(validationBounds.HasValue());
    const float edgeTolerance =
        static_cast<float>(validationBounds.Value().Diameter()) * 1.0e-6F;
    const auto justOutside = jaguar::SimplePolygon::Create({
        {-static_cast<double>(edgeTolerance), 1.5}, {1.0, 1.0}, {1.0, 2.0}});
    assert(justOutside.HasValue());
    assert(!jaguar::ShapeModificationValid(
        justOutside.Value(), validationBounds.Value(),
        jaguar::ShapeModifyMode::Inflate));

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

    const auto nanTolerance = jaguar::SimplifyShape(
        redundant.Value(), jaguar::ShapeModifyMode::Inflate,
        std::numeric_limits<double>::quiet_NaN());
    assert(nanTolerance.HasValue());
    assert(nanTolerance.Value().VertexCount() == redundant.Value().VertexCount());
    const auto infiniteTolerance = jaguar::SimplifyShape(
        redundant.Value(), jaguar::ShapeModifyMode::Inflate,
        std::numeric_limits<double>::infinity());
    assert(infiniteTolerance.HasValue());

    // jagua-rs does not reject negative area-change ratios; with a negative
    // limit no candidate can be accepted, so it returns the original shape.
    const auto negativeTolerance = jaguar::SimplifyShape(
        redundant.Value(), jaguar::ShapeModifyMode::Inflate, -0.1);
    assert(negativeTolerance.HasValue());
    assert(negativeTolerance.Value().VertexCount() == redundant.Value().VertexCount());
    assert(std::abs(negativeTolerance.Value().Area() - redundant.Value().Area()) <
           1.0e-12);

    const auto inflated = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Inflate, 1.0);
    assert(inflated.HasValue());
    assert(inflated.Value().VertexCount() == 68);
    assert(std::abs(inflated.Value().Area() - 15.1364769) < 1.0e-3);

    const auto deflated = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Deflate, 0.25);
    assert(deflated.HasValue());
    assert(std::abs(deflated.Value().Area() - 2.25) < 1.0e-9);

    const auto negativeInflate = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Inflate, -0.25);
    assert(negativeInflate.HasValue());
    assert(std::abs(negativeInflate.Value().Area() - 2.25) < 1.0e-9);
    assert(std::abs(negativeInflate.Value().BoundingBox().xMin - 0.25) < 1.0e-9);
    assert(std::abs(negativeInflate.Value().BoundingBox().xMax - 1.75) < 1.0e-9);

    const auto largeSquare = jaguar::SimplePolygon::Create({
        {1000000.0, 1000000.0}, {2000000.0, 1000000.0},
        {2000000.0, 2000000.0}, {1000000.0, 2000000.0}});
    assert(largeSquare.HasValue());
    const auto subUlpInflate = jaguar::OffsetShape(
        largeSquare.Value(), jaguar::ShapeModifyMode::Inflate, 0.01);
    assert(subUlpInflate.HasValue());
    const std::vector<jaguar::Point2d> expectedSubUlpVertices = {
        {1000000.0, 2000000.0}, {1000000.0, 1000000.0},
        {2000000.0, 1000000.0}, {2000000.0, 2000000.0}};
    assert(subUlpInflate.Value().Vertices() == expectedSubUlpVertices);
    assert(subUlpInflate.Value().Area() == largeSquare.Value().Area());
    const auto subUlpDeflate = jaguar::OffsetShape(
        largeSquare.Value(), jaguar::ShapeModifyMode::Deflate, 0.01);
    assert(subUlpDeflate.HasValue());
    assert(subUlpDeflate.Value().Vertices() == expectedSubUlpVertices);
    assert(subUlpDeflate.Value().Area() == largeSquare.Value().Area());

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

    const auto wrappedNotch = jaguar::SimplePolygon::Create({
        {0.1, 6.0}, {0.1, 4.0}, {0.0, 4.0}, {0.0, 0.0},
        {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}, {0.0, 6.0}});
    assert(wrappedNotch.HasValue());
    const auto closedWrappedNotch = jaguar::CloseNarrowConcavities(
        wrappedNotch.Value(), jaguar::ShapeModifyMode::Inflate, {0.2, 0.01});
    const std::vector<jaguar::Point2d> expectedWrappedNotch{
        {0.0, 4.0}, {0.0, 0.0}, {10.0, 0.0},
        {10.0, 10.0}, {0.0, 10.0}, {0.0, 6.0}};
    assert(closedWrappedNotch.Vertices() == expectedWrappedNotch);
    assert(std::abs(closedWrappedNotch.Area() - 100.0) < 1.0e-6);

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

    const auto nanOffset = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Inflate,
        std::numeric_limits<double>::quiet_NaN());
    assert(!nanOffset.HasValue());
    assert(nanOffset.GetError().code == jaguar::ErrorCode::OffsetFailed);
    assert(nanOffset.GetError().message ==
           "Offset resulted in an empty polygon");
    const auto infiniteOffset = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Inflate,
        std::numeric_limits<double>::infinity());
    assert(!infiniteOffset.HasValue());
    assert(infiniteOffset.GetError().code == jaguar::ErrorCode::OffsetFailed);
    assert(infiniteOffset.GetError().message ==
           "Offset resulted in an empty polygon");
    const auto negativeInfiniteOffset = jaguar::OffsetShape(
        source.Value(), jaguar::ShapeModifyMode::Inflate,
        -std::numeric_limits<double>::infinity());
    assert(!negativeInfiniteOffset.HasValue());
    assert(negativeInfiniteOffset.GetError().code == jaguar::ErrorCode::OffsetFailed);
    assert(negativeInfiniteOffset.GetError().message ==
           "Offset resulted in an empty polygon");

    const auto nonFiniteOffsets = jaguar::OffsetShapes(
        source.Value(), jaguar::ShapeModifyMode::Inflate,
        std::numeric_limits<double>::infinity());
    assert(!nonFiniteOffsets.HasValue());
    assert(nonFiniteOffsets.GetError().code == jaguar::ErrorCode::OffsetFailed);
    assert(nonFiniteOffsets.GetError().message ==
           "Offset resulted in an empty polygon");

    constexpr float epsilonBoundarySide =
        0.00200000009499490261078F;
    constexpr float epsilonBoundaryDistance =
        0.00099999993108212947845F;
    const auto epsilonBoundarySquare = jaguar::SimplePolygon::Create({
        {0.0, 0.0},
        {epsilonBoundarySide, 0.0},
        {epsilonBoundarySide, epsilonBoundarySide},
        {0.0, epsilonBoundarySide}});
    assert(epsilonBoundarySquare.HasValue());
    const auto epsilonBoundaryOffset = jaguar::OffsetShape(
        epsilonBoundarySquare.Value(), jaguar::ShapeModifyMode::Deflate,
        epsilonBoundaryDistance);
    assert(!epsilonBoundaryOffset.HasValue());

    const auto nearZeroEdge = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0e-8, 0.0}, {1.0, 0.0},
        {1.0, 1.0}, {0.0, 1.0}});
    assert(nearZeroEdge.HasValue());
    const auto zeroOffsetNearEdge = jaguar::OffsetShape(
        nearZeroEdge.Value(), jaguar::ShapeModifyMode::Inflate, 0.0);
    assert(zeroOffsetNearEdge.HasValue());
    assert(zeroOffsetNearEdge.Value().VertexCount() == 4);
    assert(zeroOffsetNearEdge.Value().Vertices()[0].x ==
           static_cast<double>(1.0e-8F));

    const auto splitCandidate = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {2.0, 1.0}, {0.0, 4.0}});
    assert(splitCandidate.HasValue());
    const auto splitOffset = jaguar::OffsetShape(
        splitCandidate.Value(), jaguar::ShapeModifyMode::Deflate, 0.45);
    assert(splitOffset.HasValue());
    assert(splitOffset.Value().VertexCount() == 25);
    assert(std::abs(splitOffset.Value().Area() - 2.898932695) < 1.0e-6);
    const std::vector<jaguar::Point2d> expectedSplitOffsetVertices{
        {0.449999988, 2.513751030}, {0.449999988, 0.449999988},
        {3.549999952, 0.449999988}, {3.549999952, 2.513751030},
        {2.374422550, 0.750384927}, {2.348123312, 0.714850664},
        {2.318464279, 0.682068348}, {2.285731554, 0.652354419},
        {2.250241280, 0.625995636}, {2.212336063, 0.603246450},
        {2.172381401, 0.584326327}, {2.130763292, 0.569417894},
        {2.087882996, 0.558665037}, {2.044154644, 0.552171469},
        {2.000000000, 0.550000012}, {1.955845475, 0.552171469},
        {1.912117004, 0.558665037}, {1.869236827, 0.569417894},
        {1.827618480, 0.584326327}, {1.787663937, 0.603246450},
        {1.749758601, 0.625995636}, {1.714268446, 0.652354419},
        {1.681535840, 0.682068348}, {1.651876688, 0.714850664},
        {1.625577331, 0.750384927}};
    assert(splitOffset.Value().Vertices().size() ==
           expectedSplitOffsetVertices.size());
    for (std::size_t i = 0; i < expectedSplitOffsetVertices.size(); ++i)
    {
        assert(std::abs(splitOffset.Value().Vertices()[i].x -
                        expectedSplitOffsetVertices[i].x) < 1.0e-6);
        assert(std::abs(splitOffset.Value().Vertices()[i].y -
                        expectedSplitOffsetVertices[i].y) < 1.0e-6);
    }

    const auto splitComponents = jaguar::OffsetShapes(
        splitCandidate.Value(), jaguar::ShapeModifyMode::Deflate, 0.45);
    assert(splitComponents.HasValue());
    assert(splitComponents.Value().size() == 1);
    const auto& firstSplitComponent = splitComponents.Value()[0];
    assert(firstSplitComponent.Vertices() == splitOffset.Value().Vertices());

    const auto narrowBridge = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {5.0, 0.0}, {5.0, 2.0}, {7.0, 2.0},
        {7.0, 0.0}, {12.0, 0.0}, {12.0, 5.0}, {7.0, 5.0},
        {7.0, 3.0}, {5.0, 3.0}, {5.0, 5.0}, {0.0, 5.0}});
    assert(narrowBridge.HasValue());
    const auto narrowBridgeComponents = jaguar::OffsetShapes(
        narrowBridge.Value(), jaguar::ShapeModifyMode::Deflate, 0.6);
    assert(narrowBridgeComponents.HasValue());
    assert(narrowBridgeComponents.Value().size() == 2);
    assert(narrowBridgeComponents.Value()[0].VertexCount() == 27);
    assert(std::abs(narrowBridgeComponents.Value()[0].Area() -
                    14.520097733) < 1.0e-5);
    assert(narrowBridgeComponents.Value()[1].VertexCount() == 27);
    assert(std::abs(narrowBridgeComponents.Value()[1].Area() -
                    14.520095825) < 1.0e-5);
    const auto narrowBridgeFirst = jaguar::OffsetShape(
        narrowBridge.Value(), jaguar::ShapeModifyMode::Deflate, 0.6);
    assert(narrowBridgeFirst.HasValue());
    assert(narrowBridgeFirst.Value().Vertices() ==
           narrowBridgeComponents.Value()[0].Vertices());

    const auto comb = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {22.0, 0.0}, {22.0, 12.0}, {16.0, 12.0},
        {16.0, 4.0}, {14.0, 4.0}, {14.0, 12.0}, {8.0, 12.0},
        {8.0, 4.0}, {6.0, 4.0}, {6.0, 12.0}, {0.0, 12.0}});
    assert(comb.HasValue());
    const auto combOffset = jaguar::OffsetShape(
        comb.Value(), jaguar::ShapeModifyMode::Deflate, 2.1);
    assert(combOffset.HasValue());
    assert(combOffset.Value().VertexCount() == 16);
    assert(std::abs(combOffset.Value().Area() - 14.690850258) < 1.0e-5);
    assert(std::abs(combOffset.Value().BoundingBox().xMin - 2.099999904633) <
           1.0e-6);
    assert(std::abs(combOffset.Value().BoundingBox().yMin - 2.099999904633) <
           1.0e-6);
    assert(std::abs(combOffset.Value().BoundingBox().xMax - 5.111481667) <
           1.0e-6);
    assert(std::abs(combOffset.Value().BoundingBox().yMax - 9.899999618530) <
           1.0e-6);
    const auto combComponents = jaguar::OffsetShapes(
        comb.Value(), jaguar::ShapeModifyMode::Deflate, 2.1);
    assert(combComponents.HasValue());
    assert(combComponents.Value().size() == 3);
    assert(combComponents.Value()[0].VertexCount() == 16);
    assert(std::abs(combComponents.Value()[0].Area() - 14.690850258) <
           1.0e-5);
    assert(combComponents.Value()[1].VertexCount() == 28);
    assert(std::abs(combComponents.Value()[1].Area() - 15.341698647) <
           1.0e-5);
    assert(combComponents.Value()[2].VertexCount() == 16);
    assert(std::abs(combComponents.Value()[2].Area() - 14.690842628) <
           1.0e-5);
    assert(combOffset.Value().Vertices() == combComponents.Value()[0].Vertices());

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

    const auto convexBevel = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.1, 2.0}, {4.0, 4.0},
        {2.0, 4.1}, {0.0, 4.0}, {-0.1, 2.0}});
    assert(convexBevel.HasValue());
    const auto simplifiedBevel = jaguar::SimplifyShape(
        convexBevel.Value(), jaguar::ShapeModifyMode::Inflate, 0.05);
    assert(simplifiedBevel.HasValue());
    const std::vector<jaguar::Point2d> expectedBevelVertices{
        {-0.19999993, 0.0}, {4.0, 0.0},
        {4.199501, 3.990025}, {0.0099749565, 4.199501}};
    assert(simplifiedBevel.Value().Vertices().size() ==
           expectedBevelVertices.size());
    for (std::size_t i = 0; i < expectedBevelVertices.size(); ++i)
    {
        assert(std::abs(simplifiedBevel.Value().Vertices()[i].x -
                        expectedBevelVertices[i].x) < 1.0e-5);
        assert(std::abs(simplifiedBevel.Value().Vertices()[i].y -
                        expectedBevelVertices[i].y) < 1.0e-5);
    }
    assert(std::abs(simplifiedBevel.Value().Area() - 17.198004) < 1.0e-4);
}
