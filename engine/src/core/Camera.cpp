#include "engine/core/Camera.h"
#include "engine/core/Input.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

glm::vec3 Camera::Forward() const
{
    glm::vec3 dir;
    dir.x = std::cos(glm::radians(Yaw)) * std::cos(glm::radians(Pitch));
    dir.y = std::sin(glm::radians(Pitch));
    dir.z = std::sin(glm::radians(Yaw)) * std::cos(glm::radians(Pitch));
    return glm::normalize(dir);
}

glm::vec3 Camera::Right() const
{
    return glm::normalize(glm::cross(Forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

void Camera::Update(const Input& input, float deltaTime, bool lookEnabled)
{
    if (lookEnabled)
    {
        glm::vec2 delta = input.GetMouseDelta();
        Yaw += delta.x * MouseSensitivity;
        Pitch -= delta.y * MouseSensitivity;
        Pitch = glm::clamp(Pitch, -89.0f, 89.0f);
    }

    const glm::vec3 forward = Forward();
    const glm::vec3 right = Right();
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    float speed = MoveSpeed * deltaTime;
    if (input.IsKeyDown(GLFW_KEY_LEFT_SHIFT))
        speed *= 3.0f;

    if (input.IsKeyDown(GLFW_KEY_W)) Position += forward * speed;
    if (input.IsKeyDown(GLFW_KEY_S)) Position -= forward * speed;
    if (input.IsKeyDown(GLFW_KEY_D)) Position += right * speed;
    if (input.IsKeyDown(GLFW_KEY_A)) Position -= right * speed;
    if (input.IsKeyDown(GLFW_KEY_E)) Position += up * speed;
    if (input.IsKeyDown(GLFW_KEY_Q)) Position -= up * speed;
}

glm::mat4 Camera::GetView() const
{
    return glm::lookAt(Position, Position + Forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjection(float aspectRatio) const
{
    return glm::perspective(glm::radians(FovDegrees), aspectRatio, NearPlane, FarPlane);
}

} // namespace engine
