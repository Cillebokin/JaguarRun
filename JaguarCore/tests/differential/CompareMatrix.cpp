#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/SimplePolygon.h"

#include <cassert>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string loadFile(const char* name)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\") + name,
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\") + name,
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + name})
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

std::map<std::string, std::string> loadRustResults()
{
    const std::string content = loadFile("matrix_b11.txt");
    assert(!content.empty());
    std::map<std::string, std::string> result;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        const std::size_t separator = line.find('=');
        assert(separator != std::string::npos);
        result.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    return result;
}

bool rustExpected(const std::map<std::string, std::string>& results,
                  const std::string& key)
{
    const auto found = results.find(key);
    assert(found != results.end());
    assert(found->second == "true" || found->second == "false");
    return found->second == "true";
}
}

void runEdgeMatrixDifferentialTests()
{
    const auto rustResults = loadRustResults();
    assert(rustResults.size() == 18);
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
        const auto first = jaguar::Edge2d::Create(
            {number(values[1]), number(values[2])},
            {number(values[3]), number(values[4])});
        const auto second = jaguar::Edge2d::Create(
            {number(values[5]), number(values[6])},
            {number(values[7]), number(values[8])});
        assert(first.HasValue() && second.HasValue());
        const bool actual = first.Value().Intersects(second.Value());
        assert(actual == expected(values[9]));
        assert(actual == rustExpected(rustResults, "edge." + values[0]));
        ++cases;
    }
    assert(cases == 9);
}

void runContainmentMatrixDifferentialTests()
{
    const auto rustResults = loadRustResults();
    assert(rustResults.size() == 18);
    const auto bounds = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(bounds.HasValue());
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    jaguar::CollisionEngine engine(bounds.Value(), config);

    const auto hazardShape = jaguar::SimplePolygon::Create({
        {2.0, 2.0}, {8.0, 2.0}, {8.0, 8.0}, {2.0, 8.0}});
    assert(hazardShape.HasValue());

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
        const auto query = jaguar::SimplePolygon::Create({
            {number(values[1]), number(values[2])},
            {number(values[3]), number(values[2])},
            {number(values[3]), number(values[4])},
            {number(values[1]), number(values[4])}});
        assert(query.HasValue());
        const auto handle = engine.RegisterHazard({
            {values[9] == "hole" ? jaguar::HazardKind::Hole : jaguar::HazardKind::Exterior,
             0, 0, 0},
            std::make_shared<const jaguar::SimplePolygon>(hazardShape.Value()), false});
        assert(handle.HasValue());
        const bool actual = engine.DetectPolygonCollision(query.Value(), jaguar::NoFilter{});
        assert(actual == expected(values[10]));
        assert(actual == rustExpected(rustResults, "containment." + values[0]));
        assert(engine.DeregisterHazard(handle.Value()).HasValue());
        ++cases;
    }
    assert(cases == 9);
}
