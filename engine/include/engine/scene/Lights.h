#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "engine/scene/Texture.h"

namespace engine {

struct DirectionalLight
{
    glm::vec3 Direction{-0.4f, -0.85f, -0.35f};
    glm::vec3 Color{1.0f, 0.96f, 0.88f};
    float Intensity = 3.0f;
    bool CastsShadows = true;
};

struct PointLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 20.0f;
    float Radius = 15.0f;
    bool CastsShadows = false;
    bool Enabled = true;
    std::shared_ptr<TextureData> Cookie;
};

// Source-style spot with a smooth inner/outer cone. The first enabled,
// shadow-casting spot currently receives the dedicated spot shadow map.
struct SpotLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Direction{0.0f, -1.0f, 0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 30.0f;
    float Range = 25.0f;
    float InnerConeDeg = 20.0f;
    float OuterConeDeg = 35.0f;
    bool CastsShadows = false;
    bool Enabled = true;
    std::shared_ptr<TextureData> Cookie;
};

// Source 2 barn/rect-inspired projected rectangular area light. The finite
// luminaire is used for representative-point lighting, while the barn shape,
// softness and cookie are evaluated in the projected light frustum.
struct AreaLight
{
    glm::vec3 Position{0.0f};
    glm::vec3 Direction{0.0f, -1.0f, 0.0f};
    glm::vec3 Up{0.0f, 1.0f, 0.0f};
    glm::vec3 Color{1.0f};
    float Intensity = 100.0f;
    float Range = 30.0f;
    glm::vec2 Size{2.0f, 1.0f};
    glm::vec2 Softness{0.15f, 0.15f};
    float BarnAngleDeg = 55.0f;
    float MinRoughness = 0.08f;
    bool CastsShadows = false;
    bool Enabled = true;
    std::shared_ptr<TextureData> Cookie;
};

} // namespace engine
