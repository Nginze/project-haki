#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "Haki/AssetLoader/GltfDocumentCache.h"

#include <cstdint>

namespace Haki
{
namespace
{

constexpr std::uintmax_t MaxCachedDocumentBytes = 128ull * 1024ull * 1024ull;

} // namespace

GltfDocument::~GltfDocument()
{
    cgltf_free(data_);
}

std::shared_ptr<const GltfDocument> GltfDocumentCache::Load(const std::filesystem::path &source, std::string &error)
{
    const std::filesystem::path normalized = std::filesystem::absolute(source).lexically_normal();
    if (const auto cached = documents_.find(normalized); cached != documents_.end())
    {
        return cached->second;
    }

    cgltf_options options{};
    cgltf_data *data = nullptr;
    ++parseCount_;
    if (cgltf_parse_file(&options, normalized.string().c_str(), &data) != cgltf_result_success)
    {
        error = "Could not parse glTF file: " + source.string();
        return nullptr;
    }
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> owned(data, cgltf_free);
    if (cgltf_load_buffers(&options, data, normalized.string().c_str()) != cgltf_result_success)
    {
        error = "Could not load glTF buffers: " + source.string();
        return nullptr;
    }

    auto document = std::shared_ptr<const GltfDocument>(new GltfDocument(owned.release()));
    std::uintmax_t bytes = 0;
    for (cgltf_size index = 0; index < data->buffers_count; ++index)
    {
        bytes += data->buffers[index].size;
    }
    std::error_code sizeError;
    bytes += std::filesystem::file_size(normalized, sizeError);
    if (!sizeError && bytes <= MaxCachedDocumentBytes)
    {
        documents_.emplace(normalized, document);
    }
    return document;
}

void GltfDocumentCache::Clear()
{
    documents_.clear();
}

} // namespace Haki
