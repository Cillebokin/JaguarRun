#include "LbfProductSupport.h"

#include <algorithm>
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
        return true;
    output << "LBF ENTRY TEST FAILURE: " << message << '\n';
    return false;
}

std::string ReadFixture(const char* fileName)
{
    for (const std::string& path : {
             std::string("SparrowCore/Tests/Fixtures/") + fileName,
             std::string("../SparrowCore/Tests/Fixtures/") + fileName,
             std::string("..\\SparrowCore\\Tests\\Fixtures\\") + fileName})
    {
        std::ifstream input(path, std::ios::binary);
        if (input.is_open())
            return std::string(std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>());
    }
    return {};
}
}

bool RunLbfEntryTests(std::ostream& output)
{
    using namespace sparrow::lbf_entry;

    RuntimeConfig config = RustDefaultRuntimeConfig();
    config.configuredSeed = 42U;
    config.optimizer.samplesPerItem = 64U;
    config.optimizer.localSearchFraction = 0.25;

    const std::string sppInput = ReadFixture("LbfSppSmall.json");
    if (!Expect(!sppInput.empty(), output, "SPP fixture must be readable"))
        return false;
    const auto spp = RunSppLbf(sppInput, config, 42U, "sol_spp.svg");
    if (!Expect(spp.HasValue(), output, "SPP LBF product entry must solve"))
        return false;
    if (!Expect(spp.Value().outputJson.find("\"solution\"") != std::string::npos
                    && spp.Value().outputJson.find("\"config\"") != std::string::npos
                    && spp.Value().outputJson.find("\"n_samples\": 64")
                           != std::string::npos,
                output,
                "SPP output must contain Rust-shaped solution and config"))
        return false;
    if (!Expect(spp.Value().svgs.size() == 1U
                    && spp.Value().svgs.front().first == "sol_spp.svg"
                    && spp.Value().svgs.front().second.find("<svg")
                           != std::string::npos,
                output,
                "SPP entry must return its layout SVG"))
        return false;

    std::vector<std::string> sppDiagnostics;
    const auto sppWithDiagnostics = RunSppLbf(
        sppInput,
        config,
        42U,
        "sol_spp_diagnostics.svg",
        [&sppDiagnostics](const std::string& message) {
            sppDiagnostics.push_back(message);
        });
    if (!Expect(sppWithDiagnostics.HasValue(),
                output,
                "SPP LBF diagnostics must preserve solve success"))
    {
        return false;
    }
    if (!Expect(std::any_of(
                    sppDiagnostics.begin(),
                    sppDiagnostics.end(),
                    [](const std::string& message) {
                        return message.find("[UNI: ") == 0U
                            && message.find("/48] better:") != std::string::npos
                            && message.find("better: r: ") != std::string::npos
                            && message.find("°, t: (") != std::string::npos;
                    }),
                output,
                "SPP diagnostics must report accepted uniform candidates with budget"))
    {
        return false;
    }

    const std::string bppInput = ReadFixture("LbfBppSmall.json");
    if (!Expect(!bppInput.empty(), output, "BPP fixture must be readable"))
        return false;
    const auto bpp = RunBppLbf(bppInput, config, 42U, "sol_bpp");
    if (!Expect(bpp.HasValue(), output, "BPP LBF product entry must solve"))
        return false;
    if (!Expect(bpp.Value().outputJson.find("\"bins\"") != std::string::npos
                    && bpp.Value().outputJson.find("\"solution\"")
                           != std::string::npos
                    && bpp.Value().outputJson.find("\"config\"")
                           != std::string::npos,
                output,
                "BPP output must flatten instance and include solution/config"))
        return false;
    if (!Expect(bpp.Value().svgs.size() == 1U
                    && bpp.Value().svgs.front().first == "sol_bpp_0.svg"
                    && bpp.Value().svgs.front().second.find("<svg")
                           != std::string::npos,
                output,
                "BPP entry must return one SVG per solution layout"))
        return false;

    std::vector<std::string> bppDiagnostics;
    const auto bppWithDiagnostics = RunBppLbf(
        bppInput,
        config,
        42U,
        "sol_bpp_diagnostics",
        [&bppDiagnostics](const std::string& message) {
            bppDiagnostics.push_back(message);
        });
    if (!Expect(bppWithDiagnostics.HasValue(),
                output,
                "BPP LBF diagnostics must preserve solve success"))
    {
        return false;
    }
    if (!Expect(std::any_of(
                    bppDiagnostics.begin(),
                    bppDiagnostics.end(),
                    [](const std::string& message) {
                        return message.find("searching in layout Closed { bin_id: ")
                               == 0U;
                    }),
                output,
                "BPP diagnostics must identify searched bin layouts"))
    {
        return false;
    }

    const std::string wasmJson = SerializeWasmResult(spp.Value());
    return Expect(wasmJson.find("\"solve_time_ms\"") != std::string::npos
                      && wasmJson.find("\"svgs\"") != std::string::npos,
                  output,
                  "WASM output must wrap JSON, SVG pairs and solve time");
}
