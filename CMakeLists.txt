cmake_minimum_required(VERSION 3.16)
project(Engine VERSION 1.0.0 LANGUAGES C CXX)

option(HAKI_ENABLE_DEBUG_CAMERA "Enable the development free-look camera" ON)
option(HAKI_ENABLE_IMGUI "Enable Dear ImGui debug tooling" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(CTest)


# Fetch Remote libraries

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4 
)

set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(GLFW_INSTALL OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(glfw)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1 
)
FetchContent_MakeAvailable(glm)

# JSON
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# cgltf
FetchContent_Declare(
    cgltf_source
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
)
FetchContent_GetProperties(cgltf_source)
if(NOT cgltf_source_POPULATED)
    FetchContent_Populate(cgltf_source)
endif()
add_library(cgltf_headers INTERFACE)
target_include_directories(cgltf_headers INTERFACE ${cgltf_source_SOURCE_DIR})

# Jolt Physics
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.3.0
    SOURCE_SUBDIR  Build
)

# The engine relies on RTTI (dynamic_cast, std::any) and exceptions (nlohmann_json).
set(CPP_RTTI_ENABLED ON CACHE INTERNAL "")
set(CPP_EXCEPTIONS_ENABLED ON CACHE INTERNAL "")
set(OVERRIDE_CXX_FLAGS OFF CACHE INTERNAL "")
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE INTERNAL "")
set(ENABLE_ALL_WARNINGS OFF CACHE INTERNAL "")
set(DOUBLE_PRECISION OFF CACHE INTERNAL "")
set(TARGET_UNIT_TESTS OFF CACHE INTERNAL "")
set(TARGET_HELLO_WORLD OFF CACHE INTERNAL "")
set(TARGET_PERFORMANCE_TEST OFF CACHE INTERNAL "")
set(TARGET_SAMPLES OFF CACHE INTERNAL "")
set(TARGET_VIEWER OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(JoltPhysics)

# Lua 5.4 (GitHub mirror of the official sources; built here because Lua ships no CMake)
FetchContent_Declare(
    lua_source
    URL      https://github.com/lua/lua/archive/refs/tags/v5.4.7.tar.gz
    URL_HASH SHA256=5c39111b3fc4c1c9e56671008955a1730f54a15b95e1f1bd0752b868b929d8e3
)
FetchContent_GetProperties(lua_source)
if(NOT lua_source_POPULATED)
    FetchContent_Populate(lua_source)
endif()
file(GLOB LUA_SOURCES "${lua_source_SOURCE_DIR}/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|luac|onelua|ltests)\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE ON)
target_include_directories(lua_static PUBLIC ${lua_source_SOURCE_DIR})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua_static PUBLIC m ${CMAKE_DL_LIBS})
elseif(UNIX)
    target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
endif()

# sol2 (header-only C++ bindings for Lua)
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.5.0
)
FetchContent_GetProperties(sol2)
if(NOT sol2_POPULATED)
    FetchContent_Populate(sol2)
endif()
add_library(sol2_headers INTERFACE)
target_include_directories(sol2_headers INTERFACE ${sol2_SOURCE_DIR}/include)
# Errors are reported by the ScriptSystem; keep sol2 from also printing them.
target_compile_definitions(sol2_headers INTERFACE SOL_ALL_SAFETIES_ON=1 SOL_PRINT_ERRORS=0)
target_link_libraries(sol2_headers INTERFACE lua_static)


# Build vendor libs  

add_subdirectory(vendor/gainput)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp" 
    "src/*.hpp"
    "src/*.h"
)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/Tests/")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/main\.cpp$")
if(NOT HAKI_ENABLE_DEBUG_CAMERA)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/Systems/CameraSystem\.cpp$")
endif()
if(NOT HAKI_ENABLE_IMGUI)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/Debug/(ImGuiManager|EditorPanels|EditorTheme)\.cpp$")
endif()

set(THIRDPARTY_SOURCES
    vendor/glad/glad.c
    vendor/stb/stb.cpp
    vendor/entt/entt.hpp
)

add_library(HakiEngine STATIC ${ENGINE_SOURCES} ${THIRDPARTY_SOURCES})
add_executable(${PROJECT_NAME} src/main.cpp)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE HAKI_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Assets/.haki-assets.stamp"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_assets.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${CMAKE_CURRENT_BINARY_DIR}/Assets"
    DEPENDS ${HAKI_ASSET_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_assets.py"
    COMMENT "Synchronizing runtime assets"
)
add_custom_target(HakiAssets ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Assets/.haki-assets.stamp")
add_dependencies(${PROJECT_NAME} HakiAssets)

target_compile_definitions(HakiEngine PUBLIC
    HAKI_ENABLE_DEBUG_CAMERA=$<BOOL:${HAKI_ENABLE_DEBUG_CAMERA}>
    HAKI_ENABLE_IMGUI=$<BOOL:${HAKI_ENABLE_IMGUI}>
)

target_include_directories(HakiEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/gainput/lib/include
)

find_package(OpenGL REQUIRED)

if(HAKI_ENABLE_IMGUI)
    add_library(HakiImGui STATIC
        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_glfw.cpp
        vendor/imgui/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(HakiImGui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/backends
    )

    target_link_libraries(HakiImGui PUBLIC
        glfw
        OpenGL::GL
    )

    target_link_libraries(HakiEngine PRIVATE HakiImGui)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
endif()

target_link_libraries(HakiEngine
    PUBLIC
        glfw
        glm::glm
        OpenGL::GL
        nlohmann_json::nlohmann_json
        gainput
        cgltf_headers
        Jolt
        sol2_headers
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(HakiEngine PUBLIC X11::X11)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE HakiEngine)

if(BUILD_TESTING)
    function(add_input_test_executable target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE HakiEngine)
    endfunction()

    add_input_test_executable(InputManagerTests
        src/Tests/Input/InputManagerTests.cpp
    )
    add_test(NAME InputManager.Lifecycle COMMAND InputManagerTests lifecycle)
    add_test(NAME InputManager.SafeRawQueries COMMAND InputManagerTests safe-queries)
    add_test(NAME InputManager.ActionMappings COMMAND InputManagerTests action-mappings)
    set_tests_properties(
        InputManager.Lifecycle
        InputManager.SafeRawQueries
        InputManager.ActionMappings
        PROPERTIES SKIP_RETURN_CODE 77
    )

    add_input_test_executable(InputManagerInteractiveTests
        src/Tests/Input/InputManagerInteractive.cpp
    )
    add_executable(SceneManagerTests src/Tests/Scene/SceneManagerTests.cpp)
    target_link_libraries(SceneManagerTests PRIVATE HakiEngine)

    add_test(NAME SceneManager.Lifecycle COMMAND SceneManagerTests lifecycle)
    add_test(NAME SceneManager.Factories COMMAND SceneManagerTests factories)
    add_test(NAME SceneManager.Transitions COMMAND SceneManagerTests transitions)

    if(HAKI_ENABLE_DEBUG_CAMERA)
        add_executable(CameraSystemTests src/Tests/Systems/CameraSystemTests.cpp)
        target_link_libraries(CameraSystemTests PRIVATE HakiEngine)
        add_test(NAME CameraSystem.DebugFreeLook COMMAND CameraSystemTests)
    endif()

    add_executable(TransformHierarchySystemTests src/Tests/Systems/TransformHierarchySystemTests.cpp)
    target_link_libraries(TransformHierarchySystemTests PRIVATE HakiEngine)
    add_test(NAME TransformHierarchySystem.Attachments COMMAND TransformHierarchySystemTests)

    if(HAKI_ENABLE_IMGUI)
        add_executable(EditorTests src/Tests/Debug/EditorTests.cpp)
        target_link_libraries(EditorTests PRIVATE HakiEngine)
        add_test(NAME Editor.Overlay COMMAND EditorTests)
    endif()

    add_executable(ProjectSettingsTests src/Tests/Debug/ProjectSettingsTests.cpp)
    target_link_libraries(ProjectSettingsTests PRIVATE HakiEngine)
    add_test(NAME Project.Settings COMMAND ProjectSettingsTests)

    add_executable(AnimationSystemTests src/Tests/Systems/AnimationSystemTests.cpp)
    target_link_libraries(AnimationSystemTests PRIVATE HakiEngine)
    add_test(NAME AnimationSystem.Sampling COMMAND AnimationSystemTests)

    add_executable(PhysicsSystemTests src/Tests/Systems/PhysicsSystemTests.cpp)
    target_link_libraries(PhysicsSystemTests PRIVATE HakiEngine)
    add_test(NAME PhysicsSystem.Simulation COMMAND PhysicsSystemTests)

    add_executable(PlayerControllerSystemTests src/Tests/Systems/PlayerControllerSystemTests.cpp)
    target_link_libraries(PlayerControllerSystemTests PRIVATE HakiEngine)
    add_test(NAME PlayerControllerSystem.Movement COMMAND PlayerControllerSystemTests)

    add_executable(SceneCameraTests src/Tests/Scene/SceneCameraTests.cpp)
    target_link_libraries(SceneCameraTests PRIVATE HakiEngine)
    add_test(NAME Scene.CameraProjection COMMAND SceneCameraTests)

    add_executable(ScriptSystemTests src/Tests/Scripting/ScriptSystemTests.cpp)
    target_link_libraries(ScriptSystemTests PRIVATE HakiEngine)
    add_test(NAME Scripting.Runtime COMMAND ScriptSystemTests)

    add_executable(AssetManagerTests src/Tests/Asset/AssetManagerTests.cpp)
    target_link_libraries(AssetManagerTests PRIVATE HakiEngine)

    add_test(NAME AssetManager.Lifecycle COMMAND AssetManagerTests lifecycle)
    add_test(NAME AssetManager.Loaders COMMAND AssetManagerTests loaders)
    add_test(NAME AssetManager.Handles COMMAND AssetManagerTests handles)
    add_test(NAME AssetManager.Identity COMMAND AssetManagerTests identity)
    add_test(NAME AssetManager.SkeletalLoaders COMMAND AssetManagerTests skeletal-loaders)
    set_tests_properties(AssetManager.SkeletalLoaders PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(RendererTests src/Tests/Renderer/RendererTests.cpp)
    target_link_libraries(RendererTests PRIVATE HakiEngine)

    add_test(NAME Renderer.Forward COMMAND RendererTests)
    set_tests_properties(Renderer.Forward PROPERTIES
        SKIP_RETURN_CODE 77
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(CanvasTests src/Tests/Renderer/CanvasTests.cpp)
    target_link_libraries(CanvasTests PRIVATE HakiEngine)
    add_test(NAME Renderer.Canvas COMMAND CanvasTests)

    add_executable(DebugDrawTests src/Tests/Renderer/DebugDrawTests.cpp)
    target_link_libraries(DebugDrawTests PRIVATE HakiEngine)
    add_test(NAME Renderer.DebugDraw COMMAND DebugDrawTests)

    add_executable(FrustumTests src/Tests/Renderer/FrustumTests.cpp)
    target_link_libraries(FrustumTests PRIVATE HakiEngine)
    add_test(NAME Renderer.Frustum COMMAND FrustumTests)

    add_executable(FramebufferTests src/Tests/Renderer/FramebufferTests.cpp)
    target_link_libraries(FramebufferTests PRIVATE HakiEngine)
    add_test(NAME Framebuffer.Lifecycle COMMAND FramebufferTests)
    set_tests_properties(Framebuffer.Lifecycle PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(SceneSerializationTests src/Tests/Serialization/SceneSerializationTests.cpp)
    target_link_libraries(SceneSerializationTests PRIVATE HakiEngine)
    add_test(NAME Serialization.Catalog COMMAND SceneSerializationTests catalog)
    add_test(NAME Serialization.Scene COMMAND SceneSerializationTests scene)
    add_test(NAME Serialization.Errors COMMAND SceneSerializationTests errors)
    add_test(NAME Serialization.SceneManager COMMAND SceneSerializationTests manager)
    add_test(NAME Serialization.Physics COMMAND SceneSerializationTests physics)
    add_test(NAME Serialization.Scripting COMMAND SceneSerializationTests scripting)
    add_test(NAME Serialization.Cameras COMMAND SceneSerializationTests cameras)
    add_test(NAME Serialization.Locomotion COMMAND SceneSerializationTests locomotion)
    set_tests_properties(
        Serialization.Catalog
        Serialization.Scene
        Serialization.Errors
        Serialization.SceneManager
        Serialization.Physics
        Serialization.Scripting
        Serialization.Cameras
        Serialization.Locomotion
        PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
