#pragma once

#include <filesystem>
#include <string>

#include "engine/core/ApplicationDesc.h"

namespace engine
{

// Human-readable, version-tolerant application settings. Unknown keys are
// ignored so newer configs can still be consumed by older tools.
bool SaveApplicationConfig(const std::filesystem::path& path, const ApplicationDesc& config,
                           std::string* error = nullptr);
bool LoadApplicationConfig(const std::filesystem::path& path, ApplicationDesc& config,
                           std::string* error = nullptr);

} // namespace engine
