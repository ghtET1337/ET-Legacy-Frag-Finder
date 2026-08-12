// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <filesystem>

namespace etlfrag::captureui {

#ifdef _WIN32
void open(
    HWND owner,
    const std::filesystem::path& settingsPath,
    const std::filesystem::path& suggestedFfmpeg,
    const std::filesystem::path& suggestedOutputFolder);
void shutdown();
#endif

} // namespace etlfrag::captureui
