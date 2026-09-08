// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifdef _WIN32
#include <windows.h>
#include "clip_export.hpp"
namespace etlfrag::cutui {
void open(HWND owner, const ClipSource& source, const std::filesystem::path& settingsPath);
}
#endif
