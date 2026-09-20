#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/geometry/edge.h"
#include "JaguarCore/geometry/simple_polygon.h"

#include <cassert>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string loadFile(const char* name)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\tests\\fixtures\\rust_reference\\") + name,
             std::string("JaguarCore\\tests\\fixtures\\rust_reference\\") + name})
    {
        std::ifstream input(path);
        if (input)
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    return {};
}

std::vector<std::string> fields(const std::string& line)
{
    std::istringstream stream(line);
    std::vector<std::string> result;
    std::string field;
    while (stream >> field)
        result.push_back(field);
    return result;
}

double number(const std::string& value)
{
    return std::stod(value);
}

bool expected(const std::string& value)
{
    return value == "1";
}
}

void runEdgeMatrixDifferentialTests()
{
    const std::string content = loadFile("edge_matrix.tsv");
    assert(!content.empty());
    std::istringstream input(content);
    std::string line;
    std::size_t cases = 0;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        const auto values = fields(line);
        assert(values.size() == 10);
        const auto first = jaguar::Edge2d::create(
            {number(values[1]), number(values[2])},
            {number(values[3]), number(values[4])});
        const auto second = jaguar::Edge2d::create(
            {number(values[5]), number(values[6])},
            {number(values[7]), number(values[8])});
        assert(first.hasValue() && second.hasValue());
        assert(first.value().intersects(second.value()) == expected(values[9]));
        ++cases;
    }
    assert(cases == 9);
}

void runContainmentMatrixDifferentialTests()
{
    const auto bounds = jaguar::Rect2d::create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.hasValue());
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    jaguar::CollisionEngine engine(bounds.value(), config);

    const auto hazardShape = jaguar::SimplePolygon::create({
        {2.0, 2.0}, {8.0, 2.0}, {8.0, 8.0}, {2.0, 8.0}});
    assert(hazardShape.hasValue());

    const std::string content = loadFile("containment_matrix.tsv");
    assert(!content.empty());
    std::istringstream input(content);
    std::string line;
    std::size_t cases = 0;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        const auto values = fields(line);
        assert(values.size() == 11);
        const auto query = jaguar::SimplePolygon::create({
            {number(values[1]), number(values[2])},
            {number(values[3]), number(values[2])},
            {number(values[3]), number(values[4])},
            {number(values[1]), number(values[4])}});
        assert(query.hasValue());
        const auto handle = engine.registerHazard({
            {values[9] == "hole" ? jaguar::HazardKind::Hole : jaguar::HazardKind::Exterior,
             0, 0, 0},
            std::make_shared<const jaguar::SimplePolygon>(hazardShape.value()), false});
        assert(handle.hasValue());
        const bool actual = engine.detectPolygonCollision(query.value(), jaguar::NoFilter{});
        assert(actual == expected(values[10]));
        assert(engine.deregisterHazard(handle.value()).hasValue());
        ++cases;
    }
    assert(cases == 9);
}
