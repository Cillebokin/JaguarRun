#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Geometry/Circle.h"
#include "JaguarCore/Geometry/ConvexHull.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/OriginalShape.h"
#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/Geometry/ShapeModification.h"
#include "JaguarCore/Geometry/Transformation.h"

#include <cctype>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

using ReferenceMap = std::map<std::string, std::string>;

ReferenceMap loadGeometryReference()
{
    const std::string contents = loadReference("geometry_primitives.txt");
    assert(!contents.empty());

    ReferenceMap reference;
    std::istringstream input(contents);
    for (std::string line; std::getline(input, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::size_t separator = line.find('=');
        if (line.empty() || separator == std::string::npos || line[0] == '#')
            continue;
        reference.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    assert(!reference.empty());
    return reference;
}

const std::string& referenceValue(const ReferenceMap& reference, const char* key)
{
    const auto found = reference.find(key);
    assert(found != reference.end());
    return found->second;
}

std::vector<double> numericValues(const std::string& text)
{
    std::vector<double> values;
    for (std::size_t index = 0; index < text.size();)
    {
        const char current = text[index];
        const bool valueBoundary = index == 0 || text[index - 1] == '=' ||
            text[index - 1] == ':' || text[index - 1] == ',' ||
            text[index - 1] == ';' || text[index - 1] == '>';
        const bool startsNumber =
            valueBoundary && (std::isdigit(static_cast<unsigned char>(current)) || current == '.' ||
            ((current == '-' || current == '+') && index + 1 < text.size() &&
             (std::isdigit(static_cast<unsigned char>(text[index + 1])) ||
              text[index + 1] == '.')));
        if (!startsNumber)
        {
            ++index;
            continue;
        }

        char* end = nullptr;
        const double value = std::strtod(text.c_str() + index, &end);
        if (end == text.c_str() + index)
        {
            ++index;
            continue;
        }
        values.push_back(value);
        index = static_cast<std::size_t>(end - text.c_str());
    }
    return values;
}

std::vector<bool> booleanValues(const std::string& text)
{
    std::vector<bool> values;
    std::size_t position = 0;
    while (position < text.size())
    {
        const std::size_t yes = text.find("true", position);
        const std::size_t no = text.find("false", position);
        if (yes == std::string::npos && no == std::string::npos)
            break;
        if (yes != std::string::npos && (no == std::string::npos || yes < no))
        {
            values.push_back(true);
            position = yes + 4;
        }
        else
        {
            values.push_back(false);
            position = no + 5;
        }
    }
    return values;
}

std::vector<jaguar::GeoPosition> geoPositions(const std::string& text)
{
    std::vector<jaguar::GeoPosition> values;
    std::size_t position = 0;
    while (position < text.size())
    {
        const std::size_t interior = text.find("Interior", position);
        const std::size_t exterior = text.find("Exterior", position);
        if (interior == std::string::npos && exterior == std::string::npos)
            break;
        if (interior != std::string::npos &&
            (exterior == std::string::npos || interior < exterior))
        {
            values.push_back(jaguar::GeoPosition::Interior);
            position = interior + 8;
        }
        else
        {
            values.push_back(jaguar::GeoPosition::Exterior);
            position = exterior + 8;
        }
    }
    return values;
}

void expectRustNumbers(const ReferenceMap& reference,
                       const char* key,
                       const std::vector<double>& actual)
{
    const std::vector<double> expected = numericValues(referenceValue(reference, key));
    if (expected.size() != actual.size())
    {
        std::fprintf(stderr,
                     "B1 reference %s field count: Rust %zu, C++ %zu\n",
                     key,
                     expected.size(),
                     actual.size());
    }
    assert(expected.size() == actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        assert(jaguar::detail::AlmostEqualRustF32(
            static_cast<float>(actual[index]), static_cast<float>(expected[index])));
    }
}

void expectRustBooleans(const ReferenceMap& reference,
                        const char* key,
                        const std::vector<bool>& actual)
{
    assert(booleanValues(referenceValue(reference, key)) == actual);
}

void expectRustPositions(const ReferenceMap& reference,
                         const char* key,
                         const std::vector<jaguar::GeoPosition>& actual)
{
    assert(geoPositions(referenceValue(reference, key)) == actual);
}

void runB1PrimitiveReferenceTests()
{
    const ReferenceMap reference = loadGeometryReference();
    const float normalizeInputs[] = {-7.0F, -1.0F, 7.0F, 1000000.0F};
    std::vector<double> normalizedAngles;
    for (const float angle : normalizeInputs)
        normalizedAngles.push_back(jaguar::NormalizeRotationRustF32(angle));
    expectRustNumbers(reference, "geometry.normalize_rotation", normalizedAngles);
    expectRustBooleans(reference, "geometry.normalize_rotation_nonfinite", {
        std::isnan(jaguar::NormalizeRotationRustF32(
            std::numeric_limits<float>::quiet_NaN())),
        std::isnan(jaguar::NormalizeRotationRustF32(
            std::numeric_limits<float>::infinity())),
        std::isnan(jaguar::NormalizeRotationRustF32(
            -std::numeric_limits<float>::infinity()))});
    expectRustBooleans(reference, "geometry.normalize_rotation_negative_zero", {
        std::signbit(jaguar::NormalizeRotationRustF32(-0.0F))});

    const jaguar::Transform2d emptyTransform = jaguar::Transform2d::Identity();
    const jaguar::Transform2d decomposedTransform =
        jaguar::Transform2d::FromRotationTranslation(0.37, {1.25, -0.75});
    const jaguar::Transform2d roundTrippedTransform =
        decomposedTransform.FollowedByRustF32(emptyTransform);
    expectRustNumbers(reference, "geometry.dtransformation", {
        emptyTransform.rotationRad,
        emptyTransform.translation.x,
        emptyTransform.translation.y,
        decomposedTransform.rotationRad,
        decomposedTransform.translation.x,
        decomposedTransform.translation.y,
        roundTrippedTransform.rotationRad,
        roundTrippedTransform.translation.x,
        roundTrippedTransform.translation.y});
    const auto appliedTransformPoint = decomposedTransform.ApplyRustF32({2.0, -3.0});
    expectRustNumbers(reference, "geometry.transformation_apply", {
        appliedTransformPoint.x, appliedTransformPoint.y});
    const auto rotateThenTranslate =
        jaguar::Transform2d::FromRotationTranslation(0.5, {0.0, 0.0})
            .FollowedByRustF32(
                jaguar::Transform2d::FromRotationTranslation(0.0, {3.0, -2.0}));
    const auto translateThenRotate =
        jaguar::Transform2d::FromRotationTranslation(0.0, {3.0, -2.0})
            .FollowedByRustF32(
                jaguar::Transform2d::FromRotationTranslation(0.5, {0.0, 0.0}));
    expectRustNumbers(reference, "geometry.transformation_order", {
        rotateThenTranslate.rotationRad,
        rotateThenTranslate.translation.x,
        rotateThenTranslate.translation.y,
        translateThenRotate.rotationRad,
        translateThenRotate.translation.x,
        translateThenRotate.translation.y});

    const auto metricCircle = jaguar::Circle2d::Create({2.0, 3.0}, 5.0);
    assert(metricCircle.HasValue());
    const auto metricCircleBounds = metricCircle.Value().BoundingBoxRustF32();
    expectRustNumbers(reference, "geometry.circle_basics", {
        metricCircle.Value().AreaRustF32(),
        metricCircleBounds.xMin, metricCircleBounds.yMin,
        metricCircleBounds.xMax, metricCircleBounds.yMax,
        metricCircle.Value().DiameterRustF32()});
    expectRustBooleans(reference, "geometry.circle_contains", {
        metricCircle.Value().ContainsRustF32({2.0, 3.0})});
    const auto zeroRadius = jaguar::Circle2d::Create({0.0, 0.0}, 0.0);
    expectRustBooleans(reference, "geometry.circle_construction", {
        zeroRadius.HasValue(),
        jaguar::Circle2d::Create({0.0, 0.0}, -1.0).HasValue(),
        jaguar::Circle2d::Create(
            {std::numeric_limits<double>::infinity(), 0.0}, 1.0).HasValue(),
        jaguar::Circle2d::Create(
            {0.0, 0.0}, std::numeric_limits<double>::infinity()).HasValue()});

    const auto unitCircle = jaguar::Circle2d::Create({0.0, 0.0}, 1.0);
    const auto tangentCircle = jaguar::Circle2d::Create({2.0, 0.0}, 1.0);
    const auto separatedCircle = jaguar::Circle2d::Create({2.01, 0.0}, 1.0);
    const auto tangentCircleEdge = jaguar::Edge2d::Create({-2.0, 1.0}, {2.0, 1.0});
    const auto separatedCircleEdge = jaguar::Edge2d::Create({-2.0, 1.01}, {2.0, 1.01});
    const auto tangentCircleRect = jaguar::Rect2d::Create(1.0, 0.0, 2.0, 1.0);
    const auto separatedCircleRect = jaguar::Rect2d::Create(1.01, 0.0, 2.0, 1.0);
    assert(unitCircle.HasValue() && tangentCircle.HasValue() && separatedCircle.HasValue());
    assert(tangentCircleEdge.HasValue() && separatedCircleEdge.HasValue());
    assert(tangentCircleRect.HasValue() && separatedCircleRect.HasValue());

    expectRustBooleans(reference, "circle.collision", {
        unitCircle.Value().IntersectsRustF32(tangentCircle.Value()),
        unitCircle.Value().IntersectsRustF32(separatedCircle.Value()),
        unitCircle.Value().IntersectsRustF32(tangentCircleEdge.Value()),
        unitCircle.Value().IntersectsRustF32(separatedCircleEdge.Value()),
        unitCircle.Value().IntersectsRustF32(tangentCircleRect.Value()),
        unitCircle.Value().IntersectsRustF32(separatedCircleRect.Value())});

    const jaguar::Point2d insideCirclePoint{0.25, 0.0};
    const jaguar::Point2d outsideCirclePoint{3.0, 0.0};
    const auto outsideCircle = jaguar::Circle2d::Create({3.0, 0.0}, 1.0);
    const auto overlappingCircleEdge = jaguar::Edge2d::Create({-2.0, 0.25}, {2.0, 0.25});
    assert(outsideCircle.HasValue() && overlappingCircleEdge.HasValue());
    expectRustNumbers(reference, "circle.distance", {
        unitCircle.Value().DiameterRustF32(),
        unitCircle.Value().DistanceToRustF32(insideCirclePoint),
        unitCircle.Value().DistanceToRustF32(outsideCirclePoint),
        unitCircle.Value().SquaredDistanceToRustF32(outsideCirclePoint),
        unitCircle.Value().DistanceToRustF32(outsideCircle.Value()),
        unitCircle.Value().SquaredDistanceToRustF32(outsideCircle.Value()),
        unitCircle.Value().DistanceToRustF32(overlappingCircleEdge.Value()),
        unitCircle.Value().SquaredDistanceToRustF32(overlappingCircleEdge.Value())});

    const auto pointInsideSeparation =
        unitCircle.Value().SeparationDistanceRustF32(insideCirclePoint);
    const auto pointOutsideSeparation =
        unitCircle.Value().SeparationDistanceRustF32(outsideCirclePoint);
    const auto circleOutsideSeparation =
        unitCircle.Value().SeparationDistanceRustF32(outsideCircle.Value());
    const auto edgeInsideSeparation =
        unitCircle.Value().SeparationDistanceRustF32(overlappingCircleEdge.Value());
    expectRustPositions(reference, "circle.separation", {
        pointInsideSeparation.first, pointOutsideSeparation.first,
        circleOutsideSeparation.first, edgeInsideSeparation.first});
    expectRustNumbers(reference, "circle.separation", {
        pointInsideSeparation.second, pointOutsideSeparation.second,
        circleOutsideSeparation.second, edgeInsideSeparation.second});

    const auto pointInsideSquared =
        unitCircle.Value().SquaredSeparationDistanceRustF32(insideCirclePoint);
    const auto pointOutsideSquared =
        unitCircle.Value().SquaredSeparationDistanceRustF32(outsideCirclePoint);
    const auto circleOutsideSquared =
        unitCircle.Value().SquaredSeparationDistanceRustF32(outsideCircle.Value());
    const auto edgeInsideSquared =
        unitCircle.Value().SquaredSeparationDistanceRustF32(overlappingCircleEdge.Value());
    expectRustPositions(reference, "circle.squared_separation", {
        pointInsideSquared.first, pointOutsideSquared.first,
        circleOutsideSquared.first, edgeInsideSquared.first});
    expectRustNumbers(reference, "circle.squared_separation", {
        pointInsideSquared.second, pointOutsideSquared.second,
        circleOutsideSquared.second, edgeInsideSquared.second});

    const auto boundingCircle = jaguar::Circle2d::BoundingCircleRustF32(
        {unitCircle.Value(), jaguar::Circle2d{{4.0, 0.0}, 2.0}});
    assert(boundingCircle.HasValue());
    expectRustNumbers(reference, "circle.bounding_circle", {
        boundingCircle.Value().center.x, boundingCircle.Value().center.y,
        boundingCircle.Value().radius, boundingCircle.Value().DiameterRustF32()});
    jaguar::Circle2d transformedCircle{{1.0, 0.0}, 1.0};
    transformedCircle.TransformRustF32(jaguar::Transform2d::FromRotationTranslation(
        0.5 * std::acos(-1.0), {10.0, 20.0}));
    expectRustNumbers(reference, "circle.transform", {
        transformedCircle.center.x, transformedCircle.center.y, transformedCircle.radius});

    const auto edge = jaguar::Edge2d::Create({0.0, 0.0}, {3.0, 4.0});
    assert(edge.HasValue());
    const auto front = edge.Value().ExtendedAtFrontRustF32(2.0);
    const auto back = edge.Value().ExtendedAtBackRustF32(2.0);
    const auto scaled = edge.Value().ScaledRustF32(2.0);
    const auto reversed = edge.Value().ReversedRustF32();
    const auto centroid = edge.Value().CentroidRustF32();
    expectRustNumbers(reference, "edge.operations", {
        edge.Value().XMinRustF32(), edge.Value().YMinRustF32(),
        edge.Value().XMaxRustF32(), edge.Value().YMaxRustF32(),
        edge.Value().LengthRustF32(), centroid.x, centroid.y,
        front.start.x, front.start.y, back.end.x, back.end.y,
        scaled.start.x, scaled.start.y, scaled.end.x, scaled.end.y,
        reversed.start.x, reversed.start.y, reversed.end.x, reversed.end.y});
    const auto closestPoint = edge.Value().ClosestPointRustF32({1.0, 2.0});
    expectRustNumbers(reference, "geometry.edge_closest", {
        closestPoint.x, closestPoint.y,
        edge.Value().DistanceToRustF32({1.0, 2.0}),
        edge.Value().SquaredDistanceToRustF32({1.0, 2.0})});
    const auto nonzeroEdge = jaguar::Edge2d::Create({0.0, 0.0}, {1.0, 0.0});
    expectRustBooleans(reference, "geometry.edge_construction", {
        nonzeroEdge.HasValue(),
        jaguar::Edge2d::Create({1.0, 1.0}, {1.0, 1.0}).HasValue(),
        jaguar::Edge2d::Create(
            {1.0, 0.0}, {1.0 + std::numeric_limits<float>::epsilon() * 0.5, 0.0})
            .HasValue()});

    const auto crossingA = jaguar::Edge2d::Create({0.0, 0.0}, {10.0, 10.0});
    const auto crossingB = jaguar::Edge2d::Create({0.0, 10.0}, {10.0, 0.0});
    const auto endpointA = jaguar::Edge2d::Create({0.0, 0.0}, {2.0, 0.0});
    const auto endpointB = jaguar::Edge2d::Create({2.0, 0.0}, {2.0, 1.0});
    const auto collinear = jaguar::Edge2d::Create({1.0, 0.0}, {3.0, 0.0});
    assert(crossingA.HasValue() && crossingB.HasValue() && endpointA.HasValue() &&
           endpointB.HasValue() && collinear.HasValue());
    const auto properIntersection =
        crossingA.Value().IntersectionPointRustF32(crossingB.Value());
    const auto endpointIntersection =
        endpointA.Value().IntersectionPointRustF32(endpointB.Value());
    assert(properIntersection.has_value() && endpointIntersection.has_value());
    expectRustNumbers(reference, "edge.intersection", {
        properIntersection->x, properIntersection->y,
        endpointIntersection->x, endpointIntersection->y});
    expectRustBooleans(reference, "geometry.edge_intersects", {
        crossingA.Value().IntersectsRustF32(crossingB.Value()),
        endpointA.Value().IntersectsRustF32(endpointB.Value()),
        endpointA.Value().IntersectsRustF32(collinear.Value())});
    expectRustBooleans(reference, "edge.intersection", {
        endpointA.Value().IntersectionPointRustF32(collinear.Value()).has_value()});
    jaguar::Edge2d transformedEdge = edge.Value();
    transformedEdge.TransformRustF32(jaguar::Transform2d::FromRotationTranslation(
        0.5 * std::acos(-1.0), {10.0, 20.0}));
    expectRustNumbers(reference, "edge.transform", {
        transformedEdge.start.x, transformedEdge.start.y,
        transformedEdge.end.x, transformedEdge.end.y});

    const auto utilityRect = jaguar::Rect2d::Create(0.0, 0.0, 4.0, 2.0);
    const auto diagonalRect = jaguar::Rect2d::FromDiagonalCornersRustF32(
        {4.0, 5.0}, {-2.0, -1.0});
    assert(utilityRect.HasValue() && diagonalRect.HasValue());
    const auto squareRect = utilityRect.Value().InflateToSquareRustF32();
    const auto resizedRect = utilityRect.Value().ResizeByRustF32(-0.5, -0.5);
    const auto scaledRect = utilityRect.Value().ScaledRustF32(1.5);
    assert(resizedRect.has_value() && scaledRect.HasValue());
    expectRustNumbers(reference, "rect.operations", {
        diagonalRect.Value().xMin, diagonalRect.Value().yMin,
        diagonalRect.Value().xMax, diagonalRect.Value().yMax,
        squareRect.xMin, squareRect.yMin, squareRect.xMax, squareRect.yMax,
        resizedRect->xMin, resizedRect->yMin, resizedRect->xMax, resizedRect->yMax,
        scaledRect.Value().xMin, scaledRect.Value().yMin,
        scaledRect.Value().xMax, scaledRect.Value().yMax,
        utilityRect.Value().WidthRustF32(), utilityRect.Value().HeightRustF32(),
        utilityRect.Value().AreaRustF32(), utilityRect.Value().DiameterRustF32()});

    const auto overlapRect = jaguar::Rect2d::Create(3.0, 1.0, 5.0, 4.0);
    assert(overlapRect.HasValue());
    const auto intersection = jaguar::Rect2d::IntersectionRustF32(
        utilityRect.Value(), overlapRect.Value());
    assert(intersection.has_value());
    const auto bounding = jaguar::Rect2d::BoundingRectRustF32(
        utilityRect.Value(), overlapRect.Value());
    const auto corners = utilityRect.Value().CornersRustF32();
    expectRustNumbers(reference, "rect.combination", {
        intersection->xMin, intersection->yMin, intersection->xMax, intersection->yMax,
        bounding.xMin, bounding.yMin, bounding.xMax, bounding.yMax,
        corners[0].x, corners[0].y, corners[1].x, corners[1].y,
        corners[2].x, corners[2].y, corners[3].x, corners[3].y});
    const auto edges = utilityRect.Value().EdgesRustF32();
    std::vector<double> edgeCoordinates;
    for (const auto& rectEdge : edges)
    {
        edgeCoordinates.insert(edgeCoordinates.end(), {
            rectEdge.start.x, rectEdge.start.y, rectEdge.end.x, rectEdge.end.y});
    }
    expectRustNumbers(reference, "rect.edges", edgeCoordinates);

    const jaguar::Point2d insideRectPoint{1.0, 1.0};
    const jaguar::Point2d outsideRectPoint{-3.0, 4.0};
    const auto insideSeparation = utilityRect.Value().SeparationDistanceRustF32(insideRectPoint);
    const auto outsideSeparation = utilityRect.Value().SeparationDistanceRustF32(outsideRectPoint);
    const auto insideSquared = utilityRect.Value().SquaredSeparationDistanceRustF32(insideRectPoint);
    const auto outsideSquared = utilityRect.Value().SquaredSeparationDistanceRustF32(outsideRectPoint);
    expectRustNumbers(reference, "rect.distance", {
        utilityRect.Value().DistanceToRustF32(insideRectPoint),
        utilityRect.Value().DistanceToRustF32(outsideRectPoint),
        utilityRect.Value().SquaredDistanceToRustF32(outsideRectPoint),
        insideSeparation.second, outsideSeparation.second,
        insideSquared.second, outsideSquared.second});
    expectRustPositions(reference, "rect.distance", {
        insideSeparation.first, outsideSeparation.first,
        insideSquared.first, outsideSquared.first});
    expectRustBooleans(reference, "rect.distance", {
        !utilityRect.Value().ResizeByRustF32(-2.0, 0.0).has_value()});

    const jaguar::Rect2d hazardBounds{0.0, 0.0, 2.0, 2.0};
    const float epsilon = std::numeric_limits<float>::epsilon();
    const jaguar::Rect2d nearZeroBounds{-epsilon * 0.25, 0.5, 1.0, 1.5};
    const jaguar::Rect2d enclosedBounds{-1.0, -1.0, 3.0, 3.0};
    const jaguar::Rect2d intersectingBounds{1.5, 1.5, 3.0, 3.0};
    const jaguar::Rect2d disjointBounds{3.0, 0.0, 4.0, 1.0};
    expectRustBooleans(reference, "rect.almost_relation_cases", {
        hazardBounds.AlmostRelationToRustF32(nearZeroBounds) == jaguar::RectRelation::Surrounding,
        hazardBounds.AlmostRelationToRustF32(enclosedBounds) == jaguar::RectRelation::Enclosed,
        hazardBounds.AlmostRelationToRustF32(intersectingBounds) == jaguar::RectRelation::Intersecting,
        hazardBounds.AlmostRelationToRustF32(disjointBounds) == jaguar::RectRelation::Disjoint});
    const auto quadrants = utilityRect.Value().QuadrantsRustF32();
    const auto utilityCentroid = utilityRect.Value().CentroidRustF32();
    std::vector<double> rectBasics = {utilityCentroid.x, utilityCentroid.y};
    for (const auto& quadrant : quadrants)
    {
        rectBasics.insert(rectBasics.end(), {
            quadrant.xMin, quadrant.yMin, quadrant.xMax, quadrant.yMax});
    }
    rectBasics.insert(rectBasics.end(), {
        utilityRect.Value().WidthRustF32(), utilityRect.Value().HeightRustF32()});
    expectRustNumbers(reference, "geometry.rect_basics", rectBasics);
    const auto enclosedRect = jaguar::Rect2d::Create(1.0, 0.5, 3.0, 1.5);
    const auto overlapForRelation = jaguar::Rect2d::Create(3.0, 1.0, 5.0, 4.0);
    const auto disjointForRelation = jaguar::Rect2d::Create(5.0, 0.0, 6.0, 1.0);
    assert(enclosedRect.HasValue() && overlapForRelation.HasValue() &&
           disjointForRelation.HasValue());
    expectRustBooleans(reference, "geometry.rect_relations", {
        utilityRect.Value().RelationToRustF32(enclosedRect.Value()) ==
            jaguar::RectRelation::Surrounding,
        enclosedRect.Value().RelationToRustF32(utilityRect.Value()) ==
            jaguar::RectRelation::Enclosed,
        utilityRect.Value().RelationToRustF32(overlapForRelation.Value()) ==
            jaguar::RectRelation::Intersecting,
        utilityRect.Value().RelationToRustF32(disjointForRelation.Value()) ==
            jaguar::RectRelation::Disjoint});
    const auto throughRectEdge = jaguar::Edge2d::Create({-1.0, 1.0}, {5.0, 1.0});
    const auto outsideRectEdge = jaguar::Edge2d::Create({-1.0, 3.0}, {5.0, 3.0});
    assert(throughRectEdge.HasValue() && outsideRectEdge.HasValue());
    expectRustBooleans(reference, "geometry.rect_edge_intersects", {
        throughRectEdge.Value().IntersectsRustF32(utilityRect.Value()),
        outsideRectEdge.Value().IntersectsRustF32(utilityRect.Value())});
    expectRustBooleans(reference, "geometry.rect_construction", {
        jaguar::Rect2d::Create(0.0, 0.0, 1.0, 1.0).HasValue(),
        jaguar::Rect2d::Create(1.0, 0.0, 1.0, 1.0).HasValue(),
        jaguar::Rect2d::Create(0.0, 1.0, 1.0, 1.0).HasValue(),
        jaguar::Rect2d::Create(0.0, 0.0,
            std::numeric_limits<double>::quiet_NaN(), 1.0).HasValue(),
        jaguar::Rect2d::Create(0.0, 0.0,
            std::numeric_limits<double>::infinity(), 1.0).HasValue()});

    const jaguar::Point2d pointA{1.25, -2.5};
    const jaguar::Point2d pointB{4.5, 2.5};
    expectRustNumbers(reference, "point.distance", {
        pointA.DistanceToRustF32(pointB),
        pointA.SquaredDistanceToRustF32(pointB),
        jaguar::Point2d{16777216.0, 0.0}.DistanceToRustF32({16777217.0, 0.0})});

    const auto distancePolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 2.0}, {0.0, 2.0}});
    assert(distancePolygon.HasValue());
    const auto polygonInsideSeparation =
        distancePolygon.Value().SeparationDistanceRustF32(insideRectPoint);
    const auto polygonOutsideSeparation =
        distancePolygon.Value().SeparationDistanceRustF32(outsideRectPoint);
    const auto polygonInsideSquared =
        distancePolygon.Value().SquaredSeparationDistanceRustF32(insideRectPoint);
    const auto polygonOutsideSquared =
        distancePolygon.Value().SquaredSeparationDistanceRustF32(outsideRectPoint);
    expectRustNumbers(reference, "polygon.distance", {
        distancePolygon.Value().DistanceToRustF32(insideRectPoint),
        distancePolygon.Value().SquaredDistanceToRustF32(insideRectPoint),
        distancePolygon.Value().DistanceToRustF32(outsideRectPoint),
        distancePolygon.Value().SquaredDistanceToRustF32(outsideRectPoint),
        polygonInsideSeparation.second, polygonOutsideSeparation.second,
        polygonInsideSquared.second, polygonOutsideSquared.second});
    expectRustPositions(reference, "polygon.distance", {
        polygonInsideSeparation.first, polygonOutsideSeparation.first,
        polygonInsideSquared.first, polygonOutsideSquared.first});

    const auto referencePolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 2.0}, {0.0, 2.0}});
    assert(referencePolygon.HasValue());
    const auto& referencePoi = referencePolygon.Value().PointOfInaccessibility();
    std::vector<double> polygonBasics = {
        static_cast<double>(referencePolygon.Value().VertexCount()),
        referencePolygon.Value().Area(), referencePolygon.Value().Diameter(),
        referencePolygon.Value().BoundingBox().xMin,
        referencePolygon.Value().BoundingBox().yMin,
        referencePolygon.Value().BoundingBox().xMax,
        referencePolygon.Value().BoundingBox().yMax,
        referencePolygon.Value().Centroid().x,
        referencePolygon.Value().Centroid().y,
        referencePoi.center.x, referencePoi.center.y, referencePoi.radius};
    for (const auto& polygonEdge : referencePolygon.Value().Edges())
    {
        polygonBasics.insert(polygonBasics.end(), {
            polygonEdge.start.x, polygonEdge.start.y,
            polygonEdge.end.x, polygonEdge.end.y});
    }
    expectRustNumbers(reference, "geometry.simple_polygon_basics", polygonBasics);
    expectRustBooleans(reference, "geometry.simple_polygon_contains", {
        referencePolygon.Value().ContainsRustF32({1.0, 1.0}),
        referencePolygon.Value().ContainsRustF32({0.0, 1.0}),
        referencePolygon.Value().ContainsRustF32({-1.0, 1.0})});

    const auto clockwisePolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 2.0}, {4.0, 2.0}, {4.0, 0.0}});
    assert(clockwisePolygon.HasValue());
    std::vector<double> clockwiseVertices;
    for (const auto& point : clockwisePolygon.Value().Vertices())
        clockwiseVertices.insert(clockwiseVertices.end(), {point.x, point.y});
    expectRustNumbers(reference, "geometry.simple_polygon_clockwise_vertices",
                      clockwiseVertices);

    const auto validPolygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {4.0, 0.0}, {4.0, 2.0}, {0.0, 2.0}});
    const auto tooFewPolygon = jaguar::SimplePolygon::Create({{0.0, 0.0}, {1.0, 0.0}});
    const auto duplicatePolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {2.0, 2.0}, {0.0, 2.0}});
    const auto collinearPolygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}});
    const auto bowtiePolygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}, {2.0, 0.0}});
    const auto tinyPolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 1.0e-5}, {1.0e-5, 1.0e-5}, {1.0e-5, 0.0}});
    expectRustBooleans(reference, "geometry.simple_polygon_construction", {
        validPolygon.HasValue(), tooFewPolygon.HasValue(), duplicatePolygon.HasValue(),
        collinearPolygon.HasValue(), bowtiePolygon.HasValue(), tinyPolygon.HasValue()});

    const auto polygonTransform = jaguar::Transform2d::FromRotationTranslation(
        0.37, {10.0, -2.0});
    const auto transformedPolygon = referencePolygon.Value().TransformedRustF32(
        polygonTransform);
    assert(transformedPolygon.HasValue());
    std::vector<double> transformedPolygonValues;
    for (const auto& point : transformedPolygon.Value().Vertices())
        transformedPolygonValues.insert(transformedPolygonValues.end(), {point.x, point.y});
    const auto& transformedBox = transformedPolygon.Value().BoundingBox();
    const auto transformedCentroid = transformedPolygon.Value().Centroid();
    transformedPolygonValues.insert(transformedPolygonValues.end(), {
        transformedBox.xMin, transformedBox.yMin, transformedBox.xMax, transformedBox.yMax,
        transformedCentroid.x, transformedCentroid.y,
        transformedPolygon.Value().Area(), transformedPolygon.Value().Diameter()});
    expectRustNumbers(reference, "geometry.simple_polygon_transform",
                      transformedPolygonValues);

    jaguar::OriginalShape originalShape{
        referencePolygon.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.5, {10.0, 20.0}),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    const auto internalShape = originalShape.ConvertToInternal();
    assert(internalShape.HasValue());
    const auto& rawBounds = originalShape.shape.BoundingBox();
    const auto rawCentroid = originalShape.shape.Centroid();
    const auto& internalBounds = internalShape.Value().BoundingBox();
    const auto internalCentroid = internalShape.Value().Centroid();
    expectRustNumbers(reference, "geometry.original_shape", {
        originalShape.shape.Area(), originalShape.shape.Diameter(),
        rawBounds.xMin, rawBounds.yMin, rawBounds.xMax, rawBounds.yMax,
        rawCentroid.x, rawCentroid.y,
        static_cast<double>(internalShape.Value().VertexCount()),
        internalShape.Value().Vertices()[0].x, internalShape.Value().Vertices()[0].y,
        internalBounds.xMin, internalBounds.yMin, internalBounds.xMax, internalBounds.yMax,
        internalCentroid.x, internalCentroid.y,
        internalShape.Value().Area(), internalShape.Value().Diameter()});

    const jaguar::Transform2d transformA = jaguar::Transform2d::FromRotationTranslation(
        100000.123456789, {1.23456789, -7.65432109});
    const jaguar::Transform2d transformB = jaguar::Transform2d::FromRotationTranslation(
        -99999.87654321, {0.333333333, 0.777777777});
    const jaguar::Transform2d followed = transformA.FollowedByRustF32(transformB);
    const jaguar::Transform2d inverse = transformA.InverseRustF32();
    expectRustNumbers(reference, "geometry.transform_followed_by", {
        followed.rotationRad, followed.translation.x, followed.translation.y});
    expectRustNumbers(reference, "geometry.transform_inverse", {
        inverse.rotationRad, inverse.translation.x, inverse.translation.y});

    const auto tiedXHull = jaguar::ConvexHull({
        {0.0, 1.0}, {1.0, 0.0}, {0.0, 0.0}, {1.0, 1.0}});
    std::vector<double> hullCoordinates;
    for (const auto& point : tiedXHull)
        hullCoordinates.insert(hullCoordinates.end(), {point.x, point.y});
    expectRustNumbers(reference, "geometry.convex_hull_tied_x", hullCoordinates);
    expectRustNumbers(reference, "geometry.convex_hull_singleton_count", {
        static_cast<double>(jaguar::ConvexHull({{1.0, 2.0}}).size())});
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
    runB1PrimitiveReferenceTests();
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
