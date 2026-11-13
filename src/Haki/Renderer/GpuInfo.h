#pragma once

#include <string>

namespace Haki
{

// GPU name and video memory, for the telemetry overlay. Requires a current OpenGL context.
class GpuInfo
{
public:
    enum class MemorySource
    {
        None,   // driver exposes no memory query (typical on Intel / Mesa)
        Nvidia, // GL_NVX_gpu_memory_info: total and available
        Amd     // GL_ATI_meminfo: free texture memory only
    };

    // Reads GL_RENDERER and detects which memory extension is available.
    void Initialize();
    // Re-queries memory; cheap, but call at most a few times per second.
    void UpdateMemory();

    const std::string &GetRenderer() const { return renderer_; }
    MemorySource GetMemorySource() const { return source_; }
    // Megabytes; -1 when unknown.
    int GetTotalMemoryMb() const { return totalMb_; }
    int GetUsedMemoryMb() const { return usedMb_; }
    int GetFreeMemoryMb() const { return freeMb_; }

private:
    std::string renderer_ = "unknown GPU";
    MemorySource source_ = MemorySource::None;
    int totalMb_ = -1;
    int usedMb_ = -1;
    int freeMb_ = -1;
};

} // namespace Haki
