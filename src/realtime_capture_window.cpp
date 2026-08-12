// SPDX-License-Identifier: GPL-3.0-or-later
#include "realtime_capture_window.hpp"

#ifdef _WIN32

#include "realtime_capture.hpp"

#include <windows.h>
#include <audioclient.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmdeviceapi.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace etlfrag::captureui {
namespace {

constexpr wchar_t kClassName[] = L"ETLFragFinderRealtimeCapture";
constexpr wchar_t kSafeEtlCommandsDisplay[] =
    L"/r_fullscreen 0\r\n/vid_restart";
constexpr wchar_t kSafeEtlCommandsClipboard[] =
    L"r_fullscreen 0; vid_restart";
constexpr UINT kCaptureStatus = WM_APP + 120;
constexpr UINT kCaptureFinished = WM_APP + 121;
constexpr int kHotkeyId = 1;

constexpr COLORREF kBackground = RGB(14, 17, 21);
constexpr COLORREF kPanel = RGB(24, 29, 35);
constexpr COLORREF kControl = RGB(31, 37, 44);
constexpr COLORREF kControlPressed = RGB(40, 47, 56);
constexpr COLORREF kBorder = RGB(61, 70, 81);
constexpr COLORREF kText = RGB(238, 241, 245);
constexpr COLORREF kMuted = RGB(158, 168, 180);
constexpr COLORREF kAccent = RGB(74, 134, 211);
constexpr COLORREF kDanger = RGB(218, 77, 77);
constexpr COLORREF kSuccess = RGB(104, 199, 139);
constexpr COLORREF kWarning = RGB(242, 157, 65);

constexpr GUID kMmDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C,
    {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
constexpr GUID kPcmSubformat = {
    0x00000001, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
constexpr GUID kFloatSubformat = {
    0x00000003, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

enum ControlId {
    IdOutputPath = 8101,
    IdChooseOutput,
    IdFfmpegPath,
    IdChooseFfmpeg,
    IdDisplay,
    IdFrameRate,
    IdEncoder,
    IdQuality,
    IdPacing,
    IdCursor,
    IdSystemAudio,
    IdStartStop,
    IdOpenOutput,
    IdCopyEtlCommands,
    IdStatus,
};

struct DisplayInfo {
    std::wstring name;
    RECT bounds{};
    bool primary = false;
};

struct CaptureResult {
    bool success = false;
    std::filesystem::path outputPath;
    std::filesystem::path fallbackPath;
    std::wstring detail;
};

struct State {
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND outputPath = nullptr;
    HWND chooseOutput = nullptr;
    HWND ffmpegPath = nullptr;
    HWND chooseFfmpeg = nullptr;
    HWND display = nullptr;
    HWND frameRate = nullptr;
    HWND encoder = nullptr;
    HWND quality = nullptr;
    HWND pacing = nullptr;
    HWND cursor = nullptr;
    HWND systemAudio = nullptr;
    HWND startStop = nullptr;
    HWND openOutput = nullptr;
    HWND copyEtlCommands = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
    HFONT smallFont = nullptr;
    HFONT labelFont = nullptr;
    HFONT titleFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH controlBrush = nullptr;
    int dpi = 96;
    std::filesystem::path settingsPath;
    std::filesystem::path suggestedFfmpeg;
    std::filesystem::path suggestedOutputFolder;
    std::filesystem::path lastOutput;
    std::vector<DisplayInfo> displays;
    bool drawCursor = false;
    bool captureSystemAudio = true;
    bool running = false;
    bool messageBoxOpen = false;
    bool closeAfterFinish = false;
    HANDLE worker = nullptr;
    HANDLE stopEvent = nullptr;
};

State* gState = nullptr;

int showCaptureMessage(
    HWND owner,
    const wchar_t* text,
    const wchar_t* caption,
    UINT type) {
    if (gState == nullptr) return MessageBoxW(owner, text, caption, type);
    const bool previous = gState->messageBoxOpen;
    gState->messageBoxOpen = true;
    const int answer = MessageBoxW(owner, text, caption, type);
    gState->messageBoxOpen = previous;
    return answer;
}

bool copyUnicodeText(HWND owner, const std::wstring& value) {
    if (!OpenClipboard(owner)) return false;
    bool copied = false;
    HGLOBAL memory = nullptr;
    if (EmptyClipboard()) {
        const SIZE_T bytes = (value.size() + 1) * sizeof(wchar_t);
        memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory != nullptr) {
            void* destination = GlobalLock(memory);
            if (destination != nullptr) {
                CopyMemory(destination, value.c_str(), bytes);
                GlobalUnlock(memory);
                if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr) {
                    copied = true;
                    memory = nullptr; // The clipboard owns the handle now.
                }
            }
        }
    }
    if (memory != nullptr) GlobalFree(memory);
    CloseClipboard();
    return copied;
}

bool copySafeEtlCommands(HWND owner, bool showConfirmation) {
    const bool copied = copyUnicodeText(owner, kSafeEtlCommandsClipboard);
    if (showConfirmation) {
        showCaptureMessage(
            owner,
            copied
                ? L"Copied to the clipboard:\r\n\r\n"
                  L"r_fullscreen 0; vid_restart\r\n\r\n"
                  L"Paste this line into the open ET: Legacy console and press Enter."
                : L"The ET: Legacy commands could not be copied to the clipboard.",
            copied ? L"ETL commands copied" : L"Clipboard error",
            MB_OK | (copied ? MB_ICONINFORMATION : MB_ICONERROR));
    }
    return copied;
}

bool confirmFastCaptureSafety(HWND owner) {
    const TASKDIALOG_BUTTON buttons[] = {
        {1001, L"Open Fast Capture\nContinue without copying the commands"},
        {1002, L"Copy commands and open Fast Capture\nCopies a single ETL-console line"},
        {IDCANCEL, L"Cancel"},
    };
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof(dialog);
    dialog.hwndParent = owner;
    dialog.hInstance = GetModuleHandleW(nullptr);
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_SIZE_TO_CONTENT |
                     TDF_USE_COMMAND_LINKS |
                     TDF_EXPANDED_BY_DEFAULT;
    dialog.pszWindowTitle = L"Fast Capture — display safety warning";
    dialog.pszMainIcon = TD_WARNING_ICON;
    dialog.pszMainInstruction =
        L"Use ET: Legacy in windowed mode and keep the display state unchanged.";
    dialog.pszContent =
        L"Before recording, run the two commands below in the ET: Legacy console. "
        L"Alt+Tab, switching windows or displays, entering/leaving fullscreen, or "
        L"changing resolution or refresh rate may invalidate Windows Desktop "
        L"Duplication and immediately interrupt the recording.";
    dialog.pszExpandedInformation = kSafeEtlCommandsDisplay;
    dialog.pszExpandedControlText = L"Show ETL console commands";
    dialog.pszCollapsedControlText = L"Hide ETL console commands";
    dialog.cButtons = static_cast<UINT>(std::size(buttons));
    dialog.pButtons = buttons;
    dialog.nDefaultButton = 1002;
    dialog.pszFooter =
        L"Known FFmpeg symptom: AcquireNextFrame failed: 887a0026 "
        L"(DXGI_ERROR_ACCESS_LOST). The current recording then stops and its "
        L"recoverable MKV/log are kept; Frag Finder never joins it to another clip.";

    int selected = IDCANCEL;
    const HRESULT shown = TaskDialogIndirect(&dialog, &selected, nullptr, nullptr);
    if (SUCCEEDED(shown)) {
        if (selected == 1002 && !copySafeEtlCommands(owner, false)) {
            showCaptureMessage(
                owner,
                L"Fast Capture can still be opened, but the ETL commands could not "
                L"be copied. Use /r_fullscreen 0 followed by /vid_restart manually.",
                L"Clipboard error",
                MB_OK | MB_ICONWARNING);
        }
        return selected == 1001 || selected == 1002;
    }

    const int fallback = MessageBoxW(
        owner,
        L"Fast Capture requires a stable windowed ET: Legacy display.\r\n\r\n"
        L"Run /r_fullscreen 0 and then /vid_restart before recording. Alt+Tab, "
        L"switching windows/displays or changing fullscreen, resolution or refresh "
        L"rate can cause FFmpeg error 887a0026 and stop the recording.\r\n\r\n"
        L"Continue to Fast Capture?",
        L"Fast Capture — display safety warning",
        MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING);
    return fallback == IDYES;
}

int dpiFor(HWND window) {
    using Function = UINT(WINAPI*)(HWND);
    if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
        if (auto function = reinterpret_cast<Function>(GetProcAddress(user, "GetDpiForWindow"))) {
            const int value = static_cast<int>(function(window));
            if (value > 0) return value;
        }
    }
    HDC dc = GetDC(window);
    const int value = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) ReleaseDC(window, dc);
    return value > 0 ? value : 96;
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
        SendMessageW(control, WM_SETFONT,
                     reinterpret_cast<WPARAM>(font != nullptr ? font : gState->font), TRUE);
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
        extendedStyle, className, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, gState->window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    setFont(control, font);
    SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    return control;
}

std::wstring readIni(const wchar_t* key, const std::wstring& fallback = {}) {
    if (gState == nullptr || gState->settingsPath.empty()) return fallback;
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(
        L"RealtimeCapture", key, fallback.c_str(), buffer.data(),
        static_cast<DWORD>(buffer.size()), gState->settingsPath.c_str());
    buffer.resize(length);
    return buffer;
}

void writeIni(const wchar_t* key, const std::wstring& value) {
    if (gState != nullptr && !gState->settingsPath.empty()) {
        WritePrivateProfileStringW(
            L"RealtimeCapture", key, value.c_str(), gState->settingsPath.c_str());
    }
}

int readIniInt(const wchar_t* key, int fallback) {
    const std::wstring value = readIni(key);
    if (value.empty()) return fallback;
    try { return std::stoi(value); } catch (...) { return fallback; }
}

void postStatus(HWND window, const std::wstring& value) {
    auto* message = new std::wstring(value);
    if (!PostMessageW(window, kCaptureStatus, 0, reinterpret_cast<LPARAM>(message))) {
        delete message;
    }
}

void postFinished(HWND window, std::unique_ptr<CaptureResult> result) {
    CaptureResult* raw = result.release();
    if (!PostMessageW(window, kCaptureFinished, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
}

BOOL CALLBACK enumerateMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* displays = reinterpret_cast<std::vector<DisplayInfo>*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        DisplayInfo display;
        display.name = info.szDevice;
        display.bounds = info.rcMonitor;
        display.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        displays->push_back(std::move(display));
    }
    return TRUE;
}

std::vector<DisplayInfo> enumerateDisplays() {
    std::vector<DisplayInfo> displays;
    EnumDisplayMonitors(nullptr, nullptr, enumerateMonitor, reinterpret_cast<LPARAM>(&displays));
    if (displays.empty()) {
        DisplayInfo fallback;
        fallback.name = L"Primary display";
        fallback.bounds.right = GetSystemMetrics(SM_CXSCREEN);
        fallback.bounds.bottom = GetSystemMetrics(SM_CYSCREEN);
        fallback.primary = true;
        displays.push_back(std::move(fallback));
    }
    return displays;
}

std::optional<std::filesystem::path> chooseFolder(HWND owner) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose real-time capture output folder");
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
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
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter = L"ffmpeg executable (ffmpeg.exe)\0ffmpeg.exe\0Applications (*.exe)\0*.exe\0\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = L"Locate ffmpeg.exe";
    if (GetOpenFileNameW(&dialog)) return std::filesystem::path(path);
    return std::nullopt;
}

std::filesystem::path moduleFolder() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path findFfmpeg(const std::filesystem::path& suggested) {
    std::error_code error;
    std::vector<std::filesystem::path> candidates;
    if (!suggested.empty()) candidates.push_back(suggested);
    const std::wstring clipSetting = [] {
        if (gState == nullptr || gState->settingsPath.empty()) return std::wstring{};
        std::wstring value(32768, L'\0');
        const DWORD length = GetPrivateProfileStringW(
            L"ClipExport", L"FfmpegExecutable", L"", value.data(),
            static_cast<DWORD>(value.size()), gState->settingsPath.c_str());
        value.resize(length);
        return value;
    }();
    if (!clipSetting.empty()) candidates.emplace_back(clipSetting);
    candidates.push_back(moduleFolder() / L"ffmpeg.exe");
    candidates.push_back(moduleFolder() / L"ffmpeg" / L"bin" / L"ffmpeg.exe");
    for (const auto& candidate : candidates) {
        error.clear();
        if (std::filesystem::is_regular_file(candidate, error) && !error) return candidate;
    }
    return {};
}

std::filesystem::path defaultOutputFolder() {
    if (gState != nullptr && !gState->suggestedOutputFolder.empty()) {
        return gState->suggestedOutputFolder / L"realtime";
    }
    PWSTR documents = nullptr;
    std::filesystem::path result = moduleFolder() / L"captures";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
        result = std::filesystem::path(documents) / L"ETLegacy" /
                 L"fragfinder-clips" / L"realtime";
        CoTaskMemFree(documents);
    }
    return result;
}

std::wstring timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t value[32]{};
    swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    return value;
}

std::filesystem::path uniqueOutputPath(
    const std::filesystem::path& folder,
    const std::wstring& extension) {
    const std::wstring base = L"ETL-fast-capture-" + timestamp();
    std::filesystem::path candidate = folder / (base + extension);
    std::error_code error;
    for (int suffix = 2; std::filesystem::exists(candidate, error) && suffix < 1000; ++suffix) {
        error.clear();
        candidate = folder / (base + L"-" + std::to_wstring(suffix) + extension);
    }
    return candidate;
}

std::wstring quoteArgument(const std::filesystem::path& path) {
    const std::wstring value = path.wstring();
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(character);
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            result.push_back(character);
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

struct ChildProcess {
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE stdinWrite = nullptr;
};

void closeChildProcess(ChildProcess& child) {
    if (child.stdinWrite != nullptr) CloseHandle(child.stdinWrite);
    if (child.thread != nullptr) CloseHandle(child.thread);
    if (child.process != nullptr) CloseHandle(child.process);
    child = {};
}

bool launchProcess(
    const std::filesystem::path& executable,
    const std::wstring& arguments,
    const std::filesystem::path& logPath,
    bool appendLog,
    ChildProcess& child,
    std::wstring& error) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE stdinRead = nullptr;
    HANDLE stdinWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &security, 0)) {
        error = L"Could not create the FFmpeg control pipe.";
        return false;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);

    HANDLE log = CreateFileW(
        logPath.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
        appendLog ? OPEN_ALWAYS : CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite);
        error = L"Could not create the real-time capture log.";
        return false;
    }
    if (appendLog) SetFilePointer(log, 0, nullptr, FILE_END);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdinRead;
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION process{};
    std::wstring command = quoteArgument(executable) + L" " + arguments;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const std::wstring workingFolder = executable.parent_path().wstring();
    const BOOL created = CreateProcessW(
        executable.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,
        workingFolder.empty() ? nullptr : workingFolder.c_str(),
        &startup, &process);
    CloseHandle(stdinRead);
    CloseHandle(log);
    if (!created) {
        CloseHandle(stdinWrite);
        error = L"FFmpeg could not be started (Windows error " +
                std::to_wstring(GetLastError()) + L").";
        return false;
    }
    child.process = process.hProcess;
    child.thread = process.hThread;
    child.stdinWrite = stdinWrite;
    return true;
}

bool runProcess(
    const std::filesystem::path& executable,
    const std::wstring& arguments,
    const std::filesystem::path& logPath,
    bool appendLog,
    DWORD timeoutMs,
    DWORD& exitCode) {
    ChildProcess child;
    std::wstring error;
    if (!launchProcess(executable, arguments, logPath, appendLog, child, error)) return false;
    const DWORD wait = WaitForSingleObject(child.process, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(child.process, 1);
        WaitForSingleObject(child.process, 3000);
    }
    exitCode = 1;
    GetExitCodeProcess(child.process, &exitCode);
    closeChildProcess(child);
    return wait == WAIT_OBJECT_0 && exitCode == 0;
}

class WasapiLoopback {
public:
    ~WasapiLoopback() { close(); }

    bool initialize(std::wstring& error) {
        HRESULT result = CoCreateInstance(
            kMmDeviceEnumerator, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
        if (FAILED(result)) {
            error = L"WASAPI could not enumerate the default playback device.";
            return false;
        }
        result = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        if (FAILED(result)) {
            error = L"No default Windows playback device was found.";
            return false;
        }
        result = device_->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&client_));
        if (FAILED(result)) {
            error = L"The default playback device does not expose WASAPI loopback.";
            return false;
        }
        result = client_->GetMixFormat(&format_);
        if (FAILED(result) || format_ == nullptr) {
            error = L"Windows could not provide the playback-device audio format.";
            return false;
        }
        if (!describeFormat(error)) return false;
        result = client_->Initialize(
            AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
            0, 0, format_, nullptr);
        if (FAILED(result)) {
            error = L"WASAPI loopback initialization failed (0x" + hex(result) + L").";
            return false;
        }
        result = client_->GetService(
            __uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture_));
        if (FAILED(result)) {
            error = L"WASAPI did not provide an audio capture client.";
            return false;
        }
        return true;
    }

    bool start(std::wstring& error) {
        const HRESULT result = client_->Start();
        if (FAILED(result)) {
            error = L"Windows could not start system-audio capture (0x" + hex(result) + L").";
            return false;
        }
        started_ = true;
        return true;
    }

    void stop() {
        if (started_ && client_ != nullptr) client_->Stop();
        started_ = false;
    }

    IAudioCaptureClient* capture() const { return capture_; }
    UINT32 blockAlign() const { return format_ != nullptr ? format_->nBlockAlign : 0; }
    const RealtimeAudioInput& input() const { return input_; }
    void shutdown() { close(); }

private:
    template <typename T>
    static void release(T*& value) {
        if (value != nullptr) value->Release();
        value = nullptr;
    }

    static std::wstring hex(HRESULT value) {
        std::wostringstream output;
        output << std::hex << std::uppercase << static_cast<unsigned long>(value);
        return output.str();
    }

    bool describeFormat(std::wstring& error) {
        WORD tag = format_->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE &&
            format_->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            const GUID subtype = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format_)->SubFormat;
            if (IsEqualGUID(subtype, kFloatSubformat)) tag = WAVE_FORMAT_IEEE_FLOAT;
            else if (IsEqualGUID(subtype, kPcmSubformat)) tag = WAVE_FORMAT_PCM;
        }
        if (tag == WAVE_FORMAT_IEEE_FLOAT && format_->wBitsPerSample == 32) {
            input_.sampleFormat = L"f32le";
        } else if (tag == WAVE_FORMAT_IEEE_FLOAT && format_->wBitsPerSample == 64) {
            input_.sampleFormat = L"f64le";
        } else if (tag == WAVE_FORMAT_PCM && format_->wBitsPerSample == 8) {
            input_.sampleFormat = L"u8";
        } else if (tag == WAVE_FORMAT_PCM && format_->wBitsPerSample == 16) {
            input_.sampleFormat = L"s16le";
        } else if (tag == WAVE_FORMAT_PCM && format_->wBitsPerSample == 24) {
            input_.sampleFormat = L"s24le";
        } else if (tag == WAVE_FORMAT_PCM && format_->wBitsPerSample == 32) {
            input_.sampleFormat = L"s32le";
        } else {
            error = L"The playback device uses an unsupported WASAPI sample format (" +
                    std::to_wstring(format_->wBitsPerSample) + L"-bit).";
            return false;
        }
        input_.sampleRate = static_cast<int>(format_->nSamplesPerSec);
        input_.channels = static_cast<int>(format_->nChannels);
        return input_.sampleRate > 0 && input_.channels > 0;
    }

    void close() {
        stop();
        release(capture_);
        release(client_);
        release(device_);
        release(enumerator_);
        if (format_ != nullptr) CoTaskMemFree(format_);
        format_ = nullptr;
    }

    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioClient* client_ = nullptr;
    IAudioCaptureClient* capture_ = nullptr;
    WAVEFORMATEX* format_ = nullptr;
    RealtimeAudioInput input_;
    bool started_ = false;
};

bool connectAudioPipe(
    HANDLE pipe, HANDLE process, HANDLE stopEvent, std::wstring& error) {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        error = L"Could not create the audio-pipe event.";
        return false;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    if (!connected) {
        const DWORD pipeError = GetLastError();
        if (pipeError == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        } else if (pipeError == ERROR_IO_PENDING) {
            HANDLE waits[] = {event, process, stopEvent};
            const DWORD wait = WaitForMultipleObjects(3, waits, FALSE, 15000);
            if (wait == WAIT_OBJECT_0) {
                DWORD transferred = 0;
                connected = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
            } else {
                CancelIoEx(pipe, &overlapped);
                DWORD transferred = 0;
                GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
                connected = FALSE;
            }
            if (wait == WAIT_OBJECT_0 + 1) {
                error = L"FFmpeg exited before connecting to the system-audio pipe.";
            } else if (wait == WAIT_TIMEOUT) {
                error = L"FFmpeg did not connect to the system-audio pipe within 15 seconds.";
            } else if (wait == WAIT_OBJECT_0 + 2) {
                error = L"Capture was cancelled while starting.";
            }
        } else {
            error = L"Windows could not connect the system-audio pipe.";
        }
    }
    CloseHandle(event);
    return connected == TRUE;
}

bool writeAudioPacket(
    HANDLE pipe,
    const BYTE* data,
    DWORD size,
    HANDLE process,
    HANDLE stopEvent,
    HANDLE writeEvent) {
    DWORD offset = 0;
    while (offset < size) {
        ResetEvent(writeEvent);
        OVERLAPPED overlapped{};
        overlapped.hEvent = writeEvent;
        DWORD written = 0;
        const BOOL completed = WriteFile(
            pipe, data + offset, size - offset, nullptr, &overlapped);
        if (!completed) {
            const DWORD ioError = GetLastError();
            if (ioError != ERROR_IO_PENDING) return false;
            HANDLE waits[] = {writeEvent, process, stopEvent};
            const DWORD wait = WaitForMultipleObjects(3, waits, FALSE, 5000);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &overlapped);
                GetOverlappedResult(pipe, &overlapped, &written, TRUE);
                return false;
            }
            if (!GetOverlappedResult(pipe, &overlapped, &written, FALSE)) return false;
        } else if (!GetOverlappedResult(pipe, &overlapped, &written, FALSE)) {
            return false;
        }
        if (written == 0) return false;
        offset += written;
    }
    return true;
}

bool probeEncoder(
    const RealtimeCaptureSettings& settings,
    RealtimeCaptureEncoder encoder,
    const std::filesystem::path& logPath) {
    DWORD exitCode = 1;
    return runProcess(
        settings.ffmpegExecutable,
        buildRealtimeEncoderProbeArguments(encoder),
        logPath,
        true,
        15000,
        exitCode);
}

std::optional<RealtimeCaptureEncoder> resolveEncoder(
    const RealtimeCaptureSettings& settings,
    const std::filesystem::path& logPath,
    HWND window) {
    std::vector<RealtimeCaptureEncoder> candidates;
    if (settings.encoder == RealtimeCaptureEncoder::Auto) {
        candidates = {
            RealtimeCaptureEncoder::NvidiaNvenc,
            RealtimeCaptureEncoder::AmdAmf,
            RealtimeCaptureEncoder::IntelQsv,
            RealtimeCaptureEncoder::SoftwareX264,
        };
    } else {
        candidates.push_back(settings.encoder);
    }
    for (const RealtimeCaptureEncoder candidate : candidates) {
        postStatus(window, L"Testing " + realtimeCaptureEncoderName(candidate) + L"...");
        if (probeEncoder(settings, candidate, logPath)) return candidate;
    }
    return std::nullopt;
}

struct CaptureProgress {
    long long frames = 0;
    long long outTimeUs = 0;
    long long duplicateFrames = 0;
    long long droppedFrames = 0;
};

std::optional<CaptureProgress> readCaptureProgress(
    const std::filesystem::path& logPath) {
    std::ifstream input(logPath, std::ios::binary);
    if (!input) return std::nullopt;
    CaptureProgress progress;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        try {
            if (key == "frame") progress.frames = std::stoll(value);
            else if (key == "out_time_us") progress.outTimeUs = std::stoll(value);
            else if (key == "dup_frames") progress.duplicateFrames = std::stoll(value);
            else if (key == "drop_frames") progress.droppedFrames = std::stoll(value);
        } catch (...) {
        }
    }
    if (progress.frames <= 0 || progress.outTimeUs <= 0) return std::nullopt;
    return progress;
}

std::wstring captureProgressSummary(
    const std::filesystem::path& logPath,
    int targetFps,
    RealtimeCapturePacing pacing) {
    const auto progress = readCaptureProgress(logPath);
    if (!progress.has_value()) return {};
    const double outputFps =
        static_cast<double>(progress->frames) * 1000000.0 /
        static_cast<double>(progress->outTimeUs);
    const long long sourceFrames = std::max<long long>(
        0, progress->frames - progress->duplicateFrames);
    const double sourceFps =
        static_cast<double>(sourceFrames) * 1000000.0 /
        static_cast<double>(progress->outTimeUs);
    std::wostringstream output;
    output << std::fixed << std::setprecision(1);
    if (pacing == RealtimeCapturePacing::SmoothCfr) {
        output << L"output " << outputFps << L" FPS CFR / target " << targetFps
               << L" • source approx. " << sourceFps << L" FPS"
               << L" • " << progress->frames << L" output frames";
    } else {
        output << L"actual " << outputFps << L" FPS VFR / target " << targetFps
               << L" • " << progress->frames << L" new desktop frames";
    }
    if (progress->droppedFrames > 0) {
        output << L" • " << progress->droppedFrames << L" dropped";
    }
    if (progress->duplicateFrames > 0) {
        output << L" • " << progress->duplicateFrames << L" paced duplicates";
    }
    return output.str();
}

bool captureLogHasDesktopDuplicationLoss(
    const std::filesystem::path& logPath) {
    std::ifstream input(logPath, std::ios::binary);
    if (!input) return false;
    const std::string text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    return isRealtimeDesktopDuplicationLoss(text);
}

struct CaptureRequest {
    HWND window = nullptr;
    HANDLE stopEvent = nullptr;
    RealtimeCaptureSettings settings;
    std::filesystem::path temporaryMkv;
    std::filesystem::path finalMp4;
    std::filesystem::path logPath;
};

unsigned __stdcall captureWorker(void* parameter) {
    std::unique_ptr<CaptureRequest> request(
        reinterpret_cast<CaptureRequest*>(parameter));
    auto result = std::make_unique<CaptureResult>();
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        result->detail = L"Windows COM initialization failed; capture cannot start.";
        postFinished(request->window, std::move(result));
        return 0;
    }

    const auto encoder = resolveEncoder(
        request->settings, request->logPath, request->window);
    if (!encoder.has_value()) {
        result->detail = request->settings.encoder == RealtimeCaptureEncoder::Auto
            ? L"No supported H.264 encoder was available. Check FFmpeg and your GPU driver."
            : realtimeCaptureEncoderName(request->settings.encoder) +
                  L" is not available on this computer.";
        if (uninitializeCom) CoUninitialize();
        postFinished(request->window, std::move(result));
        return 0;
    }

    WasapiLoopback loopback;
    std::optional<RealtimeAudioInput> audio;
    std::wstring error;
    if (request->settings.captureSystemAudio) {
        postStatus(request->window, L"Opening Windows system audio through WASAPI loopback...");
        if (!loopback.initialize(error)) {
            result->detail = error;
            loopback.shutdown();
            if (uninitializeCom) CoUninitialize();
            postFinished(request->window, std::move(result));
            return 0;
        }
        audio = loopback.input();
        audio->pipeName = L"\\\\.\\pipe\\ETLFragFinderAudio-" +
                          std::to_wstring(GetCurrentProcessId()) + L"-" +
                          std::to_wstring(GetTickCount64());
    }

    HANDLE audioPipe = INVALID_HANDLE_VALUE;
    if (audio.has_value()) {
        audioPipe = CreateNamedPipeW(
            audio->pipeName.c_str(),
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 1024 * 1024, 1024 * 1024, 0, nullptr);
        if (audioPipe == INVALID_HANDLE_VALUE) {
            result->detail = L"Windows could not create the system-audio pipe.";
            loopback.shutdown();
            if (uninitializeCom) CoUninitialize();
            postFinished(request->window, std::move(result));
            return 0;
        }
    }

    postStatus(
        request->window,
        L"Starting " + std::to_wstring(request->settings.frameRate) +
        L" FPS capture with " + realtimeCaptureEncoderName(*encoder) + L"...");
    ChildProcess ffmpeg;
    const std::wstring captureArguments = buildRealtimeCaptureArguments(
        request->settings, *encoder, audio, request->temporaryMkv);
    if (!launchProcess(
            request->settings.ffmpegExecutable,
            captureArguments,
            request->logPath,
            true,
            ffmpeg,
            error)) {
        if (audioPipe != INVALID_HANDLE_VALUE) CloseHandle(audioPipe);
        result->detail = error;
        loopback.shutdown();
        if (uninitializeCom) CoUninitialize();
        postFinished(request->window, std::move(result));
        return 0;
    }

    bool captureReady = true;
    if (audio.has_value()) {
        captureReady = connectAudioPipe(
            audioPipe, ffmpeg.process, request->stopEvent, error);
        if (captureReady) captureReady = loopback.start(error);
    }
    if (!captureReady) {
        const char quit[] = "q\n";
        DWORD ignored = 0;
        WriteFile(ffmpeg.stdinWrite, quit, sizeof(quit) - 1, &ignored, nullptr);
        if (audioPipe != INVALID_HANDLE_VALUE) CloseHandle(audioPipe);
        WaitForSingleObject(ffmpeg.process, 3000);
        closeChildProcess(ffmpeg);
        result->detail = error;
        loopback.shutdown();
        if (uninitializeCom) CoUninitialize();
        postFinished(request->window, std::move(result));
        return 0;
    }

    postStatus(
        request->window,
        L"RECORDING • target " + std::to_wstring(request->settings.frameRate) +
        L" FPS • " + realtimeCapturePacingName(request->settings.pacing) +
        L" • " + realtimeCaptureEncoderName(*encoder) + L" • F9 stops and saves");
    HANDLE writeEvent = audio.has_value()
        ? CreateEventW(nullptr, TRUE, FALSE, nullptr)
        : nullptr;
    if (audio.has_value() && writeEvent == nullptr) {
        error = L"Windows could not create the system-audio write event.";
    }
    std::vector<BYTE> silence;
    bool audioWriteFailed = audio.has_value() && writeEvent == nullptr;
    bool processExited = false;
    while (!audioWriteFailed &&
           WaitForSingleObject(request->stopEvent, 0) != WAIT_OBJECT_0) {
        if (WaitForSingleObject(ffmpeg.process, 0) == WAIT_OBJECT_0) {
            processExited = true;
            break;
        }
        if (!audio.has_value()) {
            HANDLE waits[] = {ffmpeg.process, request->stopEvent};
            const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 100);
            if (wait == WAIT_OBJECT_0) processExited = true;
            if (wait != WAIT_TIMEOUT) break;
            continue;
        }

        UINT32 packetFrames = 0;
        HRESULT packetResult = loopback.capture()->GetNextPacketSize(&packetFrames);
        if (FAILED(packetResult)) {
            audioWriteFailed = true;
            error = L"WASAPI stopped delivering system audio.";
            break;
        }
        while (packetFrames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            packetResult = loopback.capture()->GetBuffer(
                &data, &frames, &flags, nullptr, nullptr);
            if (FAILED(packetResult)) {
                audioWriteFailed = true;
                error = L"WASAPI system-audio buffer failed.";
                break;
            }
            const DWORD bytes = frames * loopback.blockAlign();
            const BYTE* output = data;
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || output == nullptr) {
                silence.assign(bytes, 0);
                output = silence.data();
            }
            const bool written = writeAudioPacket(
                audioPipe, output, bytes,
                ffmpeg.process, request->stopEvent, writeEvent);
            loopback.capture()->ReleaseBuffer(frames);
            if (!written) {
                if (WaitForSingleObject(request->stopEvent, 0) != WAIT_OBJECT_0) {
                    audioWriteFailed = true;
                    error = L"FFmpeg stopped accepting system audio.";
                }
                break;
            }
            packetResult = loopback.capture()->GetNextPacketSize(&packetFrames);
            if (FAILED(packetResult)) {
                audioWriteFailed = true;
                error = L"WASAPI system-audio capture was interrupted.";
                break;
            }
        }
        if (audioWriteFailed) break;
        WaitForSingleObject(request->stopEvent, 2);
    }

    const bool userStopped =
        WaitForSingleObject(request->stopEvent, 0) == WAIT_OBJECT_0;
    if (userStopped && !processExited) {
        postStatus(request->window, L"Finalizing the high-FPS recording...");
        const char quit[] = "q\n";
        DWORD ignored = 0;
        WriteFile(ffmpeg.stdinWrite, quit, sizeof(quit) - 1, &ignored, nullptr);
    }
    loopback.stop();
    if (writeEvent != nullptr) CloseHandle(writeEvent);
    if (audioPipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(audioPipe, nullptr);
        DisconnectNamedPipe(audioPipe);
        CloseHandle(audioPipe);
    }

    DWORD wait = WaitForSingleObject(ffmpeg.process, 20000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(ffmpeg.process, 1);
        WaitForSingleObject(ffmpeg.process, 3000);
    }
    DWORD captureExitCode = 1;
    GetExitCodeProcess(ffmpeg.process, &captureExitCode);
    closeChildProcess(ffmpeg);

    std::error_code fileError;
    const bool hasTemporaryVideo =
        std::filesystem::is_regular_file(request->temporaryMkv, fileError) &&
        !fileError && std::filesystem::file_size(request->temporaryMkv, fileError) > 0 &&
        !fileError;
    const bool desktopDuplicationLost =
        captureLogHasDesktopDuplicationLoss(request->logPath);
    if (!hasTemporaryVideo) {
        result->detail = desktopDuplicationLost
            ? L"Fast Capture stopped because Windows invalidated Desktop Duplication "
              L"(FFmpeg error 887a0026). Alt+Tab, switching windows/displays, or "
              L"changing fullscreen, resolution or refresh rate can trigger this. "
              L"Run /r_fullscreen 0 followed by /vid_restart before recording again."
            : !error.empty()
                  ? error
                  : L"FFmpeg did not produce a recording. Open the capture log for details.";
    } else if (!userStopped || captureExitCode != 0 || audioWriteFailed) {
        result->fallbackPath = request->temporaryMkv;
        if (desktopDuplicationLost) {
            result->detail =
                L"Fast Capture stopped because Windows invalidated Desktop Duplication "
                L"(FFmpeg error 887a0026). Alt+Tab, switching windows/displays, or "
                L"changing fullscreen, resolution or refresh rate can trigger this. "
                L"Run /r_fullscreen 0 followed by /vid_restart before recording again. "
                L"Frag Finder did not resume or join this recording to another clip.";
        } else if (!error.empty()) {
            result->detail = error;
        } else if (!userStopped) {
            result->detail = L"FFmpeg stopped before the user ended the recording";
            if (captureExitCode != 0) {
                result->detail += L" (exit code " +
                                  std::to_wstring(captureExitCode) + L")";
            }
            result->detail += L".";
        } else {
            result->detail = L"FFmpeg could not finalize the active recording";
            if (captureExitCode != 0) {
                result->detail += L" (exit code " +
                                  std::to_wstring(captureExitCode) + L")";
            }
            result->detail += L".";
        }
        result->detail += L" The recoverable MKV and capture log were kept.";
    } else {
        postStatus(request->window, L"Creating the final MP4 without re-encoding...");
        DWORD remuxExitCode = 1;
        const bool remuxed = runProcess(
            request->settings.ffmpegExecutable,
            buildRealtimeRemuxArguments(request->temporaryMkv, request->finalMp4),
            request->logPath,
            true,
            120000,
            remuxExitCode);
        fileError.clear();
        const bool finalExists =
            remuxed && std::filesystem::is_regular_file(request->finalMp4, fileError) &&
            !fileError && std::filesystem::file_size(request->finalMp4, fileError) > 0;
        if (finalExists) {
            result->success = true;
            result->outputPath = request->finalMp4;
            result->detail = realtimeCaptureEncoderName(*encoder);
            const std::wstring progress = captureProgressSummary(
                request->logPath,
                request->settings.frameRate,
                request->settings.pacing);
            if (!progress.empty()) result->detail += L" • " + progress;
            fileError.clear();
            std::filesystem::remove(request->temporaryMkv, fileError);
        } else {
            result->fallbackPath = request->temporaryMkv;
            result->detail =
                L"The MP4 remux failed, but the recoverable MKV recording was kept.";
        }
    }

    loopback.shutdown();
    if (uninitializeCom) CoUninitialize();
    postFinished(request->window, std::move(result));
    return 0;
}

RealtimeCaptureEncoder selectedEncoder() {
    const int index = static_cast<int>(
        SendMessageW(gState->encoder, CB_GETCURSEL, 0, 0));
    return static_cast<RealtimeCaptureEncoder>(std::clamp(index, 0, 4));
}

RealtimeCaptureQuality selectedQuality() {
    const int index = static_cast<int>(
        SendMessageW(gState->quality, CB_GETCURSEL, 0, 0));
    return static_cast<RealtimeCaptureQuality>(std::clamp(index, 0, 2));
}

RealtimeCapturePacing selectedPacing() {
    const int index = static_cast<int>(
        SendMessageW(gState->pacing, CB_GETCURSEL, 0, 0));
    return static_cast<RealtimeCapturePacing>(std::clamp(index, 0, 1));
}

int selectedFrameRate() {
    const std::wstring text = getText(gState->frameRate);
    try {
        return std::stoi(text);
    } catch (...) {
        return 250;
    }
}

void saveSettings(const RealtimeCaptureSettings& settings) {
    writeIni(L"OutputFolder", settings.outputFolder.wstring());
    writeIni(L"FfmpegExecutable", settings.ffmpegExecutable.wstring());
    writeIni(L"DisplayIndex", std::to_wstring(settings.displayIndex));
    writeIni(L"FrameRate", std::to_wstring(settings.frameRate));
    writeIni(L"Encoder", std::to_wstring(static_cast<int>(settings.encoder)));
    writeIni(L"Quality", std::to_wstring(static_cast<int>(settings.quality)));
    writeIni(L"Pacing", std::to_wstring(static_cast<int>(settings.pacing)));
    writeIni(L"DrawCursor", settings.drawCursor ? L"1" : L"0");
    writeIni(L"SystemAudio", settings.captureSystemAudio ? L"1" : L"0");
}

RealtimeCaptureSettings settingsFromControls() {
    RealtimeCaptureSettings settings;
    settings.outputFolder = std::filesystem::path(getText(gState->outputPath));
    settings.ffmpegExecutable = std::filesystem::path(getText(gState->ffmpegPath));
    settings.displayIndex = std::max(
        0, static_cast<int>(SendMessageW(gState->display, CB_GETCURSEL, 0, 0)));
    settings.frameRate = selectedFrameRate();
    settings.encoder = selectedEncoder();
    settings.quality = selectedQuality();
    settings.pacing = selectedPacing();
    settings.drawCursor = gState->drawCursor;
    settings.captureSystemAudio = gState->captureSystemAudio;
    return settings;
}

void setRunningUi(bool running) {
    gState->running = running;
    SetWindowTextW(
        gState->startStop,
        running ? L"Stop and save  (F9)" : L"Start recording  (F9)");
    for (HWND control : {
             gState->outputPath,
             gState->chooseOutput,
             gState->ffmpegPath,
             gState->chooseFfmpeg,
             gState->display,
             gState->frameRate,
             gState->encoder,
             gState->quality,
             gState->pacing,
             gState->cursor,
             gState->systemAudio,
             gState->copyEtlCommands}) {
        EnableWindow(control, !running);
    }
    InvalidateRect(gState->startStop, nullptr, TRUE);
}

void startCapture() {
    if (gState == nullptr || gState->running || gState->messageBoxOpen) return;
    RealtimeCaptureSettings settings = settingsFromControls();
    if (const auto validation = validateRealtimeCaptureSettings(settings)) {
        showCaptureMessage(
            gState->window, validation->c_str(), L"Fast capture",
            MB_OK | MB_ICONWARNING);
        return;
    }
    if (settings.frameRate >= 250 &&
        settings.encoder == RealtimeCaptureEncoder::SoftwareX264) {
        const int answer = showCaptureMessage(
            gState->window,
            L"Software x264 is unlikely to sustain 250+ FPS at 1080p. "
            L"Use Auto or a hardware encoder unless this is intentional. Continue?",
            L"High-FPS capture",
            MB_YESNO | MB_ICONWARNING);
        if (answer != IDYES) return;
    }
    std::error_code error;
    std::filesystem::create_directories(settings.outputFolder, error);
    if (error) {
        showCaptureMessage(
            gState->window, L"The output folder could not be created.",
            L"Fast capture", MB_OK | MB_ICONERROR);
        return;
    }
    saveSettings(settings);
    auto request = std::make_unique<CaptureRequest>();
    request->window = gState->window;
    request->stopEvent = gState->stopEvent;
    request->settings = settings;
    request->finalMp4 = uniqueOutputPath(settings.outputFolder, L".mp4");
    request->temporaryMkv = request->finalMp4;
    request->temporaryMkv.replace_extension(L".recording.mkv");
    request->logPath = request->finalMp4;
    request->logPath.replace_extension(L".capture.log.txt");
    ResetEvent(gState->stopEvent);
    setRunningUi(true);
    SetWindowTextW(gState->status, L"Preparing high-FPS capture...");
    unsigned threadId = 0;
    gState->worker = reinterpret_cast<HANDLE>(_beginthreadex(
        nullptr, 0, captureWorker, request.release(), 0, &threadId));
    if (gState->worker == nullptr) {
        setRunningUi(false);
        showCaptureMessage(
            gState->window,
            L"The real-time capture worker could not be started.",
            L"Fast capture", MB_OK | MB_ICONERROR);
    }
}

void stopCapture() {
    if (gState != nullptr && gState->running) {
        SetWindowTextW(gState->status, L"Stopping and finalizing the recording...");
        SetEvent(gState->stopEvent);
        EnableWindow(gState->startStop, FALSE);
    }
}

void toggleCapture() {
    if (gState == nullptr || gState->messageBoxOpen) return;
    if (gState->running) stopCapture();
    else startCapture();
}

void fillSolid(HDC dc, const RECT& rectangle, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rectangle, brush);
    DeleteObject(brush);
}

void drawOwnerButton(const DRAWITEMSTRUCT& item) {
    const int id = GetDlgCtrlID(item.hwndItem);
    const bool checkbox = id == IdCursor || id == IdSystemAudio;
    const bool checked = id == IdCursor ? gState->drawCursor : gState->captureSystemAudio;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    RECT rectangle = item.rcItem;
    fillSolid(item.hDC, rectangle, kPanel);
    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? kMuted : kText);
    SelectObject(item.hDC, gState->labelFont);
    if (checkbox) {
        RECT box{rectangle.left + scaled(2), rectangle.top + scaled(6),
                 rectangle.left + scaled(20), rectangle.top + scaled(24)};
        HBRUSH brush = CreateSolidBrush(checked ? kAccent : kControl);
        HPEN pen = CreatePen(PS_SOLID, 1, checked ? kAccent : kBorder);
        const HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
        const HGDIOBJ oldPen = SelectObject(item.hDC, pen);
        Rectangle(item.hDC, box.left, box.top, box.right, box.bottom);
        SelectObject(item.hDC, oldPen);
        SelectObject(item.hDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        if (checked) {
            SetTextColor(item.hDC, RGB(255, 255, 255));
            RECT tick = box;
            DrawTextW(item.hDC, L"✓", -1, &tick,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        RECT label = rectangle;
        label.left += scaled(28);
        SetTextColor(item.hDC, disabled ? kMuted : kText);
        DrawTextW(item.hDC, text, -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        COLORREF color = pressed ? kControlPressed : kControl;
        if (id == IdStartStop) {
            color = gState->running
                ? kDanger
                : (pressed ? RGB(57, 108, 174) : kAccent);
        }
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        const HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
        const HGDIOBJ oldPen = SelectObject(item.hDC, pen);
        RoundRect(item.hDC, rectangle.left, rectangle.top,
                  rectangle.right, rectangle.bottom, scaled(5), scaled(5));
        SelectObject(item.hDC, oldPen);
        SelectObject(item.hDC, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        DrawTextW(item.hDC, text, -1, &rectangle,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void recreateFonts() {
    for (HFONT font : {
             gState->font, gState->smallFont,
             gState->labelFont, gState->titleFont}) {
        if (font != nullptr) DeleteObject(font);
    }
    gState->font = CreateFontW(
        -scaled(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gState->smallFont = CreateFontW(
        -scaled(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gState->labelFont = CreateFontW(
        -scaled(14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
    gState->titleFont = CreateFontW(
        -scaled(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
}

void layout(int width, int height) {
    const int margin = scaled(24);
    const int gap = scaled(12);
    const int labelHeight = scaled(18);
    const int editHeight = scaled(34);
    const int buttonWidth = scaled(150);
    int y = scaled(112);
    MoveWindow(gState->outputPath, margin, y,
               width - margin * 2 - buttonWidth - gap, editHeight, TRUE);
    MoveWindow(gState->chooseOutput, width - margin - buttonWidth, y,
               buttonWidth, editHeight, TRUE);
    y += scaled(52);
    MoveWindow(gState->ffmpegPath, margin, y,
               width - margin * 2 - buttonWidth - gap, editHeight, TRUE);
    MoveWindow(gState->chooseFfmpeg, width - margin - buttonWidth, y,
               buttonWidth, editHeight, TRUE);

    y += scaled(76);
    const int fieldWidth = (width - margin * 2 - gap * 3) / 4;
    MoveWindow(gState->display, margin, y + labelHeight + scaled(5),
               fieldWidth, scaled(260), TRUE);
    MoveWindow(gState->frameRate, margin + fieldWidth + gap,
               y + labelHeight + scaled(5), fieldWidth, scaled(280), TRUE);
    MoveWindow(gState->encoder, margin + (fieldWidth + gap) * 2,
               y + labelHeight + scaled(5), fieldWidth, scaled(260), TRUE);
    MoveWindow(gState->quality, margin + (fieldWidth + gap) * 3,
               y + labelHeight + scaled(5), fieldWidth, scaled(200), TRUE);

    y += scaled(86);
    MoveWindow(gState->pacing, margin, y + labelHeight + scaled(5),
               scaled(330), scaled(180), TRUE);
    MoveWindow(gState->cursor, margin + scaled(354), y + labelHeight + scaled(5),
               scaled(190), scaled(32), TRUE);
    MoveWindow(gState->systemAudio, margin + scaled(564), y + labelHeight + scaled(5),
               scaled(245), scaled(32), TRUE);

    const int actionsY = height - scaled(116);
    MoveWindow(gState->startStop, margin, actionsY, scaled(220), scaled(42), TRUE);
    MoveWindow(gState->openOutput, margin + scaled(232), actionsY,
               scaled(180), scaled(42), TRUE);
    MoveWindow(gState->copyEtlCommands, margin + scaled(424), actionsY,
               scaled(230), scaled(42), TRUE);
    MoveWindow(gState->status, margin, height - scaled(58),
               width - margin * 2, scaled(34), TRUE);
}

void paint(HWND window) {
    PAINTSTRUCT paintInfo{};
    HDC dc = BeginPaint(window, &paintInfo);
    RECT client{};
    GetClientRect(window, &client);
    fillSolid(dc, client, kBackground);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, gState->titleFont);
    SetTextColor(dc, kText);
    RECT title{scaled(24), scaled(20), client.right - scaled(24), scaled(55)};
    DrawTextW(dc, L"Fast Real-Time High-FPS Capture", -1, &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, gState->smallFont);
    SetTextColor(dc, kMuted);
    RECT subtitle{scaled(24), scaled(56), client.right - scaled(24), scaled(94)};
    DrawTextW(
        dc,
        L"Smooth constant pacing or measurable source VFR • WASAPI game audio • F9 start/stop",
        -1, &subtitle, DT_LEFT | DT_TOP | DT_SINGLELINE);

    RECT outputLabel{scaled(24), scaled(94), client.right - scaled(24), scaled(111)};
    DrawTextW(dc, L"Output folder", -1, &outputLabel, DT_LEFT | DT_SINGLELINE);
    RECT ffmpegLabel{scaled(24), scaled(146), client.right - scaled(24), scaled(163)};
    DrawTextW(dc, L"FFmpeg executable", -1, &ffmpegLabel, DT_LEFT | DT_SINGLELINE);

    const int y = scaled(240);
    const int margin = scaled(24);
    const int gap = scaled(12);
    const int fieldWidth = (client.right - margin * 2 - gap * 3) / 4;
    const wchar_t* labels[] = {
        L"Display", L"Target FPS (30–1000)", L"Encoder", L"Quality"};
    for (int index = 0; index < 4; ++index) {
        RECT label{
            margin + (fieldWidth + gap) * index,
            y,
            margin + (fieldWidth + gap) * index + fieldWidth,
            y + scaled(18)};
        DrawTextW(dc, labels[index], -1, &label, DT_LEFT | DT_SINGLELINE);
    }

    RECT pacingLabel{margin, scaled(326), margin + scaled(330), scaled(344)};
    DrawTextW(dc, L"Frame pacing", -1, &pacingLabel, DT_LEFT | DT_SINGLELINE);

    SelectObject(dc, gState->labelFont);
    SetTextColor(dc, kWarning);
    RECT note{
        margin,
        scaled(408),
        client.right - margin,
        std::max(static_cast<LONG>(scaled(440)), client.bottom - scaled(128))};
    DrawTextW(
        dc,
        L"RECORDING SAFETY — Run /r_fullscreen 0 and then /vid_restart before F9. "
        L"Alt+Tab, switching windows or displays, entering/leaving fullscreen, or changing "
        L"resolution/refresh rate may trigger FFmpeg error 887a0026 and stop the recording. "
        L"Do not change the display state while recording.",
        -1, &note, DT_LEFT | DT_WORDBREAK);
    EndPaint(window, &paintInfo);
}

void initializeControls() {
    const DWORD edit = WS_BORDER | ES_AUTOHSCROLL;
    gState->outputPath = makeControl(0, WC_EDITW, L"", edit, IdOutputPath);
    gState->chooseOutput = makeControl(
        0, WC_BUTTONW, L"Choose output", BS_OWNERDRAW | WS_TABSTOP,
        IdChooseOutput, gState->labelFont);
    gState->ffmpegPath = makeControl(
        0, WC_EDITW, L"", edit | ES_READONLY, IdFfmpegPath);
    gState->chooseFfmpeg = makeControl(
        0, WC_BUTTONW, L"Locate ffmpeg", BS_OWNERDRAW | WS_TABSTOP,
        IdChooseFfmpeg, gState->labelFont);
    gState->display = makeControl(
        0, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdDisplay);
    gState->frameRate = makeControl(
        0, WC_COMBOBOXW, L"250",
        CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, IdFrameRate);
    gState->encoder = makeControl(
        0, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdEncoder);
    gState->quality = makeControl(
        0, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdQuality);
    gState->pacing = makeControl(
        0, WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IdPacing);
    gState->cursor = makeControl(
        0, WC_BUTTONW, L"Capture mouse cursor", BS_OWNERDRAW | WS_TABSTOP,
        IdCursor, gState->labelFont);
    gState->systemAudio = makeControl(
        0, WC_BUTTONW, L"Capture game/system audio", BS_OWNERDRAW | WS_TABSTOP,
        IdSystemAudio, gState->labelFont);
    gState->startStop = makeControl(
        0, WC_BUTTONW, L"Start recording  (F9)", BS_OWNERDRAW | WS_TABSTOP,
        IdStartStop, gState->labelFont);
    gState->openOutput = makeControl(
        0, WC_BUTTONW, L"Open output folder", BS_OWNERDRAW | WS_TABSTOP,
        IdOpenOutput, gState->labelFont);
    gState->copyEtlCommands = makeControl(
        0, WC_BUTTONW, L"Copy ETL windowed commands", BS_OWNERDRAW | WS_TABSTOP,
        IdCopyEtlCommands, gState->labelFont);
    gState->status = makeControl(
        0, WC_STATICW,
        L"Ready • choose 250 FPS or higher, then press F9",
        SS_LEFT | SS_CENTERIMAGE, IdStatus, gState->smallFont);

    gState->displays = enumerateDisplays();
    for (std::size_t index = 0; index < gState->displays.size(); ++index) {
        const DisplayInfo& display = gState->displays[index];
        const int width = display.bounds.right - display.bounds.left;
        const int height = display.bounds.bottom - display.bounds.top;
        std::wstring label = L"Display " + std::to_wstring(index + 1) + L" • " +
                             std::to_wstring(width) + L"×" + std::to_wstring(height);
        if (display.primary) label += L" • Primary";
        SendMessageW(gState->display, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    }
    const int savedDisplay = std::clamp(
        readIniInt(L"DisplayIndex", 0), 0,
        static_cast<int>(gState->displays.size()) - 1);
    SendMessageW(gState->display, CB_SETCURSEL, savedDisplay, 0);

    for (const wchar_t* value : {
             L"60", L"120", L"144", L"240", L"250",
             L"300", L"360", L"480", L"500"}) {
        SendMessageW(gState->frameRate, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(value));
    }
    const std::wstring savedFps = std::to_wstring(readIniInt(L"FrameRate", 250));
    const LRESULT fpsIndex = SendMessageW(
        gState->frameRate, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(savedFps.c_str()));
    if (fpsIndex != CB_ERR) {
        SendMessageW(gState->frameRate, CB_SETCURSEL, fpsIndex, 0);
    } else {
        SetWindowTextW(gState->frameRate, savedFps.c_str());
    }

    for (int index = 0; index <= 4; ++index) {
        const std::wstring name = realtimeCaptureEncoderName(
            static_cast<RealtimeCaptureEncoder>(index));
        SendMessageW(gState->encoder, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(gState->encoder, CB_SETCURSEL,
                 std::clamp(readIniInt(L"Encoder", 0), 0, 4), 0);

    for (int index = 0; index <= 2; ++index) {
        const std::wstring name = realtimeCaptureQualityName(
            static_cast<RealtimeCaptureQuality>(index));
        SendMessageW(gState->quality, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(gState->quality, CB_SETCURSEL,
                 std::clamp(readIniInt(L"Quality", 1), 0, 2), 0);

    for (int index = 0; index <= 1; ++index) {
        const std::wstring name = realtimeCapturePacingName(
            static_cast<RealtimeCapturePacing>(index));
        SendMessageW(gState->pacing, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(gState->pacing, CB_SETCURSEL,
                 std::clamp(readIniInt(L"Pacing", 0), 0, 1), 0);

    gState->drawCursor = readIniInt(L"DrawCursor", 0) != 0;
    gState->captureSystemAudio = readIniInt(L"SystemAudio", 1) != 0;
    std::filesystem::path output = readIni(L"OutputFolder");
    if (output.empty()) output = defaultOutputFolder();
    std::filesystem::path ffmpeg = readIni(L"FfmpegExecutable");
    std::error_code error;
    if (!std::filesystem::is_regular_file(ffmpeg, error) || error) {
        ffmpeg = findFfmpeg(gState->suggestedFfmpeg);
    }
    SetWindowTextW(gState->outputPath, output.c_str());
    SetWindowTextW(gState->ffmpegPath, ffmpeg.c_str());
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            gState->window = window;
            gState->dpi = dpiFor(window);
            recreateFonts();
            gState->backgroundBrush = CreateSolidBrush(kBackground);
            gState->panelBrush = CreateSolidBrush(kPanel);
            gState->controlBrush = CreateSolidBrush(kControl);
            gState->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            initializeControls();
            if (!RegisterHotKey(window, kHotkeyId, MOD_NOREPEAT, VK_F9)) {
                SetWindowTextW(
                    gState->status,
                    L"F9 is already used by another application • use the Start/Stop button");
            }
            return 0;
        case WM_SIZE:
            layout(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_DPICHANGED: {
            const int dpi = HIWORD(wParam);
            if (dpi > 0) {
                gState->dpi = dpi;
                recreateFonts();
                EnumChildWindows(
                    window,
                    [](HWND child, LPARAM font) -> BOOL {
                        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(gState->font));
            }
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                SetWindowPos(
                    window, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            RECT client{};
            GetClientRect(window, &client);
            layout(client.right, client.bottom);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = scaled(800);
            limits->ptMinTrackSize.y = scaled(590);
            return 0;
        }
        case WM_PAINT:
            paint(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DRAWITEM:
            drawOwnerButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(
                dc,
                reinterpret_cast<HWND>(lParam) == gState->status
                    ? kSuccess
                    : kMuted);
            return reinterpret_cast<LRESULT>(gState->backgroundBrush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kControl);
            return reinterpret_cast<LRESULT>(gState->controlBrush);
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IdChooseOutput:
                    if (const auto folder = chooseFolder(window)) {
                        SetWindowTextW(gState->outputPath, folder->c_str());
                    }
                    return 0;
                case IdChooseFfmpeg:
                    if (const auto path = chooseFfmpeg(window)) {
                        SetWindowTextW(gState->ffmpegPath, path->c_str());
                    }
                    return 0;
                case IdCursor:
                    gState->drawCursor = !gState->drawCursor;
                    InvalidateRect(gState->cursor, nullptr, TRUE);
                    return 0;
                case IdSystemAudio:
                    gState->captureSystemAudio = !gState->captureSystemAudio;
                    InvalidateRect(gState->systemAudio, nullptr, TRUE);
                    return 0;
                case IdStartStop:
                    toggleCapture();
                    return 0;
                case IdOpenOutput: {
                    std::filesystem::path folder = getText(gState->outputPath);
                    if (!gState->lastOutput.empty()) folder = gState->lastOutput.parent_path();
                    ShellExecuteW(window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                case IdCopyEtlCommands:
                    if (copySafeEtlCommands(window, true)) {
                        SetWindowTextW(
                            gState->status,
                            L"Copied: r_fullscreen 0; vid_restart • paste into the ETL console");
                    }
                    return 0;
            }
            break;
        case WM_HOTKEY:
            if (wParam == kHotkeyId) toggleCapture();
            return 0;
        case kCaptureStatus: {
            std::unique_ptr<std::wstring> value(
                reinterpret_cast<std::wstring*>(lParam));
            if (value) SetWindowTextW(gState->status, value->c_str());
            return 0;
        }
        case kCaptureFinished: {
            std::unique_ptr<CaptureResult> result(
                reinterpret_cast<CaptureResult*>(lParam));
            if (gState->worker != nullptr) {
                WaitForSingleObject(gState->worker, 1000);
                CloseHandle(gState->worker);
                gState->worker = nullptr;
            }
            setRunningUi(false);
            EnableWindow(gState->startStop, TRUE);
            if (result && result->success) {
                gState->lastOutput = result->outputPath;
                std::wstring status = L"Saved: " +
                                      result->outputPath.filename().wstring();
                if (!result->detail.empty()) status += L" • " + result->detail;
                SetWindowTextW(gState->status, status.c_str());
            } else if (result) {
                gState->lastOutput = result->fallbackPath;
                SetWindowTextW(gState->status, result->detail.c_str());
                showCaptureMessage(
                    window, result->detail.c_str(), L"Fast capture failed",
                    MB_OK | MB_ICONERROR);
            }
            if (gState->closeAfterFinish) DestroyWindow(window);
            return 0;
        }
        case WM_CLOSE:
            if (gState->running) {
                const int answer = showCaptureMessage(
                    window,
                    L"Stop and save the active recording before closing?",
                    L"Fast capture", MB_OKCANCEL | MB_ICONQUESTION);
                if (answer == IDOK) {
                    gState->closeAfterFinish = true;
                    stopCapture();
                }
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            UnregisterHotKey(window, kHotkeyId);
            if (gState->stopEvent != nullptr) CloseHandle(gState->stopEvent);
            for (HFONT font : {
                     gState->font, gState->smallFont,
                     gState->labelFont, gState->titleFont}) {
                if (font != nullptr) DeleteObject(font);
            }
            for (HBRUSH brush : {
                     gState->backgroundBrush,
                     gState->panelBrush,
                     gState->controlBrush}) {
                if (brush != nullptr) DeleteObject(brush);
            }
            delete gState;
            gState = nullptr;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void registerWindowClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = kClassName;
    registered = RegisterClassExW(&windowClass) != 0 ||
                 GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

void open(
    HWND owner,
    const std::filesystem::path& settingsPath,
    const std::filesystem::path& suggestedFfmpeg,
    const std::filesystem::path& suggestedOutputFolder) {
    if (gState != nullptr && gState->window != nullptr) {
        ShowWindow(gState->window, SW_RESTORE);
        SetForegroundWindow(gState->window);
        return;
    }
    if (!confirmFastCaptureSafety(owner)) return;
    registerWindowClass();
    gState = new State();
    gState->owner = owner;
    gState->settingsPath = settingsPath;
    gState->suggestedFfmpeg = suggestedFfmpeg;
    gState->suggestedOutputFolder = suggestedOutputFolder;
    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClassName,
        L"ET: Legacy Frag Finder — Fast Real-Time High-FPS Capture",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1040, 700,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) {
        delete gState;
        gState = nullptr;
        MessageBoxW(
            owner,
            L"The Fast Real-Time Capture window could not be created.",
            L"Frag Finder", MB_OK | MB_ICONERROR);
        return;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
}

void shutdown() {
    if (gState == nullptr) return;
    if (gState->running && gState->stopEvent != nullptr) {
        SetEvent(gState->stopEvent);
        if (gState->worker != nullptr) {
            WaitForSingleObject(gState->worker, 30000);
        }
    }
    if (gState != nullptr && gState->window != nullptr) {
        DestroyWindow(gState->window);
    }
}

} // namespace etlfrag::captureui

#endif
