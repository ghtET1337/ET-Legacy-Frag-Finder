// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_cut_window.hpp"
#include "demo_cut.hpp"
#include "etl_demo_parser.hpp"
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <thread>

namespace etlfrag::cutui {
namespace {
constexpr int Dialog = 201, Source = 2101, Action = 2102, Before = 2103, After = 2104,
              Range = 2105, Progress = 2106, Status = 2107, Save = 2108, Folder = 2109;
constexpr UINT ProgressMessage = WM_APP + 90, DoneMessage = WM_APP + 91;
struct State {
    ClipSource source;
    std::filesystem::path settings, destination;
    std::atomic_bool cancel{false};
    std::thread worker;
    DemoCutResult result;
    std::wstring error;
    bool busy = false, closeAfter = false;
    HBRUSH background = nullptr;
};
std::wstring wide(const std::string& text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size, 0);
    if (size) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
    return result;
}
std::wstring timeText(std::int32_t ms) { return wide(formatDuration(ms)); }
std::wstring seconds(int ms) {
    std::wostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3) << ms / 1000.0;
    return out.str();
}
bool readSeconds(HWND dialog, int id, int& ms) {
    wchar_t buffer[64]{};
    GetDlgItemTextW(dialog, id, buffer, 64);
    std::wstring value(buffer);
    std::replace(value.begin(), value.end(), L',', L'.');
    std::wistringstream input(value);
    input.imbue(std::locale::classic());
    double number = 0;
    if (!(input >> number) || !std::isfinite(number) || number < 0 || number > 3600) return false;
    input >> std::ws;
    if (!input.eof()) return false;
    ms = static_cast<int>(std::llround(number * 1000));
    return true;
}
bool readOptions(HWND dialog, State& state, DemoCutOptions& options) {
    options.actionStartMs = state.source.actionStartMs;
    options.actionEndMs = state.source.actionEndMs;
    return readSeconds(dialog, Before, options.beforeMs) && readSeconds(dialog, After, options.afterMs);
}
void updateRange(HWND dialog, State& state) {
    DemoCutOptions options;
    if (!readOptions(dialog, state, options)) {
        SetDlgItemTextW(dialog, Range, L"Enter 0 to 3600 seconds in both fields (decimals allowed).");
        return;
    }
    const auto start = std::max<std::int32_t>(0, options.actionStartMs - options.beforeMs);
    const auto end = std::int64_t(options.actionEndMs) + options.afterMs;
    if (end > INT32_MAX) { SetDlgItemTextW(dialog, Range, L"The selected range is too large."); return; }
    const auto text = L"Requested range: " + timeText(start) + L"  -  " + timeText(static_cast<int>(end)) +
                      L"   |   Duration: " + timeText(static_cast<int>(end) - start);
    SetDlgItemTextW(dialog, Range, text.c_str());
}
void startCut(HWND dialog, State& state) {
    DemoCutOptions options;
    if (!readOptions(dialog, state, options)) {
        MessageBoxW(dialog, L"Enter a number from 0 to 3600 seconds in each field.", L"Cut demo", MB_OK | MB_ICONWARNING);
        return;
    }
    wchar_t filename[32768]{};
    const auto start = std::max(0, options.actionStartMs - options.beforeMs);
    const auto end = std::int64_t(options.actionEndMs) + options.afterMs;
    auto base = state.source.demoPath.stem().wstring() + L"_cut_" + std::to_wstring(start) + L"-" + std::to_wstring(end) + L".dm_84";
    wcsncpy(filename, base.c_str(), 32767);
    wchar_t savedFolder[32768]{};
    GetPrivateProfileStringW(L"DemoCut", L"OutputFolder", L"", savedFolder, 32768, state.settings.c_str());
    std::filesystem::path initial = savedFolder;
    std::error_code ec;
    if (!std::filesystem::is_directory(initial, ec)) initial = state.source.demoPath.parent_path();
    OPENFILENAMEW save{};
    save.lStructSize = sizeof(save);
    save.hwndOwner = dialog;
    save.lpstrFilter = L"ET: Legacy client demo (*.dm_84)\0*.dm_84\0\0";
    save.lpstrFile = filename;
    save.nMaxFile = 32768;
    save.lpstrInitialDir = initial.c_str();
    save.lpstrDefExt = L"dm_84";
    save.lpstrTitle = L"Save a new cut demo";
    save.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&save)) return;
    state.destination = filename;
    if (std::filesystem::exists(state.destination, ec)) {
        MessageBoxW(dialog, L"That file already exists. Choose a new filename. Existing demos are never overwritten.", L"Cut demo", MB_OK | MB_ICONWARNING);
        return;
    }
    WritePrivateProfileStringW(L"DemoCut", L"BeforeMs", std::to_wstring(options.beforeMs).c_str(), state.settings.c_str());
    WritePrivateProfileStringW(L"DemoCut", L"AfterMs", std::to_wstring(options.afterMs).c_str(), state.settings.c_str());
    state.busy = true;
    state.cancel = false;
    state.error.clear();
    for (int id : {Before, After, Save, Folder}) EnableWindow(GetDlgItem(dialog, id), FALSE);
    SetDlgItemTextW(dialog, Status, L"Reading source demo and writing the selected action...");
    SetDlgItemTextW(dialog, IDCANCEL, L"Cancel cut");
    SendDlgItemMessageW(dialog, Progress, PBM_SETPOS, 0, 0);
    options.cancel = &state.cancel;
    options.progress = [dialog](int value) { PostMessageW(dialog, ProgressMessage, value, 0); };
    try {
        state.worker = std::thread([dialog, &state, options] {
            try { state.result = cutDemo(state.source.demoPath, state.destination, options); }
            catch (const std::exception& e) { state.error = wide(e.what()); }
            catch (...) { state.error = L"Unexpected error while cutting the demo."; }
            PostMessageW(dialog, DoneMessage, 0, 0);
        });
    } catch (const std::exception& e) {
        state.error = wide(e.what());
        PostMessageW(dialog, DoneMessage, 0, 0);
    }
}
INT_PTR CALLBACK procedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        state->background = CreateSolidBrush(RGB(24, 29, 35));
        SetDlgItemTextW(dialog, Source, state->source.demoPath.filename().c_str());
        const auto action = state->source.label + L"   |   " + timeText(state->source.actionStartMs) + L" - " + timeText(state->source.actionEndMs);
        SetDlgItemTextW(dialog, Action, action.c_str());
        const int before = std::min(3600000u, GetPrivateProfileIntW(L"DemoCut", L"BeforeMs", 5000, state->settings.c_str()));
        const int after = std::min(3600000u, GetPrivateProfileIntW(L"DemoCut", L"AfterMs", 3000, state->settings.c_str()));
        SetDlgItemTextW(dialog, Before, seconds(before).c_str());
        SetDlgItemTextW(dialog, After, seconds(after).c_str());
        SendDlgItemMessageW(dialog, Before, EM_SETLIMITTEXT, 20, 0);
        SendDlgItemMessageW(dialog, After, EM_SETLIMITTEXT, 20, 0);
        SendDlgItemMessageW(dialog, Progress, PBM_SETRANGE32, 0, 100);
        updateRange(dialog, *state);
        RECT owner{}, own{};
        GetWindowRect(GetParent(dialog), &owner);
        GetWindowRect(dialog, &own);
        SetWindowPos(dialog, nullptr, std::max(0L, owner.left + (owner.right-owner.left-(own.right-own.left))/2),
                     std::max(0L, owner.top + (owner.bottom-owner.top-(own.bottom-own.top))/2), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return TRUE;
    }
    if (!state) return FALSE;
    switch (message) {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(238, 241, 245));
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(24, 29, 35));
        return reinterpret_cast<INT_PTR>(state->background);
    case ProgressMessage:
        SendDlgItemMessageW(dialog, Progress, PBM_SETPOS, wParam, 0);
        return TRUE;
    case DoneMessage: {
        if (state->worker.joinable()) state->worker.join();
        state->busy = false;
        if (state->closeAfter) { EndDialog(dialog, 0); return TRUE; }
        for (int id : {Before, After, Save}) EnableWindow(GetDlgItem(dialog, id), TRUE);
        SetDlgItemTextW(dialog, IDCANCEL, L"Close");
        if (!state->error.empty()) {
            SetDlgItemTextW(dialog, Status, state->error.c_str());
        } else {
            SendDlgItemMessageW(dialog, Progress, PBM_SETPOS, 100, 0);
            EnableWindow(GetDlgItem(dialog, Folder), TRUE);
            const auto& r = state->result;
            std::wostringstream text;
            text << L"Saved: " << state->destination.filename().wstring() << L"\r\n"
                 << L"Actual range: " << timeText(r.actualStartMs) << L" - " << timeText(r.actualEndMs)
                 << L" | " << std::fixed << std::setprecision(2) << r.bytes / 1048576.0 << L" MB";
            if (r.clampedToDemoEnd) text << L" | End limited to available demo.";
            SetDlgItemTextW(dialog, Status, text.str().c_str());
            WritePrivateProfileStringW(L"DemoCut", L"OutputFolder", state->destination.parent_path().c_str(), state->settings.c_str());
        }
        return TRUE;
    }
    case WM_COMMAND:
        if ((LOWORD(wParam) == Before || LOWORD(wParam) == After) && HIWORD(wParam) == EN_CHANGE) {
            updateRange(dialog, *state); return TRUE;
        }
        if (LOWORD(wParam) == Save && !state->busy) { startCut(dialog, *state); return TRUE; }
        if (LOWORD(wParam) == Folder && !state->busy) {
            const auto args = L"/select,\"" + std::filesystem::absolute(state->destination).wstring() + L"\"";
            ShellExecuteW(dialog, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (LOWORD(wParam) != IDCANCEL) break;
        [[fallthrough]];
    case WM_CLOSE:
        if (state->busy) {
            state->cancel = true;
            if (message == WM_CLOSE) state->closeAfter = true;
            SetDlgItemTextW(dialog, Status, L"Cancelling the cut...");
        } else EndDialog(dialog, 0);
        return TRUE;
    case WM_DESTROY:
        state->cancel = true;
        if (state->worker.joinable()) state->worker.join();
        if (state->background) DeleteObject(state->background);
        state->background = nullptr;
        return TRUE;
    }
    return FALSE;
}
} // namespace
void open(HWND owner, const ClipSource& source, const std::filesystem::path& settingsPath) {
    State state;
    state.source = source;
    state.settings = settingsPath;
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(Dialog), owner,
                        procedure, reinterpret_cast<LPARAM>(&state)) == -1)
        MessageBoxW(owner, L"Could not open the Cut demo window.", L"Cut demo", MB_OK | MB_ICONERROR);
}
} // namespace etlfrag::cutui
