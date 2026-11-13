#include "Haki/Systems/TransformHierarchySystem.h"
#include "Haki/Scene/Components.h"
#include <glm/gtc/matrix_transform.hpp>
#include <functional>
#include <unordered_map>

namespace Haki {
namespace {
glm::mat4 Matrix(const TransformComponent &t) {
    return glm::translate(glm::mat4(1),t.Translation)*glm::mat4_cast(t.Rotation)*glm::scale(glm::mat4(1),t.Scale);
}
}
void TransformHierarchySystem::Update(entt::registry &registry) {
    std::unordered_map<std::string,entt::entity> ids;
    for (auto e:registry.view<EntityIdComponent>()) ids.emplace(registry.get<EntityIdComponent>(e).Id,e);
    std::unordered_map<entt::entity,int> state;
    std::function<bool(entt::entity)> resolve = [&](entt::entity e) {
        if (state[e]==2) return true;
        if (state[e]==1) return false;
        state[e]=1;
        auto &t=registry.get<TransformComponent>(e);
        if (auto *parent=registry.try_get<ParentTransformComponent>(e)) {
            auto it=ids.find(parent->ParentId);
            if(it==ids.end() || !registry.all_of<TransformComponent>(it->second) || !resolve(it->second)) return false;
            const auto world=registry.get<TransformComponent>(it->second).WorldMatrix*Matrix(parent->Local);
            t.Translation=glm::vec3(world[3]);
            t.Scale={glm::length(glm::vec3(world[0])),glm::length(glm::vec3(world[1])),glm::length(glm::vec3(world[2]))};
            if (t.Scale.x<=0 || t.Scale.y<=0 || t.Scale.z<=0) return false;
            t.Rotation=glm::normalize(glm::quat_cast(glm::mat3(glm::vec3(world[0])/t.Scale.x,
                glm::vec3(world[1])/t.Scale.y,glm::vec3(world[2])/t.Scale.z)));
            t.WorldMatrix=world;
        } else t.WorldMatrix=Matrix(t);
        state[e]=2;
        return true;
    };
    for(auto e:registry.view<TransformComponent>()) resolve(e);
}
}
