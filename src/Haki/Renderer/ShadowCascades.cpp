#include "Haki/Renderer/ShadowCascades.h"

#include <algorithm>
#include <cmath>

namespace Haki
{

std::array<float, ShadowCascadeCount> CalculatePssmSplits(float nearPlane, float farPlane, float lambda)
{
    const float nearDistance = std::max(nearPlane, 0.001f);
    const float farDistance = std::max(farPlane, nearDistance + 0.001f);
    const float splitLambda = std::clamp(lambda, 0.0f, 1.0f);
    std::array<float, ShadowCascadeCount> splits{};

    for (std::size_t index = 0; index < ShadowCascadeCount; ++index)
    {
        const float ratio = static_cast<float>(index + 1) / static_cast<float>(ShadowCascadeCount);
        const float logarithmic = nearDistance * std::pow(farDistance / nearDistance, ratio);
        const float uniform = nearDistance + (farDistance - nearDistance) * ratio;
        splits[index] = std::lerp(uniform, logarithmic, splitLambda);
    }
    splits.back() = farDistance;
    return splits;
}

} // namespace Haki
