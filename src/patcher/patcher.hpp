#pragma once

#include <filesystem>

namespace wemod
{
/// Patch WeMod install at dir, throws on error.
void patch(const std::filesystem::path& install);
/// Restore WeMod install at dir from backup, throws on error.
void unpatch(const std::filesystem::path& install);
}
