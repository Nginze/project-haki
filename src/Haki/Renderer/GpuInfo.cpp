#include "Haki/Renderer/GpuInfo.h"

#include <glad/glad.h>

#include <cstring>

namespace Haki
{
namespace
{

// Not in the generated glad loader; values from the extension specifications.
constexpr GLenum GpuMemoryInfoTotalAvailableNvx = 0x9048;   // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX (KiB)
constexpr GLenum GpuMemoryInfoCurrentAvailableNvx = 0x9049; // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX (KiB)
constexpr GLenum TextureFreeMemoryAti = 0x87FC;             // GL_TEXTURE_FREE_MEMORY_ATI (4 values, KiB)

bool HasExtension(const char *name)
{
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint index = 0; index < count; ++index)
    {
        const auto *extension = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
        if (extension && std::strcmp(extension, name) == 0)
        {
            return true;
        }
    }
    return false;
}

} // namespace

void GpuInfo::Initialize()
{
    if (const auto *name = reinterpret_cast<const char *>(glGetString(GL_RENDERER)))
    {
        renderer_ = name;
    }
    if (HasExtension("GL_NVX_gpu_memory_info"))
    {
        source_ = MemorySource::Nvidia;
    }
    else if (HasExtension("GL_ATI_meminfo"))
    {
        source_ = MemorySource::Amd;
    }
    UpdateMemory();
}

void GpuInfo::UpdateMemory()
{
    switch (source_)
    {
    case MemorySource::Nvidia:
    {
        GLint totalKb = 0;
        GLint availableKb = 0;
        glGetIntegerv(GpuMemoryInfoTotalAvailableNvx, &totalKb);
        glGetIntegerv(GpuMemoryInfoCurrentAvailableNvx, &availableKb);
        totalMb_ = totalKb / 1024;
        freeMb_ = availableKb / 1024;
        usedMb_ = totalMb_ - freeMb_;
        break;
    }
    case MemorySource::Amd:
    {
        GLint values[4] = {};
        glGetIntegerv(TextureFreeMemoryAti, values);
        freeMb_ = values[0] / 1024;
        break;
    }
    case MemorySource::None:
        break;
    }
    // Drain errors from a driver that advertised but rejected the query.
    while (glGetError() != GL_NO_ERROR)
    {
    }
}

} // namespace Haki
