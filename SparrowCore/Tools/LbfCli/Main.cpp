#include "../Lbf/LbfProductSupport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
struct Arguments
{
    std::string inputFile;
    std::string solutionFolder;
    std::optional<std::string> configFile;
    std::string logLevel = "info";
    std::string problemVariant;
    bool help = false;
    bool version = false;
};

void PrintUsage(std::ostream& output)
{
    output << "usage: LbfCli -i|--input-file <FILE> -s|--solution-folder <FOLDER> "
              "-p|--prob-var <bpp|spp> [options]\n"
              "  -V, --version\n"
              "  -c, --config-file <FILE>\n"
              "  -l, --log-level <off|error|warn|info|debug|trace> (default: info)\n";
}

void PrintVersion(std::ostream& output)
{
    output << "lbf 0.0.1\n";
}

bool ParseArguments(int argc, char** argv, Arguments& args)
{
    std::set<std::string> seen;
    for (int i = 1; i < argc; ++i)
    {
        const std::string option = argv[i];
        if (option == "-h" || option == "--help")
        {
            PrintUsage(std::cout);
            args.help = true;
            return true;
        }
        if (option == "-V" || option == "--version")
        {
            PrintVersion(std::cout);
            args.version = true;
            return true;
        }
        std::string* target = nullptr;
        std::string key;
        if (option == "-i" || option == "--input-file")
        {
            target = &args.inputFile;
            key = "input-file";
        }
        else if (option == "-s" || option == "--solution-folder")
        {
            target = &args.solutionFolder;
            key = "solution-folder";
        }
        else if (option == "-p" || option == "--prob-var")
        {
            target = &args.problemVariant;
            key = "prob-var";
        }
        else if (option == "-l" || option == "--log-level")
        {
            target = &args.logLevel;
            key = "log-level";
        }
        else if (option == "-c" || option == "--config-file")
        {
            key = "config-file";
            args.configFile = std::string{};
            target = &*args.configFile;
        }
        else
        {
            std::cerr << "unknown option: " << option << '\n';
            PrintUsage(std::cerr);
            return false;
        }
        if (!seen.insert(key).second)
        {
            std::cerr << "duplicate option: " << option << '\n';
            return false;
        }
        if (i + 1 >= argc)
        {
            std::cerr << "missing value for option: " << option << '\n';
            PrintUsage(std::cerr);
            return false;
        }
        if (target == nullptr)
            return false;
        *target = argv[++i];
    }
    if (args.inputFile.empty() || args.solutionFolder.empty()
        || args.problemVariant.empty())
    {
        PrintUsage(std::cerr);
        return false;
    }
    if (args.problemVariant != "bpp" && args.problemVariant != "spp")
    {
        std::cerr << "invalid problem variant; expected bpp or spp\n";
        return false;
    }
    if (args.logLevel != "off" && args.logLevel != "error"
        && args.logLevel != "warn" && args.logLevel != "info"
        && args.logLevel != "debug" && args.logLevel != "trace")
    {
        std::cerr << "invalid log level\n";
        return false;
    }
    return true;
}

bool ReadFile(const std::string& path, std::string& contents)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
        return false;
    contents.assign(std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool ReadNumber(const QJsonValue& value, double& result)
{
    if (!value.isDouble())
        return false;
    result = value.toDouble();
    return std::isfinite(result);
}

double RustF32(double value) noexcept
{
    return static_cast<double>(static_cast<float>(value));
}

class RawJsonNumberScanner
{
public:
    explicit RawJsonNumberScanner(std::string_view json) : m_Json(json) {}

    bool Scan(std::map<std::string, std::string>& numbers,
              std::set<std::string>& duplicateKeys)
    {
        m_Numbers = &numbers;
        m_DuplicateKeys = &duplicateKeys;
        std::string path;
        if (!parseValue(path, 0U))
            return false;
        skipWhitespace();
        return m_Position == m_Json.size();
    }

private:
    static constexpr std::size_t kMaxDepth = 256U;

    void skipWhitespace()
    {
        while (m_Position < m_Json.size())
        {
            const char value = m_Json[m_Position];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
                break;
            ++m_Position;
        }
    }

    static std::string childPath(const std::string& path,
                                 const std::string& segment)
    {
        return path + "/" + segment;
    }

    static std::string escapedPathSegment(const std::string& key)
    {
        std::string escaped;
        escaped.reserve(key.size());
        for (const char value : key)
        {
            if (value == '~')
                escaped += "~0";
            else if (value == '/')
                escaped += "~1";
            else
                escaped.push_back(value);
        }
        return escaped;
    }

    void clearPath(const std::string& path)
    {
        for (auto it = m_Numbers->begin(); it != m_Numbers->end();)
        {
            const bool samePath = it->first == path;
            const bool child = it->first.size() > path.size() &&
                it->first.compare(0U, path.size(), path) == 0 &&
                (path.empty() || it->first[path.size()] == '/');
            if (samePath || child)
                it = m_Numbers->erase(it);
            else
                ++it;
        }
    }

    bool parseString(std::string* decoded)
    {
        if (m_Position >= m_Json.size() || m_Json[m_Position] != '"')
            return false;
        ++m_Position;
        if (decoded != nullptr)
            decoded->clear();
        while (m_Position < m_Json.size())
        {
            const unsigned char value =
                static_cast<unsigned char>(m_Json[m_Position++]);
            if (value == '"')
                return true;
            if (value < 0x20U)
                return false;
            if (value != '\\')
            {
                if (decoded != nullptr)
                    decoded->push_back(static_cast<char>(value));
                continue;
            }
            if (m_Position >= m_Json.size())
                return false;
            const char escape = m_Json[m_Position++];
            switch (escape)
            {
            case '"': if (decoded) decoded->push_back('"'); break;
            case '\\': if (decoded) decoded->push_back('\\'); break;
            case '/': if (decoded) decoded->push_back('/'); break;
            case 'b': if (decoded) decoded->push_back('\b'); break;
            case 'f': if (decoded) decoded->push_back('\f'); break;
            case 'n': if (decoded) decoded->push_back('\n'); break;
            case 'r': if (decoded) decoded->push_back('\r'); break;
            case 't': if (decoded) decoded->push_back('\t'); break;
            case 'u':
            {
                std::uint32_t codePoint = 0U;
                if (!readHexQuad(codePoint))
                    return false;
                if (codePoint >= 0xd800U && codePoint <= 0xdbffU)
                {
                    if (m_Position + 2U > m_Json.size() ||
                        m_Json[m_Position] != '\\' ||
                        m_Json[m_Position + 1U] != 'u')
                    {
                        return false;
                    }
                    m_Position += 2U;
                    std::uint32_t low = 0U;
                    if (!readHexQuad(low) || low < 0xdc00U || low > 0xdfffU)
                        return false;
                    codePoint = 0x10000U +
                        ((codePoint - 0xd800U) << 10U) + (low - 0xdc00U);
                }
                else if (codePoint >= 0xdc00U && codePoint <= 0xdfffU)
                {
                    return false;
                }
                if (decoded != nullptr)
                    appendUtf8(*decoded, codePoint);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool readHexQuad(std::uint32_t& result)
    {
        if (m_Position + 4U > m_Json.size())
            return false;
        result = 0U;
        for (unsigned int i = 0U; i < 4U; ++i)
        {
            const char digit = m_Json[m_Position++];
            std::uint32_t nibble = 0U;
            if (digit >= '0' && digit <= '9')
                nibble = static_cast<std::uint32_t>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                nibble = static_cast<std::uint32_t>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                nibble = static_cast<std::uint32_t>(digit - 'A' + 10);
            else
                return false;
            result = (result << 4U) | nibble;
        }
        return true;
    }

    static void appendUtf8(std::string& output, std::uint32_t codePoint)
    {
        if (codePoint <= 0x7fU)
        {
            output.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7ffU)
        {
            output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
        }
        else if (codePoint <= 0xffffU)
        {
            output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
        }
    }

    bool parseNumber(const std::string& path)
    {
        const std::size_t start = m_Position;
        if (m_Json[m_Position] == '-')
            ++m_Position;
        if (m_Position >= m_Json.size())
            return false;
        if (m_Json[m_Position] == '0')
        {
            ++m_Position;
        }
        else
        {
            if (m_Json[m_Position] < '1' || m_Json[m_Position] > '9')
                return false;
            while (m_Position < m_Json.size() &&
                   m_Json[m_Position] >= '0' && m_Json[m_Position] <= '9')
            {
                ++m_Position;
            }
        }
        if (m_Position < m_Json.size() && m_Json[m_Position] == '.')
        {
            ++m_Position;
            const std::size_t fractionStart = m_Position;
            while (m_Position < m_Json.size() &&
                   m_Json[m_Position] >= '0' && m_Json[m_Position] <= '9')
            {
                ++m_Position;
            }
            if (m_Position == fractionStart)
                return false;
        }
        if (m_Position < m_Json.size() &&
            (m_Json[m_Position] == 'e' || m_Json[m_Position] == 'E'))
        {
            ++m_Position;
            if (m_Position < m_Json.size() &&
                (m_Json[m_Position] == '+' || m_Json[m_Position] == '-'))
            {
                ++m_Position;
            }
            const std::size_t exponentStart = m_Position;
            while (m_Position < m_Json.size() &&
                   m_Json[m_Position] >= '0' && m_Json[m_Position] <= '9')
            {
                ++m_Position;
            }
            if (m_Position == exponentStart)
                return false;
        }
        (*m_Numbers)[path] = std::string(m_Json.substr(start, m_Position - start));
        return true;
    }

    bool parseObject(const std::string& path, std::size_t depth)
    {
        ++m_Position;
        skipWhitespace();
        if (m_Position < m_Json.size() && m_Json[m_Position] == '}')
        {
            ++m_Position;
            return true;
        }
        while (m_Position < m_Json.size())
        {
            std::string key;
            if (!parseString(&key))
                return false;
            skipWhitespace();
            if (m_Position >= m_Json.size() || m_Json[m_Position++] != ':')
                return false;
            skipWhitespace();
            const std::string valuePath = childPath(path, escapedPathSegment(key));
            if (!m_SeenObjectKeys.insert(valuePath).second)
                m_DuplicateKeys->insert(valuePath);
            if (!parseValue(valuePath, depth + 1U))
                return false;
            skipWhitespace();
            if (m_Position >= m_Json.size())
                return false;
            if (m_Json[m_Position] == '}')
            {
                ++m_Position;
                return true;
            }
            if (m_Json[m_Position++] != ',')
                return false;
            skipWhitespace();
        }
        return false;
    }

    bool parseArray(const std::string& path, std::size_t depth)
    {
        ++m_Position;
        skipWhitespace();
        if (m_Position < m_Json.size() && m_Json[m_Position] == ']')
        {
            ++m_Position;
            return true;
        }
        std::size_t index = 0U;
        while (m_Position < m_Json.size())
        {
            if (!parseValue(childPath(path, std::to_string(index)), depth + 1U))
                return false;
            ++index;
            skipWhitespace();
            if (m_Position >= m_Json.size())
                return false;
            if (m_Json[m_Position] == ']')
            {
                ++m_Position;
                return true;
            }
            if (m_Json[m_Position++] != ',')
                return false;
            skipWhitespace();
        }
        return false;
    }

    bool parseValue(const std::string& path, std::size_t depth)
    {
        if (depth > kMaxDepth)
            return false;
        clearPath(path);
        skipWhitespace();
        if (m_Position >= m_Json.size())
            return false;
        switch (m_Json[m_Position])
        {
        case '{': return parseObject(path, depth);
        case '[': return parseArray(path, depth);
        case '"': return parseString(nullptr);
        case 't':
            if (m_Json.substr(m_Position, 4U) != "true") return false;
            m_Position += 4U;
            return true;
        case 'f':
            if (m_Json.substr(m_Position, 5U) != "false") return false;
            m_Position += 5U;
            return true;
        case 'n':
            if (m_Json.substr(m_Position, 4U) != "null") return false;
            m_Position += 4U;
            return true;
        default:
            if (m_Json[m_Position] == '-' ||
                (m_Json[m_Position] >= '0' && m_Json[m_Position] <= '9'))
            {
                return parseNumber(path);
            }
            return false;
        }
    }

    std::string_view m_Json;
    std::size_t m_Position = 0U;
    std::map<std::string, std::string>* m_Numbers = nullptr;
    std::set<std::string>* m_DuplicateKeys = nullptr;
    std::set<std::string> m_SeenObjectKeys;
};

bool ReadUnsigned(const QJsonValue& value,
                  std::uint64_t& result,
                  const std::map<std::string, std::string>& numbers,
                  const std::string& path)
{
    if (!value.isDouble())
        return false;
    const auto token = numbers.find(path);
    if (token == numbers.end())
        return false;
    const auto parsed = std::from_chars(
        token->second.data(), token->second.data() + token->second.size(), result);
    return parsed.ec == std::errc{} &&
           parsed.ptr == token->second.data() + token->second.size();
}

bool ReadColor(const QJsonValue& value, jaguar::SvgColor& color)
{
    if (!value.isString())
        return false;
    std::string text = value.toString().toStdString();
    if (!text.empty() && text.front() == '#')
        text.erase(text.begin());
    if (text.size() < 6U)
        return false;
    std::uint8_t components[3]{};
    for (std::size_t i = 0; i < 3U; ++i)
    {
        unsigned int component = 0U;
        const auto parsed = std::from_chars(
            text.data() + i * 2U, text.data() + i * 2U + 2U,
            component, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + i * 2U + 2U)
            return false;
        components[i] = static_cast<std::uint8_t>(component);
    }
    color = {components[0], components[1], components[2]};
    return true;
}

bool ParseTheme(const QJsonValue& value,
                sparrow::lbf_entry::RuntimeConfig& config,
                std::string& error)
{
    if (!value.isObject())
    {
        error = "svg_draw_options.theme must be an object";
        return false;
    }
    const QJsonObject theme = value.toObject();
    double number = 0.0;
    if (!ReadNumber(theme.value("stroke_width_multiplier"), number))
    {
        error = "svg_draw_options.theme.stroke_width_multiplier is invalid";
        return false;
    }
    number = RustF32(number);
    config.svg.theme.strokeWidthMultiplier = number;
    if (!ReadColor(theme.value("container_fill"), config.svg.theme.containerFill)
        || !ReadColor(theme.value("item_fill"), config.svg.theme.itemFill)
        || !ReadColor(theme.value("hole_fill"), config.holeFill)
        || !ReadColor(theme.value("collision_highlight_color"),
                      config.svg.theme.collisionHighlight))
    {
        error = "svg_draw_options.theme contains an invalid color";
        return false;
    }
    config.svg.theme.collisionStroke = config.svg.theme.collisionHighlight;

    const QJsonValue fillsValue = theme.value("qz_fill");
    if (!fillsValue.isArray() || fillsValue.toArray().size() != 10)
    {
        error = "svg_draw_options.theme.qz_fill must contain ten colors";
        return false;
    }
    for (int i = 0; i < 10; ++i)
    {
        if (!ReadColor(fillsValue.toArray().at(i),
                       config.svg.theme.qualityZoneFills[static_cast<std::size_t>(i)]))
        {
            error = "svg_draw_options.theme.qz_fill contains an invalid color";
            return false;
        }
    }
    if (!ReadNumber(theme.value("qz_stroke_opac"), number))
    {
        error = "svg_draw_options.theme.qz_stroke_opac is invalid";
        return false;
    }
    config.svg.theme.qualityZoneStrokeOpacity = RustF32(number);
    return true;
}

bool ParseConfigObject(const QJsonObject& object,
                       const std::map<std::string, std::string>& rawNumbers,
                       sparrow::lbf_entry::RuntimeConfig& config,
                       std::string& error)
{
    using sparrow::lbf_entry::RuntimeConfig;
    std::uint64_t unsignedValue = 0U;
    double number = 0.0;

    if (object.contains("cde_config"))
    {
        const QJsonValue cdeValue = object.value("cde_config");
        if (!cdeValue.isObject())
        {
            error = "cde_config must be an object";
            return false;
        }
        const QJsonObject cde = cdeValue.toObject();
        if (!ReadUnsigned(cde.value("quadtree_depth"), unsignedValue,
                          rawNumbers, "/cde_config/quadtree_depth")
            || unsignedValue > std::numeric_limits<std::uint8_t>::max())
        {
            error = "cde_config.quadtree_depth is invalid";
            return false;
        }
        config.importer.cdeConfig.quadtreeDepth =
            static_cast<std::uint8_t>(unsignedValue);
        if (!ReadUnsigned(cde.value("cd_threshold"), unsignedValue,
                          rawNumbers, "/cde_config/cd_threshold")
            || unsignedValue > std::numeric_limits<std::uint8_t>::max())
        {
            error = "cde_config.cd_threshold is invalid";
            return false;
        }
        config.importer.cdeConfig.cdThreshold =
            static_cast<std::uint8_t>(unsignedValue);

        const QJsonValue surrogateValue = cde.value("item_surrogate_config");
        if (!surrogateValue.isObject())
        {
            error = "cde_config.item_surrogate_config must be an object";
            return false;
        }
        const QJsonObject surrogate = surrogateValue.toObject();
        const QJsonValue limitsValue = surrogate.value("n_pole_limits");
        if (!limitsValue.isArray() || limitsValue.toArray().size() != 3)
        {
            error = "item_surrogate_config.n_pole_limits must have three pairs";
            return false;
        }
        for (int i = 0; i < 3; ++i)
        {
            const QJsonValue pairValue = limitsValue.toArray().at(i);
            if (!pairValue.isArray() || pairValue.toArray().size() != 2
                || !ReadUnsigned(
                    pairValue.toArray().at(0), unsignedValue, rawNumbers,
                    "/cde_config/item_surrogate_config/n_pole_limits/" +
                        std::to_string(i) + "/0"))
            {
                error = "item_surrogate_config.n_pole_limits contains an invalid pair";
                return false;
            }
            std::uint64_t count = unsignedValue;
            if (!ReadNumber(pairValue.toArray().at(1), number))
            {
                error = "item_surrogate_config.n_pole_limits contains an invalid ratio";
                return false;
            }
            number = RustF32(number);
            config.importer.cdeConfig.itemSurrogateConfig
                .poleLimits[static_cast<std::size_t>(i)] = {
                    static_cast<std::size_t>(count), number};
        }
        if (!ReadNumber(surrogate.value("ff_pole_area_ratio"), number))
        {
            error = "item_surrogate_config.ff_pole_area_ratio is invalid";
            return false;
        }
        config.importer.cdeConfig.itemSurrogateConfig.failFastPoleAreaRatio =
            RustF32(number);
        if (!ReadUnsigned(surrogate.value("n_ff_piers"), unsignedValue,
                          rawNumbers,
                          "/cde_config/item_surrogate_config/n_ff_piers"))
        {
            error = "item_surrogate_config.n_ff_piers is invalid";
            return false;
        }
        config.importer.cdeConfig.itemSurrogateConfig.failFastPiers =
            static_cast<std::size_t>(unsignedValue);
    }

    if (object.contains("poly_simpl_tolerance"))
    {
        const QJsonValue value = object.value("poly_simpl_tolerance");
        if (value.isNull())
            config.importer.shapeModifyConfig.simplifyTolerance.reset();
        else if (ReadNumber(value, number))
            config.importer.shapeModifyConfig.simplifyTolerance = RustF32(number);
        else
        {
            error = "poly_simpl_tolerance must be a number or null";
            return false;
        }
    }
    if (object.contains("narrow_concavity_cutoff"))
    {
        const QJsonValue value = object.value("narrow_concavity_cutoff");
        if (value.isNull())
            config.importer.shapeModifyConfig.narrowConcavityCutoff.reset();
        else if (value.isArray() && value.toArray().size() == 2
                 && ReadNumber(value.toArray().at(0), number))
        {
            const double first = RustF32(number);
            if (!ReadNumber(value.toArray().at(1), number))
            {
                error = "narrow_concavity_cutoff must contain two numbers";
                return false;
            }
            config.importer.shapeModifyConfig.narrowConcavityCutoff =
                std::pair<double, double>{first, RustF32(number)};
        }
        else
        {
            error = "narrow_concavity_cutoff must be a pair or null";
            return false;
        }
    }
    if (object.contains("prng_seed"))
    {
        const QJsonValue value = object.value("prng_seed");
        if (value.isNull())
            config.configuredSeed.reset();
        else if (ReadUnsigned(value, unsignedValue, rawNumbers, "/prng_seed"))
            config.configuredSeed = unsignedValue;
        else
        {
            error = "prng_seed must be an unsigned integer or null";
            return false;
        }
    }
    if (object.contains("n_samples"))
    {
        if (!ReadUnsigned(object.value("n_samples"), unsignedValue,
                          rawNumbers, "/n_samples")
            || unsignedValue > std::numeric_limits<std::size_t>::max())
        {
            error = "n_samples is invalid";
            return false;
        }
        config.optimizer.samplesPerItem = static_cast<std::size_t>(unsignedValue);
    }
    if (object.contains("ls_frac"))
    {
        if (!ReadNumber(object.value("ls_frac"), number))
        {
            error = "ls_frac is invalid";
            return false;
        }
        config.optimizer.localSearchFraction = RustF32(number);
    }

    if (object.contains("svg_draw_options"))
    {
        const QJsonValue svgValue = object.value("svg_draw_options");
        if (!svgValue.isObject())
        {
            error = "svg_draw_options must be an object";
            return false;
        }
        const QJsonObject svg = svgValue.toObject();
        if (svg.contains("theme")
            && !ParseTheme(svg.value("theme"), config, error))
            return false;
        const auto readBool = [&](const char* field, bool& result) {
            if (!svg.contains(field))
                return true;
            const QJsonValue value = svg.value(field);
            if (!value.isBool())
                return false;
            result = value.toBool();
            return true;
        };
        if (!readBool("quadtree", config.svgQuadtree)
            || !readBool("surrogate", config.svg.drawSurrogates)
            || !readBool("highlight_collisions", config.svg.highlightCollisions)
            || !readBool("draw_cd_shapes", config.svgDrawCdShapes)
            || !readBool("highlight_cd_shapes", config.svgHighlightCdShapes))
        {
            error = "svg_draw_options boolean field is invalid";
            return false;
        }
    }
    const auto validation = config.optimizer.Validate();
    if (!validation.HasValue())
    {
        error = validation.GetError().message;
        return false;
    }
    return true;
}

bool IsKnownLbfConfigFieldPath(const std::string& path)
{
    static const std::set<std::string> fields{
        "/cde_config",
        "/cde_config/quadtree_depth",
        "/cde_config/cd_threshold",
        "/cde_config/item_surrogate_config",
        "/cde_config/item_surrogate_config/n_pole_limits",
        "/cde_config/item_surrogate_config/ff_pole_area_ratio",
        "/cde_config/item_surrogate_config/n_ff_piers",
        "/poly_simpl_tolerance",
        "/narrow_concavity_cutoff",
        "/prng_seed",
        "/n_samples",
        "/ls_frac",
        "/svg_draw_options",
        "/svg_draw_options/theme",
        "/svg_draw_options/theme/stroke_width_multiplier",
        "/svg_draw_options/theme/container_fill",
        "/svg_draw_options/theme/item_fill",
        "/svg_draw_options/theme/hole_fill",
        "/svg_draw_options/theme/qz_fill",
        "/svg_draw_options/theme/qz_stroke_opac",
        "/svg_draw_options/theme/collision_highlight_color",
        "/svg_draw_options/quadtree",
        "/svg_draw_options/surrogate",
        "/svg_draw_options/highlight_collisions",
        "/svg_draw_options/draw_cd_shapes",
        "/svg_draw_options/highlight_cd_shapes",
    };
    return fields.find(path) != fields.end();
}

bool ParseConfig(const std::string& json,
                 sparrow::lbf_entry::RuntimeConfig& config,
                 std::string& error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error = "incorrect config file format";
        return false;
    }
    std::map<std::string, std::string> rawNumbers;
    std::set<std::string> duplicateKeys;
    if (!RawJsonNumberScanner(json).Scan(rawNumbers, duplicateKeys))
    {
        error = "incorrect config file format";
        return false;
    }
    for (const std::string& path : duplicateKeys)
    {
        if (IsKnownLbfConfigFieldPath(path))
        {
            error = "duplicate config field: " + path;
            return false;
        }
    }
    return ParseConfigObject(document.object(), rawNumbers, config, error);
}

bool EnsureDirectory(const std::filesystem::path& path,
                     std::string& error)
{
    std::error_code code;
    std::filesystem::create_directories(path, code);
    if (code)
    {
        error = "could not create solution folder: " + path.string();
        return false;
    }
    return true;
}

bool WriteFile(const std::filesystem::path& path,
               const std::string& contents,
               std::string& error)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        error = "could not write output file: " + path.string();
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output.good())
    {
        error = "failed while writing output file: " + path.string();
        return false;
    }
    return true;
}

int LogRank(const std::string& level)
{
    if (level == "off") return 0;
    if (level == "error") return 1;
    if (level == "warn") return 2;
    if (level == "info") return 3;
    if (level == "debug") return 4;
    return 5;
}

int Run(int argc, char** argv)
{
    Arguments args;
    if (!ParseArguments(argc, argv, args))
        return 2;
    if (args.help)
        return 0;
    if (args.version)
        return 0;

    const int logRank = LogRank(args.logLevel);
    std::string inputJson;
    if (!ReadFile(args.inputFile, inputJson))
    {
        std::cerr << "could not open instance file: " << args.inputFile << '\n';
        return 1;
    }
    auto config = sparrow::lbf_entry::RustDefaultRuntimeConfig();
    if (args.configFile.has_value())
    {
        std::string configJson;
        if (!ReadFile(*args.configFile, configJson))
        {
            std::cerr << "could not open config file: " << *args.configFile << '\n';
            return 1;
        }
        std::string configError;
        if (!ParseConfig(configJson, config, configError))
        {
            std::cerr << configError << '\n';
            return 1;
        }
    }
    else if (logRank >= 2)
    {
        std::cout << "[WARN] No config file provided, use --config-file to provide a custom config\n";
    }

    const std::uint64_t effectiveSeed = config.configuredSeed.value_or(
        sparrow::lbf_entry::RandomSeed());
    config.optimizer.seed = effectiveSeed;
    if (logRank >= 3)
    {
        std::cout << "[INFO] LBF config: "
                  << sparrow::lbf_entry::SerializeRuntimeConfig(config) << '\n';
    }

    const std::filesystem::path inputPath(args.inputFile);
    const std::string inputStem = inputPath.stem().string();
    if (inputStem.empty())
    {
        std::cerr << "could not determine input file stem\n";
        return 1;
    }
    const std::filesystem::path solutionFolder(args.solutionFolder);
    std::string error;
    if (!EnsureDirectory(solutionFolder, error))
    {
        std::cerr << error << '\n';
        return 1;
    }

    sparrow::lbf_entry::DiagnosticCallback diagnostics;
    if (logRank >= 4)
    {
        diagnostics = [](const std::string& message) {
            std::cout << "[DEBUG] " << message << '\n';
        };
    }
    sparrow::Result<sparrow::lbf_entry::Artifact> result =
        args.problemVariant == "spp"
            ? sparrow::lbf_entry::RunSppLbf(
                  inputJson, config, effectiveSeed,
                  "sol_" + inputStem + ".svg", diagnostics)
            : sparrow::lbf_entry::RunBppLbf(
                  inputJson, config, effectiveSeed,
                  "sol_" + inputStem, diagnostics);
    if (!result.HasValue())
    {
        std::cerr << "LBF solve failed: " << result.GetError().message << '\n';
        return 1;
    }

    const std::filesystem::path outputPath = solutionFolder
        / ("sol_" + inputStem + ".json");
    if (!WriteFile(outputPath, result.Value().outputJson, error))
    {
        std::cerr << error << '\n';
        return 1;
    }
    for (const auto& [fileName, svg] : result.Value().svgs)
    {
        if (!WriteFile(solutionFolder / fileName, svg, error))
        {
            std::cerr << error << '\n';
            return 1;
        }
        if (logRank >= 3)
            std::cout << "[INFO] SVG written: " << (solutionFolder / fileName).string() << '\n';
    }
    if (logRank >= 3)
    {
        std::cout << "[INFO] Solution JSON written: " << outputPath.string()
                  << "\n[INFO] seed=" << effectiveSeed
                  << " samples=" << result.Value().metrics.sampleCount
                  << " placed=" << result.Value().metrics.placedItemCount
                  << " density=" << result.Value().metrics.density << '\n';
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    try
    {
        return Run(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "LBF CLI error: " << error.what() << '\n';
        return 1;
    }
}
