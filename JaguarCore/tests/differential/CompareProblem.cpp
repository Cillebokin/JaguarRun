#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"
#include "JaguarCore/Assertions.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/IO/Importer.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/IO/SvgExport.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
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

ReferenceMap loadReferenceMap(const char* fileName)
{
    const auto contents = loadReference(fileName);
    assert(!contents.empty());
    ReferenceMap reference;
    std::istringstream input(contents);
    for (std::string line; std::getline(input, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto separator = line.find('=');
        if (line.empty() || separator == std::string::npos || line[0] == '#')
            continue;
        reference.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    assert(!reference.empty());
    return reference;
}

ReferenceMap loadProblemReference()
{
    return loadReferenceMap("problem_types_b7.txt");
}

void expectRustNumbers(const ReferenceMap& reference,
                       const std::string& key,
                       const std::vector<double>& actual)
{
    const auto found = reference.find(key);
    if (found == reference.end())
        std::fprintf(stderr, "B7 Rust reference is missing key: %s\n", key.c_str());
    assert(found != reference.end());

    std::vector<float> expected;
    std::istringstream input(found->second);
    bool parseValid = true;
    for (std::string part; std::getline(input, part, ',');)
    {
        try
        {
            std::size_t parsed = 0;
            const float value = std::stof(part, &parsed);
            if (parsed != part.size())
            {
                parseValid = false;
                break;
            }
            expected.push_back(value);
        }
        catch (...)
        {
            parseValid = false;
            break;
        }
    }
    if (!parseValid)
        expected.clear();

    if (expected.size() != actual.size())
        std::fprintf(stderr,
                     "B7 reference %s: Rust has %zu values, C++ has %zu\n",
                     key.c_str(), expected.size(), actual.size());
    assert(expected.size() == actual.size());

    std::size_t index = 0;
    for (double actualValue : actual)
    {
        const float actualF32 = static_cast<float>(actualValue);
        if (!jaguar::detail::AlmostEqualRustF32(expected[index], actualF32))
            std::fprintf(stderr,
                         "B7 reference %s[%zu]: Rust %.9g, C++ %.9g\n",
                         key.c_str(), index, expected[index], actualF32);
        assert(jaguar::detail::AlmostEqualRustF32(expected[index], actualF32));
        ++index;
    }
}

void expectRustNumbers(const ReferenceMap& reference,
                       const std::string& key,
                       std::initializer_list<double> actual)
{
    expectRustNumbers(reference, key, std::vector<double>(actual));
}

void expectRustText(const ReferenceMap& reference,
                    const std::string& key,
                    const std::string& actual)
{
    const auto found = reference.find(key);
    if (found == reference.end())
        std::fprintf(stderr, "B7 Rust reference is missing key: %s\n", key.c_str());
    assert(found != reference.end());
    if (found->second != actual)
        std::fprintf(stderr, "B7 reference %s: Rust %s, C++ %s\n",
                     key.c_str(), found->second.c_str(), actual.c_str());
    assert(found->second == actual);
}

std::vector<std::string> svgPathElementsForGroup(const std::string& svg,
                                                const std::string& groupId);
std::string svgAttribute(const std::string& element, const std::string& name);

std::vector<std::string> svgPathDataForGroup(const std::string& svg,
                                             const std::string& groupId)
{
    std::vector<std::string> paths;
    for (const std::string& element : svgPathElementsForGroup(svg, groupId))
        paths.push_back(svgAttribute(element, "d"));
    return paths;
}

std::vector<std::string> svgPathElementsForGroup(const std::string& svg,
                                                const std::string& groupId)
{
    const std::string groupMarker = "<g id=\"" + groupId + "\"";
    const std::size_t groupStart = svg.find(groupMarker);
    assert(groupStart != std::string::npos);
    const std::size_t groupEnd = svg.find("</g>", groupStart);
    assert(groupEnd != std::string::npos);

    std::vector<std::string> elements;
    std::size_t cursor = groupStart;
    while ((cursor = svg.find("<path", cursor)) != std::string::npos &&
           cursor < groupEnd)
    {
        const std::size_t tagEnd = svg.find('>', cursor);
        assert(tagEnd != std::string::npos && tagEnd < groupEnd);
        std::size_t elementEnd = tagEnd + 1;
        if (tagEnd == 0 || svg[tagEnd - 1] != '/')
        {
            const std::size_t closingTag = svg.find("</path>", elementEnd);
            assert(closingTag != std::string::npos && closingTag < groupEnd);
            elementEnd = closingTag + std::string("</path>").size();
        }
        elements.push_back(svg.substr(cursor, elementEnd - cursor));
        cursor = elementEnd;
    }
    return elements;
}

std::string svgAttribute(const std::string& element, const std::string& name)
{
    const std::string marker = " " + name + "=\"";
    const std::size_t markerStart = element.find(marker);
    assert(markerStart != std::string::npos);
    const std::size_t valueStart = markerStart + marker.size();
    const std::size_t valueEnd = element.find('"', valueStart);
    assert(valueEnd != std::string::npos);
    return element.substr(valueStart, valueEnd - valueStart);
}

std::string svgQualityTitle(const std::string& element)
{
    const std::string marker = "<title>quality zone, q: ";
    const std::size_t markerStart = element.find(marker);
    assert(markerStart != std::string::npos);
    const std::size_t valueStart = markerStart + marker.size();
    const std::size_t valueEnd = element.find("</title>", valueStart);
    assert(valueEnd != std::string::npos);
    return element.substr(valueStart, valueEnd - valueStart);
}

std::vector<std::string> svgPathStyleState(
    const std::string& svg,
    const std::string& groupId,
    const std::vector<std::string>& attributes)
{
    const auto elements = svgPathElementsForGroup(svg, groupId);
    std::vector<std::string> states;
    states.reserve(elements.size());
    for (const std::string& element : elements)
    {
        std::ostringstream fields;
        if (groupId == "quality_zones")
        {
            const std::string dataQuality = svgAttribute(element, "data-quality");
            assert(dataQuality == svgQualityTitle(element));
            fields << "q=" << dataQuality << ',';
        }
        for (std::size_t i = 0; i < attributes.size(); ++i)
        {
            if (i != 0)
                fields << ',';
            fields << attributes[i] << '=' << svgAttribute(element, attributes[i]);
        }
        states.push_back(fields.str());
    }
    return states;
}

std::string importedBppSvgContainerPathStyleState(const std::string& svg)
{
    const auto container = svgPathStyleState(
        svg, "container", {"d", "fill", "stroke", "stroke-width"});
    std::ostringstream state;
    state << "container:" << container.size() << ':';
    for (std::size_t i = 0; i < container.size(); ++i)
    {
        if (i != 0)
            state << '|';
        state << container[i];
    }
    return state.str();
}

std::string importedBppSvgQualityZeroHolePathStyleState(
    const std::string& svg)
{
    const auto qualityZones = svgPathStyleState(
        svg, "quality_zones",
        {"d", "fill", "fill-opacity", "stroke", "stroke-width",
         "stroke-opacity", "stroke-dasharray", "stroke-linecap",
         "stroke-linejoin"});
    std::vector<std::string> holePaths;
    for (const std::string& qualityZone : qualityZones)
    {
        if (qualityZone.rfind("q=0,", 0) == 0)
            holePaths.push_back(qualityZone);
    }
    assert(!holePaths.empty());
    std::ostringstream state;
    state << "quality0:" << holePaths.size() << ':';
    for (std::size_t i = 0; i < holePaths.size(); ++i)
    {
        if (i != 0)
            state << '|';
        state << holePaths[i];
    }
    return state.str();
}

std::string importedBppSvgQualityTwoFivePathStyleState(
    const std::string& svg)
{
    const auto qualityZones = svgPathStyleState(
        svg, "quality_zones",
        {"d", "fill", "fill-opacity", "stroke", "stroke-width",
         "stroke-opacity", "stroke-dasharray", "stroke-linecap",
         "stroke-linejoin"});
    std::vector<std::string> selectedPaths;
    std::size_t qualityTwoCount = 0;
    std::size_t qualityFiveCount = 0;
    for (const std::string& qualityZone : qualityZones)
    {
        if (qualityZone.rfind("q=2,", 0) == 0)
        {
            ++qualityTwoCount;
            selectedPaths.push_back(qualityZone);
        }
        else if (qualityZone.rfind("q=5,", 0) == 0)
        {
            ++qualityFiveCount;
            selectedPaths.push_back(qualityZone);
        }
    }
    assert(!selectedPaths.empty());
    std::ostringstream state;
    state << "quality2=" << qualityTwoCount << ";quality5="
          << qualityFiveCount << ";paths:";
    for (std::size_t i = 0; i < selectedPaths.size(); ++i)
    {
        if (i != 0)
            state << '|';
        state << selectedPaths[i];
    }
    return state.str();
}

std::string importedBppSvgCdStyleState(const std::string& svg)
{
    const auto container = svgPathStyleState(
        svg, "container", {"fill", "stroke", "stroke-width"});
    const auto qualityZones = svgPathStyleState(
        svg, "quality_zones",
        {"fill", "fill-opacity", "stroke", "stroke-width",
         "stroke-opacity", "stroke-dasharray", "stroke-linecap",
         "stroke-linejoin"});
    const auto join = [](const std::vector<std::string>& values) {
        std::ostringstream output;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i != 0)
                output << '|';
            output << values[i];
        }
        return output.str();
    };
    std::ostringstream state;
    state << "container:" << container.size() << ':' << join(container)
          << ";quality_zones:" << qualityZones.size() << ':'
          << join(qualityZones);
    return state.str();
}

std::string importedBppSvgPathState(const std::string& svg)
{
    const auto containerPaths = svgPathDataForGroup(svg, "container");
    const auto qualityZonePaths = svgPathDataForGroup(svg, "quality_zones");
    const auto join = [](const std::vector<std::string>& paths) {
        std::ostringstream value;
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            if (i != 0)
                value << '|';
            value << paths[i];
        }
        return value.str();
    };
    std::ostringstream state;
    state << "container:" << containerPaths.size() << ':' << join(containerPaths)
          << ";quality_zones:" << qualityZonePaths.size() << ':'
          << join(qualityZonePaths);
    return state.str();
}

std::string svgOpeningTagWithAttribute(const std::string& svg,
                                      const std::string& tagName,
                                      const std::string& attribute,
                                      const std::string& expectedValue)
{
    const std::string marker = "<" + tagName;
    std::size_t cursor = 0;
    while ((cursor = svg.find(marker, cursor)) != std::string::npos)
    {
        const std::size_t tagEnd = svg.find('>', cursor);
        assert(tagEnd != std::string::npos);
        const std::string tag = svg.substr(cursor, tagEnd - cursor + 1);
        if (svgAttribute(tag, attribute) == expectedValue)
            return tag;
        cursor = tagEnd + 1;
    }
    assert(false && "missing SVG element with expected attribute");
    return {};
}

std::string importedBppSvgPlacedState(const std::string& svg)
{
    const auto originalItemPaths = svgPathElementsForGroup(svg, "item_1");
    const auto collisionItemPaths = svgPathElementsForGroup(svg, "cd_shape_1");
    const auto collisionLines = svgPathElementsForGroup(svg, "collision_lines");
    assert(originalItemPaths.size() == 1);
    assert(collisionItemPaths.size() == 1);

    const std::string itemUse = svgOpeningTagWithAttribute(
        svg, "use", "href", "#item_1");
    const std::string cdUse = svgOpeningTagWithAttribute(
        svg, "use", "href", "#cd_shape_1");
    assert(collisionLines.size() == 1);
    std::ostringstream state;
    state << "item=" << svgAttribute(originalItemPaths.front(), "d")
          << ",fill=" << svgAttribute(originalItemPaths.front(), "fill")
          << ",fill-opacity="
          << svgAttribute(originalItemPaths.front(), "fill-opacity")
          << ",fill-rule="
          << svgAttribute(originalItemPaths.front(), "fill-rule")
          << ",stroke=" << svgAttribute(originalItemPaths.front(), "stroke")
          << ",stroke-width="
          << svgAttribute(originalItemPaths.front(), "stroke-width")
          << ",href=" << svgAttribute(itemUse, "href")
          << ",transform=" << svgAttribute(itemUse, "transform")
          << ";cd=" << svgAttribute(collisionItemPaths.front(), "d")
          << ",fill=" << svgAttribute(collisionItemPaths.front(), "fill")
          << ",stroke=" << svgAttribute(collisionItemPaths.front(), "stroke")
          << ",stroke-width="
          << svgAttribute(collisionItemPaths.front(), "stroke-width")
          << ",stroke-opacity="
          << svgAttribute(collisionItemPaths.front(), "stroke-opacity")
          << ",stroke-dasharray="
          << svgAttribute(collisionItemPaths.front(), "stroke-dasharray")
          << ",stroke-linecap="
          << svgAttribute(collisionItemPaths.front(), "stroke-linecap")
          << ",stroke-linejoin="
          << svgAttribute(collisionItemPaths.front(), "stroke-linejoin")
          << ",href=" << svgAttribute(cdUse, "href")
          << ",transform=" << svgAttribute(cdUse, "transform")
          << ";collisions=" << collisionLines.size();
    for (const std::string& line : collisionLines)
    {
        state << '|' << svgAttribute(line, "d")
              << ",stroke=" << svgAttribute(line, "stroke")
              << ",stroke-opacity=" << svgAttribute(line, "stroke-opacity")
              << ",stroke-width=" << svgAttribute(line, "stroke-width")
              << ",stroke-dasharray="
              << svgAttribute(line, "stroke-dasharray")
              << ",stroke-linecap=" << svgAttribute(line, "stroke-linecap")
              << ",stroke-linejoin=" << svgAttribute(line, "stroke-linejoin");
    }
    return state.str();
}

std::string importedBppSvgQualityFilterState(const jaguar::Layout& layout,
                                             const std::string& svg)
{
    const std::string groupMarker = "<g id=\"collision_lines\"";
    const auto collisionLines = svg.find(groupMarker) == std::string::npos
        ? std::vector<std::string>{}
        : svgPathElementsForGroup(svg, "collision_lines");
    std::ostringstream state;
    state << "feasible=" << (layout.IsFeasible() ? "true" : "false")
          << ";collisions=" << collisionLines.size();
    for (const std::string& line : collisionLines)
    {
        state << '|' << svgAttribute(line, "d")
              << ",stroke=" << svgAttribute(line, "stroke")
              << ",stroke-opacity=" << svgAttribute(line, "stroke-opacity")
              << ",stroke-width=" << svgAttribute(line, "stroke-width")
              << ",stroke-dasharray="
              << svgAttribute(line, "stroke-dasharray")
              << ",stroke-linecap=" << svgAttribute(line, "stroke-linecap")
              << ",stroke-linejoin=" << svgAttribute(line, "stroke-linejoin");
    }
    return state.str();
}

std::string canonicalSvgCollisionLinePath(const std::string& pathData)
{
    assert(pathData.size() > 1 && pathData.front() == 'M');
    const std::size_t lineCommand = pathData.find(" L");
    assert(lineCommand != std::string::npos);

    std::string firstEndpoint = pathData.substr(1, lineCommand - 1);
    std::string secondEndpoint = pathData.substr(lineCommand + 2);
    if (secondEndpoint < firstEndpoint)
        std::swap(firstEndpoint, secondEndpoint);
    return "M" + firstEndpoint + " L" + secondEndpoint;
}

std::string importedBppSvgMultiHazardState(const jaguar::Layout& layout,
                                           const std::string& svg)
{
    const std::string groupMarker = "<g id=\"collision_lines\"";
    const auto collisionLines = svg.find(groupMarker) == std::string::npos
        ? std::vector<std::string>{}
        : svgPathElementsForGroup(svg, "collision_lines");
    std::vector<std::string> lineStates;
    lineStates.reserve(collisionLines.size());
    for (const std::string& line : collisionLines)
    {
        std::ostringstream lineState;
        lineState << canonicalSvgCollisionLinePath(svgAttribute(line, "d"))
                  << ",stroke=" << svgAttribute(line, "stroke")
                  << ",stroke-opacity=" << svgAttribute(line, "stroke-opacity")
                  << ",stroke-width=" << svgAttribute(line, "stroke-width")
                  << ",stroke-dasharray="
                  << svgAttribute(line, "stroke-dasharray")
                  << ",stroke-linecap=" << svgAttribute(line, "stroke-linecap")
                  << ",stroke-linejoin=" << svgAttribute(line, "stroke-linejoin");
        lineStates.push_back(lineState.str());
    }
    std::sort(lineStates.begin(), lineStates.end());

    std::ostringstream state;
    state << "feasible=" << (layout.IsFeasible() ? "true" : "false")
          << ";collisions=" << lineStates.size();
    for (const std::string& lineState : lineStates)
        state << '|' << lineState;
    return state.str();
}

std::vector<std::string> svgCircleElementsForGroup(const std::string& svg,
                                                  const std::string& groupId)
{
    const std::string groupMarker = "<g id=\"" + groupId + "\"";
    const std::size_t groupStart = svg.find(groupMarker);
    assert(groupStart != std::string::npos);
    const std::size_t groupEnd = svg.find("</g>", groupStart);
    assert(groupEnd != std::string::npos);

    std::vector<std::string> elements;
    std::size_t cursor = groupStart;
    while ((cursor = svg.find("<circle", cursor)) != std::string::npos &&
           cursor < groupEnd)
    {
        const std::size_t tagEnd = svg.find('>', cursor);
        assert(tagEnd != std::string::npos && tagEnd < groupEnd);
        elements.push_back(svg.substr(cursor, tagEnd - cursor + 1));
        cursor = tagEnd + 1;
    }
    return elements;
}

std::string importedBppSvgExteriorState(const jaguar::Layout& layout,
                                        const std::string& svg)
{
    const auto circles = svgCircleElementsForGroup(svg, "collision_lines");
    std::ostringstream state;
    state << "feasible=" << (layout.IsFeasible() ? "true" : "false")
          << ";exterior_circles=" << circles.size();
    for (const std::string& circle : circles)
    {
        state << "|cx=" << svgAttribute(circle, "cx")
              << ",cy=" << svgAttribute(circle, "cy")
              << ",r=" << svgAttribute(circle, "r")
              << ",fill=" << svgAttribute(circle, "fill");
    }
    return state.str();
}

void appendImportedShapeState(std::vector<double>& values,
                              const jaguar::SimplePolygon& shape)
{
    const auto centroid = shape.Centroid();
    values.push_back(static_cast<double>(shape.VertexCount()));
    values.push_back(shape.Area());
    values.push_back(centroid.x);
    values.push_back(centroid.y);
    for (const auto& vertex : shape.Vertices())
    {
        values.push_back(vertex.x);
        values.push_back(vertex.y);
    }
}

std::vector<double> importedItemShapeState(const jaguar::Item& item)
{
    std::vector<double> values;
    appendImportedShapeState(values, item.GetOriginalShape().shape);
    appendImportedShapeState(values, item.CollisionShape());
    return values;
}

std::vector<double> importedContainerShapeState(
    const jaguar::Container& container)
{
    std::vector<double> values;
    appendImportedShapeState(values, container.GetOriginalShape().shape);
    appendImportedShapeState(values, container.CollisionShape());
    return values;
}

std::vector<double> importedBppBinCollectionState(
    const jaguar::BPInstance& instance)
{
    std::vector<double> values;
    values.push_back(static_cast<double>(instance.BinCount()));
    for (std::size_t i = 0; i < instance.BinCount(); ++i)
    {
        const jaguar::Bin& bin = instance.GetBin(i);
        values.push_back(static_cast<double>(bin.index));
        values.push_back(static_cast<double>(bin.externalId));
        values.push_back(static_cast<double>(bin.stock));
        values.push_back(static_cast<double>(bin.cost));
        appendImportedShapeState(values, bin.container.GetOriginalShape().shape);
        appendImportedShapeState(values, bin.container.CollisionShape());
    }
    return values;
}

std::string importedBppBinIntegerState(const jaguar::BPInstance& instance)
{
    std::ostringstream state;
    for (std::size_t i = 0; i < instance.BinCount(); ++i)
    {
        if (i != 0)
            state << ';';
        const jaguar::Bin& bin = instance.GetBin(i);
        state << bin.index << '/' << bin.externalId << '/' << bin.stock << '/'
              << bin.cost;
    }
    return state.str();
}

std::string externalBppBinIntegerState(
    const jaguar::ExternalBPInstance& instance)
{
    std::ostringstream state;
    for (std::size_t i = 0; i < instance.bins.size(); ++i)
    {
        if (i != 0)
            state << ';';
        const jaguar::ExternalBin& bin = instance.bins[i];
        state << bin.container.id << '/' << bin.stock << '/' << bin.cost;
    }
    return state.str();
}

void appendImportedQualityZoneState(
    std::vector<double>& values,
    const std::vector<jaguar::QualityZone>& qualityZones)
{
    // Rust stores quality zones in ten indexed slots; the C++ model stores
    // only populated groups. Expand the sparse C++ view to the same indexed
    // representation so this comparison covers geometry, not storage shape.
    constexpr std::size_t maxQualities = 10;
    values.push_back(static_cast<double>(maxQualities));
    for (std::size_t quality = 0; quality < maxQualities; ++quality)
    {
        const jaguar::QualityZone* matchingZone = nullptr;
        for (const jaguar::QualityZone& zone : qualityZones)
        {
            if (zone.quality == quality)
            {
                matchingZone = &zone;
                break;
            }
        }

        values.push_back(static_cast<double>(quality));
        values.push_back(matchingZone == nullptr
                             ? 0.0
                             : static_cast<double>(matchingZone->shapes.size()));
        if (matchingZone != nullptr)
        {
            for (const jaguar::SimplePolygon& shape : matchingZone->shapes)
                appendImportedShapeState(values, shape);
        }
    }
}

std::vector<double> importedContainerQualityShapeState(
    const jaguar::Container& container)
{
    std::vector<double> values = importedContainerShapeState(container);
    appendImportedQualityZoneState(values, container.OriginalQualityZones());
    appendImportedQualityZoneState(values, container.QualityZones());
    return values;
}

std::string importedContainerHazardProbeState(
    const jaguar::Container& container)
{
    struct Probe
    {
        const char* label;
        double xMin;
        double yMin;
        double xMax;
        double yMax;
    };
    const Probe probes[] = {
        {"explicit_q0", 12.75, 4.75, 13.25, 5.25},
        {"hole_a", 4.75, 4.75, 5.25, 5.25},
        {"hole_b", 8.75, 4.75, 9.25, 5.25},
        {"q2_a", 30.75, 4.75, 31.25, 5.25},
        {"q2_b", 25.25, 4.75, 25.75, 5.25},
        {"q2_c", 19.75, 5.25, 20.25, 5.75},
        {"q5", 5.25, 13.25, 5.75, 13.75},
    };
    const auto engine = container.CreateCollisionEngine();
    assert(engine != nullptr);

    std::ostringstream state;
    for (std::size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)
    {
        if (i != 0)
            state << ';';
        const Probe& probe = probes[i];
        const auto shape = jaguar::SimplePolygon::Create(
            {{probe.xMin, probe.yMin},
             {probe.xMax, probe.yMin},
             {probe.xMax, probe.yMax},
             {probe.xMin, probe.yMax}});
        assert(shape.HasValue());
        const auto collisions = engine->CollectPolygonCollisions(shape.Value());
        assert(collisions.size() == 1);
        const jaguar::HazardEntity& entity = collisions.front();
        state << probe.label << '=';
        switch (entity.kind)
        {
        case jaguar::HazardKind::Hole:
            state << "hole:" << entity.zoneIndex;
            break;
        case jaguar::HazardKind::InferiorQualityZone:
            state << "quality:" << entity.quality << ':' << entity.zoneIndex;
            break;
        case jaguar::HazardKind::Exterior:
            state << "exterior";
            break;
        case jaguar::HazardKind::PlacedItem:
            state << "placed_item";
            break;
        }
    }
    return state.str();
}

void compareJsonMetadataB8()
{
    const auto rustReference = loadReferenceMap("json_metadata_b8.txt");
    const auto svgReference = loadReferenceMap("svg_b8.txt");
    const auto inputs = loadReferenceMap("json_metadata_b8_inputs.txt");
    struct JsonCase
    {
        const char* inputName;
        const char* outputName;
    };
    const JsonCase cases[] = {
        {"unknown_top_level", "json.unknown_top_level_accepted"},
        {"duplicate_unknown_top_level",
         "json.duplicate_unknown_top_level_accepted"},
        {"unknown_item_metadata", "json.unknown_item_metadata_accepted"},
        {"duplicate_unknown_item_metadata",
         "json.duplicate_unknown_item_metadata_accepted"},
        {"duplicate_known_item_id", "json.duplicate_known_item_id_accepted"},
        {"unknown_rotation_field", "json.unknown_rotation_field_accepted"},
        {"duplicate_known_instance_name",
         "json.duplicate_known_instance_name_accepted"},
        {"duplicate_known_min_item_separation",
         "json.duplicate_known_min_item_separation_accepted"},
        {"duplicate_known_instance_items",
         "json.duplicate_known_instance_items_accepted"},
        {"duplicate_known_strip_height",
         "json.duplicate_known_strip_height_accepted"},
        {"duplicate_known_item_demand",
         "json.duplicate_known_item_demand_accepted"},
        {"duplicate_known_item_orientation",
         "json.duplicate_known_item_orientation_accepted"},
        {"duplicate_known_item_shape",
         "json.duplicate_known_item_shape_accepted"},
        {"duplicate_known_item_min_quality",
         "json.duplicate_known_item_min_quality_accepted"},
        {"duplicate_known_orientation_rotation",
         "json.duplicate_known_orientation_rotation_accepted"},
        {"duplicate_known_rotation_mode",
         "json.duplicate_known_rotation_mode_accepted"},
        {"duplicate_known_rotation_angles",
         "json.duplicate_known_rotation_angles_accepted"},
        {"duplicate_known_rotation_step",
         "json.duplicate_known_rotation_step_accepted"},
        {"duplicate_known_shape_type",
         "json.duplicate_known_shape_type_accepted"},
        {"duplicate_known_shape_data",
         "json.duplicate_known_shape_data_accepted"},
        {"duplicate_known_rectangle_x_min",
         "json.duplicate_known_rectangle_x_min_accepted"},
        {"duplicate_known_rectangle_y_min",
         "json.duplicate_known_rectangle_y_min_accepted"},
        {"duplicate_known_rectangle_width",
         "json.duplicate_known_rectangle_width_accepted"},
        {"duplicate_known_rectangle_height",
         "json.duplicate_known_rectangle_height_accepted"},
        {"duplicate_known_polygon_outer",
         "json.duplicate_known_polygon_outer_accepted"},
        {"duplicate_known_polygon_inner",
         "json.duplicate_known_polygon_inner_accepted"},
    };

    for (const JsonCase& testCase : cases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        const auto parsed = jaguar::ParseExternalSPInstanceJson(input->second);
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }

    const JsonCase containerCases[] = {
        {"container_unknown_field", "json.container_unknown_field_accepted"},
        {"container_duplicate_unknown_field",
         "json.container_duplicate_unknown_field_accepted"},
        {"container_duplicate_known_id",
         "json.container_duplicate_known_id_accepted"},
        {"container_duplicate_known_shape",
         "json.container_duplicate_known_shape_accepted"},
        {"container_duplicate_known_zones",
         "json.container_duplicate_known_zones_accepted"},
        {"quality_zone_unknown_field",
         "json.quality_zone_unknown_field_accepted"},
        {"quality_zone_duplicate_unknown_field",
         "json.quality_zone_duplicate_unknown_field_accepted"},
        {"quality_zone_duplicate_known_quality",
         "json.quality_zone_duplicate_known_quality_accepted"},
        {"quality_zone_duplicate_known_shape",
         "json.quality_zone_duplicate_known_shape_accepted"},
    };
    for (const JsonCase& testCase : containerCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        const auto parsed = jaguar::ParseExternalContainerJson(input->second);
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }

    const JsonCase bppCases[] = {
        {"bpp_bin_unknown_field", "json.bpp_bin_unknown_field_accepted"},
        {"bpp_bin_duplicate_unknown_field",
         "json.bpp_bin_duplicate_unknown_field_accepted"},
        {"bpp_bin_duplicate_known_stock",
         "json.bpp_bin_duplicate_known_stock_accepted"},
        {"bpp_bin_duplicate_known_cost",
         "json.bpp_bin_duplicate_known_cost_accepted"},
        {"bpp_bin_duplicate_known_id",
         "json.bpp_bin_duplicate_known_id_accepted"},
        {"bpp_bin_duplicate_known_shape",
         "json.bpp_bin_duplicate_known_shape_accepted"},
        {"bpp_bin_duplicate_known_zones",
         "json.bpp_bin_duplicate_known_zones_accepted"},
        {"bpp_instance_duplicate_known_bins",
         "json.bpp_instance_duplicate_known_bins_accepted"},
        {"bpp_instance_unknown_field",
         "json.bpp_instance_unknown_field_accepted"},
        {"bpp_instance_duplicate_unknown_field",
         "json.bpp_instance_duplicate_unknown_field_accepted"},
        {"bpp_instance_duplicate_known_name",
         "json.bpp_instance_duplicate_known_name_accepted"},
        {"bpp_instance_duplicate_known_min_item_separation",
         "json.bpp_instance_duplicate_known_min_item_separation_accepted"},
        {"bpp_instance_duplicate_known_items",
         "json.bpp_instance_duplicate_known_items_accepted"},
        {"bpp_item_unknown_field", "json.bpp_item_unknown_field_accepted"},
        {"bpp_item_duplicate_unknown_field",
         "json.bpp_item_duplicate_unknown_field_accepted"},
        {"bpp_item_duplicate_known_id",
         "json.bpp_item_duplicate_known_id_accepted"},
        {"bpp_item_duplicate_known_demand",
         "json.bpp_item_duplicate_known_demand_accepted"},
        {"bpp_instance_missing_min_item_separation",
         "json.bpp_instance_missing_min_item_separation_accepted"},
        {"bpp_instance_null_min_item_separation",
         "json.bpp_instance_null_min_item_separation_accepted"},
        {"bpp_item_u64_overflow_demand",
         "json.bpp_item_u64_overflow_demand_accepted"},
        {"bpp_bin_u64_overflow_stock",
         "json.bpp_bin_u64_overflow_stock_accepted"},
        {"bpp_bin_u64_overflow_cost",
         "json.bpp_bin_u64_overflow_cost_accepted"},
        {"bpp_bin_missing_id", "json.bpp_bin_missing_id_accepted"},
        {"bpp_bin_null_id", "json.bpp_bin_null_id_accepted"},
        {"bpp_bin_missing_shape", "json.bpp_bin_missing_shape_accepted"},
        {"bpp_bin_null_shape", "json.bpp_bin_null_shape_accepted"},
        {"bpp_bin_missing_stock", "json.bpp_bin_missing_stock_accepted"},
        {"bpp_bin_null_stock", "json.bpp_bin_null_stock_accepted"},
        {"bpp_bin_missing_cost", "json.bpp_bin_missing_cost_accepted"},
        {"bpp_bin_null_cost", "json.bpp_bin_null_cost_accepted"},
        {"bpp_bin_missing_zones", "json.bpp_bin_missing_zones_accepted"},
        {"bpp_bin_null_zones", "json.bpp_bin_null_zones_accepted"},
        {"bpp_bin_string_id", "json.bpp_bin_string_id_accepted"},
        {"bpp_bin_string_shape", "json.bpp_bin_string_shape_accepted"},
        {"bpp_bin_fractional_stock",
         "json.bpp_bin_fractional_stock_accepted"},
        {"bpp_bin_negative_cost", "json.bpp_bin_negative_cost_accepted"},
        {"bpp_bin_object_zones", "json.bpp_bin_object_zones_accepted"},
        {"bpp_instance_null_bins", "json.bpp_instance_null_bins_accepted"},
        {"bpp_instance_object_bins", "json.bpp_instance_object_bins_accepted"},
        {"bpp_instance_number_name",
         "json.bpp_instance_number_name_accepted"},
        {"bpp_instance_string_min_item_separation",
         "json.bpp_instance_string_min_item_separation_accepted"},
        {"bpp_instance_null_items",
         "json.bpp_instance_null_items_accepted"},
        {"bpp_instance_object_items",
         "json.bpp_instance_object_items_accepted"},
        {"bpp_item_string_id", "json.bpp_item_string_id_accepted"},
        {"bpp_item_fractional_demand",
         "json.bpp_item_fractional_demand_accepted"},
        {"bpp_item_string_orientation",
         "json.bpp_item_string_orientation_accepted"},
        {"bpp_item_string_shape",
         "json.bpp_item_string_shape_accepted"},
        {"bpp_item_string_min_quality",
         "json.bpp_item_string_min_quality_accepted"},
        {"bpp_item_missing_orientation",
         "json.bpp_item_missing_orientation_accepted"},
        {"bpp_item_null_orientation",
         "json.bpp_item_null_orientation_accepted"},
        {"bpp_item_empty_orientation",
         "json.bpp_item_empty_orientation_accepted"},
        {"bpp_item_null_rotation", "json.bpp_item_null_rotation_accepted"},
        {"bpp_item_empty_rotation", "json.bpp_item_empty_rotation_accepted"},
        {"bpp_item_null_rotation_mode",
         "json.bpp_item_null_rotation_mode_accepted"},
        {"bpp_item_unknown_rotation_mode",
         "json.bpp_item_unknown_rotation_mode_accepted"},
        {"bpp_item_continuous_extra_step",
         "json.bpp_item_continuous_extra_step_accepted"},
        {"bpp_item_discrete_missing_angles",
         "json.bpp_item_discrete_missing_angles_accepted"},
        {"bpp_item_discrete_null_angles",
         "json.bpp_item_discrete_null_angles_accepted"},
        {"bpp_item_stepped_missing_step",
         "json.bpp_item_stepped_missing_step_accepted"},
        {"bpp_item_stepped_null_step",
         "json.bpp_item_stepped_null_step_accepted"},
        {"bpp_item_missing_shape", "json.bpp_item_missing_shape_accepted"},
        {"bpp_item_null_shape", "json.bpp_item_null_shape_accepted"},
        {"bpp_item_missing_shape_type",
         "json.bpp_item_missing_shape_type_accepted"},
        {"bpp_item_null_shape_type",
         "json.bpp_item_null_shape_type_accepted"},
        {"bpp_item_number_shape_type",
         "json.bpp_item_number_shape_type_accepted"},
        {"bpp_item_unknown_shape_type",
         "json.bpp_item_unknown_shape_type_accepted"},
        {"bpp_item_missing_shape_data",
         "json.bpp_item_missing_shape_data_accepted"},
        {"bpp_item_null_shape_data",
         "json.bpp_item_null_shape_data_accepted"},
        {"bpp_item_array_shape_data",
         "json.bpp_item_array_shape_data_accepted"},
        {"bpp_item_rectangle_missing_x_min",
         "json.bpp_item_rectangle_missing_x_min_accepted"},
        {"bpp_item_rectangle_null_x_min",
         "json.bpp_item_rectangle_null_x_min_accepted"},
        {"bpp_item_rectangle_missing_y_min",
         "json.bpp_item_rectangle_missing_y_min_accepted"},
        {"bpp_item_rectangle_null_y_min",
         "json.bpp_item_rectangle_null_y_min_accepted"},
        {"bpp_item_rectangle_missing_width",
         "json.bpp_item_rectangle_missing_width_accepted"},
        {"bpp_item_rectangle_null_width",
         "json.bpp_item_rectangle_null_width_accepted"},
        {"bpp_item_rectangle_missing_height",
         "json.bpp_item_rectangle_missing_height_accepted"},
        {"bpp_item_rectangle_null_height",
         "json.bpp_item_rectangle_null_height_accepted"},
        {"bpp_item_simple_polygon_null_data",
         "json.bpp_item_simple_polygon_null_data_accepted"},
        {"bpp_item_simple_polygon_object_data",
         "json.bpp_item_simple_polygon_object_data_accepted"},
        {"bpp_item_simple_polygon_null_point",
         "json.bpp_item_simple_polygon_null_point_accepted"},
        {"bpp_item_simple_polygon_number_point",
         "json.bpp_item_simple_polygon_number_point_accepted"},
        {"bpp_item_simple_polygon_one_coordinate",
         "json.bpp_item_simple_polygon_one_coordinate_accepted"},
        {"bpp_item_simple_polygon_three_coordinates",
         "json.bpp_item_simple_polygon_three_coordinates_accepted"},
        {"bpp_item_simple_polygon_string_coordinate",
         "json.bpp_item_simple_polygon_string_coordinate_accepted"},
        {"bpp_item_simple_polygon_null_coordinate",
         "json.bpp_item_simple_polygon_null_coordinate_accepted"},
        {"bpp_item_simple_polygon_empty_points",
         "json.bpp_item_simple_polygon_empty_points_accepted"},
        {"bpp_item_polygon_array_data",
         "json.bpp_item_polygon_array_data_accepted"},
        {"bpp_item_polygon_missing_outer",
         "json.bpp_item_polygon_missing_outer_accepted"},
        {"bpp_item_polygon_null_outer",
         "json.bpp_item_polygon_null_outer_accepted"},
        {"bpp_item_polygon_object_outer",
         "json.bpp_item_polygon_object_outer_accepted"},
        {"bpp_item_polygon_null_inner",
         "json.bpp_item_polygon_null_inner_accepted"},
        {"bpp_item_polygon_object_inner",
         "json.bpp_item_polygon_object_inner_accepted"},
        {"bpp_item_polygon_missing_inner",
         "json.bpp_item_polygon_missing_inner_accepted"},
        {"bpp_item_multipolygon_null_data",
         "json.bpp_item_multipolygon_null_data_accepted"},
        {"bpp_item_multipolygon_object_data",
         "json.bpp_item_multipolygon_object_data_accepted"},
        {"bpp_item_multipolygon_null_element",
         "json.bpp_item_multipolygon_null_element_accepted"},
        {"bpp_item_multipolygon_missing_outer",
         "json.bpp_item_multipolygon_missing_outer_accepted"},
        {"bpp_item_multipolygon_null_outer",
         "json.bpp_item_multipolygon_null_outer_accepted"},
        {"bpp_item_multipolygon_null_inner",
         "json.bpp_item_multipolygon_null_inner_accepted"},
        {"bpp_item_multipolygon_empty_data",
         "json.bpp_item_multipolygon_empty_data_accepted"},
        {"bpp_item_valid_simple_polygon",
         "json.bpp_item_valid_simple_polygon_accepted"},
        {"bpp_item_valid_polygon_no_holes",
         "json.bpp_item_valid_polygon_no_holes_accepted"},
        {"bpp_item_valid_oblique_simple_polygon",
         "json.bpp_item_valid_oblique_simple_polygon_accepted"},
        {"bpp_item_valid_concave_polygon_no_holes",
         "json.bpp_item_valid_concave_polygon_no_holes_accepted"},
        {"bpp_item_positive_separation_simple_polygon",
         "json.bpp_item_positive_separation_simple_polygon_accepted"},
        {"bpp_item_positive_separation_polygon_no_holes",
         "json.bpp_item_positive_separation_polygon_no_holes_accepted"},
        {"bpp_bin_minsep_collapse_exact",
         "json.bpp_bin_minsep_collapse_exact_accepted"},
        {"bpp_bin_minsep_near_collapse_above",
         "json.bpp_bin_minsep_near_collapse_above_accepted"},
        {"bpp_container_minsep_hole_quality_zone",
         "json.bpp_container_minsep_hole_quality_zone_accepted"},
        {"bpp_container_minsep_invalid_hole",
         "json.bpp_container_minsep_invalid_hole_accepted"},
        {"bpp_container_minsep_invalid_quality_zone",
         "json.bpp_container_minsep_invalid_quality_zone_accepted"},
        {"bpp_container_minsep_multi_zone_order",
         "json.bpp_container_minsep_multi_zone_order_accepted"},
        {"bpp_multi_bin_sorted_zero_stock",
         "json.bpp_multi_bin_sorted_zero_stock_accepted"},
        {"bpp_multi_bin_duplicate_zero_stock_id",
         "json.bpp_multi_bin_duplicate_zero_stock_id_accepted"},
        {"bpp_multi_bin_all_zero_stock",
         "json.bpp_multi_bin_all_zero_stock_accepted"},
        {"bpp_multi_bin_u64_boundary_order",
         "json.bpp_multi_bin_u64_boundary_order_accepted"},
    };
    for (const JsonCase& testCase : bppCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        const auto parsed = jaguar::ParseExternalBPInstanceJson(input->second);
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }

    const JsonCase msppCases[] = {
        {"mspp_strip_unknown_field", "json.mspp_strip_unknown_field_accepted"},
        {"mspp_strip_duplicate_unknown_field",
         "json.mspp_strip_duplicate_unknown_field_accepted"},
        {"mspp_strip_duplicate_known_height",
         "json.mspp_strip_duplicate_known_height_accepted"},
        {"mspp_strip_duplicate_known_max_width",
         "json.mspp_strip_duplicate_known_max_width_accepted"},
        {"mspp_instance_duplicate_known_strips",
         "json.mspp_instance_duplicate_known_strips_accepted"},
        {"mspp_instance_unknown_field",
         "json.mspp_instance_unknown_field_accepted"},
        {"mspp_instance_duplicate_unknown_field",
         "json.mspp_instance_duplicate_unknown_field_accepted"},
        {"mspp_instance_duplicate_known_name",
         "json.mspp_instance_duplicate_known_name_accepted"},
        {"mspp_instance_duplicate_known_min_item_separation",
         "json.mspp_instance_duplicate_known_min_item_separation_accepted"},
        {"mspp_instance_duplicate_known_items",
         "json.mspp_instance_duplicate_known_items_accepted"},
        {"mspp_item_unknown_field", "json.mspp_item_unknown_field_accepted"},
        {"mspp_item_duplicate_unknown_field",
         "json.mspp_item_duplicate_unknown_field_accepted"},
        {"mspp_item_duplicate_known_id",
         "json.mspp_item_duplicate_known_id_accepted"},
        {"mspp_item_duplicate_known_demand",
         "json.mspp_item_duplicate_known_demand_accepted"},
        {"mspp_instance_missing_min_item_separation",
         "json.mspp_instance_missing_min_item_separation_accepted"},
        {"mspp_instance_null_min_item_separation",
         "json.mspp_instance_null_min_item_separation_accepted"},
        {"mspp_item_u64_overflow_demand",
         "json.mspp_item_u64_overflow_demand_accepted"},
        {"mspp_strip_missing_height",
         "json.mspp_strip_missing_height_accepted"},
        {"mspp_strip_null_height",
         "json.mspp_strip_null_height_accepted"},
        {"mspp_strip_missing_max_width",
         "json.mspp_strip_missing_max_width_accepted"},
        {"mspp_strip_null_max_width",
         "json.mspp_strip_null_max_width_accepted"},
        {"mspp_strip_string_height",
         "json.mspp_strip_string_height_accepted"},
        {"mspp_strip_boolean_max_width",
         "json.mspp_strip_boolean_max_width_accepted"},
        {"mspp_instance_null_strips",
         "json.mspp_instance_null_strips_accepted"},
        {"mspp_instance_array_strips",
         "json.mspp_instance_array_strips_accepted"},
        {"mspp_instance_number_name",
         "json.mspp_instance_number_name_accepted"},
        {"mspp_instance_string_min_item_separation",
         "json.mspp_instance_string_min_item_separation_accepted"},
        {"mspp_instance_null_items",
         "json.mspp_instance_null_items_accepted"},
        {"mspp_instance_object_items",
         "json.mspp_instance_object_items_accepted"},
        {"mspp_item_string_id", "json.mspp_item_string_id_accepted"},
        {"mspp_item_fractional_demand",
         "json.mspp_item_fractional_demand_accepted"},
        {"mspp_item_string_orientation",
         "json.mspp_item_string_orientation_accepted"},
        {"mspp_item_string_shape",
         "json.mspp_item_string_shape_accepted"},
        {"mspp_item_string_min_quality",
         "json.mspp_item_string_min_quality_accepted"},
        {"mspp_item_missing_orientation",
         "json.mspp_item_missing_orientation_accepted"},
        {"mspp_item_null_orientation",
         "json.mspp_item_null_orientation_accepted"},
        {"mspp_item_empty_orientation",
         "json.mspp_item_empty_orientation_accepted"},
        {"mspp_item_null_rotation", "json.mspp_item_null_rotation_accepted"},
        {"mspp_item_empty_rotation", "json.mspp_item_empty_rotation_accepted"},
        {"mspp_item_null_rotation_mode",
         "json.mspp_item_null_rotation_mode_accepted"},
        {"mspp_item_unknown_rotation_mode",
         "json.mspp_item_unknown_rotation_mode_accepted"},
        {"mspp_item_continuous_extra_step",
         "json.mspp_item_continuous_extra_step_accepted"},
        {"mspp_item_discrete_missing_angles",
         "json.mspp_item_discrete_missing_angles_accepted"},
        {"mspp_item_discrete_null_angles",
         "json.mspp_item_discrete_null_angles_accepted"},
        {"mspp_item_stepped_missing_step",
         "json.mspp_item_stepped_missing_step_accepted"},
        {"mspp_item_stepped_null_step",
         "json.mspp_item_stepped_null_step_accepted"},
        {"mspp_item_missing_shape", "json.mspp_item_missing_shape_accepted"},
        {"mspp_item_null_shape", "json.mspp_item_null_shape_accepted"},
        {"mspp_item_missing_shape_type",
         "json.mspp_item_missing_shape_type_accepted"},
        {"mspp_item_null_shape_type",
         "json.mspp_item_null_shape_type_accepted"},
        {"mspp_item_number_shape_type",
         "json.mspp_item_number_shape_type_accepted"},
        {"mspp_item_unknown_shape_type",
         "json.mspp_item_unknown_shape_type_accepted"},
        {"mspp_item_missing_shape_data",
         "json.mspp_item_missing_shape_data_accepted"},
        {"mspp_item_null_shape_data",
         "json.mspp_item_null_shape_data_accepted"},
        {"mspp_item_array_shape_data",
         "json.mspp_item_array_shape_data_accepted"},
        {"mspp_item_rectangle_missing_x_min",
         "json.mspp_item_rectangle_missing_x_min_accepted"},
        {"mspp_item_rectangle_null_x_min",
         "json.mspp_item_rectangle_null_x_min_accepted"},
        {"mspp_item_rectangle_missing_y_min",
         "json.mspp_item_rectangle_missing_y_min_accepted"},
        {"mspp_item_rectangle_null_y_min",
         "json.mspp_item_rectangle_null_y_min_accepted"},
        {"mspp_item_rectangle_missing_width",
         "json.mspp_item_rectangle_missing_width_accepted"},
        {"mspp_item_rectangle_null_width",
         "json.mspp_item_rectangle_null_width_accepted"},
        {"mspp_item_rectangle_missing_height",
         "json.mspp_item_rectangle_missing_height_accepted"},
        {"mspp_item_rectangle_null_height",
         "json.mspp_item_rectangle_null_height_accepted"},
        {"mspp_item_simple_polygon_null_data",
         "json.mspp_item_simple_polygon_null_data_accepted"},
        {"mspp_item_simple_polygon_object_data",
         "json.mspp_item_simple_polygon_object_data_accepted"},
        {"mspp_item_simple_polygon_null_point",
         "json.mspp_item_simple_polygon_null_point_accepted"},
        {"mspp_item_simple_polygon_number_point",
         "json.mspp_item_simple_polygon_number_point_accepted"},
        {"mspp_item_simple_polygon_one_coordinate",
         "json.mspp_item_simple_polygon_one_coordinate_accepted"},
        {"mspp_item_simple_polygon_three_coordinates",
         "json.mspp_item_simple_polygon_three_coordinates_accepted"},
        {"mspp_item_simple_polygon_string_coordinate",
         "json.mspp_item_simple_polygon_string_coordinate_accepted"},
        {"mspp_item_simple_polygon_null_coordinate",
         "json.mspp_item_simple_polygon_null_coordinate_accepted"},
        {"mspp_item_simple_polygon_empty_points",
         "json.mspp_item_simple_polygon_empty_points_accepted"},
        {"mspp_item_polygon_array_data",
         "json.mspp_item_polygon_array_data_accepted"},
        {"mspp_item_polygon_missing_outer",
         "json.mspp_item_polygon_missing_outer_accepted"},
        {"mspp_item_polygon_null_outer",
         "json.mspp_item_polygon_null_outer_accepted"},
        {"mspp_item_polygon_object_outer",
         "json.mspp_item_polygon_object_outer_accepted"},
        {"mspp_item_polygon_null_inner",
         "json.mspp_item_polygon_null_inner_accepted"},
        {"mspp_item_polygon_object_inner",
         "json.mspp_item_polygon_object_inner_accepted"},
        {"mspp_item_polygon_missing_inner",
         "json.mspp_item_polygon_missing_inner_accepted"},
        {"mspp_item_multipolygon_null_data",
         "json.mspp_item_multipolygon_null_data_accepted"},
        {"mspp_item_multipolygon_object_data",
         "json.mspp_item_multipolygon_object_data_accepted"},
        {"mspp_item_multipolygon_null_element",
         "json.mspp_item_multipolygon_null_element_accepted"},
        {"mspp_item_multipolygon_missing_outer",
         "json.mspp_item_multipolygon_missing_outer_accepted"},
        {"mspp_item_multipolygon_null_outer",
         "json.mspp_item_multipolygon_null_outer_accepted"},
        {"mspp_item_multipolygon_null_inner",
         "json.mspp_item_multipolygon_null_inner_accepted"},
        {"mspp_item_multipolygon_empty_data",
         "json.mspp_item_multipolygon_empty_data_accepted"},
        {"mspp_item_valid_simple_polygon",
         "json.mspp_item_valid_simple_polygon_accepted"},
        {"mspp_item_valid_polygon_no_holes",
         "json.mspp_item_valid_polygon_no_holes_accepted"},
        {"mspp_item_valid_oblique_simple_polygon",
         "json.mspp_item_valid_oblique_simple_polygon_accepted"},
        {"mspp_item_valid_concave_polygon_no_holes",
         "json.mspp_item_valid_concave_polygon_no_holes_accepted"},
        {"mspp_item_positive_separation_simple_polygon",
         "json.mspp_item_positive_separation_simple_polygon_accepted"},
        {"mspp_item_positive_separation_polygon_no_holes",
         "json.mspp_item_positive_separation_polygon_no_holes_accepted"},
        {"mspp_strip_minsep_collapse_exact",
         "json.mspp_strip_minsep_collapse_exact_accepted"},
        {"mspp_strip_minsep_near_collapse_above",
         "json.mspp_strip_minsep_near_collapse_above_accepted"},
    };
    for (const JsonCase& testCase : msppCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        const auto parsed = jaguar::ParseExternalMSPInstanceJson(input->second);
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }

    struct JsonImportCase
    {
        const char* inputName;
        const char* outputName;
        bool bpp;
    };
    const JsonImportCase importCases[] = {
        {"bpp_item_simple_polygon_empty_points",
         "json.bpp_item_simple_polygon_empty_points_import_outcome", true},
        {"bpp_item_polygon_missing_inner",
         "json.bpp_item_polygon_missing_inner_import_outcome", true},
        {"bpp_item_multipolygon_empty_data",
         "json.bpp_item_multipolygon_empty_data_import_outcome", true},
        {"mspp_item_simple_polygon_empty_points",
         "json.mspp_item_simple_polygon_empty_points_import_outcome", false},
        {"mspp_item_polygon_missing_inner",
         "json.mspp_item_polygon_missing_inner_import_outcome", false},
        {"mspp_item_multipolygon_empty_data",
         "json.mspp_item_multipolygon_empty_data_import_outcome", false},
        {"bpp_item_valid_simple_polygon",
         "json.bpp_item_valid_simple_polygon_import_outcome", true},
        {"bpp_item_valid_polygon_no_holes",
         "json.bpp_item_valid_polygon_no_holes_import_outcome", true},
        {"mspp_item_valid_simple_polygon",
         "json.mspp_item_valid_simple_polygon_import_outcome", false},
        {"mspp_item_valid_polygon_no_holes",
         "json.mspp_item_valid_polygon_no_holes_import_outcome", false},
        {"bpp_item_valid_oblique_simple_polygon",
         "json.bpp_item_valid_oblique_simple_polygon_import_outcome", true},
        {"bpp_item_valid_concave_polygon_no_holes",
         "json.bpp_item_valid_concave_polygon_no_holes_import_outcome", true},
        {"mspp_item_valid_oblique_simple_polygon",
         "json.mspp_item_valid_oblique_simple_polygon_import_outcome", false},
        {"mspp_item_valid_concave_polygon_no_holes",
         "json.mspp_item_valid_concave_polygon_no_holes_import_outcome", false},
        {"bpp_item_positive_separation_simple_polygon",
         "json.bpp_item_positive_separation_simple_polygon_import_outcome", true},
        {"bpp_item_positive_separation_polygon_no_holes",
         "json.bpp_item_positive_separation_polygon_no_holes_import_outcome", true},
        {"mspp_item_positive_separation_simple_polygon",
         "json.mspp_item_positive_separation_simple_polygon_import_outcome", false},
        {"mspp_item_positive_separation_polygon_no_holes",
         "json.mspp_item_positive_separation_polygon_no_holes_import_outcome", false},
        {"bpp_bin_minsep_collapse_exact",
         "json.bpp_bin_minsep_collapse_exact_import_outcome", true},
        {"bpp_bin_minsep_near_collapse_above",
         "json.bpp_bin_minsep_near_collapse_above_import_outcome", true},
        {"bpp_container_minsep_hole_quality_zone",
         "json.bpp_container_minsep_hole_quality_zone_import_outcome", true},
        {"bpp_container_minsep_invalid_hole",
         "json.bpp_container_minsep_invalid_hole_import_outcome", true},
        {"bpp_container_minsep_invalid_quality_zone",
         "json.bpp_container_minsep_invalid_quality_zone_import_outcome", true},
        {"bpp_container_minsep_multi_zone_order",
         "json.bpp_container_minsep_multi_zone_order_import_outcome", true},
        {"bpp_multi_bin_sorted_zero_stock",
         "json.bpp_multi_bin_sorted_zero_stock_import_outcome", true},
        {"bpp_multi_bin_duplicate_zero_stock_id",
         "json.bpp_multi_bin_duplicate_zero_stock_id_import_outcome", true},
        {"bpp_multi_bin_all_zero_stock",
         "json.bpp_multi_bin_all_zero_stock_import_outcome", true},
        {"bpp_multi_bin_u64_boundary_order",
         "json.bpp_multi_bin_u64_boundary_order_import_outcome", true},
        {"mspp_strip_minsep_collapse_exact",
         "json.mspp_strip_minsep_collapse_exact_import_outcome", false},
        {"mspp_strip_minsep_near_collapse_above",
         "json.mspp_strip_minsep_near_collapse_above_import_outcome", false},
    };
    const auto classifyImportOutcome = [](const auto& imported) {
        if (imported.HasValue())
            return std::string("success");
        switch (imported.GetError().code)
        {
        case jaguar::ErrorCode::InvalidPolygon:
            return std::string("invalid_geometry");
        case jaguar::ErrorCode::OffsetFailed:
            return std::string("offset_failure");
        case jaguar::ErrorCode::UnsupportedShape:
            return std::string("unsupported_shape");
        case jaguar::ErrorCode::InvalidInput:
            if (imported.GetError().message == "external bin IDs must be unique")
                return std::string("duplicate_bin_ids");
            if (imported.GetError().message == "instance must have positive bin stock")
                return std::string("no_positive_bin_stock");
            return std::string("other_error");
        default:
            return std::string("other_error");
        }
    };
    for (const JsonImportCase& testCase : importCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        std::string outcome;
        if (testCase.bpp)
            outcome = classifyImportOutcome(
                jaguar::Importer{}.ImportBPInstanceJson(input->second));
        else
            outcome = classifyImportOutcome(
                jaguar::Importer{}.ImportMSPInstanceJson(input->second));
        const std::string referenceKey = testCase.outputName;
        expectRustText(rustReference, std::string("debug.") + referenceKey,
                       outcome);
        expectRustText(rustReference, std::string("release.") + referenceKey,
                       outcome);
    }
    const auto duplicateBinInput =
        inputs.find("bpp_multi_bin_duplicate_zero_stock_id");
    assert(duplicateBinInput != inputs.end());
    const auto duplicateBinImport =
        jaguar::Importer{}.ImportBPInstanceJson(duplicateBinInput->second);
    assert(!duplicateBinImport.HasValue());
    assert(duplicateBinImport.GetError().code == jaguar::ErrorCode::InvalidInput);
    assert(duplicateBinImport.GetError().message ==
           "external bin IDs must be unique");

    const auto allZeroStockInput = inputs.find("bpp_multi_bin_all_zero_stock");
    assert(allZeroStockInput != inputs.end());
    const auto allZeroStockImport =
        jaguar::Importer{}.ImportBPInstanceJson(allZeroStockInput->second);
    assert(!allZeroStockImport.HasValue());
    assert(allZeroStockImport.GetError().code == jaguar::ErrorCode::InvalidInput);
    assert(allZeroStockImport.GetError().message ==
           "instance must have positive bin stock");

    const JsonCase msppStripContainerCases[] = {
        {"mspp_strip_minsep_collapse_exact",
         "json.mspp_strip_minsep_collapse_exact_container_outcome"},
        {"mspp_strip_minsep_near_collapse_above",
         "json.mspp_strip_minsep_near_collapse_above_container_outcome"},
    };
    for (const JsonCase& testCase : msppStripContainerCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        std::string outcome;
        const auto imported =
            jaguar::Importer{}.ImportMSPInstanceJson(input->second);
        if (!imported.HasValue())
        {
            outcome = "instance=other_error;container=not_reached";
        }
        else
        {
            outcome = "instance=success;container=" +
                      classifyImportOutcome(
                          imported.Value().BaseStrip().CreateContainer());
        }
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName, outcome);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName, outcome);
    }

    struct JsonImportShapeCase
    {
        const char* inputName;
        const char* outputName;
        bool bpp;
        bool expectInflatedCollisionShape;
    };
    const JsonImportShapeCase importShapeCases[] = {
        {"bpp_item_valid_simple_polygon",
         "json.bpp_item_valid_simple_polygon_shape_state", true, false},
        {"bpp_item_valid_polygon_no_holes",
         "json.bpp_item_valid_polygon_no_holes_shape_state", true, false},
        {"mspp_item_valid_simple_polygon",
         "json.mspp_item_valid_simple_polygon_shape_state", false, false},
        {"mspp_item_valid_polygon_no_holes",
         "json.mspp_item_valid_polygon_no_holes_shape_state", false, false},
        {"bpp_item_valid_oblique_simple_polygon",
         "json.bpp_item_valid_oblique_simple_polygon_shape_state", true, false},
        {"bpp_item_valid_concave_polygon_no_holes",
         "json.bpp_item_valid_concave_polygon_no_holes_shape_state", true, false},
        {"mspp_item_valid_oblique_simple_polygon",
         "json.mspp_item_valid_oblique_simple_polygon_shape_state", false, false},
        {"mspp_item_valid_concave_polygon_no_holes",
         "json.mspp_item_valid_concave_polygon_no_holes_shape_state", false, false},
        {"bpp_item_positive_separation_simple_polygon",
         "json.bpp_item_positive_separation_simple_polygon_shape_state", true, true},
        {"bpp_item_positive_separation_polygon_no_holes",
         "json.bpp_item_positive_separation_polygon_no_holes_shape_state", true, true},
        {"mspp_item_positive_separation_simple_polygon",
         "json.mspp_item_positive_separation_simple_polygon_shape_state", false, true},
        {"mspp_item_positive_separation_polygon_no_holes",
         "json.mspp_item_positive_separation_polygon_no_holes_shape_state", false, true},
    };
    for (const JsonImportShapeCase& testCase : importShapeCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        if (testCase.bpp)
        {
            const auto imported =
                jaguar::Importer{}.ImportBPInstanceJson(input->second);
            assert(imported.HasValue());
            assert(imported.Value().ItemCount() == 1);
            const auto& item = imported.Value().GetItem(0);
            if (testCase.expectInflatedCollisionShape)
                assert(item.CollisionShape().Area() >
                       item.GetOriginalShape().shape.Area());
            expectRustNumbers(rustReference,
                              std::string("debug.") + testCase.outputName,
                              importedItemShapeState(item));
            expectRustNumbers(rustReference,
                              std::string("release.") + testCase.outputName,
                              importedItemShapeState(item));
        }
        else
        {
            const auto imported =
                jaguar::Importer{}.ImportMSPInstanceJson(input->second);
            assert(imported.HasValue());
            assert(imported.Value().ItemCount() == 1);
            const auto& item = imported.Value().GetItem(0);
            if (testCase.expectInflatedCollisionShape)
                assert(item.CollisionShape().Area() >
                       item.GetOriginalShape().shape.Area());
            expectRustNumbers(rustReference,
                              std::string("debug.") + testCase.outputName,
                              importedItemShapeState(item));
            expectRustNumbers(rustReference,
                              std::string("release.") + testCase.outputName,
                              importedItemShapeState(item));
        }
    }

    struct JsonImportContainerShapeCase
    {
        const char* inputName;
        const char* outputName;
        bool bpp;
    };
    const JsonImportContainerShapeCase importContainerShapeCases[] = {
        {"bpp_item_positive_separation_simple_polygon",
         "json.bpp_item_positive_separation_simple_polygon_container_shape_state",
         true},
        {"bpp_item_positive_separation_polygon_no_holes",
         "json.bpp_item_positive_separation_polygon_no_holes_container_shape_state",
         true},
        {"mspp_item_positive_separation_simple_polygon",
         "json.mspp_item_positive_separation_simple_polygon_container_shape_state",
         false},
        {"mspp_item_positive_separation_polygon_no_holes",
         "json.mspp_item_positive_separation_polygon_no_holes_container_shape_state",
         false},
        {"bpp_bin_minsep_near_collapse_above",
         "json.bpp_bin_minsep_near_collapse_above_container_shape_state", true},
        {"mspp_strip_minsep_near_collapse_above",
         "json.mspp_strip_minsep_near_collapse_above_container_shape_state", false},
    };
    for (const JsonImportContainerShapeCase& testCase :
         importContainerShapeCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        std::vector<double> state;
        if (testCase.bpp)
        {
            const auto imported =
                jaguar::Importer{}.ImportBPInstanceJson(input->second);
            assert(imported.HasValue());
            assert(imported.Value().BinCount() == 1);
            state = importedContainerShapeState(
                imported.Value().GetBin(0).container);
        }
        else
        {
            const auto imported =
                jaguar::Importer{}.ImportMSPInstanceJson(input->second);
            assert(imported.HasValue());
            const auto container =
                imported.Value().BaseStrip().CreateContainer();
            assert(container.HasValue());
            state = importedContainerShapeState(container.Value());
        }
        expectRustNumbers(rustReference,
                          std::string("debug.") + testCase.outputName, state);
        expectRustNumbers(rustReference,
                          std::string("release.") + testCase.outputName, state);
    }

    const auto holeAndZoneInput =
        inputs.find("bpp_container_minsep_hole_quality_zone");
    assert(holeAndZoneInput != inputs.end());
    const auto holeAndZoneInstance =
        jaguar::Importer{}.ImportBPInstanceJson(holeAndZoneInput->second);
    assert(holeAndZoneInstance.HasValue());
    assert(holeAndZoneInstance.Value().BinCount() == 1);
    const auto holeAndZoneState = importedContainerQualityShapeState(
        holeAndZoneInstance.Value().GetBin(0).container);
    expectRustNumbers(
        rustReference,
        "debug.json.bpp_container_minsep_hole_quality_zone_container_quality_shape_state",
        holeAndZoneState);
    expectRustNumbers(
        rustReference,
        "release.json.bpp_container_minsep_hole_quality_zone_container_quality_shape_state",
        holeAndZoneState);

    const auto multiZoneInput =
        inputs.find("bpp_container_minsep_multi_zone_order");
    assert(multiZoneInput != inputs.end());
    const auto multiZoneInstance =
        jaguar::Importer{}.ImportBPInstanceJson(multiZoneInput->second);
    assert(multiZoneInstance.HasValue());
    assert(multiZoneInstance.Value().BinCount() == 1);
    const auto& multiZoneContainer =
        multiZoneInstance.Value().GetBin(0).container;
    const auto multiZoneShapeState =
        importedContainerQualityShapeState(multiZoneContainer);
    expectRustNumbers(
        rustReference,
        "debug.json.bpp_container_minsep_multi_zone_order_container_quality_shape_state",
        multiZoneShapeState);
    expectRustNumbers(
        rustReference,
        "release.json.bpp_container_minsep_multi_zone_order_container_quality_shape_state",
        multiZoneShapeState);

    const std::string expectedHazardMapping =
        "explicit_q0=hole:0;hole_a=hole:1;hole_b=hole:2;"
        "q2_a=quality:2:0;q2_b=quality:2:1;q2_c=quality:2:2;"
        "q5=quality:5:0";
    expectRustText(
        rustReference,
        "debug.json.bpp_container_minsep_multi_zone_order_hazard_mapping",
        expectedHazardMapping);
    expectRustText(
        rustReference,
        "release.json.bpp_container_minsep_multi_zone_order_hazard_mapping",
        expectedHazardMapping);
    const std::string actualHazardMapping =
        importedContainerHazardProbeState(multiZoneContainer);
    expectRustText(
        rustReference,
        "debug.json.bpp_container_minsep_multi_zone_order_hazard_mapping",
        actualHazardMapping);
    expectRustText(
        rustReference,
        "release.json.bpp_container_minsep_multi_zone_order_hazard_mapping",
        actualHazardMapping);
    const jaguar::Layout multiZoneLayout(multiZoneContainer);
    const auto multiZoneSvg = jaguar::ExportLayoutSvg(multiZoneLayout);
    assert(multiZoneSvg.HasValue());
    const std::string multiZoneSvgRoot = svgOpeningTagWithAttribute(
        multiZoneSvg.Value(), "svg", "xmlns", "http://www.w3.org/2000/svg");
    const auto multiZoneContainerPaths =
        svgPathElementsForGroup(multiZoneSvg.Value(), "container");
    assert(multiZoneContainerPaths.size() == 1);
    const std::string multiZoneCanvasState =
        "viewBox=" + svgAttribute(multiZoneSvgRoot, "viewBox") +
        ";container_stroke_width=" +
        svgAttribute(multiZoneContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_container_minsep_multi_zone_order_canvas_state",
        multiZoneCanvasState);
    expectRustText(
        svgReference,
        "release.svg.bpp_container_minsep_multi_zone_order_canvas_state",
        multiZoneCanvasState);

    jaguar::SvgExportOptions scaledCanvasOptions;
    scaledCanvasOptions.theme.strokeWidthMultiplier = 3.25;
    const auto scaledCanvasSvg =
        jaguar::ExportLayoutSvg(multiZoneLayout, scaledCanvasOptions);
    assert(scaledCanvasSvg.HasValue());
    const std::string scaledCanvasRoot = svgOpeningTagWithAttribute(
        scaledCanvasSvg.Value(), "svg", "xmlns", "http://www.w3.org/2000/svg");
    const auto scaledCanvasContainerPaths =
        svgPathElementsForGroup(scaledCanvasSvg.Value(), "container");
    assert(scaledCanvasContainerPaths.size() == 1);
    assert(svgAttribute(scaledCanvasRoot, "viewBox") ==
           svgAttribute(multiZoneSvgRoot, "viewBox"));
    const std::string scaledCanvasState =
        "viewBox=" + svgAttribute(scaledCanvasRoot, "viewBox") +
        ";stroke_multiplier=3.25;container_stroke_width=" +
        svgAttribute(scaledCanvasContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_container_minsep_multi_zone_order_scaled_canvas_state",
        scaledCanvasState);
    expectRustText(
        svgReference,
        "release.svg.bpp_container_minsep_multi_zone_order_scaled_canvas_state",
        scaledCanvasState);

    const auto portraitCanvasInput = inputs.find("bpp_svg_portrait_canvas");
    assert(portraitCanvasInput != inputs.end());
    const auto portraitCanvasInstance =
        jaguar::Importer{}.ImportBPInstanceJson(portraitCanvasInput->second);
    assert(portraitCanvasInstance.HasValue());
    assert(portraitCanvasInstance.Value().BinCount() == 1);
    const jaguar::Layout portraitCanvasLayout(
        portraitCanvasInstance.Value().GetBin(0).container);
    const auto portraitCanvasSvg =
        jaguar::ExportLayoutSvg(portraitCanvasLayout);
    assert(portraitCanvasSvg.HasValue());
    const std::string portraitCanvasRoot = svgOpeningTagWithAttribute(
        portraitCanvasSvg.Value(), "svg", "xmlns", "http://www.w3.org/2000/svg");
    const auto portraitContainerPaths =
        svgPathElementsForGroup(portraitCanvasSvg.Value(), "container");
    assert(portraitContainerPaths.size() == 1);
    const std::string portraitCanvasState =
        "viewBox=" + svgAttribute(portraitCanvasRoot, "viewBox") +
        ";container_stroke_width=" +
        svgAttribute(portraitContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_portrait_canvas_state",
        portraitCanvasState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_portrait_canvas_state",
        portraitCanvasState);

    jaguar::SvgExportOptions portraitScaledOptions;
    portraitScaledOptions.theme.strokeWidthMultiplier = 3.25;
    const auto portraitScaledSvg =
        jaguar::ExportLayoutSvg(portraitCanvasLayout, portraitScaledOptions);
    assert(portraitScaledSvg.HasValue());
    const std::string portraitScaledRoot = svgOpeningTagWithAttribute(
        portraitScaledSvg.Value(), "svg", "xmlns", "http://www.w3.org/2000/svg");
    const auto portraitScaledContainerPaths =
        svgPathElementsForGroup(portraitScaledSvg.Value(), "container");
    assert(portraitScaledContainerPaths.size() == 1);
    assert(svgAttribute(portraitScaledRoot, "viewBox") ==
           svgAttribute(portraitCanvasRoot, "viewBox"));
    const std::string portraitScaledCanvasState =
        "viewBox=" + svgAttribute(portraitScaledRoot, "viewBox") +
        ";stroke_multiplier=3.25;container_stroke_width=" +
        svgAttribute(portraitScaledContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_portrait_canvas_scaled_state",
        portraitScaledCanvasState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_portrait_canvas_scaled_state",
        portraitScaledCanvasState);

    const auto wideCanvasInput = inputs.find("bpp_svg_wide_canvas");
    assert(wideCanvasInput != inputs.end());
    const auto wideCanvasInstance =
        jaguar::Importer{}.ImportBPInstanceJson(wideCanvasInput->second);
    assert(wideCanvasInstance.HasValue());
    assert(wideCanvasInstance.Value().BinCount() == 1);
    const jaguar::Layout wideCanvasLayout(
        wideCanvasInstance.Value().GetBin(0).container);
    const auto wideCanvasSvg = jaguar::ExportLayoutSvg(wideCanvasLayout);
    assert(wideCanvasSvg.HasValue());
    const std::string wideCanvasRoot = svgOpeningTagWithAttribute(
        wideCanvasSvg.Value(), "svg", "xmlns", "http://www.w3.org/2000/svg");
    const auto wideContainerPaths =
        svgPathElementsForGroup(wideCanvasSvg.Value(), "container");
    assert(wideContainerPaths.size() == 1);
    const std::string wideCanvasState =
        "viewBox=" + svgAttribute(wideCanvasRoot, "viewBox") +
        ";container_stroke_width=" +
        svgAttribute(wideContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_wide_canvas_state",
        wideCanvasState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_wide_canvas_state",
        wideCanvasState);

    const auto obliqueCanvasInput = inputs.find("bpp_svg_oblique_canvas");
    assert(obliqueCanvasInput != inputs.end());
    const auto obliqueCanvasInstance =
        jaguar::Importer{}.ImportBPInstanceJson(obliqueCanvasInput->second);
    assert(obliqueCanvasInstance.HasValue());
    assert(obliqueCanvasInstance.Value().BinCount() == 1);
    const jaguar::Layout obliqueCanvasLayout(
        obliqueCanvasInstance.Value().GetBin(0).container);
    const auto obliqueCanvasSvg = jaguar::ExportLayoutSvg(obliqueCanvasLayout);
    assert(obliqueCanvasSvg.HasValue());
    const std::string obliqueCanvasRoot = svgOpeningTagWithAttribute(
        obliqueCanvasSvg.Value(), "svg", "xmlns", "http://www.w3.org/2000/svg");
    const auto obliqueContainerPaths =
        svgPathElementsForGroup(obliqueCanvasSvg.Value(), "container");
    assert(obliqueContainerPaths.size() == 1);
    const std::string obliqueCanvasState =
        "viewBox=" + svgAttribute(obliqueCanvasRoot, "viewBox") +
        ";container_stroke_width=" +
        svgAttribute(obliqueContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_oblique_canvas_state",
        obliqueCanvasState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_oblique_canvas_state",
        obliqueCanvasState);
    const std::string obliqueContainerPathState =
        "d=" + svgAttribute(obliqueContainerPaths.front(), "d") +
        ";fill=" + svgAttribute(obliqueContainerPaths.front(), "fill") +
        ";stroke=" + svgAttribute(obliqueContainerPaths.front(), "stroke") +
        ";stroke-width=" +
        svgAttribute(obliqueContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_oblique_canvas_path_state",
        obliqueContainerPathState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_oblique_canvas_path_state",
        obliqueContainerPathState);

    const auto concaveCanvasInput = inputs.find("bpp_svg_concave_canvas");
    assert(concaveCanvasInput != inputs.end());
    const auto concaveCanvasInstance =
        jaguar::Importer{}.ImportBPInstanceJson(concaveCanvasInput->second);
    assert(concaveCanvasInstance.HasValue());
    assert(concaveCanvasInstance.Value().BinCount() == 1);
    const jaguar::Layout concaveCanvasLayout(
        concaveCanvasInstance.Value().GetBin(0).container);
    const auto concaveCanvasSvg = jaguar::ExportLayoutSvg(concaveCanvasLayout);
    assert(concaveCanvasSvg.HasValue());
    const auto concaveContainerPaths =
        svgPathElementsForGroup(concaveCanvasSvg.Value(), "container");
    assert(concaveContainerPaths.size() == 1);
    const std::string concaveContainerPathState =
        "d=" + svgAttribute(concaveContainerPaths.front(), "d") +
        ";fill=" + svgAttribute(concaveContainerPaths.front(), "fill") +
        ";stroke=" + svgAttribute(concaveContainerPaths.front(), "stroke") +
        ";stroke-width=" +
        svgAttribute(concaveContainerPaths.front(), "stroke-width");
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_concave_canvas_path_state",
        concaveContainerPathState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_concave_canvas_path_state",
        concaveContainerPathState);

    const auto holeCanvasInput =
        inputs.find("bpp_container_minsep_hole_quality_zone");
    assert(holeCanvasInput != inputs.end());
    const auto holeCanvasInstance =
        jaguar::Importer{}.ImportBPInstanceJson(holeCanvasInput->second);
    assert(holeCanvasInstance.HasValue());
    assert(holeCanvasInstance.Value().BinCount() == 1);
    const jaguar::Layout holeCanvasLayout(
        holeCanvasInstance.Value().GetBin(0).container);
    const auto holeCanvasSvg = jaguar::ExportLayoutSvg(holeCanvasLayout);
    assert(holeCanvasSvg.HasValue());
    const auto holeContainerPaths =
        svgPathElementsForGroup(holeCanvasSvg.Value(), "container");
    assert(!holeContainerPaths.empty());
    std::ostringstream holeContainerPathState;
    for (std::size_t i = 0; i < holeContainerPaths.size(); ++i)
    {
        if (i != 0)
            holeContainerPathState << '|';
        const std::string& path = holeContainerPaths[i];
        holeContainerPathState
            << "d=" << svgAttribute(path, "d")
            << ";fill=" << svgAttribute(path, "fill")
            << ";stroke=" << svgAttribute(path, "stroke")
            << ";stroke-width=" << svgAttribute(path, "stroke-width");
    }
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_hole_canvas_path_state",
        holeContainerPathState.str());
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_hole_canvas_path_state",
        holeContainerPathState.str());
    const std::string holeCanvasQualityZeroState =
        importedBppSvgQualityZeroHolePathStyleState(holeCanvasSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_hole_canvas_quality_zero_state",
        holeCanvasQualityZeroState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_hole_canvas_quality_zero_state",
        holeCanvasQualityZeroState);
    const std::string holeCanvasQualityTwoFiveState =
        importedBppSvgQualityTwoFivePathStyleState(holeCanvasSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_hole_canvas_quality_2_5_state",
        holeCanvasQualityTwoFiveState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_hole_canvas_quality_2_5_state",
        holeCanvasQualityTwoFiveState);

    jaguar::SvgDebugOptions holeCanvasCdOptions;
    holeCanvasCdOptions.drawCdShapes = true;
    const auto holeCanvasCdSvg = jaguar::ExportLayoutSvg(
        holeCanvasLayout, jaguar::SvgExportOptions{}, holeCanvasCdOptions);
    assert(holeCanvasCdSvg.HasValue());
    const auto holeCanvasCdContainerPaths =
        svgPathElementsForGroup(holeCanvasCdSvg.Value(), "container");
    assert(holeCanvasCdContainerPaths.size() == 1);
    const std::string holeCanvasCdContainerPathState =
        importedBppSvgContainerPathStyleState(holeCanvasCdSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_hole_canvas_cd_path_state",
        holeCanvasCdContainerPathState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_hole_canvas_cd_path_state",
        holeCanvasCdContainerPathState);
    const std::string holeCanvasCdQualityZeroState =
        importedBppSvgQualityZeroHolePathStyleState(holeCanvasCdSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_hole_canvas_cd_quality_zero_state",
        holeCanvasCdQualityZeroState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_hole_canvas_cd_quality_zero_state",
        holeCanvasCdQualityZeroState);
    const std::string holeCanvasCdQualityTwoFiveState =
        importedBppSvgQualityTwoFivePathStyleState(holeCanvasCdSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_hole_canvas_cd_quality_2_5_state",
        holeCanvasCdQualityTwoFiveState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_hole_canvas_cd_quality_2_5_state",
        holeCanvasCdQualityTwoFiveState);

    const std::string multiZoneSvgPathState =
        importedBppSvgPathState(multiZoneSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_container_minsep_multi_zone_order_path_state",
        multiZoneSvgPathState);
    expectRustText(
        svgReference,
        "release.svg.bpp_container_minsep_multi_zone_order_path_state",
        multiZoneSvgPathState);
    jaguar::SvgDebugOptions cdSvgDebugOptions;
    cdSvgDebugOptions.drawCdShapes = true;
    const auto multiZoneCdSvg = jaguar::ExportLayoutSvg(
        multiZoneLayout, jaguar::SvgExportOptions{}, cdSvgDebugOptions);
    assert(multiZoneCdSvg.HasValue());
    const std::string multiZoneCdSvgPathState =
        importedBppSvgPathState(multiZoneCdSvg.Value());
    expectRustText(
        svgReference,
        "svg.bpp_container_minsep_multi_zone_order_cd_path_state",
        multiZoneCdSvgPathState);
    const std::string multiZoneCdSvgStyleState =
        importedBppSvgCdStyleState(multiZoneCdSvg.Value());
    expectRustText(
        svgReference,
        "svg.bpp_container_minsep_multi_zone_order_cd_style_state",
        multiZoneCdSvgStyleState);

    jaguar::Layout placedMultiZoneLayout(multiZoneContainer);
    const auto placedMultiZoneItem = placedMultiZoneLayout.PlaceItem(
        multiZoneInstance.Value().GetItem(0),
        jaguar::Transform2d::FromRotationTranslation(0.0, {5.5, 5.0}));
    assert(placedMultiZoneItem.HasValue());
    assert(placedMultiZoneLayout.PlacedItemCount() == 1);
    const auto placedMultiZoneSvg =
        jaguar::ExportLayoutSvg(placedMultiZoneLayout);
    assert(placedMultiZoneSvg.HasValue());
    jaguar::SvgDebugOptions placedCdSvgOptions;
    placedCdSvgOptions.drawCdShapes = true;
    const auto placedMultiZoneCdSvg = jaguar::ExportLayoutSvg(
        placedMultiZoneLayout, jaguar::SvgExportOptions{}, placedCdSvgOptions);
    assert(placedMultiZoneCdSvg.HasValue());
    const std::string placedSvgState =
        "original{" + importedBppSvgPlacedState(placedMultiZoneSvg.Value()) +
        "};cd{" + importedBppSvgPlacedState(placedMultiZoneCdSvg.Value()) + "}";
    expectRustText(
        svgReference,
        "svg.bpp_container_minsep_multi_zone_order_placed_state",
        placedSvgState);

    const auto minQualityInput = inputs.find("bpp_svg_quality_minimum_3");
    assert(minQualityInput != inputs.end());
    const auto minQualityInstance =
        jaguar::Importer{}.ImportBPInstanceJson(minQualityInput->second);
    assert(minQualityInstance.HasValue());
    assert(minQualityInstance.Value().BinCount() == 1);
    const jaguar::Item& minQualityItem = minQualityInstance.Value().GetItem(0);
    assert(minQualityItem.MinQuality().has_value());
    assert(*minQualityItem.MinQuality() == 3);

    jaguar::Layout q2MinQualityLayout(
        minQualityInstance.Value().GetBin(0).container);
    const auto q2MinQualityPlacement = q2MinQualityLayout.PlaceItem(
        minQualityItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {25.0, 5.25}));
    assert(q2MinQualityPlacement.HasValue());
    assert(!q2MinQualityLayout.IsFeasible());
    const auto q2MinQualitySvg =
        jaguar::ExportLayoutSvg(q2MinQualityLayout);
    assert(q2MinQualitySvg.HasValue());

    jaguar::Layout q5UnfilteredLayout(multiZoneContainer);
    const auto q5UnfilteredPlacement = q5UnfilteredLayout.PlaceItem(
        multiZoneInstance.Value().GetItem(0),
        jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 13.25}));
    assert(q5UnfilteredPlacement.HasValue());
    assert(!q5UnfilteredLayout.IsFeasible());
    const auto q5UnfilteredSvg =
        jaguar::ExportLayoutSvg(q5UnfilteredLayout);
    assert(q5UnfilteredSvg.HasValue());

    jaguar::Layout q5MinQualityLayout(
        minQualityInstance.Value().GetBin(0).container);
    const auto q5MinQualityPlacement = q5MinQualityLayout.PlaceItem(
        minQualityItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 13.25}));
    assert(q5MinQualityPlacement.HasValue());
    assert(!q5MinQualityLayout.IsFeasible());
    const auto q5MinQualitySvg = jaguar::ExportLayoutSvg(q5MinQualityLayout);
    assert(q5MinQualitySvg.HasValue());

    const std::string qualityFilterState =
        "q2_min3{" + importedBppSvgQualityFilterState(
            q2MinQualityLayout, q2MinQualitySvg.Value()) +
        "};q5_unfiltered{" + importedBppSvgQualityFilterState(
            q5UnfilteredLayout, q5UnfilteredSvg.Value()) +
        "};q5_min3{" + importedBppSvgQualityFilterState(
            q5MinQualityLayout, q5MinQualitySvg.Value()) + "}";
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_quality_zone_filter_state",
        qualityFilterState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_quality_zone_filter_state",
        qualityFilterState);

    jaguar::Layout exteriorMinQualityLayout(
        minQualityInstance.Value().GetBin(0).container);
    // The positive minsep expands the item collision shape across the
    // contracted left container boundary while the item remains near the bin.
    const auto exteriorMinQualityPlacement = exteriorMinQualityLayout.PlaceItem(
        minQualityItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.5, 15.0}));
    assert(exteriorMinQualityPlacement.HasValue());
    assert(!exteriorMinQualityLayout.IsFeasible());
    const auto exteriorMinQualitySvg =
        jaguar::ExportLayoutSvg(exteriorMinQualityLayout);
    assert(exteriorMinQualitySvg.HasValue());
    const std::string exteriorState = importedBppSvgExteriorState(
        exteriorMinQualityLayout, exteriorMinQualitySvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_exterior_collision_state",
        exteriorState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_exterior_collision_state",
        exteriorState);

    const auto multiHazardInput =
        inputs.find("bpp_svg_multi_hazard_min_quality_3");
    assert(multiHazardInput != inputs.end());
    const auto multiHazardInstance =
        jaguar::Importer{}.ImportBPInstanceJson(multiHazardInput->second);
    assert(multiHazardInstance.HasValue());
    assert(multiHazardInstance.Value().BinCount() == 1);
    const jaguar::Item& multiHazardItem =
        multiHazardInstance.Value().GetItem(0);
    assert(multiHazardItem.MinQuality().has_value());
    assert(*multiHazardItem.MinQuality() == 3);

    jaguar::Layout multiHazardLayout(
        multiHazardInstance.Value().GetBin(0).container);
    const auto multiHazardPlacement = multiHazardLayout.PlaceItem(
        multiHazardItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {12.5, 9.75}));
    assert(multiHazardPlacement.HasValue());
    assert(!multiHazardLayout.IsFeasible());
    const auto multiHazardSvg =
        jaguar::ExportLayoutSvg(multiHazardLayout);
    assert(multiHazardSvg.HasValue());
    const auto multiHazardContainerPaths =
        svgPathElementsForGroup(multiHazardSvg.Value(), "container");
    assert(!multiHazardContainerPaths.empty());
    std::ostringstream multiHazardContainerPathState;
    for (std::size_t i = 0; i < multiHazardContainerPaths.size(); ++i)
    {
        if (i != 0)
            multiHazardContainerPathState << '|';
        const std::string& path = multiHazardContainerPaths[i];
        multiHazardContainerPathState
            << "d=" << svgAttribute(path, "d")
            << ";fill=" << svgAttribute(path, "fill")
            << ";stroke=" << svgAttribute(path, "stroke")
            << ";stroke-width=" << svgAttribute(path, "stroke-width");
    }
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hole_container_path_state",
        multiHazardContainerPathState.str());
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hole_container_path_state",
        multiHazardContainerPathState.str());
    const std::string multiHazardState = importedBppSvgMultiHazardState(
        multiHazardLayout, multiHazardSvg.Value());
    assert(multiHazardState.find("feasible=false;collisions=4") !=
           std::string::npos);
    assert(multiHazardState.find("|M12.5,9.75 L13,5,") != std::string::npos);
    assert(multiHazardState.find("|M12.5,9.75 L20,5.5,") != std::string::npos);
    assert(multiHazardState.find("|M12.5,9.75 L5,5,") != std::string::npos);
    assert(multiHazardState.find("|M12.5,9.75 L9,5,") != std::string::npos);
    assert(multiHazardState.find("5.5,13.5") == std::string::npos);
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hazard_collision_state",
        multiHazardState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hazard_collision_state",
        multiHazardState);
    const std::string multiHazardQualityZeroState =
        importedBppSvgQualityZeroHolePathStyleState(multiHazardSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hole_container_quality_zero_state",
        multiHazardQualityZeroState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hole_container_quality_zero_state",
        multiHazardQualityZeroState);
    const std::string multiHazardQualityTwoFiveState =
        importedBppSvgQualityTwoFivePathStyleState(multiHazardSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hole_container_quality_2_5_state",
        multiHazardQualityTwoFiveState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hole_container_quality_2_5_state",
        multiHazardQualityTwoFiveState);

    jaguar::SvgDebugOptions multiHazardCdOptions;
    multiHazardCdOptions.drawCdShapes = true;
    const auto multiHazardCdSvg = jaguar::ExportLayoutSvg(
        multiHazardLayout, jaguar::SvgExportOptions{}, multiHazardCdOptions);
    assert(multiHazardCdSvg.HasValue());
    const auto multiHazardCdContainerPaths =
        svgPathElementsForGroup(multiHazardCdSvg.Value(), "container");
    assert(multiHazardCdContainerPaths.size() == 1);
    const std::string multiHazardCdContainerPathState =
        importedBppSvgContainerPathStyleState(multiHazardCdSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hole_container_cd_path_state",
        multiHazardCdContainerPathState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hole_container_cd_path_state",
        multiHazardCdContainerPathState);
    const std::string multiHazardCdQualityZeroState =
        importedBppSvgQualityZeroHolePathStyleState(multiHazardCdSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hole_container_cd_quality_zero_state",
        multiHazardCdQualityZeroState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hole_container_cd_quality_zero_state",
        multiHazardCdQualityZeroState);
    const std::string multiHazardCdQualityTwoFiveState =
        importedBppSvgQualityTwoFivePathStyleState(multiHazardCdSvg.Value());
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hole_container_cd_quality_2_5_state",
        multiHazardCdQualityTwoFiveState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hole_container_cd_quality_2_5_state",
        multiHazardCdQualityTwoFiveState);
    const std::string multiHazardCdState = importedBppSvgMultiHazardState(
        multiHazardLayout, multiHazardCdSvg.Value());
    assert(multiHazardCdState == multiHazardState);
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hazard_cd_collision_state",
        multiHazardCdState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hazard_cd_collision_state",
        multiHazardCdState);

    jaguar::SvgExportOptions multiHazardDisabledOptions;
    multiHazardDisabledOptions.highlightCollisions = false;
    const auto multiHazardDisabledSvg = jaguar::ExportLayoutSvg(
        multiHazardLayout, multiHazardDisabledOptions);
    assert(multiHazardDisabledSvg.HasValue());
    assert(!multiHazardLayout.IsFeasible());
    assert(multiHazardDisabledSvg.Value().find(
               "<g id=\"collision_lines\"") == std::string::npos);
    const std::string multiHazardDisabledState =
        importedBppSvgMultiHazardState(
            multiHazardLayout, multiHazardDisabledSvg.Value());
    assert(multiHazardDisabledState == "feasible=false;collisions=0");
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hazard_disabled_collision_state",
        multiHazardDisabledState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hazard_disabled_collision_state",
        multiHazardDisabledState);

    const auto multiHazardOriginalItemPaths =
        svgPathDataForGroup(multiHazardSvg.Value(), "item_1");
    const auto multiHazardCdItemPaths =
        svgPathDataForGroup(multiHazardCdSvg.Value(), "item_1");
    assert(multiHazardOriginalItemPaths.size() == 1);
    assert(multiHazardCdItemPaths.size() == 1);
    assert(multiHazardOriginalItemPaths.front() !=
           multiHazardCdItemPaths.front());

    jaguar::SvgExportOptions multiHazardCdDisabledOptions;
    multiHazardCdDisabledOptions.highlightCollisions = false;
    jaguar::SvgDebugOptions multiHazardCdDisabledDebugOptions;
    multiHazardCdDisabledDebugOptions.drawCdShapes = true;
    const auto multiHazardCdDisabledSvg = jaguar::ExportLayoutSvg(
        multiHazardLayout,
        multiHazardCdDisabledOptions,
        multiHazardCdDisabledDebugOptions);
    assert(multiHazardCdDisabledSvg.HasValue());
    assert(!multiHazardLayout.IsFeasible());
    assert(multiHazardCdDisabledSvg.Value().find(
               "<g id=\"collision_lines\"") == std::string::npos);
    const auto multiHazardCdDisabledItemPaths =
        svgPathDataForGroup(multiHazardCdDisabledSvg.Value(), "item_1");
    assert(multiHazardCdDisabledItemPaths.size() == 1);
    assert(multiHazardCdDisabledItemPaths.front() ==
           multiHazardCdItemPaths.front());
    const std::string multiHazardCdDisabledState =
        importedBppSvgMultiHazardState(
            multiHazardLayout, multiHazardCdDisabledSvg.Value()) +
        ";cd_item=" + multiHazardCdDisabledItemPaths.front();
    assert(multiHazardCdDisabledState.find(
               "feasible=false;collisions=0;cd_item=") == 0);
    expectRustText(
        svgReference,
        "debug.svg.bpp_svg_multi_hazard_cd_disabled_collision_state",
        multiHazardCdDisabledState);
    expectRustText(
        svgReference,
        "release.svg.bpp_svg_multi_hazard_cd_disabled_collision_state",
        multiHazardCdDisabledState);

    const auto multiBinInput = inputs.find("bpp_multi_bin_sorted_zero_stock");
    assert(multiBinInput != inputs.end());
    const auto multiBinInstance =
        jaguar::Importer{}.ImportBPInstanceJson(multiBinInput->second);
    assert(multiBinInstance.HasValue());
    assert(multiBinInstance.Value().BinCount() == 3);
    const std::uint64_t expectedBinIds[] = {10, 20, 30};
    const std::size_t expectedBinStocks[] = {1, 3, 2};
    const std::uint64_t expectedBinCosts[] = {101, 202, 303};
    const double expectedBinAreas[] = {24.0, 48.0, 80.0};
    for (std::size_t i = 0; i < multiBinInstance.Value().BinCount(); ++i)
    {
        const jaguar::Bin& bin = multiBinInstance.Value().GetBin(i);
        assert(bin.index == i);
        assert(bin.externalId == expectedBinIds[i]);
        assert(bin.stock == expectedBinStocks[i]);
        assert(bin.cost == expectedBinCosts[i]);
        assert(bin.container.GetOriginalShape().shape.Area() == expectedBinAreas[i]);
    }
    const auto multiBinCollectionState =
        importedBppBinCollectionState(multiBinInstance.Value());
    expectRustNumbers(
        rustReference,
        "debug.json.bpp_multi_bin_sorted_zero_stock_bin_collection_state",
        multiBinCollectionState);
    expectRustNumbers(
        rustReference,
        "release.json.bpp_multi_bin_sorted_zero_stock_bin_collection_state",
        multiBinCollectionState);

    const auto u64BoundaryInput =
        inputs.find("bpp_multi_bin_u64_boundary_order");
    assert(u64BoundaryInput != inputs.end());
    const auto u64BoundaryInstance =
        jaguar::Importer{}.ImportBPInstanceJson(u64BoundaryInput->second);
    assert(u64BoundaryInstance.HasValue());
    assert(u64BoundaryInstance.Value().BinCount() == 3);
    const std::string expectedU64BinState =
        "0/9007199254740992/1/9007199254740994;"
        "1/9007199254740993/1/9007199254740995;"
        "2/18446744073709551615/18446744073709551615/18446744073709551615";
    const std::string actualU64BinState =
        importedBppBinIntegerState(u64BoundaryInstance.Value());
    assert(actualU64BinState == expectedU64BinState);
    expectRustText(
        rustReference,
        "debug.json.bpp_multi_bin_u64_boundary_order_integer_state",
        expectedU64BinState);
    expectRustText(
        rustReference,
        "release.json.bpp_multi_bin_u64_boundary_order_integer_state",
        expectedU64BinState);
    expectRustText(
        rustReference,
        "debug.json.bpp_multi_bin_u64_boundary_order_integer_state",
        actualU64BinState);
    expectRustText(
        rustReference,
        "release.json.bpp_multi_bin_u64_boundary_order_integer_state",
        actualU64BinState);

    // serde_json accepts finite JSON numbers that overflow while narrowing to
    // f32. The pinned importer rejects infinite min_item_separation values;
    // MSP strip dimensions pass Strip::new's positive check and are rejected
    // here by C++ as a safe parse-stage boundary.
    const auto expectKnownF32OverflowDifference = [&](const JsonCase& testCase,
                                                       bool bpp) {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        bool rejectedAsSerializationFailure = false;
        if (bpp)
        {
            const auto parsed = jaguar::ParseExternalBPInstanceJson(input->second);
            rejectedAsSerializationFailure = !parsed.HasValue() &&
                parsed.GetError().code == jaguar::ErrorCode::SerializationFailure;
        }
        else
        {
            const auto parsed = jaguar::ParseExternalMSPInstanceJson(input->second);
            rejectedAsSerializationFailure = !parsed.HasValue() &&
                parsed.GetError().code == jaguar::ErrorCode::SerializationFailure;
        }
        assert(rejectedAsSerializationFailure);
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName, "true");
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName, "true");
    };
    expectKnownF32OverflowDifference(
        {"bpp_instance_f32_overflow_min_item_separation",
         "json.bpp_instance_f32_overflow_min_item_separation_accepted"},
        true);
    expectKnownF32OverflowDifference(
        {"mspp_instance_f32_overflow_min_item_separation",
         "json.mspp_instance_f32_overflow_min_item_separation_accepted"},
        false);
    expectKnownF32OverflowDifference(
        {"mspp_strip_f32_overflow_height",
         "json.mspp_strip_f32_overflow_height_accepted"},
        false);
    expectKnownF32OverflowDifference(
        {"mspp_strip_f32_overflow_max_width",
         "json.mspp_strip_f32_overflow_max_width_accepted"},
        false);

    const JsonCase bppSolutionCases[] = {
        {"bpp_solution_unknown_field", "json.bpp_solution_unknown_field_accepted"},
        {"bpp_solution_duplicate_unknown_field",
         "json.bpp_solution_duplicate_unknown_field_accepted"},
        {"bpp_solution_duplicate_known_cost",
         "json.bpp_solution_duplicate_known_cost_accepted"},
        {"bpp_solution_duplicate_known_layouts",
         "json.bpp_solution_duplicate_known_layouts_accepted"},
        {"bpp_solution_duplicate_known_density",
         "json.bpp_solution_duplicate_known_density_accepted"},
        {"bpp_solution_duplicate_known_run_time_sec",
         "json.bpp_solution_duplicate_known_run_time_sec_accepted"},
    };
    for (const JsonCase& testCase : bppSolutionCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        const auto parsed = jaguar::ParseExternalBPSolutionJson(input->second);
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }

    const JsonCase msppSolutionCases[] = {
        {"mspp_solution_unknown_field", "json.mspp_solution_unknown_field_accepted"},
        {"mspp_solution_duplicate_unknown_field",
         "json.mspp_solution_duplicate_unknown_field_accepted"},
        {"mspp_solution_duplicate_known_layouts",
         "json.mspp_solution_duplicate_known_layouts_accepted"},
        {"mspp_solution_duplicate_known_containers",
         "json.mspp_solution_duplicate_known_containers_accepted"},
        {"mspp_solution_duplicate_known_density",
         "json.mspp_solution_duplicate_known_density_accepted"},
        {"mspp_solution_duplicate_known_run_time_sec",
         "json.mspp_solution_duplicate_known_run_time_sec_accepted"},
    };
    for (const JsonCase& testCase : msppSolutionCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        const auto parsed = jaguar::ParseExternalMSPSolutionJson(input->second);
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }

    struct ItemSerializationCase
    {
        const char* inputName;
        const char* outputName;
        const char* serializedField;
    };
    const ItemSerializationCase itemCases[] = {
        {"item_min_quality_missing",
         "json.item_missing_min_quality_emits_null",
         "\"min_quality\":null"},
        {"item_min_quality_null",
         "json.item_null_min_quality_emits_null",
         "\"min_quality\":null"},
        {"item_min_quality_some",
         "json.item_present_min_quality_emits_value",
         "\"min_quality\":2"},
    };
    for (const ItemSerializationCase& testCase : itemCases)
    {
        const auto input = inputs.find(testCase.inputName);
        assert(input != inputs.end());
        const auto item = jaguar::ParseExternalItemJson(input->second);
        assert(item.HasValue());
        const auto serialized =
            jaguar::SerializeExternalItemJson(item.Value());
        assert(serialized.HasValue());
        const std::string fieldMatches =
            serialized.Value().find(testCase.serializedField) != std::string::npos
                ? "true"
                : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       fieldMatches);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       fieldMatches);
    }

    const auto compareSerializedJson =
        [&](const char* outputName, const std::string& inputJson, auto parse,
            auto serialize, std::initializer_list<const char*> requiredFields) {
            const auto input = parse(inputJson);
            assert(input.HasValue());
            const auto actualJson = serialize(input.Value());
            assert(actualJson.HasValue());
            const auto actualParsed = parse(actualJson.Value());
            assert(actualParsed.HasValue());
            const auto actualCanonical = serialize(actualParsed.Value());
            assert(actualCanonical.HasValue());

            for (const char* profile : {"debug", "release"})
            {
                const auto expected = rustReference.find(
                    std::string(profile) + ".json.serialize." + outputName);
                assert(expected != rustReference.end());
                const auto expectedParsed = parse(expected->second);
                assert(expectedParsed.HasValue());
                const auto expectedCanonical = serialize(expectedParsed.Value());
                assert(expectedCanonical.HasValue());
                assert(actualCanonical.Value() == expectedCanonical.Value());

                for (const char* field : requiredFields)
                {
                    const std::string token = std::string("\"") + field + "\":";
                    assert(actualJson.Value().find(token) != std::string::npos);
                    assert(expected->second.find(token) != std::string::npos);
                }
            }
        };

    compareSerializedJson(
        "bpp_instance", inputs.at("bpp_instance_serialization"),
        [](const std::string& json) {
            return jaguar::ParseExternalBPInstanceJson(json);
        },
        [](const jaguar::ExternalBPInstance& instance) {
            return jaguar::SerializeExternalBPInstanceJson(instance);
        },
        {"name", "min_item_separation", "items", "id", "demand",
         "orientation", "rotation", "mode", "angles", "shape", "type",
         "data", "x_min", "y_min", "width", "height", "min_quality",
         "bins", "zones", "quality", "stock", "cost"});
    compareSerializedJson(
        "bpp_u64_boundary_instance",
        inputs.at("bpp_multi_bin_u64_boundary_order"),
        [](const std::string& json) {
            return jaguar::ParseExternalBPInstanceJson(json);
        },
        [](const jaguar::ExternalBPInstance& instance) {
            return jaguar::SerializeExternalBPInstanceJson(instance);
        },
        {"name", "min_item_separation", "items", "id", "demand",
         "orientation", "rotation", "mode", "shape", "type", "data",
         "x_min", "y_min", "width", "height", "min_quality", "bins",
         "stock", "cost"});
    const auto u64SerializationInput =
        inputs.find("bpp_multi_bin_u64_boundary_order");
    assert(u64SerializationInput != inputs.end());
    const auto u64External =
        jaguar::ParseExternalBPInstanceJson(u64SerializationInput->second);
    assert(u64External.HasValue());
    const auto u64Serialized =
        jaguar::SerializeExternalBPInstanceJson(u64External.Value());
    assert(u64Serialized.HasValue());
    for (const char* token : {
             "\"id\":18446744073709551615",
             "\"stock\":18446744073709551615",
             "\"cost\":18446744073709551615",
             "\"id\":9007199254740993",
             "\"id\":9007199254740992"})
    {
        assert(u64Serialized.Value().find(token) != std::string::npos);
    }
    const auto u64Reserialized =
        jaguar::ParseExternalBPInstanceJson(u64Serialized.Value());
    assert(u64Reserialized.HasValue());
    const std::string expectedSerializedBinIntegerState =
        "18446744073709551615/18446744073709551615/18446744073709551615;"
        "9007199254740993/1/9007199254740995;"
        "9007199254740992/1/9007199254740994";
    const std::string actualSerializedBinIntegerState =
        externalBppBinIntegerState(u64Reserialized.Value());
    assert(actualSerializedBinIntegerState == expectedSerializedBinIntegerState);
    expectRustText(
        rustReference,
        "debug.json.serialize.bpp_u64_boundary_bin_integer_state",
        expectedSerializedBinIntegerState);
    expectRustText(
        rustReference,
        "release.json.serialize.bpp_u64_boundary_bin_integer_state",
        expectedSerializedBinIntegerState);
    compareSerializedJson(
        "mspp_instance", inputs.at("mspp_instance_serialization"),
        [](const std::string& json) {
            return jaguar::ParseExternalMSPInstanceJson(json);
        },
        [](const jaguar::ExternalMSPInstance& instance) {
            return jaguar::SerializeExternalMSPInstanceJson(instance);
        },
        {"name", "min_item_separation", "items", "id", "demand",
         "orientation", "rotation", "mode", "angles", "shape", "type",
         "data", "x_min", "y_min", "width", "height", "min_quality",
         "strips", "max_width"});
    compareSerializedJson(
        "bpp_solution", inputs.at("bpp_solution_serialization"),
        [](const std::string& json) {
            return jaguar::ParseExternalBPSolutionJson(json);
        },
        [](const jaguar::ExternalBPSolution& solution) {
            return jaguar::SerializeExternalBPSolutionJson(solution);
        },
        {"cost", "layouts", "container_id", "placed_items", "item_id",
         "transformation", "rotation", "translation", "density",
         "run_time_sec"});
    compareSerializedJson(
        "mspp_solution", inputs.at("mspp_solution_serialization"),
        [](const std::string& json) {
            return jaguar::ParseExternalMSPSolutionJson(json);
        },
        [](const jaguar::ExternalMSPSolution& solution) {
            return jaguar::SerializeExternalMSPSolutionJson(solution);
        },
        {"layouts", "container_id", "placed_items", "item_id",
         "transformation", "rotation", "translation", "density",
         "containers", "id", "shape", "type", "data", "x_min", "y_min",
         "width", "height", "run_time_sec"});

    const JsonCase bppFlattenedItemCases[] = {
        {"bpp_item_duplicate_known_orientation",
         "json.bpp_item_duplicate_known_orientation_accepted"},
        {"bpp_item_duplicate_known_shape",
         "json.bpp_item_duplicate_known_shape_accepted"},
        {"bpp_item_duplicate_known_min_quality",
         "json.bpp_item_duplicate_known_min_quality_accepted"},
    };
    for (const JsonCase& testCase : bppFlattenedItemCases)
    {
        const auto parsed =
            jaguar::ParseExternalBPInstanceJson(inputs.at(testCase.inputName));
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }
    const JsonCase msppFlattenedItemCases[] = {
        {"mspp_item_duplicate_known_orientation",
         "json.mspp_item_duplicate_known_orientation_accepted"},
        {"mspp_item_duplicate_known_shape",
         "json.mspp_item_duplicate_known_shape_accepted"},
        {"mspp_item_duplicate_known_min_quality",
         "json.mspp_item_duplicate_known_min_quality_accepted"},
    };
    for (const JsonCase& testCase : msppFlattenedItemCases)
    {
        const auto parsed =
            jaguar::ParseExternalMSPInstanceJson(inputs.at(testCase.inputName));
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        expectRustText(rustReference,
                       std::string("debug.") + testCase.outputName,
                       accepted);
        expectRustText(rustReference,
                       std::string("release.") + testCase.outputName,
                       accepted);
    }
}

jaguar::Item ImportItem(const jaguar::ExternalDemandItem& external,
                        jaguar::ItemIndex index,
                        const jaguar::Importer& importer)
{
    const auto item = importer.ImportItem(external.item, index);
    assert(item.HasValue());
    return item.Value();
}

jaguar::Container ImportContainer(const jaguar::ExternalContainer& external,
                                   const jaguar::Importer& importer)
{
    const auto container = importer.ImportContainer(external);
    assert(container.HasValue());
    return container.Value();
}
}

void runProblemDifferentialTests()
{
    const auto problemReference = loadProblemReference();
    const auto solutionImportReference =
        loadReferenceMap("solution_import_b8.txt");
    const auto solutionImportInputs =
        loadReferenceMap("solution_import_b8_inputs.txt");
    const auto jsonMetadataInputs =
        loadReferenceMap("json_metadata_b8_inputs.txt");
    compareJsonMetadataB8();
    const jaguar::Importer importer;
    jaguar::CDEConfig cdeConfig{};
    jaguar::ShapeModifyConfig shapeConfig{};

    const auto sppJson = loadReference("spp_instance.json");
    assert(!sppJson.empty());
    const auto sppExternal = jaguar::ParseExternalSPInstanceJson(sppJson);
    assert(sppExternal.HasValue());
    std::vector<jaguar::Item> sppItems;
    std::vector<std::size_t> sppDemands;
    for (std::size_t i = 0; i < sppExternal.Value().items.size(); ++i)
    {
        const auto& externalItem = sppExternal.Value().items[i];
        if (externalItem.demand == 0)
            continue;
        sppItems.push_back(ImportItem(externalItem, sppItems.size(), importer));
        sppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    const auto sppInstance = jaguar::SPInstance::Create(
        std::move(sppItems), std::move(sppDemands),
        sppExternal.Value().stripHeight, cdeConfig, shapeConfig);
    assert(sppInstance.HasValue());
    struct SppStripWidthCase
    {
        const char* name;
        double width;
    };
    const SppStripWidthCase invalidSppStripWidths[] = {
        {"zero", 0.0},
        {"negative", -1.0},
        {"negative_infinity", -std::numeric_limits<double>::infinity()},
        {"nan", std::numeric_limits<double>::quiet_NaN()},
    };
    for (const SppStripWidthCase& testCase : invalidSppStripWidths)
    {
        jaguar::ExternalSPSolution externalSolution{};
        externalSolution.stripWidth = testCase.width;
        const auto importedSolution =
            importer.ImportSPSolution(sppInstance.Value(), externalSolution);
        assert(!importedSolution.HasValue());
        assert(importedSolution.GetError().code == jaguar::ErrorCode::InvalidInput);
        const std::string rustError =
            std::string("err:") + importedSolution.GetError().message;
        const std::string outputKey =
            std::string("import.spp_strip_width.") + testCase.name;
        expectRustText(solutionImportReference,
                       std::string("debug.") + outputKey,
                       rustError);
        expectRustText(solutionImportReference,
                       std::string("release.") + outputKey,
                       rustError);
    }
    jaguar::ExternalSPSolution invalidWidthBeforeUnknownItem{};
    invalidWidthBeforeUnknownItem.stripWidth = 0.0;
    jaguar::ExternalPlacedItem unknownItem{};
    unknownItem.itemId = std::numeric_limits<std::uint64_t>::max();
    invalidWidthBeforeUnknownItem.layout.placedItems.push_back(unknownItem);
    const auto invalidWidthPrecedence = importer.ImportSPSolution(
        sppInstance.Value(), invalidWidthBeforeUnknownItem);
    assert(!invalidWidthPrecedence.HasValue());
    assert(invalidWidthPrecedence.GetError().code ==
           jaguar::ErrorCode::InvalidInput);
    expectRustText(solutionImportReference,
                   "debug.import.spp_invalid_width_before_unknown_item",
                   std::string("err:") + invalidWidthPrecedence.GetError().message);
    expectRustText(solutionImportReference,
                   "release.import.spp_invalid_width_before_unknown_item",
                   std::string("err:") + invalidWidthPrecedence.GetError().message);
    jaguar::ExternalSPSolution infiniteSppStripWidth{};
    infiniteSppStripWidth.stripWidth = std::numeric_limits<double>::infinity();
    const auto rejectedInfiniteSppStripWidth = importer.ImportSPSolution(
        sppInstance.Value(), infiniteSppStripWidth);
    assert(!rejectedInfiniteSppStripWidth.HasValue());
    assert(rejectedInfiniteSppStripWidth.GetError().code ==
           jaguar::ErrorCode::SnapshotMismatch);
    expectRustText(solutionImportReference,
                   "debug.import.spp_strip_width.positive_infinity_panicked",
                   "true");
    expectRustText(solutionImportReference,
                   "release.import.spp_strip_width.positive_infinity_panicked",
                   "true");
    const auto zeroWidthJson = solutionImportInputs.find(
        "spp_width_zero_unknown_item");
    assert(zeroWidthJson != solutionImportInputs.end());
    const auto parsedZeroWidthJson =
        jaguar::ParseExternalSPSolutionJson(zeroWidthJson->second);
    assert(parsedZeroWidthJson.HasValue());
    const auto importedZeroWidthJson = importer.ImportSPSolutionJson(
        sppInstance.Value(), zeroWidthJson->second);
    assert(!importedZeroWidthJson.HasValue());
    assert(importedZeroWidthJson.GetError().code == jaguar::ErrorCode::InvalidInput);
    const std::string zeroWidthJsonError =
        std::string("err:") + importedZeroWidthJson.GetError().message;
    expectRustText(solutionImportReference,
                   "debug.json.import.spp_width.zero_unknown_item",
                   zeroWidthJsonError);
    expectRustText(solutionImportReference,
                   "release.json.import.spp_width.zero_unknown_item",
                   zeroWidthJsonError);

    const auto overflowWidthJson = solutionImportInputs.find(
        "spp_width_f32_overflow");
    assert(overflowWidthJson != solutionImportInputs.end());
    const auto parsedOverflowWidthJson =
        jaguar::ParseExternalSPSolutionJson(overflowWidthJson->second);
    assert(!parsedOverflowWidthJson.HasValue());
    assert(parsedOverflowWidthJson.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    assert(parsedOverflowWidthJson.GetError().message ==
           "strip_width must be finite");
    const auto importedOverflowWidthJson = importer.ImportSPSolutionJson(
        sppInstance.Value(), overflowWidthJson->second);
    assert(!importedOverflowWidthJson.HasValue());
    assert(importedOverflowWidthJson.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    expectRustText(solutionImportReference,
                   "debug.json.import.spp_width.f32_overflow.deserialized_inf",
                   "true");
    expectRustText(solutionImportReference,
                   "release.json.import.spp_width.f32_overflow.deserialized_inf",
                   "true");
    expectRustText(solutionImportReference,
                   "debug.json.import.spp_width.f32_overflow",
                   "panic");
    expectRustText(solutionImportReference,
                   "release.json.import.spp_width.f32_overflow",
                   "panic");
    struct SolutionJsonFieldCase
    {
        const char* inputName;
        const char* outputName;
    };
    const SolutionJsonFieldCase solutionJsonFieldCases[] = {
        {"spp_solution_unknown_top_level", "unknown_top_level"},
        {"spp_solution_duplicate_unknown_top_level",
         "duplicate_unknown_top_level"},
        {"spp_solution_duplicate_known_strip_width",
         "duplicate_known_strip_width"},
        {"spp_layout_unknown_field", "layout_unknown_field"},
        {"spp_layout_duplicate_unknown_field",
         "layout_duplicate_unknown_field"},
        {"spp_layout_duplicate_known_placed_items",
         "layout_duplicate_known_placed_items"},
        {"spp_placed_item_unknown_field", "placed_item_unknown_field"},
        {"spp_placed_item_duplicate_unknown_field",
         "placed_item_duplicate_unknown_field"},
        {"spp_placed_item_duplicate_known_item_id",
         "placed_item_duplicate_known_item_id"},
        {"spp_placed_item_duplicate_known_transformation",
         "placed_item_duplicate_known_transformation"},
        {"spp_transformation_unknown_field", "transformation_unknown_field"},
        {"spp_transformation_duplicate_unknown_field",
         "transformation_duplicate_unknown_field"},
        {"spp_transformation_duplicate_known_rotation",
         "transformation_duplicate_known_rotation"},
        {"spp_transformation_duplicate_known_translation",
         "transformation_duplicate_known_translation"},
        {"spp_solution_duplicate_known_layout",
         "solution_duplicate_known_layout"},
        {"spp_solution_duplicate_known_density",
         "solution_duplicate_known_density"},
        {"spp_solution_duplicate_known_run_time_sec",
         "solution_duplicate_known_run_time_sec"},
        {"spp_layout_duplicate_known_container_id",
         "layout_duplicate_known_container_id"},
        {"spp_layout_duplicate_known_density",
         "layout_duplicate_known_density"},
    };
    for (const SolutionJsonFieldCase& testCase : solutionJsonFieldCases)
    {
        const auto input = solutionImportInputs.find(testCase.inputName);
        assert(input != solutionImportInputs.end());
        const auto parsed = jaguar::ParseExternalSPSolutionJson(input->second);
        const std::string accepted = parsed.HasValue() ? "true" : "false";
        const std::string referenceKey =
            std::string("json.spp_solution.") + testCase.outputName + "_accepted";
        expectRustText(solutionImportReference,
                       std::string("debug.") + referenceKey,
                       accepted);
        expectRustText(solutionImportReference,
                       std::string("release.") + referenceKey,
                       accepted);
    }
    expectRustNumbers(problemReference, "spp.instance",
                      {static_cast<double>(sppInstance.Value().TotalDemand()),
                       sppInstance.Value().TotalItemArea(),
                       sppInstance.Value().BaseStrip().fixedHeight,
                       sppInstance.Value().BaseStrip().width});
    auto sppProblem = jaguar::SPProblem::Create(sppInstance.Value());
    assert(sppProblem.HasValue());
    assert(sppProblem.Value().RemainingDemand(0) == 2);
    assert(sppProblem.Value().ChangeStripWidth(5.0).HasValue());
    expectRustNumbers(problemReference, "spp.changed", {sppProblem.Value().StripWidth()});
    const auto sppPlaced = sppProblem.Value().PlaceItem({
        0, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(sppPlaced.HasValue());
    expectRustNumbers(problemReference, "spp.placed",
                      {static_cast<double>(sppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(sppProblem.Value().PlacedItemCount()),
                       sppProblem.Value().Density(),
                       sppProblem.Value().StripWidth()});
    const auto sppSolution = sppProblem.Value().Save();
    assert(jaguar::ValidateSPSolution(sppProblem.Value(), sppSolution).HasValue());
    expectRustNumbers(problemReference, "spp.solution",
                      {static_cast<double>(sppSolution.layoutSnapshot.placements.size()),
                       sppSolution.Density(), sppSolution.StripWidth()});
    assert(sppProblem.Value().RemoveItem(sppPlaced.Value()).HasValue());
    expectRustNumbers(problemReference, "spp.removed",
                      {static_cast<double>(sppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(sppProblem.Value().PlacedItemCount()),
                       sppProblem.Value().Density()});
    assert(sppProblem.Value().Restore(sppSolution).HasValue());
    expectRustNumbers(problemReference, "spp.restored",
                      {static_cast<double>(sppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(sppProblem.Value().PlacedItemCount()),
                       sppProblem.Value().Density(),
                       sppProblem.Value().StripWidth()});

    // Paired case: SPP import with min_item_separation > 0. Locks the initial
    // strip width formula separation + total_area / (height - separation)
    // (jagua-rs spp/io/import.rs), the raw width bits, and the
    // "separation leaves no usable strip height" error exit.
    {
        jaguar::ExternalSPInstance sepExternal = sppExternal.Value();
        sepExternal.minItemSeparation = 0.5;
        const auto sepInstance = importer.ImportSPInstance(sepExternal);
        assert(sepInstance.HasValue());
        expectRustNumbers(problemReference, "spp_sep.instance",
                          {static_cast<double>(sepInstance.Value().TotalDemand()),
                           sepInstance.Value().TotalItemArea(),
                           sepInstance.Value().BaseStrip().fixedHeight,
                           sepInstance.Value().BaseStrip().width});
        const float sepWidth = static_cast<float>(sepInstance.Value().BaseStrip().width);
        std::uint32_t sepWidthBits = 0U;
        std::memcpy(&sepWidthBits, &sepWidth, sizeof(sepWidthBits));
        char sepWidthBitsText[16];
        std::snprintf(sepWidthBitsText, sizeof(sepWidthBitsText), "%08x", sepWidthBits);
        expectRustText(problemReference, "spp_sep.width_bits", sepWidthBitsText);

        jaguar::ExternalSPInstance tooMuchExternal = sppExternal.Value();
        tooMuchExternal.minItemSeparation = 4.0;
        const auto tooMuchResult = importer.ImportSPInstance(tooMuchExternal);
        assert(!tooMuchResult.HasValue());
        expectRustText(problemReference, "spp_sep.too_much_error",
                       tooMuchResult.GetError().message.c_str());
    }

    const auto bppJson = loadReference("bpp_instance.json");
    assert(!bppJson.empty());
    const auto bppExternal = jaguar::ParseExternalBPInstanceJson(bppJson);
    assert(bppExternal.HasValue());
    std::vector<jaguar::Item> bppItems;
    std::vector<std::size_t> bppDemands;
    for (std::size_t i = 0; i < bppExternal.Value().items.size(); ++i)
    {
        const auto& externalItem = bppExternal.Value().items[i];
        if (externalItem.demand == 0)
            continue;
        bppItems.push_back(ImportItem(externalItem, bppItems.size(), importer));
        bppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    std::vector<jaguar::Bin> bins;
    for (std::size_t i = 0; i < bppExternal.Value().bins.size(); ++i)
    {
        const auto& externalBin = bppExternal.Value().bins[i];
        if (externalBin.stock == 0)
            continue;
        bins.push_back({i, externalBin.container.id,
                        ImportContainer(externalBin.container, importer),
                        static_cast<std::size_t>(externalBin.stock),
                        externalBin.cost});
    }
    const auto bppInstance = jaguar::BPInstance::Create(
        std::move(bppItems), std::move(bppDemands), std::move(bins));
    assert(bppInstance.HasValue());
    expectRustNumbers(problemReference, "bpp.instance",
                      {static_cast<double>(bppInstance.Value().TotalDemand()),
                       bppInstance.Value().TotalItemArea(),
                       static_cast<double>(bppInstance.Value().BinCount()),
                       static_cast<double>(bppInstance.Value().GetBin(0).stock),
                       static_cast<double>(bppInstance.Value().GetBin(0).cost)});
    auto bppProblem = jaguar::BPProblem::Create(bppInstance.Value());
    assert(bppProblem.HasValue());
    const auto bppFirstPlaced = bppProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Closed, 0, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(bppFirstPlaced.HasValue());
    expectRustNumbers(problemReference, "bpp.closed",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});
    const auto bppSecondPlaced = bppProblem.Value().PlaceItem({
        jaguar::LayoutTargetKind::Open, bppFirstPlaced.Value().layoutIndex, 0, 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {3.5, 1.0})});
    assert(bppSecondPlaced.HasValue());
    expectRustNumbers(problemReference, "bpp.open",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});
    const auto bppSolution = bppProblem.Value().Save();
    assert(jaguar::ValidateBPSolution(bppProblem.Value(), bppSolution).HasValue());
    expectRustNumbers(problemReference, "bpp.solution",
                      {static_cast<double>(bppSolution.layouts.size()),
                       static_cast<double>(bppSolution.layouts[0].layoutSnapshot.placements.size()),
                       static_cast<double>(bppSolution.Cost(bppInstance.Value())),
                       bppSolution.Density()});
    const auto bppExternalSolution = jaguar::ExternalBPSolutionFromSolution(
        bppInstance.Value(), bppSolution);
    assert(bppExternalSolution.HasValue());
    const auto bppSolutionImport = importer.ImportBPSolution(
        bppInstance.Value(), bppExternalSolution.Value());
    const auto bppExternalSolutionJson =
        jaguar::SerializeExternalBPSolutionJson(bppExternalSolution.Value());
    assert(bppExternalSolutionJson.HasValue());
    const auto bppJsonSolutionImport = importer.ImportBPSolutionJson(
        bppInstance.Value(), bppExternalSolutionJson.Value());
    const auto bppMalformedJsonSolutionImport = importer.ImportBPSolutionJson(
        bppInstance.Value(),
        jsonMetadataInputs.at("bpp_solution_duplicate_known_cost"));
    const auto bppValidJsonSolutionImport = importer.ImportBPSolutionJson(
        bppInstance.Value(), jsonMetadataInputs.at("bpp_solution_unknown_field"));
    assert(!bppSolutionImport.HasValue());
    assert(!bppJsonSolutionImport.HasValue());
    assert(bppSolutionImport.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    assert(bppJsonSolutionImport.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    assert(!bppMalformedJsonSolutionImport.HasValue());
    assert(bppMalformedJsonSolutionImport.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    assert(!bppValidJsonSolutionImport.HasValue());
    assert(bppValidJsonSolutionImport.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    assert(bppSolutionImport.GetError().message ==
           "BPP solution import is not implemented in the reference crate");
    assert(bppJsonSolutionImport.GetError().message ==
           "BPP solution import is not implemented in the reference crate");
    const char* bppImportUnavailable =
        !bppSolutionImport.HasValue() && !bppJsonSolutionImport.HasValue()
            ? "true"
            : "false";
    expectRustText(solutionImportReference,
                   "debug.import.bpp_solution_unimplemented",
                   bppImportUnavailable);
    expectRustText(solutionImportReference,
                   "release.import.bpp_solution_unimplemented",
                   bppImportUnavailable);
    assert(bppProblem.Value().RemoveItem(bppSecondPlaced.Value()).HasValue());
    expectRustNumbers(problemReference, "bpp.removed",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});
    const auto bppRestored = bppProblem.Value().Restore(bppSolution);
    assert(bppRestored.HasValue());
    expectRustText(problemReference, "bpp.restore_rekey",
                   bppRestored.Value() ? "true" : "false");
    expectRustNumbers(problemReference, "bpp.restored",
                      {static_cast<double>(bppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(bppProblem.Value().RemainingBinStock(0)),
                       static_cast<double>(bppProblem.Value().OpenLayoutCount()),
                       static_cast<double>(bppProblem.Value().PlacedItemCount()),
                       static_cast<double>(bppProblem.Value().BinCost()),
                       bppProblem.Value().Density()});

    const auto msppJson = loadReference("mspp_instance.json");
    assert(!msppJson.empty());
    const auto msppExternal = jaguar::ParseExternalMSPInstanceJson(msppJson);
    assert(msppExternal.HasValue());
    std::vector<jaguar::Item> msppItems;
    std::vector<std::size_t> msppDemands;
    for (std::size_t i = 0; i < msppExternal.Value().items.size(); ++i)
    {
        const auto& externalItem = msppExternal.Value().items[i];
        if (externalItem.demand == 0)
            continue;
        msppItems.push_back(ImportItem(externalItem, msppItems.size(), importer));
        msppDemands.push_back(static_cast<std::size_t>(externalItem.demand));
    }
    const auto msppInstance = jaguar::MSPInstance::Create(
        std::move(msppItems), std::move(msppDemands),
        msppExternal.Value().strips.maxWidth,
        msppExternal.Value().strips.height, cdeConfig, shapeConfig);
    assert(msppInstance.HasValue());
    expectRustNumbers(problemReference, "mspp.instance",
                      {static_cast<double>(msppInstance.Value().TotalDemand()),
                       msppInstance.Value().TotalItemArea(),
                       msppInstance.Value().BaseStrip().maxWidth,
                       msppInstance.Value().BaseStrip().fixedHeight,
                       msppInstance.Value().BaseStrip().width});
    auto msppProblem = jaguar::MSPProblem::Create(msppInstance.Value());
    assert(msppProblem.HasValue());
    assert(msppProblem.Value().RemainingDemand(0) == 1);
    const auto msppLayout = msppProblem.Value().AddLayout(
        msppProblem.Value().Instance().BaseStrip());
    assert(msppLayout.HasValue());
    expectRustNumbers(problemReference, "mspp.layout",
                      {static_cast<double>(msppProblem.Value().LayoutCount()),
                       msppProblem.Value().StripWidth(msppLayout.Value()),
                       msppProblem.Value().TotalStripWidth()});
    const auto msppPlaced = msppProblem.Value().PlaceItem({
        msppLayout.Value(), 0,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0})});
    assert(msppPlaced.HasValue());
    expectRustNumbers(problemReference, "mspp.placed",
                      {static_cast<double>(msppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(msppProblem.Value().PlacedItemCount()),
                       msppProblem.Value().Density(),
                       msppProblem.Value().TotalStripWidth()});
    assert(msppProblem.Value().ChangeStripWidth(msppLayout.Value(), 8.0).HasValue());
    expectRustNumbers(problemReference, "mspp.changed",
                      {msppProblem.Value().StripWidth(msppLayout.Value()),
                       msppProblem.Value().TotalStripWidth(),
                       msppProblem.Value().Density()});
    assert(msppProblem.Value().FitStrip(msppLayout.Value()).HasValue());
    const auto msppSolution = msppProblem.Value().Save();
    assert(jaguar::ValidateMSPSolution(msppProblem.Value(), msppSolution).HasValue());
    expectRustNumbers(problemReference, "mspp.solution",
                      {static_cast<double>(msppSolution.layouts.size()),
                       msppSolution.TotalStripWidth(), msppSolution.Density()});
    const auto msppExternalSolution = jaguar::ExternalMSPSolutionFromSolution(
        msppInstance.Value(), msppSolution);
    assert(msppExternalSolution.HasValue());
    const auto msppSolutionImport = importer.ImportMSPSolution(
        msppInstance.Value(), msppExternalSolution.Value());
    const auto msppExternalSolutionJson =
        jaguar::SerializeExternalMSPSolutionJson(msppExternalSolution.Value());
    assert(msppExternalSolutionJson.HasValue());
    const auto msppJsonSolutionImport = importer.ImportMSPSolutionJson(
        msppInstance.Value(), msppExternalSolutionJson.Value());
    const auto msppMalformedJsonSolutionImport = importer.ImportMSPSolutionJson(
        msppInstance.Value(),
        jsonMetadataInputs.at("mspp_solution_duplicate_known_containers"));
    const auto msppValidJsonSolutionImport = importer.ImportMSPSolutionJson(
        msppInstance.Value(), jsonMetadataInputs.at("mspp_solution_unknown_field"));
    assert(!msppSolutionImport.HasValue());
    assert(!msppJsonSolutionImport.HasValue());
    assert(msppSolutionImport.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    assert(msppJsonSolutionImport.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    assert(!msppMalformedJsonSolutionImport.HasValue());
    assert(msppMalformedJsonSolutionImport.GetError().code ==
           jaguar::ErrorCode::SerializationFailure);
    assert(!msppValidJsonSolutionImport.HasValue());
    assert(msppValidJsonSolutionImport.GetError().code ==
           jaguar::ErrorCode::UnsupportedOperation);
    assert(msppSolutionImport.GetError().message ==
           "MSPP solution import is not implemented in the reference crate");
    assert(msppJsonSolutionImport.GetError().message ==
           "MSPP solution import is not implemented in the reference crate");
    const char* msppImportUnavailable =
        !msppSolutionImport.HasValue() && !msppJsonSolutionImport.HasValue()
            ? "true"
            : "false";
    expectRustText(solutionImportReference,
                   "debug.import.mspp_solution_unimplemented",
                   msppImportUnavailable);
    expectRustText(solutionImportReference,
                   "release.import.mspp_solution_unimplemented",
                   msppImportUnavailable);
    assert(msppProblem.Value().RemoveItem(msppPlaced.Value()).HasValue());
    expectRustNumbers(problemReference, "mspp.removed",
                      {static_cast<double>(msppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(msppProblem.Value().PlacedItemCount()),
                       msppProblem.Value().TotalStripWidth(),
                       msppProblem.Value().Density()});
    const auto msppRestored = msppProblem.Value().Restore(msppSolution);
    assert(msppRestored.HasValue());
    expectRustText(problemReference, "mspp.restore_rekey",
                   msppRestored.Value() ? "true" : "false");
    expectRustNumbers(problemReference, "mspp.restored",
                      {static_cast<double>(msppProblem.Value().RemainingDemand(0)),
                       static_cast<double>(msppProblem.Value().PlacedItemCount()),
                       msppProblem.Value().StripWidth(msppLayout.Value()),
                       msppProblem.Value().TotalStripWidth(),
                       msppProblem.Value().Density()});
}
