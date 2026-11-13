#pragma once

#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Haki
{

    struct PoseNode
    {
        std::string Name;
        std::int32_t ParentIndex = -1;
        std::uint32_t SourceNodeIndex = 0;

        glm::vec3 BindTranslation{0.0f};
        glm::quat BindRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 BindScale{1.0f};
    };

    struct Joint
    {
        std::uint32_t NodeIndex = 0;
        glm::mat4 InverseBindMatrix{1.0f};
    };

    class Skeleton
    {
    public:
        // Nodes are parent-before-child pose transforms. Joints retain glTF palette order.
        std::vector<PoseNode> Nodes;
        std::vector<Joint> Joints;
        glm::mat4 InverseMeshBindWorld{1.0f};
        std::unordered_map<std::string, std::uint32_t> NodeNameToIndex;
        std::unordered_map<std::string, std::uint32_t> JointNameToIndex;
    };

} // namespace Haki
