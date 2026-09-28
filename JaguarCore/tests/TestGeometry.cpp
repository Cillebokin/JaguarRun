#include "JaguarCore/Geometry/circle.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/Transformation.h"
#include "JaguarCore/NumericPolicy.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

void runGeometryTests()
{
    const jaguar::Point2d pointA{1.25, -2.5};
    const jaguar::Point2d pointB{4.5, 2.5};
    assert(std::abs(pointA.DistanceToRustF32(pointB) - std::sqrt(35.5625)) < 1.0e-6);
    assert(pointA.SquaredDistanceToRustF32(pointB) == 35.5625);
    assert((jaguar::Point2d{16777216.0, 0.0}).DistanceToRustF32(
               jaguar::Point2d{16777217.0, 0.0}) == 0.0);

    const double roundedCoordinate = static_cast<double>(1.00000006F);
    const auto roundedRect = jaguar::Rect2d::Create(
        1.00000006, -1.00000006, 2.00000012, 3.00000024);
    assert(roundedRect.HasValue());
    assert(roundedRect.Value().xMin == roundedCoordinate);
    assert(roundedRect.Value().yMin == -roundedCoordinate);
    const auto roundedEdge = jaguar::Edge2d::Create(
        {1.00000006, -1.00000006}, {2.00000012, 3.00000024});
    assert(roundedEdge.HasValue());
    assert(roundedEdge.Value().start.x == roundedCoordinate);
    assert(roundedEdge.Value().start.y == -roundedCoordinate);
    assert(roundedEdge.Value().end.x == static_cast<double>(2.00000012F));
    assert(roundedEdge.Value().end.y == static_cast<double>(3.00000024F));

    const auto diagonal = jaguar::Edge2d::Create({0.0, 0.0}, {3.0, 4.0});
    assert(diagonal.HasValue());
    assert(std::abs(diagonal.Value().Length() - 5.0) < 1.0e-12);
    assert(diagonal.Value().LengthRustF32() == 5.0);
    assert(diagonal.Value().XMinRustF32() == 0.0);
    assert(diagonal.Value().YMinRustF32() == 0.0);
    assert(diagonal.Value().XMaxRustF32() == 3.0);
    assert(diagonal.Value().YMaxRustF32() == 4.0);
    assert(diagonal.Value().CentroidRustF32() == (jaguar::Point2d{1.5, 2.0}));
    const double smallestSubnormal = static_cast<double>(
        std::numeric_limits<float>::denorm_min());
    const auto subnormalEdge = jaguar::Edge2d::Create(
        {smallestSubnormal, 0.0}, {smallestSubnormal, 1.0});
    assert(subnormalEdge.HasValue());
    assert(subnormalEdge.Value().CentroidRustF32().x == smallestSubnormal);
    const auto subnormalBoundingCircle = jaguar::Circle2d::BoundingCircleRustF32({
        jaguar::Circle2d{{smallestSubnormal, 0.0}, 0.0},
        jaguar::Circle2d{{smallestSubnormal, 1.0}, 0.0}});
    assert(subnormalBoundingCircle.HasValue());
    assert(subnormalBoundingCircle.Value().center.x == smallestSubnormal);
    const jaguar::Edge2d frontExtended =
        diagonal.Value().ExtendedAtFrontRustF32(2.0);
    const jaguar::Edge2d backExtended =
        diagonal.Value().ExtendedAtBackRustF32(2.0);
    const jaguar::Edge2d scaledEdge = diagonal.Value().ScaledRustF32(2.0);
    const jaguar::Edge2d reversedEdge = diagonal.Value().ReversedRustF32();
    assert(std::abs(frontExtended.start.x + 1.2) < 1.0e-6);
    assert(std::abs(frontExtended.start.y + 1.6) < 1.0e-6);
    assert(std::abs(backExtended.end.x - 4.2) < 1.0e-6);
    assert(std::abs(backExtended.end.y - 5.6) < 1.0e-6);
    assert(scaledEdge.start == (jaguar::Point2d{-1.5, -2.0}));
    assert(scaledEdge.end == (jaguar::Point2d{4.5, 6.0}));
    assert(reversedEdge.start == diagonal.Value().end);
    assert(reversedEdge.end == diagonal.Value().start);
    jaguar::Edge2d transformedEdge = diagonal.Value();
    transformedEdge.TransformRustF32(
        jaguar::Transform2d::FromRotationTranslation(
            0.5 * std::acos(-1.0), {10.0, 20.0}));
    assert(transformedEdge.start == (jaguar::Point2d{10.0, 20.0}));
    assert(std::abs(transformedEdge.end.x - 6.0) < 1.0e-6);
    assert(std::abs(transformedEdge.end.y - 23.0) < 1.0e-6);
    assert(diagonal.Value().BoundingBox().Contains({1.5, 2.0}));

    const auto crossingA = jaguar::Edge2d::Create({0.0, 0.0}, {10.0, 10.0});
    const auto crossingB = jaguar::Edge2d::Create({0.0, 10.0}, {10.0, 0.0});
    assert(crossingA.HasValue() && crossingB.HasValue());
    assert(crossingA.Value().Intersects(crossingB.Value()));
    const auto crossingLocation =
        crossingA.Value().IntersectionPointRustF32(crossingB.Value());
    assert(crossingLocation.has_value());
    assert(crossingLocation.value() == (jaguar::Point2d{5.0, 5.0}));

    const auto parallel = jaguar::Edge2d::Create({0.0, 1.0}, {10.0, 11.0});
    assert(parallel.HasValue());
    assert(!crossingA.Value().Intersects(parallel.Value()));
    assert(!crossingA.Value().IntersectionPointRustF32(parallel.Value()).has_value());
    const auto endpointEdge = jaguar::Edge2d::Create({0.0, 0.0}, {2.0, 0.0});
    const auto touchingEdge = jaguar::Edge2d::Create({2.0, 0.0}, {2.0, 1.0});
    const auto collinearEdge = jaguar::Edge2d::Create({1.0, 0.0}, {3.0, 0.0});
    assert(endpointEdge.HasValue() && touchingEdge.HasValue() && collinearEdge.HasValue());
    const auto endpointIntersection =
        endpointEdge.Value().IntersectionPointRustF32(touchingEdge.Value());
    assert(endpointIntersection.has_value());
    assert(endpointIntersection.value() == (jaguar::Point2d{2.0, 0.0}));
    assert(!endpointEdge.Value().IntersectionPointRustF32(collinearEdge.Value()).has_value());

    const jaguar::Rect2d collisionBounds{0.0, 0.0, 2.0, 2.0};
    const auto utilityRect = jaguar::Rect2d::Create(0.0, 0.0, 4.0, 2.0);
    const auto diagonalRect = jaguar::Rect2d::FromDiagonalCornersRustF32(
        {4.0, 5.0}, {-2.0, -1.0});
    assert(utilityRect.HasValue() && diagonalRect.HasValue());
    assert(diagonalRect.Value().xMin == -2.0 && diagonalRect.Value().yMin == -1.0);
    assert(diagonalRect.Value().xMax == 4.0 && diagonalRect.Value().yMax == 5.0);
    const jaguar::Rect2d squareRect = utilityRect.Value().InflateToSquareRustF32();
    assert(squareRect.xMin == 0.0 && squareRect.yMin == -1.0);
    assert(squareRect.xMax == 4.0 && squareRect.yMax == 3.0);
    const auto resizedRect = utilityRect.Value().ResizeByRustF32(-0.5, -0.5);
    const auto scaledRect = utilityRect.Value().ScaledRustF32(1.5);
    assert(resizedRect.has_value() && scaledRect.HasValue());
    assert(resizedRect->xMin == 0.5 && resizedRect->yMin == 0.5);
    assert(resizedRect->xMax == 3.5 && resizedRect->yMax == 1.5);
    assert(scaledRect.Value().xMin == -1.0 && scaledRect.Value().yMin == -0.5);
    assert(scaledRect.Value().xMax == 5.0 && scaledRect.Value().yMax == 2.5);
    assert(!utilityRect.Value().ResizeByRustF32(-2.0, 0.0).has_value());
    assert(!utilityRect.Value().ScaledRustF32(0.0).HasValue());
    assert(utilityRect.Value().WidthRustF32() == 4.0);
    assert(utilityRect.Value().HeightRustF32() == 2.0);
    assert(utilityRect.Value().AreaRustF32() == 8.0);
    assert(std::abs(utilityRect.Value().DiameterRustF32() - std::sqrt(20.0)) < 1.0e-6);
    const auto rectCorners = utilityRect.Value().CornersRustF32();
    assert(rectCorners[0] == (jaguar::Point2d{4.0, 2.0}));
    assert(rectCorners[1] == (jaguar::Point2d{0.0, 2.0}));
    assert(rectCorners[2] == (jaguar::Point2d{0.0, 0.0}));
    assert(rectCorners[3] == (jaguar::Point2d{4.0, 0.0}));
    const std::vector<jaguar::Edge2d> rectEdges = utilityRect.Value().EdgesRustF32();
    const std::vector<jaguar::Edge2d> rectSides = utilityRect.Value().SidesRustF32();
    assert(rectEdges.size() == 4U && rectSides.size() == 4U);
    assert(rectEdges[0].start == (jaguar::Point2d{4.0, 2.0}));
    assert(rectEdges[0].end == (jaguar::Point2d{0.0, 2.0}));
    assert(rectEdges[1].start == (jaguar::Point2d{0.0, 2.0}));
    assert(rectEdges[1].end == (jaguar::Point2d{0.0, 0.0}));
    assert(rectEdges[2].start == (jaguar::Point2d{0.0, 0.0}));
    assert(rectEdges[2].end == (jaguar::Point2d{4.0, 0.0}));
    assert(rectEdges[3].start == (jaguar::Point2d{4.0, 0.0}));
    assert(rectEdges[3].end == (jaguar::Point2d{4.0, 2.0}));
    const auto overlapRect = jaguar::Rect2d::Create(3.0, 1.0, 5.0, 4.0);
    assert(overlapRect.HasValue());
    const auto intersectionRect = jaguar::Rect2d::IntersectionRustF32(
        utilityRect.Value(), overlapRect.Value());
    const jaguar::Rect2d boundingRect = jaguar::Rect2d::BoundingRectRustF32(
        utilityRect.Value(), overlapRect.Value());
    assert(intersectionRect.has_value());
    assert(intersectionRect->xMin == 3.0 && intersectionRect->yMin == 1.0);
    assert(intersectionRect->xMax == 4.0 && intersectionRect->yMax == 2.0);
    assert(boundingRect.xMin == 0.0 && boundingRect.yMin == 0.0);
    assert(boundingRect.xMax == 5.0 && boundingRect.yMax == 4.0);
    const jaguar::Point2d insideRectPoint{1.0, 1.0};
    const jaguar::Point2d outsideRectPoint{-3.0, 4.0};
    assert(utilityRect.Value().DistanceToRustF32(insideRectPoint) == 0.0);
    assert(std::abs(utilityRect.Value().DistanceToRustF32(outsideRectPoint) -
                    std::sqrt(13.0)) < 1.0e-6);
    assert(utilityRect.Value().SquaredDistanceToRustF32(outsideRectPoint) == 13.0);
    const auto insideRectSeparation =
        utilityRect.Value().SeparationDistanceRustF32(insideRectPoint);
    const auto outsideRectSeparation =
        utilityRect.Value().SeparationDistanceRustF32(outsideRectPoint);
    const auto insideRectSquaredSeparation =
        utilityRect.Value().SquaredSeparationDistanceRustF32(insideRectPoint);
    const auto outsideRectSquaredSeparation =
        utilityRect.Value().SquaredSeparationDistanceRustF32(outsideRectPoint);
    assert(insideRectSeparation.first == jaguar::GeoPosition::Interior &&
           insideRectSeparation.second == 1.0);
    assert(outsideRectSeparation.first == jaguar::GeoPosition::Exterior &&
           std::abs(outsideRectSeparation.second - std::sqrt(13.0)) < 1.0e-6);
    assert(insideRectSquaredSeparation.first == jaguar::GeoPosition::Interior &&
           insideRectSquaredSeparation.second == 1.0);
    assert(outsideRectSquaredSeparation.first == jaguar::GeoPosition::Exterior &&
           outsideRectSquaredSeparation.second == 13.0);
    const auto distancePolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 2.0}, {0.0, 2.0}});
    assert(distancePolygon.HasValue());
    assert(distancePolygon.Value().DistanceToRustF32(insideRectPoint) == 0.0);
    assert(distancePolygon.Value().SquaredDistanceToRustF32(insideRectPoint) == 0.0);
    assert(std::abs(distancePolygon.Value().DistanceToRustF32(outsideRectPoint) -
                    std::sqrt(13.0)) < 1.0e-6);
    assert(distancePolygon.Value().SquaredDistanceToRustF32(outsideRectPoint) == 13.0);
    const auto insidePolygonSeparation =
        distancePolygon.Value().SeparationDistanceRustF32(insideRectPoint);
    const auto outsidePolygonSeparation =
        distancePolygon.Value().SeparationDistanceRustF32(outsideRectPoint);
    const auto insidePolygonSquaredSeparation =
        distancePolygon.Value().SquaredSeparationDistanceRustF32(insideRectPoint);
    const auto outsidePolygonSquaredSeparation =
        distancePolygon.Value().SquaredSeparationDistanceRustF32(outsideRectPoint);
    assert(insidePolygonSeparation.first == jaguar::GeoPosition::Interior &&
           insidePolygonSeparation.second == 1.0);
    assert(outsidePolygonSeparation.first == jaguar::GeoPosition::Exterior &&
           std::abs(outsidePolygonSeparation.second - std::sqrt(13.0)) < 1.0e-6);
    assert(insidePolygonSquaredSeparation.first == jaguar::GeoPosition::Interior &&
           insidePolygonSquaredSeparation.second == 1.0);
    assert(outsidePolygonSquaredSeparation.first == jaguar::GeoPosition::Exterior &&
           outsidePolygonSquaredSeparation.second == 13.0);

    const auto crossingRectEdge = jaguar::Edge2d::Create({-1.0, 1.0}, {3.0, 1.0});
    const auto outsideRectEdge = jaguar::Edge2d::Create({-1.0, 3.0}, {3.0, 3.0});
    const auto endpointInsideRectEdge = jaguar::Edge2d::Create({1.0, 1.0}, {3.0, 3.0});
    const auto boundaryRectEdge = jaguar::Edge2d::Create({-1.0, 0.0}, {3.0, 0.0});
    assert(crossingRectEdge.HasValue() && outsideRectEdge.HasValue());
    assert(endpointInsideRectEdge.HasValue() && boundaryRectEdge.HasValue());
    assert(crossingRectEdge.Value().IntersectsRustF32(collisionBounds));
    assert(!outsideRectEdge.Value().IntersectsRustF32(collisionBounds));
    assert(endpointInsideRectEdge.Value().IntersectsRustF32(collisionBounds));
    assert(boundaryRectEdge.Value().IntersectsRustF32(collisionBounds));
    const auto nanOutsideRectEdge = jaguar::Edge2d::Create(
        {std::numeric_limits<double>::quiet_NaN(), 3.0}, {-1.0, 1.0});
    assert(nanOutsideRectEdge.HasValue());
    assert(!nanOutsideRectEdge.Value().IntersectsRustF32(collisionBounds));

    const auto invalidEdge = jaguar::Edge2d::Create({1.0, 1.0}, {1.0, 1.0});
    assert(!invalidEdge.HasValue());
    const double halfFloatUlpAtOne = static_cast<double>(
        std::numeric_limits<float>::epsilon() * 0.5F);
    const auto f32CollapsedEdge = jaguar::Edge2d::Create(
        {1.0, 0.0}, {1.0 + halfFloatUlpAtOne, 0.0});
    assert(!f32CollapsedEdge.HasValue());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const auto nanEdge = jaguar::Edge2d::Create({nan, 0.0}, {1.0, 1.0});
    assert(nanEdge.HasValue());
    const jaguar::Rect2d nanEdgeBounds = nanEdge.Value().BoundingBox();
    assert(nanEdgeBounds.xMin == 1.0 && nanEdgeBounds.xMax == 1.0);
    const auto infiniteEdge = jaguar::Edge2d::Create({inf, 0.0}, {1.0, 1.0});
    assert(infiniteEdge.HasValue());

    const auto circle = jaguar::Circle2d::Create({2.0, 3.0}, 5.0);
    assert(circle.HasValue());
    assert(circle.Value().Contains({2.0, 3.0}));
    assert(!circle.Value().Contains({8.0, 3.0}));
    const double beyondF32 = static_cast<double>(
        std::numeric_limits<float>::max()) * 2.0;
    assert(!jaguar::Circle2d::Create({beyondF32, 0.0}, 1.0).HasValue());
    assert(!jaguar::Circle2d::Create({0.0, 0.0}, beyondF32).HasValue());
    const double negativeRadiusRoundedToZero =
        -static_cast<double>(std::numeric_limits<float>::denorm_min()) * 0.25;
    assert(jaguar::Circle2d::Create(
        {0.0, 0.0}, negativeRadiusRoundedToZero).HasValue());
    assert(!jaguar::Rect2d::Create(
        1.0, 0.0, 1.0 + halfFloatUlpAtOne, 1.0).HasValue());
    assert(!jaguar::Rect2d::Create(
        beyondF32, 0.0, beyondF32 * 1.5, 1.0).HasValue());
    const jaguar::Rect2d hazardBounds{0.0, 0.0, 2.0, 2.0};
    const jaguar::Rect2d nearZeroBounds{
        -static_cast<double>(std::numeric_limits<float>::epsilon() * 0.25F),
        0.5, 1.0, 1.5};
    const jaguar::Point2d nearBoundsPoint{
        -static_cast<double>(std::numeric_limits<float>::epsilon() * 0.25F), 1.0};
    const jaguar::Point2d outsideBoundsPoint{
        -static_cast<double>(std::numeric_limits<float>::epsilon() * 2.0F), 1.0};
    assert(!hazardBounds.ContainsRustF32(nearBoundsPoint));
    assert(hazardBounds.AlmostContainsRustF32(nearBoundsPoint));
    assert(!hazardBounds.AlmostContainsRustF32(outsideBoundsPoint));
    assert(hazardBounds.AlmostRelationToRustF32(nearZeroBounds) ==
           jaguar::RectRelation::Surrounding);
    assert(hazardBounds.RelationToRustF32(nearZeroBounds) ==
           jaguar::RectRelation::Intersecting);
    const jaguar::Rect2d enclosedBounds{-1.0, -1.0, 3.0, 3.0};
    const jaguar::Rect2d intersectingBounds{1.5, 1.5, 3.0, 3.0};
    const jaguar::Rect2d disjointBounds{3.0, 0.0, 4.0, 1.0};
    assert(hazardBounds.AlmostRelationToRustF32(enclosedBounds) ==
           jaguar::RectRelation::Enclosed);
    assert(hazardBounds.AlmostRelationToRustF32(intersectingBounds) ==
           jaguar::RectRelation::Intersecting);
    assert(hazardBounds.AlmostRelationToRustF32(disjointBounds) ==
           jaguar::RectRelation::Disjoint);
    const auto nearCornerSquare = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(nearCornerSquare.HasValue());
    const float nearCornerOffset =
        std::numeric_limits<float>::epsilon() * 0.25F;
    assert(nearCornerSquare.Value().ContainsRustF32(
        {2.0F - nearCornerOffset, nearCornerOffset}));
    const float tinySide = std::numeric_limits<float>::epsilon() * 0.5F;
    const auto tinySquare = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {tinySide, 0.0}, {tinySide, tinySide}, {0.0, tinySide}});
    assert(tinySquare.HasValue());
    assert(tinySquare.Value().ContainsRustF32(
        {tinySide * 0.5F, tinySide * 0.5F}));

    const auto wideCircle = jaguar::Circle2d::Create({0.0, 0.0}, 12345.678);
    assert(wideCircle.HasValue());
    assert(std::abs(wideCircle.Value().AreaRustF32() - 478828256.0) < 1.0);
    const auto unitCircle = jaguar::Circle2d::Create({0.0, 0.0}, 1.0);
    const auto tangentCircle = jaguar::Circle2d::Create({2.0, 0.0}, 1.0);
    const auto separatedCircle = jaguar::Circle2d::Create({2.01, 0.0}, 1.0);
    const auto tangentCircleEdge = jaguar::Edge2d::Create({-2.0, 1.0}, {2.0, 1.0});
    const auto separatedCircleEdge = jaguar::Edge2d::Create({-2.0, 1.01}, {2.0, 1.01});
    const jaguar::Rect2d tangentCircleRect{1.0, 0.0, 2.0, 1.0};
    const jaguar::Rect2d separatedCircleRect{1.01, 0.0, 2.0, 1.0};
    assert(unitCircle.HasValue() && tangentCircle.HasValue() && separatedCircle.HasValue());
    assert(tangentCircleEdge.HasValue() && separatedCircleEdge.HasValue());
    assert(unitCircle.Value().IntersectsRustF32(tangentCircle.Value()));
    assert(!unitCircle.Value().IntersectsRustF32(separatedCircle.Value()));
    assert(unitCircle.Value().IntersectsRustF32(tangentCircleEdge.Value()));
    assert(!unitCircle.Value().IntersectsRustF32(separatedCircleEdge.Value()));
    assert(unitCircle.Value().IntersectsRustF32(tangentCircleRect));
    assert(!unitCircle.Value().IntersectsRustF32(separatedCircleRect));
    const jaguar::Point2d insideCirclePoint{0.25, 0.0};
    const jaguar::Point2d outsideCirclePoint{3.0, 0.0};
    const jaguar::Circle2d distanceCircle{{3.0, 0.0}, 1.0};
    const jaguar::Edge2d overlappingCircleEdge{{-2.0, 0.25}, {2.0, 0.25}};
    assert(unitCircle.Value().DiameterRustF32() == 2.0);
    assert(unitCircle.Value().DistanceToRustF32(insideCirclePoint) == 0.0);
    assert(unitCircle.Value().DistanceToRustF32(outsideCirclePoint) == 2.0);
    assert(unitCircle.Value().SquaredDistanceToRustF32(outsideCirclePoint) == 4.0);
    assert(unitCircle.Value().DistanceToRustF32(distanceCircle) == 1.0);
    assert(unitCircle.Value().DistanceToRustF32(overlappingCircleEdge) == 0.0);
    assert(unitCircle.Value().SquaredDistanceToRustF32(distanceCircle) == 1.0);
    assert(unitCircle.Value().SquaredDistanceToRustF32(overlappingCircleEdge) == 0.0);
    assert(unitCircle.Value().SquaredDistanceToRustF32(insideCirclePoint) == 0.0);
    const auto insidePointSeparation =
        unitCircle.Value().SeparationDistanceRustF32(insideCirclePoint);
    const auto outsidePointSeparation =
        unitCircle.Value().SeparationDistanceRustF32(outsideCirclePoint);
    const auto outsideCircleSeparation =
        unitCircle.Value().SeparationDistanceRustF32(distanceCircle);
    const auto insideEdgeSeparation =
        unitCircle.Value().SeparationDistanceRustF32(overlappingCircleEdge);
    const auto squaredInsidePointSeparation =
        unitCircle.Value().SquaredSeparationDistanceRustF32(insideCirclePoint);
    const auto squaredOutsidePointSeparation =
        unitCircle.Value().SquaredSeparationDistanceRustF32(outsideCirclePoint);
    const auto squaredOutsideCircleSeparation =
        unitCircle.Value().SquaredSeparationDistanceRustF32(distanceCircle);
    const auto squaredInsideEdgeSeparation =
        unitCircle.Value().SquaredSeparationDistanceRustF32(overlappingCircleEdge);
    assert(insidePointSeparation.first == jaguar::GeoPosition::Interior &&
           insidePointSeparation.second == 0.75);
    assert(outsidePointSeparation.first == jaguar::GeoPosition::Exterior &&
           outsidePointSeparation.second == 2.0);
    assert(outsideCircleSeparation.first == jaguar::GeoPosition::Exterior &&
           outsideCircleSeparation.second == 1.0);
    assert(insideEdgeSeparation.first == jaguar::GeoPosition::Interior &&
           insideEdgeSeparation.second == 0.75);
    assert(squaredInsidePointSeparation.first == jaguar::GeoPosition::Interior &&
           squaredInsidePointSeparation.second == 0.5625);
    assert(squaredOutsidePointSeparation.first == jaguar::GeoPosition::Exterior &&
           squaredOutsidePointSeparation.second == 4.0);
    assert(squaredOutsideCircleSeparation.first == jaguar::GeoPosition::Exterior &&
           squaredOutsideCircleSeparation.second == 1.0);
    assert(squaredInsideEdgeSeparation.first == jaguar::GeoPosition::Interior &&
           squaredInsideEdgeSeparation.second == 0.5625);
    const auto circleBounds = jaguar::Circle2d::BoundingCircleRustF32(
        {unitCircle.Value(), jaguar::Circle2d{{4.0, 0.0}, 2.0}});
    assert(circleBounds.HasValue());
    assert(circleBounds.Value().center == (jaguar::Point2d{2.5, 0.0}));
    assert(circleBounds.Value().radius == 3.5);
    assert(!jaguar::Circle2d::BoundingCircleRustF32({}).HasValue());
    jaguar::Circle2d transformedCircle{{1.0, 0.0}, 1.0};
    transformedCircle.TransformRustF32(
        jaguar::Transform2d::FromRotationTranslation(
            0.5 * std::acos(-1.0), {10.0, 20.0}));
    assert(transformedCircle.center == (jaguar::Point2d{10.0, 21.0}));
    assert(transformedCircle.radius == 1.0);
    const auto roundedCircle = jaguar::Circle2d::Create(
        {16777217.0, 0.0}, 1.00000006);
    assert(roundedCircle.HasValue());
    assert(roundedCircle.Value().center.x == 16777216.0);
    assert(roundedCircle.Value().radius ==
           static_cast<double>(static_cast<float>(1.00000006)));
    const auto boundaryCircle = jaguar::Circle2d::Create({16777216.0, 0.0}, 1.0);
    assert(boundaryCircle.HasValue());
    const jaguar::Rect2d rustCircleBounds = boundaryCircle.Value().BoundingBoxRustF32();
    assert(rustCircleBounds.xMin == 16777215.0);
    assert(rustCircleBounds.xMax == 16777216.0);
    assert(boundaryCircle.Value().ContainsRustF32({16777217.0, 0.0}));

    const float maxF32 = std::numeric_limits<float>::max();
    const jaguar::Rect2d extremeRect{
        static_cast<double>(maxF32 * 0.5F),
        static_cast<double>(maxF32 * 0.5F),
        static_cast<double>(maxF32),
        static_cast<double>(maxF32)};
    const jaguar::Point2d extremeCentroid = extremeRect.CentroidRustF32();
    assert(std::isfinite(extremeCentroid.x));
    assert(std::isfinite(extremeCentroid.y));
    const auto extremeQuadrants = extremeRect.QuadrantsRustF32();
    assert(extremeQuadrants[0].xMin == extremeCentroid.x);
    assert(extremeQuadrants[0].yMin == extremeCentroid.y);
    const auto floatBits = [](double value) {
        const float converted = static_cast<float>(value);
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &converted, sizeof(bits));
        return bits;
    };
    assert(floatBits(extremeCentroid.x) == 0x7f3fffffU);
    assert(floatBits(extremeCentroid.y) == 0x7f3fffffU);
    assert(floatBits(extremeQuadrants[0].xMin) == 0x7f3fffffU);
    assert(floatBits(extremeQuadrants[0].yMin) == 0x7f3fffffU);
    assert(floatBits(extremeQuadrants[0].xMax) == 0x7f7fffffU);
    assert(floatBits(extremeQuadrants[0].yMax) == 0x7f7fffffU);

    const auto zeroRadiusCircle = jaguar::Circle2d::Create({0.0, 0.0}, 0.0);
    assert(zeroRadiusCircle.HasValue());
    assert(zeroRadiusCircle.Value().Area() == 0.0);
    assert(zeroRadiusCircle.Value().Contains({0.0, 0.0}));
    const auto negativeRadiusCircle = jaguar::Circle2d::Create({0.0, 0.0}, -0.1);
    assert(!negativeRadiusCircle.HasValue());
    const auto negativeZeroRadiusCircle = jaguar::Circle2d::Create({0.0, 0.0}, -0.0);
    assert(negativeZeroRadiusCircle.HasValue());

    const jaguar::Transform2d transform =
        jaguar::Transform2d::FromRotationTranslation(0.5 * std::acos(-1.0), {10.0, 20.0});
    const jaguar::Point2d transformed = transform.Apply({1.0, 0.0});
    assert(std::abs(transformed.x - 10.0) < 1.0e-12);
    assert(std::abs(transformed.y - 21.0) < 1.0e-12);

    const jaguar::Point2d restored = transform.Inverse().Apply(transformed);
    assert(std::abs(restored.x - 1.0) < 1.0e-12);
    assert(std::abs(restored.y) < 1.0e-12);

    const jaguar::Transform2d transformA{
        100000.123456789, {1.23456789, -7.65432109}};
    const jaguar::Transform2d transformB{
        -99999.87654321, {0.333333333, 0.777777777}};
    const jaguar::Transform2d followedBy = transformA.FollowedByRustF32(transformB);
    assert(std::abs(followedBy.rotationRad - 0.25) < 1.0e-6);
    assert(std::abs(followedBy.translation.x - (-2.110505104)) < 1.0e-6);
    assert(std::abs(followedBy.translation.y - 8.135797501) < 1.0e-6);

    const jaguar::Transform2d largeAngleInverse = transformA.InverseRustF32();
    assert(std::abs(largeAngleInverse.rotationRad - 3.052349091) < 1.0e-6);
    assert(std::abs(largeAngleInverse.translation.x - 0.547462106) < 1.0e-6);
    assert(std::abs(largeAngleInverse.translation.y - (-7.733891487)) < 1.0e-6);

    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}});
    assert(polygon.HasValue());
    const auto invalidTransform = polygon.Value().Transformed(
        jaguar::Transform2d::FromRotationTranslation(
            std::numeric_limits<double>::quiet_NaN(),
            {0.0, std::numeric_limits<double>::infinity()}));
    assert(!invalidTransform.HasValue());
    assert(invalidTransform.GetError().code == jaguar::ErrorCode::InvalidInput);

    const jaguar::NumericPolicy strictPolicy{
        1.0e-9, 1.0e-7, 1.0, 1.0e-3};
    const auto strictPolygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {0.5, 0.0}, {0.0, 0.5}}, strictPolicy);
    assert(!strictPolygon.HasValue());
    assert(strictPolygon.GetError().code == jaguar::ErrorCode::ZeroArea);
}
