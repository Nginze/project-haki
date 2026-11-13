#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

struct cgltf_data;

namespace Haki
{

class GltfDocument
{
public:
    ~GltfDocument();

    GltfDocument(const GltfDocument &) = delete;
    GltfDocument &operator=(const GltfDocument &) = delete;

    const cgltf_data *GetData() const { return data_; }

private:
    friend class GltfDocumentCache;
    explicit GltfDocument(cgltf_data *data) : data_(data) {}

    cgltf_data *data_ = nullptr;
};

class GltfDocumentCache
{
public:
    std::shared_ptr<const GltfDocument> Load(const std::filesystem::path &source, std::string &error);
    std::size_t GetParseCount() const { return parseCount_; }
    void Clear();

private:
    std::unordered_map<std::filesystem::path, std::shared_ptr<const GltfDocument>> documents_;
    std::size_t parseCount_ = 0;
};

} // namespace Haki
