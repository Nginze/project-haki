#include "Haki/Physics/PhysicsCore.h"

#include "Haki/Logging/Logger.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <thread>

namespace Haki
{
namespace
{

constexpr unsigned int TempAllocatorBytes = 10u * 1024u * 1024u;

std::unique_ptr<JPH::TempAllocatorImpl> s_tempAllocator;
std::unique_ptr<JPH::JobSystemThreadPool> s_jobSystem;
bool s_initialized = false;

void TraceToLogger(const char *format, ...)
{
    char buffer[1024];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    Logger::Debug() << "Jolt: " << buffer;
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedToLogger(const char *expression, const char *message, const char *file, JPH::uint line)
{
    Logger::Error() << "Jolt assert: " << file << ":" << line << ": (" << expression << ") "
                    << (message ? message : "");
    return true; // break into the debugger
}
#endif

} // namespace

bool PhysicsCore::Initialize()
{
    if (s_initialized)
    {
        return true;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceToLogger;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedToLogger;)

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    s_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(TempAllocatorBytes);
    const int workerThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    s_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                             workerThreads);

    s_initialized = true;
    Logger::Info() << "Physics initialized (Jolt, " << workerThreads << " worker thread(s))";
    return true;
}

void PhysicsCore::Shutdown()
{
    if (!s_initialized)
    {
        return;
    }

    s_jobSystem.reset();
    s_tempAllocator.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    s_initialized = false;
}

bool PhysicsCore::IsInitialized()
{
    return s_initialized;
}

JPH::TempAllocator &PhysicsCore::GetTempAllocator()
{
    return *s_tempAllocator;
}

JPH::JobSystem &PhysicsCore::GetJobSystem()
{
    return *s_jobSystem;
}

} // namespace Haki
