// SPDX-License-Identifier: GPL-3.0-or-later
#include "clip_export.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace etlfrag {
namespace {

std::wstring secondsText(std::int32_t milliseconds) {
    std::wostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(3)
           << static_cast<double>(milliseconds) / 1000.0;
    return output.str();
}

std::wstring sanitizePart(const std::wstring& input) {
    std::wstring result;
    result.reserve(input.size());
    bool previousSeparator = false;
    for (const wchar_t character : input) {
        const bool asciiAlphaNumeric =
            (character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z') ||
            (character >= L'0' && character <= L'9');
        if (asciiAlphaNumeric) {
            result.push_back(character);
            previousSeparator = false;
        } else if (!previousSeparator && !result.empty()) {
            result.push_back(L'_');
            previousSeparator = true;
        }
    }
    while (!result.empty() && result.back() == L'_') result.pop_back();
    if (result.empty()) result = L"clip";
    if (result.size() > 80) result.resize(80);
    return result;
}

bool equalsIgnoreCase(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) return false;
    }
    return true;
}

bool isSafeEngineToken(const std::wstring& value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t character) {
        return (character >= L'a' && character <= L'z') ||
               (character >= L'A' && character <= L'Z') ||
               (character >= L'0' && character <= L'9') ||
               character == L'_' || character == L'-' || character == L'.';
    });
}

bool isSafeQuotedEngineValue(const std::wstring& value) {
    return !value.empty() &&
           std::none_of(value.begin(), value.end(), [](wchar_t character) {
               return character == L'"' || character == L';' ||
                      character == L'\r' || character == L'\n';
           });
}

bool samePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    if (left.empty() || right.empty()) return left.empty() && right.empty();
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error) && !error) return true;
    return equalsIgnoreCase(
        left.lexically_normal().generic_wstring(),
        right.lexically_normal().generic_wstring());
}

bool filesEqual(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    std::error_code error;
    const std::uintmax_t leftSize = std::filesystem::file_size(left, error);
    if (error) return false;
    error.clear();
    if (leftSize != std::filesystem::file_size(right, error) || error) return false;

    std::ifstream leftInput(left, std::ios::binary);
    std::ifstream rightInput(right, std::ios::binary);
    if (!leftInput || !rightInput) return false;
    std::array<char, 32768> leftBuffer{};
    std::array<char, 32768> rightBuffer{};
    while (leftInput && rightInput) {
        leftInput.read(leftBuffer.data(), static_cast<std::streamsize>(leftBuffer.size()));
        rightInput.read(rightBuffer.data(), static_cast<std::streamsize>(rightBuffer.size()));
        const std::streamsize leftCount = leftInput.gcount();
        const std::streamsize rightCount = rightInput.gcount();
        if (leftCount != rightCount ||
            !std::equal(leftBuffer.begin(), leftBuffer.begin() + leftCount, rightBuffer.begin())) {
            return false;
        }
    }
    return leftInput.eof() && rightInput.eof();
}

std::wstring localTimestamp() {
    const std::time_t value = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::wostringstream output;
    output << std::setfill(L'0')
           << std::setw(4) << local.tm_year + 1900
           << std::setw(2) << local.tm_mon + 1
           << std::setw(2) << local.tm_mday << L'-'
           << std::setw(2) << local.tm_hour
           << std::setw(2) << local.tm_min
           << std::setw(2) << local.tm_sec;
    return output.str();
}

std::filesystem::path availableSiblingPath(
    const std::filesystem::path& folder,
    const std::wstring& stem,
    const std::wstring& extension) {
    for (unsigned int suffix = 0; suffix < 10000; ++suffix) {
        std::wstring name = stem;
        if (suffix > 0) name += L"-" + std::to_wstring(suffix + 1);
        name += extension;
        const std::filesystem::path candidate = folder / name;
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) && !error) return candidate;
    }
    return {};
}

bool isGeneratedProfileName(const std::wstring& name) {
    std::wstring lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t character) {
        return std::towlower(character);
    });
    return lowered.rfind(L"ff_play_", 0) == 0 ||
           lowered.rfind(L"ff_render_", 0) == 0;
}

std::uint32_t readBigEndian32(const unsigned char* value) {
    return (static_cast<std::uint32_t>(value[0]) << 24U) |
           (static_cast<std::uint32_t>(value[1]) << 16U) |
           (static_cast<std::uint32_t>(value[2]) << 8U) |
           static_cast<std::uint32_t>(value[3]);
}

std::uint64_t readBigEndian64(const unsigned char* value) {
    return (static_cast<std::uint64_t>(readBigEndian32(value)) << 32U) |
           static_cast<std::uint64_t>(readBigEndian32(value + 4));
}

} // namespace

ClipRange calculateClipRange(const ClipSource& source, const ClipExportSettings& settings) {
    const std::int64_t start = std::max<std::int64_t>(
        0,
        static_cast<std::int64_t>(source.actionStartMs) -
            std::max(0, settings.preRollMs));
    const std::int64_t actionEnd = std::max<std::int64_t>(
        source.actionStartMs,
        source.actionEndMs);
    const std::int64_t end = std::max<std::int64_t>(
        start + 1,
        actionEnd + std::max(0, settings.postRollMs));
    const double durationSeconds = static_cast<double>(end - start) / 1000.0;
    const std::uint64_t frames = static_cast<std::uint64_t>(
        std::ceil(durationSeconds * std::max(1, settings.frameRate)));

    ClipRange range;
    range.startMs = static_cast<std::int32_t>(std::min<std::int64_t>(
        start,
        std::numeric_limits<std::int32_t>::max()));
    range.endMs = static_cast<std::int32_t>(std::min<std::int64_t>(
        end,
        std::numeric_limits<std::int32_t>::max()));
    range.frameCount = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
        frames,
        1,
        std::numeric_limits<std::uint32_t>::max()));
    return range;
}

std::optional<std::wstring> validateClipExport(
    const ClipSource& source,
    const ClipExportSettings& settings) {
    if (source.demoPath.empty() || !std::filesystem::is_regular_file(source.demoPath)) {
        return L"The source demo file does not exist.";
    }
    if (source.actionStartMs < 0 || source.actionEndMs < 0) {
        return L"The selected action has an invalid time range.";
    }
    if (settings.preRollMs < 0 || settings.preRollMs > 120000 ||
        settings.postRollMs < 0 || settings.postRollMs > 120000) {
        return L"Pre-roll and post-roll must be between 0 and 120 seconds.";
    }
    if (settings.width < 320 || settings.width > 7680 ||
        settings.height < 240 || settings.height > 4320) {
        return L"The export resolution must be between 320x240 and 7680x4320.";
    }
    if (settings.frameRate < 1 || settings.frameRate > 240) {
        return L"The export frame rate must be between 1 and 240 FPS.";
    }
    if (settings.outputFolder.empty()) {
        return L"Choose an output folder for rendered clips.";
    }
    if (settings.etlHomeFolder.empty()) {
        return L"Choose the ET: Legacy user-data folder used for profiles and videos.";
    }
    if (!settings.startupConfig.empty() && settings.sourceProfileFolder.empty()) {
        return L"Select an ETL launch profile before using a startup / fragmovie CFG.";
    }
    if (!settings.sourceProfileFolder.empty() &&
        !std::filesystem::is_directory(settings.sourceProfileFolder)) {
        return L"The selected ETL profile folder no longer exists.";
    }
    if (!settings.startupConfig.empty() &&
        !std::filesystem::is_regular_file(settings.startupConfig)) {
        return L"The selected startup / fragmovie CFG no longer exists.";
    }
    return std::nullopt;
}

std::wstring makeSafeClipBaseName(
    const ClipSource& source,
    const ClipRange& range,
    std::uint64_t uniqueId) {
    std::wstring stem = sanitizePart(source.demoPath.stem().wstring());
    std::wstring label = sanitizePart(source.label);
    if (label == L"clip") label.clear();
    std::wostringstream result;
    result.imbue(std::locale::classic());
    result << stem;
    if (!label.empty()) result << L'_' << label;
    result << L'_' << std::setfill(L'0') << std::setw(6) << (range.startMs / 1000)
           << L'_' << uniqueId;
    std::wstring value = result.str();
    if (value.size() > 160) value.resize(160);
    return value;
}

std::wstring clipQualityName(ClipQuality quality) {
    switch (quality) {
        case ClipQuality::Master: return L"Master / editing";
        case ClipQuality::High: return L"High";
        case ClipQuality::Balanced: return L"Balanced";
        case ClipQuality::Compact: return L"Compact";
    }
    return L"High";
}

std::wstring clipEngineModeName(ClipEngineMode mode) {
    return mode == ClipEngineMode::NativeVideoPipeRange
               ? L"Patched ETL (exact serverTime)"
               : L"Stock ETL (corrected time)";
}

std::wstring buildPipeFormat(const ClipExportSettings& settings) {
    switch (settings.quality) {
        case ClipQuality::Master:
            return L"-preset slow -crf 10 -c:v libx264 -pix_fmt yuv420p -bf 2 "
                   L"-c:a aac -b:a 256k -movflags faststart";
        case ClipQuality::High:
            return L"-preset slow -crf 16 -c:v libx264 -pix_fmt yuv420p -bf 2 "
                   L"-c:a aac -b:a 192k -movflags faststart";
        case ClipQuality::Balanced:
            return L"-preset medium -crf 20 -c:v libx264 -pix_fmt yuv420p -bf 2 "
                   L"-c:a aac -b:a 160k -movflags faststart";
        case ClipQuality::Compact:
            return L"-preset medium -crf 24 -c:v libx264 -pix_fmt yuv420p -bf 2 "
                   L"-c:a aac -b:a 128k -movflags faststart";
    }
    return L"-preset slow -crf 16 -c:v libx264 -pix_fmt yuv420p -bf 2 "
           L"-c:a aac -b:a 192k -movflags faststart";
}

std::wstring buildRangeAction(
    const std::wstring& safeBaseName,
    const ClipRange& range,
    const ClipExportSettings& settings) {
    if (settings.engineMode == ClipEngineMode::NativeVideoPipeRange) {
        return L"seta demo_infoWindow 0; video-pipe-range " + safeBaseName + L" " +
               secondsText(range.startMs) + L" " + secondsText(range.endMs);
    }
    // Stock ETL has no demo-time range command. While video-pipe is active it
    // advances demo time by exactly 1000 / cl_aviFrameRate per captured frame,
    // so an output-frame count can still delimit the requested demo range.
    // ETL executes Cbuf_Execute twice during every Com_Frame and each pass
    // decrements `wait`; therefore the command buffer needs two wait passes
    // per captured frame. The previous one-to-one calculation stopped clips
    // at approximately half of their requested demo duration.
    const std::uint64_t waitPasses = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(range.frameCount) * 2ULL,
        static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
    return L"seta demo_infoWindow 0; seek " + secondsText(range.startMs) +
           L"; set timescale 1; set cl_aviFrameRate " + std::to_wstring(settings.frameRate) +
           L"; video-pipe " + safeBaseName +
           L"; wait " + std::to_wstring(waitPasses) +
           L"; stopvideo; quit";
}

std::wstring buildEtlClipArguments(
    const ClipSource& source,
    const std::wstring& safeBaseName,
    const ClipRange& range,
    const ClipExportSettings& settings,
    const std::wstring& launchProfile) {
    const std::wstring demo =
        std::filesystem::absolute(source.demoPath).lexically_normal().generic_wstring();
    const std::wstring action =
        L"wait " + std::to_wstring(kEtlPostSnapshotActionDelayFrames) + L"; " +
        buildRangeAction(safeBaseName, range, settings);
    // ETL allocates its main zone before configs and demo data are loaded.
    // The stock 64 MiB client zone can be exhausted by high-resolution
    // video-pipe jobs, so this must be a process-start command-line cvar.
    std::wstring arguments =
        L"+set com_zoneMegs " + std::to_wstring(kEtlRenderZoneMegs) + L" ";
    arguments += buildEtlStartupArguments(
        source.demoPath,
        settings.etlHomeFolder,
        launchProfile);
    arguments +=
        L"+set s_initsound 1 "
        L"+set s_useOpenAL 0 "
        L"+set s_muteWhenMinimized 0 "
        L"+set s_muteWhenUnfocused 0 "
        L"+set cl_avidemo 0 "
        L"+set timescale 1 "
        L"+set cl_aviFrameRate " + std::to_wstring(settings.frameRate) +
        L" +set cl_aviPipeExtension mp4 "
        L"+set cl_aviPipeFormat \"" + buildPipeFormat(settings) + L"\" "
        L"+set r_fullscreen 0 "
        L"+set r_mode -1 "
        L"+set r_customwidth " + std::to_wstring(settings.width) +
        L" +set r_customheight " + std::to_wstring(settings.height) +
        L" +set r_swapInterval 0 "
        L"+set com_maxfps " + std::to_wstring(settings.frameRate) +
        L" +set com_maxfpsUnfocused " + std::to_wstring(settings.frameRate) +
        L" +set com_maxfpsMinimized " + std::to_wstring(settings.frameRate) +
        L" +set cg_draw2D " + std::wstring(settings.drawHud ? L"1" : L"0") +
        L" +seta demo_infoWindow 0 "
        L" +set cl_videoPipeRangeQuit 1 "
        L"+vid_restart "
        L"+set activeAction \"" + action + L"\" "
        L"+wait " + std::to_wstring(kEtlColdStartDemoDelayFrames) +
        L" +demo \"" + demo + L"\"";
    return arguments;
}

std::wstring buildEtlStartupArguments(
    const std::filesystem::path& demoPath,
    const std::filesystem::path& etlHomeFolder,
    const std::wstring& launchProfile) {
    std::wstring arguments;
    if (!etlHomeFolder.empty()) {
        arguments += L"+set fs_homepath \"" +
                     etlHomeFolder.lexically_normal().generic_wstring() + L"\" ";
    }
    arguments += L"+set fs_game \"" + inferDemoModName(demoPath) + L"\" ";
    if (!launchProfile.empty()) {
        arguments += L"+set cl_profile \"" + launchProfile + L"\" ";
    }
    return arguments;
}

std::wstring buildEtlPlaybackArguments(
    const std::filesystem::path& demoPath,
    const std::filesystem::path& etlHomeFolder,
    const std::wstring& launchProfile,
    std::optional<std::int32_t> seekMs) {
    std::wstring arguments = buildEtlStartupArguments(
        demoPath,
        etlHomeFolder,
        launchProfile);
    if (seekMs.has_value() && *seekMs > 0) {
        // activeAction runs after the first active demo snapshot.  Give cgame
        // and the renderer a short second-stage settling period before seek.
        arguments += L"+set activeAction \"wait " +
                     std::to_wstring(kEtlPostSnapshotActionDelayFrames) + L"; seek " +
                     secondsText(*seekMs) + L"\" ";
    }
    const std::wstring demo =
        std::filesystem::absolute(demoPath).lexically_normal().generic_wstring();
    // Keep the demo command in ETL's command buffer until cold-start profile,
    // filesystem, renderer and cgame setup have had time to settle.  ETL's
    // `wait N` command preserves all following commands and resumes them after
    // N engine frames.  This mirrors the reliable behaviour of pasting the
    // same demo command into an already running ETL client.
    arguments += L"+wait " + std::to_wstring(kEtlColdStartDemoDelayFrames) +
                 L" +demo \"" + demo + L"\"";
    return arguments;
}

std::wstring inferDemoModName(const std::filesystem::path& demoPath) {
    std::filesystem::path current = demoPath.parent_path();
    for (int depth = 0; depth < 12 && !current.empty(); ++depth) {
        if (equalsIgnoreCase(current.filename().wstring(), L"demos") &&
            current.has_parent_path() && !current.parent_path().filename().empty()) {
            const std::wstring modName = current.parent_path().filename().wstring();
            return isSafeEngineToken(modName) ? modName : L"legacy";
        }
        current = current.parent_path();
    }
    return L"legacy";
}

std::vector<EtlProfileInfo> discoverEtlProfiles(
    const std::filesystem::path& etlHomeFolder) {
    std::vector<EtlProfileInfo> profiles;
    std::set<std::wstring> seen;
    std::error_code error;
    if (!std::filesystem::is_directory(etlHomeFolder, error) || error) return profiles;

    auto inspectRoot = [&](const std::filesystem::path& root, const std::wstring& modName) {
        error.clear();
        if (!std::filesystem::is_directory(root, error) || error) return;
        std::filesystem::directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            std::error_code entryError;
            if (!iterator->is_directory(entryError) || entryError) continue;
            const std::wstring name = iterator->path().filename().wstring();
            if (name.empty() || name.rfind(L"fragfinder_", 0) == 0 ||
                isGeneratedProfileName(name)) {
                continue;
            }
            entryError.clear();
            if (!std::filesystem::is_regular_file(
                    iterator->path() / L"etconfig.cfg", entryError) || entryError) {
                continue;
            }
            std::wstring key = iterator->path().lexically_normal().generic_wstring();
            std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
                return std::towlower(character);
            });
            if (!seen.insert(key).second) continue;
            profiles.push_back({name, modName, iterator->path()});
        }
        error.clear();
    };

    inspectRoot(etlHomeFolder / L"profiles", L"base");
    inspectRoot(etlHomeFolder / L"profile", L"base");
    std::filesystem::directory_iterator mods(
        etlHomeFolder, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    for (; !error && mods != end; mods.increment(error)) {
        std::error_code entryError;
        if (!mods->is_directory(entryError) || entryError) continue;
        const std::wstring modName = mods->path().filename().wstring();
        inspectRoot(mods->path() / L"profiles", modName);
        inspectRoot(mods->path() / L"profile", modName);
    }
    std::sort(profiles.begin(), profiles.end(), [](const EtlProfileInfo& left,
                                                    const EtlProfileInfo& right) {
        std::wstring leftKey = left.name + L"\n" + left.modName;
        std::wstring rightKey = right.name + L"\n" + right.modName;
        std::transform(leftKey.begin(), leftKey.end(), leftKey.begin(), ::towlower);
        std::transform(rightKey.begin(), rightKey.end(), rightKey.begin(), ::towlower);
        return leftKey < rightKey;
    });
    return profiles;
}

std::filesystem::path selectEtlUserDataFolder(
    const std::filesystem::path& explicitUserDataFolder,
    const std::filesystem::path& legacyClipHomeFolder,
    const std::filesystem::path& standardDocumentsHomeFolder) {
    if (!explicitUserDataFolder.empty()) return explicitUserDataFolder;
    if (!legacyClipHomeFolder.empty() &&
        !discoverEtlProfiles(legacyClipHomeFolder).empty()) {
        return legacyClipHomeFolder;
    }
    if (!standardDocumentsHomeFolder.empty() &&
        !discoverEtlProfiles(standardDocumentsHomeFolder).empty()) {
        return standardDocumentsHomeFolder;
    }
    std::error_code error;
    if (!standardDocumentsHomeFolder.empty() &&
        std::filesystem::is_directory(standardDocumentsHomeFolder, error) && !error) {
        return standardDocumentsHomeFolder;
    }
    if (!legacyClipHomeFolder.empty()) return legacyClipHomeFolder;
    return standardDocumentsHomeFolder;
}

bool prepareEtlLaunchProfile(
    const std::filesystem::path& etlHomeFolder,
    const std::filesystem::path& demoPath,
    const std::filesystem::path& selectedProfileFolder,
    const std::filesystem::path& sourceConfig,
    std::wstring& launchProfileName,
    std::filesystem::path& previousConfigBackup,
    std::wstring& error) {
    launchProfileName.clear();
    previousConfigBackup.clear();
    error.clear();
    if (selectedProfileFolder.empty()) {
        if (!sourceConfig.empty()) {
            error = L"Select an ETL profile before using a startup / fragmovie CFG. "
                    L"Frag Finder installs the selected CFG as that profile's etconfig.cfg.";
            return false;
        }
        return true;
    }

    std::error_code filesystemError;
    if (!std::filesystem::is_directory(selectedProfileFolder, filesystemError) ||
        filesystemError) {
        error = L"The selected ETL profile folder no longer exists: " +
                selectedProfileFolder.wstring();
        return false;
    }
    const std::filesystem::path profilesFolder = selectedProfileFolder.parent_path();
    if ((!equalsIgnoreCase(profilesFolder.filename().wstring(), L"profiles") &&
         !equalsIgnoreCase(profilesFolder.filename().wstring(), L"profile")) ||
        profilesFolder.parent_path().empty()) {
        error = L"The selected folder is not an ETL profile: " +
                selectedProfileFolder.wstring();
        return false;
    }
    const std::filesystem::path profileModFolder = profilesFolder.parent_path();
    if (!samePath(profileModFolder.parent_path(), etlHomeFolder)) {
        error = L"The selected profile is outside the configured ETL user-data folder: " +
                selectedProfileFolder.wstring();
        return false;
    }
    const std::wstring demoMod = inferDemoModName(demoPath);
    const std::wstring profileMod = profileModFolder.filename().wstring();
    if (!equalsIgnoreCase(profileMod, demoMod)) {
        error = L"The selected profile belongs to the '" + profileMod +
                L"' mod, but this demo requires the '" + demoMod +
                L"' mod. Select a profile listed for [" + demoMod + L"].";
        return false;
    }

    launchProfileName = selectedProfileFolder.filename().wstring();
    if (!isSafeQuotedEngineValue(launchProfileName)) {
        error = L"The selected ETL profile name contains characters that cannot be passed "
                L"safely on the ETL command line: " + launchProfileName;
        launchProfileName.clear();
        return false;
    }

    const std::filesystem::path destination = selectedProfileFolder / L"etconfig.cfg";
    if (sourceConfig.empty()) {
        filesystemError.clear();
        if (!std::filesystem::is_regular_file(destination, filesystemError) ||
            filesystemError) {
            error = L"The selected profile has no etconfig.cfg: " +
                    selectedProfileFolder.wstring();
            launchProfileName.clear();
            return false;
        }
        return true;
    }
    filesystemError.clear();
    if (!std::filesystem::is_regular_file(sourceConfig, filesystemError) ||
        filesystemError) {
        error = L"The selected CFG file no longer exists: " + sourceConfig.wstring();
        launchProfileName.clear();
        return false;
    }
    if (samePath(sourceConfig, destination) ||
        (std::filesystem::is_regular_file(destination, filesystemError) &&
         !filesystemError && filesEqual(sourceConfig, destination))) {
        return true;
    }

    const std::wstring timestamp = localTimestamp();
    const std::filesystem::path temporary = availableSiblingPath(
        selectedProfileFolder,
        L"etconfigFRAGFINDERtmp-" + timestamp,
        L".tmp");
    if (temporary.empty()) {
        error = L"Could not reserve a temporary CFG name in: " +
                selectedProfileFolder.wstring();
        launchProfileName.clear();
        return false;
    }
    filesystemError.clear();
    std::filesystem::copy_file(
        sourceConfig,
        temporary,
        std::filesystem::copy_options::overwrite_existing,
        filesystemError);
    if (filesystemError) {
        error = L"Could not copy the selected CFG into the ETL profile: " +
                selectedProfileFolder.wstring();
        launchProfileName.clear();
        return false;
    }

    bool movedPreviousConfig = false;
    filesystemError.clear();
    if (std::filesystem::is_regular_file(destination, filesystemError) && !filesystemError) {
        previousConfigBackup = availableSiblingPath(
            selectedProfileFolder,
            L"etconfigBEFOREfragfinder-" + timestamp,
            L".cfg");
        if (previousConfigBackup.empty()) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            error = L"Could not reserve a backup name for the existing etconfig.cfg.";
            launchProfileName.clear();
            return false;
        }
        filesystemError.clear();
        std::filesystem::rename(destination, previousConfigBackup, filesystemError);
        if (filesystemError) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            error = L"Could not back up the selected profile's existing etconfig.cfg. "
                    L"Close ET: Legacy and try again.";
            previousConfigBackup.clear();
            launchProfileName.clear();
            return false;
        }
        movedPreviousConfig = true;
    }

    filesystemError.clear();
    std::filesystem::rename(temporary, destination, filesystemError);
    if (filesystemError) {
        if (movedPreviousConfig) {
            std::error_code restoreError;
            std::filesystem::rename(previousConfigBackup, destination, restoreError);
            if (restoreError) {
                error = L"Could not install the selected CFG and could not automatically "
                        L"restore the previous etconfig.cfg. The backup remains at: " +
                        previousConfigBackup.wstring();
            } else {
                previousConfigBackup.clear();
                error = L"Could not install the selected CFG. The previous etconfig.cfg "
                        L"was restored.";
            }
        } else {
            error = L"Could not install the selected CFG as etconfig.cfg.";
        }
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        launchProfileName.clear();
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> discoverLegacyFragFinderProfiles(
    const std::filesystem::path& etlHomeFolder) {
    std::vector<std::filesystem::path> profiles;
    std::set<std::wstring> seen;
    std::error_code error;
    if (!std::filesystem::is_directory(etlHomeFolder, error) || error) return profiles;

    auto inspectRoot = [&](const std::filesystem::path& root) {
        error.clear();
        if (!std::filesystem::is_directory(root, error) || error) return;
        std::filesystem::directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            std::error_code entryError;
            const std::filesystem::file_status status =
                iterator->symlink_status(entryError);
            if (entryError || !std::filesystem::is_directory(status) ||
                std::filesystem::is_symlink(status) ||
                !isGeneratedProfileName(iterator->path().filename().wstring())) {
                continue;
            }
            std::wstring key = iterator->path().lexically_normal().generic_wstring();
            std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
                return std::towlower(character);
            });
            if (seen.insert(key).second) profiles.push_back(iterator->path());
        }
        error.clear();
    };

    inspectRoot(etlHomeFolder / L"profiles");
    inspectRoot(etlHomeFolder / L"profile");
    std::filesystem::directory_iterator mods(
        etlHomeFolder, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    for (; !error && mods != end; mods.increment(error)) {
        std::error_code entryError;
        if (!mods->is_directory(entryError) || entryError) continue;
        inspectRoot(mods->path() / L"profiles");
        inspectRoot(mods->path() / L"profile");
    }
    std::sort(profiles.begin(), profiles.end());
    return profiles;
}

std::filesystem::path expectedEtlVideoPath(
    const std::wstring& safeBaseName,
    const ClipExportSettings& settings) {
    return settings.etlHomeFolder / L"videos" / (safeBaseName + L".mp4");
}

bool isCompleteMp4(const std::filesystem::path& path) {
    std::error_code sizeError;
    const std::uint64_t fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError || fileSize < 24) return false;

    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    bool foundFileType = false;
    bool foundMovie = false;
    bool foundMedia = false;
    std::uint64_t offset = 0;
    for (int box = 0; box < 256 && offset + 8 <= fileSize; ++box) {
        unsigned char header[16]{};
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        input.read(reinterpret_cast<char*>(header), 8);
        if (input.gcount() != 8) return false;

        std::uint64_t boxSize = readBigEndian32(header);
        std::uint64_t headerSize = 8;
        if (boxSize == 1) {
            input.read(reinterpret_cast<char*>(header + 8), 8);
            if (input.gcount() != 8) return false;
            boxSize = readBigEndian64(header + 8);
            headerSize = 16;
        } else if (boxSize == 0) {
            boxSize = fileSize - offset;
        }
        if (boxSize < headerSize || boxSize > fileSize - offset) return false;

        const char type[5] = {
            static_cast<char>(header[4]), static_cast<char>(header[5]),
            static_cast<char>(header[6]), static_cast<char>(header[7]), '\0'};
        if (std::string(type) == "ftyp") foundFileType = true;
        if (std::string(type) == "moov") foundMovie = true;
        if (std::string(type) == "mdat") foundMedia = true;

        offset += boxSize;
        if (offset == fileSize) break;
    }
    return foundFileType && foundMovie && foundMedia;
}

std::wstring buildDiscordCopyArguments(
    const std::filesystem::path& input,
    const std::filesystem::path& output) {
    // Keep the original render untouched. This second encode deliberately uses
    // conservative parameters that are widely supported by browser/Discord
    // players, even when the master was captured at 120 FPS or above 1080p.
    return L"-y -nostdin -i \"" + input.wstring() +
           L"\" -map 0:v:0 -map 0:a? "
           L"-vf \"scale=1920:1080:force_original_aspect_ratio=decrease:flags=lanczos,"
           L"pad=1920:1080:(ow-iw)/2:(oh-ih)/2:black,fps=60\" "
           L"-fps_mode cfr -c:v libx264 -preset medium -crf 18 "
           L"-pix_fmt yuv420p -profile:v high -level:v 4.2 "
           L"-g 120 -keyint_min 120 -sc_threshold 0 "
           L"-c:a aac -b:a 192k -ar 48000 -ac 2 "
           L"-movflags faststart \"" + output.wstring() + L"\"";
}

} // namespace etlfrag
