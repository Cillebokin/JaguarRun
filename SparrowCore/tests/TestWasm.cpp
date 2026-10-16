#include "SparrowWasm.h"

#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }
    output << "WASM TEST FAILURE: " << message << '\n';
    return false;
}

std::string FindFixture()
{
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/LbfSppSmall.json",
             "../SparrowCore/Tests/Fixtures/LbfSppSmall.json",
             "../JaguarRun/SparrowCore/Tests/Fixtures/LbfSppSmall.json",
             "..\\SparrowCore\\Tests\\Fixtures\\LbfSppSmall.json"})
    {
        std::ifstream input(path, std::ios::binary);
        if (input.is_open())
        {
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>());
        }
    }
    return {};
}
}

bool RunWasmTests(std::ostream& output)
{
    const std::string input = FindFixture();
    if (!Expect(!input.empty(), output, "WASM fixture must be readable"))
    {
        return false;
    }

    std::vector<char> json(65536U);
    const int jsonResult = SparrowOptimizeJson(
        input.data(), input.size(), json.data(), json.size());
    if (!Expect(jsonResult == 0
                    && std::string(json.data()).find("\"layout\"")
                        != std::string::npos,
                output,
                "JSON C ABI must optimize and serialize a solution"))
    {
        return false;
    }

    std::vector<char> svg(65536U);
    const int svgResult = SparrowOptimizeSvg(
        input.data(), input.size(), svg.data(), svg.size());
    if (!Expect(svgResult == 0
                    && std::string(svg.data()).find("<svg")
                        != std::string::npos,
                output,
                "SVG C ABI must export the optimized layout"))
    {
        return false;
    }

    std::vector<char> error(128U);
    const int emptyResult = SparrowOptimizeJson(
        nullptr, 0U, error.data(), error.size());
    if (!Expect(emptyResult == 1
                    && std::string(error.data()).find("input is empty")
                        != std::string::npos,
                output,
                "JSON C ABI must report empty input"))
    {
        return false;
    }

    std::vector<char> malformed(128U);
    const std::string badJson = "{not json";
    const int malformedResult = SparrowOptimizeJson(
        badJson.data(), badJson.size(), malformed.data(), malformed.size());
    if (!Expect(malformedResult == 2
                    && std::string(malformed.data()).find("\"error\"")
                        != std::string::npos,
                output,
                "JSON C ABI must report import failure as code 2 with an error payload"))
    {
        return false;
    }

    std::vector<char> svgMalformed(128U);
    const int svgMalformedResult = SparrowOptimizeSvg(
        badJson.data(), badJson.size(), svgMalformed.data(), svgMalformed.size());
    if (!Expect(svgMalformedResult == 2
                    && std::string(svgMalformed.data()).find("\"error\"")
                        != std::string::npos,
                output,
                "SVG C ABI must report import failure as code 2 with an error payload"))
    {
        return false;
    }

    std::vector<char> tiny(1U);
    const int capacityResult = SparrowOptimizeJson(
        input.data(), input.size(), tiny.data(), tiny.size());
    return Expect(capacityResult == 3,
                  output,
                  "JSON C ABI must reject an undersized output buffer");
}
