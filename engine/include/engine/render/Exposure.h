#pragma once

namespace engine {

// Backend-neutral temporal adaptation used after either backend has measured
// the scene luminance with its native GPU path.
float AdaptExposure(float currentExposure, float averageLuminance,
                    float key, float minimum, float maximum,
                    float speed, float deltaTime);

} // namespace engine
