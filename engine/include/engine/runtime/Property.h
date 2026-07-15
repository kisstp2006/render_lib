#pragma once

#include "engine/asset/AssetGuid.h"
#include "engine/plugin/PluginApi.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::runtime
{

enum class PropertyType : uint8_t
{
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Vector2,
    Vector3,
    Vector4,
    Quaternion,
    String,
    AssetGuid,
    Enumeration
};

enum class PropertyFlags : uint32_t
{
    None = 0,
    ReadOnly = 1u << 0u,
    Color = 1u << 1u,
    AngleDegrees = 1u << 2u,
    Multiline = 1u << 3u,
    Hidden = 1u << 4u,
    // Not persisted by World scene serialization (runtime caches, handles,
    // or derived values).
    Transient = 1u << 5u,
    // Minimum/Maximum are meaningful. Without this flag the numeric range is
    // intentionally unconstrained, even when both fields are zero.
    HasRange = 1u << 6u
};

constexpr PropertyFlags operator|(PropertyFlags left, PropertyFlags right)
{
    return static_cast<PropertyFlags>(static_cast<uint32_t>(left) |
                                      static_cast<uint32_t>(right));
}

constexpr bool HasFlag(PropertyFlags value, PropertyFlags flag)
{
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

using PropertyValue = std::variant<bool, int64_t, uint64_t, double,
                                   glm::vec2, glm::vec3, glm::vec4, glm::quat,
                                   std::string, assets::AssetGuid>;

struct PropertyMetadata
{
    std::string Name;
    std::string DisplayName;
    PropertyType Type = PropertyType::String;
    PropertyFlags Flags = PropertyFlags::None;
    double Minimum = 0.0;
    double Maximum = 0.0;
    double Step = 0.0;
    std::vector<std::string> EnumValues;
    std::function<std::optional<PropertyValue>(const void*)> Get;
    std::function<bool(void*, const PropertyValue&, std::string*)> Set;
};

std::string PropertyValueToString(PropertyType type, const PropertyValue& value);
std::optional<PropertyValue> PropertyValueFromString(PropertyType type,
                                                     std::string_view text,
                                                     std::string* error = nullptr);

struct PropertyOptions
{
    std::string DisplayName;
    PropertyFlags Flags = PropertyFlags::None;
    double Minimum = 0.0;
    double Maximum = 0.0;
    double Step = 0.0;
    std::vector<std::string> EnumValues;
};

namespace detail
{

template <typename T> struct PropertyTraits;
template <> struct PropertyTraits<bool> { static constexpr PropertyType Type = PropertyType::Boolean; };
template <> struct PropertyTraits<int> { static constexpr PropertyType Type = PropertyType::SignedInteger; };
template <> struct PropertyTraits<int64_t> { static constexpr PropertyType Type = PropertyType::SignedInteger; };
template <> struct PropertyTraits<uint32_t> { static constexpr PropertyType Type = PropertyType::UnsignedInteger; };
template <> struct PropertyTraits<uint64_t> { static constexpr PropertyType Type = PropertyType::UnsignedInteger; };
template <> struct PropertyTraits<float> { static constexpr PropertyType Type = PropertyType::FloatingPoint; };
template <> struct PropertyTraits<double> { static constexpr PropertyType Type = PropertyType::FloatingPoint; };
template <> struct PropertyTraits<glm::vec2> { static constexpr PropertyType Type = PropertyType::Vector2; };
template <> struct PropertyTraits<glm::vec3> { static constexpr PropertyType Type = PropertyType::Vector3; };
template <> struct PropertyTraits<glm::vec4> { static constexpr PropertyType Type = PropertyType::Vector4; };
template <> struct PropertyTraits<glm::quat> { static constexpr PropertyType Type = PropertyType::Quaternion; };
template <> struct PropertyTraits<std::string> { static constexpr PropertyType Type = PropertyType::String; };
template <> struct PropertyTraits<assets::AssetGuid> { static constexpr PropertyType Type = PropertyType::AssetGuid; };

template <typename T> PropertyValue ToValue(const T& value)
{
    if constexpr (std::is_enum_v<T>)
        return static_cast<int64_t>(value);
    else if constexpr (std::is_same_v<T, int>)
        return static_cast<int64_t>(value);
    else if constexpr (std::is_same_v<T, uint32_t>)
        return static_cast<uint64_t>(value);
    else if constexpr (std::is_same_v<T, float>)
        return static_cast<double>(value);
    else
        return value;
}

template <typename T> bool FromValue(const PropertyValue& value, T& output)
{
    if constexpr (std::is_enum_v<T>)
    {
        if (const auto* number = std::get_if<int64_t>(&value))
        {
            output = static_cast<T>(*number);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, int>)
    {
        if (const auto* number = std::get_if<int64_t>(&value))
        {
            output = static_cast<T>(*number);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, uint32_t>)
    {
        if (const auto* number = std::get_if<uint64_t>(&value))
        {
            output = static_cast<uint32_t>(*number);
            return true;
        }
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        if (const auto* number = std::get_if<double>(&value))
        {
            output = static_cast<float>(*number);
            return true;
        }
    }
    else if (const auto* typed = std::get_if<T>(&value))
    {
        output = *typed;
        return true;
    }
    return false;
}

} // namespace detail

template <typename ComponentType, typename MemberType>
PropertyMetadata MakeProperty(std::string name, MemberType ComponentType::* member,
                              PropertyOptions options = {})
{
    using CleanMember = std::remove_cv_t<MemberType>;
    PropertyMetadata property;
    property.Name = std::move(name);
    property.DisplayName = options.DisplayName.empty() ? property.Name
                                                       : std::move(options.DisplayName);
    if constexpr (std::is_enum_v<CleanMember>)
        property.Type = PropertyType::Enumeration;
    else
        property.Type = detail::PropertyTraits<CleanMember>::Type;
    property.Flags = options.Flags;
    property.Minimum = options.Minimum;
    property.Maximum = options.Maximum;
    property.Step = options.Step;
    property.EnumValues = std::move(options.EnumValues);
    property.Get = [member](const void* instance) -> std::optional<PropertyValue> {
        if (!instance)
            return std::nullopt;
        return detail::ToValue(static_cast<const ComponentType*>(instance)->*member);
    };
    property.Set = [member, readOnly = HasFlag(property.Flags, PropertyFlags::ReadOnly)](
                       void* instance, const PropertyValue& value, std::string* error) {
        if (!instance || readOnly)
        {
            if (error) *error = readOnly ? "Property is read-only" : "Component instance is null";
            return false;
        }
        CleanMember converted{};
        if (!detail::FromValue(value, converted))
        {
            if (error) *error = "Property value has the wrong type";
            return false;
        }
        static_cast<ComponentType*>(instance)->*member = std::move(converted);
        return true;
    };
    return property;
}

} // namespace engine::runtime
