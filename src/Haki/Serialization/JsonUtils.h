#pragma once

#include "Haki/Serialization/SerializationError.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Haki::JsonUtils
{

using Json = nlohmann::json;

inline void AddError(std::vector<SerializationError> &errors, const std::filesystem::path &source,
                     std::string pointer, std::string message)
{
    errors.push_back({source, std::move(pointer), std::move(message)});
}

inline bool ValidateFields(const Json &value, std::initializer_list<std::string_view> allowed,
                           const std::filesystem::path &source, std::string_view pointer,
                           std::vector<SerializationError> &errors)
{
    if (!value.is_object())
    {
        AddError(errors, source, std::string(pointer), "Expected an object");
        return false;
    }

    std::unordered_set<std::string_view> names(allowed.begin(), allowed.end());
    bool valid = true;
    for (const auto &[name, ignored] : value.items())
    {
        (void)ignored;
        if (!names.contains(name))
        {
            AddError(errors, source, std::string(pointer) + "/" + name, "Unknown field");
            valid = false;
        }
    }
    return valid;
}

inline bool ReadFile(const std::filesystem::path &source, Json &document,
                     std::vector<SerializationError> &errors)
{
    std::ifstream input(source);
    if (!input)
    {
        AddError(errors, source, "", "Could not open file");
        return false;
    }
    try
    {
        input >> document;
        return true;
    }
    catch (const Json::parse_error &error)
    {
        AddError(errors, source, "", std::string("Invalid JSON: ") + error.what());
        return false;
    }
}

inline bool ValidateHeader(const Json &document, std::string_view expectedType,
                           const std::filesystem::path &source, std::vector<SerializationError> &errors)
{
    bool valid = true;
    if (!document.contains("type") || !document["type"].is_string() ||
        document["type"].get<std::string>() != expectedType)
    {
        AddError(errors, source, "/type", "Expected document type '" + std::string(expectedType) + "'");
        valid = false;
    }
    if (!document.contains("version") || !document["version"].is_number_integer() ||
        document["version"].get<int>() != 1)
    {
        AddError(errors, source, "/version", "Only schema version 1 is supported");
        valid = false;
    }
    return valid;
}

} // namespace Haki::JsonUtils
