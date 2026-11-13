#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Haki
{

class DebugDraw;
class Scene;
struct EditorState;

// Entities arranged by ParentTransformComponent: Roots index into Nodes, each node lists its children.
// Entities whose parent is missing become roots; members of a parent cycle are also listed as roots,
// so every entity appears exactly once.
struct HierarchyTree
{
    struct Node
    {
        entt::entity Entity = entt::null;
        std::vector<std::size_t> Children;
    };
    std::vector<Node> Nodes;
    std::vector<std::size_t> Roots;
};
HierarchyTree BuildHierarchyTree(const entt::registry &registry);

// Display name: NameComponent, else the authored entity ID, else "Entity #<index>".
std::string GetEntityLabel(const entt::registry &registry, entt::entity entity);

// Width range of a side pane, so both panes together never cover the middle of the screen.
struct PaneWidthRange
{
    float Min = 0.0f;
    float Max = 0.0f;
};
PaneWidthRange GetPaneWidthRange(float screenWidth);

// The fixed left (hierarchy) and right (inspector) editor panes.
class EditorPanels
{
public:
    // Both return the width the pane occupies (0 when hidden or closed with its x button).
    float DrawHierarchy(Scene *scene, EditorState &state, float top, float screenWidth, float screenHeight);
    float DrawInspector(Scene *scene, EditorState &state, float top, float screenWidth, float screenHeight);

    // Axis cross and bounds of the selected entity in the 3D view.
    static void DrawSelection(Scene &scene, entt::entity entity, DebugDraw &draw);

private:
    char filter_[128] = {};
    int clipIndex_ = 0;
    entt::entity clipIndexEntity_ = entt::null;
};

} // namespace Haki
