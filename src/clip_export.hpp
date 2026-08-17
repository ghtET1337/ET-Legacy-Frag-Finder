// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace etlfrag {

inline constexpr int kEtlColdStartDemoDelayFrames = 500;
inline constexpr int kEtlPostSnapshotActionDelayFrames = 100;
inline constexpr int kEtlRenderZoneMegs = 512;

enum class ClipQuality {
    Master,
    High,
    Balanced,
    Compact,
};

enum class ClipEngineMode {
    // Works with stock ET: Legacy 2.83+ by translating a range to
    // seek + video-pipe + frame-counted wait + stopvideo.
    CompatibleVideoPipe,
    // Uses the optional ET: Legacy engine patch shipped with Frag Finder.
    NativeVideoPipeRange,
};

struct ClipExportSettings {
    int preRollMs = 5000;
    int postRollMs = 3000;
    int width = 1920;
    int height = 1080;
    int frameRate = 60;
    bool drawHud = true;
    bool createDiscordCopy = false;
    ClipQuality quality = ClipQuality::High;
    ClipEngineMode engineMode = ClipEngineMode::CompatibleVideoPipe;
    std::filesystem::path outputFolder;
    std::filesystem::path etlHomeFolder;
    std::filesystem::path ffmpegExecutable;
    std::filesystem::path sourceProfileFolder;
    std::filesystem::path startupConfig;
};

struct EtlProfileInfo {
    std::wstring name;
    std::wstring modName;
    std::filesystem::path folder;
};

struct ClipSource {
    std::filesystem::path demoPath;
    std::wstring label;
    std::int32_t actionStartMs = 0;
    std::int32_t actionEndMs = 0;
};

struct ClipRange {
    std::int32_t startMs = 0;
    std::int32_t endMs = 0;
    std::uint32_t frameCount = 1;
};

ClipRange calculateClipRange(const ClipSource& source, const ClipExportSettings& settings);
std::optional<std::wstring> validateClipExport(
    const ClipSource& source,
    const ClipExportSettings& settings);
std::wstring makeSafeClipBaseName(
    const ClipSource& source,
    const ClipRange& range,
    std::uint64_t uniqueId);
std::wstring clipQualityName(ClipQuality quality);
std::wstring clipEngineModeName(ClipEngineMode mode);
std::wstring buildPipeFormat(const ClipExportSettings& settings);
std::wstring buildRangeAction(
    const std::wstring& safeBaseName,
    const ClipRange& range,
    const ClipExportSettings& settings);
std::wstring buildEtlStartupArguments(
    const std::filesystem::path& demoPath,
    const std::filesystem::path& etlHomeFolder,
    const std::wstring& launchProfile = {});
std::wstring buildEtlPlaybackArguments(
    const std::filesystem::path& demoPath,
    const std::filesystem::path& etlHomeFolder,
    const std::wstring& launchProfile,
    std::optional<std::int32_t> seekMs);
std::wstring buildEtlClipArguments(
    const ClipSource& source,
    const std::wstring& safeBaseName,
    const ClipRange& range,
    const ClipExportSettings& settings,
    const std::wstring& launchProfile = {});
std::wstring inferDemoModName(const std::filesystem::path& demoPath);
std::vector<EtlProfileInfo> discoverEtlProfiles(
    const std::filesystem::path& etlHomeFolder);
std::filesystem::path selectEtlUserDataFolder(
    const std::filesystem::path& explicitUserDataFolder,
    const std::filesystem::path& legacyClipHomeFolder,
    const std::filesystem::path& standardDocumentsHomeFolder);
bool prepareEtlLaunchProfile(
    const std::filesystem::path& etlHomeFolder,
    const std::filesystem::path& demoPath,
    const std::filesystem::path& selectedProfileFolder,
    const std::filesystem::path& sourceConfig,
    std::wstring& launchProfileName,
    std::filesystem::path& previousConfigBackup,
    std::wstring& error);
std::vector<std::filesystem::path> discoverLegacyFragFinderProfiles(
    const std::filesystem::path& etlHomeFolder);
std::filesystem::path expectedEtlVideoPath(
    const std::wstring& safeBaseName,
    const ClipExportSettings& settings);
bool isCompleteMp4(const std::filesystem::path& path);
std::wstring buildDiscordCopyArguments(
    const std::filesystem::path& input,
    const std::filesystem::path& output);

} // namespace etlfrag
