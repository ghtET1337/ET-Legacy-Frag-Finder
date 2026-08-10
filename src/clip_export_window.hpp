// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include "clip_export.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace etlfrag::clipui {

#ifdef _WIN32
void open(
    HWND owner,
    const std::filesystem::path& etlExecutable,
    const std::filesystem::path& settingsPath,
    bool launchAsAdministrator,
    const std::filesystem::path& etlHomeFolder,
    const std::filesystem::path& sourceProfileFolder,
    const std::filesystem::path& startupConfig,
    const std::vector<ClipSource>& sources);
std::size_t enqueue(
    HWND owner,
    const std::filesystem::path& etlExecutable,
    const std::filesystem::path& settingsPath,
    bool launchAsAdministrator,
    const std::filesystem::path& etlHomeFolder,
    const std::filesystem::path& sourceProfileFolder,
    const std::filesystem::path& startupConfig,
    const std::vector<ClipSource>& sources);
void shutdown();
#endif

} // namespace etlfrag::clipui
