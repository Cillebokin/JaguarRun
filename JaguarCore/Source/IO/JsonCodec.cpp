#include "JaguarCore/IO/JsonCodec.h"

#include "JaguarCore/Assertions.h"

#include "JaguarCore/Entities/Layout.h"
#include "JaguarCore/Geometry/RustF32.h"
#include "JaguarCore/Problems/Bpp.h"
#include "JaguarCore/Problems/MSpp.h"
#include "JaguarCore/Problems/Spp.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace jaguar
{
namespace
{
Result<ExternalLayout> ExternalLayoutFromSnapshot(
    const LayoutSnapshot& snapshot, ContainerId containerId)
{
    Layout layout(snapshot.container);
    const auto restored = layout.Restore(snapshot);
    if (!restored.HasValue())
        return Result<ExternalLayout>::FromError(restored.GetError());
    return ExternalLayoutFromLayout(layout, containerId);
}

ExternalContainer externalRectangleContainer(const Container& container,
                                             ContainerId id)
{
    const Rect2d bounds = container.GetOriginalShape().shape.BoundingBox();
    return ExternalContainer{
        id,
        ExternalShape::Rectangle(bounds.xMin, bounds.yMin,
                                 bounds.Width(), bounds.Height()),
        {}};
}

struct JsonValue
{
    using Array = std::vector<JsonValue>;
    struct Object : std::map<std::string, JsonValue>
    {
        using Base = std::map<std::string, JsonValue>;
        using Base::Base;

        std::set<std::string> duplicateKeys;
    };
    using Storage = std::variant<std::nullptr_t,
                                 bool,
                                 double,
                                 std::uint64_t,
                                 std::string,
                                 Array,
                                 Object>;

    JsonValue() : value(nullptr) {}
    explicit JsonValue(bool valueIn) : value(valueIn) {}
    explicit JsonValue(double valueIn) : value(valueIn) {}
    explicit JsonValue(std::uint64_t valueIn) : value(valueIn) {}
    explicit JsonValue(std::string valueIn) : value(std::move(valueIn)) {}
    explicit JsonValue(Array valueIn) : value(std::move(valueIn)) {}
    explicit JsonValue(Object valueIn) : value(std::move(valueIn)) {}

    Storage value;
};

class JsonParser
{
public:
    explicit JsonParser(const std::string& input)
        : m_Input(input)
    {
    }

    bool parse(JsonValue& value)
    {
        skipWhitespace();
        if (!parseValue(value))
            return false;
        skipWhitespace();
        if (m_Position != m_Input.size())
            return fail("unexpected trailing data");
        return true;
    }

    const std::string& GetError() const noexcept { return m_Error; }
    std::size_t errorPosition() const noexcept { return m_ErrorPosition; }

private:
    bool fail(const char* message)
    {
        if (m_Error.empty())
        {
            m_Error = message;
            m_ErrorPosition = m_Position;
        }
        return false;
    }

    bool fail(const std::string& message)
    {
        if (m_Error.empty())
        {
            m_Error = message;
            m_ErrorPosition = m_Position;
        }
        return false;
    }

    void skipWhitespace() noexcept
    {
        while (m_Position < m_Input.size())
        {
            const char character = m_Input[m_Position];
            if (character != ' ' && character != '\t' && character != '\r' &&
                character != '\n')
                break;
            ++m_Position;
        }
    }

    bool consume(char expected)
    {
        if (m_Position >= m_Input.size() || m_Input[m_Position] != expected)
            return fail(std::string("expected '") + expected + "'");
        ++m_Position;
        return true;
    }

    bool parseValue(JsonValue& value)
    {
        if (m_Position >= m_Input.size())
            return fail("unexpected end of input");

        switch (m_Input[m_Position])
        {
        case 'n':
            if (!consumeLiteral("null"))
                return false;
            value = JsonValue{};
            return true;
        case 't':
            if (!consumeLiteral("true"))
                return false;
            value = JsonValue(true);
            return true;
        case 'f':
            if (!consumeLiteral("false"))
                return false;
            value = JsonValue(false);
            return true;
        case '"':
        {
            std::string stringValue;
            if (!parseString(stringValue))
                return false;
            value = JsonValue(std::move(stringValue));
            return true;
        }
        case '[':
            return parseArray(value);
        case '{':
            return parseObject(value);
        default:
            if (m_Input[m_Position] == '-' ||
                (m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9'))
                return parseNumber(value);
            return fail("unexpected character");
        }
    }

    bool consumeLiteral(const char* literal)
    {
        const std::string expected(literal);
        if (m_Input.compare(m_Position, expected.size(), expected) != 0)
            return fail("invalid literal");
        m_Position += expected.size();
        return true;
    }

    bool parseArray(JsonValue& value)
    {
        if (!consume('['))
            return false;
        JsonValue::Array array;
        skipWhitespace();
        if (m_Position < m_Input.size() && m_Input[m_Position] == ']')
        {
            ++m_Position;
            value = JsonValue(std::move(array));
            return true;
        }

        while (true)
        {
            skipWhitespace();
            JsonValue element;
            if (!parseValue(element))
                return false;
            array.push_back(std::move(element));
            skipWhitespace();
            if (m_Position >= m_Input.size())
                return fail("unterminated array");
            if (m_Input[m_Position] == ']')
            {
                ++m_Position;
                value = JsonValue(std::move(array));
                return true;
            }
            if (!consume(','))
                return false;
        }
    }

    bool parseObject(JsonValue& value)
    {
        if (!consume('{'))
            return false;
        JsonValue::Object object;
        skipWhitespace();
        if (m_Position < m_Input.size() && m_Input[m_Position] == '}')
        {
            ++m_Position;
            value = JsonValue(std::move(object));
            return true;
        }

        while (true)
        {
            skipWhitespace();
            if (m_Position >= m_Input.size() || m_Input[m_Position] != '"')
                return fail("object key must be a string");

            std::string key;
            if (!parseString(key))
                return false;
            skipWhitespace();
            if (!consume(':'))
                return false;
            skipWhitespace();
            JsonValue member;
            if (!parseValue(member))
                return false;
            if (!object.emplace(key, std::move(member)).second)
                object.duplicateKeys.emplace(std::move(key));
            skipWhitespace();
            if (m_Position >= m_Input.size())
                return fail("unterminated object");
            if (m_Input[m_Position] == '}')
            {
                ++m_Position;
                value = JsonValue(std::move(object));
                return true;
            }
            if (!consume(','))
                return false;
        }
    }

    static bool isHex(char character) noexcept
    {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
    }

    static std::uint32_t hexValue(char character) noexcept
    {
        if (character >= '0' && character <= '9')
            return static_cast<std::uint32_t>(character - '0');
        if (character >= 'a' && character <= 'f')
            return static_cast<std::uint32_t>(character - 'a' + 10);
        return static_cast<std::uint32_t>(character - 'A' + 10);
    }

    bool parseHex4(std::uint32_t& value)
    {
        if (m_Position + 4 > m_Input.size())
            return fail("incomplete unicode escape");
        value = 0;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const char character = m_Input[m_Position++];
            if (!isHex(character))
                return fail("invalid unicode escape");
            value = value * 16 + hexValue(character);
        }
        return true;
    }

    static void appendUtf8(std::string& output, std::uint32_t codePoint)
    {
        if (codePoint <= 0x7f)
        {
            output.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7ff)
        {
            output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
        else if (codePoint <= 0xffff)
        {
            output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
        else
        {
            output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }

    bool parseString(std::string& output)
    {
        if (!consume('"'))
            return false;
        output.clear();
        while (m_Position < m_Input.size())
        {
            const unsigned char character =
                static_cast<unsigned char>(m_Input[m_Position++]);
            if (character == '"')
                return true;
            if (character < 0x20)
                return fail("unescaped control character in string");
            if (character != '\\')
            {
                output.push_back(static_cast<char>(character));
                continue;
            }

            if (m_Position >= m_Input.size())
                return fail("incomplete string escape");
            const char escape = m_Input[m_Position++];
            switch (escape)
            {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
            {
                std::uint32_t codePoint = 0;
                if (!parseHex4(codePoint))
                    return false;
                if (codePoint >= 0xd800 && codePoint <= 0xdbff)
                {
                    if (m_Position + 6 > m_Input.size() ||
                        m_Input[m_Position] != '\\' ||
                        m_Input[m_Position + 1] != 'u')
                        return fail("unicode high surrogate is not followed by a low surrogate");
                    m_Position += 2;
                    std::uint32_t low = 0;
                    if (!parseHex4(low) || low < 0xdc00 || low > 0xdfff)
                        return fail("invalid unicode low surrogate");
                    codePoint = 0x10000 + ((codePoint - 0xd800) << 10) +
                                (low - 0xdc00);
                }
                else if (codePoint >= 0xdc00 && codePoint <= 0xdfff)
                {
                    return fail("unexpected unicode low surrogate");
                }
                appendUtf8(output, codePoint);
                break;
            }
            default:
                return fail("invalid string escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber(JsonValue& value)
    {
        const std::size_t start = m_Position;
        if (m_Input[m_Position] == '-')
            ++m_Position;

        if (m_Position >= m_Input.size())
            return fail("incomplete number");
        if (m_Input[m_Position] == '0')
        {
            ++m_Position;
            if (m_Position < m_Input.size() &&
                m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                return fail("leading zero in number");
        }
        else if (m_Input[m_Position] >= '1' && m_Input[m_Position] <= '9')
        {
            while (m_Position < m_Input.size() &&
                   m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                ++m_Position;
        }
        else
        {
            return fail("invalid number");
        }

        bool isInteger = m_Input[start] != '-';
        if (m_Position < m_Input.size() && m_Input[m_Position] == '.')
        {
            isInteger = false;
            ++m_Position;
            if (m_Position >= m_Input.size() ||
                m_Input[m_Position] < '0' || m_Input[m_Position] > '9')
                return fail("fraction requires digits");
            while (m_Position < m_Input.size() &&
                   m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                ++m_Position;
        }
        if (m_Position < m_Input.size() &&
            (m_Input[m_Position] == 'e' || m_Input[m_Position] == 'E'))
        {
            isInteger = false;
            ++m_Position;
            if (m_Position < m_Input.size() &&
                (m_Input[m_Position] == '+' || m_Input[m_Position] == '-'))
                ++m_Position;
            if (m_Position >= m_Input.size() ||
                m_Input[m_Position] < '0' || m_Input[m_Position] > '9')
                return fail("exponent requires digits");
            while (m_Position < m_Input.size() &&
                   m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9')
                ++m_Position;
        }

        const std::string token = m_Input.substr(start, m_Position - start);
        if (isInteger)
        {
            errno = 0;
            char* end = nullptr;
            const unsigned long long integerValue =
                std::strtoull(token.c_str(), &end, 10);
            if (errno == 0 && end == token.c_str() + token.size())
            {
                value = JsonValue(static_cast<std::uint64_t>(integerValue));
                return true;
            }
        }

        errno = 0;
        char* end = nullptr;
        const double numberValue = std::strtod(token.c_str(), &end);
        if (errno == ERANGE || end != token.c_str() + token.size() ||
            !std::isfinite(numberValue))
            return fail("number is outside the supported finite range");
        value = JsonValue(numberValue);
        return true;
    }

    const std::string& m_Input;
    std::size_t m_Position = 0;
    std::string m_Error;
    std::size_t m_ErrorPosition = 0;
};

class JsonWriter
{
public:
    explicit JsonWriter(bool pretty)
        : m_Pretty(pretty)
    {
    }

    bool write(const JsonValue& value)
    {
        return writeValue(value, 0);
    }

    const std::string& GetError() const noexcept { return m_Error; }

    std::string takeResult() { return m_Output.str(); }

private:
    bool fail(const char* message)
    {
        if (m_Error.empty())
            m_Error = message;
        return false;
    }

    static std::string escaped(const std::string& input)
    {
        std::ostringstream output;
        output << '"';
        for (unsigned char character : input)
        {
            switch (character)
            {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20)
                {
                    output << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<int>(character)
                           << std::dec << std::setfill(' ');
                }
                else
                {
                    output << static_cast<char>(character);
                }
                break;
            }
        }
        output << '"';
        return output.str();
    }

    void indent(std::size_t level)
    {
        if (m_Pretty)
            m_Output << std::string(level * 2, ' ');
    }

    void lineBreak()
    {
        if (m_Pretty)
            m_Output << '\n';
    }

    bool writeValue(const JsonValue& value, std::size_t level)
    {
        if (std::holds_alternative<std::nullptr_t>(value.value))
        {
            m_Output << "null";
            return true;
        }
        if (const auto* boolean = std::get_if<bool>(&value.value))
        {
            m_Output << (*boolean ? "true" : "false");
            return true;
        }
        if (const auto* number = std::get_if<double>(&value.value))
        {
            if (!std::isfinite(*number))
                return fail("cannot serialize a non-finite number");
            m_Output << std::setprecision(17) << *number;
            return true;
        }
        if (const auto* integer = std::get_if<std::uint64_t>(&value.value))
        {
            m_Output << *integer;
            return true;
        }
        if (const auto* stringValue = std::get_if<std::string>(&value.value))
        {
            m_Output << escaped(*stringValue);
            return true;
        }
        if (const auto* array = std::get_if<JsonValue::Array>(&value.value))
        {
            m_Output << '[';
            if (!array->empty())
            {
                lineBreak();
                for (std::size_t i = 0; i < array->size(); ++i)
                {
                    indent(level + 1);
                    if (!writeValue((*array)[i], level + 1))
                        return false;
                    if (i + 1 != array->size())
                        m_Output << ',';
                    lineBreak();
                }
                indent(level);
            }
            m_Output << ']';
            return true;
        }

        const auto* object = std::get_if<JsonValue::Object>(&value.value);
        if (!object)
            return fail("unknown JSON value type");
        m_Output << '{';
        if (!object->empty())
        {
            lineBreak();
            std::size_t index = 0;
            for (const auto& entry : *object)
            {
                indent(level + 1);
                m_Output << escaped(entry.first);
                m_Output << (m_Pretty ? ": " : ":");
                if (!writeValue(entry.second, level + 1))
                    return false;
                if (++index != object->size())
                    m_Output << ',';
                lineBreak();
            }
            indent(level);
        }
        m_Output << '}';
        return true;
    }

    bool m_Pretty = false;
    std::ostringstream m_Output;
    std::string m_Error;
};

template <typename T>
Result<T> serializationError(const std::string& message)
{
    return Result<T>::FromError({ErrorCode::SerializationFailure, message});
}

Result<std::string> writeJson(const JsonValue& value, JsonCodecOptions options)
{
    JsonWriter writer(options.pretty);
    if (!writer.write(value))
        return serializationError<std::string>(writer.GetError());
    return Result<std::string>::FromValue(writer.takeResult());
}

template <typename T, typename ParseFunction>
Result<T> parseJson(const std::string& json, ParseFunction parseFunction)
{
    JsonParser parser(json);
    JsonValue root;
    if (!parser.parse(root))
    {
        return serializationError<T>(
            "JSON parse error at byte " + std::to_string(parser.errorPosition()) +
            ": " + parser.GetError());
    }

    T result{};
    std::string error;
    if (!parseFunction(root, result, error))
        return serializationError<T>(error);
    return Result<T>::FromValue(std::move(result));
}

const JsonValue::Object* objectValue(const JsonValue& value,
                                     const char* name,
                                     std::string& error)
{
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (!object)
    {
        error = std::string(name) + " must be an object";
        return nullptr;
    }
    return object;
}

const JsonValue::Array* arrayValue(const JsonValue& value,
                                   const char* name,
                                   std::string& error)
{
    const auto* array = std::get_if<JsonValue::Array>(&value.value);
    if (!array)
    {
        error = std::string(name) + " must be an array";
        return nullptr;
    }
    return array;
}

const JsonValue* requiredMember(const JsonValue::Object& object,
                                const char* name,
                                std::string& error)
{
    const auto found = object.find(name);
    if (found == object.end())
    {
        error = std::string("missing required field '") + name + "'";
        return nullptr;
    }
    return &found->second;
}

bool rejectDuplicateFields(const JsonValue& value,
                           std::initializer_list<const char*> knownFields,
                           std::string& error)
{
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (!object)
        return false;
    for (const char* field : knownFields)
    {
        if (object->duplicateKeys.find(field) == object->duplicateKeys.end())
            continue;
        error = std::string("duplicate field '") + field + "'";
        return true;
    }
    return false;
}

bool numberValue(const JsonValue& value,
                 double& result,
                 const char* name,
                 std::string& error)
{
    if (const auto* number = std::get_if<double>(&value.value))
    {
        if (!std::isfinite(*number))
        {
            error = std::string(name) + " must be finite";
            return false;
        }
        result = static_cast<double>(static_cast<float>(*number));
        if (!std::isfinite(result))
        {
            error = std::string(name) + " must be finite";
            return false;
        }
        return true;
    }
    if (const auto* integer = std::get_if<std::uint64_t>(&value.value))
    {
        result = static_cast<double>(static_cast<float>(*integer));
        if (!std::isfinite(result))
        {
            error = std::string(name) + " must be finite";
            return false;
        }
        return true;
    }
    error = std::string(name) + " must be a number";
    return false;
}

bool unsignedValue(const JsonValue& value,
                   std::uint64_t& result,
                   const char* name,
                   std::string& error)
{
    if (const auto* integer = std::get_if<std::uint64_t>(&value.value))
    {
        result = *integer;
        return true;
    }
    error = std::string(name) + " must be a nonnegative integer";
    return false;
}

bool stringValue(const JsonValue& value,
                 std::string& result,
                 const char* name,
                 std::string& error)
{
    const auto* string = std::get_if<std::string>(&value.value);
    if (!string)
    {
        error = std::string(name) + " must be a string";
        return false;
    }
    result = *string;
    return true;
}

bool pointValue(const JsonValue& value,
                Point2d& point,
                const char* name,
                std::string& error)
{
    const auto* array = arrayValue(value, name, error);
    if (!array || array->size() != 2)
    {
        if (array)
            error = std::string(name) + " must contain exactly two coordinates";
        return false;
    }
    if (!numberValue((*array)[0], point.x, "point.x", error) ||
        !numberValue((*array)[1], point.y, "point.y", error))
        return false;
    if (!point.IsFinite())
    {
        error = std::string(name) + " coordinates must be finite";
        return false;
    }
    return true;
}

JsonValue pointJson(const Point2d& point)
{
    return JsonValue(JsonValue::Array{JsonValue(point.x), JsonValue(point.y)});
}

bool pointsValue(const JsonValue& value,
                 std::vector<Point2d>& points,
                 const char* name,
                 std::string& error)
{
    const auto* array = arrayValue(value, name, error);
    if (!array)
        return false;
    points.clear();
    points.reserve(array->size());
    for (std::size_t i = 0; i < array->size(); ++i)
    {
        Point2d point{};
        if (!pointValue((*array)[i], point, "polygon point", error))
        {
            error = std::string(name) + "[" + std::to_string(i) + "]: " + error;
            return false;
        }
        points.push_back(point);
    }
    return true;
}

JsonValue pointsJson(const std::vector<Point2d>& points)
{
    JsonValue::Array array;
    array.reserve(points.size());
    for (const Point2d& point : points)
        array.emplace_back(pointJson(point));
    return JsonValue(std::move(array));
}

bool parsePolygonData(const JsonValue& value,
                      ExternalPolygon& polygon,
                      std::string& error)
{
    if (rejectDuplicateFields(value, {"outer", "inner"}, error))
        return false;
    const auto* object = objectValue(value, "polygon data", error);
    if (!object)
        return false;
    const JsonValue* outer = requiredMember(*object, "outer", error);
    if (!outer || !pointsValue(*outer, polygon.outer, "outer", error))
        return false;

    polygon.holes.clear();
    const auto inner = object->find("inner");
    if (inner == object->end())
        return true;
    const auto* holes = arrayValue(inner->second, "inner", error);
    if (!holes)
        return false;
    polygon.holes.reserve(holes->size());
    for (std::size_t i = 0; i < holes->size(); ++i)
    {
        std::vector<Point2d> hole;
        if (!pointsValue((*holes)[i], hole, "inner polygon", error))
        {
            error = "inner[" + std::to_string(i) + "]: " + error;
            return false;
        }
        polygon.holes.push_back(std::move(hole));
    }
    return true;
}

bool parseShapeValue(const JsonValue& value,
                     ExternalShape& shape,
                     std::string& error)
{
    if (rejectDuplicateFields(value, {"type", "data"}, error))
        return false;
    const auto* object = objectValue(value, "shape", error);
    if (!object)
        return false;
    const JsonValue* typeValue = requiredMember(*object, "type", error);
    const JsonValue* dataValue = requiredMember(*object, "data", error);
    if (!typeValue || !dataValue)
        return false;
    std::string type;
    if (!stringValue(*typeValue, type, "shape.type", error))
        return false;

    if (type == "rectangle")
    {
        if (rejectDuplicateFields(*dataValue,
                                  {"x_min", "y_min", "width", "height"}, error))
            return false;
        const auto* data = objectValue(*dataValue, "rectangle data", error);
        if (!data)
            return false;
        ExternalRectangle rectangle{};
        const JsonValue* xMin = requiredMember(*data, "x_min", error);
        const JsonValue* yMin = requiredMember(*data, "y_min", error);
        const JsonValue* width = requiredMember(*data, "width", error);
        const JsonValue* height = requiredMember(*data, "height", error);
        if (!xMin || !yMin || !width || !height ||
            !numberValue(*xMin, rectangle.xMin, "x_min", error) ||
            !numberValue(*yMin, rectangle.yMin, "y_min", error) ||
            !numberValue(*width, rectangle.width, "width", error) ||
            !numberValue(*height, rectangle.height, "height", error))
            return false;
        shape = ExternalShape::Rectangle(rectangle.xMin, rectangle.yMin,
                                          rectangle.width, rectangle.height);
        return true;
    }
    if (type == "simple_polygon")
    {
        std::vector<Point2d> points;
        if (!pointsValue(*dataValue, points, "shape.data", error))
            return false;
        shape = ExternalShape::SimplePolygon(std::move(points));
        return true;
    }
    if (type == "polygon")
    {
        ExternalPolygon polygon{};
        if (!parsePolygonData(*dataValue, polygon, error))
            return false;
        shape = ExternalShape::Polygon(std::move(polygon.outer),
                                       std::move(polygon.holes));
        return true;
    }
    if (type == "multi_polygon")
    {
        const auto* data = arrayValue(*dataValue, "multi_polygon data", error);
        if (!data)
            return false;
        std::vector<ExternalPolygon> polygons;
        polygons.reserve(data->size());
        for (std::size_t i = 0; i < data->size(); ++i)
        {
            ExternalPolygon polygon{};
            if (!parsePolygonData((*data)[i], polygon, error))
            {
                error = "multi_polygon[" + std::to_string(i) + "]: " + error;
                return false;
            }
            polygons.push_back(std::move(polygon));
        }
        shape = ExternalShape::MultiPolygon(std::move(polygons));
        return true;
    }

    error = "unsupported shape type '" + type + "'";
    return false;
}

JsonValue polygonJson(const ExternalPolygon& polygon)
{
    JsonValue::Object data;
    data.emplace("outer", pointsJson(polygon.outer));
    JsonValue::Array inner;
    inner.reserve(polygon.holes.size());
    for (const auto& hole : polygon.holes)
        inner.emplace_back(pointsJson(hole));
    data.emplace("inner", JsonValue(std::move(inner)));
    return JsonValue(std::move(data));
}

JsonValue shapeJson(const ExternalShape& shape)
{
    JsonValue::Object object;
    std::visit(
        [&object](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ExternalRectangle>)
            {
                object.emplace("type", JsonValue(std::string("rectangle")));
                JsonValue::Object data;
                data.emplace("x_min", JsonValue(value.xMin));
                data.emplace("y_min", JsonValue(value.yMin));
                data.emplace("width", JsonValue(value.width));
                data.emplace("height", JsonValue(value.height));
                object.emplace("data", JsonValue(std::move(data)));
            }
            else if constexpr (std::is_same_v<T, ExternalSimplePolygon>)
            {
                object.emplace("type", JsonValue(std::string("simple_polygon")));
                object.emplace("data", pointsJson(value.points));
            }
            else if constexpr (std::is_same_v<T, ExternalPolygon>)
            {
                object.emplace("type", JsonValue(std::string("polygon")));
                object.emplace("data", polygonJson(value));
            }
            else
            {
                object.emplace("type", JsonValue(std::string("multi_polygon")));
                JsonValue::Array data;
                data.reserve(value.polygons.size());
                for (const ExternalPolygon& polygon : value.polygons)
                    data.emplace_back(polygonJson(polygon));
                object.emplace("data", JsonValue(std::move(data)));
            }
        },
        shape.value);
    return JsonValue(std::move(object));
}

bool parseRotationValue(const JsonValue& value,
                        RotationPolicy& rotation,
                        std::string& error)
{
    if (rejectDuplicateFields(value, {"rotation"}, error))
        return false;
    const auto* object = objectValue(value, "orientation", error);
    if (!object)
        return false;
    if (object->size() != 1 || object->find("rotation") == object->end())
    {
        error = "orientation must contain only the required 'rotation' field";
        return false;
    }
    const auto* rotationObject = objectValue(object->at("rotation"),
                                             "orientation.rotation", error);
    if (!rotationObject)
        return false;
    if (rejectDuplicateFields(object->at("rotation"), {"mode"}, error))
        return false;
    const JsonValue* modeValue = requiredMember(*rotationObject, "mode", error);
    if (!modeValue)
        return false;
    std::string mode;
    if (!stringValue(*modeValue, mode, "rotation.mode", error))
        return false;

    if (mode == "continuous")
    {
        if (rotationObject->size() != 1)
        {
            error = "continuous rotation does not accept additional fields";
            return false;
        }
        rotation = RotationPolicy::Continuous();
        return true;
    }
    if (mode == "discrete")
    {
        if (rejectDuplicateFields(object->at("rotation"), {"mode", "angles"}, error))
            return false;
        if (rotationObject->size() != 2)
        {
            error = "discrete rotation requires only the angles field";
            return false;
        }
        const JsonValue* anglesValue = requiredMember(*rotationObject, "angles", error);
        if (!anglesValue)
            return false;
        const auto* angles = arrayValue(*anglesValue, "rotation.angles", error);
        if (!angles)
            return false;
        std::vector<double> values;
        values.reserve(angles->size());
        for (std::size_t i = 0; i < angles->size(); ++i)
        {
            double angle = 0.0;
            if (!numberValue((*angles)[i], angle, "rotation angle", error))
                return false;
            values.push_back(angle);
        }
        rotation = RotationPolicy::Discrete(std::move(values));
        return true;
    }
    if (mode == "stepped")
    {
        if (rejectDuplicateFields(object->at("rotation"), {"mode", "step"}, error))
            return false;
        if (rotationObject->size() != 2)
        {
            error = "stepped rotation requires only the step field";
            return false;
        }
        const JsonValue* stepValue = requiredMember(*rotationObject, "step", error);
        double step = 0.0;
        if (!stepValue || !numberValue(*stepValue, step, "rotation.step", error))
            return false;
        rotation = RotationPolicy::Stepped(step);
        return true;
    }

    error = "unsupported rotation mode '" + mode + "'";
    return false;
}

JsonValue rotationJson(const RotationPolicy& rotation)
{
    JsonValue::Object rotationObject;
    switch (rotation.kind)
    {
    case RotationPolicyKind::None:
        rotationObject.emplace("mode", JsonValue(std::string("discrete")));
        rotationObject.emplace("angles", JsonValue(JsonValue::Array{
                                                     JsonValue(0.0)}));
        break;
    case RotationPolicyKind::Discrete:
    {
        rotationObject.emplace("mode", JsonValue(std::string("discrete")));
        JsonValue::Array angles;
        angles.reserve(rotation.anglesDegrees.size());
        for (double angle : rotation.anglesDegrees)
            angles.emplace_back(JsonValue(angle));
        rotationObject.emplace("angles", JsonValue(std::move(angles)));
        break;
    }
    case RotationPolicyKind::Stepped:
        rotationObject.emplace("mode", JsonValue(std::string("stepped")));
        rotationObject.emplace("step", JsonValue(rotation.stepDegrees));
        break;
    case RotationPolicyKind::Continuous:
        rotationObject.emplace("mode", JsonValue(std::string("continuous")));
        break;
    }

    JsonValue::Object orientation;
    orientation.emplace("rotation", JsonValue(std::move(rotationObject)));
    return JsonValue(std::move(orientation));
}

bool parseItemValue(const JsonValue& value,
                    ExternalItem& item,
                    std::string& error)
{
    if (rejectDuplicateFields(value,
                              {"id", "orientation", "shape", "min_quality",
                               "allowed_orientations", "allowed_rotations",
                               "allowed_reflection_axes"}, error))
        return false;
    const auto* object = objectValue(value, "item", error);
    if (!object)
        return false;
    for (const char* legacy : {"allowed_orientations", "allowed_rotations",
                               "allowed_reflection_axes"})
    {
        if (object->find(legacy) != object->end())
        {
            error = "legacy orientation fields are not supported; use orientation.rotation";
            return false;
        }
    }

    const JsonValue* idValue = requiredMember(*object, "id", error);
    const JsonValue* orientationValue = requiredMember(*object, "orientation", error);
    const JsonValue* shapeValue = requiredMember(*object, "shape", error);
    std::uint64_t id = 0;
    if (!idValue || !unsignedValue(*idValue, id, "item.id", error))
        return false;
    if (!orientationValue || !parseRotationValue(*orientationValue, item.rotation, error))
        return false;
    if (!shapeValue || !parseShapeValue(*shapeValue, item.shape, error))
        return false;

    item.id = id;
    item.minQuality.reset();
    const auto minQuality = object->find("min_quality");
    if (minQuality != object->end() &&
        !std::holds_alternative<std::nullptr_t>(minQuality->second.value))
    {
        std::uint64_t quality = 0;
        if (!unsignedValue(minQuality->second, quality, "min_quality", error) ||
            quality > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        {
            if (error.empty())
                error = "min_quality is outside the supported range";
            return false;
        }
        item.minQuality = static_cast<std::size_t>(quality);
    }
    return true;
}

JsonValue itemJson(const ExternalItem& item)
{
    JsonValue::Object object;
    object.emplace("id", JsonValue(item.id));
    object.emplace("orientation", rotationJson(item.rotation));
    object.emplace("shape", shapeJson(item.shape));
    if (item.minQuality.has_value())
        object.emplace("min_quality", JsonValue(static_cast<std::uint64_t>(*item.minQuality)));
    return JsonValue(std::move(object));
}

bool parseQualityZones(const JsonValue& value,
                       std::vector<ExternalQualityZone>& zones,
                       std::string& error)
{
    const auto* array = arrayValue(value, "zones", error);
    if (!array)
        return false;
    zones.clear();
    zones.reserve(array->size());
    for (std::size_t i = 0; i < array->size(); ++i)
    {
        if (rejectDuplicateFields((*array)[i], {"quality", "shape"}, error))
            return false;
        const auto* object = objectValue((*array)[i], "quality zone", error);
        if (!object)
            return false;
        const JsonValue* qualityValue = requiredMember(*object, "quality", error);
        const JsonValue* shapeValue = requiredMember(*object, "shape", error);
        std::uint64_t quality = 0;
        ExternalQualityZone zone{};
        if (!qualityValue || !unsignedValue(*qualityValue, quality, "quality", error) ||
            quality > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            !shapeValue || !parseShapeValue(*shapeValue, zone.shape, error))
            return false;
        zone.quality = static_cast<std::size_t>(quality);
        zones.push_back(std::move(zone));
    }
    return true;
}

JsonValue containerJson(const ExternalContainer& container)
{
    JsonValue::Object object;
    object.emplace("id", JsonValue(container.id));
    object.emplace("shape", shapeJson(container.shape));
    if (!container.zones.empty())
    {
        JsonValue::Array zones;
        zones.reserve(container.zones.size());
        for (const ExternalQualityZone& zone : container.zones)
        {
            JsonValue::Object zoneObject;
            zoneObject.emplace("quality", JsonValue(static_cast<std::uint64_t>(zone.quality)));
            zoneObject.emplace("shape", shapeJson(zone.shape));
            zones.emplace_back(JsonValue(std::move(zoneObject)));
        }
        object.emplace("zones", JsonValue(std::move(zones)));
    }
    return JsonValue(std::move(object));
}

bool parseContainerValue(const JsonValue& value,
                         ExternalContainer& container,
                         std::string& error)
{
    if (rejectDuplicateFields(value, {"id", "shape", "zones"}, error))
        return false;
    const auto* object = objectValue(value, "container", error);
    if (!object)
        return false;
    const JsonValue* idValue = requiredMember(*object, "id", error);
    const JsonValue* shapeValue = requiredMember(*object, "shape", error);
    if (!idValue || !shapeValue ||
        !unsignedValue(*idValue, container.id, "container.id", error) ||
        !parseShapeValue(*shapeValue, container.shape, error))
        return false;
    container.zones.clear();
    const auto zones = object->find("zones");
    if (zones != object->end() && !parseQualityZones(zones->second, container.zones, error))
        return false;
    return true;
}

JsonValue transformationJson(const ExternalTransformation& transformation)
{
    JsonValue::Object object;
    object.emplace("rotation", JsonValue(transformation.rotationDegrees));
    object.emplace("translation", pointJson(transformation.translation));
    return JsonValue(std::move(object));
}

bool parseTransformationValue(const JsonValue& value,
                              ExternalTransformation& transformation,
                              std::string& error)
{
    if (rejectDuplicateFields(value, {"rotation", "translation"}, error))
        return false;
    const auto* object = objectValue(value, "transformation", error);
    if (!object)
        return false;
    const JsonValue* rotation = requiredMember(*object, "rotation", error);
    const JsonValue* translation = requiredMember(*object, "translation", error);
    if (!rotation || !translation ||
        !numberValue(*rotation, transformation.rotationDegrees,
                     "transformation.rotation", error) ||
        !pointValue(*translation, transformation.translation,
                    "transformation.translation", error))
        return false;
    return true;
}

JsonValue placedItemJson(const ExternalPlacedItem& placedItem)
{
    JsonValue::Object object;
    object.emplace("item_id", JsonValue(placedItem.itemId));
    object.emplace("transformation", transformationJson(placedItem.transformation));
    return JsonValue(std::move(object));
}

bool parseLayoutValue(const JsonValue& value,
                      ExternalLayout& layout,
                      std::string& error)
{
    if (rejectDuplicateFields(value, {"container_id", "placed_items", "density"}, error))
        return false;
    const auto* object = objectValue(value, "layout", error);
    if (!object)
        return false;
    const JsonValue* containerId = requiredMember(*object, "container_id", error);
    const JsonValue* placedItems = requiredMember(*object, "placed_items", error);
    const JsonValue* density = requiredMember(*object, "density", error);
    if (!containerId || !placedItems || !density ||
        !unsignedValue(*containerId, layout.containerId, "container_id", error) ||
        !numberValue(*density, layout.density, "density", error))
        return false;

    const auto* array = arrayValue(*placedItems, "placed_items", error);
    if (!array)
        return false;
    layout.placedItems.clear();
    layout.placedItems.reserve(array->size());
    for (std::size_t i = 0; i < array->size(); ++i)
    {
        if (rejectDuplicateFields((*array)[i], {"item_id", "transformation"}, error))
            return false;
        const auto* placed = objectValue((*array)[i], "placed item", error);
        if (!placed)
            return false;
        const JsonValue* itemId = requiredMember(*placed, "item_id", error);
        const JsonValue* transformation = requiredMember(*placed, "transformation", error);
        ExternalPlacedItem result{};
        if (!itemId || !transformation ||
            !unsignedValue(*itemId, result.itemId, "item_id", error) ||
            !parseTransformationValue(*transformation, result.transformation, error))
            return false;
        layout.placedItems.push_back(std::move(result));
    }
    return true;
}

JsonValue layoutJson(const ExternalLayout& layout)
{
    JsonValue::Object object;
    object.emplace("container_id", JsonValue(layout.containerId));
    JsonValue::Array placedItems;
    placedItems.reserve(layout.placedItems.size());
    for (const ExternalPlacedItem& item : layout.placedItems)
        placedItems.emplace_back(placedItemJson(item));
    object.emplace("placed_items", JsonValue(std::move(placedItems)));
    object.emplace("density", JsonValue(layout.density));
    return JsonValue(std::move(object));
}

bool demandItemValue(const JsonValue& value,
                     ExternalDemandItem& demandItem,
                     std::string& error)
{
    if (rejectDuplicateFields(value, {"demand"}, error))
        return false;
    if (!parseItemValue(value, demandItem.item, error))
        return false;
    const auto* object = objectValue(value, "item", error);
    if (!object)
        return false;
    const JsonValue* demand = requiredMember(*object, "demand", error);
    return demand && unsignedValue(*demand, demandItem.demand, "demand", error);
}

JsonValue demandItemJson(const ExternalDemandItem& demandItem)
{
    JsonValue value = itemJson(demandItem.item);
    auto* object = std::get_if<JsonValue::Object>(&value.value);
    object->emplace("demand", JsonValue(demandItem.demand));
    return value;
}

bool demandItemsValue(const JsonValue& value,
                      std::vector<ExternalDemandItem>& items,
                      std::string& error)
{
    const auto* array = arrayValue(value, "items", error);
    if (!array)
        return false;
    items.clear();
    items.reserve(array->size());
    for (const JsonValue& itemValue : *array)
    {
        ExternalDemandItem item{};
        if (!demandItemValue(itemValue, item, error))
            return false;
        items.push_back(std::move(item));
    }
    return true;
}

JsonValue demandItemsJson(const std::vector<ExternalDemandItem>& items)
{
    JsonValue::Array array;
    array.reserve(items.size());
    for (const ExternalDemandItem& item : items)
        array.emplace_back(demandItemJson(item));
    return JsonValue(std::move(array));
}

bool commonInstanceFields(const JsonValue::Object& object,
                          std::string& name,
                          double& separation,
                          std::vector<ExternalDemandItem>& items,
                          std::string& error)
{
    const JsonValue* nameValue = requiredMember(object, "name", error);
    const JsonValue* separationValue = nullptr;
    const auto separationIt = object.find("min_item_separation");
    if (separationIt != object.end())
        separationValue = &separationIt->second;
    const JsonValue* itemsValue = requiredMember(object, "items", error);
    if (!nameValue || !itemsValue ||
        !stringValue(*nameValue, name, "name", error))
        return false;
    separation = 0.0;
    if (separationValue &&
        !numberValue(*separationValue, separation, "min_item_separation", error))
        return false;
    return demandItemsValue(*itemsValue, items, error);
}

JsonValue commonInstanceJson(const std::string& name,
                             double separation,
                             const std::vector<ExternalDemandItem>& items)
{
    JsonValue::Object object;
    object.emplace("name", JsonValue(name));
    object.emplace("min_item_separation", JsonValue(separation));
    object.emplace("items", demandItemsJson(items));
    return JsonValue(std::move(object));
}

bool parseSPInstanceValue(const JsonValue& value,
                          ExternalSPInstance& instance,
                          std::string& error)
{
    if (rejectDuplicateFields(value,
                              {"name", "min_item_separation", "items", "strip_height"},
                              error))
        return false;
    const auto* object = objectValue(value, "SP instance", error);
    if (!object || !commonInstanceFields(*object, instance.name,
                                         instance.minItemSeparation,
                                         instance.items, error))
        return false;
    const JsonValue* height = requiredMember(*object, "strip_height", error);
    return height && numberValue(*height, instance.stripHeight, "strip_height", error);
}

JsonValue spInstanceJson(const ExternalSPInstance& instance)
{
    JsonValue value = commonInstanceJson(instance.name, instance.minItemSeparation,
                                          instance.items);
    auto* object = std::get_if<JsonValue::Object>(&value.value);
    object->emplace("strip_height", JsonValue(instance.stripHeight));
    return value;
}

bool parseSPSolutionValue(const JsonValue& value,
                          ExternalSPSolution& solution,
                          std::string& error)
{
    if (rejectDuplicateFields(value,
                              {"strip_width", "layout", "density", "run_time_sec"},
                              error))
        return false;
    const auto* object = objectValue(value, "SP solution", error);
    if (!object)
        return false;
    const JsonValue* width = requiredMember(*object, "strip_width", error);
    const JsonValue* layout = requiredMember(*object, "layout", error);
    const JsonValue* density = requiredMember(*object, "density", error);
    const JsonValue* runtime = requiredMember(*object, "run_time_sec", error);
    return width && layout && density && runtime &&
           numberValue(*width, solution.stripWidth, "strip_width", error) &&
           parseLayoutValue(*layout, solution.layout, error) &&
           numberValue(*density, solution.density, "density", error) &&
           unsignedValue(*runtime, solution.runTimeSec, "run_time_sec", error);
}

JsonValue spSolutionJson(const ExternalSPSolution& solution)
{
    JsonValue::Object object;
    object.emplace("strip_width", JsonValue(solution.stripWidth));
    object.emplace("layout", layoutJson(solution.layout));
    object.emplace("density", JsonValue(solution.density));
    object.emplace("run_time_sec", JsonValue(solution.runTimeSec));
    return JsonValue(std::move(object));
}

bool parseBinValue(const JsonValue& value,
                   ExternalBin& bin,
                   std::string& error)
{
    if (rejectDuplicateFields(value, {"stock", "cost"}, error))
        return false;
    if (!parseContainerValue(value, bin.container, error))
        return false;
    const auto* object = objectValue(value, "bin", error);
    if (!object)
        return false;
    const JsonValue* stock = requiredMember(*object, "stock", error);
    const JsonValue* cost = requiredMember(*object, "cost", error);
    return stock && cost &&
           unsignedValue(*stock, bin.stock, "stock", error) &&
           unsignedValue(*cost, bin.cost, "cost", error);
}

JsonValue binJson(const ExternalBin& bin)
{
    JsonValue value = containerJson(bin.container);
    auto* object = std::get_if<JsonValue::Object>(&value.value);
    object->emplace("stock", JsonValue(bin.stock));
    object->emplace("cost", JsonValue(bin.cost));
    return value;
}

bool binsValue(const JsonValue& value,
               std::vector<ExternalBin>& bins,
               std::string& error)
{
    const auto* array = arrayValue(value, "bins", error);
    if (!array)
        return false;
    bins.clear();
    bins.reserve(array->size());
    for (const JsonValue& binValue : *array)
    {
        ExternalBin bin{};
        if (!parseBinValue(binValue, bin, error))
            return false;
        bins.push_back(std::move(bin));
    }
    return true;
}

JsonValue binsJson(const std::vector<ExternalBin>& bins)
{
    JsonValue::Array array;
    array.reserve(bins.size());
    for (const ExternalBin& bin : bins)
        array.emplace_back(binJson(bin));
    return JsonValue(std::move(array));
}

bool parseBPInstanceValue(const JsonValue& value,
                          ExternalBPInstance& instance,
                          std::string& error)
{
    if (rejectDuplicateFields(value,
                              {"name", "min_item_separation", "items", "bins"},
                              error))
        return false;
    const auto* object = objectValue(value, "BP instance", error);
    if (!object || !commonInstanceFields(*object, instance.name,
                                         instance.minItemSeparation,
                                         instance.items, error))
        return false;
    const JsonValue* bins = requiredMember(*object, "bins", error);
    return bins && binsValue(*bins, instance.bins, error);
}

JsonValue bpInstanceJson(const ExternalBPInstance& instance)
{
    JsonValue value = commonInstanceJson(instance.name, instance.minItemSeparation,
                                          instance.items);
    auto* object = std::get_if<JsonValue::Object>(&value.value);
    object->emplace("bins", binsJson(instance.bins));
    return value;
}

bool layoutsValue(const JsonValue& value,
                  std::vector<ExternalLayout>& layouts,
                  std::string& error)
{
    const auto* array = arrayValue(value, "layouts", error);
    if (!array)
        return false;
    layouts.clear();
    layouts.reserve(array->size());
    for (const JsonValue& layoutValue : *array)
    {
        ExternalLayout layout{};
        if (!parseLayoutValue(layoutValue, layout, error))
            return false;
        layouts.push_back(std::move(layout));
    }
    return true;
}

JsonValue layoutsJson(const std::vector<ExternalLayout>& layouts)
{
    JsonValue::Array array;
    array.reserve(layouts.size());
    for (const ExternalLayout& layout : layouts)
        array.emplace_back(layoutJson(layout));
    return JsonValue(std::move(array));
}

bool parseBPSolutionValue(const JsonValue& value,
                          ExternalBPSolution& solution,
                          std::string& error)
{
    if (rejectDuplicateFields(value,
                              {"cost", "layouts", "density", "run_time_sec"},
                              error))
        return false;
    const auto* object = objectValue(value, "BP solution", error);
    if (!object)
        return false;
    const JsonValue* cost = requiredMember(*object, "cost", error);
    const JsonValue* layouts = requiredMember(*object, "layouts", error);
    const JsonValue* density = requiredMember(*object, "density", error);
    const JsonValue* runtime = requiredMember(*object, "run_time_sec", error);
    return cost && layouts && density && runtime &&
           unsignedValue(*cost, solution.cost, "cost", error) &&
           layoutsValue(*layouts, solution.layouts, error) &&
           numberValue(*density, solution.density, "density", error) &&
           unsignedValue(*runtime, solution.runTimeSec, "run_time_sec", error);
}

JsonValue bpSolutionJson(const ExternalBPSolution& solution)
{
    JsonValue::Object object;
    object.emplace("cost", JsonValue(solution.cost));
    object.emplace("layouts", layoutsJson(solution.layouts));
    object.emplace("density", JsonValue(solution.density));
    object.emplace("run_time_sec", JsonValue(solution.runTimeSec));
    return JsonValue(std::move(object));
}

bool parseMSPStripValue(const JsonValue& value,
                        ExternalMSPStrip& strip,
                        std::string& error)
{
    if (rejectDuplicateFields(value, {"height", "max_width"}, error))
        return false;
    const auto* object = objectValue(value, "strips", error);
    if (!object)
        return false;
    const JsonValue* height = requiredMember(*object, "height", error);
    const JsonValue* maxWidth = requiredMember(*object, "max_width", error);
    return height && maxWidth &&
           numberValue(*height, strip.height, "strips.height", error) &&
           numberValue(*maxWidth, strip.maxWidth, "strips.max_width", error);
}

JsonValue mspStripJson(const ExternalMSPStrip& strip)
{
    JsonValue::Object object;
    object.emplace("height", JsonValue(strip.height));
    object.emplace("max_width", JsonValue(strip.maxWidth));
    return JsonValue(std::move(object));
}

bool parseMSPInstanceValue(const JsonValue& value,
                           ExternalMSPInstance& instance,
                           std::string& error)
{
    if (rejectDuplicateFields(value,
                              {"name", "min_item_separation", "items", "strips"},
                              error))
        return false;
    const auto* object = objectValue(value, "MSP instance", error);
    if (!object || !commonInstanceFields(*object, instance.name,
                                         instance.minItemSeparation,
                                         instance.items, error))
        return false;
    const JsonValue* strips = requiredMember(*object, "strips", error);
    return strips && parseMSPStripValue(*strips, instance.strips, error);
}

JsonValue mspInstanceJson(const ExternalMSPInstance& instance)
{
    JsonValue value = commonInstanceJson(instance.name, instance.minItemSeparation,
                                          instance.items);
    auto* object = std::get_if<JsonValue::Object>(&value.value);
    object->emplace("strips", mspStripJson(instance.strips));
    return value;
}

bool containersValue(const JsonValue& value,
                     std::vector<ExternalContainer>& containers,
                     std::string& error)
{
    const auto* array = arrayValue(value, "containers", error);
    if (!array)
        return false;
    containers.clear();
    containers.reserve(array->size());
    for (const JsonValue& containerValue : *array)
    {
        ExternalContainer container{};
        if (!parseContainerValue(containerValue, container, error))
            return false;
        containers.push_back(std::move(container));
    }
    return true;
}

JsonValue containersJson(const std::vector<ExternalContainer>& containers)
{
    JsonValue::Array array;
    array.reserve(containers.size());
    for (const ExternalContainer& container : containers)
        array.emplace_back(containerJson(container));
    return JsonValue(std::move(array));
}

bool parseMSPSolutionValue(const JsonValue& value,
                           ExternalMSPSolution& solution,
                           std::string& error)
{
    if (rejectDuplicateFields(value,
                              {"layouts", "containers", "density", "run_time_sec"},
                              error))
        return false;
    const auto* object = objectValue(value, "MSP solution", error);
    if (!object)
        return false;
    const JsonValue* layouts = requiredMember(*object, "layouts", error);
    const JsonValue* containers = requiredMember(*object, "containers", error);
    const JsonValue* density = requiredMember(*object, "density", error);
    const JsonValue* runtime = requiredMember(*object, "run_time_sec", error);
    return layouts && containers && density && runtime &&
           layoutsValue(*layouts, solution.layouts, error) &&
           containersValue(*containers, solution.containers, error) &&
           numberValue(*density, solution.density, "density", error) &&
           unsignedValue(*runtime, solution.runTimeSec, "run_time_sec", error);
}

JsonValue mspSolutionJson(const ExternalMSPSolution& solution)
{
    JsonValue::Object object;
    object.emplace("layouts", layoutsJson(solution.layouts));
    object.emplace("containers", containersJson(solution.containers));
    object.emplace("density", JsonValue(solution.density));
    object.emplace("run_time_sec", JsonValue(solution.runTimeSec));
    return JsonValue(std::move(object));
}
}

JAGUARCORE_API Result<ExternalLayout> ExternalLayoutFromLayout(
    const Layout& layout, std::optional<ContainerId> containerId)
{
    ExternalLayout external{};
    external.containerId = containerId.value_or(layout.GetContainer().Id());
    external.density = layout.Density();

    const LayoutSnapshot snapshot = layout.Save();
    external.placedItems.reserve(snapshot.placements.size());
    for (const LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        const PlacedItem& placedItem = placement.placedItem;
        const Transform2d transform =
        placedItem.GetItem().GetOriginalShape().preTransform.FollowedByRustF32(
                placedItem.Transform());
        if (!std::isfinite(transform.rotationRad) ||
            !transform.translation.IsFinite())
        {
            return Result<ExternalLayout>::FromError(
                {ErrorCode::SerializationFailure,
                 "layout contains a non-finite placement transformation"});
        }
        const float rotationDegrees =
            detail::ToDegreesRustF32(transform.rotationRad);
        external.placedItems.push_back(
            {placedItem.GetItem().ExternalId(),
             {static_cast<double>(rotationDegrees),
              transform.translation}});
    }
    return Result<ExternalLayout>::FromValue(std::move(external));
}

JAGUARCORE_API Result<ExternalSPSolution> ExternalSPSolutionFromSolution(
    const SPInstance& instance,
    const SPSolution& solution,
    std::uint64_t runTimeSec)
{
    auto problem = SPProblem::Create(instance);
    if (!problem.HasValue())
        return Result<ExternalSPSolution>::FromError(problem.GetError());
    const auto restored = problem.Value().Restore(solution);
    if (!restored.HasValue())
        return Result<ExternalSPSolution>::FromError(
            {ErrorCode::SerializationFailure,
             "cannot restore the SPP solution for export"});
    const auto valid = ValidateSPSolution(problem.Value(), solution);
    if (!valid.HasValue())
        return Result<ExternalSPSolution>::FromError(
            {ErrorCode::SerializationFailure,
             "cannot export an invalid SPP solution"});
    const auto externalLayout = ExternalLayoutFromSnapshot(
        solution.layoutSnapshot, solution.layoutSnapshot.container.Id());
    if (!externalLayout.HasValue())
        return Result<ExternalSPSolution>::FromError(externalLayout.GetError());
    return Result<ExternalSPSolution>::FromValue(
        ExternalSPSolution{solution.strip.width, externalLayout.Value(),
                           solution.Density(), runTimeSec});
}

JAGUARCORE_API Result<ExternalBPSolution> ExternalBPSolutionFromSolution(
    const BPInstance& instance,
    const BPSolution& solution,
    std::uint64_t runTimeSec)
{
    auto problem = BPProblem::Create(instance);
    if (!problem.HasValue())
        return Result<ExternalBPSolution>::FromError(problem.GetError());
    const auto restored = problem.Value().Restore(solution);
    if (!restored.HasValue())
        return Result<ExternalBPSolution>::FromError(
            {ErrorCode::SerializationFailure,
             "cannot restore the BPP solution for export"});
    const auto valid = ValidateBPSolution(problem.Value(), solution);
    if (!valid.HasValue())
        return Result<ExternalBPSolution>::FromError(
            {ErrorCode::SerializationFailure,
             "cannot export an invalid BPP solution"});
    ExternalBPSolution external{};
    external.cost = solution.Cost(instance);
    external.density = solution.Density();
    external.runTimeSec = runTimeSec;
    external.layouts.reserve(solution.layouts.size());
    for (const BPLayoutSnapshot& snapshot : solution.layouts)
    {
        if (snapshot.binIndex >= instance.BinCount())
        {
            return Result<ExternalBPSolution>::FromError(
                {ErrorCode::SerializationFailure,
                 "BPP solution references an invalid bin index"});
        }
        const auto layout = ExternalLayoutFromSnapshot(
            snapshot.layoutSnapshot,
            instance.GetBin(snapshot.binIndex).externalId);
        if (!layout.HasValue())
            return Result<ExternalBPSolution>::FromError(layout.GetError());
        external.layouts.push_back(layout.Value());
    }
    return Result<ExternalBPSolution>::FromValue(std::move(external));
}

JAGUARCORE_API Result<ExternalMSPSolution> ExternalMSPSolutionFromSolution(
    const MSPInstance& instance,
    const MSPSolution& solution,
    std::uint64_t runTimeSec)
{
    auto problem = MSPProblem::Create(instance);
    if (!problem.HasValue())
        return Result<ExternalMSPSolution>::FromError(problem.GetError());
    const auto restored = problem.Value().Restore(solution);
    if (!restored.HasValue())
        return Result<ExternalMSPSolution>::FromError(
            {ErrorCode::SerializationFailure,
             "cannot restore the MSPP solution for export"});
    const auto valid = ValidateMSPSolution(problem.Value(), solution);
    if (!valid.HasValue())
        return Result<ExternalMSPSolution>::FromError(
            {ErrorCode::SerializationFailure,
             "cannot export an invalid MSPP solution"});
    ExternalMSPSolution external{};
    external.density = solution.Density();
    external.runTimeSec = runTimeSec;
    external.layouts.reserve(solution.layouts.size());
    external.containers.reserve(solution.layouts.size());
    for (std::size_t index = 0; index < solution.layouts.size(); ++index)
    {
        const MSPLayoutSnapshot& snapshot = solution.layouts[index];
        const ContainerId containerId = static_cast<ContainerId>(index);
        const auto layout = ExternalLayoutFromSnapshot(
            snapshot.layoutSnapshot, containerId);
        if (!layout.HasValue())
            return Result<ExternalMSPSolution>::FromError(layout.GetError());
        external.layouts.push_back(layout.Value());
        external.containers.push_back(externalRectangleContainer(
            snapshot.layoutSnapshot.container, containerId));
    }
    return Result<ExternalMSPSolution>::FromValue(std::move(external));
}

JAGUARCORE_API Result<ExternalItem> ParseExternalItemJson(const std::string& json)
{
    return parseJson<ExternalItem>(json, parseItemValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalItemJson(
    const ExternalItem& item, JsonCodecOptions options)
{
    return writeJson(itemJson(item), options);
}

JAGUARCORE_API Result<ExternalContainer> ParseExternalContainerJson(
    const std::string& json)
{
    return parseJson<ExternalContainer>(json, parseContainerValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalContainerJson(
    const ExternalContainer& container, JsonCodecOptions options)
{
    return writeJson(containerJson(container), options);
}

JAGUARCORE_API Result<ExternalLayout> ParseExternalLayoutJson(
    const std::string& json)
{
    return parseJson<ExternalLayout>(json, parseLayoutValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalLayoutJson(
    const ExternalLayout& layout, JsonCodecOptions options)
{
    return writeJson(layoutJson(layout), options);
}

JAGUARCORE_API Result<ExternalSPInstance> ParseExternalSPInstanceJson(
    const std::string& json)
{
    return parseJson<ExternalSPInstance>(json, parseSPInstanceValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalSPInstanceJson(
    const ExternalSPInstance& instance, JsonCodecOptions options)
{
    return writeJson(spInstanceJson(instance), options);
}

JAGUARCORE_API Result<ExternalSPSolution> ParseExternalSPSolutionJson(
    const std::string& json)
{
    return parseJson<ExternalSPSolution>(json, parseSPSolutionValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalSPSolutionJson(
    const ExternalSPSolution& solution, JsonCodecOptions options)
{
    return writeJson(spSolutionJson(solution), options);
}

JAGUARCORE_API Result<ExternalBPInstance> ParseExternalBPInstanceJson(
    const std::string& json)
{
    return parseJson<ExternalBPInstance>(json, parseBPInstanceValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalBPInstanceJson(
    const ExternalBPInstance& instance, JsonCodecOptions options)
{
    return writeJson(bpInstanceJson(instance), options);
}

JAGUARCORE_API Result<ExternalBPSolution> ParseExternalBPSolutionJson(
    const std::string& json)
{
    return parseJson<ExternalBPSolution>(json, parseBPSolutionValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalBPSolutionJson(
    const ExternalBPSolution& solution, JsonCodecOptions options)
{
    return writeJson(bpSolutionJson(solution), options);
}

JAGUARCORE_API Result<ExternalMSPInstance> ParseExternalMSPInstanceJson(
    const std::string& json)
{
    return parseJson<ExternalMSPInstance>(json, parseMSPInstanceValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalMSPInstanceJson(
    const ExternalMSPInstance& instance, JsonCodecOptions options)
{
    return writeJson(mspInstanceJson(instance), options);
}

JAGUARCORE_API Result<ExternalMSPSolution> ParseExternalMSPSolutionJson(
    const std::string& json)
{
    return parseJson<ExternalMSPSolution>(json, parseMSPSolutionValue);
}

JAGUARCORE_API Result<std::string> SerializeExternalMSPSolutionJson(
    const ExternalMSPSolution& solution, JsonCodecOptions options)
{
    return writeJson(mspSolutionJson(solution), options);
}
}
