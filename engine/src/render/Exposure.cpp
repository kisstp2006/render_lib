#include "engine/render/Exposure.h"

#include <algorithm>
#include <cmath>

namespace engine {

float AdaptExposure(float currentExposure, float averageLuminance,
                    float key, float minimum, float maximum,
                    float speed, float deltaTime)
{
    const float safeLuminance = std::max(averageLuminance, 1e-4f);
    const float target = std::clamp(key / safeLuminance, minimum, maximum);
    const float blend = 1.0f - std::exp(-std::max(deltaTime, 0.0f)
                                        * std::max(speed, 0.0f));
    return currentExposure + (target - currentExposure) * blend;
}

} // namespace engine
