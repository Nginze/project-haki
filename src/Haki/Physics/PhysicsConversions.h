#pragma once

#include <Jolt/Jolt.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Haki
{

inline JPH::Vec3 ToJolt(const glm::vec3 &value)
{
    return JPH::Vec3(value.x, value.y, value.z);
}

inline JPH::Quat ToJolt(const glm::quat &value)
{
    return JPH::Quat(value.x, value.y, value.z, value.w);
}

inline glm::vec3 ToGlm(JPH::Vec3Arg value)
{
    return {value.GetX(), value.GetY(), value.GetZ()};
}

inline glm::quat ToGlm(JPH::QuatArg value)
{
    // glm::quat's constructor takes w first.
    return {value.GetW(), value.GetX(), value.GetY(), value.GetZ()};
}

} // namespace Haki
