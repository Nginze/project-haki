#pragma once
#include <entt/entt.hpp>
namespace Haki {
// TransformComponent remains a world pose. ParentTransformComponent stores the authored local pose.
class TransformHierarchySystem {
public:
    static void Update(entt::registry &registry);
};
}
