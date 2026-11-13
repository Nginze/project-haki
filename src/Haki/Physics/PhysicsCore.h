#pragma once

namespace JPH
{
class TempAllocator;
class JobSystem;
} // namespace JPH

namespace Haki
{

// Process-wide Jolt setup: allocator hooks, type registry, scratch allocator and job threads.
// Initialize once before any PhysicsSystem is initialized and shut down after all of them are gone.
// Does not touch the window or OpenGL, so it also works headless (dedicated servers, tests).
class PhysicsCore
{
public:
    PhysicsCore() = delete;

    static bool Initialize();
    static void Shutdown();
    static bool IsInitialized();

    // Valid only while initialized.
    static JPH::TempAllocator &GetTempAllocator();
    static JPH::JobSystem &GetJobSystem();
};

} // namespace Haki
