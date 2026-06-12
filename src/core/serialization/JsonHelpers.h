#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tsq::core::serialization
{
class JsonValue
{
public:
    enum class Type
    {
        null,
        boolean,
        number,
        string,
        array,
        object
    };

    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;

    JsonValue() = default;

    static JsonValue null();
    static JsonValue boolean (bool value);
    static JsonValue number (double value);
    static JsonValue string (std::string value);
    static JsonValue array (Array value = {});
    static JsonValue object (Object value = {});

    Type type() const noexcept;
    bool isNull() const noexcept;
    bool isBoolean() const noexcept;
    bool isNumber() const noexcept;
    bool isString() const noexcept;
    bool isArray() const noexcept;
    bool isObject() const noexcept;

    bool asBoolean() const;
    double asNumber() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;
    Array& asArray();
    Object& asObject();

private:
    Type type_ = Type::null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

JsonValue parseJson (std::string_view text);
std::string writeJson (const JsonValue& value);

const JsonValue& requireField (const JsonValue& object, const std::string& fieldName);
const JsonValue::Object& requireObject (const JsonValue& value, const std::string& description);
const JsonValue::Array& requireArray (const JsonValue& value, const std::string& description);
std::string requireString (const JsonValue& value, const std::string& description);
bool requireBool (const JsonValue& value, const std::string& description);
std::int64_t requireInt64 (const JsonValue& value, const std::string& description);
int requireInt (const JsonValue& value, const std::string& description);

JsonValue::Array stringArrayToJson (const std::vector<std::string>& values);
std::vector<std::string> stringArrayFromJson (const JsonValue& value, const std::string& description);
}
