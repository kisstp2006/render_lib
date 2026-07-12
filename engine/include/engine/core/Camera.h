#pragma once

#include <glm/glm.hpp>

namespace engine {

class Input;

// Simple free-fly camera (Quake/Source style: WASD + mouse look, right-click to
// engage look in the sandbox app).
class Camera
{
public:
    void Update(const Input& input, float deltaTime, bool lookEnabled);

    glm::mat4 GetView() const;
    glm::mat4 GetProjection(float aspectRatio) const;

    glm::vec3 Position{0.0f, 1.8f, 6.0f};
    float Yaw = -90.0f;
    float Pitch = -10.0f;
    float MoveSpeed = 6.0f;
    float MouseSensitivity = 0.12f;
    float FovDegrees = 60.0f;
    float NearPlane = 0.05f;
    float FarPlane = 500.0f;

    glm::vec3 Forward() const;
    glm::vec3 Right() const;
};

} // namespace engine
