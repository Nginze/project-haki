#pragma once

#include "Haki/Renderer/RendererTypes.h"

#include <array>

namespace Haki
{

std::array<float, ShadowCascadeCount> CalculatePssmSplits(float nearPlane, float farPlane, float lambda);

} // namespace Haki
