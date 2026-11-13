#include "Haki/Debug/EditorPanels.h"
#include "Haki/Debug/TelemetryOverlay.h"
#include "Haki/Scene/Components.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace
{

bool Near(float left, float right, float tolerance = 0.01f)
{
    return std::abs(left - right) < tolerance;
}

void Check(bool condition, const char *name, int &failures)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    failures += condition ? 0 : 1;
}

entt::entity Create(entt::registry &registry, const std::string &id, const std::string &parent = {})
{
    const entt::entity entity = registry.create();
    registry.emplace<Haki::EntityIdComponent>(entity, id);
    if (!parent.empty())
    {
        registry.emplace<Haki::ParentTransformComponent>(entity).ParentId = parent;
    }
    return entity;
}

const Haki::HierarchyTree::Node *FindNode(const Haki::HierarchyTree &tree, entt::entity entity)
{
    for (const auto &node : tree.Nodes)
    {
        if (node.Entity == entity) return &node;
    }
    return nullptr;
}

bool IsRoot(const Haki::HierarchyTree &tree, entt::entity entity)
{
    return std::any_of(tree.Roots.begin(), tree.Roots.end(),
                       [&](std::size_t index) { return tree.Nodes[index].Entity == entity; });
}

} // namespace

int main()
{
    using namespace Haki;
    int failures = 0;

    std::cout << "=== telemetry layout ===\n";
    glm::vec2 origin = ComputeTelemetryOrigin(true, 22.0f, true, 280.0f);
    Check(Near(origin.x, 292.0f) && Near(origin.y, 34.0f), "telemetry sits past the hierarchy pane and menu bar",
          failures);
    origin = ComputeTelemetryOrigin(false, 22.0f, false, 280.0f);
    Check(Near(origin.x, 12.0f) && Near(origin.y, 12.0f), "telemetry snaps to the corner when the editor is hidden",
          failures);
    origin = ComputeTelemetryOrigin(true, 22.0f, false, 280.0f);
    Check(Near(origin.x, 12.0f) && Near(origin.y, 34.0f), "only the visible menu bar offsets the telemetry",
          failures);

    const PaneWidthRange wide = GetPaneWidthRange(1920.0f);
    const PaneWidthRange narrow = GetPaneWidthRange(1000.0f);
    Check(Near(wide.Max, 420.0f) && Near(wide.Min, 220.0f), "panes are limited to 420 px on wide windows", failures);
    Check(Near(narrow.Max, 280.0f) && 2.0f * narrow.Max <= 0.56f * 1000.0f + 0.01f && narrow.Min <= narrow.Max,
          "on narrow windows both panes together leave the middle of the screen clear", failures);

    std::cout << "=== frame stats ===\n";
    FrameStats stats(1.0f);
    for (int index = 0; index < 59; ++index) stats.AddFrame(1.0f / 60.0f);
    stats.AddFrame(0.1f); // one hitch
    Check(Near(stats.GetMinimumFps(), 10.0f), "minimum FPS reports the slowest frame in the window", failures);
    Check(stats.GetAverageFps() > 50.0f && stats.GetAverageFps() < 60.0f, "average FPS counts frames over time",
          failures);
    for (int index = 0; index < 120; ++index) stats.AddFrame(1.0f / 60.0f);
    Check(Near(stats.GetMinimumFps(), 60.0f, 0.5f) && Near(stats.GetAverageFrameMs(), 16.67f, 0.05f),
          "frames older than the window drop out", failures);
    stats.AddFrame(0.0f);
    stats.AddFrame(std::nanf(""));
    Check(Near(stats.GetAverageFps(), 60.0f, 0.5f), "zero and non-finite frame times are ignored", failures);

    std::cout << "=== telemetry text ===\n";
    TelemetrySnapshot snapshot;
    snapshot.Gpu = "Test GPU";
    snapshot.GpuTimings.Valid = true;
    snapshot.GpuTimings.SsaoMs = 1.0f;
    snapshot.SsaoEnabled = false;
    snapshot.MemorySource = GpuInfo::MemorySource::Nvidia;
    snapshot.VramUsedMb = 1500;
    snapshot.VramTotalMb = 8192;
    auto lines = BuildTelemetryLines(snapshot);
    const auto line = [&lines](const std::string &label) {
        const auto found = std::find_if(lines.begin(), lines.end(),
                                        [&](const TelemetryLine &l) { return l.Label == label; });
        return found != lines.end() ? found->Value : std::string("<missing>");
    };
    Check(line("GPU") == "Test GPU" && line("VRAM") == "1500 / 8192 MB", "GPU name and VRAM usage are shown",
          failures);
    Check(line("GPU ms").find("ssao") == std::string::npos && line("GPU ms").find("shadow") != std::string::npos,
          "disabled passes are left out of the GPU breakdown", failures);
    Check(line("CPU ms").find("physics") != std::string::npos && line("CPU ms").find("lua") != std::string::npos,
          "the CPU breakdown lists engine systems", failures);
    snapshot.MemorySource = GpuInfo::MemorySource::None;
    snapshot.GpuTimings.Valid = false;
    lines = BuildTelemetryLines(snapshot);
    Check(line("VRAM").rfind("n/a", 0) == 0 && line("GPU ms").find("waiting") != std::string::npos,
          "missing VRAM queries and pending timers are explained", failures);

    std::cout << "=== hierarchy ===\n";
    entt::registry registry;
    const entt::entity player = Create(registry, "player");
    const entt::entity body = Create(registry, "body", "player");
    const entt::entity weapon = Create(registry, "weapon", "body");
    const entt::entity orphan = Create(registry, "orphan", "missing");
    const entt::entity loopA = Create(registry, "loop-a", "loop-b");
    const entt::entity loopB = Create(registry, "loop-b", "loop-a");
    const entt::entity unnamed = registry.create();
    registry.emplace<NameComponent>(player, "Player");

    const HierarchyTree tree = BuildHierarchyTree(registry);
    Check(tree.Nodes.size() == 7, "every entity appears in the tree", failures);
    const auto *playerNode = FindNode(tree, player);
    const auto *bodyNode = FindNode(tree, body);
    Check(playerNode && playerNode->Children.size() == 1 && tree.Nodes[playerNode->Children[0]].Entity == body &&
              bodyNode && bodyNode->Children.size() == 1 && tree.Nodes[bodyNode->Children[0]].Entity == weapon,
          "children nest under their parents", failures);
    Check(IsRoot(tree, player) && !IsRoot(tree, body) && IsRoot(tree, orphan) && IsRoot(tree, unnamed),
          "entities with a missing parent are listed at the top level", failures);
    Check(IsRoot(tree, loopA) && IsRoot(tree, loopB) && FindNode(tree, loopA)->Children.empty() &&
              FindNode(tree, loopB)->Children.empty(),
          "parent cycles are listed flat instead of recursing", failures);
    Check(tree.Nodes.front().Entity == player, "entities are listed in creation (scene file) order", failures);
    Check(GetEntityLabel(registry, player) == "Player" && GetEntityLabel(registry, body) == "body" &&
              GetEntityLabel(registry, unnamed).rfind("Entity #", 0) == 0,
          "labels prefer the name, then the authored id, then the EnTT index", failures);

    return failures == 0 ? 0 : 1;
}
