#include "LbfProductSupport.h"

#include "../../Source/Optimizer/LbfOptimizerTrace.h"
#include "JaguarCore/Geometry/Edge.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/Geometry/SimplePolygon.h"
#include "JaguarCore/IO/JsonCodec.h"
#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/Spp.h"
#include "SparrowCore/Optimizer/BppLbfOptimizer.h"
#include "SparrowCore/Optimizer/SppLbfOptimizer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>
#include <optional>
#include <utility>
#include <vector>

namespace sparrow::lbf_entry
{
namespace
{
double RustF32Value(double value) noexcept
{
    return static_cast<double>(static_cast<float>(value));
}

std::string Number(double value)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(9) << value;
    return output.str();
}

std::string NumberF32(double value)
{
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed))
        return "null";
    char buffer[64]{};
    const auto formatted = std::to_chars(
        buffer, buffer + sizeof(buffer), narrowed, std::chars_format::general);
    if (formatted.ec == std::errc{})
        return std::string(buffer, formatted.ptr);
    return Number(static_cast<double>(narrowed));
}

std::string Color(const jaguar::SvgColor& color)
{
    std::ostringstream output;
    output << '"' << '#' << std::uppercase << std::hex << std::setfill('0')
           << std::setw(2) << static_cast<unsigned int>(color.red)
           << std::setw(2) << static_cast<unsigned int>(color.green)
           << std::setw(2) << static_cast<unsigned int>(color.blue) << '"';
    return output.str();
}

std::string QuoteJson(const std::string& text)
{
    std::ostringstream output;
    output << '"';
    for (const unsigned char ch : text)
    {
        switch (ch)
        {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20U)
            {
                output << "\\u00" << std::hex << std::setw(2)
                       << std::setfill('0') << static_cast<unsigned int>(ch)
                       << std::dec << std::setfill(' ');
            }
            else
            {
                output << static_cast<char>(ch);
            }
        }
    }
    output << '"';
    return output.str();
}

std::string AppendObjectMember(std::string object,
                               const char* key,
                               const std::string& value)
{
    while (!object.empty()
           && (object.back() == ' ' || object.back() == '\t'
               || object.back() == '\r' || object.back() == '\n'))
    {
        object.pop_back();
    }
    if (object.empty() || object.back() != '}')
    {
        return {};
    }
    object.pop_back();
    object += ",\n  \"";
    object += key;
    object += "\": ";
    object += value;
    object += "\n}";
    return object;
}

std::string SerializeBppOutput(const jaguar::ExternalBPInstance& instance,
                               const jaguar::ExternalBPSolution& solution,
                               const RuntimeConfig& config)
{
    const auto instanceJson = jaguar::SerializeExternalBPInstanceJson(
        instance, {true});
    const auto solutionJson = jaguar::SerializeExternalBPSolutionJson(
        solution, {true});
    if (!instanceJson.HasValue() || !solutionJson.HasValue())
    {
        return {};
    }

    std::string output = instanceJson.Value();
    while (!output.empty()
           && (output.back() == ' ' || output.back() == '\t'
               || output.back() == '\r' || output.back() == '\n'))
    {
        output.pop_back();
    }
    if (output.empty() || output.back() != '}')
    {
        return {};
    }
    output.pop_back();
    output += ",\n  \"solution\": ";
    output += solutionJson.Value();
    output += ",\n  \"config\": ";
    output += SerializeRuntimeConfig(config);
    output += "\n}";
    return output;
}

jaguar::Result<std::string> ExportSvg(const jaguar::LayoutSnapshot& snapshot,
                                      const RuntimeConfig& config)
{
    jaguar::Layout layout(snapshot.container);
    const auto restored = layout.Restore(snapshot);
    if (!restored.HasValue())
    {
        return jaguar::Result<std::string>::FromError(restored.GetError());
    }
    const jaguar::SvgDebugOptions debugOptions{
        config.svgQuadtree,
        config.svgDrawCdShapes,
        config.svgHighlightCdShapes};
    return jaguar::ExportLayoutSvg(layout, config.svg, debugOptions);
}

std::uint64_t ElapsedMilliseconds(
    std::chrono::steady_clock::time_point start) noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count()));
}

std::uint64_t ElapsedSeconds(
    std::chrono::steady_clock::time_point start) noexcept
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start);
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed.count()));
}

std::optional<std::vector<jaguar::Point2d>> itemOuterPoints(
    const jaguar::ExternalShape& shape)
{
    if (const auto* simple = std::get_if<jaguar::ExternalSimplePolygon>(
            &shape.value))
    {
        return simple->points;
    }
    if (const auto* polygon = std::get_if<jaguar::ExternalPolygon>(
            &shape.value);
        polygon && polygon->holes.empty())
    {
        return polygon->outer;
    }
    return std::nullopt;
}

std::optional<std::vector<jaguar::Point2d>> shapeOuterPoints(
    const jaguar::ExternalShape& shape)
{
    if (const auto* simple = std::get_if<jaguar::ExternalSimplePolygon>(
            &shape.value))
    {
        return simple->points;
    }
    if (const auto* polygon = std::get_if<jaguar::ExternalPolygon>(
            &shape.value))
    {
        return polygon->outer;
    }
    return std::nullopt;
}

std::vector<jaguar::Point2d> normalizeImportedPoints(
    std::vector<jaguar::Point2d> points)
{
    for (jaguar::Point2d& point : points)
    {
        point.x = static_cast<double>(static_cast<float>(point.x));
        point.y = static_cast<double>(static_cast<float>(point.y));
    }
    if (points.size() > 1U && points.front() == points.back())
        points.pop_back();

    std::vector<std::size_t> duplicateVertices;
    duplicateVertices.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const std::size_t next = (i + 1U) % points.size();
        if (jaguar::detail::AlmostEqualRustF32(
                static_cast<float>(points[i].x),
                static_cast<float>(points[next].x))
            && jaguar::detail::AlmostEqualRustF32(
                static_cast<float>(points[i].y),
                static_cast<float>(points[next].y)))
        {
            duplicateVertices.push_back(i);
        }
    }
    std::sort(duplicateVertices.rbegin(), duplicateVertices.rend());
    for (const std::size_t index : duplicateVertices)
    {
        if (index < points.size())
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return points;
}

std::optional<std::vector<jaguar::Point2d>> findFailingSimplePolygonPoints(
    const std::vector<jaguar::ExternalDemandItem>* items,
    const std::vector<jaguar::ExternalBin>* bins,
    const std::string& errorMessage)
{
    const auto findMatch = [&errorMessage](std::vector<jaguar::Point2d> points)
        -> std::optional<std::vector<jaguar::Point2d>>
    {
        points = normalizeImportedPoints(std::move(points));
        const auto polygon = jaguar::SimplePolygon::Create(points);
        if (!polygon.HasValue() && polygon.GetError().message == errorMessage)
            return points;
        return std::nullopt;
    };

    if (items)
    {
        for (const jaguar::ExternalDemandItem& demandItem : *items)
        {
            auto points = itemOuterPoints(demandItem.item.shape);
            if (!points)
                continue;
            if (auto match = findMatch(std::move(*points)))
                return match;
        }
    }

    if (bins)
    {
        for (const jaguar::ExternalBin& bin : *bins)
        {
            auto outer = shapeOuterPoints(bin.container.shape);
            if (outer)
            {
                if (auto match = findMatch(std::move(*outer)))
                    return match;
            }

            if (const auto* polygon = std::get_if<jaguar::ExternalPolygon>(
                    &bin.container.shape.value))
            {
                for (const auto& hole : polygon->holes)
                {
                    if (auto match = findMatch(hole))
                        return match;
                }
            }

            std::vector<const jaguar::ExternalQualityZone*> zones;
            zones.reserve(bin.container.zones.size());
            for (const jaguar::ExternalQualityZone& zone : bin.container.zones)
                zones.push_back(&zone);
            std::stable_sort(zones.begin(), zones.end(),
                [](const auto* lhs, const auto* rhs) {
                    return lhs->quality < rhs->quality;
                });
            for (const jaguar::ExternalQualityZone* zone : zones)
            {
                auto points = shapeOuterPoints(zone->shape);
                if (!points)
                    continue;
                if (auto match = findMatch(std::move(*points)))
                    return match;
            }
        }
    }
    return std::nullopt;
}

std::string rustF32Debug(float value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-inf" : "inf";

    char buffer[64]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        return "0.0";
    std::string formatted(buffer, result.ptr);
    if (formatted.find_first_of(".eE") == std::string::npos)
        formatted += ".0";
    const std::size_t exponent = formatted.find_first_of("eE");
    if (exponent != std::string::npos)
    {
        const int exponentValue = std::stoi(formatted.substr(exponent + 1U));
        formatted = formatted.substr(0, exponent) + "e"
            + std::to_string(exponentValue);
    }
    return formatted;
}

std::string rustPointList(const std::vector<jaguar::Point2d>& points)
{
    std::string result = "[";
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (i != 0U)
            result += ", ";
        result += "Point(" + rustF32Debug(static_cast<float>(points[i].x))
            + ", " + rustF32Debug(static_cast<float>(points[i].y)) + ")";
    }
    result += "]";
    return result;
}

std::optional<std::pair<std::size_t, std::size_t>> firstSelfIntersection(
    const std::vector<jaguar::Point2d>& points)
{
    const std::size_t count = points.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t iNext = (i + 1U) % count;
        const auto first = jaguar::Edge2d::Create(points[i], points[iNext]);
        if (!first.HasValue())
            return std::nullopt;

        for (std::size_t j = i + 1U; j < count; ++j)
        {
            const std::size_t jNext = (j + 1U) % count;
            if (iNext == j || jNext == i)
                continue;
            const auto second = jaguar::Edge2d::Create(points[j], points[jNext]);
            if (!second.HasValue())
                return std::nullopt;
            if (first.Value().IntersectsRustF32(second.Value()))
                return std::pair<std::size_t, std::size_t>{i, j};
        }
    }
    return std::nullopt;
}

jaguar::Error FormatImporterError(jaguar::Error error,
                                  ErrorTextFormat errorTextFormat,
                                  const std::vector<jaguar::ExternalDemandItem>* items,
                                  const std::vector<jaguar::ExternalBin>* bins,
                                  const double* sppStripHeight = nullptr)
{
    if (errorTextFormat == ErrorTextFormat::RustWasm)
    {
        if (error.message == "minimum item separation must be finite and nonnegative")
        {
            error.message =
                "Importer error: min_item_separation must be finite and nonnegative";
        }
        else if (error.message == "external item IDs must be unique")
        {
            error.message = "Importer error: item IDs must be unique";
        }
        else if (error.message == "external bin IDs must be unique")
        {
            error.message = "Importer error: bin IDs must be unique";
        }
        else if (error.message == "SPP instance has invalid items or height")
        {
            if (sppStripHeight && std::isinf(*sppStripHeight)
                && *sppStripHeight > 0.0)
            {
                error.message = "Importer error: strip width must be positive";
            }
            else
            {
                error.message = "Importer error: strip height must be positive";
            }
        }
        else if (error.message == "item polygons with holes are unsupported")
        {
            const jaguar::ExternalItem* itemWithHoles = nullptr;
            if (items)
            {
                for (const jaguar::ExternalDemandItem& demandItem : *items)
                {
                    const auto* polygon = std::get_if<jaguar::ExternalPolygon>(
                        &demandItem.item.shape.value);
                    if (polygon && !polygon->holes.empty())
                    {
                        itemWithHoles = &demandItem.item;
                        break;
                    }
                }
            }
            if (itemWithHoles)
            {
                error.message = "Importer error: item "
                    + std::to_string(itemWithHoles->id)
                    + " has unsupported holes";
            }
            else
            {
                error.message = "Importer error: " + error.message;
            }
        }
        else if (error.message == "item multipolygons are unsupported")
        {
            error.message = "Importer error: No support for multipolygons yet";
        }
        else if (error.message == "container multipolygons are unsupported")
        {
            error.message = "Importer error: No support for multipolygon shapes yet";
        }
        else if (error.message == "simple polygon needs at least three points"
                 || error.message == "simple polygon has no area"
                 || error.message == "simple polygon contains intersecting edges")
        {
            const auto points = findFailingSimplePolygonPoints(
                items, bins, error.message);
            if (!points)
            {
                error.message = "Importer error: " + error.message;
            }
            else
            {
                const std::string pointList = rustPointList(*points);
                if (error.message == "simple polygon needs at least three points")
                {
                    error.message = "Importer error: Simple polygon must have at least 3 points: "
                        + pointList;
                }
                else if (error.message == "simple polygon has no area")
                {
                    error.message = "Importer error: Simple polygon has no area: "
                        + pointList;
                }
                else if (const auto edges = firstSelfIntersection(*points))
                {
                    error.message = "Importer error: Simple polygon contains intersecting edges "
                        + std::to_string(edges->first) + " and "
                        + std::to_string(edges->second) + ": " + pointList;
                }
                else
                {
                    error.message = "Importer error: " + error.message;
                }
            }
        }
        else
        {
            error.message = "Importer error: " + error.message;
        }
    }
    return error;
}
}

RuntimeConfig RustDefaultRuntimeConfig()
{
    RuntimeConfig config;
    config.optimizer.samplesPerItem = 5000U;
    config.optimizer.localSearchFraction = RustF32Value(0.2);

    config.importer.cdeConfig.quadtreeDepth = 5U;
    config.importer.cdeConfig.cdThreshold = 96U;
    config.importer.cdeConfig.itemSurrogateConfig.poleLimits = {
        {{100U, RustF32Value(0.0)},
         {20U, RustF32Value(0.75)},
         {10U, RustF32Value(0.90)}}};
    config.importer.cdeConfig.itemSurrogateConfig.failFastPoleAreaRatio =
        RustF32Value(0.5);
    config.importer.cdeConfig.itemSurrogateConfig.failFastPiers = 0U;
    config.importer.shapeModifyConfig.simplifyTolerance = RustF32Value(0.001);
    config.importer.shapeModifyConfig.narrowConcavityCutoff =
        std::pair<double, double>{RustF32Value(0.01), RustF32Value(0.01)};

    config.svg.title.clear();
    config.svg.includeAxes = false;
    config.svg.drawSurrogates = false;
    config.svg.highlightCollisions = true;
    config.svg.theme.strokeWidthMultiplier = RustF32Value(2.0);
    config.svg.theme.containerFill = {0xcc, 0x82, 0x4a};
    config.svg.theme.itemFill = {0xff, 0xc8, 0x79};
    config.svg.theme.collisionStroke = {0x00, 0xff, 0x00};
    config.svg.theme.collisionHighlight = {0x00, 0xff, 0x00};
    config.svg.theme.qualityZoneFills = {{
        {0x00, 0x00, 0x00}, {0xff, 0x00, 0x00}, {0xff, 0x5e, 0x00},
        {0xff, 0xa5, 0x00}, {0xc7, 0xa9, 0x00}, {0xff, 0xff, 0x00},
        {0xcb, 0xff, 0x00}, {0xcb, 0xff, 0x00}, {0xcb, 0xff, 0x00},
        {0xcb, 0xff, 0x00}}};
    config.svg.theme.qualityZoneStrokeOpacity = RustF32Value(0.5);
    return config;
}

std::uint64_t RandomSeed()
{
    std::random_device source;
    const std::uint64_t high = static_cast<std::uint64_t>(source()) << 32U;
    return high ^ static_cast<std::uint64_t>(source());
}

std::string SerializeRuntimeConfig(const RuntimeConfig& config)
{
    const jaguar::CDEConfig& cde = config.importer.cdeConfig;
    const jaguar::SurrogateConfig& surrogate = cde.itemSurrogateConfig;
    const auto& shape = config.importer.shapeModifyConfig;
    const jaguar::SvgLayoutTheme& theme = config.svg.theme;

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\n"
           << "  \"cde_config\": {\n"
           << "    \"quadtree_depth\": "
           << static_cast<unsigned int>(cde.quadtreeDepth) << ",\n"
           << "    \"cd_threshold\": "
           << static_cast<unsigned int>(cde.cdThreshold) << ",\n"
           << "    \"item_surrogate_config\": {\n"
           << "      \"n_pole_limits\": [["
           << surrogate.poleLimits[0].first << ", "
           << NumberF32(surrogate.poleLimits[0].second) << "], ["
           << surrogate.poleLimits[1].first << ", "
           << NumberF32(surrogate.poleLimits[1].second) << "], ["
           << surrogate.poleLimits[2].first << ", "
           << NumberF32(surrogate.poleLimits[2].second) << "]],\n"
           << "      \"ff_pole_area_ratio\": "
           << NumberF32(surrogate.failFastPoleAreaRatio) << ",\n"
           << "      \"n_ff_piers\": " << surrogate.failFastPiers << "\n"
           << "    }\n"
           << "  },\n"
           << "  \"poly_simpl_tolerance\": ";
    if (shape.simplifyTolerance.has_value())
        output << NumberF32(*shape.simplifyTolerance);
    else
        output << "null";
    output << ",\n  \"narrow_concavity_cutoff\": ";
    if (shape.narrowConcavityCutoff.has_value())
    {
        output << '[' << NumberF32(shape.narrowConcavityCutoff->first) << ", "
               << NumberF32(shape.narrowConcavityCutoff->second) << ']';
    }
    else
    {
        output << "null";
    }
    output << ",\n  \"prng_seed\": ";
    if (config.configuredSeed.has_value())
        output << *config.configuredSeed;
    else
        output << "null";
    output << ",\n  \"n_samples\": " << config.optimizer.samplesPerItem
           << ",\n  \"ls_frac\": "
           << NumberF32(config.optimizer.localSearchFraction)
           << ",\n  \"svg_draw_options\": {\n"
           << "    \"theme\": {\n"
           << "      \"stroke_width_multiplier\": "
           << NumberF32(theme.strokeWidthMultiplier) << ",\n"
           << "      \"container_fill\": " << Color(theme.containerFill)
           << ",\n"
           << "      \"item_fill\": " << Color(theme.itemFill) << ",\n"
           << "      \"hole_fill\": " << Color(config.holeFill) << ",\n"
           << "      \"qz_fill\": [";
    for (std::size_t i = 0; i < theme.qualityZoneFills.size(); ++i)
    {
        if (i != 0U)
            output << ", ";
        output << Color(theme.qualityZoneFills[i]);
    }
    output << "],\n"
           << "      \"qz_stroke_opac\": "
           << NumberF32(theme.qualityZoneStrokeOpacity) << ",\n"
           << "      \"collision_highlight_color\": "
           << Color(theme.collisionHighlight) << "\n"
           << "    },\n"
           << "    \"quadtree\": "
           << (config.svgQuadtree ? "true" : "false") << ",\n"
           << "    \"surrogate\": "
           << (config.svg.drawSurrogates ? "true" : "false") << ",\n"
           << "    \"highlight_collisions\": "
           << (config.svg.highlightCollisions ? "true" : "false") << ",\n"
           << "    \"draw_cd_shapes\": "
           << (config.svgDrawCdShapes ? "true" : "false") << ",\n"
           << "    \"highlight_cd_shapes\": "
           << (config.svgHighlightCdShapes ? "true" : "false") << "\n"
           << "  }\n"
           << '}';
    return output.str();
}

std::string SerializeWasmResult(const Artifact& artifact)
{
    std::ostringstream output;
    output << "{\"output\":" << artifact.outputJson << ",\"svgs\":[";
    for (std::size_t i = 0; i < artifact.svgs.size(); ++i)
    {
        if (i != 0U)
            output << ',';
        output << '[' << QuoteJson(artifact.svgs[i].first) << ','
               << QuoteJson(artifact.svgs[i].second) << ']';
    }
    output << "],\"solve_time_ms\":" << artifact.solveTimeMs << '}';
    return output.str();
}

Result<Artifact> RunSppLbf(const std::string& instanceJson,
                           const RuntimeConfig& config,
                           std::uint64_t effectiveSeed,
                           const std::string& svgFileName,
                           const DiagnosticCallback& diagnostics,
                           ErrorTextFormat errorTextFormat)
{
    const auto epoch = std::chrono::steady_clock::now();
    const auto external = jaguar::ParseExternalSPInstanceJson(
        instanceJson,
        jaguar::JsonParseOptions{errorTextFormat == ErrorTextFormat::RustWasm});
    if (!external.HasValue())
        return Result<Artifact>::FromError(external.GetError());

    const jaguar::Importer importer(config.importer);
    const auto instance = importer.ImportSPInstance(external.Value());
    if (!instance.HasValue())
    {
        return Result<Artifact>::FromError(
            FormatImporterError(instance.GetError(), errorTextFormat,
                                &external.Value().items, nullptr,
                                &external.Value().stripHeight));
    }

    LbfConfig solverConfig = config.optimizer;
    solverConfig.seed = effectiveSeed;
    const auto solveStart = std::chrono::steady_clock::now();
    const auto result = diagnostics
        ? SolveSppLbfWithDiagnostics(instance.Value(), solverConfig, diagnostics)
        : SolveSppLbf(instance.Value(), solverConfig);
    const std::uint64_t solveTimeMs = ElapsedMilliseconds(solveStart);
    if (!result.HasValue())
        return Result<Artifact>::FromError(result.GetError());

    const auto externalSolution = jaguar::ExternalSPSolutionFromSolution(
        instance.Value(), result.Value().solution, ElapsedSeconds(epoch));
    if (!externalSolution.HasValue())
        return Result<Artifact>::FromError(externalSolution.GetError());
    const jaguar::ExternalSPOutput externalOutput{
        external.Value(), externalSolution.Value()};
    const auto output = jaguar::SerializeExternalSPOutputJson(
        externalOutput, {true});
    if (!output.HasValue())
        return Result<Artifact>::FromError(output.GetError());
    const std::string finalOutput = AppendObjectMember(
        output.Value(), "config", SerializeRuntimeConfig(config));
    if (finalOutput.empty())
    {
        return Result<Artifact>::FromError(
            {jaguar::ErrorCode::SerializationFailure,
             "could not append LBF config to SPP output"});
    }

    const auto svg = ExportSvg(result.Value().solution.layoutSnapshot, config);
    if (!svg.HasValue())
        return Result<Artifact>::FromError(svg.GetError());

    Artifact artifact;
    artifact.outputJson = finalOutput;
    artifact.svgs.emplace_back(svgFileName, svg.Value());
    artifact.solveTimeMs = solveTimeMs;
    artifact.metrics = result.Value().metrics;
    return Result<Artifact>::FromValue(std::move(artifact));
}

Result<Artifact> RunBppLbf(const std::string& instanceJson,
                           const RuntimeConfig& config,
                           std::uint64_t effectiveSeed,
                           const std::string& svgFileStem,
                           const DiagnosticCallback& diagnostics,
                           ErrorTextFormat errorTextFormat)
{
    const auto epoch = std::chrono::steady_clock::now();
    const auto external = jaguar::ParseExternalBPInstanceJson(
        instanceJson,
        jaguar::JsonParseOptions{errorTextFormat == ErrorTextFormat::RustWasm});
    if (!external.HasValue())
        return Result<Artifact>::FromError(external.GetError());

    const jaguar::Importer importer(config.importer);
    const auto instance = importer.ImportBPInstance(external.Value());
    if (!instance.HasValue())
    {
        return Result<Artifact>::FromError(
            FormatImporterError(instance.GetError(), errorTextFormat,
                                &external.Value().items,
                                &external.Value().bins));
    }

    LbfConfig solverConfig = config.optimizer;
    solverConfig.seed = effectiveSeed;
    const auto solveStart = std::chrono::steady_clock::now();
    const auto result = diagnostics
        ? SolveBppLbfWithDiagnostics(instance.Value(), solverConfig, diagnostics)
        : SolveBppLbf(instance.Value(), solverConfig);
    const std::uint64_t solveTimeMs = ElapsedMilliseconds(solveStart);
    if (!result.HasValue())
        return Result<Artifact>::FromError(result.GetError());

    const auto externalSolution = jaguar::ExternalBPSolutionFromSolution(
        instance.Value(), result.Value().solution, ElapsedSeconds(epoch));
    if (!externalSolution.HasValue())
        return Result<Artifact>::FromError(externalSolution.GetError());
    const std::string output = SerializeBppOutput(
        external.Value(), externalSolution.Value(), config);
    if (output.empty())
    {
        return Result<Artifact>::FromError(
            {jaguar::ErrorCode::SerializationFailure,
             "could not serialize BPP LBF output"});
    }

    Artifact artifact;
    artifact.outputJson = output;
    artifact.solveTimeMs = solveTimeMs;
    artifact.metrics = result.Value().metrics;
    for (std::size_t i = 0; i < result.Value().solution.layouts.size(); ++i)
    {
        const auto svg = ExportSvg(
            result.Value().solution.layouts[i].layoutSnapshot, config);
        if (!svg.HasValue())
            return Result<Artifact>::FromError(svg.GetError());
        artifact.svgs.emplace_back(
            svgFileStem + "_" + std::to_string(i) + ".svg", svg.Value());
    }
    return Result<Artifact>::FromValue(std::move(artifact));
}
}
