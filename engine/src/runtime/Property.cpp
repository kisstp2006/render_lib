#include "engine/runtime/Property.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace engine::runtime
{
namespace
{

template <size_t Count>
std::optional<std::array<float, Count>> ParseFloats(std::string_view text)
{
    std::array<float, Count> values{};
    std::string copy(text);
    for (char& character : copy)
        if (character == ',' || character == ';') character = ' ';
    std::istringstream stream(copy);
    for (float& value : values)
        if (!(stream >> value)) return std::nullopt;
    std::string trailing;
    if (stream >> trailing) return std::nullopt;
    return values;
}

template <typename T> std::optional<T> ParseInteger(std::string_view text)
{
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

} // namespace

std::string PropertyValueToString(PropertyType type, const PropertyValue& value)
{
    std::ostringstream stream;
    stream << std::setprecision(9);
    switch (type)
    {
    case PropertyType::Boolean:
        return std::get<bool>(value) ? "true" : "false";
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        return std::to_string(std::get<int64_t>(value));
    case PropertyType::UnsignedInteger:
        return std::to_string(std::get<uint64_t>(value));
    case PropertyType::FloatingPoint:
        stream << std::get<double>(value); break;
    case PropertyType::Vector2:
    {
        const auto& v = std::get<glm::vec2>(value); stream << v.x << ' ' << v.y; break;
    }
    case PropertyType::Vector3:
    {
        const auto& v = std::get<glm::vec3>(value); stream << v.x << ' ' << v.y << ' ' << v.z; break;
    }
    case PropertyType::Vector4:
    {
        const auto& v = std::get<glm::vec4>(value); stream << v.x << ' ' << v.y << ' ' << v.z << ' ' << v.w; break;
    }
    case PropertyType::Quaternion:
    {
        const auto& q = std::get<glm::quat>(value); stream << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z; break;
    }
    case PropertyType::String:
        return std::get<std::string>(value);
    case PropertyType::AssetGuid:
    {
        const assets::AssetGuid guid = std::get<assets::AssetGuid>(value);
        return guid.IsValid() ? guid.ToString() : std::string{};
    }
    }
    return stream.str();
}

std::optional<PropertyValue> PropertyValueFromString(PropertyType type,
                                                     std::string_view text,
                                                     std::string* error)
{
    const auto fail = [&]() -> std::optional<PropertyValue> {
        if (error) *error = "Invalid serialized property value '" + std::string(text) + "'";
        return std::nullopt;
    };
    switch (type)
    {
    case PropertyType::Boolean:
        if (text == "true" || text == "1") return PropertyValue(true);
        if (text == "false" || text == "0") return PropertyValue(false);
        return fail();
    case PropertyType::SignedInteger:
    case PropertyType::Enumeration:
        if (auto value = ParseInteger<int64_t>(text)) return PropertyValue(*value);
        return fail();
    case PropertyType::UnsignedInteger:
        if (auto value = ParseInteger<uint64_t>(text)) return PropertyValue(*value);
        return fail();
    case PropertyType::FloatingPoint:
    {
        std::string copy(text); char* end = nullptr;
        const double value = std::strtod(copy.c_str(), &end);
        if (end && *end == '\0') return PropertyValue(value);
        return fail();
    }
    case PropertyType::Vector2:
        if (auto v = ParseFloats<2>(text)) return PropertyValue(glm::vec2((*v)[0], (*v)[1]));
        return fail();
    case PropertyType::Vector3:
        if (auto v = ParseFloats<3>(text)) return PropertyValue(glm::vec3((*v)[0], (*v)[1], (*v)[2]));
        return fail();
    case PropertyType::Vector4:
        if (auto v = ParseFloats<4>(text)) return PropertyValue(glm::vec4((*v)[0], (*v)[1], (*v)[2], (*v)[3]));
        return fail();
    case PropertyType::Quaternion:
        if (auto v = ParseFloats<4>(text)) return PropertyValue(glm::quat((*v)[0], (*v)[1], (*v)[2], (*v)[3]));
        return fail();
    case PropertyType::String:
        return PropertyValue(std::string(text));
    case PropertyType::AssetGuid:
        if (text.empty()) return PropertyValue(assets::AssetGuid{});
        if (auto guid = assets::AssetGuid::Parse(text)) return PropertyValue(*guid);
        return fail();
    }
    return fail();
}

} // namespace engine::runtime
