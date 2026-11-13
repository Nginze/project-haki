#include "Haki/Debug/EditorPanels.h"

#include "Haki/AssetManager/AnimationClip.h"
#include "Haki/AssetManager/AssetManager.h"
#include "Haki/Debug/EditorState.h"
#include "Haki/GL/Mesh.h"
#include "Haki/Renderer/DebugDraw.h"
#include "Haki/Scene/Components.h"
#include "Haki/Scene/Scene.h"
#include "Haki/Systems/AnimationSystem.h"

#include <imgui.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Haki
{
namespace
{

constexpr ImGuiWindowFlags PaneFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
constexpr float DefaultPaneWidth = 280.0f;

const char *ToString(PhysicsMotionType motion)
{
    switch (motion)
    {
    case PhysicsMotionType::Static: return "Static";
    case PhysicsMotionType::Kinematic: return "Kinematic";
    case PhysicsMotionType::Dynamic: return "Dynamic";
    }
    return "?";
}

const char *ToString(PhysicsLayer layer)
{
    switch (layer)
    {
    case PhysicsLayer::Static: return "Static";
    case PhysicsLayer::Dynamic: return "Dynamic";
    case PhysicsLayer::Character: return "Character";
    case PhysicsLayer::Trigger: return "Trigger";
    case PhysicsLayer::Count: break;
    }
    return "?";
}

// Short type tags shown dimmed after each name in the hierarchy.
std::string EntityTags(const entt::registry &registry, entt::entity entity)
{
    std::string tags;
    const auto tag = [&tags](bool present, const char *text) {
        if (present) tags += text;
    };
    tag(registry.all_of<CameraComponent>(entity), " [cam]");
    tag(registry.any_of<DirectionalLightComponent, PointLightComponent, SpotLightComponent>(entity), " [light]");
    tag(registry.all_of<RigidBodyComponent>(entity), " [body]");
    tag(registry.all_of<CharacterControllerComponent>(entity), " [char]");
    tag(registry.all_of<AnimatorComponent>(entity), " [anim]");
    tag(registry.all_of<ScriptComponent>(entity), " [lua]");
    return tags;
}

void ReadOnlyRow(const char *label, const char *format, auto... values)
{
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(110.0f);
    ImGui::Text(format, values...);
}

void SetWorldMatrix(TransformComponent &transform)
{
    transform.WorldMatrix = glm::translate(glm::mat4(1.0f), transform.Translation) *
                            glm::mat4_cast(transform.Rotation) * glm::scale(glm::mat4(1.0f), transform.Scale);
}

// Position / rotation (Euler degrees) / scale editors. Returns true when anything changed.
bool EditTransform(TransformComponent &transform)
{
    bool changed = ImGui::DragFloat3("Position", &transform.Translation.x, 0.05f);
    glm::vec3 degrees = glm::degrees(glm::eulerAngles(glm::normalize(transform.Rotation)));
    if (ImGui::DragFloat3("Rotation", &degrees.x, 0.25f))
    {
        transform.Rotation = glm::normalize(glm::quat(glm::radians(degrees)));
        changed = true;
    }
    if (ImGui::DragFloat3("Scale", &transform.Scale.x, 0.01f, 0.001f, 1000.0f, "%.3f"))
    {
        transform.Scale = glm::max(transform.Scale, glm::vec3(0.001f));
        changed = true;
    }
    return changed;
}

template <typename Light>
void EditCommonLight(Light &light)
{
    if (!std::isfinite(light.Color.x) || !std::isfinite(light.Color.y) || !std::isfinite(light.Color.z))
    {
        light.Color = glm::vec3(1.0f);
    }
    if (!std::isfinite(light.Intensity) || light.Intensity < 0.0f)
    {
        light.Intensity = 0.0f;
    }
    ImGui::ColorEdit3("Color", &light.Color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Intensity", &light.Intensity, 0.05f, 0.0f, std::numeric_limits<float>::max(), "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
}

void EditRange(float &range)
{
    if (!std::isfinite(range) || range <= 0.0f)
    {
        range = 10.0f;
    }
    ImGui::DragFloat("Range", &range, 0.1f, 0.01f, std::numeric_limits<float>::max(), "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
}

void EditLight(SpotLightComponent &light)
{
    constexpr float AngleStep = 0.1f;
    constexpr float MaximumAngle = 90.0f - AngleStep;
    EditCommonLight(light);
    EditRange(light.Range);
    if (!std::isfinite(light.InnerAngleDegrees) || !std::isfinite(light.OuterAngleDegrees))
    {
        light.InnerAngleDegrees = 20.0f;
        light.OuterAngleDegrees = 30.0f;
    }
    light.OuterAngleDegrees = std::clamp(light.OuterAngleDegrees, AngleStep, MaximumAngle);
    light.InnerAngleDegrees = std::clamp(light.InnerAngleDegrees, 0.0f, light.OuterAngleDegrees - AngleStep);
    ImGui::DragFloat("Inner Angle", &light.InnerAngleDegrees, AngleStep, 0.0f, light.OuterAngleDegrees - AngleStep,
                     "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Outer Angle", &light.OuterAngleDegrees, AngleStep, light.InnerAngleDegrees + AngleStep,
                     MaximumAngle, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
}

void DrawTransformSection(Scene &scene, entt::entity entity)
{
    entt::registry &registry = scene.GetRegistry();
    auto *transform = registry.try_get<TransformComponent>(entity);
    if (!transform || !ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    if (auto *parent = registry.try_get<ParentTransformComponent>(entity))
    {
        // The world pose is rebuilt from the parent every frame, so edit the local offset instead.
        ImGui::TextDisabled("Local to parent '%s'", parent->ParentId.c_str());
        EditTransform(parent->Local);
        ReadOnlyRow("World", "%.2f, %.2f, %.2f", transform->Translation.x, transform->Translation.y,
                    transform->Translation.z);
        return;
    }
    if (EditTransform(*transform))
    {
        SetWorldMatrix(*transform);
        // Characters and kinematic bodies follow their transform on their own; others need a teleport.
        if (registry.all_of<RigidBodyComponent>(entity))
        {
            scene.GetPhysics().Teleport(registry, entity);
        }
    }
}

void DrawPhysicsSections(Scene &scene, entt::entity entity)
{
    entt::registry &registry = scene.GetRegistry();
    PhysicsSystem &physics = scene.GetPhysics();
    if (auto *body = registry.try_get<RigidBodyComponent>(entity);
        body && ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ReadOnlyRow("Motion", "%s", ToString(body->Motion));
        ReadOnlyRow("Layer", "%s%s", ToString(body->Layer), body->IsSensor ? " (sensor)" : "");
        ReadOnlyRow("Mass", body->Mass > 0.0f ? "%.2f kg" : "auto", body->Mass);
        if (body->BodyId == InvalidPhysicsBodyId)
        {
            ReadOnlyRow("Body", "%s", "not created yet");
        }
        else
        {
            ReadOnlyRow("Body", "#%u  %s", body->BodyId, physics.IsBodyAwake(registry, entity) ? "awake" : "sleeping");
        }
        bool material = ImGui::DragFloat("Friction", &body->Friction, 0.01f, 0.0f, 2.0f, "%.2f",
                                         ImGuiSliderFlags_AlwaysClamp);
        material |= ImGui::DragFloat("Restitution", &body->Restitution, 0.01f, 0.0f, 1.0f, "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
        ImGui::BeginDisabled(body->Motion != PhysicsMotionType::Dynamic);
        material |= ImGui::DragFloat("Gravity Factor", &body->GravityFactor, 0.01f, -10.0f, 10.0f, "%.2f",
                                     ImGuiSliderFlags_AlwaysClamp);
        if (const auto velocity = physics.GetLinearVelocity(registry, entity))
        {
            glm::vec3 edited = *velocity;
            if (ImGui::DragFloat3("Velocity", &edited.x, 0.05f))
            {
                physics.SetLinearVelocity(registry, entity, edited);
            }
        }
        ImGui::EndDisabled();
        if (material)
        {
            physics.ApplyBodyMaterial(registry, entity);
        }
    }
    if (const auto *collider = registry.try_get<ColliderComponent>(entity);
        collider && ImGui::CollapsingHeader("Collider"))
    {
        switch (collider->Shape)
        {
        case ColliderShape::Box:
            ReadOnlyRow("Box", "half %.2f x %.2f x %.2f", collider->HalfExtents.x, collider->HalfExtents.y,
                        collider->HalfExtents.z);
            break;
        case ColliderShape::Sphere: ReadOnlyRow("Sphere", "r %.2f", collider->Radius); break;
        case ColliderShape::Capsule:
            ReadOnlyRow("Capsule", "r %.2f  half height %.2f", collider->Radius, collider->HalfHeight);
            break;
        }
        ReadOnlyRow("Offset", "%.2f, %.2f, %.2f", collider->Offset.x, collider->Offset.y, collider->Offset.z);
        ImGui::TextDisabled("Shapes are baked into Jolt; edit them in Blender.");
    }
    if (auto *character = registry.try_get<CharacterControllerComponent>(entity);
        character && ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ReadOnlyRow("Grounded", "%s", character->Grounded ? "yes" : "no");
        ReadOnlyRow("Velocity", "%.2f, %.2f, %.2f  (%.2f m/s)", character->Velocity.x, character->Velocity.y,
                    character->Velocity.z, glm::length(glm::vec2(character->Velocity.x, character->Velocity.z)));
        ReadOnlyRow("Ground N", "%.2f, %.2f, %.2f", character->GroundNormal.x, character->GroundNormal.y,
                    character->GroundNormal.z);
        ImGui::DragFloat3("Desired Vel", &character->DesiredVelocity.x, 0.05f);
        ImGui::SetItemTooltip("Usually overwritten every tick by the player script or controller.");
        ImGui::DragFloat("Gravity Factor", &character->GravityFactor, 0.01f, -10.0f, 10.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Step Height", &character->StepHeight, 0.01f, 0.0f, 2.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp);
        ReadOnlyRow("Capsule", "r %.2f  half height %.2f", character->Radius, character->HalfHeight);
    }
    if (auto *player = registry.try_get<PlayerInputComponent>(entity);
        player && ImGui::CollapsingHeader("Player Input", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::DragFloat("Move Speed", &player->MoveSpeed, 0.05f, 0.0f, 50.0f, "%.2f m/s",
                         ImGuiSliderFlags_AlwaysClamp);
    }
}

void DrawRenderingSections(entt::registry &registry, entt::entity entity)
{
    if (auto *camera = registry.try_get<CameraComponent>(entity);
        camera && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int projection = static_cast<int>(camera->Projection);
        if (ImGui::Combo("Projection", &projection, "Perspective\0Orthographic\0"))
        {
            camera->Projection = static_cast<CameraProjection>(projection);
        }
        if (camera->Projection == CameraProjection::Perspective)
        {
            ImGui::DragFloat("FOV Y", &camera->FieldOfViewDegrees, 0.25f, 1.0f, 179.0f, "%.1f deg",
                             ImGuiSliderFlags_AlwaysClamp);
        }
        else
        {
            ImGui::DragFloat("Ortho Size", &camera->OrthographicSize, 0.1f, 0.5f, 500.0f, "%.1f m",
                             ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::DragFloat("Near", &camera->NearPlane, 0.01f, 0.001f, camera->FarPlane * 0.99f, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Far", &camera->FarPlane, 1.0f, camera->NearPlane * 1.01f, 100000.0f, "%.1f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Primary", &camera->Primary);
    }
    if (auto *light = registry.try_get<DirectionalLightComponent>(entity);
        light && ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditCommonLight(*light);
    }
    if (auto *light = registry.try_get<PointLightComponent>(entity);
        light && ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditCommonLight(*light);
        EditRange(light->Range);
    }
    if (auto *light = registry.try_get<SpotLightComponent>(entity);
        light && ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        EditLight(*light);
    }
    if (const auto *mesh = registry.try_get<MeshRendererComponent>(entity);
        mesh && ImGui::CollapsingHeader("Mesh Renderer"))
    {
        const Mesh *resource = AssetManager::GetMesh(mesh->Mesh);
        ReadOnlyRow("Submeshes", "%u", resource ? resource->GetSubmeshCount() : 0u);
        ReadOnlyRow("Materials", "%zu", mesh->Materials.size());
    }
    if (const auto *mesh = registry.try_get<SkinnedMeshRendererComponent>(entity);
        mesh && ImGui::CollapsingHeader("Skinned Mesh Renderer"))
    {
        const Mesh *resource = AssetManager::GetMesh(mesh->Mesh);
        ReadOnlyRow("Submeshes", "%u", resource ? resource->GetSubmeshCount() : 0u);
        ReadOnlyRow("Materials", "%zu", mesh->Materials.size());
        ReadOnlyRow("Bones", "%zu", mesh->FinalBoneMatrices.size());
    }
}

void DrawScriptSection(Scene &scene, entt::entity entity)
{
    entt::registry &registry = scene.GetRegistry();
    auto *script = registry.try_get<ScriptComponent>(entity);
    if (!script || !ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    ReadOnlyRow("Path", "%s", script->Path.c_str());
    ScriptSystem &scripts = scene.GetScripts();
    auto properties = scripts.GetInstanceProperties(entity);
    if (properties.empty())
    {
        ImGui::TextDisabled("No live properties (not running yet, or none declared).");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Live values: edits change the running instance, not the scene file.");
    ImGui::PopStyleColor();
    for (auto &[name, value] : properties)
    {
        ImGui::PushID(name.c_str());
        bool changed = false;
        if (auto *flag = std::get_if<bool>(&value))
        {
            changed = ImGui::Checkbox(name.c_str(), flag);
        }
        else if (auto *number = std::get_if<double>(&value))
        {
            changed = ImGui::DragScalar(name.c_str(), ImGuiDataType_Double, number, 0.05f, nullptr, nullptr, "%.3f");
        }
        else if (auto *text = std::get_if<std::string>(&value))
        {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "%s", text->c_str());
            if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer)))
            {
                *text = buffer;
                changed = true;
            }
        }
        else if (auto *vector = std::get_if<glm::vec3>(&value))
        {
            changed = ImGui::DragFloat3(name.c_str(), &vector->x, 0.05f);
        }
        if (changed)
        {
            scripts.SetInstanceProperty(entity, name, value);
        }
        ImGui::PopID();
    }
}

} // namespace

HierarchyTree BuildHierarchyTree(const entt::registry &registry)
{
    HierarchyTree tree;
    std::unordered_map<entt::entity, std::size_t> indexOf;
    std::unordered_map<std::string, entt::entity> ids;
    for (const entt::entity entity : registry.view<entt::entity>())
    {
        indexOf.emplace(entity, tree.Nodes.size());
        tree.Nodes.push_back({entity, {}});
        if (const auto *id = registry.try_get<EntityIdComponent>(entity))
        {
            ids.emplace(id->Id, entity);
        }
    }
    // Creation order reads naturally (scene file order); EnTT iterates newest first.
    std::reverse(tree.Nodes.begin(), tree.Nodes.end());
    for (std::size_t index = 0; index < tree.Nodes.size(); ++index)
    {
        indexOf[tree.Nodes[index].Entity] = index;
    }

    std::vector<bool> attached(tree.Nodes.size(), false);
    for (std::size_t index = 0; index < tree.Nodes.size(); ++index)
    {
        const auto *parent = registry.try_get<ParentTransformComponent>(tree.Nodes[index].Entity);
        const auto found = parent ? ids.find(parent->ParentId) : ids.end();
        if (found != ids.end() && found->second != tree.Nodes[index].Entity)
        {
            tree.Nodes[indexOf.at(found->second)].Children.push_back(index);
            attached[index] = true;
        }
        else
        {
            tree.Roots.push_back(index);
        }
    }

    // Anything not reachable from a root sits in a parent cycle: list it at the top level, without children,
    // so the hierarchy view never recurses forever.
    std::vector<bool> reached(tree.Nodes.size(), false);
    std::vector<std::size_t> stack(tree.Roots.begin(), tree.Roots.end());
    while (!stack.empty())
    {
        const std::size_t index = stack.back();
        stack.pop_back();
        if (reached[index]) continue;
        reached[index] = true;
        for (const std::size_t child : tree.Nodes[index].Children) stack.push_back(child);
    }
    for (std::size_t index = 0; index < tree.Nodes.size(); ++index)
    {
        if (!reached[index])
        {
            tree.Roots.push_back(index);
            tree.Nodes[index].Children.clear();
        }
    }
    return tree;
}

std::string GetEntityLabel(const entt::registry &registry, entt::entity entity)
{
    if (const auto *name = registry.try_get<NameComponent>(entity); name && !name->Name.empty())
    {
        return name->Name;
    }
    if (const auto *id = registry.try_get<EntityIdComponent>(entity); id && !id->Id.empty())
    {
        return id->Id;
    }
    return "Entity #" + std::to_string(entt::to_integral(entity));
}

PaneWidthRange GetPaneWidthRange(float screenWidth)
{
    constexpr float MinimumWidth = 220.0f;
    constexpr float MaximumWidth = 420.0f;
    constexpr float MaximumScreenShare = 0.28f; // per pane: the middle ~44% always stays clear
    const float maximum = std::max(std::min(MaximumWidth, screenWidth * MaximumScreenShare), 120.0f);
    return {std::min(MinimumWidth, maximum), maximum};
}

float EditorPanels::DrawHierarchy(Scene *scene, EditorState &state, float top, float screenWidth, float screenHeight)
{
    if (!state.ShowHierarchy)
    {
        return 0.0f;
    }
    const float height = std::max(screenHeight - top, 1.0f);
    const PaneWidthRange range = GetPaneWidthRange(screenWidth);
    ImGui::SetNextWindowPos({0.0f, top}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({std::clamp(DefaultPaneWidth, range.Min, range.Max), height}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({range.Min, height}, {range.Max, height});
    float width = 0.0f;
    bool open = true;
    if (ImGui::Begin("Scene Hierarchy", &open, PaneFlags))
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Search entities  (a,b  -exclude)", filter_, sizeof(filter_));
        const ImGuiTextFilter filter(filter_);
        if (!scene)
        {
            ImGui::TextDisabled("No active scene");
        }
        else
        {
            entt::registry &registry = scene->GetRegistry();
            if (state.Selected != entt::null && !registry.valid(state.Selected))
            {
                state.Selected = entt::null;
            }
            const HierarchyTree tree = BuildHierarchyTree(registry);
            ImGui::TextDisabled("%zu entities", tree.Nodes.size());
            ImGui::Separator();
            ImGui::BeginChild("##entities");

            const auto drawLeafLabel = [&](entt::entity entity, const std::string &label) {
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextDisabled("%s", EntityTags(registry, entity).c_str());
                (void)label;
            };
            if (filter.IsActive())
            {
                // Flat list of matches while searching.
                for (const HierarchyTree::Node &node : tree.Nodes)
                {
                    const std::string label = GetEntityLabel(registry, node.Entity);
                    const auto *id = registry.try_get<EntityIdComponent>(node.Entity);
                    if (!filter.PassFilter(label.c_str()) && !(id && filter.PassFilter(id->Id.c_str())))
                    {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(entt::to_integral(node.Entity)));
                    if (ImGui::Selectable(label.c_str(), state.Selected == node.Entity))
                    {
                        state.Selected = node.Entity;
                    }
                    drawLeafLabel(node.Entity, label);
                    ImGui::PopID();
                }
            }
            else
            {
                const auto drawNode = [&](const auto &self, std::size_t index) -> void {
                    const HierarchyTree::Node &node = tree.Nodes[index];
                    const std::string label = GetEntityLabel(registry, node.Entity);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                               ImGuiTreeNodeFlags_OpenOnDoubleClick;
                    if (node.Children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (state.Selected == node.Entity) flags |= ImGuiTreeNodeFlags_Selected;
                    ImGui::PushID(static_cast<int>(entt::to_integral(node.Entity)));
                    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    {
                        state.Selected = node.Entity;
                    }
                    drawLeafLabel(node.Entity, label);
                    if (open && !node.Children.empty())
                    {
                        for (const std::size_t child : node.Children) self(self, child);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                };
                for (const std::size_t root : tree.Roots) drawNode(drawNode, root);
            }
            ImGui::EndChild();
        }
        width = ImGui::GetWindowWidth();
    }
    ImGui::End();
    if (!open)
    {
        state.ShowHierarchy = false;
        return 0.0f;
    }
    return width;
}

float EditorPanels::DrawInspector(Scene *scene, EditorState &state, float top, float screenWidth, float screenHeight)
{
    if (!state.ShowInspector)
    {
        return 0.0f;
    }
    const float height = std::max(screenHeight - top, 1.0f);
    const PaneWidthRange range = GetPaneWidthRange(screenWidth);
    ImGui::SetNextWindowPos({screenWidth, top}, ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowSize({std::clamp(DefaultPaneWidth + 40.0f, range.Min, range.Max), height},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({range.Min, height}, {range.Max, height});
    float width = 0.0f;
    bool open = true;
    if (ImGui::Begin("Inspector", &open, PaneFlags))
    {
        // Leave ~40% of the pane for labels so names like "Gravity Factor" are not clipped.
        ImGui::PushItemWidth(-std::max(110.0f, ImGui::GetContentRegionAvail().x * 0.42f));
        entt::registry *registry = scene ? &scene->GetRegistry() : nullptr;
        const entt::entity entity = state.Selected;
        if (!registry || entity == entt::null || !registry->valid(entity))
        {
            ImGui::TextDisabled("Select an entity in the Scene Hierarchy.");
        }
        else
        {
            ImGui::TextUnformatted(GetEntityLabel(*registry, entity).c_str());
            const auto *id = registry->try_get<EntityIdComponent>(entity);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("id %s  |  entt #%u", id ? id->Id.c_str() : "-",
                               static_cast<unsigned>(entt::to_integral(entity)));
            ImGui::PopStyleColor();
            ImGui::Separator();
            DrawTransformSection(*scene, entity);
            DrawPhysicsSections(*scene, entity);
            DrawRenderingSections(*registry, entity);

            if (auto *animator = registry->try_get<AnimatorComponent>(entity);
                animator && ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const bool locomotion = animator->Source == AnimatorSource::Locomotion;
                ReadOnlyRow("Playing", "%s", locomotion ? "locomotion blend" : animator->CurrentClipName.c_str());
                if (!locomotion)
                {
                    ReadOnlyRow("Time", "%.2f s%s", animator->CurrentTime,
                                animator->OneShotFinished ? "  (finished)" : animator->Loop ? "  (loop)" : "");
                }
                if (animator->FadeDuration > 0.0f)
                {
                    ReadOnlyRow("Fade", "%.0f%%", 100.0f * animator->FadeElapsed / animator->FadeDuration);
                }
                ImGui::DragFloat("Speed", &animator->PlaybackSpeed, 0.01f, -4.0f, 4.0f, "%.2f",
                                 ImGuiSliderFlags_AlwaysClamp);
                ImGui::DragFloat("Default Fade", &animator->DefaultFade, 0.01f, 0.0f, 2.0f, "%.2f s",
                                 ImGuiSliderFlags_AlwaysClamp);

                std::vector<const std::string *> names;
                for (const auto &[name, handle] : animator->Clips) names.push_back(&name);
                std::sort(names.begin(), names.end(), [](const auto *a, const auto *b) { return *a < *b; });
                if (clipIndexEntity_ != entity)
                {
                    clipIndexEntity_ = entity;
                    clipIndex_ = 0;
                }
                clipIndex_ = std::clamp(clipIndex_, 0, std::max(static_cast<int>(names.size()) - 1, 0));
                if (!names.empty())
                {
                    if (ImGui::BeginCombo("Clip", names[static_cast<std::size_t>(clipIndex_)]->c_str()))
                    {
                        for (std::size_t index = 0; index < names.size(); ++index)
                        {
                            if (ImGui::Selectable(names[index]->c_str(), static_cast<int>(index) == clipIndex_))
                            {
                                clipIndex_ = static_cast<int>(index);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::Button("Play (0.2 s fade)"))
                    {
                        AnimationSystem::Play(*animator, *names[static_cast<std::size_t>(clipIndex_)], 0.2f);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Loop"))
                    {
                        AnimationSystem::Play(*animator, *names[static_cast<std::size_t>(clipIndex_)], 0.2f, true);
                    }
                }
                if (auto *blend = registry->try_get<LocomotionAnimationComponent>(entity))
                {
                    ImGui::BeginDisabled(locomotion);
                    if (ImGui::Button("Back to Locomotion"))
                    {
                        AnimationSystem::ReturnToLocomotion(*animator, blend->Fade);
                    }
                    ImGui::EndDisabled();
                    ImGui::SeparatorText("Locomotion");
                    ReadOnlyRow("Speed", "%.2f m/s", animator->LocomotionSpeed);
                    ReadOnlyRow("Blend", "%.2f", animator->LocomotionPosition);
                    for (const LocomotionSample &sample : AnimationSystem::GetLocomotionSamples(*blend))
                    {
                        ImGui::BulletText("%s @ %.2f m/s", sample.Clip.c_str(), sample.Speed);
                    }
                    ImGui::DragFloat("Fade Back", &blend->Fade, 0.01f, 0.0f, 2.0f, "%.2f s",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Smoothing", &blend->Smoothing, 0.005f, 0.0f, 1.0f, "%.3f s",
                                     ImGuiSliderFlags_AlwaysClamp);
                    ImGui::DragFloat("Max Speedup", &blend->MaxPlaybackScale, 0.01f, 1.0f, 4.0f, "%.2fx",
                                     ImGuiSliderFlags_AlwaysClamp);
                }
            }
            DrawScriptSection(*scene, entity);
        }
        ImGui::PopItemWidth();
        width = ImGui::GetWindowWidth();
    }
    ImGui::End();
    if (!open)
    {
        state.ShowInspector = false;
        return 0.0f;
    }
    return width;
}

void EditorPanels::DrawSelection(Scene &scene, entt::entity entity, DebugDraw &draw)
{
    entt::registry &registry = scene.GetRegistry();
    const auto *transform = registry.valid(entity) ? registry.try_get<TransformComponent>(entity) : nullptr;
    if (!transform)
    {
        return;
    }
    const glm::vec4 highlight{1.0f, 0.65f, 0.1f, 1.0f};
    draw.Axes(transform->WorldMatrix, 0.75f, false);

    const auto *mesh = registry.try_get<MeshRendererComponent>(entity);
    const auto *skinned = registry.try_get<SkinnedMeshRendererComponent>(entity);
    const Mesh *resource = AssetManager::GetMesh(mesh ? mesh->Mesh : skinned ? skinned->Mesh : MeshHandle{});
    if (resource)
    {
        const MeshBounds &bounds = resource->GetBounds();
        draw.Box(transform->WorldMatrix, bounds.Center, bounds.Extents, highlight, false);
    }
    else if (const auto *character = registry.try_get<CharacterControllerComponent>(entity))
    {
        const float halfHeight = character->HalfHeight + character->Radius;
        draw.Box(transform->Translation + glm::vec3(0.0f, halfHeight, 0.0f),
                 glm::vec3(character->Radius, halfHeight, character->Radius), highlight, false);
    }
    else if (const auto *collider = registry.try_get<ColliderComponent>(entity))
    {
        const glm::vec3 extents = collider->Shape == ColliderShape::Box ? collider->HalfExtents
                                  : collider->Shape == ColliderShape::Sphere
                                      ? glm::vec3(collider->Radius)
                                      : glm::vec3(collider->Radius, collider->HalfHeight + collider->Radius,
                                                  collider->Radius);
        const glm::mat4 pose = glm::translate(glm::mat4(1.0f), transform->Translation) *
                               glm::mat4_cast(transform->Rotation) * glm::scale(glm::mat4(1.0f), transform->Scale);
        draw.Box(pose, collider->Offset, extents, highlight, false);
    }
}

} // namespace Haki
