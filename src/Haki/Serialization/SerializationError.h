#pragma once

#include <filesystem>
#include <string>

namespace Haki
{

struct SerializationError
{
    std::filesystem::path Source;
    std::string JsonPointer;
    std::string Message;
};

} // namespace Haki
