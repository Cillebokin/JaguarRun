#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>

namespace
{
std::map<std::string, std::string> loadReference(const char* name)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\") + name,
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\") + name,
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + name})
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            continue;
        const std::string content{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
        std::map<std::string, std::string> result;
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.empty() || line.front() == '#')
                continue;
            const std::size_t separator = line.find('=');
            assert(separator != std::string::npos);
            result.emplace(line.substr(0, separator), line.substr(separator + 1));
        }
        return result;
    }
    return {};
}

bool rustExpected(const std::map<std::string, std::string>& reference,
                  const char* key)
{
    const auto found = reference.find(key);
    assert(found != reference.end());
    assert(found->second == "true" || found->second == "false");
    return found->second == "true";
}

std::shared_ptr<const jaguar::SimplePolygon> rectangleShape(
    double xMin, double yMin, double xMax, double yMax)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(polygon.HasValue());
    return std::make_shared<const jaguar::SimplePolygon>(polygon.Value());
}

jaguar::SimplePolygon Rectangle(double xMin,
                                double yMin,
                                double xMax,
                                double yMax)
{
    return *rectangleShape(xMin, yMin, xMax, yMax);
}
}

void runBoundaryDifferentialTests()
{
    const auto reference = loadReference("boundary_b11.txt");
    assert(reference.size() == 11);
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;

    const auto rootBounds = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(rootBounds.HasValue());
    jaguar::CollisionEngine rootBoundedEngine(rootBounds.Value(), config);
    const auto rootExterior = rootBoundedEngine.RegisterHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0},
        rectangleShape(0.0, 0.0, 10.0, 10.0), false});
    assert(rootExterior.HasValue());
    const auto partiallyOutOfBounds = jaguar::SimplePolygon::Create({
        {-1.0, 1.0}, {1.0, 1.0}, {1.0, 2.0}, {-1.0, 2.0}});
    assert(partiallyOutOfBounds.HasValue());
    jaguar::HazardFilter ignoreExterior{};
    ignoreExterior.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Exterior;
    };
    // Rust returns collision immediately when a polygon extends beyond the CDE root,
    // before consulting the hazard filter.
    assert(rootBoundedEngine.DetectPolygonCollision(
               partiallyOutOfBounds.Value(), ignoreExterior) ==
           rustExpected(reference, "boundary.root_out_of_bounds_ignores_exterior"));

    const auto properFirst = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 4.0});
    const auto properSecond = jaguar::Edge2d::Create({0.0, 4.0}, {4.0, 0.0});
    assert(properFirst.HasValue() && properSecond.HasValue());
    assert(properFirst.Value().Intersects(properSecond.Value()) ==
           rustExpected(reference, "boundary.edge.proper"));
    const auto tangentFirst = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 0.0});
    const auto tangentSecond = jaguar::Edge2d::Create({2.0, 0.0}, {2.0, 3.0});
    assert(tangentFirst.HasValue() && tangentSecond.HasValue());
    assert(tangentFirst.Value().Intersects(tangentSecond.Value()) ==
           rustExpected(reference, "boundary.edge.tangent"));

    const auto endpointFirst = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 0.0});
    const auto endpointSecond = jaguar::Edge2d::Create({4.0, 0.0}, {4.0, 4.0});
    assert(endpointFirst.HasValue() && endpointSecond.HasValue());
    assert(endpointFirst.Value().Intersects(endpointSecond.Value()) ==
           rustExpected(reference, "boundary.edge.endpoint"));

    const auto collinearOverlapFirst = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 0.0});
    const auto collinearOverlapSecond = jaguar::Edge2d::Create({2.0, 0.0}, {6.0, 0.0});
    assert(collinearOverlapFirst.HasValue() && collinearOverlapSecond.HasValue());
    assert(collinearOverlapFirst.Value().Intersects(collinearOverlapSecond.Value()) ==
           rustExpected(reference, "boundary.edge.collinear_overlap"));

    const auto gap = jaguar::Edge2d::Create({0.0, 0.0}, {4.0, 0.0});
    const auto smallGap = jaguar::Edge2d::Create({4.0 + 5.0e-10, 0.0},
                                                   {8.0, 1.0});
    const auto largeGap = jaguar::Edge2d::Create({4.0 + 5.0e-7, 0.0},
                                                   {8.0, 1.0});
    assert(gap.HasValue() && smallGap.HasValue() && largeGap.HasValue());
    assert(gap.Value().Intersects(smallGap.Value()) ==
           rustExpected(reference, "boundary.edge.small_gap"));
    assert(gap.Value().Intersects(largeGap.Value()) ==
           rustExpected(reference, "boundary.edge.large_gap"));

    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 20.0, 20.0);
    assert(bounds.HasValue());
    jaguar::CollisionEngine engine(bounds.Value(), config);
    const auto hazard = engine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 900, 0, 0},
        rectangleShape(4.0, 4.0, 6.0, 6.0), true});
    assert(hazard.HasValue());

    const auto cornerTouch = Rectangle(6.0, 6.0, 8.0, 8.0);
    const auto smallPolygonGap = Rectangle(6.0 + 5.0e-10, 6.0,
                                            8.0 + 5.0e-10, 8.0);
    const auto largePolygonGap = Rectangle(6.0 + 5.0e-7, 6.0,
                                            8.0 + 5.0e-7, 8.0);
    assert(engine.DetectPolygonCollision(cornerTouch, jaguar::NoFilter{}) ==
           rustExpected(reference, "boundary.polygon.corner_touch"));
    assert(engine.DetectPolygonCollision(smallPolygonGap, jaguar::NoFilter{}) ==
           rustExpected(reference, "boundary.polygon.small_gap"));
    assert(engine.DetectPolygonCollision(largePolygonGap, jaguar::NoFilter{}) ==
           rustExpected(reference, "boundary.polygon.large_gap"));

    const auto interior = Rectangle(4.0 + 5.0e-10, 4.0 + 5.0e-10,
                                    6.0 - 5.0e-10, 6.0 - 5.0e-10);
    assert(engine.DetectPolygonCollision(interior, jaguar::NoFilter{}) ==
           rustExpected(reference, "boundary.polygon.interior"));
}
