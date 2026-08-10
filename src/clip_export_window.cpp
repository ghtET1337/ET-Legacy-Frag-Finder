// SPDX-License-Identifier: GPL-3.0-or-later
#include "clip_export_window.hpp"

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfplay.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace etlfrag::clipui {
namespace {

constexpr wchar_t kClassName[] = L"ETLFragFinderClipExporter";
constexpr UINT kQueueChanged = WM_APP + 80;
constexpr UINT kRenderFinished = WM_APP + 81;
constexpr UINT kMediaEvent = WM_APP + 82;

constexpr COLORREF kBackground = RGB(14, 17, 21);
constexpr COLORREF kPanel = RGB(24, 29, 35);
constexpr COLORREF kControl = RGB(31, 37, 44);
constexpr COLORREF kText = RGB(238, 241, 245);
constexpr COLORREF kMuted = RGB(158, 168, 180);
constexpr COLORREF kSuccess = RGB(104, 199, 139);
constexpr COLORREF kDanger = RGB(234, 107, 107);

enum ControlId {
    IdOutputPath = 7101,
    IdChooseOutput,
    IdHomePath,
    IdChooseHome,
    IdFfmpegPath,
    IdChooseFfmpeg,
    IdPreRoll,
    IdPostRoll,
    IdResolution,
    IdWidth,
    IdHeight,
    IdFrameRate,
    IdQuality,
    IdEngineMode,
    IdHud,
    IdDiscordCopy,
    IdStartQueue,
    IdCancel,
    IdRemove,
    IdClearFinished,
    IdOpenOutput,
    IdPreview,
    IdOpenExternal,
    IdQueue,
    IdPreviewPanel,
    IdStatus,
};

enum class JobStatus {
    Queued,
    Starting,
    Rendering,
    Completed,
    Failed,
    Cancelled,
};

struct ClipJob {
    std::uint64_t id = 0;
    ClipSource source;
    ClipRange range;
    ClipExportSettings settings;
    std::wstring baseName;
    JobStatus status = JobStatus::Queued;
    std::wstring detail = L"Waiting";
    std::filesystem::path outputPath;
    std::filesystem::path logPath;
};

struct State;
State* gState = nullptr;

class MediaCallback final : public IMFPMediaPlayerCallback {
public:
    explicit MediaCallback(HWND window) : window_(window) {}

    STDMETHODIMP QueryInterface(REFIID iid, void** value) override {
        if (value == nullptr) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFPMediaPlayerCallback)) {
            *value = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *value = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }

    STDMETHODIMP_(ULONG) Release() override {
        const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&references_));
        if (remaining == 0) delete this;
        return remaining;
    }

    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* eventHeader) override {
        if (window_ != nullptr && eventHeader != nullptr) {
            PostMessageW(window_, kMediaEvent, eventHeader->eEventType, eventHeader->hrEvent);
        }
    }

private:
    ~MediaCallback() = default;
    LONG references_ = 1;
    HWND window_ = nullptr;
};

struct State {
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND outputPath = nullptr;
    HWND chooseOutput = nullptr;
    HWND homePath = nullptr;
    HWND chooseHome = nullptr;
    HWND ffmpegPath = nullptr;
    HWND chooseFfmpeg = nullptr;
    HWND preRoll = nullptr;
    HWND postRoll = nullptr;
    HWND resolution = nullptr;
    HWND width = nullptr;
    HWND height = nullptr;
    HWND frameRate = nullptr;
    HWND quality = nullptr;
    HWND engineMode = nullptr;
    HWND hud = nullptr;
    HWND discordCopy = nullptr;
    HWND startQueue = nullptr;
    HWND cancel = nullptr;
    HWND remove = nullptr;
    HWND clearFinished = nullptr;
    HWND openOutput = nullptr;
    HWND preview = nullptr;
    HWND openExternal = nullptr;
    HWND queue = nullptr;
    HWND previewPanel = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
    HFONT smallFont = nullptr;
    HFONT titleFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH controlBrush = nullptr;
    int dpi = 96;

    std::filesystem::path etlExecutable;
    std::filesystem::path settingsPath;
    bool launchAsAdministrator = false;
    ClipExportSettings settings;
    std::vector<ClipJob> jobs;
    std::uint64_t nextId = 1;
    std::mutex jobsMutex;
    std::atomic_bool rendering{false};
    std::atomic_bool cancelRequested{false};
    HANDLE worker = nullptr;
    HANDLE activeProcess = nullptr;
    std::mutex processMutex;

    bool mediaFoundationStarted = false;
    HMODULE mediaFoundationLibrary = nullptr;
    HMODULE mediaPlayerLibrary = nullptr;
    HRESULT(WINAPI* mediaStartup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI* mediaShutdown)() = nullptr;
    HRESULT(WINAPI* createMediaPlayer)(
        LPCWSTR, BOOL, MFP_CREATION_OPTIONS, IMFPMediaPlayerCallback*, HWND,
        IMFPMediaPlayer**) = nullptr;
    IMFPMediaPlayer* mediaPlayer = nullptr;
    MediaCallback* mediaCallback = nullptr;
};

int dpiFor(HWND window) {
    using Function = UINT(WINAPI*)(HWND);
    if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
        if (auto function = reinterpret_cast<Function>(GetProcAddress(user, "GetDpiForWindow"))) {
            const int dpi = static_cast<int>(function(window));
            if (dpi > 0) return dpi;
        }
    }
    HDC dc = GetDC(window);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) ReleaseDC(window, dc);
    return dpi > 0 ? dpi : 96;
}

int scaled(int value) {
    return MulDiv(value, gState != nullptr ? gState->dpi : 96, 96);
}

std::wstring getText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length + 1), L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void setFont(HWND control, HFONT font = nullptr) {
    if (control != nullptr) {
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font != nullptr ? font : gState->font),
            TRUE);
    }
}

HWND makeControl(
    DWORD extendedStyle,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int id,
    HFONT font = nullptr) {
    HWND control = CreateWindowExW(
        extendedStyle,
        className,
        text,
        style | WS_CHILD | WS_VISIBLE,
        0,
        0,
        10,
        10,
        gState->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    setFont(control, font);
    return control;
}

void setStatus(const std::wstring& value) {
    if (gState != nullptr && gState->status != nullptr) {
        SetWindowTextW(gState->status, value.c_str());
    }
}

std::wstring environmentValue(const wchar_t* name) {
    const DWORD count = GetEnvironmentVariableW(name, nullptr, 0);
    if (count == 0) return {};
    std::wstring value(count, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), count);
    if (written == 0) return {};
    value.resize(written);
    return value;
}

std::filesystem::path modulePath() {
    std::wstring value(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    value.resize(length);
    return std::filesystem::path(value);
}

std::filesystem::path knownFolder(REFKNOWNFOLDERID identifier) {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(identifier, KF_FLAG_DEFAULT, nullptr, &raw)) && raw != nullptr) {
        std::filesystem::path result(raw);
        CoTaskMemFree(raw);
        return result;
    }
    return {};
}

std::filesystem::path inferEtlHome(const ClipSource* source) {
    if (source != nullptr) {
        std::filesystem::path current = source->demoPath.parent_path();
        for (int depth = 0; depth < 5 && !current.empty(); ++depth) {
            const std::wstring name = current.filename().wstring();
            if (_wcsicmp(name.c_str(), L"ETLegacy") == 0 ||
                _wcsicmp(name.c_str(), L"ET Legacy") == 0) {
                return current;
            }
            if (_wcsicmp(name.c_str(), L"demos") == 0 && current.has_parent_path() &&
                current.parent_path().has_parent_path()) {
                const std::filesystem::path candidate = current.parent_path().parent_path();
                if (!candidate.empty() && !candidate.filename().empty()) return candidate;
            }
            current = current.parent_path();
        }
    }
    const std::filesystem::path documents = knownFolder(FOLDERID_Documents);
    if (!documents.empty()) return documents / L"ETLegacy";
    const std::wstring profile = environmentValue(L"USERPROFILE");
    if (!profile.empty()) return std::filesystem::path(profile) / L"Documents" / L"ETLegacy";
    return modulePath().parent_path();
}

std::filesystem::path findFfmpeg(const std::filesystem::path& etlExecutable) {
    for (const std::filesystem::path& candidate : {
             etlExecutable.parent_path() / L"ffmpeg.exe",
             modulePath().parent_path() / L"ffmpeg.exe"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) return candidate;
    }
    std::wstring buffer(32768, L'\0');
    const DWORD length = SearchPathW(
        nullptr,
        L"ffmpeg.exe",
        nullptr,
        static_cast<DWORD>(buffer.size()),
        buffer.data(),
        nullptr);
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
    return {};
}

std::optional<std::filesystem::path> chooseFolder(HWND owner, const wchar_t* title) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog))) || dialog == nullptr) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(title);
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                result = std::filesystem::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

std::optional<std::filesystem::path> chooseFfmpeg(HWND owner) {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"ffmpeg executable (ffmpeg.exe)\0ffmpeg.exe\0Applications (*.exe)\0*.exe\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = L"Locate ffmpeg.exe";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return std::filesystem::path(path.c_str());
}

int readIniInt(const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(
        L"ClipExport", key, fallback, gState->settingsPath.c_str()));
}

std::filesystem::path readIniPath(const wchar_t* key) {
    std::wstring value(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(
        L"ClipExport", key, L"", value.data(), static_cast<DWORD>(value.size()),
        gState->settingsPath.c_str());
    value.resize(length);
    return std::filesystem::path(value);
}

std::filesystem::path readEtlUserDataFolder() {
    std::wstring value(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(
        L"ETLegacy",
        L"UserDataFolder",
        L"",
        value.data(),
        static_cast<DWORD>(value.size()),
        gState->settingsPath.c_str());
    value.resize(length);
    return std::filesystem::path(value);
}

std::wstring readPlaybackValue(const wchar_t* key) {
    std::wstring value(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(
        L"Playback",
        key,
        L"",
        value.data(),
        static_cast<DWORD>(value.size()),
        gState->settingsPath.c_str());
    value.resize(length);
    return value;
}

void writeIni(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(L"ClipExport", key, value.c_str(), gState->settingsPath.c_str());
}

void saveSettings(const ClipExportSettings& settings) {
    writeIni(L"OutputFolder", settings.outputFolder.wstring());
    writeIni(L"EtlHomeFolder", settings.etlHomeFolder.wstring());
    writeIni(L"FfmpegExecutable", settings.ffmpegExecutable.wstring());
    writeIni(L"PreRollMs", std::to_wstring(settings.preRollMs));
    writeIni(L"PostRollMs", std::to_wstring(settings.postRollMs));
    writeIni(L"Width", std::to_wstring(settings.width));
    writeIni(L"Height", std::to_wstring(settings.height));
    writeIni(L"FrameRate", std::to_wstring(settings.frameRate));
    writeIni(L"DrawHud", settings.drawHud ? L"1" : L"0");
    writeIni(L"CreateDiscordCopy", settings.createDiscordCopy ? L"1" : L"0");
    writeIni(L"Quality", std::to_wstring(static_cast<int>(settings.quality)));
    writeIni(L"EngineMode", std::to_wstring(static_cast<int>(settings.engineMode)));
    WritePrivateProfileStringW(
        L"ETLegacy",
        L"UserDataFolder",
        settings.etlHomeFolder.c_str(),
        gState->settingsPath.c_str());
}

void loadSettings(const ClipSource* source) {
    gState->settings.preRollMs = std::clamp(readIniInt(L"PreRollMs", 5000), 0, 120000);
    gState->settings.postRollMs = std::clamp(readIniInt(L"PostRollMs", 3000), 0, 120000);
    gState->settings.width = std::clamp(readIniInt(L"Width", 1920), 320, 7680);
    gState->settings.height = std::clamp(readIniInt(L"Height", 1080), 240, 4320);
    gState->settings.frameRate = std::clamp(readIniInt(L"FrameRate", 60), 1, 240);
    gState->settings.drawHud = readIniInt(L"DrawHud", 1) != 0;
    gState->settings.createDiscordCopy = readIniInt(L"CreateDiscordCopy", 0) != 0;
    gState->settings.quality = static_cast<ClipQuality>(
        std::clamp(readIniInt(L"Quality", 1), 0, 3));
    gState->settings.engineMode = static_cast<ClipEngineMode>(
        std::clamp(readIniInt(L"EngineMode", 1), 0, 1));
    gState->settings.outputFolder = readIniPath(L"OutputFolder");
    if (gState->settings.outputFolder.empty()) {
        const std::filesystem::path videos = knownFolder(FOLDERID_Videos);
        gState->settings.outputFolder =
            (videos.empty() ? modulePath().parent_path() : videos) / L"ETL Frag Finder";
    }
    gState->settings.etlHomeFolder = readEtlUserDataFolder();
    if (gState->settings.etlHomeFolder.empty()) {
        gState->settings.etlHomeFolder = readIniPath(L"EtlHomeFolder");
    }
    if (gState->settings.etlHomeFolder.empty()) {
        gState->settings.etlHomeFolder = inferEtlHome(source);
    }
    gState->settings.ffmpegExecutable = readIniPath(L"FfmpegExecutable");
    std::error_code error;
    if (!std::filesystem::is_regular_file(gState->settings.ffmpegExecutable, error) || error) {
        gState->settings.ffmpegExecutable = findFfmpeg(gState->etlExecutable);
    }
}

int parseInteger(HWND control, int fallback) {
    const std::wstring value = getText(control);
    if (value.empty()) return fallback;
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

void controlsFromSettings() {
    SetWindowTextW(gState->outputPath, gState->settings.outputFolder.c_str());
    SetWindowTextW(gState->homePath, gState->settings.etlHomeFolder.c_str());
    SetWindowTextW(gState->ffmpegPath, gState->settings.ffmpegExecutable.c_str());
    auto seconds = [](int milliseconds) {
        std::wostringstream value;
        value << std::fixed << std::setprecision(1)
              << static_cast<double>(milliseconds) / 1000.0;
        return value.str();
    };
    SetWindowTextW(gState->preRoll, seconds(gState->settings.preRollMs).c_str());
    SetWindowTextW(gState->postRoll, seconds(gState->settings.postRollMs).c_str());
    SetWindowTextW(gState->width, std::to_wstring(gState->settings.width).c_str());
    SetWindowTextW(gState->height, std::to_wstring(gState->settings.height).c_str());
    SetWindowTextW(gState->frameRate, std::to_wstring(gState->settings.frameRate).c_str());
    SendMessageW(gState->quality, CB_SETCURSEL, static_cast<WPARAM>(gState->settings.quality), 0);
    SendMessageW(gState->engineMode, CB_SETCURSEL, static_cast<WPARAM>(gState->settings.engineMode), 0);
    SendMessageW(gState->hud, BM_SETCHECK, gState->settings.drawHud ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(gState->discordCopy, BM_SETCHECK,
                 gState->settings.createDiscordCopy ? BST_CHECKED : BST_UNCHECKED, 0);

    int resolutionIndex = 4;
    const std::pair<int, int> size{gState->settings.width, gState->settings.height};
    const std::pair<int, int> presets[] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    for (int index = 0; index < 4; ++index) {
        if (size == presets[index]) resolutionIndex = index;
    }
    SendMessageW(gState->resolution, CB_SETCURSEL, resolutionIndex, 0);
}

std::optional<ClipExportSettings> settingsFromControls() {
    ClipExportSettings settings = gState->settings;
    auto parseSeconds = [](HWND control, int fallback) {
        std::wstring value = getText(control);
        std::replace(value.begin(), value.end(), L',', L'.');
        try {
            std::size_t consumed = 0;
            const double seconds = std::stod(value, &consumed);
            if (consumed != value.size() || seconds < 0.0 || seconds > 120.0) return fallback;
            return static_cast<int>(std::lround(seconds * 1000.0));
        } catch (...) {
            return fallback;
        }
    };
    settings.preRollMs = parseSeconds(gState->preRoll, -1);
    settings.postRollMs = parseSeconds(gState->postRoll, -1);
    settings.width = parseInteger(gState->width, -1);
    settings.height = parseInteger(gState->height, -1);
    settings.frameRate = parseInteger(gState->frameRate, -1);
    settings.drawHud = SendMessageW(gState->hud, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.createDiscordCopy =
        SendMessageW(gState->discordCopy, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int quality = static_cast<int>(SendMessageW(gState->quality, CB_GETCURSEL, 0, 0));
    const int engineMode = static_cast<int>(SendMessageW(gState->engineMode, CB_GETCURSEL, 0, 0));
    settings.quality = static_cast<ClipQuality>(std::clamp(quality, 0, 3));
    settings.engineMode = static_cast<ClipEngineMode>(std::clamp(engineMode, 0, 1));
    settings.outputFolder = std::filesystem::path(getText(gState->outputPath));
    settings.etlHomeFolder = std::filesystem::path(getText(gState->homePath));
    settings.ffmpegExecutable = std::filesystem::path(getText(gState->ffmpegPath));
    // The main-window selector can be changed while the exporter remains
    // open. Refresh these two values at the moment a queue is started so the
    // visible global choice always applies to every still-queued job.
    settings.sourceProfileFolder =
        std::filesystem::path(readPlaybackValue(L"ProfileFolder"));
    const std::wstring startupConfig = readPlaybackValue(L"StartupConfig");
    settings.startupConfig =
        startupConfig == L"@destiny"
            ? modulePath().parent_path() / L"presets" / L"destiny-fragmovie.cfg"
            : std::filesystem::path(startupConfig);
    if (settings.preRollMs < 0 || settings.postRollMs < 0 || settings.width < 0 ||
        settings.height < 0 || settings.frameRate < 0) {
        MessageBoxW(
            gState->window,
            L"Check the pre-roll, post-roll, resolution and FPS values.",
            L"Invalid clip settings",
            MB_OK | MB_ICONWARNING);
        return std::nullopt;
    }
    return settings;
}

std::wstring statusName(JobStatus status) {
    switch (status) {
        case JobStatus::Queued: return L"Queued";
        case JobStatus::Starting: return L"Starting ETL";
        case JobStatus::Rendering: return L"Rendering";
        case JobStatus::Completed: return L"Completed";
        case JobStatus::Failed: return L"Failed";
        case JobStatus::Cancelled: return L"Cancelled";
    }
    return L"Unknown";
}

std::wstring durationText(std::int32_t milliseconds) {
    const int totalSeconds = std::max(0, milliseconds) / 1000;
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    const int millis = std::max(0, milliseconds) % 1000;
    std::wostringstream value;
    value << minutes << L':' << std::setfill(L'0') << std::setw(2) << seconds
          << L'.' << std::setw(3) << millis;
    return value.str();
}

void setListCell(HWND list, int row, int column, const std::wstring& value) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(value.c_str()));
}

void populateQueue() {
    if (gState == nullptr || gState->queue == nullptr) return;
    const int selected = ListView_GetNextItem(gState->queue, -1, LVNI_SELECTED);
    std::uint64_t selectedId = 0;
    if (selected >= 0) {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = selected;
        if (ListView_GetItem(gState->queue, &item)) selectedId = static_cast<std::uint64_t>(item.lParam);
    }
    ListView_DeleteAllItems(gState->queue);
    std::lock_guard<std::mutex> lock(gState->jobsMutex);
    int row = 0;
    for (const ClipJob& job : gState->jobs) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(job.source.label.c_str());
        item.lParam = static_cast<LPARAM>(job.id);
        ListView_InsertItem(gState->queue, &item);
        setListCell(gState->queue, row, 1, job.source.demoPath.filename().wstring());
        setListCell(gState->queue, row, 2, durationText(job.range.startMs));
        setListCell(gState->queue, row, 3, durationText(job.range.endMs));
        setListCell(gState->queue, row, 4, durationText(job.range.endMs - job.range.startMs));
        std::wstring format = std::to_wstring(job.settings.width) + L"x" +
                              std::to_wstring(job.settings.height) + L" @ " +
                              std::to_wstring(job.settings.frameRate);
        setListCell(gState->queue, row, 5, format);
        setListCell(gState->queue, row, 6, statusName(job.status));
        setListCell(gState->queue, row, 7, job.detail);
        if (job.id == selectedId) {
            ListView_SetItemState(gState->queue, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
        ++row;
    }
    EnableWindow(gState->startQueue, !gState->rendering && row > 0);
    EnableWindow(gState->cancel, gState->rendering);
}

std::optional<std::size_t> selectedJobIndex() {
    const int row = ListView_GetNextItem(gState->queue, -1, LVNI_SELECTED);
    if (row < 0) return std::nullopt;
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!ListView_GetItem(gState->queue, &item)) return std::nullopt;
    const std::uint64_t id = static_cast<std::uint64_t>(item.lParam);
    std::lock_guard<std::mutex> lock(gState->jobsMutex);
    for (std::size_t index = 0; index < gState->jobs.size(); ++index) {
        if (gState->jobs[index].id == id) return index;
    }
    return std::nullopt;
}

std::size_t addSources(const std::vector<ClipSource>& sources) {
    std::lock_guard<std::mutex> lock(gState->jobsMutex);
    std::size_t added = 0;
    for (const ClipSource& source : sources) {
        const bool duplicate = std::any_of(
            gState->jobs.begin(), gState->jobs.end(),
            [&source](const ClipJob& job) {
                return job.source.demoPath == source.demoPath &&
                       job.source.actionStartMs == source.actionStartMs &&
                       job.source.actionEndMs == source.actionEndMs &&
                       job.status != JobStatus::Failed && job.status != JobStatus::Cancelled;
            });
        if (duplicate) continue;
        ClipJob job;
        job.id = gState->nextId++;
        job.source = source;
        job.settings = gState->settings;
        job.range = calculateClipRange(source, job.settings);
        job.baseName = makeSafeClipBaseName(source, job.range, job.id);
        gState->jobs.push_back(std::move(job));
        ++added;
    }
    if (added > 0) {
        setStatus(std::to_wstring(added) + L" action(s) added to the clip render queue.");
    } else if (!sources.empty()) {
        setStatus(L"Those actions are already present in the queue.");
    }
    return added;
}

bool binaryContainsNativeCommand(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    constexpr char needle[] = "video-pipe-range";
    std::string tail;
    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) break;
        std::string block = tail;
        block.append(buffer.data(), static_cast<std::size_t>(count));
        if (block.find(needle) != std::string::npos) return true;
        const std::size_t keep = sizeof(needle) - 2;
        tail = block.size() > keep ? block.substr(block.size() - keep) : block;
    }
    return false;
}

void prependFfmpegToPath(const std::filesystem::path& ffmpeg) {
    if (ffmpeg.empty()) return;
    const std::wstring folder = ffmpeg.parent_path().wstring();
    std::wstring current = environmentValue(L"PATH");
    if (current.find(folder) == std::wstring::npos) {
        SetEnvironmentVariableW(L"PATH", (folder + L";" + current).c_str());
    }
}

std::filesystem::path uniqueDestination(
    const std::filesystem::path& folder,
    const std::filesystem::path& requested) {
    std::filesystem::path result = folder / requested.filename();
    std::error_code error;
    if (!std::filesystem::exists(result, error)) return result;
    for (int index = 2; index < 10000; ++index) {
        result = folder / (requested.stem().wstring() + L"_" + std::to_wstring(index) +
                           requested.extension().wstring());
        if (!std::filesystem::exists(result, error)) return result;
    }
    return folder / (requested.stem().wstring() + L"_copy" + requested.extension().wstring());
}

bool moveFileRecoverably(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::error_code& error) {
    std::filesystem::rename(source, destination, error);
    if (!error) return true;
    error.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) return false;
    std::error_code removeError;
    std::filesystem::remove(source, removeError);
    return true;
}

std::vector<std::filesystem::path> videoOutputCandidates(const ClipJob& job) {
    std::vector<std::filesystem::path> candidates;
    std::set<std::wstring> known;
    const std::wstring filename = job.baseName + L".mp4";
    const auto add = [&](const std::filesystem::path& videosFolder) {
        if (videosFolder.empty()) return;
        const std::filesystem::path candidate =
            (videosFolder / filename).lexically_normal();
        std::wstring key = candidate.wstring();
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
            return static_cast<wchar_t>(std::towlower(value));
        });
        if (known.insert(key).second) candidates.push_back(candidate);
    };
    const auto addRoot = [&](const std::filesystem::path& root) {
        if (root.empty()) return;
        add(root / L"videos");
        std::error_code iteratorError;
        for (std::filesystem::directory_iterator entry(root, iteratorError), end;
             !iteratorError && entry != end;
             entry.increment(iteratorError)) {
            std::error_code typeError;
            if (entry->is_directory(typeError) && !typeError) {
                add(entry->path() / L"videos");
            }
        }
    };

    // ETL writes video-pipe files below fs_homepath/fs_game/videos. The home
    // user-data folder selected in Frag Finder is fs_homepath, while fs_game is
    // commonly "legacy" and may be another mod. Check the root and each
    // immediate mod folder without assuming one fixed game directory.
    addRoot(job.settings.etlHomeFolder);
    addRoot(gState->etlExecutable.parent_path());

    // A demo stored under <home>/<mod>/demos gives us the exact mod folder,
    // even when it is outside the selected ETL user-data folder or uses a custom name.
    std::filesystem::path current = job.source.demoPath.parent_path();
    for (int depth = 0; depth < 5 && !current.empty(); ++depth) {
        const std::wstring name = current.filename().wstring();
        if (_wcsicmp(name.c_str(), L"demos") == 0 && current.has_parent_path()) {
            add(current.parent_path() / L"videos");
            break;
        }
        current = current.parent_path();
    }
    return candidates;
}

std::filesystem::path firstExistingFile(
    const std::vector<std::filesystem::path>& candidates) {
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
    return {};
}

bool canOpenExclusively(const std::filesystem::path& path) {
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

std::filesystem::path firstFinalizedVideo(
    const std::vector<std::filesystem::path>& candidates) {
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(candidate, error) || error) continue;
        // ETL's Windows video-pipe closes its FILE stream before ffmpeg has
        // necessarily finished writing the MP4. Do not copy an open or
        // structurally incomplete file: that produces an unplayable mdat-only
        // file without the final moov atom.
        if (canOpenExclusively(candidate) && isCompleteMp4(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::filesystem::path> logCandidates(
    const std::vector<std::filesystem::path>& videos) {
    std::vector<std::filesystem::path> logs;
    logs.reserve(videos.size());
    for (const std::filesystem::path& video : videos) {
        logs.emplace_back(video.wstring() + L"-log.txt");
    }
    return logs;
}

BOOL CALLBACK closeProcessWindow(HWND window, LPARAM processIdValue) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == static_cast<DWORD>(processIdValue)) {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

void cancelProcessGracefully(HANDLE process) {
    if (process == nullptr) return;
    const DWORD processId = GetProcessId(process);
    EnumWindows(closeProcessWindow, static_cast<LPARAM>(processId));
    if (WaitForSingleObject(process, 5000) == WAIT_TIMEOUT) {
        TerminateProcess(process, 2);
        WaitForSingleObject(process, 2000);
    }
}

struct ChildProcessResult {
    bool started = false;
    bool cancelled = false;
    DWORD exitCode = 1;
    DWORD windowsError = ERROR_SUCCESS;
};

ChildProcessResult runHiddenFfmpeg(
    const std::filesystem::path& executable,
    const std::wstring& arguments,
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& logPath) {
    ChildProcessResult result;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    const HANDLE log = CreateFileW(
        logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        result.windowsError = GetLastError();
        return result;
    }
    const HANDLE nullInput = CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        result.windowsError = GetLastError();
        CloseHandle(log);
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = nullInput;
    startup.hStdOutput = log;
    startup.hStdError = log;

    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + executable.wstring() + L"\" " + arguments;
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const BOOL created = CreateProcessW(
        executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startup, &process);
    result.windowsError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(nullInput);
    CloseHandle(log);
    if (!created) return result;

    result.started = true;
    CloseHandle(process.hThread);
    {
        std::lock_guard<std::mutex> processLock(gState->processMutex);
        gState->activeProcess = process.hProcess;
    }
    while (WaitForSingleObject(process.hProcess, 250) == WAIT_TIMEOUT) {
        if (gState->cancelRequested.load()) {
            result.cancelled = true;
            cancelProcessGracefully(process.hProcess);
            break;
        }
    }
    GetExitCodeProcess(process.hProcess, &result.exitCode);
    {
        std::lock_guard<std::mutex> processLock(gState->processMutex);
        if (gState->activeProcess == process.hProcess) gState->activeProcess = nullptr;
    }
    CloseHandle(process.hProcess);
    return result;
}

void updateJob(
    std::size_t index,
    JobStatus status,
    const std::wstring& detail,
    const std::filesystem::path& output = {},
    const std::filesystem::path& log = {}) {
    {
        std::lock_guard<std::mutex> lock(gState->jobsMutex);
        if (index >= gState->jobs.size()) return;
        gState->jobs[index].status = status;
        gState->jobs[index].detail = detail;
        if (!output.empty()) gState->jobs[index].outputPath = output;
        if (!log.empty()) gState->jobs[index].logPath = log;
    }
    PostMessageW(gState->window, kQueueChanged, 0, 0);
}

unsigned __stdcall renderWorker(void*) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (std::size_t index = 0;; ++index) {
        if (gState->cancelRequested.load()) break;
        ClipJob job;
        {
            std::lock_guard<std::mutex> lock(gState->jobsMutex);
            while (index < gState->jobs.size() && gState->jobs[index].status != JobStatus::Queued) {
                ++index;
            }
            if (index >= gState->jobs.size()) break;
            job = gState->jobs[index];
        }

        updateJob(index, JobStatus::Starting, L"Launching ET: Legacy");
        std::error_code folderError;
        std::filesystem::create_directories(job.settings.outputFolder, folderError);
        if (folderError) {
            updateJob(index, JobStatus::Failed, L"Could not create an output directory");
            continue;
        }
        const std::vector<std::filesystem::path> expectedVideos =
            videoOutputCandidates(job);
        const std::vector<std::filesystem::path> expectedLogs =
            logCandidates(expectedVideos);
        for (const std::filesystem::path& candidate : expectedVideos) {
            folderError.clear();
            std::filesystem::remove(candidate, folderError);
        }
        for (const std::filesystem::path& candidate : expectedLogs) {
            folderError.clear();
            std::filesystem::remove(candidate, folderError);
        }

        prependFfmpegToPath(job.settings.ffmpegExecutable);
        std::wstring renderProfile;
        std::filesystem::path previousConfigBackup;
        std::wstring preparationError;
        if (!prepareEtlLaunchProfile(
                job.settings.etlHomeFolder,
                job.source.demoPath,
                job.settings.sourceProfileFolder,
                job.settings.startupConfig,
                renderProfile,
                previousConfigBackup,
                preparationError)) {
            updateJob(index, JobStatus::Failed, preparationError);
            continue;
        }
        const std::wstring arguments = buildEtlClipArguments(
            job.source,
            job.baseName,
            job.range,
            job.settings,
            renderProfile);
        const std::wstring workingDirectory =
            gState->etlExecutable.parent_path().wstring();

        SHELLEXECUTEINFOW launch{};
        launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        launch.hwnd = gState->window;
        launch.lpVerb = gState->launchAsAdministrator ? L"runas" : L"open";
        launch.lpFile = gState->etlExecutable.c_str();
        launch.lpParameters = arguments.c_str();
        launch.lpDirectory = workingDirectory.c_str();
        launch.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&launch) || launch.hProcess == nullptr) {
            updateJob(index, JobStatus::Failed,
                      L"ETL launch failed (Windows error " + std::to_wstring(GetLastError()) + L")");
            continue;
        }
        {
            std::lock_guard<std::mutex> processLock(gState->processMutex);
            gState->activeProcess = launch.hProcess;
        }
        updateJob(index, JobStatus::Rendering,
                  job.settings.engineMode == ClipEngineMode::NativeVideoPipeRange
                      ? L"video-pipe-range is rendering — ETL closes when complete"
                      : L"Stock ETL timed range is rendering — ETL closes when complete");

        while (WaitForSingleObject(launch.hProcess, 250) == WAIT_TIMEOUT) {
            if (gState->cancelRequested.load()) {
                cancelProcessGracefully(launch.hProcess);
                break;
            }
        }
        DWORD exitCode = 1;
        GetExitCodeProcess(launch.hProcess, &exitCode);
        CloseHandle(launch.hProcess);
        {
            std::lock_guard<std::mutex> processLock(gState->processMutex);
            gState->activeProcess = nullptr;
        }
        if (gState->cancelRequested.load()) {
            updateJob(index, JobStatus::Cancelled, L"Render cancelled by user");
            break;
        }

        // Stock ETL closes the Windows pipe stream without necessarily waiting
        // for ffmpeg. The ETL process can therefore exit while ffmpeg is still
        // writing mdat and before it has created the MP4 moov atom. Wait for
        // both an exclusive file handle and a complete MP4 box structure.
        // ETL writes below fs_homepath/fs_game/videos, so accept the configured
        // home, the demo's mod directory and discovered one-level mod folders.
        updateJob(index, JobStatus::Rendering, L"ETL finished — finalizing MP4");
        std::filesystem::path renderedVideo;
        for (int attempt = 0; attempt < 480; ++attempt) {
            if (gState->cancelRequested.load()) break;
            renderedVideo = firstFinalizedVideo(expectedVideos);
            if (!renderedVideo.empty()) break;
            Sleep(250);
        }
        if (gState->cancelRequested.load()) {
            updateJob(index, JobStatus::Cancelled, L"Render cancelled by user");
            break;
        }
        const std::filesystem::path renderedLog = firstExistingFile(expectedLogs);
        if (renderedVideo.empty()) {
            const std::filesystem::path incompleteVideo = firstExistingFile(expectedVideos);
            std::wstring detail;
            if (!incompleteVideo.empty()) {
                detail = L"ffmpeg left an incomplete MP4 (missing final moov atom)";
            } else {
                detail = L"No MP4 was found in the ETL user-data/mod videos folders";
            }
            if (exitCode != 0) detail += L" (ETL exit " + std::to_wstring(exitCode) + L")";
            if (job.settings.engineMode == ClipEngineMode::NativeVideoPipeRange) {
                detail += L" — verify the patched ETL engine";
            } else {
                detail += L" — check ffmpeg and ETL 2.83+";
            }
            updateJob(index, JobStatus::Failed, detail, incompleteVideo, renderedLog);
            continue;
        }

        const std::filesystem::path destination =
            uniqueDestination(job.settings.outputFolder, renderedVideo.filename());
        std::error_code moveError;
        if (!moveFileRecoverably(renderedVideo, destination, moveError)) {
            const std::string moveMessage = moveError.message();
            updateJob(index, JobStatus::Failed,
                      L"MP4 was rendered but could not be moved: " +
                          std::wstring(moveMessage.begin(), moveMessage.end()),
                      renderedVideo,
                      renderedLog);
            continue;
        }
        std::filesystem::path movedLog;
        if (!renderedLog.empty()) {
            movedLog = destination.parent_path() /
                       (destination.stem().wstring() + L".ffmpeg.log.txt");
            std::error_code logError;
            moveFileRecoverably(renderedLog, movedLog, logError);
        }
        if (!job.settings.createDiscordCopy) {
            updateJob(index, JobStatus::Completed,
                      L"Ready — double-click to preview", destination, movedLog);
            continue;
        }

        const std::filesystem::path discordDestination = uniqueDestination(
            job.settings.outputFolder,
            destination.stem().wstring() + L"-discord.mp4");
        const std::filesystem::path discordTemporary =
            job.settings.outputFolder /
            (discordDestination.stem().wstring() + L".part-" +
             std::to_wstring(job.id) + L".mp4");
        const std::filesystem::path discordLog = uniqueDestination(
            job.settings.outputFolder,
            discordDestination.stem().wstring() + L".ffmpeg.log.txt");
        std::error_code cleanupError;
        std::filesystem::remove(discordTemporary, cleanupError);

        updateJob(index, JobStatus::Rendering,
                  L"Main MP4 ready — creating Discord copy (1080p / 60 FPS)",
                  destination, movedLog);
        const ChildProcessResult discordResult = runHiddenFfmpeg(
            job.settings.ffmpegExecutable,
            buildDiscordCopyArguments(destination, discordTemporary),
            job.settings.outputFolder,
            discordLog);

        if (discordResult.cancelled || gState->cancelRequested.load()) {
            cleanupError.clear();
            std::filesystem::remove(discordTemporary, cleanupError);
            updateJob(index, JobStatus::Completed,
                      L"Main MP4 ready — Discord copy cancelled",
                      destination, movedLog);
            break;
        }
        if (!discordResult.started) {
            updateJob(index, JobStatus::Completed,
                      L"Main MP4 ready — Discord copy could not start (Windows error " +
                          std::to_wstring(discordResult.windowsError) + L")",
                      destination, discordLog);
            continue;
        }
        if (discordResult.exitCode != 0 || !isCompleteMp4(discordTemporary)) {
            cleanupError.clear();
            std::filesystem::remove(discordTemporary, cleanupError);
            updateJob(index, JobStatus::Completed,
                      L"Main MP4 ready — Discord copy failed; see " +
                          discordLog.filename().wstring(),
                      destination, discordLog);
            continue;
        }

        std::error_code discordMoveError;
        if (!moveFileRecoverably(
                discordTemporary, discordDestination, discordMoveError)) {
            updateJob(index, JobStatus::Completed,
                      L"Main MP4 ready — Discord copy could not be moved",
                      destination, discordLog);
            continue;
        }
        updateJob(index, JobStatus::Completed,
                  L"Ready — Discord copy: " + discordDestination.filename().wstring(),
                  destination, movedLog);
    }
    gState->rendering.store(false);
    PostMessageW(gState->window, kRenderFinished, 0, 0);
    CoUninitialize();
    return 0;
}

void startRendering() {
    if (gState->rendering.load()) return;
    const auto selectedSettings = settingsFromControls();
    if (!selectedSettings.has_value()) return;
    std::error_code error;
    if (!std::filesystem::is_regular_file(gState->etlExecutable, error) || error) {
        MessageBoxW(gState->window, L"Locate a valid etl.exe in the main Frag Finder window first.",
                    L"ET: Legacy not found", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!std::filesystem::is_regular_file(selectedSettings->ffmpegExecutable, error) || error) {
        MessageBoxW(gState->window,
                    L"Locate ffmpeg.exe first. ET: Legacy needs it to encode video and audio.",
                    L"ffmpeg not found", MB_OK | MB_ICONWARNING);
        return;
    }
    ClipExportSettings effective = *selectedSettings;
    if (effective.engineMode == ClipEngineMode::NativeVideoPipeRange &&
        !binaryContainsNativeCommand(gState->etlExecutable)) {
        const int choice = MessageBoxW(
            gState->window,
            L"This etl.exe does not contain the video-pipe-range command.\n\n"
            L"Use the corrected stock ET: Legacy 2.83+ time controller for this queue instead?",
            L"Native range command unavailable",
            MB_YESNO | MB_ICONINFORMATION);
        if (choice != IDYES) return;
        effective.engineMode = ClipEngineMode::CompatibleVideoPipe;
        SendMessageW(gState->engineMode, CB_SETCURSEL,
                     static_cast<WPARAM>(ClipEngineMode::CompatibleVideoPipe), 0);
    }

    {
        std::lock_guard<std::mutex> lock(gState->jobsMutex);
        bool hasQueued = false;
        for (ClipJob& job : gState->jobs) {
            if (job.status != JobStatus::Queued) continue;
            job.settings = effective;
            job.range = calculateClipRange(job.source, effective);
            job.baseName = makeSafeClipBaseName(job.source, job.range, job.id);
            job.detail = L"Waiting";
            hasQueued = true;
            if (const auto validation = validateClipExport(job.source, effective)) {
                MessageBoxW(gState->window, validation->c_str(), L"Invalid clip job", MB_OK | MB_ICONWARNING);
                return;
            }
        }
        if (!hasQueued) {
            MessageBoxW(gState->window, L"Add at least one action to the queue.",
                        L"Empty render queue", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    gState->settings = effective;
    saveSettings(effective);
    gState->cancelRequested.store(false);
    gState->rendering.store(true);
    populateQueue();
    unsigned threadId = 0;
    gState->worker = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, renderWorker, nullptr, 0, &threadId));
    if (gState->worker == nullptr) {
        gState->rendering.store(false);
        MessageBoxW(gState->window, L"The render worker could not be started.",
                    L"Clip export error", MB_OK | MB_ICONERROR);
        populateQueue();
        return;
    }
    setStatus(L"Rendering the queue sequentially — ETL will close after each range.");
}

void cancelRendering() {
    if (!gState->rendering.load()) return;
    gState->cancelRequested.store(true);
    setStatus(L"Cancelling — Frag Finder is asking ETL to close cleanly…");
}

void releaseMediaPlayer() {
    if (gState->mediaPlayer != nullptr) {
        gState->mediaPlayer->Shutdown();
        gState->mediaPlayer->Release();
        gState->mediaPlayer = nullptr;
    }
    if (gState->mediaCallback != nullptr) {
        gState->mediaCallback->Release();
        gState->mediaCallback = nullptr;
    }
}

void initializeMediaFoundation() {
    gState->mediaFoundationLibrary = LoadLibraryW(L"mfplat.dll");
    gState->mediaPlayerLibrary = LoadLibraryW(L"mfplay.dll");
    if (gState->mediaFoundationLibrary == nullptr || gState->mediaPlayerLibrary == nullptr) {
        return;
    }
    gState->mediaStartup = reinterpret_cast<decltype(gState->mediaStartup)>(
        GetProcAddress(gState->mediaFoundationLibrary, "MFStartup"));
    gState->mediaShutdown = reinterpret_cast<decltype(gState->mediaShutdown)>(
        GetProcAddress(gState->mediaFoundationLibrary, "MFShutdown"));
    gState->createMediaPlayer = reinterpret_cast<decltype(gState->createMediaPlayer)>(
        GetProcAddress(gState->mediaPlayerLibrary, "MFPCreateMediaPlayer"));
    if (gState->mediaStartup == nullptr || gState->mediaShutdown == nullptr ||
        gState->createMediaPlayer == nullptr) {
        return;
    }
    gState->mediaFoundationStarted =
        SUCCEEDED(gState->mediaStartup(MF_VERSION, MFSTARTUP_FULL));
}

void previewSelected() {
    const auto index = selectedJobIndex();
    if (!index.has_value()) return;
    std::filesystem::path output;
    {
        std::lock_guard<std::mutex> lock(gState->jobsMutex);
        if (*index >= gState->jobs.size() || gState->jobs[*index].status != JobStatus::Completed) return;
        output = gState->jobs[*index].outputPath;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(output, error) || error) return;
    releaseMediaPlayer();
    if (!gState->mediaFoundationStarted || gState->createMediaPlayer == nullptr) {
        MessageBoxW(gState->window,
                    L"The optional Windows Media Foundation preview is not installed. "
                    L"The rendered MP4 can still be opened externally.",
                    L"Preview unavailable", MB_OK | MB_ICONINFORMATION);
        return;
    }
    gState->mediaCallback = new MediaCallback(gState->window);
    const HRESULT result = gState->createMediaPlayer(
        output.c_str(), TRUE, 0, gState->mediaCallback, gState->previewPanel,
        &gState->mediaPlayer);
    if (FAILED(result)) {
        releaseMediaPlayer();
        MessageBoxW(gState->window,
                    L"Windows Media Foundation could not preview this MP4. Use Open externally instead.",
                    L"Preview unavailable", MB_OK | MB_ICONINFORMATION);
        return;
    }
    SetWindowTextW(gState->preview, L"Pause preview");
    setStatus(L"Previewing " + output.filename().wstring());
}

void togglePreview() {
    if (gState->mediaPlayer == nullptr) {
        previewSelected();
        return;
    }
    MFP_MEDIAPLAYER_STATE state = MFP_MEDIAPLAYER_STATE_EMPTY;
    if (FAILED(gState->mediaPlayer->GetState(&state))) return;
    if (state == MFP_MEDIAPLAYER_STATE_PLAYING) {
        gState->mediaPlayer->Pause();
        SetWindowTextW(gState->preview, L"Resume preview");
    } else {
        gState->mediaPlayer->Play();
        SetWindowTextW(gState->preview, L"Pause preview");
    }
}

void openSelectedExternally() {
    const auto index = selectedJobIndex();
    if (!index.has_value()) return;
    std::filesystem::path output;
    {
        std::lock_guard<std::mutex> lock(gState->jobsMutex);
        if (*index >= gState->jobs.size()) return;
        output = gState->jobs[*index].outputPath;
    }
    if (!output.empty()) ShellExecuteW(gState->window, L"open", output.c_str(), nullptr,
                                       output.parent_path().c_str(), SW_SHOWNORMAL);
}

void removeSelected() {
    if (gState->rendering.load()) return;
    const auto index = selectedJobIndex();
    if (!index.has_value()) return;
    {
        std::lock_guard<std::mutex> lock(gState->jobsMutex);
        if (*index < gState->jobs.size()) gState->jobs.erase(gState->jobs.begin() + *index);
    }
    populateQueue();
}

void clearFinished() {
    if (gState->rendering.load()) return;
    {
        std::lock_guard<std::mutex> lock(gState->jobsMutex);
        gState->jobs.erase(
            std::remove_if(
                gState->jobs.begin(), gState->jobs.end(),
                [](const ClipJob& job) { return job.status != JobStatus::Queued; }),
            gState->jobs.end());
    }
    populateQueue();
}

void updateResolutionPreset() {
    const int selection = static_cast<int>(SendMessageW(gState->resolution, CB_GETCURSEL, 0, 0));
    const std::pair<int, int> presets[] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    if (selection >= 0 && selection < 4) {
        SetWindowTextW(gState->width, std::to_wstring(presets[selection].first).c_str());
        SetWindowTextW(gState->height, std::to_wstring(presets[selection].second).c_str());
    }
}

void layout(int width, int height) {
    const int margin = scaled(18);
    const int gap = scaled(10);
    const int button = scaled(126);
    const int editHeight = scaled(30);
    const int labelHeight = scaled(17);
    int y = scaled(62);

    MoveWindow(gState->outputPath, margin, y, width - margin * 2 - button - gap, editHeight, TRUE);
    MoveWindow(gState->chooseOutput, width - margin - button, y, button, editHeight, TRUE);
    y += scaled(38);
    MoveWindow(gState->homePath, margin, y, width - margin * 2 - button - gap, editHeight, TRUE);
    MoveWindow(gState->chooseHome, width - margin - button, y, button, editHeight, TRUE);
    y += scaled(38);
    MoveWindow(gState->ffmpegPath, margin, y, width - margin * 2 - button - gap, editHeight, TRUE);
    MoveWindow(gState->chooseFfmpeg, width - margin - button, y, button, editHeight, TRUE);

    const int settingsTop = y + scaled(47);
    int x = margin;
    auto place = [&](HWND control, int widthValue) {
        MoveWindow(control, x, settingsTop + labelHeight + scaled(3), widthValue, editHeight, TRUE);
        x += widthValue + gap;
    };
    place(gState->preRoll, scaled(82));
    place(gState->postRoll, scaled(82));
    place(gState->resolution, scaled(132));
    place(gState->width, scaled(74));
    place(gState->height, scaled(74));
    place(gState->frameRate, scaled(80));
    place(gState->quality, scaled(145));
    place(gState->engineMode, scaled(205));
    MoveWindow(gState->hud, x, settingsTop + labelHeight + scaled(5), scaled(110), editHeight, TRUE);

    const int actionY = settingsTop + scaled(58);
    int actionX = margin;
    const int actionWidth = scaled(132);
    for (HWND control : {gState->startQueue, gState->cancel, gState->remove,
                         gState->clearFinished, gState->openOutput}) {
        MoveWindow(control, actionX, actionY, actionWidth, scaled(32), TRUE);
        actionX += actionWidth + gap;
    }
    MoveWindow(gState->discordCopy, actionX, actionY + scaled(2),
               scaled(300), editHeight, TRUE);

    const int queueY = actionY + scaled(45);
    const int statusHeight = scaled(26);
    const int previewWidth = std::clamp(width / 3, scaled(300), scaled(480));
    const int contentBottom = height - margin - statusHeight;
    MoveWindow(gState->queue, margin, queueY,
               width - margin * 2 - previewWidth - gap, contentBottom - queueY, TRUE);
    const int previewX = width - margin - previewWidth;
    const int previewButtonY = contentBottom - scaled(38);
    MoveWindow(gState->previewPanel, previewX, queueY, previewWidth,
               std::max(scaled(120), previewButtonY - queueY - gap), TRUE);
    MoveWindow(gState->preview, previewX, previewButtonY, (previewWidth - gap) / 2,
               scaled(32), TRUE);
    MoveWindow(gState->openExternal, previewX + (previewWidth + gap) / 2, previewButtonY,
               (previewWidth - gap) / 2, scaled(32), TRUE);
    MoveWindow(gState->status, margin, height - margin - statusHeight,
               width - margin * 2, statusHeight, TRUE);
    if (gState->mediaPlayer != nullptr) gState->mediaPlayer->UpdateVideo();
}

void paint(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, gState->backgroundBrush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kText);
    SelectObject(dc, gState->titleFont);
    RECT title{scaled(18), scaled(14), client.right - scaled(18), scaled(44)};
    DrawTextW(dc, L"Clip exporter", -1, &title, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, gState->smallFont);
    SetTextColor(dc, kMuted);
    RECT subtitle{scaled(18), scaled(40), client.right - scaled(18), scaled(58)};
    DrawTextW(dc,
              L"Frame-accurate demo ranges through ETL video-pipe, including game audio.",
              -1, &subtitle, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    const int settingsTop = scaled(62 + 38 + 38 + 47);
    const int positions[] = {scaled(18), scaled(110), scaled(202), scaled(344),
                             scaled(428), scaled(512), scaled(602), scaled(757)};
    const wchar_t* labels[] = {L"Pre-roll (s)", L"Post-roll (s)", L"Preset", L"Width",
                               L"Height", L"FPS", L"Quality", L"Range controller"};
    SelectObject(dc, gState->smallFont);
    SetTextColor(dc, kMuted);
    for (int index = 0; index < 8; ++index) {
        RECT label{positions[index], settingsTop, positions[index] + scaled(130),
                   settingsTop + scaled(17)};
        DrawTextW(dc, labels[index], -1, &label, DT_LEFT | DT_SINGLELINE);
    }
    EndPaint(window, &paint);
}

void createInterface(HWND window) {
    gState->window = window;
    gState->dpi = dpiFor(window);
    gState->backgroundBrush = CreateSolidBrush(kBackground);
    gState->panelBrush = CreateSolidBrush(kPanel);
    gState->controlBrush = CreateSolidBrush(kControl);
    gState->font = CreateFontW(-scaled(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    gState->smallFont = CreateFontW(-scaled(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    gState->titleFont = CreateFontW(-scaled(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Semibold");

    const DWORD edit = ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP;
    gState->outputPath = makeControl(0, WC_EDITW, L"", edit | ES_READONLY, IdOutputPath);
    gState->chooseOutput = makeControl(0, WC_BUTTONW, L"Output folder", BS_PUSHBUTTON | WS_TABSTOP, IdChooseOutput);
    gState->homePath = makeControl(0, WC_EDITW, L"", edit | ES_READONLY, IdHomePath);
    gState->chooseHome = makeControl(
        0,
        WC_BUTTONW,
        L"ETL user data",
        BS_PUSHBUTTON | WS_TABSTOP,
        IdChooseHome);
    gState->ffmpegPath = makeControl(0, WC_EDITW, L"", edit | ES_READONLY, IdFfmpegPath);
    gState->chooseFfmpeg = makeControl(0, WC_BUTTONW, L"Locate ffmpeg", BS_PUSHBUTTON | WS_TABSTOP, IdChooseFfmpeg);
    gState->preRoll = makeControl(0, WC_EDITW, L"5.0", ES_CENTER | edit, IdPreRoll);
    gState->postRoll = makeControl(0, WC_EDITW, L"3.0", ES_CENTER | edit, IdPostRoll);
    gState->resolution = makeControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdResolution);
    for (const wchar_t* value : {L"1280 x 720", L"1920 x 1080", L"2560 x 1440",
                                 L"3840 x 2160", L"Custom"}) {
        SendMessageW(gState->resolution, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    gState->width = makeControl(0, WC_EDITW, L"1920", ES_NUMBER | ES_CENTER | edit, IdWidth);
    gState->height = makeControl(0, WC_EDITW, L"1080", ES_NUMBER | ES_CENTER | edit, IdHeight);
    gState->frameRate = makeControl(0, WC_COMBOBOXW, L"60", CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, IdFrameRate);
    for (const wchar_t* value : {L"30", L"60", L"120"}) {
        SendMessageW(gState->frameRate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    gState->quality = makeControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdQuality);
    for (const ClipQuality quality : {ClipQuality::Master, ClipQuality::High,
                                      ClipQuality::Balanced, ClipQuality::Compact}) {
        const std::wstring value = clipQualityName(quality);
        SendMessageW(gState->quality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    }
    gState->engineMode = makeControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdEngineMode);
    for (const ClipEngineMode mode : {ClipEngineMode::CompatibleVideoPipe,
                                      ClipEngineMode::NativeVideoPipeRange}) {
        const std::wstring value = clipEngineModeName(mode);
        SendMessageW(gState->engineMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    }
    gState->hud = makeControl(0, WC_BUTTONW, L"Include HUD", BS_AUTOCHECKBOX | WS_TABSTOP, IdHud);
    gState->discordCopy = makeControl(
        0, WC_BUTTONW, L"Create Discord copy (1080p / 60 FPS)",
        BS_AUTOCHECKBOX | WS_TABSTOP, IdDiscordCopy);
    gState->startQueue = makeControl(0, WC_BUTTONW, L"Render queue", BS_PUSHBUTTON | WS_TABSTOP, IdStartQueue);
    gState->cancel = makeControl(0, WC_BUTTONW, L"Cancel render", BS_PUSHBUTTON | WS_TABSTOP, IdCancel);
    gState->remove = makeControl(0, WC_BUTTONW, L"Remove selected", BS_PUSHBUTTON | WS_TABSTOP, IdRemove);
    gState->clearFinished = makeControl(0, WC_BUTTONW, L"Clear finished", BS_PUSHBUTTON | WS_TABSTOP, IdClearFinished);
    gState->openOutput = makeControl(0, WC_BUTTONW, L"Open output", BS_PUSHBUTTON | WS_TABSTOP, IdOpenOutput);
    gState->preview = makeControl(0, WC_BUTTONW, L"Preview selected", BS_PUSHBUTTON | WS_TABSTOP, IdPreview);
    gState->openExternal = makeControl(0, WC_BUTTONW, L"Open externally", BS_PUSHBUTTON | WS_TABSTOP, IdOpenExternal);
    gState->previewPanel = makeControl(WS_EX_CLIENTEDGE, WC_STATICW,
                                       L"Rendered clip preview", SS_CENTER | SS_CENTERIMAGE,
                                       IdPreviewPanel);

    const DWORD listStyle = LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
    gState->queue = makeControl(0, WC_LISTVIEWW, L"", listStyle, IdQueue);
    ListView_SetExtendedListViewStyle(gState->queue, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(gState->queue, kPanel);
    ListView_SetTextBkColor(gState->queue, kPanel);
    ListView_SetTextColor(gState->queue, kText);
    const wchar_t* headings[] = {L"Action", L"Demo", L"Start", L"End", L"Length",
                                 L"Format", L"Status", L"Details"};
    const int widths[] = {160, 210, 82, 82, 78, 145, 105, 270};
    for (int index = 0; index < 8; ++index) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(headings[index]);
        column.cx = scaled(widths[index]);
        column.iSubItem = index;
        ListView_InsertColumn(gState->queue, index, &column);
    }
    gState->status = makeControl(0, WC_STATICW, L"Ready", SS_LEFT | SS_CENTERIMAGE, IdStatus, gState->smallFont);

    for (HWND control : {gState->outputPath, gState->homePath, gState->ffmpegPath,
                         gState->preRoll, gState->postRoll, gState->resolution,
                         gState->width, gState->height, gState->frameRate,
                         gState->quality, gState->engineMode, gState->queue}) {
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
    for (HWND control : {gState->chooseOutput, gState->chooseHome, gState->chooseFfmpeg,
                         gState->hud, gState->discordCopy, gState->startQueue,
                         gState->cancel, gState->remove,
                         gState->clearFinished, gState->openOutput, gState->preview,
                         gState->openExternal}) {
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
    EnableWindow(gState->cancel, FALSE);
    initializeMediaFoundation();
    controlsFromSettings();
    populateQueue();
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            createInterface(window);
            return 0;
        case WM_SIZE:
            layout(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_PAINT:
            paint(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(gState->backgroundBrush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kControl);
            SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(gState->controlBrush);
        }
        case kQueueChanged:
            populateQueue();
            return 0;
        case kRenderFinished:
            if (gState->worker != nullptr) {
                CloseHandle(gState->worker);
                gState->worker = nullptr;
            }
            populateQueue();
            setStatus(gState->cancelRequested.load()
                          ? L"Render queue cancelled. Unstarted jobs remain queued."
                          : L"Render queue finished. Completed clips are ready for preview.");
            return 0;
        case kMediaEvent:
            if (static_cast<MFP_EVENT_TYPE>(wParam) == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
                SetWindowTextW(gState->preview, L"Replay selected");
            } else if (FAILED(static_cast<HRESULT>(lParam))) {
                SetWindowTextW(gState->preview, L"Preview selected");
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IdChooseOutput: {
                    const auto path = chooseFolder(window, L"Choose clip output folder");
                    if (path) SetWindowTextW(gState->outputPath, path->c_str());
                    return 0;
                }
                case IdChooseHome: {
                    const auto path = chooseFolder(
                        window,
                        L"Choose ETL user data / fs_homepath (normally Documents\\ETLegacy)");
                    if (path) {
                        SetWindowTextW(gState->homePath, path->c_str());
                        writeIni(L"EtlHomeFolder", path->wstring());
                        WritePrivateProfileStringW(
                            L"ETLegacy",
                            L"UserDataFolder",
                            path->c_str(),
                            gState->settingsPath.c_str());
                    }
                    return 0;
                }
                case IdChooseFfmpeg: {
                    const auto path = chooseFfmpeg(window);
                    if (path) SetWindowTextW(gState->ffmpegPath, path->c_str());
                    return 0;
                }
                case IdResolution:
                    if (HIWORD(wParam) == CBN_SELCHANGE) updateResolutionPreset();
                    return 0;
                case IdStartQueue:
                    startRendering();
                    return 0;
                case IdCancel:
                    cancelRendering();
                    return 0;
                case IdRemove:
                    removeSelected();
                    return 0;
                case IdClearFinished:
                    clearFinished();
                    return 0;
                case IdOpenOutput: {
                    const std::filesystem::path path(getText(gState->outputPath));
                    std::error_code error;
                    std::filesystem::create_directories(path, error);
                    ShellExecuteW(window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                case IdPreview:
                    togglePreview();
                    return 0;
                case IdOpenExternal:
                    openSelectedExternally();
                    return 0;
            }
            break;
        case WM_NOTIFY: {
            const NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->idFrom == IdQueue && header->code == NM_DBLCLK) {
                previewSelected();
                return 0;
            }
            if (header->idFrom == IdQueue && header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    draw->clrTextBk = kPanel;
                    draw->clrText = kText;
                    const std::uint64_t id = static_cast<std::uint64_t>(draw->nmcd.lItemlParam);
                    std::lock_guard<std::mutex> lock(gState->jobsMutex);
                    const auto found = std::find_if(gState->jobs.begin(), gState->jobs.end(),
                        [id](const ClipJob& job) { return job.id == id; });
                    if (found != gState->jobs.end()) {
                        if (found->status == JobStatus::Completed) draw->clrText = kSuccess;
                        if (found->status == JobStatus::Failed || found->status == JobStatus::Cancelled)
                            draw->clrText = kDanger;
                    }
                    return CDRF_DODEFAULT;
                }
            }
            break;
        }
        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = procedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(cls.hInstance, MAKEINTRESOURCEW(101));
    cls.hIconSm = cls.hIcon;
    cls.hbrBackground = nullptr;
    cls.lpszClassName = kClassName;
    registered = RegisterClassExW(&cls) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}

} // namespace

std::size_t updateQueue(
    HWND owner,
    const std::filesystem::path& etlExecutable,
    const std::filesystem::path& settingsPath,
    bool launchAsAdministrator,
    const std::filesystem::path& etlHomeFolder,
    const std::filesystem::path& sourceProfileFolder,
    const std::filesystem::path& startupConfig,
    const std::vector<ClipSource>& sources,
    bool showWindow) {
    if (gState == nullptr) {
        gState = new State();
        gState->owner = owner;
        gState->etlExecutable = etlExecutable;
        gState->settingsPath = settingsPath;
        gState->launchAsAdministrator = launchAsAdministrator;
        gState->nextId = (static_cast<std::uint64_t>(GetTickCount64()) << 16) |
                         static_cast<std::uint64_t>(GetCurrentProcessId());
        loadSettings(sources.empty() ? nullptr : &sources.front());
        if (!etlHomeFolder.empty()) {
            gState->settings.etlHomeFolder = etlHomeFolder;
        }
        gState->settings.sourceProfileFolder = sourceProfileFolder;
        gState->settings.startupConfig = startupConfig;
        if (!registerClass()) {
            delete gState;
            gState = nullptr;
            MessageBoxW(owner, L"The clip exporter window could not be registered.",
                        L"Clip exporter error", MB_OK | MB_ICONERROR);
            return 0;
        }
        const int dpi = dpiFor(owner);
        gState->window = CreateWindowExW(
            WS_EX_APPWINDOW,
            kClassName,
            L"ET: Legacy Frag Finder by ght — Clip exporter",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            MulDiv(1360, dpi, 96),
            MulDiv(820, dpi, 96),
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (gState->window == nullptr) {
            delete gState;
            gState = nullptr;
            return 0;
        }
    } else {
        gState->owner = owner;
        gState->etlExecutable = etlExecutable;
        gState->settingsPath = settingsPath;
        gState->launchAsAdministrator = launchAsAdministrator;
        if (!etlHomeFolder.empty()) {
            gState->settings.etlHomeFolder = etlHomeFolder;
            if (gState->homePath != nullptr) {
                SetWindowTextW(gState->homePath, etlHomeFolder.c_str());
            }
        }
        gState->settings.sourceProfileFolder = sourceProfileFolder;
        gState->settings.startupConfig = startupConfig;
    }
    const std::size_t added = addSources(sources);
    populateQueue();
    if (showWindow) {
        ShowWindow(gState->window, SW_SHOW);
        SetForegroundWindow(gState->window);
    }
    return added;
}

void open(
    HWND owner,
    const std::filesystem::path& etlExecutable,
    const std::filesystem::path& settingsPath,
    bool launchAsAdministrator,
    const std::filesystem::path& etlHomeFolder,
    const std::filesystem::path& sourceProfileFolder,
    const std::filesystem::path& startupConfig,
    const std::vector<ClipSource>& sources) {
    updateQueue(
        owner,
        etlExecutable,
        settingsPath,
        launchAsAdministrator,
        etlHomeFolder,
        sourceProfileFolder,
        startupConfig,
        sources,
        true);
}

std::size_t enqueue(
    HWND owner,
    const std::filesystem::path& etlExecutable,
    const std::filesystem::path& settingsPath,
    bool launchAsAdministrator,
    const std::filesystem::path& etlHomeFolder,
    const std::filesystem::path& sourceProfileFolder,
    const std::filesystem::path& startupConfig,
    const std::vector<ClipSource>& sources) {
    return updateQueue(
        owner,
        etlExecutable,
        settingsPath,
        launchAsAdministrator,
        etlHomeFolder,
        sourceProfileFolder,
        startupConfig,
        sources,
        false);
}

void shutdown() {
    if (gState == nullptr) return;
    gState->cancelRequested.store(true);
    {
        std::lock_guard<std::mutex> processLock(gState->processMutex);
        if (gState->activeProcess != nullptr) cancelProcessGracefully(gState->activeProcess);
    }
    if (gState->worker != nullptr) {
        WaitForSingleObject(gState->worker, 10000);
        CloseHandle(gState->worker);
        gState->worker = nullptr;
    }
    releaseMediaPlayer();
    if (gState->mediaFoundationStarted && gState->mediaShutdown != nullptr) {
        gState->mediaShutdown();
    }
    if (gState->mediaPlayerLibrary != nullptr) FreeLibrary(gState->mediaPlayerLibrary);
    if (gState->mediaFoundationLibrary != nullptr) FreeLibrary(gState->mediaFoundationLibrary);
    if (gState->window != nullptr) DestroyWindow(gState->window);
    for (HFONT font : {gState->font, gState->smallFont, gState->titleFont}) {
        if (font != nullptr) DeleteObject(font);
    }
    for (HBRUSH brush : {gState->backgroundBrush, gState->panelBrush, gState->controlBrush}) {
        if (brush != nullptr) DeleteObject(brush);
    }
    delete gState;
    gState = nullptr;
}

} // namespace etlfrag::clipui

#endif // _WIN32
