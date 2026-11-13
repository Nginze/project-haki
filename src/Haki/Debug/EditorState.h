#pragma once

#include <entt/entt.hpp>
#include <string>

namespace Haki
{

// Everything the runtime editor overlay toggles. Owned by ImGuiManager, read by Game.
struct EditorState
{
    // Panes and overlays (View menu).
    bool ShowHierarchy = true;
    bool ShowInspector = true;
    bool ShowTelemetry = true;
    float PaneOpacity = 0.72f;
    bool ShowGrid = false;
    bool ShowSelection = true; // highlight the selected entity in the 3D view

    // Fullscreen playtest (F11): hides the editor UI and debug overlays, and the window goes fullscreen.
    bool Playtest = false;
    bool PlaytestTelemetry = false; // telemetry brought back with F3 while playtesting

    entt::entity Selected = entt::null;

    // Physics menu.
    bool ShowRigidBodies = false;
    bool ShowSensors = false;
    bool ShowCharacters = false;
    bool SimulationPaused = false;
    bool StepRequested = false; // consumed by Game

    // Window menu requests, consumed by Game.
    int RequestedWidth = 0;
    int RequestedHeight = 0;
    bool RequestMaximize = false;
    bool Fullscreen = false;
    bool Vsync = true;
    bool AutosaveProjectSettings = true;
    bool SaveProjectSettings = false;
    std::string ProjectSaveStatus;

    // Layout of the visible editor UI this frame, in window pixels (0 when hidden).
    float MenuBarHeight = 0.0f;
    float HierarchyWidth = 0.0f;
    float InspectorWidth = 0.0f;

    bool EditorVisible() const { return !Playtest; }
    bool TelemetryVisible() const { return Playtest ? PlaytestTelemetry : ShowTelemetry; }
    bool PhysicsOverlayVisible() const { return !Playtest && (ShowRigidBodies || ShowSensors || ShowCharacters); }
};

} // namespace Haki
