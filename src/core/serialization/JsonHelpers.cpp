#include "core/serialization/JsonHelpers.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tsq::core::serialization
{
namespace
{
class JsonParser
{
public:
    explicit JsonParser (std::string_view text)
        : text_ (text)
    {
    }

    JsonValue parse()
    {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();

        if (! isAtEnd())
            throw std::runtime_error ("Unexpected trailing characters after JSON document");

        return value;
    }

private:
    JsonValue parseValue()
    {
        skipWhitespace();

        if (isAtEnd())
            throw std::runtime_error ("Unexpected end of JSON document");

        const auto c = peek();
        if (c == '{')
            return parseObject();
        if (c == '[')
            return parseArray();
        if (c == '"')
            return JsonValue::string (parseString());
        if (c == 't')
            return parseLiteral ("true", JsonValue::boolean (true));
        if (c == 'f')
            return parseLiteral ("false", JsonValue::boolean (false));
        if (c == 'n')
            return parseLiteral ("null", JsonValue::null());
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();

        throw std::runtime_error ("Unexpected character while parsing JSON value");
    }

    JsonValue parseObject()
    {
        consume ('{');
        JsonValue::Object object;
        skipWhitespace();

        if (tryConsume ('}'))
            return JsonValue::object (std::move (object));

        while (true)
        {
            skipWhitespace();
            if (peek() != '"')
                throw std::runtime_error ("Expected JSON object key string");

            auto key = parseString();
            skipWhitespace();
            consume (':');
            object.emplace (std::move (key), parseValue());
            skipWhitespace();

            if (tryConsume ('}'))
                break;

            consume (',');
        }

        return JsonValue::object (std::move (object));
    }

    JsonValue parseArray()
    {
        consume ('[');
        JsonValue::Array array;
        skipWhitespace();

        if (tryConsume (']'))
            return JsonValue::array (std::move (array));

        while (true)
        {
            array.push_back (parseValue());
            skipWhitespace();

            if (tryConsume (']'))
                break;

            consume (',');
        }

        return JsonValue::array (std::move (array));
    }

    std::string parseString()
    {
        consume ('"');
        std::string result;

        while (! isAtEnd())
        {
            const auto c = advance();
            if (c == '"')
                return result;

            if (c != '\\')
            {
                result.push_back (c);
                continue;
            }

            if (isAtEnd())
                throw std::runtime_error ("Unexpected end of JSON escape sequence");

            const auto escaped = advance();
            switch (escaped)
            {
                case '"': result.push_back ('"'); break;
                case '\\': result.push_back ('\\'); break;
                case '/': result.push_back ('/'); break;
                case 'b': result.push_back ('\b'); break;
                case 'f': result.push_back ('\f'); break;
                case 'n': result.push_back ('\n'); break;
                case 'r': result.push_back ('\r'); break;
                case 't': result.push_back ('\t'); break;
                default:
                    throw std::runtime_error ("Unsupported JSON escape sequence");
            }
        }

        throw std::runtime_error ("Unterminated JSON string");
    }

    JsonValue parseNumber()
    {
        const auto start = index_;

        if (peek() == '-')
            advance();

        if (isAtEnd())
            throw std::runtime_error ("Unexpected end of JSON number");

        if (peek() == '0')
            advance();
        else
        {
            if (! isDigit (peek()))
                throw std::runtime_error ("Invalid JSON number");

            while (! isAtEnd() && isDigit (peek()))
                advance();
        }

        if (! isAtEnd() && peek() == '.')
        {
            advance();
            if (isAtEnd() || ! isDigit (peek()))
                throw std::runtime_error ("Invalid JSON fractional number");

            while (! isAtEnd() && isDigit (peek()))
                advance();
        }

        if (! isAtEnd() && (peek() == 'e' || peek() == 'E'))
        {
            advance();
            if (! isAtEnd() && (peek() == '+' || peek() == '-'))
                advance();

            if (isAtEnd() || ! isDigit (peek()))
                throw std::runtime_error ("Invalid JSON exponent");

            while (! isAtEnd() && isDigit (peek()))
                advance();
        }

        const auto token = std::string { text_.substr (start, index_ - start) };
        return JsonValue::number (std::stod (token));
    }

    JsonValue parseLiteral (std::string_view literal, JsonValue value)
    {
        if (text_.substr (index_, literal.size()) != literal)
            throw std::runtime_error ("Invalid JSON literal");

        index_ += literal.size();
        return value;
    }

    void skipWhitespace()
    {
        while (! isAtEnd())
        {
            const auto c = peek();
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                break;

            ++index_;
        }
    }

    bool isAtEnd() const noexcept
    {
        return index_ >= text_.size();
    }

    char peek() const
    {
        if (isAtEnd())
            throw std::runtime_error ("Unexpected end of JSON document");

        return text_[index_];
    }

    char advance()
    {
        const auto c = peek();
        ++index_;
        return c;
    }

    void consume (char expected)
    {
        if (advance() != expected)
            throw std::runtime_error ("Unexpected character while parsing JSON");
    }

    bool tryConsume (char expected)
    {
        if (isAtEnd() || peek() != expected)
            return false;

        ++index_;
        return true;
    }

    static bool isDigit (char c) noexcept
    {
        return c >= '0' && c <= '9';
    }

    std::string_view text_;
    std::size_t index_ = 0;
};

void writeEscapedString (std::ostream& stream, const std::string& value)
{
    stream << '"';

    for (const auto c : value)
    {
        switch (c)
        {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default: stream << c; break;
        }
    }

    stream << '"';
}

void writeValue (std::ostream& stream, const JsonValue& value, int indent)
{
    const auto nextIndent = indent + 2;
    const auto indentation = [] (int width) { return std::string (static_cast<std::size_t> (width), ' '); };

    switch (value.type())
    {
        case JsonValue::Type::null:
            stream << "null";
            break;

        case JsonValue::Type::boolean:
            stream << (value.asBoolean() ? "true" : "false");
            break;

        case JsonValue::Type::number:
            stream << std::setprecision (15) << value.asNumber();
            break;

        case JsonValue::Type::string:
            writeEscapedString (stream, value.asString());
            break;

        case JsonValue::Type::array:
        {
            const auto& array = value.asArray();
            stream << '[';
            if (! array.empty())
            {
                stream << '\n';
                for (std::size_t index = 0; index < array.size(); ++index)
                {
                    stream << indentation (nextIndent);
                    writeValue (stream, array[index], nextIndent);
                    if (index + 1 < array.size())
                        stream << ',';
                    stream << '\n';
                }
                stream << indentation (indent);
            }
            stream << ']';
            break;
        }

        case JsonValue::Type::object:
        {
            const auto& object = value.asObject();
            stream << '{';
            if (! object.empty())
            {
                stream << '\n';
                auto index = std::size_t { 0 };
                for (const auto& [key, child] : object)
                {
                    stream << indentation (nextIndent);
                    writeEscapedString (stream, key);
                    stream << ": ";
                    writeValue (stream, child, nextIndent);
                    if (index + 1 < object.size())
                        stream << ',';
                    stream << '\n';
                    ++index;
                }
                stream << indentation (indent);
            }
            stream << '}';
            break;
        }
    }
}
}

JsonValue JsonValue::null()
{
    return JsonValue {};
}

JsonValue JsonValue::boolean (bool value)
{
    JsonValue result;
    result.type_ = Type::boolean;
    result.boolean_ = value;
    return result;
}

JsonValue JsonValue::number (double value)
{
    if (! std::isfinite (value))
        throw std::invalid_argument ("JSON number must be finite");

    JsonValue result;
    result.type_ = Type::number;
    result.number_ = value;
    return result;
}

JsonValue JsonValue::string (std::string value)
{
    JsonValue result;
    result.type_ = Type::string;
    result.string_ = std::move (value);
    return result;
}

JsonValue JsonValue::array (Array value)
{
    JsonValue result;
    result.type_ = Type::array;
    result.array_ = std::move (value);
    return result;
}

JsonValue JsonValue::object (Object value)
{
    JsonValue result;
    result.type_ = Type::object;
    result.object_ = std::move (value);
    return result;
}

JsonValue::Type JsonValue::type() const noexcept { return type_; }
bool JsonValue::isNull() const noexcept { return type_ == Type::null; }
bool JsonValue::isBoolean() const noexcept { return type_ == Type::boolean; }
bool JsonValue::isNumber() const noexcept { return type_ == Type::number; }
bool JsonValue::isString() const noexcept { return type_ == Type::string; }
bool JsonValue::isArray() const noexcept { return type_ == Type::array; }
bool JsonValue::isObject() const noexcept { return type_ == Type::object; }

bool JsonValue::asBoolean() const
{
    if (! isBoolean())
        throw std::runtime_error ("Expected JSON boolean");

    return boolean_;
}

double JsonValue::asNumber() const
{
    if (! isNumber())
        throw std::runtime_error ("Expected JSON number");

    return number_;
}

const std::string& JsonValue::asString() const
{
    if (! isString())
        throw std::runtime_error ("Expected JSON string");

    return string_;
}

const JsonValue::Array& JsonValue::asArray() const
{
    if (! isArray())
        throw std::runtime_error ("Expected JSON array");

    return array_;
}

const JsonValue::Object& JsonValue::asObject() const
{
    if (! isObject())
        throw std::runtime_error ("Expected JSON object");

    return object_;
}

JsonValue::Array& JsonValue::asArray()
{
    if (! isArray())
        throw std::runtime_error ("Expected JSON array");

    return array_;
}

JsonValue::Object& JsonValue::asObject()
{
    if (! isObject())
        throw std::runtime_error ("Expected JSON object");

    return object_;
}

JsonValue parseJson (std::string_view text)
{
    return JsonParser { text }.parse();
}

std::string writeJson (const JsonValue& value)
{
    std::ostringstream stream;
    writeValue (stream, value, 0);
    stream << '\n';
    return stream.str();
}

const JsonValue& requireField (const JsonValue& object, const std::string& fieldName)
{
    const auto& fields = requireObject (object, "object containing field '" + fieldName + "'");
    const auto match = fields.find (fieldName);
    if (match == fields.end())
        throw std::runtime_error ("Missing required field '" + fieldName + "'");

    return match->second;
}

const JsonValue::Object& requireObject (const JsonValue& value, const std::string& description)
{
    if (! value.isObject())
        throw std::runtime_error ("Expected " + description + " to be an object");

    return value.asObject();
}

const JsonValue::Array& requireArray (const JsonValue& value, const std::string& description)
{
    if (! value.isArray())
        throw std::runtime_error ("Expected " + description + " to be an array");

    return value.asArray();
}

std::string requireString (const JsonValue& value, const std::string& description)
{
    if (! value.isString())
        throw std::runtime_error ("Expected " + description + " to be a string");

    return value.asString();
}

bool requireBool (const JsonValue& value, const std::string& description)
{
    if (! value.isBoolean())
        throw std::runtime_error ("Expected " + description + " to be a boolean");

    return value.asBoolean();
}

std::int64_t requireInt64 (const JsonValue& value, const std::string& description)
{
    if (! value.isNumber())
        throw std::runtime_error ("Expected " + description + " to be a number");

    const auto number = value.asNumber();
    const auto rounded = std::llround (number);
    if (std::fabs (number - static_cast<double> (rounded)) > 0.0000001)
        throw std::runtime_error ("Expected " + description + " to be an integer");

    return rounded;
}

int requireInt (const JsonValue& value, const std::string& description)
{
    const auto number = requireInt64 (value, description);
    if (number < static_cast<std::int64_t> (std::numeric_limits<int>::min())
        || number > static_cast<std::int64_t> (std::numeric_limits<int>::max()))
        throw std::runtime_error ("Expected " + description + " to fit in an int");

    return static_cast<int> (number);
}

JsonValue::Array stringArrayToJson (const std::vector<std::string>& values)
{
    JsonValue::Array result;
    result.reserve (values.size());

    for (const auto& value : values)
        result.push_back (JsonValue::string (value));

    return result;
}

std::vector<std::string> stringArrayFromJson (const JsonValue& value, const std::string& description)
{
    std::vector<std::string> result;

    for (const auto& item : requireArray (value, description))
        result.push_back (requireString (item, description + " item"));

    return result;
}
}
