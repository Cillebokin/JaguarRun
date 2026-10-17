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
#include "JaguarCore/FailFast/Surrogate.h"

#include <algorithm>
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
#include <utility>
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

ReferenceMap loadReferenceMap(const char* fileName)
{
    const std::string contents = loadReference(fileName);
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
                     "Rust reference %s field count: Rust %zu, C++ %zu\n",
                     key,
                     expected.size(),
                     actual.size());
    }
    assert(expected.size() == actual.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const float actualValue = static_cast<float>(actual[index]);
        const float expectedValue = static_cast<float>(expected[index]);
        const bool matches =
            jaguar::detail::AlmostEqualRustF32(actualValue, expectedValue);
        if (!matches)
        {
            std::fprintf(stderr,
                         "Rust reference %s[%zu]: Rust %.9g, C++ %.9g\n",
                         key,
                         index,
                         expectedValue,
                         actualValue);
        }
        assert(matches);
    }
}

std::vector<double> shapeModificationValues(const jaguar::SimplePolygon& shape)
{
    std::vector<double> values{
        static_cast<double>(shape.VertexCount()), shape.Area()};
    for (const jaguar::Point2d& point : shape.Vertices())
    {
        values.push_back(point.x);
        values.push_back(point.y);
    }
    return values;
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

    const jaguar::Transform2d transformBase =
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.25, -2.5});
    const jaguar::Transform2d transformRotation =
        jaguar::Transform2d::FromRotationTranslation(0.375, {0.0, 0.0});
    const jaguar::Transform2d transformTranslation =
        jaguar::Transform2d::FromRotationTranslation(0.0, {3.0, -4.0});
    const auto rotatePath = transformBase.FollowedByRustF32(
        jaguar::Transform2d::FromRotationTranslation(0.2, {0.0, 0.0}));
    const auto translatePath = transformBase.FollowedByRustF32(
        jaguar::Transform2d::FromRotationTranslation(0.0, {3.0, -1.0}));
    const auto rotateTranslatePath = transformBase.FollowedByRustF32(
        jaguar::Transform2d::FromRotationTranslation(0.5, {2.0, -1.0}));
    const auto translateRotatePath = transformBase.FollowedByRustF32(
        jaguar::Transform2d::FromRotationTranslation(0.0, {2.0, -1.0})
            .FollowedByRustF32(
                jaguar::Transform2d::FromRotationTranslation(0.5, {0.0, 0.0})));
    const auto transformOther =
        jaguar::Transform2d::FromRotationTranslation(-0.3, {0.25, 1.5});
    const auto transformedPath = transformBase.FollowedByRustF32(transformOther);
    const jaguar::Transform2d decomposedOperation =
        jaguar::Transform2d::FromRotationTranslation(-0.4, {0.5, 2.25});
    const auto decomposedPath = transformBase.FollowedByRustF32(decomposedOperation);
    const std::vector<jaguar::Transform2d> transformationPaths = {
        jaguar::Transform2d::Identity(),
        transformTranslation,
        transformRotation,
        rotatePath,
        translatePath,
        rotateTranslatePath,
        translateRotatePath,
        transformedPath,
        decomposedPath,
        transformBase.InverseRustF32()};
    std::vector<double> transformationMatrixProbes;
    std::vector<double> transformationDecompositions;
    for (const auto& path : transformationPaths)
    {
        for (const jaguar::Point2d& probe : {
                 jaguar::Point2d{0.0, 0.0},
                 jaguar::Point2d{1.0, 0.0},
                 jaguar::Point2d{0.0, 1.0}})
        {
            const auto point = path.ApplyRustF32(probe);
            transformationMatrixProbes.insert(
                transformationMatrixProbes.end(), {point.x, point.y});
        }
        transformationDecompositions.insert(
            transformationDecompositions.end(),
            {path.rotationRad, path.translation.x, path.translation.y});
    }
    expectRustNumbers(reference, "geometry.transformation_matrix_probes",
                      transformationMatrixProbes);
    expectRustNumbers(reference, "geometry.transformation_decompositions",
                      transformationDecompositions);
    const std::vector<jaguar::Transform2d> emptyTransformCases = {
        jaguar::Transform2d::Identity(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0}),
        jaguar::Transform2d::FromRotationTranslation(0.375, {0.0, 0.0}),
        jaguar::Transform2d::FromRotationTranslation(2.0 * std::acos(-1.0),
                                                     {0.0, 0.0}),
        jaguar::Transform2d::FromRotationTranslation(0.0, {-0.0, 0.0})};
    std::vector<bool> mapsToEmptyTransform;
    for (const auto& transformCase : emptyTransformCases)
    {
        mapsToEmptyTransform.push_back(
            transformCase.rotationRad == 0.0 &&
            transformCase.translation.x == 0.0 &&
            transformCase.translation.y == 0.0);
    }
    expectRustBooleans(reference, "geometry.transformation_is_empty",
                       mapsToEmptyTransform);

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

    const jaguar::Circle2d boundingSingle{{3.0, -4.0}, 0.0};
    const auto singleBoundingCircle =
        jaguar::Circle2d::BoundingCircleRustF32({boundingSingle});
    const auto outerFirst = jaguar::Circle2d::BoundingCircleRustF32({
        jaguar::Circle2d{{0.0, 0.0}, 10.0},
        jaguar::Circle2d{{1.0, 0.0}, 2.0}});
    const auto innerFirst = jaguar::Circle2d::BoundingCircleRustF32({
        jaguar::Circle2d{{0.0, 0.0}, 1.0},
        jaguar::Circle2d{{1.0, 0.0}, 10.0}});
    const auto concentricNested = jaguar::Circle2d::BoundingCircleRustF32({
        jaguar::Circle2d{{0.0, 0.0}, 1.0},
        jaguar::Circle2d{{0.0, 0.0}, 2.0}});
    assert(singleBoundingCircle.HasValue() && outerFirst.HasValue() &&
           innerFirst.HasValue() && concentricNested.HasValue());
    expectRustBooleans(reference, "geometry.circle_bounding_circle_empty_contract", {
        !jaguar::Circle2d::BoundingCircleRustF32({}).HasValue()});
    expectRustNumbers(reference, "geometry.circle_bounding_circle_finite_values", {
        singleBoundingCircle.Value().center.x,
        singleBoundingCircle.Value().center.y,
        singleBoundingCircle.Value().radius,
        outerFirst.Value().center.x,
        outerFirst.Value().center.y,
        outerFirst.Value().radius,
        innerFirst.Value().center.x,
        innerFirst.Value().center.y,
        innerFirst.Value().radius});
    expectRustBooleans(reference, "geometry.circle_bounding_circle_finite_contract", {
        singleBoundingCircle.Value().center.x == boundingSingle.center.x &&
            singleBoundingCircle.Value().center.y == boundingSingle.center.y &&
            singleBoundingCircle.Value().radius == boundingSingle.radius,
        outerFirst.Value().center.x == 0.0 && outerFirst.Value().center.y == 0.0 &&
            outerFirst.Value().radius == 10.0,
        innerFirst.Value().center.x == 5.0 && innerFirst.Value().center.y == 0.0 &&
            innerFirst.Value().radius == 6.0,
        std::hypot(innerFirst.Value().center.x - 1.0,
                   innerFirst.Value().center.y) + 10.0 > innerFirst.Value().radius,
        std::isnan(concentricNested.Value().center.x) &&
            std::isnan(concentricNested.Value().center.y) &&
            std::isnan(concentricNested.Value().radius)});

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

    const jaguar::Edge2d zeroLengthEdge{{1.0, 2.0}, {1.0, 2.0}};
    const auto zeroLengthClosest = zeroLengthEdge.ClosestPointRustF32({2.0, 3.0});
    const auto zeroLengthCenter = zeroLengthEdge.CentroidRustF32();
    const auto zeroLengthBounds = zeroLengthEdge.BoundingBox();
    expectRustNumbers(reference, "geometry.edge_zero_length_operations", {
        zeroLengthClosest.x, zeroLengthClosest.y,
        zeroLengthEdge.LengthRustF32(), zeroLengthCenter.x, zeroLengthCenter.y,
        zeroLengthBounds.xMin, zeroLengthBounds.yMin,
        zeroLengthBounds.xMax, zeroLengthBounds.yMax,
        zeroLengthEdge.SquaredDistanceToRustF32({2.0, 3.0})});
    const auto zeroLengthFront = zeroLengthEdge.ExtendedAtFrontRustF32(2.0);
    const auto zeroLengthBack = zeroLengthEdge.ExtendedAtBackRustF32(2.0);
    const auto zeroLengthScaled = zeroLengthEdge.ScaledRustF32(3.0);
    const auto zeroLengthReversed = zeroLengthEdge.ReversedRustF32();
    const jaguar::Edge2d zeroLengthCrossing{{0.0, 2.0}, {2.0, 2.0}};
    expectRustNumbers(reference, "geometry.edge_zero_length_scale_reverse", {
        zeroLengthScaled.start.x, zeroLengthScaled.start.y,
        zeroLengthScaled.end.x, zeroLengthScaled.end.y,
        zeroLengthReversed.start.x, zeroLengthReversed.start.y,
        zeroLengthReversed.end.x, zeroLengthReversed.end.y});
    expectRustBooleans(reference, "geometry.edge_zero_length_finite_contract", {
        std::isnan(zeroLengthFront.start.x), std::isnan(zeroLengthFront.start.y),
        zeroLengthFront.end.x == zeroLengthEdge.end.x &&
            zeroLengthFront.end.y == zeroLengthEdge.end.y,
        zeroLengthBack.start.x == zeroLengthEdge.start.x &&
            zeroLengthBack.start.y == zeroLengthEdge.start.y,
        std::isnan(zeroLengthBack.end.x), std::isnan(zeroLengthBack.end.y),
        zeroLengthScaled.start.x == 1.0 && zeroLengthScaled.start.y == 2.0 &&
            zeroLengthScaled.end.x == 1.0 && zeroLengthScaled.end.y == 2.0,
        zeroLengthReversed.start.x == zeroLengthEdge.end.x &&
            zeroLengthReversed.start.y == zeroLengthEdge.end.y &&
            zeroLengthReversed.end.x == zeroLengthEdge.start.x &&
            zeroLengthReversed.end.y == zeroLengthEdge.start.y,
        zeroLengthEdge.IntersectsRustF32(zeroLengthCrossing),
        zeroLengthEdge.IntersectionPointRustF32(zeroLengthCrossing).has_value()});

    const float smallestNormal = std::numeric_limits<float>::min();
    const auto underflowLengthEdge = jaguar::Edge2d::Create(
        {0.0, 0.0}, {static_cast<double>(smallestNormal), 0.0});
    assert(underflowLengthEdge.HasValue());
    const auto underflowFront = underflowLengthEdge.Value().ExtendedAtFrontRustF32(1.0);
    const auto underflowBack = underflowLengthEdge.Value().ExtendedAtBackRustF32(1.0);
    expectRustBooleans(reference, "geometry.edge_underflow_length_contract", {
        underflowLengthEdge.HasValue(),
        underflowLengthEdge.Value().LengthRustF32() == 0.0,
        std::isinf(underflowFront.start.x) && std::signbit(underflowFront.start.x),
        std::isnan(underflowFront.start.y),
        std::isinf(underflowBack.end.x) && !std::signbit(underflowBack.end.x),
        std::isnan(underflowBack.end.y)});

    const float quietNan = std::numeric_limits<float>::quiet_NaN();
    const auto nanEdge = jaguar::Edge2d::Create({quietNan, 0.0}, {1.0, 1.0});
    const auto infiniteEdge = jaguar::Edge2d::Create(
        {std::numeric_limits<float>::infinity(), 0.0}, {1.0, 1.0});
    assert(nanEdge.HasValue() && infiniteEdge.HasValue());
    const auto nanEdgeBounds = nanEdge.Value().BoundingBox();
    expectRustBooleans(reference, "geometry.edge_nonfinite_bbox_contract", {
        nanEdge.HasValue(), infiniteEdge.HasValue(),
        !std::isnan(nanEdgeBounds.xMin), !std::isnan(nanEdgeBounds.yMin),
        !std::isnan(nanEdgeBounds.xMax), !std::isnan(nanEdgeBounds.yMax),
        nanEdgeBounds.xMin == nanEdgeBounds.xMax,
        std::isinf(infiniteEdge.Value().BoundingBox().xMax)});
    expectRustNumbers(reference, "geometry.edge_nan_bbox_values", {
        nanEdgeBounds.xMin, nanEdgeBounds.yMin,
        nanEdgeBounds.xMax, nanEdgeBounds.yMax});

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
    const std::vector<std::pair<jaguar::Edge2d, jaguar::Edge2d>> edgeContactCases = {
        {jaguar::Edge2d{{0.0, 0.0}, {4.0, 0.0}},
         jaguar::Edge2d{{2.0, -1.0}, {2.0, 1.0}}},
        {jaguar::Edge2d{{0.0, 0.0}, {2.0, 0.0}},
         jaguar::Edge2d{{2.0, 0.0}, {2.0, 1.0}}},
        {jaguar::Edge2d{{0.0, 0.0}, {4.0, 0.0}},
         jaguar::Edge2d{{2.0, 0.0}, {2.0, 2.0}}},
        {jaguar::Edge2d{{0.0, 0.0}, {2.0, 0.0}},
         jaguar::Edge2d{{1.0, 0.0}, {3.0, 0.0}}},
        {jaguar::Edge2d{{0.0, 0.0}, {2.0, 0.0}},
         jaguar::Edge2d{{2.0, 0.0}, {4.0, 0.0}}},
        {jaguar::Edge2d{{0.0, 0.0}, {4.0, 0.0}},
         jaguar::Edge2d{{0.0, 1.0}, {4.0, 1.0}}},
        {jaguar::Edge2d{{0.0, 0.0}, {1.0, 1.0}},
         jaguar::Edge2d{{2.0, 0.0}, {2.0, 2.0}}}};
    std::vector<bool> edgeContactResults;
    std::vector<double> edgeContactPoints;
    for (const auto& edgeCase : edgeContactCases)
    {
        const auto point = edgeCase.first.IntersectionPointRustF32(edgeCase.second);
        edgeContactResults.push_back(point.has_value());
        if (point)
            edgeContactPoints.insert(edgeContactPoints.end(), {point->x, point->y});
    }
    expectRustBooleans(reference, "geometry.edge_collides_at", edgeContactResults);
    expectRustNumbers(reference, "geometry.edge_collides_at_points", edgeContactPoints);
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
    const jaguar::Rect2d thresholdRect{1.0, 1.0, 3.0, 3.0};
    float ulpBelowOne = 1.0F;
    std::vector<float> ulpBoundaries;
    for (unsigned int ulp = 1; ulp <= 16; ++ulp)
    {
        ulpBelowOne = std::nextafter(ulpBelowOne, 0.0F);
        if (ulp == 1 || ulp == 2 || ulp == 3 || ulp == 4 || ulp == 8 || ulp == 16)
            ulpBoundaries.push_back(ulpBelowOne);
    }
    std::vector<bool> rectangleRelationUlpCases;
    for (const float boundaryXMin : ulpBoundaries)
    {
        const jaguar::Rect2d nearBoundaryRect{
            static_cast<double>(boundaryXMin), 1.5, 3.0, 2.5};
        const auto strictRelation = thresholdRect.RelationToRustF32(nearBoundaryRect);
        const auto almostRelation =
            thresholdRect.AlmostRelationToRustF32(nearBoundaryRect);
        rectangleRelationUlpCases.push_back(strictRelation == jaguar::RectRelation::Intersecting);
        rectangleRelationUlpCases.push_back(almostRelation == jaguar::RectRelation::Surrounding);
        rectangleRelationUlpCases.push_back(almostRelation == jaguar::RectRelation::Intersecting);
    }
    expectRustBooleans(reference, "geometry.rect_relation_ulp_boundary",
                       rectangleRelationUlpCases);
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

    const auto boundaryRect = jaguar::Rect2d::Create(0.0, 0.0, 2.0, 1.0);
    const auto edgeTouchRect = jaguar::Rect2d::Create(2.0, 0.0, 3.0, 1.0);
    const auto cornerTouchRect = jaguar::Rect2d::Create(2.0, 1.0, 3.0, 2.0);
    const auto overlapBoundaryRect = jaguar::Rect2d::Create(1.0, 0.5, 3.0, 2.0);
    assert(boundaryRect.HasValue() && edgeTouchRect.HasValue() &&
           cornerTouchRect.HasValue() && overlapBoundaryRect.HasValue());
    expectRustBooleans(reference, "geometry.rect_boundary_intersections", {
        jaguar::Rect2d::IntersectionRustF32(
            boundaryRect.Value(), overlapBoundaryRect.Value()).has_value(),
        jaguar::Rect2d::IntersectionRustF32(
            boundaryRect.Value(), edgeTouchRect.Value()).has_value(),
        jaguar::Rect2d::IntersectionRustF32(
            boundaryRect.Value(), cornerTouchRect.Value()).has_value(),
        jaguar::Rect2d::IntersectionRustF32(
            boundaryRect.Value(), disjointBounds).has_value()});
    const auto collapseWidth = boundaryRect.Value().ResizeByRustF32(-1.0, 0.0);
    const auto collapseHeight = boundaryRect.Value().ResizeByRustF32(0.0, -0.5);
    const auto collapseBoth = boundaryRect.Value().ResizeByRustF32(-1.0, -0.5);
    const auto noResize = boundaryRect.Value().ResizeByRustF32(0.0, 0.0);
    expectRustBooleans(reference, "geometry.rect_boundary_resizes", {
        !collapseWidth.has_value(), !collapseHeight.has_value(),
        !collapseBoth.has_value(), noResize.has_value() &&
            noResize->xMin == boundaryRect.Value().xMin &&
            noResize->yMin == boundaryRect.Value().yMin &&
            noResize->xMax == boundaryRect.Value().xMax &&
            noResize->yMax == boundaryRect.Value().yMax});
    const auto zeroScale = boundaryRect.Value().ScaledRustF32(0.0);
    expectRustBooleans(reference, "geometry.rect_zero_scale_rejected", {
        !zeroScale.HasValue()});

    const jaguar::Rect2d degenerateZeroWidthRect{1.0, 0.0, 1.0, 2.0};
    const jaguar::Rect2d degenerateZeroHeightRect{0.0, 1.0, 2.0, 1.0};
    const auto appendDegenerateRectMetrics = [](const jaguar::Rect2d& value,
                                                std::vector<double>& output) {
        const auto center = value.CentroidRustF32();
        output.insert(output.end(), {
            value.WidthRustF32(), value.HeightRustF32(), value.AreaRustF32(),
            value.DiameterRustF32(), center.x, center.y});
    };
    std::vector<double> degenerateRectMetrics;
    appendDegenerateRectMetrics(degenerateZeroWidthRect, degenerateRectMetrics);
    appendDegenerateRectMetrics(degenerateZeroHeightRect, degenerateRectMetrics);
    expectRustNumbers(reference, "geometry.rect_degenerate_metrics",
                      degenerateRectMetrics);

    std::vector<double> degenerateRectShapes;
    for (const jaguar::Rect2d& degenerateRect : {
             degenerateZeroWidthRect, degenerateZeroHeightRect})
    {
        const auto degenerateCorners = degenerateRect.CornersRustF32();
        for (const auto& corner : degenerateCorners)
            degenerateRectShapes.insert(
                degenerateRectShapes.end(), {corner.x, corner.y});
        for (const auto& degenerateEdge : degenerateRect.EdgesRustF32())
            degenerateRectShapes.insert(degenerateRectShapes.end(), {
                degenerateEdge.start.x, degenerateEdge.start.y,
                degenerateEdge.end.x, degenerateEdge.end.y});
    }
    expectRustNumbers(reference, "geometry.rect_degenerate_boundary_shapes",
                      degenerateRectShapes);
    std::vector<double> degenerateRectSides;
    for (const jaguar::Rect2d& degenerateRect : {
             degenerateZeroWidthRect, degenerateZeroHeightRect})
    {
        for (const auto& side : degenerateRect.SidesRustF32())
            degenerateRectSides.insert(degenerateRectSides.end(), {
                side.start.x, side.start.y, side.end.x, side.end.y});
    }
    expectRustNumbers(reference, "geometry.rect_degenerate_sides",
                      degenerateRectSides);

    const jaguar::Rect2d areaRect{0.0, 0.0, 2.0, 2.0};
    expectRustBooleans(reference, "geometry.rect_degenerate_intersections", {
        jaguar::Rect2d::IntersectionRustF32(
            degenerateZeroWidthRect, areaRect).has_value(),
        jaguar::Rect2d::IntersectionRustF32(
            degenerateZeroWidthRect, degenerateZeroWidthRect).has_value(),
        jaguar::Rect2d::IntersectionRustF32(
            degenerateZeroHeightRect, areaRect).has_value(),
        jaguar::Rect2d::IntersectionRustF32(
            degenerateZeroHeightRect, degenerateZeroHeightRect).has_value()});
    const jaguar::Rect2d lowerLeftAreaRect{-2.0, -1.0, 0.0, 1.0};
    const jaguar::Rect2d upperRightAreaRect{3.0, 2.0, 4.0, 3.0};
    std::vector<double> degenerateBoundingRects;
    for (const jaguar::Rect2d& bounds : {
             jaguar::Rect2d::BoundingRectRustF32(
                 degenerateZeroWidthRect, lowerLeftAreaRect),
             jaguar::Rect2d::BoundingRectRustF32(
                 degenerateZeroHeightRect, upperRightAreaRect),
             jaguar::Rect2d::BoundingRectRustF32(
                 degenerateZeroWidthRect, degenerateZeroHeightRect)})
    {
        degenerateBoundingRects.insert(degenerateBoundingRects.end(), {
            bounds.xMin, bounds.yMin, bounds.xMax, bounds.yMax});
    }
    expectRustNumbers(reference, "geometry.rect_degenerate_bounding_rects",
                      degenerateBoundingRects);
    const auto recoverWidth = degenerateZeroWidthRect.ResizeByRustF32(1.0, 0.0);
    const auto recoverHeight = degenerateZeroHeightRect.ResizeByRustF32(0.0, 1.0);
    assert(recoverWidth.has_value() && recoverHeight.has_value());
    expectRustNumbers(reference, "geometry.rect_degenerate_resize_recovery", {
        recoverWidth->xMin, recoverWidth->yMin, recoverWidth->xMax, recoverWidth->yMax,
        recoverHeight->xMin, recoverHeight->yMin,
        recoverHeight->xMax, recoverHeight->yMax});
    const auto negativeScale = utilityRect.Value().ScaledRustF32(-1.0);
    const auto unchangedWidthScale = degenerateZeroWidthRect.ScaledRustF32(1.0);
    const auto expandedWidthScale = degenerateZeroWidthRect.ScaledRustF32(2.0);
    const auto unchangedHeightScale = degenerateZeroHeightRect.ScaledRustF32(1.0);
    const auto expandedHeightScale = degenerateZeroHeightRect.ScaledRustF32(2.0);
    expectRustBooleans(reference, "geometry.rect_degenerate_scale_contract", {
        !negativeScale.HasValue(), !unchangedWidthScale.HasValue(),
        !expandedWidthScale.HasValue(), !unchangedHeightScale.HasValue(),
        !expandedHeightScale.HasValue()});
    const auto hasInvalidInputError = [](const jaguar::Result<jaguar::Rect2d>& result) {
        return !result.HasValue() &&
            result.GetError().code == jaguar::ErrorCode::InvalidInput;
    };
    assert(hasInvalidInputError(negativeScale));
    assert(hasInvalidInputError(unchangedWidthScale));
    assert(hasInvalidInputError(expandedWidthScale));
    assert(hasInvalidInputError(unchangedHeightScale));
    assert(hasInvalidInputError(expandedHeightScale));

    const float nextAfterOne = std::nextafter(1.0F, 2.0F);
    const auto oneUlpRect = jaguar::Rect2d::Create(
        1.0, 0.0, static_cast<double>(nextAfterOne), 1.0);
    const auto oneUlpHeightRect = jaguar::Rect2d::Create(
        0.0, 1.0, 1.0, static_cast<double>(nextAfterOne));
    assert(oneUlpRect.HasValue() && oneUlpHeightRect.HasValue());
    const auto oneUlpQuadrants = oneUlpRect.Value().QuadrantsRustF32();
    const auto oneUlpHeightQuadrants =
        oneUlpHeightRect.Value().QuadrantsRustF32();
    const bool oneUlpHasCollapsedQuadrant = std::any_of(
        oneUlpQuadrants.begin(), oneUlpQuadrants.end(), [](const jaguar::Rect2d& quadrant) {
            return quadrant.WidthRustF32() == 0.0 || quadrant.HeightRustF32() == 0.0;
        });
    const bool oneUlpHeightHasCollapsedQuadrant = std::any_of(
        oneUlpHeightQuadrants.begin(), oneUlpHeightQuadrants.end(),
        [](const jaguar::Rect2d& quadrant) {
            return quadrant.WidthRustF32() == 0.0 || quadrant.HeightRustF32() == 0.0;
        });
    assert(oneUlpHasCollapsedQuadrant && oneUlpHeightHasCollapsedQuadrant);
    const jaguar::Rect2d zeroWidthRect{1.0, 0.0, 1.0, 1.0};
    const auto zeroWidthQuadrants = zeroWidthRect.QuadrantsRustF32();
    assert(std::all_of(zeroWidthQuadrants.begin(), zeroWidthQuadrants.end(),
                       [](const jaguar::Rect2d& quadrant) {
                           return quadrant.WidthRustF32() == 0.0;
                       }));
    const jaguar::Rect2d zeroHeightRect{0.0, 1.0, 1.0, 1.0};
    const auto zeroHeightQuadrants = zeroHeightRect.QuadrantsRustF32();
    assert(std::all_of(zeroHeightQuadrants.begin(), zeroHeightQuadrants.end(),
                       [](const jaguar::Rect2d& quadrant) {
                           return quadrant.HeightRustF32() == 0.0;
                       }));
    const auto& rustQuadrantPanics =
        booleanValues(referenceValue(reference, "geometry.rect_quadrant_panic_contract"));
    assert((rustQuadrantPanics == std::vector<bool>{true, true, true, true}));

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

    const jaguar::Point2d accessorPoint{1.25, -2.5};
    const auto accessorEdge = jaguar::Edge2d::Create({0.0, 0.0}, {3.0, 4.0});
    assert(accessorEdge.HasValue());
    const auto accessorBox = accessorEdge.Value().BoundingBox();
    const auto accessorSides = utilityRect.Value().SidesRustF32();
    std::vector<double> independentAccessors = {
        accessorPoint.x, accessorPoint.y,
        accessorBox.xMin, accessorBox.yMin, accessorBox.xMax, accessorBox.yMax};
    for (const auto& side : accessorSides)
    {
        independentAccessors.insert(independentAccessors.end(), {
            side.start.x, side.start.y, side.end.x, side.end.y});
    }
    expectRustNumbers(reference, "geometry.independent_accessors",
                      independentAccessors);

    const jaguar::Transform2d transformFrom =
        jaguar::Transform2d::FromRotationTranslation(0.0, {3.0, -2.0});
    const jaguar::Point2d transformFromPoint{2.0, -3.0};
    const auto transformedFromPoint = transformFrom.ApplyRustF32(transformFromPoint);
    const auto transformFromCircle = jaguar::Circle2d::Create({1.0, 2.0}, 1.5);
    const auto transformFromEdge = jaguar::Edge2d::Create({-1.0, 0.0}, {2.0, 1.0});
    assert(transformFromCircle.HasValue() && transformFromEdge.HasValue());
    auto transformedFromCircle = transformFromCircle.Value();
    transformedFromCircle.TransformRustF32(transformFrom);
    auto transformedFromEdge = transformFromEdge.Value();
    transformedFromEdge.TransformRustF32(transformFrom);
    const auto transformedFromPolygon = referencePolygon.Value().TransformedRustF32(
        transformFrom);
    assert(transformedFromPolygon.HasValue());
    std::vector<double> transformFromValues = {
        transformedFromPoint.x, transformedFromPoint.y,
        transformedFromCircle.center.x, transformedFromCircle.center.y,
        transformedFromCircle.radius,
        transformedFromEdge.start.x, transformedFromEdge.start.y,
        transformedFromEdge.end.x, transformedFromEdge.end.y};
    for (const auto& point : transformedFromPolygon.Value().Vertices())
        transformFromValues.insert(transformFromValues.end(), {point.x, point.y});
    const auto& transformedFromBox = transformedFromPolygon.Value().BoundingBox();
    const auto transformedFromCentroid = transformedFromPolygon.Value().Centroid();
    const auto& transformedFromPoi =
        transformedFromPolygon.Value().PointOfInaccessibility();
    transformFromValues.insert(transformFromValues.end(), {
        transformedFromBox.xMin, transformedFromBox.yMin,
        transformedFromBox.xMax, transformedFromBox.yMax,
        transformedFromCentroid.x, transformedFromCentroid.y,
        transformedFromPolygon.Value().Area(),
        transformedFromPolygon.Value().Diameter(),
        transformedFromPoi.center.x, transformedFromPoi.center.y,
        transformedFromPoi.radius});
    expectRustNumbers(reference, "geometry.transform_from", transformFromValues);

    jaguar::SurrogateConfig transformSurrogateConfig;
    transformSurrogateConfig.poleLimits = {{{1U, 0.0}, {0U, 0.0}, {0U, 0.0}}};
    transformSurrogateConfig.failFastPoleAreaRatio = 1.0;
    transformSurrogateConfig.failFastPiers = 1U;
    auto surrogateSource = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 2.0}, {0.0, 2.0}});
    assert(surrogateSource.HasValue());
    const auto generatedSurrogate =
        surrogateSource.Value().GenerateSurrogate(transformSurrogateConfig);
    assert(generatedSurrogate.HasValue());
    const auto transformedSurrogatePolygon =
        surrogateSource.Value().TransformedRustF32(
            jaguar::Transform2d::FromRotationTranslation(0.37, {3.0, -2.0}));
    assert(transformedSurrogatePolygon.HasValue());
    const jaguar::Surrogate* transformedSurrogate =
        transformedSurrogatePolygon.Value().GetSurrogate();
    assert(transformedSurrogate != nullptr);
    std::vector<double> transformedSurrogateValues = {
        static_cast<double>(transformedSurrogatePolygon.Value().VertexCount())};
    for (const auto& point : transformedSurrogatePolygon.Value().Vertices())
        transformedSurrogateValues.insert(
            transformedSurrogateValues.end(), {point.x, point.y});
    const auto& transformedSurrogateBounds =
        transformedSurrogatePolygon.Value().BoundingBox();
    const auto transformedSurrogateCentroid =
        transformedSurrogatePolygon.Value().Centroid();
    const auto& transformedSurrogatePoi =
        transformedSurrogatePolygon.Value().PointOfInaccessibility();
    transformedSurrogateValues.insert(transformedSurrogateValues.end(), {
        transformedSurrogateBounds.xMin, transformedSurrogateBounds.yMin,
        transformedSurrogateBounds.xMax, transformedSurrogateBounds.yMax,
        transformedSurrogateCentroid.x, transformedSurrogateCentroid.y,
        transformedSurrogatePolygon.Value().Area(),
        transformedSurrogatePolygon.Value().Diameter(),
        transformedSurrogatePoi.center.x, transformedSurrogatePoi.center.y,
        transformedSurrogatePoi.radius,
        static_cast<double>(transformedSurrogate->Poles().size()),
        static_cast<double>(transformedSurrogate->FailFastPoleCount()),
        static_cast<double>(transformedSurrogate->Piers().size()),
        transformedSurrogate->ConvexHullArea(),
        static_cast<double>(transformedSurrogate->ConvexHullIndices().size())});
    for (const std::size_t index : transformedSurrogate->ConvexHullIndices())
        transformedSurrogateValues.push_back(static_cast<double>(index));
    for (const auto& pole : transformedSurrogate->Poles())
        transformedSurrogateValues.insert(transformedSurrogateValues.end(), {
            pole.center.x, pole.center.y, pole.radius});
    for (const auto& pier : transformedSurrogate->Piers())
        transformedSurrogateValues.insert(transformedSurrogateValues.end(), {
            pier.start.x, pier.start.y, pier.end.x, pier.end.y});
#ifdef JAGUARCORE_TEST_RELEASE
    expectRustNumbers(reference, "geometry.transform_from_surrogate_release",
                      transformedSurrogateValues);
#else
    expectRustNumbers(reference, "geometry.transform_from_surrogate_debug",
                      transformedSurrogateValues);
#endif
    const auto& rustSurrogateState = booleanValues(
        referenceValue(reference, "geometry.transform_from_surrogate_state"));
    assert((rustSurrogateState == std::vector<bool>{true, true}));

    const auto dispatchCircle = jaguar::Circle2d::Create({0.0, 0.0}, 1.0);
    const auto dispatchRect = jaguar::Rect2d::Create(0.0, 0.0, 2.0, 2.0);
    const auto dispatchTouchingRect = jaguar::Rect2d::Create(2.0, 0.0, 4.0, 2.0);
    const auto dispatchSeparatedRect = jaguar::Rect2d::Create(2.01, 0.0, 4.0, 2.0);
    const float dispatchEpsilon = std::numeric_limits<float>::epsilon();
    const auto dispatchNearRect = jaguar::Rect2d::Create(
        -1.0, 0.0, -dispatchEpsilon * 0.25, 2.0);
    const auto dispatchFarRect = jaguar::Rect2d::Create(
        -1.0, 0.0, -dispatchEpsilon * 2.0, 2.0);
    const auto dispatchCrossingEdge = jaguar::Edge2d::Create({-1.0, 1.0}, {3.0, 1.0});
    assert(dispatchCircle.HasValue() && dispatchRect.HasValue() &&
           dispatchTouchingRect.HasValue() && dispatchSeparatedRect.HasValue() &&
           dispatchNearRect.HasValue() && dispatchFarRect.HasValue() &&
           dispatchCrossingEdge.HasValue());
    const jaguar::Point2d circleInsidePoint{0.25, 0.0};
    const jaguar::Point2d circleOutsidePoint{3.0, 0.0};
    const jaguar::Point2d rectInsidePoint{1.0, 1.0};
    const jaguar::Point2d rectOutsidePoint{3.0, 1.0};
    const jaguar::Point2d nearRectPoint{-dispatchEpsilon * 0.25, 1.0};
    const jaguar::Point2d farRectPoint{-dispatchEpsilon * 2.0, 1.0};
    expectRustBooleans(reference, "geometry.trait_dispatch", {
        dispatchCircle.Value().ContainsRustF32(circleInsidePoint),
        dispatchCircle.Value().ContainsRustF32(circleOutsidePoint),
        dispatchRect.Value().ContainsRustF32(rectInsidePoint),
        dispatchRect.Value().ContainsRustF32(rectOutsidePoint),
        referencePolygon.Value().ContainsRustF32(rectInsidePoint),
        referencePolygon.Value().ContainsRustF32({5.0, 1.0}),
        dispatchRect.Value().ContainsRustF32(rectInsidePoint),
        dispatchRect.Value().ContainsRustF32(rectOutsidePoint),
        dispatchRect.Value().RelationToRustF32(dispatchTouchingRect.Value()) !=
            jaguar::RectRelation::Disjoint,
        dispatchRect.Value().RelationToRustF32(dispatchSeparatedRect.Value()) !=
            jaguar::RectRelation::Disjoint,
        dispatchRect.Value().AlmostRelationToRustF32(dispatchNearRect.Value()) !=
            jaguar::RectRelation::Disjoint,
        dispatchRect.Value().AlmostRelationToRustF32(dispatchFarRect.Value()) !=
            jaguar::RectRelation::Disjoint,
        dispatchRect.Value().AlmostContainsRustF32(nearRectPoint),
        dispatchRect.Value().AlmostContainsRustF32(farRectPoint),
        dispatchCrossingEdge.Value().IntersectsRustF32(dispatchRect.Value()),
        dispatchCrossingEdge.Value().IntersectsRustF32(dispatchRect.Value())});

    expectRustNumbers(reference, "geometry.simple_polygon_helpers", {
        referencePolygon.Value().Diameter(),
        referencePolygon.Value().BoundingBox().xMin,
        referencePolygon.Value().BoundingBox().yMin,
        referencePolygon.Value().BoundingBox().xMax,
        referencePolygon.Value().BoundingBox().yMax,
        referencePolygon.Value().Area(),
        referencePolygon.Value().PointOfInaccessibility().center.x,
        referencePolygon.Value().PointOfInaccessibility().center.y,
        referencePolygon.Value().PointOfInaccessibility().radius});

    const auto emptyPolygon = jaguar::SimplePolygon::Create({});
    const auto singletonPolygon = jaguar::SimplePolygon::Create({{1.0, 2.0}});
    const auto twoPointPolygon = jaguar::SimplePolygon::Create({{0.0, 0.0}, {2.0, 2.0}});
    const auto diagonalCollinearPolygon = jaguar::SimplePolygon::Create(
        {{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}});
    expectRustBooleans(reference, "geometry.simple_polygon_helper_rejections", {
        !emptyPolygon.HasValue(), !singletonPolygon.HasValue(),
        !twoPointPolygon.HasValue(), !diagonalCollinearPolygon.HasValue()});
    const auto& rustHelperFailures = booleanValues(
        referenceValue(reference, "geometry.simple_polygon_helper_failures"));
    assert((rustHelperFailures == std::vector<bool>{
        true, true, true, true, true, true, true, false, true, false}));

    const auto duplicateVertexPolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}});
    const auto horizontalLinePolygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}});
    expectRustBooleans(reference, "geometry.simple_polygon_degenerate_construction", {
        !duplicateVertexPolygon.HasValue(), !horizontalLinePolygon.HasValue()});
    const auto& helperDegenerateContract = booleanValues(
        referenceValue(reference, "geometry.simple_polygon_degenerate_helper_contract"));
    assert((helperDegenerateContract == std::vector<bool>{
        true, true, true, true, true, true, true}));

    const auto tiedXHull = jaguar::ConvexHull({
        {0.0, 1.0}, {1.0, 0.0}, {0.0, 0.0}, {1.0, 1.0}});
    std::vector<double> hullCoordinates;
    for (const auto& point : tiedXHull)
        hullCoordinates.insert(hullCoordinates.end(), {point.x, point.y});
    expectRustNumbers(reference, "geometry.convex_hull_tied_x", hullCoordinates);
    expectRustNumbers(reference, "geometry.convex_hull_singleton_count", {
        static_cast<double>(jaguar::ConvexHull({{1.0, 2.0}}).size())});

    const std::vector<std::vector<jaguar::Point2d>> degenerateHullInputs = {
        {},
        {{1.0, 2.0}},
        {{1.0, 2.0}, {1.0, 2.0}},
        {{0.0, 0.0}, {2.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}},
        {{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}},
        {{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}}};
    std::vector<double> degenerateHullCounts;
    std::vector<double> degenerateHullPoints;
    for (const auto& hullInput : degenerateHullInputs)
    {
        const auto hull = jaguar::ConvexHull(hullInput);
        degenerateHullCounts.push_back(static_cast<double>(hull.size()));
        for (const auto& point : hull)
            degenerateHullPoints.insert(
                degenerateHullPoints.end(), {point.x, point.y});
    }
    expectRustNumbers(reference, "geometry.convex_hull_finite_degenerates_counts",
                      degenerateHullCounts);
    expectRustNumbers(reference, "geometry.convex_hull_finite_degenerates_points",
                      degenerateHullPoints);
}
}

void runB2ShapeModificationDifferentialTests()
{
    const ReferenceMap b2Reference = loadReferenceMap("shape_modification_b2.txt");

    const auto narrowNotch = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 9.9}, {4.0, 9.9}, {4.0, 10.0}, {0.0, 10.0}});
    assert(narrowNotch.HasValue());
    const auto narrowInflated = jaguar::CloseNarrowConcavities(
        narrowNotch.Value(), jaguar::ShapeModifyMode::Inflate, {0.2, 0.0015});
    expectRustNumbers(b2Reference, "b2.narrow_notch.inflate_half_open_area",
                      shapeModificationValues(narrowInflated));

    const auto wrappedNotch = jaguar::SimplePolygon::Create({
        {0.1, 6.0}, {0.1, 4.0}, {0.0, 4.0}, {0.0, 0.0},
        {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}, {0.0, 6.0}});
    assert(wrappedNotch.HasValue());
    const auto wrappedInflated = jaguar::CloseNarrowConcavities(
        wrappedNotch.Value(), jaguar::ShapeModifyMode::Inflate, {0.2, 0.01});
    expectRustNumbers(b2Reference, "b2.wrapped_notch.inflate",
                      shapeModificationValues(wrappedInflated));

    const auto convexBump = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {5.0, 10.1}, {4.0, 10.0}, {0.0, 10.0}});
    assert(convexBump.HasValue());
    const auto bumpDeflated = jaguar::CloseNarrowConcavities(
        convexBump.Value(), jaguar::ShapeModifyMode::Deflate, {0.2, 0.01});
    expectRustNumbers(b2Reference, "b2.convex_bump.deflate",
                      shapeModificationValues(bumpDeflated));
}

void runGeometryDifferentialTests()
{
    runB2ShapeModificationDifferentialTests();

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
    const ReferenceMap b4aReference = loadReferenceMap("collision_entire_b4a.txt");
    assert(referenceValue(b4aReference,
                         "b4a.virtual_root_contains_filtered_entire") == "true");
    assert(referenceValue(b4aReference, "b4a.detect_without_filter") == "true");
    assert(referenceValue(b4aReference, "b4a.detect_filtering_entire") == "panic");

    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.HasValue());
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;

    // Pinned Rust panics in the post-edge Entire arm if the filter removes
    // that hazard; C++ preserves the filter contract and reports no collision.
    jaguar::CollisionEngine entireFilterEngine(bounds.Value(), config);
    const auto entireExterior = entireFilterEngine.RegisterHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0},
        rectangleShape(0.0, 0.0, 10.0, 10.0), false});
    assert(entireExterior.HasValue());
    const auto entireHole = entireFilterEngine.RegisterHazard({
        {jaguar::HazardKind::Hole, 0, 0, 0},
        rectangleShape(2.0, 2.0, 8.0, 8.0), false});
    assert(entireHole.HasValue());
    const auto filteredEntireQuery = jaguar::SimplePolygon::Create({
        {4.1, 4.1}, {4.3, 4.1}, {4.3, 4.3}, {4.1, 4.3}});
    assert(filteredEntireQuery.HasValue());
    assert(entireFilterEngine.DetectPolygonCollision(
        filteredEntireQuery.Value(), jaguar::NoFilter{}));
    jaguar::HazardFilter ignoreEntireHole{};
    ignoreEntireHole.irrelevantHandle =
        [handle = entireHole.Value()](jaguar::HazardHandle candidate,
                                      const jaguar::HazardEntity&) {
            return candidate == handle;
        };
    assert(!entireFilterEngine.DetectPolygonCollision(
        filteredEntireQuery.Value(), ignoreEntireHole));

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
