// SPDX-License-Identifier: GPL-3.0-or-later
#include "etl_demo_parser.hpp"
#include "app_storage.hpp"

#include <windows.h>
#include <windowsx.h>
#include <process.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"ETLFragFinderWindow";
constexpr wchar_t kTimelineClass[] = L"ETLFragFinderTimeline";
constexpr wchar_t kProtocolInspectorClass[] = L"ETLFragFinderProtocolInspector";
constexpr wchar_t kApplicationName[] = L"ET: Legacy Frag Finder by ght";
constexpr int kApplicationIconResource = 101;
constexpr int kPlaybackPrerollMs = 5000;
constexpr UINT kFolderScanProgressMessage = WM_APP + 1;
constexpr UINT kFolderScanCompleteMessage = WM_APP + 2;
constexpr UINT kFolderChangedMessage = WM_APP + 3;
constexpr UINT_PTR kFolderWatchDebounceTimer = 5001;

constexpr COLORREF kBackground = RGB(14, 17, 21);
constexpr COLORREF kHeader = RGB(18, 22, 27);
constexpr COLORREF kPanel = RGB(24, 29, 35);
constexpr COLORREF kControl = RGB(31, 37, 44);
constexpr COLORREF kControlPressed = RGB(40, 47, 56);
constexpr COLORREF kBorder = RGB(61, 70, 81);
constexpr COLORREF kText = RGB(238, 241, 245);
constexpr COLORREF kMuted = RGB(158, 168, 180);
constexpr COLORREF kAccent = RGB(74, 134, 211);
constexpr COLORREF kAccentPressed = RGB(57, 108, 174);
constexpr COLORREF kSuccess = RGB(104, 199, 139);
constexpr COLORREF kDanger = RGB(234, 107, 107);
constexpr COLORREF kWarmup = RGB(221, 174, 88);

enum ControlId {
    IdOpenDemo = 1001,
    IdDemoPath,
    IdTabMultiKills,
    IdTabAllEvents,
    IdTabFolderScan,
    IdTabHighlights,
    IdTabLibrary,
    IdExportCurrent,
    IdLaunchAsAdministrator,
    IdLaunchWithoutSeeking,
    IdPlayer,
    IdMinimumKills,
    IdMinimumHeadshots,
    IdMaximumGap,
    IdWeapon,
    IdTeamKills,
    IdWarmupKills,
    IdPostDeathExplosives,
    IdPostDeathWindow,
    IdSearch,
    IdChooseEtl,
    IdPlayRun,
    IdAddRunHighlight,
    IdRunList,
    IdRunKillList,
    IdAllEventList,
    IdEventPlayer,
    IdPlayEvent,
    IdViewProtocolLog,
    IdFolderPath,
    IdChooseFolder,
    IdFolderMinimumKills,
    IdFolderMinimumHeadshots,
    IdFolderMaximumGap,
    IdFolderWeapon,
    IdFolderTeamKills,
    IdFolderWarmupKills,
    IdFolderPostDeathExplosives,
    IdFolderPostDeathWindow,
    IdFolderQuery,
    IdFolderField,
    IdFolderApplyFilters,
    IdFolderScan,
    IdPlayFolderRun,
    IdAddFolderHighlight,
    IdFolderRunList,
    IdFolderKillList,
    IdFolderWatch,
    IdTimeline,
    IdHighlightInfo,
    IdPlayHighlight,
    IdRemoveHighlight,
    IdClearHighlights,
    IdHighlightList,
    IdLibraryQuery,
    IdLibraryField,
    IdLibraryScope,
    IdLibraryDuplicates,
    IdLibrarySearch,
    IdLibraryOpen,
    IdLibraryList,
    IdStatus,
    IdPlayerLabel = 2001,
    IdMinimumLabel,
    IdMinimumHeadshotsLabel,
    IdGapLabel,
    IdWeaponLabel,
    IdPostDeathWindowLabel,
    IdEventPlayerLabel,
    IdFolderMinimumLabel,
    IdFolderMinimumHeadshotsLabel,
    IdFolderGapLabel,
    IdFolderWeaponLabel,
    IdFolderPostDeathWindowLabel,
    IdFolderQueryLabel,
    IdFolderFieldLabel,
    IdLibraryQueryLabel,
    IdLibraryFieldLabel,
};

enum class ListKind {
    Runs,
    RunKills,
    AllEvents,
    FolderRuns,
    FolderKills,
    Highlights,
    Library,
};

struct ListSortState {
    ListKind kind;
    int column = -1;
    bool ascending = true;
};

struct PlayerSelection {
    int clientNum = -1;
    int sessionId = -1;
};

struct FolderRunResult {
    etlfrag::IndexedDemoSummary demo;
    etlfrag::FragRun run;
    std::vector<etlfrag::KillEvent> kills;
};

struct FolderScanOutcome {
    std::filesystem::path folder;
    std::size_t filesFound = 0;
    std::size_t filesParsed = 0;
    std::size_t filesFailed = 0;
    std::size_t filesWithoutPov = 0;
    std::size_t filesWithWarnings = 0;
    std::size_t filesLoadedFromIndex = 0;
    std::size_t filesNewOrChanged = 0;
    std::size_t staleEntriesRemoved = 0;
    bool cancelled = false;
    bool automatic = false;
    std::string fatalError;
    std::vector<std::string> failures;
};

struct FolderScanRequest {
    HWND window = nullptr;
    std::filesystem::path folder;
    std::filesystem::path indexPath;
    std::shared_ptr<std::atomic_bool> cancelRequested;
    bool automatic = false;
};

struct FolderWatchRequest {
    HWND window = nullptr;
    std::filesystem::path folder;
    HANDLE stopEvent = nullptr;
};

struct ProtocolInspectorState {
    HWND window = nullptr;
    HWND description = nullptr;
    HWND query = nullptr;
    HWND findNext = nullptr;
    HWND saveText = nullptr;
    HWND text = nullptr;
    HWND status = nullptr;
    HFONT uiFont = nullptr;
    HFONT buttonFont = nullptr;
    HFONT monoFont = nullptr;
    HFONT smallFont = nullptr;
    int dpi = 96;
    std::filesystem::path demoPath;
    std::string utf8Text;
    std::wstring wideText;
    std::size_t lineCount = 0;
};

constexpr int kProtocolQueryId = 3001;
constexpr int kProtocolFindId = 3002;
constexpr int kProtocolSaveId = 3003;
constexpr int kProtocolTextId = 3004;

struct AppState {
    HWND window = nullptr;
    HWND openDemo = nullptr;
    HWND demoPath = nullptr;
    HWND tabMultiKills = nullptr;
    HWND tabAllEvents = nullptr;
    HWND tabFolderScan = nullptr;
    HWND tabHighlights = nullptr;
    HWND tabLibrary = nullptr;
    HWND exportCurrent = nullptr;
    HWND launchAsAdministrator = nullptr;
    HWND launchWithoutSeekingControl = nullptr;
    HWND playerLabel = nullptr;
    HWND player = nullptr;
    HWND minimumLabel = nullptr;
    HWND minimumKills = nullptr;
    HWND minimumHeadshotsLabel = nullptr;
    HWND minimumHeadshots = nullptr;
    HWND gapLabel = nullptr;
    HWND maximumGap = nullptr;
    HWND weaponLabel = nullptr;
    HWND weapon = nullptr;
    HWND teamKills = nullptr;
    HWND warmupKills = nullptr;
    HWND postDeathExplosives = nullptr;
    HWND postDeathWindowLabel = nullptr;
    HWND postDeathWindow = nullptr;
    HWND search = nullptr;
    HWND chooseEtl = nullptr;
    HWND playRun = nullptr;
    HWND addRunHighlight = nullptr;
    HWND runList = nullptr;
    HWND runKillList = nullptr;
    HWND allEventList = nullptr;
    HWND eventPlayerLabel = nullptr;
    HWND eventPlayer = nullptr;
    HWND playEvent = nullptr;
    HWND viewProtocolLog = nullptr;
    HWND folderPath = nullptr;
    HWND chooseFolder = nullptr;
    HWND folderMinimumLabel = nullptr;
    HWND folderMinimumKills = nullptr;
    HWND folderMinimumHeadshotsLabel = nullptr;
    HWND folderMinimumHeadshots = nullptr;
    HWND folderGapLabel = nullptr;
    HWND folderMaximumGap = nullptr;
    HWND folderWeaponLabel = nullptr;
    HWND folderWeapon = nullptr;
    HWND folderTeamKills = nullptr;
    HWND folderWarmupKills = nullptr;
    HWND folderPostDeathExplosives = nullptr;
    HWND folderPostDeathWindowLabel = nullptr;
    HWND folderPostDeathWindow = nullptr;
    HWND folderQueryLabel = nullptr;
    HWND folderQuery = nullptr;
    HWND folderFieldLabel = nullptr;
    HWND folderField = nullptr;
    HWND folderApplyFilters = nullptr;
    HWND folderScan = nullptr;
    HWND playFolderRun = nullptr;
    HWND addFolderHighlight = nullptr;
    HWND folderRunList = nullptr;
    HWND folderKillList = nullptr;
    HWND folderWatch = nullptr;
    HWND timeline = nullptr;
    HWND highlightInfo = nullptr;
    HWND playHighlight = nullptr;
    HWND removeHighlight = nullptr;
    HWND clearHighlights = nullptr;
    HWND highlightList = nullptr;
    HWND libraryQueryLabel = nullptr;
    HWND libraryQuery = nullptr;
    HWND libraryFieldLabel = nullptr;
    HWND libraryField = nullptr;
    HWND libraryScope = nullptr;
    HWND libraryDuplicates = nullptr;
    HWND librarySearch = nullptr;
    HWND libraryOpen = nullptr;
    HWND libraryList = nullptr;
    HWND status = nullptr;

    HFONT font = nullptr;
    HFONT smallFont = nullptr;
    HFONT labelFont = nullptr;
    HFONT titleFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH controlBrush = nullptr;
    HIMAGELIST rowHeightImages = nullptr;
    int dpi = 96;
    int activeTab = 0;
    bool demoLoaded = false;
    bool launchEtlAsAdministrator = false;
    bool launchWithoutSeeking = false;
    bool includeTeamKills = false;
    bool includeWarmupKills = false;
    bool postDeathExplosivesEnabled = false;
    bool folderIncludeTeamKills = false;
    bool folderIncludeWarmupKills = false;
    bool folderPostDeathExplosivesEnabled = false;
    bool folderWatchEnabled = true;
    bool libraryFolderOnly = false;
    bool libraryDuplicatesOnly = false;
    bool folderRescanPending = false;
    bool folderScanRunning = false;
    std::shared_ptr<std::atomic_bool> folderCancelRequested;
    HANDLE folderThread = nullptr;
    HANDLE folderWatchThread = nullptr;
    HANDLE folderWatchStopEvent = nullptr;
    std::size_t folderFilesFound = 0;
    std::size_t folderFilesParsed = 0;
    std::size_t folderFilesFailed = 0;
    std::size_t folderFilesWithoutPov = 0;
    std::size_t folderFilesLoadedFromIndex = 0;
    std::size_t folderFilesNewOrChanged = 0;
    std::size_t indexedDemoCount = 0;
    std::int32_t timelineHoverMs = -1;

    etlfrag::DemoInfo demo;
    std::vector<etlfrag::FragRun> runs;
    std::vector<PlayerSelection> playerIds;
    std::vector<PlayerSelection> eventPlayerIds;
    std::vector<int> weaponIds;
    std::vector<int> folderWeaponIds;
    std::vector<etlfrag::IndexedDemoSummary> folderDemos;
    std::vector<FolderRunResult> folderRuns;
    std::vector<std::size_t> folderRows;
    std::vector<std::pair<std::size_t, std::size_t>> folderKillRows;
    etlfrag::DemoInfo folderTimelineDemo;
    std::vector<etlfrag::FragRun> folderTimelineRuns;
    std::int64_t folderTimelineDemoId = -1;
    std::size_t folderTimelineSelectedRun = static_cast<std::size_t>(-1);
    etlfrag::RunFilter folderAppliedFilter;
    ListSortState runSort{ListKind::Runs};
    ListSortState runKillSort{ListKind::RunKills};
    ListSortState allEventSort{ListKind::AllEvents};
    ListSortState folderRunSort{ListKind::FolderRuns};
    ListSortState folderKillSort{ListKind::FolderKills};
    ListSortState highlightSort{ListKind::Highlights};
    ListSortState librarySort{ListKind::Library};
    etlfrag::SqliteDemoIndex demoIndex;
    std::vector<etlfrag::IndexedDemoSummary> libraryRows;
    std::vector<etlfrag::HighlightItem> highlights;
    std::filesystem::path demoFolder;
    std::filesystem::path etlExecutable;
    std::filesystem::path iniPath;
    std::filesystem::path dataFolder;
    std::filesystem::path indexPath;
    std::filesystem::path highlightsPath;
    HWND protocolInspector = nullptr;
};

AppState gApp;

LRESULT CALLBACK listViewSubclass(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData);
LRESULT CALLBACK timelineProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK protocolInspectorProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
void updateWindowTitle();
void showTab(int tab);
void searchLibrary(bool reportStatus);
etlfrag::DemoSearchField selectedSearchField(HWND fieldControl);

int scale(int value) {
    return MulDiv(value, gApp.dpi, 96);
}

int dpiForWindow(HWND window) {
    using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        const FARPROC address = GetProcAddress(user32, "GetDpiForWindow");
        GetDpiForWindowFunction function = nullptr;
        static_assert(sizeof(function) == sizeof(address));
        std::memcpy(&function, &address, sizeof(function));
        if (function != nullptr) {
            const int dpi = static_cast<int>(function(window));
            if (dpi > 0) return dpi;
        }
    }
    HDC dc = GetDC(window);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) ReleaseDC(window, dc);
    return dpi > 0 ? dpi : 96;
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0) {
        return std::wstring(utf8.begin(), utf8.end());
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), count);
    return result;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0) {
        return std::string(wide.begin(), wide.end());
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.data(),
        static_cast<int>(wide.size()),
        result.data(),
        count,
        nullptr,
        nullptr);
    return result;
}

std::wstring durationText(std::int32_t milliseconds, bool millis = true) {
    return toWide(etlfrag::formatDuration(milliseconds, millis));
}

void setStatus(const std::wstring& text) {
    if (gApp.status != nullptr) {
        SetWindowTextW(gApp.status, text.c_str());
    }
}

void setFont(HWND control, HFONT font = nullptr) {
    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font != nullptr ? font : gApp.font),
        TRUE);
}

HWND createControl(
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
        gApp.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    setFont(control, font);
    return control;
}

void addListColumn(HWND list, int index, int width, const wchar_t* title) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<wchar_t*>(title);
    column.cx = scale(width);
    column.iSubItem = index;
    if (ListView_InsertColumn(list, index, &column) >= 0) {
        if (HWND header = ListView_GetHeader(list); header != nullptr) {
            HDITEMW item{};
            item.mask = HDI_FORMAT;
            if (Header_GetItem(header, index, &item)) {
                item.fmt |= HDF_OWNERDRAW;
                Header_SetItem(header, index, &item);
            }
        }
    }
}

void setListCell(HWND list, int row, int column, const std::wstring& text) {
    ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str()));
}

int addListRow(HWND list, const std::wstring& text, LPARAM data) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(list);
    item.pszText = const_cast<wchar_t*>(text.c_str());
    item.lParam = data;
    return ListView_InsertItem(list, &item);
}

int selectedListData(HWND list) {
    const int row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (row < 0) {
        return -1;
    }
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    return ListView_GetItem(list, &item) ? static_cast<int>(item.lParam) : -1;
}

int compareNumbers(std::int64_t left, std::int64_t right) {
    return left < right ? -1 : (left > right ? 1 : 0);
}

int compareText(const std::wstring& left, const std::wstring& right) {
    const int result = CompareStringOrdinal(
        left.c_str(), static_cast<int>(left.size()),
        right.c_str(), static_cast<int>(right.size()), TRUE);
    if (result == CSTR_LESS_THAN) {
        return -1;
    }
    if (result == CSTR_GREATER_THAN) {
        return 1;
    }
    return 0;
}

std::wstring describeRun(
    const etlfrag::DemoInfo& demo,
    const etlfrag::FragRun& run) {
    std::wstring description;
    for (const std::size_t killIndex : run.killIndices) {
        if (killIndex >= demo.kills.size()) {
            continue;
        }
        const etlfrag::KillEvent& kill = demo.kills[killIndex];
        if (!description.empty()) {
            description += L", ";
        }
        description += toWide(kill.targetName) + L" (" +
                       toWide(etlfrag::weaponName(kill.weapon)) + L")";
    }
    return description;
}

std::wstring describeKills(const std::vector<etlfrag::KillEvent>& kills) {
    std::wstring description;
    for (const etlfrag::KillEvent& kill : kills) {
        if (!description.empty()) description += L", ";
        description += toWide(kill.targetName) + L" (" +
                       toWide(etlfrag::weaponName(kill.weapon)) + L")";
    }
    return description;
}

std::wstring eventSortText(const etlfrag::KillEvent& kill) {
    std::wstring text;
    if (kill.suicide) {
        text = L"SUICIDE";
    } else if (kill.attacker < 0) {
        text = L"WORLD";
    } else if (kill.teamKill) {
        text = L"TEAMKILL";
    } else {
        text = L"KILL";
    }
    if (kill.headshot) {
        text += L" HEADSHOT KILL";
    }
    text += L" " + toWide(etlfrag::matchPhaseName(kill.matchPhase));
    return text;
}

std::filesystem::path relativeDemoPath(const std::filesystem::path& path) {
    std::filesystem::path relative = path.lexically_relative(gApp.demoFolder);
    return relative.empty() ? path.filename() : relative;
}

int compareRunRows(LPARAM leftData, LPARAM rightData, int column) {
    const std::size_t leftIndex = static_cast<std::size_t>(leftData);
    const std::size_t rightIndex = static_cast<std::size_t>(rightData);
    if (leftIndex >= gApp.runs.size() || rightIndex >= gApp.runs.size()) {
        return compareNumbers(leftData, rightData);
    }
    const etlfrag::FragRun& left = gApp.runs[leftIndex];
    const etlfrag::FragRun& right = gApp.runs[rightIndex];
    const etlfrag::KillEvent* leftFirst =
        !left.killIndices.empty() && left.killIndices.front() < gApp.demo.kills.size()
            ? &gApp.demo.kills[left.killIndices.front()]
            : nullptr;
    const etlfrag::KillEvent* rightFirst =
        !right.killIndices.empty() && right.killIndices.front() < gApp.demo.kills.size()
            ? &gApp.demo.kills[right.killIndices.front()]
            : nullptr;
    switch (column) {
        case 0: return compareNumbers(left.startDemoTimeMs, right.startDemoTimeMs);
        case 1:
            return compareNumbers(
                leftFirst != nullptr ? leftFirst->matchRemainingMs : -1,
                rightFirst != nullptr ? rightFirst->matchRemainingMs : -1);
        case 2: return compareText(toWide(left.attackerName), toWide(right.attackerName));
        case 3: return compareNumbers(left.killIndices.size(), right.killIndices.size());
        case 4: return compareNumbers(left.headshotCount, right.headshotCount);
        case 5:
            return compareNumbers(
                left.endDemoTimeMs - left.startDemoTimeMs,
                right.endDemoTimeMs - right.startDemoTimeMs);
        case 6: return compareText(describeRun(gApp.demo, left), describeRun(gApp.demo, right));
        default: return 0;
    }
}

int compareSingleKillRows(LPARAM leftData, LPARAM rightData, int column, bool allEvents) {
    const std::size_t leftIndex = static_cast<std::size_t>(leftData);
    const std::size_t rightIndex = static_cast<std::size_t>(rightData);
    if (leftIndex >= gApp.demo.kills.size() || rightIndex >= gApp.demo.kills.size()) {
        return compareNumbers(leftData, rightData);
    }
    const etlfrag::KillEvent& left = gApp.demo.kills[leftIndex];
    const etlfrag::KillEvent& right = gApp.demo.kills[rightIndex];
    if (allEvents) {
        switch (column) {
            case 0: return compareNumbers(leftIndex, rightIndex);
            case 1: return compareNumbers(left.demoTimeMs, right.demoTimeMs);
            case 2: return compareNumbers(left.matchRemainingMs, right.matchRemainingMs);
            case 3: return compareText(toWide(left.attackerName), toWide(right.attackerName));
            case 4: return compareText(toWide(left.targetName), toWide(right.targetName));
            case 5:
                return compareText(
                    toWide(etlfrag::weaponName(left.weapon)),
                    toWide(etlfrag::weaponName(right.weapon)));
            case 6: return compareText(eventSortText(left), eventSortText(right));
            default: return 0;
        }
    }
    switch (column) {
        case 0: return compareNumbers(left.demoTimeMs, right.demoTimeMs);
        case 1: return compareNumbers(left.matchRemainingMs, right.matchRemainingMs);
        case 2: return compareText(toWide(left.targetName), toWide(right.targetName));
        case 3:
            return compareText(
                toWide(etlfrag::weaponName(left.weapon)),
                toWide(etlfrag::weaponName(right.weapon)));
        case 4: return compareText(eventSortText(left), eventSortText(right));
        default: return 0;
    }
}

int compareFolderRunRows(LPARAM leftData, LPARAM rightData, int column) {
    const std::size_t leftRow = static_cast<std::size_t>(leftData);
    const std::size_t rightRow = static_cast<std::size_t>(rightData);
    if (leftRow >= gApp.folderRows.size() || rightRow >= gApp.folderRows.size()) {
        return compareNumbers(leftData, rightData);
    }
    const std::size_t leftIndex = gApp.folderRows[leftRow];
    const std::size_t rightIndex = gApp.folderRows[rightRow];
    if (leftIndex >= gApp.folderRuns.size() || rightIndex >= gApp.folderRuns.size()) {
        return compareNumbers(leftData, rightData);
    }
    const FolderRunResult& leftResult = gApp.folderRuns[leftIndex];
    const FolderRunResult& rightResult = gApp.folderRuns[rightIndex];
    const etlfrag::FragRun& left = leftResult.run;
    const etlfrag::FragRun& right = rightResult.run;
    const etlfrag::KillEvent* leftFirst = leftResult.kills.empty() ? nullptr : &leftResult.kills.front();
    const etlfrag::KillEvent* rightFirst = rightResult.kills.empty() ? nullptr : &rightResult.kills.front();
    switch (column) {
        case 0:
            return compareText(
                relativeDemoPath(leftResult.demo.path).wstring(),
                relativeDemoPath(rightResult.demo.path).wstring());
        case 1: return compareText(toWide(leftResult.demo.mapName), toWide(rightResult.demo.mapName));
        case 2: return compareText(toWide(leftResult.demo.povName), toWide(rightResult.demo.povName));
        case 3: return compareNumbers(left.startDemoTimeMs, right.startDemoTimeMs);
        case 4:
            return compareNumbers(
                leftFirst != nullptr ? leftFirst->matchRemainingMs : -1,
                rightFirst != nullptr ? rightFirst->matchRemainingMs : -1);
        case 5: return compareNumbers(left.killIndices.size(), right.killIndices.size());
        case 6: return compareNumbers(left.headshotCount, right.headshotCount);
        case 7:
            return compareNumbers(
                left.endDemoTimeMs - left.startDemoTimeMs,
                right.endDemoTimeMs - right.startDemoTimeMs);
        case 8:
            return compareText(describeKills(leftResult.kills), describeKills(rightResult.kills));
        default: return 0;
    }
}

int compareFolderKillRows(LPARAM leftData, LPARAM rightData, int column) {
    const std::size_t leftRow = static_cast<std::size_t>(leftData);
    const std::size_t rightRow = static_cast<std::size_t>(rightData);
    if (leftRow >= gApp.folderKillRows.size() || rightRow >= gApp.folderKillRows.size()) {
        return compareNumbers(leftData, rightData);
    }
    const auto [leftRunIndex, leftKillIndex] = gApp.folderKillRows[leftRow];
    const auto [rightRunIndex, rightKillIndex] = gApp.folderKillRows[rightRow];
    if (leftRunIndex >= gApp.folderRuns.size() || rightRunIndex >= gApp.folderRuns.size() ||
        leftKillIndex >= gApp.folderRuns[leftRunIndex].kills.size() ||
        rightKillIndex >= gApp.folderRuns[rightRunIndex].kills.size()) {
        return compareNumbers(leftData, rightData);
    }
    const etlfrag::KillEvent& left = gApp.folderRuns[leftRunIndex].kills[leftKillIndex];
    const etlfrag::KillEvent& right = gApp.folderRuns[rightRunIndex].kills[rightKillIndex];
    switch (column) {
        case 0: return compareNumbers(left.demoTimeMs, right.demoTimeMs);
        case 1: return compareNumbers(left.matchRemainingMs, right.matchRemainingMs);
        case 2: return compareText(toWide(left.targetName), toWide(right.targetName));
        case 3:
            return compareText(
                toWide(etlfrag::weaponName(left.weapon)),
                toWide(etlfrag::weaponName(right.weapon)));
        case 4: return compareText(eventSortText(left), eventSortText(right));
        default: return 0;
    }
}

int compareLibraryRows(LPARAM leftData, LPARAM rightData, int column) {
    const std::size_t leftIndex = static_cast<std::size_t>(leftData);
    const std::size_t rightIndex = static_cast<std::size_t>(rightData);
    if (leftIndex >= gApp.libraryRows.size() || rightIndex >= gApp.libraryRows.size()) {
        return compareNumbers(leftData, rightData);
    }
    const auto& left = gApp.libraryRows[leftIndex];
    const auto& right = gApp.libraryRows[rightIndex];
    switch (column) {
        case 0: return compareText(toWide(left.fileName), toWide(right.fileName));
        case 1: return compareText(toWide(left.recordedDate), toWide(right.recordedDate));
        case 2: return compareText(toWide(left.mapName), toWide(right.mapName));
        case 3: return compareText(toWide(left.povName), toWide(right.povName));
        case 4: return compareNumbers(left.playerCount, right.playerCount);
        case 5: return compareNumbers(left.eventCount, right.eventCount);
        case 6:
            return compareNumbers(
                left.lastServerTimeMs - left.firstServerTimeMs,
                right.lastServerTimeMs - right.firstServerTimeMs);
        case 7: return compareNumbers(left.duplicateCount, right.duplicateCount);
        case 8: return compareText(left.path.wstring(), right.path.wstring());
        default: return 0;
    }
}

int compareHighlightRows(LPARAM leftData, LPARAM rightData, int column) {
    const std::size_t leftIndex = static_cast<std::size_t>(leftData);
    const std::size_t rightIndex = static_cast<std::size_t>(rightData);
    if (leftIndex >= gApp.highlights.size() || rightIndex >= gApp.highlights.size()) {
        return compareNumbers(leftData, rightData);
    }
    const etlfrag::HighlightItem& left = gApp.highlights[leftIndex];
    const etlfrag::HighlightItem& right = gApp.highlights[rightIndex];
    switch (column) {
        case 0:
            return compareText(left.demoPath.filename().wstring(), right.demoPath.filename().wstring());
        case 1: return compareText(toWide(left.mapName), toWide(right.mapName));
        case 2: return compareText(toWide(left.povName), toWide(right.povName));
        case 3: return compareNumbers(left.startDemoTimeMs, right.startDemoTimeMs);
        case 4: return compareNumbers(left.matchRemainingMs, right.matchRemainingMs);
        case 5: return compareNumbers(left.events.size(), right.events.size());
        case 6: return compareNumbers(left.headshotCount, right.headshotCount);
        case 7:
            return compareNumbers(
                left.endDemoTimeMs - left.startDemoTimeMs,
                right.endDemoTimeMs - right.startDemoTimeMs);
        case 8: return compareText(toWide(left.description), toWide(right.description));
        default: return 0;
    }
}

int CALLBACK compareListRows(LPARAM leftData, LPARAM rightData, LPARAM sortData) {
    const auto* state = reinterpret_cast<const ListSortState*>(sortData);
    if (state == nullptr) {
        return 0;
    }
    int result = 0;
    switch (state->kind) {
        case ListKind::Runs:
            result = compareRunRows(leftData, rightData, state->column);
            break;
        case ListKind::RunKills:
            result = compareSingleKillRows(leftData, rightData, state->column, false);
            break;
        case ListKind::AllEvents:
            result = compareSingleKillRows(leftData, rightData, state->column, true);
            break;
        case ListKind::FolderRuns:
            result = compareFolderRunRows(leftData, rightData, state->column);
            break;
        case ListKind::FolderKills:
            result = compareFolderKillRows(leftData, rightData, state->column);
            break;
        case ListKind::Highlights:
            result = compareHighlightRows(leftData, rightData, state->column);
            break;
        case ListKind::Library:
            result = compareLibraryRows(leftData, rightData, state->column);
            break;
    }
    if (result == 0) {
        return compareNumbers(leftData, rightData);
    }
    return state->ascending ? result : -result;
}

ListSortState* sortStateFor(HWND list) {
    if (list == gApp.runList) return &gApp.runSort;
    if (list == gApp.runKillList) return &gApp.runKillSort;
    if (list == gApp.allEventList) return &gApp.allEventSort;
    if (list == gApp.folderRunList) return &gApp.folderRunSort;
    if (list == gApp.folderKillList) return &gApp.folderKillSort;
    if (list == gApp.highlightList) return &gApp.highlightSort;
    if (list == gApp.libraryList) return &gApp.librarySort;
    return nullptr;
}

bool defaultAscendingFor(const ListSortState& state, int column) {
    return !((state.kind == ListKind::Runs && (column == 3 || column == 4)) ||
             (state.kind == ListKind::FolderRuns && (column == 5 || column == 6)) ||
             (state.kind == ListKind::Highlights && (column == 5 || column == 6)) ||
             (state.kind == ListKind::Library &&
              (column == 4 || column == 5 || column == 6 || column == 7)));
}

void updateHeaderSortIndicator(HWND list, const ListSortState& state) {
    HWND header = ListView_GetHeader(list);
    if (header == nullptr) {
        return;
    }
    const int count = Header_GetItemCount(header);
    for (int column = 0; column < count; ++column) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        if (!Header_GetItem(header, column, &item)) {
            continue;
        }
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (column == state.column) {
            item.fmt |= state.ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItem(header, column, &item);
    }
    InvalidateRect(header, nullptr, TRUE);
}

void applyListSort(HWND list, ListSortState& state) {
    if (state.column < 0) {
        return;
    }
    ListView_SortItems(list, compareListRows, reinterpret_cast<LPARAM>(&state));
    updateHeaderSortIndicator(list, state);
}

void sortListByColumn(HWND list, int column) {
    ListSortState* state = sortStateFor(list);
    if (state == nullptr) {
        return;
    }
    if (state->column == column) {
        state->ascending = !state->ascending;
    } else {
        state->column = column;
        state->ascending = defaultAscendingFor(*state, column);
    }
    applyListSort(list, *state);
}

void configureListView(HWND list) {
    ListView_SetExtendedListViewStyle(
        list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    ListView_SetBkColor(list, kPanel);
    ListView_SetTextBkColor(list, kPanel);
    ListView_SetTextColor(list, kText);
    ListView_SetImageList(list, gApp.rowHeightImages, LVSIL_SMALL);
    SetWindowTheme(list, L"DarkMode_Explorer", nullptr);
    if (HWND header = ListView_GetHeader(list); header != nullptr) {
        SetWindowTheme(header, L"", L"");
        setFont(header, gApp.smallFont);
    }
    SetWindowSubclass(list, listViewSubclass, 1, 0);
}

std::filesystem::path modulePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::wstring environmentValue(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring result(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, result.data(), needed);
    if (written == 0) {
        return {};
    }
    result.resize(written);
    return result;
}

std::filesystem::path applicationDataFolder() {
    const std::wstring localAppData = environmentValue(L"LOCALAPPDATA");
    if (!localAppData.empty()) {
        return std::filesystem::path(localAppData) / L"ETLFragFinder";
    }
    return modulePath().parent_path() / L"data";
}

void initializePersistentStorage() {
    gApp.dataFolder = applicationDataFolder();
    gApp.iniPath = gApp.dataFolder / L"settings.ini";
    gApp.indexPath = gApp.dataFolder / L"demo-index-v3.sqlite3";
    gApp.highlightsPath = gApp.dataFolder / L"highlights-v1.bin";

    std::error_code folderError;
    std::filesystem::create_directories(gApp.dataFolder, folderError);
    if (folderError) {
        setStatus(L"The application data folder could not be created.");
    }
    std::filesystem::path legacyIni = modulePath();
    legacyIni.replace_extension(L".ini");
    std::error_code iniError;
    if (!std::filesystem::exists(gApp.iniPath, iniError) && !iniError &&
        std::filesystem::is_regular_file(legacyIni, iniError) && !iniError) {
        std::filesystem::copy_file(
            legacyIni,
            gApp.iniPath,
            std::filesystem::copy_options::skip_existing,
            iniError);
    }

    std::string indexError;
    if (!gApp.demoIndex.open(gApp.indexPath, indexError)) {
        setStatus(L"The SQLite demo index could not be opened: " + toWide(indexError));
    } else {
        std::string sizeError;
        const std::size_t indexed = gApp.demoIndex.size(sizeError);
        if (sizeError.empty()) gApp.indexedDemoCount = indexed;
    }

    std::string highlightError;
    if (!etlfrag::loadHighlights(gApp.highlightsPath, gApp.highlights, highlightError)) {
        gApp.highlights.clear();
        setStatus(L"The saved highlight basket could not be loaded; a new basket will be created.");
    }
}

void refreshIndexedDemoCount() {
    std::string error;
    const std::size_t count = gApp.demoIndex.size(error);
    if (error.empty()) gApp.indexedDemoCount = count;
}

bool saveEtlPath() {
    return WritePrivateProfileStringW(
               L"ETLegacy", L"Executable", gApp.etlExecutable.c_str(), gApp.iniPath.c_str()) != FALSE;
}

bool savePlaybackSettings() {
    const BOOL administratorSaved = WritePrivateProfileStringW(
        L"Playback",
        L"LaunchAsAdministrator",
        gApp.launchEtlAsAdministrator ? L"1" : L"0",
        gApp.iniPath.c_str());
    const BOOL noSeekSaved = WritePrivateProfileStringW(
        L"Playback",
        L"LaunchWithoutSeeking",
        gApp.launchWithoutSeeking ? L"1" : L"0",
        gApp.iniPath.c_str());
    return administratorSaved != FALSE && noSeekSaved != FALSE;
}

void updatePlaybackButtonLabels() {
    const wchar_t* runLabel =
        gApp.launchWithoutSeeking ? L"Play selected  (no seek)" : L"Play selected  (−5s)";
    const wchar_t* eventLabel = gApp.launchWithoutSeeking
                                    ? L"Play selected event  (no seek)"
                                    : L"Play selected event  (−5s)";
    for (HWND control : {gApp.playRun, gApp.playFolderRun, gApp.playHighlight}) {
        if (control != nullptr) SetWindowTextW(control, runLabel);
    }
    if (gApp.playEvent != nullptr) SetWindowTextW(gApp.playEvent, eventLabel);
}

void findEtlExecutable() {
    std::wstring configured(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(
        L"ETLegacy",
        L"Executable",
        L"",
        configured.data(),
        static_cast<DWORD>(configured.size()),
        gApp.iniPath.c_str());
    configured.resize(length);
    if (!configured.empty() && std::filesystem::is_regular_file(configured)) {
        gApp.etlExecutable = configured;
        return;
    }

    std::vector<std::filesystem::path> candidates = {modulePath().parent_path() / L"etl.exe"};
    const std::wstring programFiles = environmentValue(L"ProgramFiles");
    const std::wstring programFilesX86 = environmentValue(L"ProgramFiles(x86)");
    const std::wstring localAppData = environmentValue(L"LOCALAPPDATA");
    for (const std::wstring& base : {programFiles, programFilesX86}) {
        if (!base.empty()) {
            candidates.emplace_back(std::filesystem::path(base) / L"ETLegacy" / L"etl.exe");
            candidates.emplace_back(std::filesystem::path(base) / L"ET Legacy" / L"etl.exe");
        }
    }
    if (!localAppData.empty()) {
        candidates.emplace_back(std::filesystem::path(localAppData) / L"ETLegacy" / L"etl.exe");
    }
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            gApp.etlExecutable = candidate;
            saveEtlPath();
            return;
        }
    }
}

bool chooseEtlExecutable() {
    std::wstring file(32768, L'\0');
    if (!gApp.etlExecutable.empty()) {
        const std::wstring current = gApp.etlExecutable.wstring();
        std::copy(current.begin(), current.end(), file.begin());
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gApp.window;
    dialog.lpstrFilter = L"ET: Legacy (etl.exe)\0etl.exe\0Applications (*.exe)\0*.exe\0\0";
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrTitle = L"Locate ET: Legacy executable";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return false;
    }
    gApp.etlExecutable = std::filesystem::path(file.c_str());
    if (!saveEtlPath()) {
        MessageBoxW(
            gApp.window,
            L"The ET: Legacy path could not be saved in the application settings folder.",
            L"Settings warning",
            MB_OK | MB_ICONWARNING);
    }
    setStatus(L"ET: Legacy executable: " + gApp.etlExecutable.wstring());
    return true;
}

std::optional<std::filesystem::path> chooseDemo() {
    std::wstring file(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gApp.window;
    dialog.lpstrFilter = L"ET: Legacy demos (*.dm_84)\0*.dm_84\0All files\0*.*\0\0";
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrTitle = L"Open an ET: Legacy demo";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::filesystem::path(file.c_str());
}

std::optional<std::filesystem::path> chooseDemoFolder() {
    IFileOpenDialog* dialog = nullptr;
    const HRESULT created = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(created) || dialog == nullptr) {
        MessageBoxW(
            gApp.window,
            L"The folder selection dialog could not be opened.",
            L"Folder selection error",
            MB_OK | MB_ICONERROR);
        return std::nullopt;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(
        options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
        FOS_DONTADDTORECENT);
    dialog->SetTitle(L"Choose a folder containing ET: Legacy demos");

    std::error_code initialFolderError;
    if (!gApp.demoFolder.empty() &&
        std::filesystem::is_directory(gApp.demoFolder, initialFolderError) &&
        !initialFolderError) {
        IShellItem* initialFolder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                gApp.demoFolder.c_str(), nullptr, IID_PPV_ARGS(&initialFolder))) &&
            initialFolder != nullptr) {
            dialog->SetFolder(initialFolder);
            initialFolder->Release();
        }
    }

    const HRESULT shown = dialog->Show(gApp.window);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return std::nullopt;
    }
    if (FAILED(shown)) {
        dialog->Release();
        MessageBoxW(
            gApp.window,
            L"The selected folder could not be opened.",
            L"Folder selection error",
            MB_OK | MB_ICONERROR);
        return std::nullopt;
    }

    IShellItem* item = nullptr;
    PWSTR selectedPath = nullptr;
    const HRESULT result = dialog->GetResult(&item);
    if (SUCCEEDED(result) && item != nullptr) {
        item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
    }
    std::optional<std::filesystem::path> folder;
    if (selectedPath != nullptr) {
        folder = std::filesystem::path(selectedPath);
        CoTaskMemFree(selectedPath);
    }
    if (item != nullptr) {
        item->Release();
    }
    dialog->Release();
    return folder;
}

int inspectorScale(const ProtocolInspectorState& state, int value) {
    return MulDiv(value, state.dpi, 96);
}

void recreateInspectorFonts(ProtocolInspectorState& state) {
    for (HFONT font : {state.uiFont, state.buttonFont, state.monoFont, state.smallFont}) {
        if (font != nullptr) DeleteObject(font);
    }
    state.uiFont = CreateFontW(
        -inspectorScale(state, 15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    state.buttonFont = CreateFontW(
        -inspectorScale(state, 14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
    state.monoFont = CreateFontW(
        -inspectorScale(state, 13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    state.smallFont = CreateFontW(
        -inspectorScale(state, 12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND control : {state.description, state.query}) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.uiFont), TRUE);
        }
    }
    for (HWND control : {state.findNext, state.saveText}) {
        if (control != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.buttonFont), TRUE);
        }
    }
    if (state.text != nullptr) {
        SendMessageW(state.text, WM_SETFONT, reinterpret_cast<WPARAM>(state.monoFont), TRUE);
    }
    if (state.status != nullptr) {
        SendMessageW(state.status, WM_SETFONT, reinterpret_cast<WPARAM>(state.smallFont), TRUE);
    }
}

void layoutProtocolInspector(ProtocolInspectorState& state) {
    RECT client{};
    GetClientRect(state.window, &client);
    const int margin = inspectorScale(state, 16);
    const int gap = inspectorScale(state, 10);
    const int descriptionHeight = inspectorScale(state, 24);
    const int controlHeight = inspectorScale(state, 34);
    const int statusHeight = inspectorScale(state, 25);
    const int buttonWidth = inspectorScale(state, 120);
    const int toolbarY = margin + descriptionHeight + inspectorScale(state, 6);
    MoveWindow(
        state.description,
        margin,
        margin,
        std::max(1, static_cast<int>(client.right) - margin * 2),
        descriptionHeight,
        TRUE);
    MoveWindow(
        state.saveText,
        client.right - margin - buttonWidth,
        toolbarY,
        buttonWidth,
        controlHeight,
        TRUE);
    MoveWindow(
        state.findNext,
        client.right - margin - buttonWidth * 2 - gap,
        toolbarY,
        buttonWidth,
        controlHeight,
        TRUE);
    MoveWindow(
        state.query,
        margin,
        toolbarY,
        std::max(
            1,
            static_cast<int>(client.right) - margin * 2 - buttonWidth * 2 - gap * 2),
        controlHeight,
        TRUE);
    const int textY = toolbarY + controlHeight + gap;
    MoveWindow(
        state.text,
        margin,
        textY,
        std::max(1, static_cast<int>(client.right) - margin * 2),
        std::max(1, static_cast<int>(client.bottom) - textY - statusHeight - gap),
        TRUE);
    MoveWindow(
        state.status,
        margin,
        client.bottom - statusHeight,
        std::max(1, static_cast<int>(client.right) - margin * 2),
        statusHeight,
        TRUE);
}

void updateProtocolInspectorStatus(ProtocolInspectorState& state, const std::wstring& suffix = {}) {
    std::wostringstream status;
    status << state.lineCount << L" lines • " << std::fixed << std::setprecision(1)
           << static_cast<double>(state.utf8Text.size()) / (1024.0 * 1024.0)
           << L" MiB • decoded protocol + complete raw message hex";
    if (!suffix.empty()) status << L" • " << suffix;
    SetWindowTextW(state.status, status.str().c_str());
}

void findNextProtocolText(ProtocolInspectorState& state) {
    const int queryLength = GetWindowTextLengthW(state.query);
    std::wstring query(static_cast<std::size_t>(std::max(0, queryLength)) + 1, L'\0');
    GetWindowTextW(state.query, query.data(), static_cast<int>(query.size()));
    query.resize(static_cast<std::size_t>(std::max(0, queryLength)));
    if (query.empty()) {
        SetFocus(state.query);
        updateProtocolInspectorStatus(state, L"enter text to search");
        return;
    }

    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessageW(
        state.text,
        EM_GETSEL,
        reinterpret_cast<WPARAM>(&selectionStart),
        reinterpret_cast<LPARAM>(&selectionEnd));
    const auto equalIgnoringCase = [](wchar_t left, wchar_t right) {
        return std::towlower(left) == std::towlower(right);
    };
    const std::size_t start = std::min<std::size_t>(selectionEnd, state.wideText.size());
    auto found = std::search(
        state.wideText.begin() + static_cast<std::ptrdiff_t>(start),
        state.wideText.end(),
        query.begin(),
        query.end(),
        equalIgnoringCase);
    bool wrapped = false;
    if (found == state.wideText.end() && start > 0) {
        found = std::search(
            state.wideText.begin(),
            state.wideText.begin() + static_cast<std::ptrdiff_t>(start),
            query.begin(),
            query.end(),
            equalIgnoringCase);
        wrapped = found != state.wideText.begin() + static_cast<std::ptrdiff_t>(start);
    }
    if (found == state.wideText.end() ||
        (start > 0 && found == state.wideText.begin() + static_cast<std::ptrdiff_t>(start))) {
        MessageBeep(MB_ICONINFORMATION);
        updateProtocolInspectorStatus(state, L"text not found");
        return;
    }

    const std::size_t position = static_cast<std::size_t>(found - state.wideText.begin());
    SendMessageW(
        state.text,
        EM_SETSEL,
        static_cast<WPARAM>(position),
        static_cast<LPARAM>(position + query.size()));
    SendMessageW(state.text, EM_SCROLLCARET, 0, 0);
    SetFocus(state.text);
    updateProtocolInspectorStatus(
        state,
        L"match at character " + std::to_wstring(position) + (wrapped ? L" (wrapped)" : L""));
}

void saveProtocolInspectorText(ProtocolInspectorState& state) {
    std::wstring file(32768, L'\0');
    const std::wstring suggested = state.demoPath.filename().wstring() + L".protocol.txt";
    std::copy(suggested.begin(), suggested.end(), file.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state.window;
    dialog.lpstrFilter = L"Text log (*.txt)\0*.txt\0All files\0*.*\0\0";
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrDefExt = L"txt";
    dialog.lpstrTitle = L"Save full demo protocol log";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) return;

    std::ofstream output(std::filesystem::path(file.c_str()), std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(
            state.window,
            L"The protocol log file could not be created.",
            L"Save error",
            MB_OK | MB_ICONERROR);
        return;
    }
    output.write("\xEF\xBB\xBF", 3);
    output.write(state.utf8Text.data(), static_cast<std::streamsize>(state.utf8Text.size()));
    if (!output) {
        MessageBoxW(
            state.window,
            L"The protocol log could not be written completely.",
            L"Save error",
            MB_OK | MB_ICONERROR);
        return;
    }
    updateProtocolInspectorStatus(state, L"saved to " + std::filesystem::path(file.c_str()).filename().wstring());
}

LRESULT CALLBACK protocolInspectorProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {
    auto* state = reinterpret_cast<ProtocolInspectorState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<ProtocolInspectorState*>(create->lpCreateParams);
        if (state == nullptr) return FALSE;
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_CREATE: {
            state->dpi = dpiForWindow(window);
            state->description = CreateWindowExW(
                0,
                WC_STATICW,
                L"Chronological protocol inspector — decoded records and every original message byte.",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                0, 0, 10, 10, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            state->query = CreateWindowExW(
                0,
                WC_EDITW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
                0, 0, 10, 10, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProtocolQueryId)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(
                state->query,
                EM_SETCUEBANNER,
                TRUE,
                reinterpret_cast<LPARAM>(L"Search server commands, configstrings, players, events or raw hex"));
            state->findNext = CreateWindowExW(
                0,
                WC_BUTTONW,
                L"Find next",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProtocolFindId)),
                GetModuleHandleW(nullptr), nullptr);
            state->saveText = CreateWindowExW(
                0,
                WC_BUTTONW,
                L"Save text…",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProtocolSaveId)),
                GetModuleHandleW(nullptr), nullptr);
            state->text = CreateWindowExW(
                0,
                WC_EDITW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
                    ES_LEFT | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                    ES_NOHIDESEL,
                0, 0, 10, 10, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProtocolTextId)),
                GetModuleHandleW(nullptr), nullptr);
            state->status = CreateWindowExW(
                0,
                WC_STATICW,
                L"",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                0, 0, 10, 10, window, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(state->text, EM_SETLIMITTEXT, 0x7ffffffe, 0);
            SetWindowTheme(state->query, L"DarkMode_CFD", nullptr);
            SetWindowTheme(state->text, L"DarkMode_CFD", nullptr);
            SetWindowTheme(state->findNext, L"DarkMode_Explorer", nullptr);
            SetWindowTheme(state->saveText, L"DarkMode_Explorer", nullptr);
            recreateInspectorFonts(*state);
            SetWindowTextW(state->text, state->wideText.c_str());
            updateProtocolInspectorStatus(*state);
            layoutProtocolInspector(*state);
            const BOOL dark = TRUE;
            if (FAILED(DwmSetWindowAttribute(window, 20, &dark, sizeof(dark)))) {
                DwmSetWindowAttribute(window, 19, &dark, sizeof(dark));
            }
            return 0;
        }
        case WM_SIZE:
            layoutProtocolInspector(*state);
            return 0;
        case WM_DPICHANGED: {
            const int newDpi = HIWORD(wParam);
            if (newDpi > 0 && newDpi != state->dpi) {
                state->dpi = newDpi;
                recreateInspectorFonts(*state);
            }
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                SetWindowPos(
                    window,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            layoutProtocolInspector(*state);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kProtocolFindId) {
                findNextProtocolText(*state);
                return 0;
            }
            if (LOWORD(wParam) == kProtocolSaveId) {
                saveProtocolInspectorText(*state);
                return 0;
            }
            if (LOWORD(wParam) == kProtocolQueryId && HIWORD(wParam) == EN_UPDATE &&
                (GetKeyState(VK_RETURN) & 0x8000) != 0) {
                findNextProtocolText(*state);
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH brush = CreateSolidBrush(kBackground);
            FillRect(dc, &client, brush);
            DeleteObject(brush);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            if (reinterpret_cast<HWND>(lParam) == state->text) {
                SetTextColor(dc, kText);
                SetBkColor(dc, kControl);
                return reinterpret_cast<LRESULT>(gApp.controlBrush);
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, reinterpret_cast<HWND>(lParam) == state->description ? kText : kMuted);
            return reinterpret_cast<LRESULT>(gApp.backgroundBrush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kControl);
            return reinterpret_cast<LRESULT>(gApp.controlBrush);
        }
        case WM_DESTROY:
            if (gApp.protocolInspector == window) gApp.protocolInspector = nullptr;
            return 0;
        case WM_NCDESTROY:
            for (HFONT font : {state->uiFont, state->buttonFont, state->monoFont, state->smallFont}) {
                if (font != nullptr) DeleteObject(font);
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            delete state;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void openProtocolInspector() {
    if (!gApp.demoLoaded || !std::filesystem::is_regular_file(gApp.demo.path)) {
        MessageBoxW(
            gApp.window,
            L"Open a valid demo before viewing its full protocol log.",
            L"No demo loaded",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (gApp.protocolInspector != nullptr) {
        DestroyWindow(gApp.protocolInspector);
        gApp.protocolInspector = nullptr;
    }

    const HCURSOR previousCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    setStatus(L"Decoding the complete demo protocol and raw message data…");
    UpdateWindow(gApp.window);
    try {
        etlfrag::DemoParseOptions options;
        options.collectProtocolLog = true;
        etlfrag::DemoInfo detailed = etlfrag::DemoParser{}.parse(gApp.demo.path, options);
        auto state = std::make_unique<ProtocolInspectorState>();
        state->demoPath = gApp.demo.path;
        state->utf8Text =
            "ET: Legacy Frag Finder by ght — Full demo protocol inspector\r\n"
            "File: " + toUtf8(gApp.demo.path.wstring()) + "\r\n"
            "Map: " + detailed.mapName + " | POV: " + detailed.povName + "\r\n"
            "The RAW rows are a complete hexadecimal representation of every demo message payload.\r\n"
            "Decoded rows expose gamestate, configstrings, baselines, server commands, snapshots, "
            "player state, entity state, downloads and obituary events in file order.\r\n\r\n" +
            detailed.protocolLog;
        state->lineCount = static_cast<std::size_t>(
            std::count(state->utf8Text.begin(), state->utf8Text.end(), '\n'));
        state->wideText = toWide(state->utf8Text);

        const int dpi = dpiForWindow(gApp.window);
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        const int width = std::min(
            MulDiv(1220, dpi, 96),
            std::max(
                MulDiv(760, dpi, 96),
                static_cast<int>(workArea.right - workArea.left)));
        const int height = std::min(
            MulDiv(820, dpi, 96),
            std::max(
                MulDiv(560, dpi, 96),
                static_cast<int>(workArea.bottom - workArea.top)));
        const std::wstring title =
            L"Full demo protocol — " + gApp.demo.path.filename().wstring();
        HWND inspector = CreateWindowExW(
            WS_EX_APPWINDOW,
            kProtocolInspectorClass,
            title.c_str(),
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            width,
            height,
            gApp.window,
            nullptr,
            GetModuleHandleW(nullptr),
            state.get());
        if (inspector == nullptr) {
            throw std::runtime_error("The protocol inspector window could not be created");
        }
        state.release();
        gApp.protocolInspector = inspector;
        ShowWindow(inspector, SW_SHOW);
        UpdateWindow(inspector);
        setStatus(L"Full protocol inspector opened for " + gApp.demo.path.filename().wstring());
    } catch (const std::exception& error) {
        MessageBoxW(
            gApp.window,
            (L"The full demo protocol could not be decoded.\n\n" + toWide(error.what())).c_str(),
            L"Protocol inspector error",
            MB_OK | MB_ICONERROR);
        setStatus(L"Could not open the full demo protocol inspector.");
    }
    SetCursor(previousCursor);
}

bool hasDemoExtension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return extension == L".dm_84";
}

std::vector<std::filesystem::path> findDemoFiles(
    const std::filesystem::path& folder,
    const std::shared_ptr<std::atomic_bool>& cancelRequested) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        folder,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (cancelRequested != nullptr && cancelRequested->load()) {
            break;
        }
        if (!error) {
            std::error_code fileError;
            if (iterator->is_regular_file(fileError) && !fileError &&
                hasDemoExtension(iterator->path())) {
                files.push_back(iterator->path());
            }
        }
        iterator.increment(error);
        if (error) {
            error.clear();
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool refreshFolderDemos(bool reportError) {
    std::string error;
    gApp.folderDemos = gApp.demoIndex.listFolder(gApp.demoFolder, error);
    if (error.empty()) return true;
    gApp.folderDemos.clear();
    if (reportError) {
        MessageBoxW(
            gApp.window,
            toWide(error).c_str(),
            L"Could not read demo index",
            MB_OK | MB_ICONERROR);
    }
    return false;
}

unsigned __stdcall folderScanWorker(void* rawRequest) {
    std::unique_ptr<FolderScanRequest> request(
        static_cast<FolderScanRequest*>(rawRequest));
    auto outcome = std::make_unique<FolderScanOutcome>();
    outcome->folder = request->folder;
    outcome->automatic = request->automatic;

    etlfrag::SqliteDemoIndex indexDatabase;
    if (!indexDatabase.open(request->indexPath, outcome->fatalError)) {
        FolderScanOutcome* completed = outcome.release();
        if (!PostMessageW(
                request->window,
                kFolderScanCompleteMessage,
                0,
                reinterpret_cast<LPARAM>(completed))) {
            delete completed;
        }
        return 0;
    }

    const std::vector<std::filesystem::path> files =
        findDemoFiles(request->folder, request->cancelRequested);
    outcome->filesFound = files.size();
    if (request->cancelRequested != nullptr && request->cancelRequested->load()) {
        outcome->cancelled = true;
    }
    auto lastProgress = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    for (std::size_t fileIndex = 0; fileIndex < files.size(); ++fileIndex) {
        if (request->cancelRequested != nullptr && request->cancelRequested->load()) {
            outcome->cancelled = true;
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if (fileIndex == 0 || fileIndex + 1 == files.size() ||
            now - lastProgress >= std::chrono::milliseconds(120)) {
            PostMessageW(
                request->window,
                kFolderScanProgressMessage,
                static_cast<WPARAM>(fileIndex + 1),
                static_cast<LPARAM>(files.size()));
            lastProgress = now;
        }
        try {
            etlfrag::DemoInfo demo;
            std::string cacheError;
            std::optional<etlfrag::DemoInfo> cached =
                indexDatabase.findFresh(files[fileIndex], cacheError);
            if (!cacheError.empty()) {
                throw std::runtime_error(cacheError);
            }
            if (cached.has_value()) {
                demo = std::move(*cached);
                demo.path = files[fileIndex];
                ++outcome->filesLoadedFromIndex;
            } else {
                demo = etlfrag::DemoParser{}.parse(files[fileIndex]);
                ++outcome->filesParsed;
                ++outcome->filesNewOrChanged;
                std::string upsertError;
                if (!indexDatabase.upsert(files[fileIndex], demo, upsertError)) {
                    throw std::runtime_error(upsertError);
                }
            }
            if (demo.povClientNum < 0) {
                ++outcome->filesWithoutPov;
            }
            if (!demo.warnings.empty()) {
                ++outcome->filesWithWarnings;
            }
        } catch (const std::exception& error) {
            ++outcome->filesFailed;
            if (outcome->failures.size() < 20) {
                outcome->failures.push_back(
                    toUtf8(files[fileIndex].filename().wstring()) + " — " + error.what());
            }
        } catch (...) {
            ++outcome->filesFailed;
            if (outcome->failures.size() < 20) {
                outcome->failures.push_back(
                    toUtf8(files[fileIndex].filename().wstring()) + " — unknown parser error");
            }
        }
    }

    if (!outcome->cancelled) {
        std::string pruneError;
        if (!indexDatabase.pruneMissingInFolder(
                request->folder,
                outcome->staleEntriesRemoved,
                pruneError)) {
            outcome->fatalError = pruneError;
        }
    }

    FolderScanOutcome* completed = outcome.release();
    if (!PostMessageW(
            request->window,
            kFolderScanCompleteMessage,
            0,
            reinterpret_cast<LPARAM>(completed))) {
        delete completed;
    }
    return 0;
}

unsigned __stdcall folderWatchWorker(void* rawRequest) {
    std::unique_ptr<FolderWatchRequest> request(
        static_cast<FolderWatchRequest*>(rawRequest));
    HANDLE directory = CreateFileW(
        request->folder.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE) return 0;

    HANDLE changedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (changedEvent == nullptr) {
        CloseHandle(directory);
        return 0;
    }
    std::vector<unsigned char> buffer(32768);
    for (;;) {
        ResetEvent(changedEvent);
        OVERLAPPED operation{};
        operation.hEvent = changedEvent;
        if (!ReadDirectoryChangesW(
                directory,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr,
                &operation,
                nullptr)) {
            break;
        }
        HANDLE waits[2]{request->stopEvent, changedEvent};
        const DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            CancelIoEx(directory, &operation);
            break;
        }
        if (waitResult != WAIT_OBJECT_0 + 1) {
            CancelIoEx(directory, &operation);
            break;
        }
        DWORD bytes = 0;
        if (!GetOverlappedResult(directory, &operation, &bytes, FALSE)) {
            if (GetLastError() == ERROR_OPERATION_ABORTED) break;
            continue;
        }
        if (bytes > 0) {
            PostMessageW(request->window, kFolderChangedMessage, 0, 0);
        }
    }
    CloseHandle(changedEvent);
    CloseHandle(directory);
    return 0;
}

void stopFolderWatcher() {
    KillTimer(gApp.window, kFolderWatchDebounceTimer);
    if (gApp.folderWatchStopEvent != nullptr) {
        SetEvent(gApp.folderWatchStopEvent);
    }
    if (gApp.folderWatchThread != nullptr) {
        WaitForSingleObject(gApp.folderWatchThread, INFINITE);
        CloseHandle(gApp.folderWatchThread);
        gApp.folderWatchThread = nullptr;
    }
    if (gApp.folderWatchStopEvent != nullptr) {
        CloseHandle(gApp.folderWatchStopEvent);
        gApp.folderWatchStopEvent = nullptr;
    }
}

void startFolderWatcher() {
    stopFolderWatcher();
    std::error_code error;
    if (!gApp.folderWatchEnabled || gApp.demoFolder.empty() ||
        !std::filesystem::is_directory(gApp.demoFolder, error) || error) {
        return;
    }
    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent == nullptr) return;
    auto request = std::make_unique<FolderWatchRequest>();
    request->window = gApp.window;
    request->folder = gApp.demoFolder;
    request->stopEvent = stopEvent;
    const std::uintptr_t threadValue = _beginthreadex(
        nullptr, 0, folderWatchWorker, request.get(), 0, nullptr);
    if (threadValue == 0) {
        CloseHandle(stopEvent);
        return;
    }
    request.release();
    gApp.folderWatchStopEvent = stopEvent;
    gApp.folderWatchThread = reinterpret_cast<HANDLE>(threadValue);
}

std::wstring eventType(const etlfrag::KillEvent& kill) {
    std::wstring type;
    if (kill.suicide) {
        type = L"SUICIDE";
    } else if (kill.attacker < 0) {
        type = L"WORLD";
    } else if (kill.teamKill) {
        type = L"TEAMKILL";
    } else {
        type = L"KILL";
    }
    if (kill.headshot) {
        type += L"  •  HEADSHOT KILL";
    }
    if (kill.matchPhase == etlfrag::MatchPhase::Warmup) {
        type += L"  •  WARMUP";
    } else if (kill.matchPhase == etlfrag::MatchPhase::Intermission) {
        type += L"  •  INTERMISSION";
    } else if (kill.matchPhase == etlfrag::MatchPhase::Unknown) {
        type += L"  •  PHASE UNKNOWN";
    }
    return type;
}

PlayerSelection selectedEventPlayer() {
    if (gApp.eventPlayer == nullptr) {
        return {};
    }
    const int selection = static_cast<int>(
        SendMessageW(gApp.eventPlayer, CB_GETCURSEL, 0, 0));
    if (selection < 0 || selection >= static_cast<int>(gApp.eventPlayerIds.size())) {
        return {};
    }
    return gApp.eventPlayerIds[static_cast<std::size_t>(selection)];
}

int selectedEventPlayerId() {
    return selectedEventPlayer().clientNum;
}

bool eventInvolvesPlayer(const etlfrag::KillEvent& kill, const PlayerSelection& player) {
    if (player.clientNum < 0) return true;
    const bool attacker = kill.attacker == player.clientNum &&
                          (player.sessionId < 0 || kill.attackerSessionId == player.sessionId);
    const bool target = kill.target == player.clientNum &&
                        (player.sessionId < 0 || kill.targetSessionId == player.sessionId);
    return attacker || target;
}

std::wstring playerDisplayName(int clientNum, int sessionId = -1) {
    for (const etlfrag::Player& player : gApp.demo.players) {
        if (player.clientNum == clientNum &&
            (sessionId < 0 || player.sessionId == sessionId)) {
            return toWide(player.cleanName);
        }
    }
    return L"Player #" + std::to_wstring(clientNum);
}

std::size_t visibleEventCount() {
    const PlayerSelection player = selectedEventPlayer();
    return static_cast<std::size_t>(std::count_if(
        gApp.demo.kills.begin(),
        gApp.demo.kills.end(),
        [player](const etlfrag::KillEvent& kill) {
            return eventInvolvesPlayer(kill, player);
        }));
}

void updateTabLabels() {
    std::wstring multi = L"Multi-kill finder";
    std::wstring events = L"All kills / events";
    std::wstring folder = L"Folder scan";
    std::wstring highlights = L"Highlights  (" + std::to_wstring(gApp.highlights.size()) + L")";
    std::wstring library = L"Demo library";
    if (gApp.demoLoaded) {
        multi += L"  (" + std::to_wstring(gApp.runs.size()) + L")";
        const std::size_t visibleEvents = visibleEventCount();
        events += L"  (" + std::to_wstring(visibleEvents);
        if (selectedEventPlayerId() >= 0) {
            events += L" / " + std::to_wstring(gApp.demo.kills.size());
        }
        events += L")";
    }
    if (gApp.folderFilesFound > 0 || !gApp.folderRows.empty()) {
        folder += L"  (" + std::to_wstring(gApp.folderRows.size()) + L")";
    }
    if (!gApp.libraryRows.empty()) {
        library += L"  (" + std::to_wstring(gApp.libraryRows.size()) + L")";
    }
    SetWindowTextW(gApp.tabMultiKills, multi.c_str());
    SetWindowTextW(gApp.tabAllEvents, events.c_str());
    SetWindowTextW(gApp.tabFolderScan, folder.c_str());
    SetWindowTextW(gApp.tabHighlights, highlights.c_str());
    SetWindowTextW(gApp.tabLibrary, library.c_str());
}

void populateRunKillDetails(int runIndex) {
    ListView_DeleteAllItems(gApp.runKillList);
    if (runIndex < 0 || runIndex >= static_cast<int>(gApp.runs.size())) {
        return;
    }
    const etlfrag::FragRun& run = gApp.runs[static_cast<std::size_t>(runIndex)];
    for (const std::size_t killIndex : run.killIndices) {
        const etlfrag::KillEvent& kill = gApp.demo.kills[killIndex];
        const int row = addListRow(
            gApp.runKillList, durationText(kill.demoTimeMs), static_cast<LPARAM>(killIndex));
        setListCell(
            gApp.runKillList,
            row,
            1,
            kill.matchRemainingMs >= 0 ? durationText(kill.matchRemainingMs, false) : L"—");
        setListCell(gApp.runKillList, row, 2, toWide(kill.targetName));
        setListCell(gApp.runKillList, row, 3, toWide(etlfrag::weaponName(kill.weapon)));
        setListCell(gApp.runKillList, row, 4, eventType(kill));
    }
    applyListSort(gApp.runKillList, gApp.runKillSort);
}

void populateAllEvents() {
    ListView_DeleteAllItems(gApp.allEventList);
    const PlayerSelection player = selectedEventPlayer();
    for (std::size_t index = 0; index < gApp.demo.kills.size(); ++index) {
        const etlfrag::KillEvent& kill = gApp.demo.kills[index];
        if (!eventInvolvesPlayer(kill, player)) {
            continue;
        }
        const int row = addListRow(
            gApp.allEventList,
            std::to_wstring(index + 1),
            static_cast<LPARAM>(index));
        setListCell(gApp.allEventList, row, 1, durationText(kill.demoTimeMs));
        setListCell(
            gApp.allEventList,
            row,
            2,
            kill.matchRemainingMs >= 0 ? durationText(kill.matchRemainingMs, false) : L"—");
        setListCell(gApp.allEventList, row, 3, toWide(kill.attackerName));
        setListCell(gApp.allEventList, row, 4, toWide(kill.targetName));
        setListCell(gApp.allEventList, row, 5, toWide(etlfrag::weaponName(kill.weapon)));
        setListCell(gApp.allEventList, row, 6, eventType(kill));
    }
    applyListSort(gApp.allEventList, gApp.allEventSort);
    const bool hasEvents = ListView_GetItemCount(gApp.allEventList) > 0;
    if (hasEvents) {
        ListView_SetItemState(
            gApp.allEventList,
            0,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
    }
    EnableWindow(gApp.playEvent, hasEvents);
    updateTabLabels();
    InvalidateRect(gApp.timeline, nullptr, FALSE);
}

void populateFolderWeapons() {
    SendMessageW(gApp.folderWeapon, CB_RESETCONTENT, 0, 0);
    gApp.folderWeaponIds.clear();
    SendMessageW(
        gApp.folderWeapon,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"All weapons"));
    gApp.folderWeaponIds.push_back(-1);
    for (int weapon = 1; weapon <= 55; ++weapon) {
        const std::wstring name = toWide(etlfrag::weaponName(weapon));
        SendMessageW(
            gApp.folderWeapon,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(name.c_str()));
        gApp.folderWeaponIds.push_back(weapon);
    }
    SendMessageW(gApp.folderWeapon, CB_SETCURSEL, 0, 0);
}

void populatePostDeathWindows(HWND combo) {
    for (const wchar_t* value : {L"3.0", L"5.0", L"8.0"}) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    SendMessageW(combo, CB_SETCURSEL, 1, 0);
    SendMessageW(combo, CB_LIMITTEXT, 12, 0);
}

void loadFolderTimelineForResult(std::size_t resultIndex);

void populateFolderKillDetails(int rowIndex) {
    ListView_DeleteAllItems(gApp.folderKillList);
    gApp.folderKillRows.clear();
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gApp.folderRows.size())) {
        EnableWindow(gApp.playFolderRun, FALSE);
        EnableWindow(gApp.addFolderHighlight, FALSE);
        return;
    }

    const std::size_t runIndex = gApp.folderRows[static_cast<std::size_t>(rowIndex)];
    if (runIndex >= gApp.folderRuns.size()) {
        EnableWindow(gApp.playFolderRun, FALSE);
        EnableWindow(gApp.addFolderHighlight, FALSE);
        return;
    }
    const FolderRunResult& result = gApp.folderRuns[runIndex];
    for (std::size_t killIndex = 0; killIndex < result.kills.size(); ++killIndex) {
        const etlfrag::KillEvent& kill = result.kills[killIndex];
        const int detailIndex = static_cast<int>(gApp.folderKillRows.size());
        gApp.folderKillRows.emplace_back(runIndex, killIndex);
        const int row = addListRow(
            gApp.folderKillList,
            durationText(kill.demoTimeMs),
            static_cast<LPARAM>(detailIndex));
        setListCell(
            gApp.folderKillList,
            row,
            1,
            kill.matchRemainingMs >= 0 ? durationText(kill.matchRemainingMs, false) : L"—");
        setListCell(gApp.folderKillList, row, 2, toWide(kill.targetName));
        setListCell(
            gApp.folderKillList,
            row,
            3,
            toWide(etlfrag::weaponName(kill.weapon)));
        setListCell(gApp.folderKillList, row, 4, eventType(kill));
    }
    applyListSort(gApp.folderKillList, gApp.folderKillSort);
    loadFolderTimelineForResult(runIndex);
    EnableWindow(gApp.playFolderRun, TRUE);
    EnableWindow(gApp.addFolderHighlight, TRUE);
}

void loadFolderTimelineForResult(std::size_t resultIndex) {
    gApp.folderTimelineSelectedRun = static_cast<std::size_t>(-1);
    if (resultIndex >= gApp.folderRuns.size()) return;
    const FolderRunResult& selected = gApp.folderRuns[resultIndex];
    if (gApp.folderTimelineDemoId != selected.demo.id) {
        std::string error;
        std::optional<etlfrag::DemoInfo> loaded =
            gApp.demoIndex.loadById(selected.demo.id, error);
        if (!loaded.has_value()) {
            gApp.folderTimelineDemo = {};
            gApp.folderTimelineRuns.clear();
            gApp.folderTimelineDemoId = -1;
            if (!error.empty()) setStatus(L"Could not load the selected timeline: " + toWide(error));
            return;
        }
        gApp.folderTimelineDemo = std::move(*loaded);
        etlfrag::RunFilter filter = gApp.folderAppliedFilter;
        filter.playerClientNum = gApp.folderTimelineDemo.povClientNum;
        filter.playerSessionId = -1;
        try {
            gApp.folderTimelineRuns =
                etlfrag::findFragRuns(gApp.folderTimelineDemo, filter);
            gApp.folderTimelineDemoId = selected.demo.id;
        } catch (const std::exception& errorValue) {
            gApp.folderTimelineDemo = {};
            gApp.folderTimelineRuns.clear();
            gApp.folderTimelineDemoId = -1;
            setStatus(L"Could not build the selected timeline: " + toWide(errorValue.what()));
            return;
        }
    }
    for (std::size_t index = 0; index < gApp.folderTimelineRuns.size(); ++index) {
        const etlfrag::FragRun& candidate = gApp.folderTimelineRuns[index];
        if (candidate.startDemoTimeMs == selected.run.startDemoTimeMs &&
            candidate.endDemoTimeMs == selected.run.endDemoTimeMs &&
            candidate.attacker == selected.run.attacker &&
            candidate.attackerSessionId == selected.run.attackerSessionId &&
            candidate.killIndices.size() == selected.kills.size()) {
            gApp.folderTimelineSelectedRun = index;
            break;
        }
    }
    gApp.timelineHoverMs = -1;
}

void populateFolderResults() {
    ListView_DeleteAllItems(gApp.folderRunList);
    ListView_DeleteAllItems(gApp.folderKillList);
    gApp.folderRows.clear();
    gApp.folderKillRows.clear();

    for (std::size_t runIndex = 0; runIndex < gApp.folderRuns.size(); ++runIndex) {
        const FolderRunResult& result = gApp.folderRuns[runIndex];
        if (result.kills.empty()) continue;
        const etlfrag::KillEvent& firstKill = result.kills.front();
        const int resultIndex = static_cast<int>(gApp.folderRows.size());
        gApp.folderRows.push_back(runIndex);
        const int row = addListRow(
            gApp.folderRunList,
            relativeDemoPath(result.demo.path).wstring(),
            static_cast<LPARAM>(resultIndex));
        setListCell(gApp.folderRunList, row, 1, toWide(result.demo.mapName));
        setListCell(gApp.folderRunList, row, 2, toWide(result.demo.povName));
        setListCell(gApp.folderRunList, row, 3, durationText(result.run.startDemoTimeMs));
        setListCell(
            gApp.folderRunList,
            row,
            4,
            firstKill.matchRemainingMs >= 0
                ? durationText(firstKill.matchRemainingMs, false)
                : L"—");
        setListCell(gApp.folderRunList, row, 5, std::to_wstring(result.kills.size()));
        setListCell(
            gApp.folderRunList,
            row,
            6,
            std::to_wstring(result.run.headshotCount));
        setListCell(
            gApp.folderRunList,
            row,
            7,
            durationText(result.run.endDemoTimeMs - result.run.startDemoTimeMs));
        setListCell(gApp.folderRunList, row, 8, describeKills(result.kills));
    }

    const bool hasResults = !gApp.folderRows.empty();
    EnableWindow(gApp.playFolderRun, hasResults);
    EnableWindow(gApp.addFolderHighlight, hasResults);
    applyListSort(gApp.folderRunList, gApp.folderRunSort);
    if (hasResults) {
        ListView_SetItemState(
            gApp.folderRunList,
            0,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
        populateFolderKillDetails(selectedListData(gApp.folderRunList));
    }
    updateTabLabels();
    updateWindowTitle();
    InvalidateRect(gApp.window, nullptr, FALSE);
    InvalidateRect(gApp.timeline, nullptr, FALSE);
}

std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
    GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<std::size_t>(std::max(0, length)));
    return value;
}

bool parseIntegerField(HWND control, int minimum, int maximum, int& value) {
    const std::wstring text = controlText(control);
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = std::wcstol(text.c_str(), &end, 10);
    while (end != nullptr && *end != L'\0' && std::iswspace(*end)) ++end;
    if (text.empty() || end == text.c_str() || (end != nullptr && *end != L'\0') ||
        errno == ERANGE || parsed < minimum || parsed > maximum) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool parseSecondsField(HWND control, double minimum, double maximum, double& value) {
    const std::wstring text = controlText(control);
    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(text.c_str(), &end);
    while (end != nullptr && *end != L'\0' && std::iswspace(*end)) ++end;
    if (text.empty() || end == text.c_str() || (end != nullptr && *end != L'\0') ||
        errno == ERANGE || !std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
        return false;
    }
    value = parsed;
    return true;
}

bool readFilterInputs(bool folder, etlfrag::RunFilter& filter) {
    HWND minimumControl = folder ? gApp.folderMinimumKills : gApp.minimumKills;
    HWND minimumHeadshotsControl =
        folder ? gApp.folderMinimumHeadshots : gApp.minimumHeadshots;
    HWND gapControl = folder ? gApp.folderMaximumGap : gApp.maximumGap;
    HWND weaponControl = folder ? gApp.folderWeapon : gApp.weapon;
    HWND postDeathControl = folder ? gApp.folderPostDeathWindow : gApp.postDeathWindow;
    const std::vector<int>& weaponIds = folder ? gApp.folderWeaponIds : gApp.weaponIds;
    const bool includeTeamKills = folder ? gApp.folderIncludeTeamKills : gApp.includeTeamKills;
    const bool includeWarmupKills =
        folder ? gApp.folderIncludeWarmupKills : gApp.includeWarmupKills;
    const bool postDeathEnabled =
        folder ? gApp.folderPostDeathExplosivesEnabled : gApp.postDeathExplosivesEnabled;

    if (!parseIntegerField(minimumControl, 1, 99, filter.minimumKills)) {
        MessageBoxW(
            gApp.window,
            L"Minimum kills must be a whole number from 1 to 99.",
            L"Invalid minimum kills",
            MB_OK | MB_ICONINFORMATION);
        SetFocus(minimumControl);
        return false;
    }
    if (!parseIntegerField(minimumHeadshotsControl, 0, 99, filter.minimumHeadshots)) {
        MessageBoxW(
            gApp.window,
            L"Minimum headshots must be a whole number from 0 to 99. Use 0 to disable this filter.",
            L"Invalid minimum headshots",
            MB_OK | MB_ICONINFORMATION);
        SetFocus(minimumHeadshotsControl);
        return false;
    }
    double gapSeconds = 0.0;
    if (!parseSecondsField(gapControl, 0.0, 3600.0, gapSeconds)) {
        MessageBoxW(
            gApp.window,
            L"Maximum gap must be a finite number from 0 to 3600 seconds.",
            L"Invalid maximum gap",
            MB_OK | MB_ICONINFORMATION);
        SetFocus(gapControl);
        return false;
    }
    filter.maximumGapMs = static_cast<std::int32_t>(gapSeconds * 1000.0 + 0.5);

    const int weaponSelection = static_cast<int>(
        SendMessageW(weaponControl, CB_GETCURSEL, 0, 0));
    if (weaponSelection >= 0 && weaponSelection < static_cast<int>(weaponIds.size())) {
        filter.weapon = weaponIds[static_cast<std::size_t>(weaponSelection)];
    }
    filter.includeTeamKills = includeTeamKills;
    filter.includeWarmupKills = includeWarmupKills;

    if (postDeathEnabled) {
        double seconds = 0.0;
        if (!parseSecondsField(postDeathControl, 0.1, 600.0, seconds)) {
            MessageBoxW(
                gApp.window,
                L"Enter a finite post-death explosive window from 0.1 to 600 seconds, for example 4.5.",
                L"Invalid post-death window",
                MB_OK | MB_ICONINFORMATION);
            SetFocus(postDeathControl);
            return false;
        }
        filter.postDeathExplosiveWindowMs = static_cast<std::int32_t>(
            seconds * 1000.0 + 0.5);
    }
    return true;
}

bool applyFolderFilters(bool reportStatus) {
    if (gApp.folderDemos.empty()) {
        if (reportStatus) {
            setStatus(L"No cached POV demos are available. Scan a folder first.");
        }
        return false;
    }
    const bool hasCurrentHeadshotIndex = std::any_of(
        gApp.folderDemos.begin(),
        gApp.folderDemos.end(),
        [](const etlfrag::IndexedDemoSummary& summary) {
            return summary.parserRevision == etlfrag::kDemoIndexParserRevision;
        });
    if (!hasCurrentHeadshotIndex) {
        if (reportStatus) {
            setStatus(
                L"This folder was indexed by an older parser. Select Update index once "
                L"to add exact headshot-hit data.");
        }
        return false;
    }

    etlfrag::RunFilter filter;
    if (!readFilterInputs(true, filter)) {
        return false;
    }
    const HCURSOR previousCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const std::wstring query = controlText(gApp.folderQuery);
    const bool searchActive = !query.empty();
    std::set<std::int64_t> matchingDemoIds;
    if (searchActive) {
        std::string searchError;
        const std::vector<etlfrag::IndexedDemoSummary> matches = gApp.demoIndex.search(
            toUtf8(query),
            selectedSearchField(gApp.folderField),
            gApp.demoFolder,
            false,
            gApp.folderDemos.size(),
            searchError);
        if (!searchError.empty()) {
            SetCursor(previousCursor);
            if (reportStatus) {
                MessageBoxW(
                    gApp.window,
                    toWide(searchError).c_str(),
                    L"Folder search error",
                    MB_OK | MB_ICONERROR);
            } else {
                setStatus(L"Folder search error: " + toWide(searchError));
            }
            return false;
        }
        for (const etlfrag::IndexedDemoSummary& match : matches) {
            matchingDemoIds.insert(match.id);
        }
    }
    gApp.folderRuns.clear();
    gApp.folderTimelineDemo = {};
    gApp.folderTimelineRuns.clear();
    gApp.folderTimelineDemoId = -1;
    gApp.folderTimelineSelectedRun = static_cast<std::size_t>(-1);
    gApp.folderAppliedFilter = filter;
    std::size_t povDemos = 0;
    std::size_t failedLoads = 0;
    try {
        for (const etlfrag::IndexedDemoSummary& summary : gApp.folderDemos) {
            if (searchActive && matchingDemoIds.count(summary.id) == 0) continue;
            if (summary.povClientNum < 0 ||
                summary.parserRevision != etlfrag::kDemoIndexParserRevision) {
                continue;
            }
            ++povDemos;
            std::string loadError;
            std::optional<etlfrag::DemoInfo> demo =
                gApp.demoIndex.loadById(summary.id, loadError);
            if (!demo.has_value()) {
                ++failedLoads;
                continue;
            }
            filter.playerClientNum = demo->povClientNum;
            filter.playerSessionId = -1;
            const std::vector<etlfrag::FragRun> runs =
                etlfrag::findFragRuns(*demo, filter);
            for (const etlfrag::FragRun& run : runs) {
                FolderRunResult result;
                result.demo = summary;
                result.run = run;
                result.kills.reserve(run.killIndices.size());
                for (const std::size_t killIndex : run.killIndices) {
                    if (killIndex < demo->kills.size()) {
                        result.kills.push_back(demo->kills[killIndex]);
                    }
                }
                if (!result.kills.empty()) {
                    gApp.folderRuns.push_back(std::move(result));
                }
            }
        }
    } catch (const std::exception& error) {
        SetCursor(previousCursor);
        MessageBoxW(
            gApp.window,
            toWide(error.what()).c_str(),
            L"Invalid folder filter",
            MB_OK | MB_ICONERROR);
        return false;
    }

    populateFolderResults();
    SetCursor(previousCursor);
    if (reportStatus) {
        std::wstring message;
        if (searchActive) {
            message = L"Search matched " + std::to_wstring(matchingDemoIds.size()) +
                      L" indexed demo(s) • ";
        }
        message += L"Filters applied to " + std::to_wstring(povDemos) +
                   L" cached POV demo(s) • " + std::to_wstring(gApp.folderRows.size()) +
                   L" multi-kill(s) shown • no demo files rescanned";
        if (failedLoads > 0) {
            message += L" • " + std::to_wstring(failedLoads) +
                       L" index record(s) unreadable";
        }
        setStatus(message);
    }
    return true;
}

void setFolderScanControlsEnabled(bool enabled) {
    for (HWND control : {
             gApp.chooseFolder,
             gApp.folderMinimumKills,
             gApp.folderMinimumHeadshots,
             gApp.folderMaximumGap,
             gApp.folderWeapon,
             gApp.folderTeamKills,
             gApp.folderWarmupKills,
             gApp.folderPostDeathExplosives,
             gApp.folderQuery,
             gApp.folderField,
             gApp.folderWatch}) {
        EnableWindow(control, enabled ? TRUE : FALSE);
    }
    EnableWindow(
        gApp.folderPostDeathWindow,
        enabled ? TRUE : FALSE);
    EnableWindow(
        gApp.folderPostDeathWindowLabel,
        enabled ? TRUE : FALSE);
    EnableWindow(
        gApp.folderApplyFilters,
        enabled && !gApp.folderDemos.empty() ? TRUE : FALSE);
}

void setDemoFolder(const std::filesystem::path& folder) {
    if (gApp.folderScanRunning) {
        MessageBoxW(
            gApp.window,
            L"Cancel the current folder update before selecting another folder.",
            L"Folder update in progress",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    stopFolderWatcher();
    gApp.demoFolder = folder;
    SetWindowTextW(gApp.folderPath, folder.c_str());
    gApp.folderDemos.clear();
    gApp.folderRuns.clear();
    gApp.folderRows.clear();
    gApp.folderKillRows.clear();
    gApp.folderFilesFound = 0;
    gApp.folderFilesParsed = 0;
    gApp.folderFilesFailed = 0;
    gApp.folderFilesWithoutPov = 0;
    gApp.folderFilesLoadedFromIndex = 0;
    gApp.folderFilesNewOrChanged = 0;
    gApp.folderTimelineDemo = {};
    gApp.folderTimelineRuns.clear();
    gApp.folderTimelineDemoId = -1;
    ListView_DeleteAllItems(gApp.folderRunList);
    ListView_DeleteAllItems(gApp.folderKillList);
    EnableWindow(gApp.playFolderRun, FALSE);
    EnableWindow(gApp.addFolderHighlight, FALSE);
    const std::vector<std::filesystem::path> files = findDemoFiles(folder, nullptr);
    gApp.folderFilesFound = files.size();
    refreshFolderDemos(true);
    gApp.folderFilesLoadedFromIndex = gApp.folderDemos.size();
    if (!gApp.folderDemos.empty()) {
        applyFolderFilters(false);
    }
    EnableWindow(gApp.folderApplyFilters, !gApp.folderDemos.empty());
    SetWindowTextW(gApp.folderScan, L"Update index");
    updateTabLabels();
    updateWindowTitle();
    InvalidateRect(gApp.window, nullptr, FALSE);
    if (!WritePrivateProfileStringW(
            L"Folder", L"Path", folder.c_str(), gApp.iniPath.c_str())) {
        MessageBoxW(
            gApp.window,
            L"The selected folder could not be saved in the application settings.",
            L"Settings warning",
            MB_OK | MB_ICONWARNING);
    }
    startFolderWatcher();
    if (!gApp.folderDemos.empty()) {
        const std::size_t povDemos = static_cast<std::size_t>(std::count_if(
            gApp.folderDemos.begin(), gApp.folderDemos.end(),
            [](const etlfrag::IndexedDemoSummary& demo) { return demo.povClientNum >= 0; }));
        setStatus(
            L"Loaded " + std::to_wstring(povDemos) +
            L" POV demo(s) from the persistent index • " +
            std::to_wstring(gApp.folderFilesFound) +
            L" .dm_84 file(s) currently present • select Update index to catch changes");
    } else {
        setStatus(L"Folder selected — configure filters, then select Update index.");
    }
}

void startFolderScan(bool automatic = false) {
    if (gApp.folderScanRunning) {
        if (automatic) {
            gApp.folderRescanPending = true;
            return;
        }
        if (gApp.folderCancelRequested != nullptr) {
            gApp.folderCancelRequested->store(true);
        }
        SetWindowTextW(gApp.folderScan, L"Cancelling…");
        EnableWindow(gApp.folderScan, FALSE);
        setStatus(L"Cancelling folder scan after the current demo…");
        return;
    }
    std::error_code folderError;
    if (gApp.demoFolder.empty() ||
        !std::filesystem::is_directory(gApp.demoFolder, folderError) || folderError) {
        MessageBoxW(
            gApp.window,
            L"Choose a valid demo folder before starting the scan.",
            L"No demo folder selected",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    gApp.folderRescanPending = false;

    auto request = std::make_unique<FolderScanRequest>();
    request->window = gApp.window;
    request->folder = gApp.demoFolder;
    request->indexPath = gApp.indexPath;
    request->cancelRequested = std::make_shared<std::atomic_bool>(false);
    request->automatic = automatic;
    const std::shared_ptr<std::atomic_bool> cancelRequested = request->cancelRequested;

    const std::uintptr_t threadValue = _beginthreadex(
        nullptr,
        0,
        folderScanWorker,
        request.get(),
        0,
        nullptr);
    if (threadValue == 0) {
        MessageBoxW(
            gApp.window,
            L"The background folder scan could not be started.",
            L"Folder scan error",
            MB_OK | MB_ICONERROR);
        return;
    }
    HANDLE thread = reinterpret_cast<HANDLE>(threadValue);
    request.release();
    gApp.folderThread = thread;
    gApp.folderCancelRequested = cancelRequested;
    gApp.folderScanRunning = true;
    EnableWindow(gApp.folderApplyFilters, FALSE);
    setFolderScanControlsEnabled(false);
    SetWindowTextW(gApp.folderScan, L"Cancel scan");
    setStatus(
        automatic
            ? L"A folder change was detected — updating the persistent index…"
            : L"Updating the persistent index — unchanged demos will be reused without reparsing…");
    updateTabLabels();
    InvalidateRect(gApp.window, nullptr, FALSE);
}

void finishFolderScan(std::unique_ptr<FolderScanOutcome> outcome) {
    if (gApp.folderThread != nullptr) {
        CloseHandle(gApp.folderThread);
        gApp.folderThread = nullptr;
    }
    gApp.folderScanRunning = false;
    gApp.folderCancelRequested.reset();
    gApp.demoFolder = outcome->folder;
    gApp.folderFilesFound = outcome->filesFound;
    gApp.folderFilesLoadedFromIndex = outcome->filesLoadedFromIndex;
    gApp.folderFilesParsed = outcome->filesParsed;
    gApp.folderFilesFailed = outcome->filesFailed;
    gApp.folderFilesWithoutPov = outcome->filesWithoutPov;
    gApp.folderFilesNewOrChanged = outcome->filesNewOrChanged;
    refreshFolderDemos(true);
    setFolderScanControlsEnabled(true);
    EnableWindow(gApp.folderScan, TRUE);
    SetWindowTextW(gApp.folderScan, L"Update index");
    if (!gApp.folderDemos.empty()) {
        applyFolderFilters(false);
    } else {
        populateFolderResults();
    }

    const std::size_t povDemos = static_cast<std::size_t>(std::count_if(
        gApp.folderDemos.begin(), gApp.folderDemos.end(),
        [](const etlfrag::IndexedDemoSummary& demo) { return demo.povClientNum >= 0; }));
    if (!outcome->fatalError.empty()) {
        setStatus(L"Folder index error: " + toWide(outcome->fatalError));
        if (!outcome->automatic) {
            MessageBoxW(
                gApp.window,
                toWide(outcome->fatalError).c_str(),
                L"Folder index error",
                MB_OK | MB_ICONERROR);
        }
    } else if (outcome->cancelled) {
        setStatus(
            L"Folder scan cancelled • " + std::to_wstring(povDemos) +
            L" POV demo(s) available • " + std::to_wstring(gApp.folderRows.size()) +
            L" multi-kill(s) shown • completed index work was saved");
    } else if (gApp.folderFilesFound == 0) {
        setStatus(L"No .dm_84 files were found in the selected folder or its subfolders.");
    } else {
        setStatus(
            L"Folder scan complete • " + std::to_wstring(gApp.folderFilesFound) +
            L" demo(s) found • " + std::to_wstring(povDemos) +
            L" POV demo(s) available • " + std::to_wstring(gApp.folderRows.size()) +
            L" multi-kill(s) shown • " + std::to_wstring(gApp.folderFilesFailed) +
            L" unreadable • " + std::to_wstring(gApp.folderFilesLoadedFromIndex) +
            L" unchanged loaded from index • " +
            std::to_wstring(gApp.folderFilesNewOrChanged) + L" parsed and indexed • " +
            std::to_wstring(outcome->filesWithWarnings) + L" recovered with warning(s) • " +
            std::to_wstring(outcome->staleEntriesRemoved) + L" stale record(s) removed");
    }

    if (!outcome->automatic && !outcome->failures.empty()) {
        std::wstring details =
            L"Some demos could not be indexed:\n\n";
        for (const std::string& failure : outcome->failures) {
            details += L"• " + toWide(failure) + L"\n";
        }
        if (outcome->filesFailed > outcome->failures.size()) {
            details += L"\n…and " +
                       std::to_wstring(outcome->filesFailed - outcome->failures.size()) +
                       L" more.";
        }
        MessageBoxW(
            gApp.window,
            details.c_str(),
            L"Unreadable demo files",
            MB_OK | MB_ICONWARNING);
    }
    refreshIndexedDemoCount();
    searchLibrary(false);
    if (gApp.folderRescanPending && !outcome->cancelled) {
        gApp.folderRescanPending = false;
        startFolderScan(true);
    }
}

void updateWindowTitle() {
    std::wstring title = kApplicationName;
    if (gApp.activeTab == 2) {
        title += L" — Folder scan";
        if (!gApp.demoFolder.empty()) {
            std::wstring folderName = gApp.demoFolder.filename().wstring();
            if (folderName.empty()) {
                folderName = gApp.demoFolder.wstring();
            }
            title += L": " + folderName;
        }
    } else if (gApp.activeTab == 3) {
        title += L" — Highlight basket";
    } else if (gApp.activeTab == 4) {
        title += L" — Demo library";
    } else if (gApp.demoLoaded) {
        title += L" — " + gApp.demo.path.filename().wstring();
    }
    SetWindowTextW(gApp.window, title.c_str());
}

void searchRuns() {
    if (!gApp.demoLoaded) {
        return;
    }
    etlfrag::RunFilter filter;
    const int playerSelection = static_cast<int>(SendMessageW(gApp.player, CB_GETCURSEL, 0, 0));
    if (playerSelection >= 0 && playerSelection < static_cast<int>(gApp.playerIds.size())) {
        const PlayerSelection player = gApp.playerIds[static_cast<std::size_t>(playerSelection)];
        filter.playerClientNum = player.clientNum;
        filter.playerSessionId = player.sessionId;
    }
    if (!readFilterInputs(false, filter)) {
        return;
    }

    try {
        gApp.runs = etlfrag::findFragRuns(gApp.demo, filter);
    } catch (const std::exception& error) {
        MessageBoxW(gApp.window, toWide(error.what()).c_str(), L"Invalid filter", MB_OK | MB_ICONERROR);
        return;
    }

    ListView_DeleteAllItems(gApp.runList);
    ListView_DeleteAllItems(gApp.runKillList);
    for (std::size_t index = 0; index < gApp.runs.size(); ++index) {
        const etlfrag::FragRun& run = gApp.runs[index];
        const etlfrag::KillEvent& firstKill = gApp.demo.kills[run.killIndices.front()];
        const int row = addListRow(
            gApp.runList,
            durationText(run.startDemoTimeMs),
            static_cast<LPARAM>(index));
        setListCell(
            gApp.runList,
            row,
            1,
            firstKill.matchRemainingMs >= 0 ? durationText(firstKill.matchRemainingMs, false) : L"—");
        setListCell(gApp.runList, row, 2, toWide(run.attackerName));
        setListCell(gApp.runList, row, 3, std::to_wstring(run.killIndices.size()));
        setListCell(gApp.runList, row, 4, std::to_wstring(run.headshotCount));
        setListCell(
            gApp.runList,
            row,
            5,
            durationText(run.endDemoTimeMs - run.startDemoTimeMs));

        setListCell(gApp.runList, row, 6, describeRun(gApp.demo, run));
    }

    EnableWindow(gApp.playRun, !gApp.runs.empty());
    EnableWindow(gApp.addRunHighlight, !gApp.runs.empty());
    applyListSort(gApp.runList, gApp.runSort);
    if (!gApp.runs.empty()) {
        ListView_SetItemState(
            gApp.runList,
            0,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
        populateRunKillDetails(selectedListData(gApp.runList));
    }
    updateTabLabels();
    InvalidateRect(gApp.window, nullptr, FALSE);
    InvalidateRect(gApp.timeline, nullptr, FALSE);
    setStatus(
        L"Found " + std::to_wstring(gApp.runs.size()) + L" multi-kill sequence(s) • " +
        std::to_wstring(gApp.demo.kills.size()) + L" total demo events indexed");
}

void populateFilters() {
    SendMessageW(gApp.player, CB_RESETCONTENT, 0, 0);
    SendMessageW(gApp.eventPlayer, CB_RESETCONTENT, 0, 0);
    gApp.playerIds.clear();
    gApp.eventPlayerIds.clear();
    SendMessageW(gApp.player, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All players"));
    SendMessageW(
        gApp.eventPlayer,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(L"All players — complete demo log"));
    gApp.playerIds.push_back({-1, -1});
    gApp.eventPlayerIds.push_back({-1, -1});
    int povSelection = 0;
    for (const etlfrag::Player& player : gApp.demo.players) {
        const std::size_t slotSessions = static_cast<std::size_t>(std::count_if(
            gApp.demo.players.begin(), gApp.demo.players.end(),
            [&](const etlfrag::Player& candidate) {
                return candidate.clientNum == player.clientNum;
            }));
        std::wstring label = toWide(player.cleanName) + L"  (#" +
                             std::to_wstring(player.clientNum) + L", " +
                             toWide(etlfrag::teamName(player.team));
        if (slotSessions > 1) {
            label += L", session " + std::to_wstring(player.sessionId + 1);
        }
        label += L")";
        SendMessageW(gApp.player, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(
            gApp.eventPlayer,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str()));
        gApp.playerIds.push_back({player.clientNum, player.sessionId});
        gApp.eventPlayerIds.push_back({player.clientNum, player.sessionId});
        if (player.clientNum == gApp.demo.povClientNum &&
            (gApp.demo.povName.empty() || player.cleanName == gApp.demo.povName)) {
            povSelection = static_cast<int>(gApp.playerIds.size() - 1);
        }
    }
    SendMessageW(gApp.player, CB_SETCURSEL, povSelection, 0);
    SendMessageW(gApp.eventPlayer, CB_SETCURSEL, 0, 0);

    std::set<int> weapons;
    for (const etlfrag::KillEvent& kill : gApp.demo.kills) {
        if (kill.attacker >= 0 && !kill.suicide) {
            weapons.insert(kill.weapon);
        }
    }
    SendMessageW(gApp.weapon, CB_RESETCONTENT, 0, 0);
    gApp.weaponIds.clear();
    SendMessageW(gApp.weapon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All weapons"));
    gApp.weaponIds.push_back(-1);
    for (const int weapon : weapons) {
        const std::wstring name = toWide(etlfrag::weaponName(weapon));
        SendMessageW(gApp.weapon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        gApp.weaponIds.push_back(weapon);
    }
    SendMessageW(gApp.weapon, CB_SETCURSEL, 0, 0);
}

void loadDemo(const std::filesystem::path& path) {
    const HCURSOR previousCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    setStatus(L"Analyzing binary demo stream…");
    UpdateWindow(gApp.window);
    try {
        bool loadedFromIndex = false;
        std::string indexError;
        std::optional<etlfrag::DemoInfo> cached =
            gApp.demoIndex.findFresh(path, indexError);
        etlfrag::DemoInfo loaded;
        if (cached.has_value()) {
            loaded = std::move(*cached);
            loaded.path = path;
            loadedFromIndex = true;
        } else {
            loaded = etlfrag::DemoParser{}.parse(path);
            std::string upsertError;
            if (!gApp.demoIndex.upsert(path, loaded, upsertError)) {
                indexError = upsertError;
            }
        }
        gApp.demo = std::move(loaded);
        gApp.demoLoaded = true;
        EnableWindow(gApp.viewProtocolLog, TRUE);
        gApp.timelineHoverMs = -1;
        SetWindowTextW(gApp.demoPath, path.c_str());
        populateFilters();
        populateAllEvents();
        searchRuns();
        if (!loadedFromIndex) {
            refreshIndexedDemoCount();
            searchLibrary(false);
        }
        updateWindowTitle();
        setStatus(
            L"Map: " + toWide(gApp.demo.mapName) + L" • POV: " + toWide(gApp.demo.povName) +
            L" • Duration: " + durationText(gApp.demo.lastServerTimeMs - gApp.demo.firstServerTimeMs) +
            L" • Players: " + std::to_wstring(gApp.demo.players.size()) +
            L" • Events indexed: " + std::to_wstring(gApp.demo.kills.size()) +
            (loadedFromIndex ? L" • loaded from persistent index" : L" • parsed from demo") +
            (!gApp.demo.warnings.empty()
                 ? L" • recovered with " + std::to_wstring(gApp.demo.warnings.size()) +
                       L" parser warning(s)"
                 : L"") +
            (!indexError.empty() ? L" • index warning: " + toWide(indexError) : L""));
        InvalidateRect(gApp.window, nullptr, FALSE);
    } catch (const std::exception& error) {
        MessageBoxW(
            gApp.window,
            toWide(error.what()).c_str(),
            L"Could not read demo",
            MB_OK | MB_ICONERROR);
        setStatus(
            gApp.demoLoaded
                ? L"Could not read the selected demo; the previously loaded demo remains open."
                : L"Could not read the selected demo.");
    }
    SetCursor(previousCursor);
}

void playAtPath(
    const std::filesystem::path& demoPath,
    std::int32_t demoTimeMs,
    const std::wstring& description) {
    if (!std::filesystem::is_regular_file(demoPath)) {
        MessageBoxW(
            gApp.window,
            L"The selected demo file no longer exists.",
            L"Playback error",
            MB_OK | MB_ICONERROR);
        return;
    }
    if (gApp.etlExecutable.empty() || !std::filesystem::is_regular_file(gApp.etlExecutable)) {
        if (!chooseEtlExecutable()) {
            return;
        }
    }

    try {
        const bool automaticSeek = !gApp.launchWithoutSeeking;
        const std::int32_t seekMs = automaticSeek
                                        ? std::max<std::int32_t>(
                                              0, demoTimeMs - kPlaybackPrerollMs)
                                        : 0;
        std::wostringstream seconds;
        seconds.setf(std::ios::fixed);
        seconds.precision(3);
        seconds << static_cast<double>(seekMs) / 1000.0;

        // ET: Legacy accepts an absolute demo path, but on Windows its demo
        // loader looks for '/' when it extracts the file name.  Supplying the
        // generic form keeps the absolute-path and downstream demo-name logic
        // consistent on every supported ETL release.
        const std::filesystem::path absoluteDemo =
            std::filesystem::absolute(demoPath).lexically_normal();
        const std::wstring engineDemoPath = absoluteDemo.generic_wstring();

        // Do not execute `seek` as the next startup command.  At that point
        // `demo` has only advanced the client to CA_PRIMED; cgame and the first
        // active snapshot are not ready yet.  ETL's activeAction is explicitly
        // run by CL_FirstSnapshot after the demo becomes active, which makes
        // seeking safe for both current and older ET: Legacy clients.
        std::wstring arguments;
        if (automaticSeek && seekMs > 0) {
            arguments = L"+set activeAction \"seek " + seconds.str() + L"\" ";
        }
        arguments += L"+demo \"" + engineDemoPath + L"\"";

        // A shortcut's compatibility flag is not inherited when Frag Finder
        // starts etl.exe directly. The explicit runas verb asks Windows for
        // elevation and shows the normal UAC prompt only when the user enabled
        // the option in the application.
        const wchar_t* launchVerb =
            gApp.launchEtlAsAdministrator ? L"runas" : L"open";

        const HINSTANCE result = ShellExecuteW(
            gApp.window,
            launchVerb,
            gApp.etlExecutable.c_str(),
            arguments.c_str(),
            gApp.etlExecutable.parent_path().c_str(),
            SW_SHOWNORMAL);

        const INT_PTR shellResult = reinterpret_cast<INT_PTR>(result);
        try {
            std::ofstream log(
                gApp.dataFolder / L"playback-launch.log",
                std::ios::binary | std::ios::trunc);
            if (log) {
                SYSTEMTIME now{};
                GetLocalTime(&now);
                log << "ET: Legacy Frag Finder 1.7.2 playback launch\r\n"
                    << "Time: " << std::setfill('0') << std::setw(4) << now.wYear << '-'
                    << std::setw(2) << now.wMonth << '-' << std::setw(2) << now.wDay << ' '
                    << std::setw(2) << now.wHour << ':' << std::setw(2) << now.wMinute << ':'
                    << std::setw(2) << now.wSecond << "\r\n"
                    << "Executable: " << toUtf8(gApp.etlExecutable.wstring()) << "\r\n"
                    << "Working directory: "
                    << toUtf8(gApp.etlExecutable.parent_path().wstring()) << "\r\n"
                    << "Demo: " << toUtf8(absoluteDemo.wstring()) << "\r\n"
                    << "Verb: " << toUtf8(launchVerb) << "\r\n"
                    << "Administrator requested: "
                    << (gApp.launchEtlAsAdministrator ? "yes" : "no") << "\r\n"
                    << "Automatic seek enabled: " << (automaticSeek ? "yes" : "no")
                    << "\r\n"
                    << "Requested event time (ms): " << demoTimeMs << "\r\n"
                    << "Effective seek time (ms): " << seekMs << "\r\n"
                    << "Arguments: " << toUtf8(arguments) << "\r\n"
                    << "ShellExecute result: " << shellResult << "\r\n";
            }
        } catch (...) {
            // A diagnostic log must never prevent demo playback.
        }

        if (shellResult <= 32) {
            throw std::runtime_error(
                "ShellExecute failed with code " + std::to_string(shellResult));
        }
        if (automaticSeek) {
            setStatus(
                L"Playing " + description + L" from " + durationText(seekMs) +
                L" (5 seconds before the event)" +
                (gApp.launchEtlAsAdministrator ? L" • administrator requested" : L"") +
                L" • launch log saved");
        } else {
            setStatus(
                L"Diagnostic playback: " + description +
                L" launched from the beginning without seeking" +
                (gApp.launchEtlAsAdministrator ? L" • administrator requested" : L"") +
                L" • launch log saved");
        }
    } catch (const std::exception& error) {
        MessageBoxW(
            gApp.window,
            (L"ET: Legacy could not be started. Check the path to etl.exe.\n\n" +
             toWide(error.what()) +
             L"\n\nLaunch details: " +
             (gApp.dataFolder / L"playback-launch.log").wstring())
                .c_str(),
            L"Playback error",
            MB_OK | MB_ICONERROR);
    }
}

void playAt(std::int32_t demoTimeMs, const std::wstring& description) {
    if (gApp.demoLoaded) {
        playAtPath(gApp.demo.path, demoTimeMs, description);
    }
}

void playSelectedRun() {
    const int runIndex = selectedListData(gApp.runList);
    if (runIndex >= 0 && runIndex < static_cast<int>(gApp.runs.size())) {
        playAt(gApp.runs[static_cast<std::size_t>(runIndex)].startDemoTimeMs, L"multi-kill");
    }
}

void playSelectedEvent() {
    const int killIndex = selectedListData(gApp.allEventList);
    if (killIndex >= 0 && killIndex < static_cast<int>(gApp.demo.kills.size())) {
        playAt(gApp.demo.kills[static_cast<std::size_t>(killIndex)].demoTimeMs, L"selected event");
    }
}

void playSelectedFolderRun() {
    const int rowIndex = selectedListData(gApp.folderRunList);
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gApp.folderRows.size())) {
        return;
    }
    const std::size_t runIndex = gApp.folderRows[static_cast<std::size_t>(rowIndex)];
    if (runIndex >= gApp.folderRuns.size()) return;
    const FolderRunResult& result = gApp.folderRuns[runIndex];
    playAtPath(
        result.demo.path, result.run.startDemoTimeMs,
        L"folder multi-kill in " + result.demo.path.filename().wstring());
}

etlfrag::HighlightItem makeHighlight(
    const etlfrag::DemoInfo& demo,
    const etlfrag::FragRun& run) {
    etlfrag::HighlightItem highlight;
    highlight.demoPath = demo.path;
    highlight.mapName = demo.mapName;
    highlight.povName = run.attackerName.empty() ? demo.povName : run.attackerName;
    highlight.startDemoTimeMs = run.startDemoTimeMs;
    highlight.endDemoTimeMs = run.endDemoTimeMs;
    highlight.headshotCount = run.headshotCount;
    highlight.description = toUtf8(describeRun(demo, run));
    if (!run.killIndices.empty() && run.killIndices.front() < demo.kills.size()) {
        highlight.matchRemainingMs = demo.kills[run.killIndices.front()].matchRemainingMs;
    }
    for (const std::size_t killIndex : run.killIndices) {
        if (killIndex >= demo.kills.size()) {
            continue;
        }
        const etlfrag::KillEvent& kill = demo.kills[killIndex];
        etlfrag::HighlightEvent event;
        event.demoTimeMs = kill.demoTimeMs;
        event.victim = kill.targetName;
        event.weapon = etlfrag::weaponName(kill.weapon);
        event.headshot = kill.headshot;
        event.teamKill = kill.teamKill;
        highlight.events.push_back(std::move(event));
    }
    return highlight;
}

etlfrag::HighlightItem makeHighlight(const FolderRunResult& result) {
    etlfrag::HighlightItem highlight;
    highlight.demoPath = result.demo.path;
    highlight.mapName = result.demo.mapName;
    highlight.povName = result.run.attackerName.empty()
                            ? result.demo.povName
                            : result.run.attackerName;
    highlight.startDemoTimeMs = result.run.startDemoTimeMs;
    highlight.endDemoTimeMs = result.run.endDemoTimeMs;
    highlight.headshotCount = result.run.headshotCount;
    highlight.description = toUtf8(describeKills(result.kills));
    if (!result.kills.empty()) {
        highlight.matchRemainingMs = result.kills.front().matchRemainingMs;
    }
    for (const etlfrag::KillEvent& kill : result.kills) {
        highlight.events.push_back({
            kill.demoTimeMs,
            kill.targetName,
            etlfrag::weaponName(kill.weapon),
            kill.headshot,
            kill.teamKill,
        });
    }
    return highlight;
}

void populateHighlightResults() {
    ListView_DeleteAllItems(gApp.highlightList);
    for (std::size_t index = 0; index < gApp.highlights.size(); ++index) {
        const etlfrag::HighlightItem& highlight = gApp.highlights[index];
        const int row = addListRow(
            gApp.highlightList,
            highlight.demoPath.filename().wstring(),
            static_cast<LPARAM>(index));
        setListCell(gApp.highlightList, row, 1, toWide(highlight.mapName));
        setListCell(gApp.highlightList, row, 2, toWide(highlight.povName));
        setListCell(gApp.highlightList, row, 3, durationText(highlight.startDemoTimeMs));
        setListCell(
            gApp.highlightList,
            row,
            4,
            highlight.matchRemainingMs >= 0
                ? durationText(highlight.matchRemainingMs, false)
                : L"—");
        setListCell(
            gApp.highlightList,
            row,
            5,
            std::to_wstring(highlight.events.size()));
        setListCell(
            gApp.highlightList,
            row,
            6,
            std::to_wstring(highlight.headshotCount));
        setListCell(
            gApp.highlightList,
            row,
            7,
            durationText(highlight.endDemoTimeMs - highlight.startDemoTimeMs));
        setListCell(gApp.highlightList, row, 8, toWide(highlight.description));
    }
    applyListSort(gApp.highlightList, gApp.highlightSort);
    const bool hasHighlights = !gApp.highlights.empty();
    EnableWindow(gApp.playHighlight, hasHighlights);
    EnableWindow(gApp.removeHighlight, hasHighlights);
    EnableWindow(gApp.clearHighlights, hasHighlights);
    if (hasHighlights) {
        ListView_SetItemState(
            gApp.highlightList,
            0,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
    }
    updateTabLabels();
    if (gApp.timeline != nullptr) {
        InvalidateRect(gApp.timeline, nullptr, FALSE);
    }
    InvalidateRect(gApp.window, nullptr, FALSE);
}

bool addHighlightItem(etlfrag::HighlightItem highlight) {
    const auto duplicate = std::find_if(
        gApp.highlights.begin(),
        gApp.highlights.end(),
        [&highlight](const etlfrag::HighlightItem& current) {
            return etlfrag::sameHighlight(current, highlight);
        });
    if (duplicate != gApp.highlights.end()) {
        setStatus(L"This multi-kill is already in the highlight basket.");
        return false;
    }
    std::vector<etlfrag::HighlightItem> candidate = gApp.highlights;
    candidate.push_back(std::move(highlight));
    std::string saveError;
    if (!etlfrag::saveHighlights(gApp.highlightsPath, candidate, saveError)) {
        MessageBoxW(
            gApp.window,
            toWide(saveError).c_str(),
            L"Could not save highlight basket",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    gApp.highlights = std::move(candidate);
    populateHighlightResults();
    setStatus(
        L"Added to highlight basket • " + std::to_wstring(gApp.highlights.size()) +
        L" saved highlight(s)");
    return true;
}

void addSelectedRunToHighlights() {
    const int runIndex = selectedListData(gApp.runList);
    if (runIndex < 0 || runIndex >= static_cast<int>(gApp.runs.size())) {
        return;
    }
    addHighlightItem(makeHighlight(gApp.demo, gApp.runs[static_cast<std::size_t>(runIndex)]));
}

void addSelectedFolderRunToHighlights() {
    const int rowIndex = selectedListData(gApp.folderRunList);
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gApp.folderRows.size())) {
        return;
    }
    const std::size_t runIndex = gApp.folderRows[static_cast<std::size_t>(rowIndex)];
    if (runIndex >= gApp.folderRuns.size()) return;
    addHighlightItem(makeHighlight(gApp.folderRuns[runIndex]));
}

void playSelectedHighlight() {
    const int highlightIndex = selectedListData(gApp.highlightList);
    if (highlightIndex < 0 || highlightIndex >= static_cast<int>(gApp.highlights.size())) {
        return;
    }
    const etlfrag::HighlightItem& highlight =
        gApp.highlights[static_cast<std::size_t>(highlightIndex)];
    playAtPath(
        highlight.demoPath,
        highlight.startDemoTimeMs,
        L"saved highlight in " + highlight.demoPath.filename().wstring());
}

void removeSelectedHighlight() {
    const int highlightIndex = selectedListData(gApp.highlightList);
    if (highlightIndex < 0 || highlightIndex >= static_cast<int>(gApp.highlights.size())) {
        return;
    }
    std::vector<etlfrag::HighlightItem> candidate = gApp.highlights;
    candidate.erase(candidate.begin() + highlightIndex);
    std::string saveError;
    if (!etlfrag::saveHighlights(gApp.highlightsPath, candidate, saveError)) {
        MessageBoxW(gApp.window, toWide(saveError).c_str(), L"Could not save highlight basket", MB_OK | MB_ICONWARNING);
        return;
    }
    gApp.highlights = std::move(candidate);
    populateHighlightResults();
    setStatus(L"Highlight removed from the basket.");
}

void clearHighlights() {
    if (gApp.highlights.empty()) {
        return;
    }
    if (MessageBoxW(
            gApp.window,
            L"Remove every saved highlight from the basket?",
            L"Clear highlight basket",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    std::vector<etlfrag::HighlightItem> candidate;
    std::string saveError;
    if (!etlfrag::saveHighlights(gApp.highlightsPath, candidate, saveError)) {
        MessageBoxW(gApp.window, toWide(saveError).c_str(), L"Could not save highlight basket", MB_OK | MB_ICONWARNING);
        return;
    }
    gApp.highlights.clear();
    populateHighlightResults();
    setStatus(L"Highlight basket cleared.");
}

etlfrag::DemoSearchField selectedSearchField(HWND fieldControl) {
    switch (static_cast<int>(SendMessageW(fieldControl, CB_GETCURSEL, 0, 0))) {
        case 1: return etlfrag::DemoSearchField::Nickname;
        case 2: return etlfrag::DemoSearchField::Map;
        case 3: return etlfrag::DemoSearchField::Date;
        case 4: return etlfrag::DemoSearchField::Filename;
        default: return etlfrag::DemoSearchField::All;
    }
}

void populateLibraryResults() {
    ListView_DeleteAllItems(gApp.libraryList);
    for (std::size_t index = 0; index < gApp.libraryRows.size(); ++index) {
        const etlfrag::IndexedDemoSummary& demo = gApp.libraryRows[index];
        const int row = addListRow(
            gApp.libraryList,
            toWide(demo.fileName),
            static_cast<LPARAM>(index));
        setListCell(gApp.libraryList, row, 1, toWide(demo.recordedDate));
        setListCell(gApp.libraryList, row, 2, toWide(demo.mapName));
        setListCell(gApp.libraryList, row, 3, toWide(demo.povName));
        setListCell(gApp.libraryList, row, 4, std::to_wstring(demo.playerCount));
        setListCell(gApp.libraryList, row, 5, std::to_wstring(demo.eventCount));
        const std::int32_t duration =
            std::max<std::int32_t>(0, demo.lastServerTimeMs - demo.firstServerTimeMs);
        setListCell(gApp.libraryList, row, 6, durationText(duration));
        setListCell(
            gApp.libraryList,
            row,
            7,
            demo.duplicateCount > 1 ? std::to_wstring(demo.duplicateCount) : L"—");
        setListCell(gApp.libraryList, row, 8, demo.path.wstring());
    }
    applyListSort(gApp.libraryList, gApp.librarySort);
    const bool hasRows = !gApp.libraryRows.empty();
    EnableWindow(gApp.libraryOpen, hasRows);
    if (hasRows) {
        ListView_SetItemState(
            gApp.libraryList,
            0,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
    }
    updateTabLabels();
    InvalidateRect(gApp.window, nullptr, FALSE);
}

void searchLibrary(bool reportStatus) {
    const std::wstring query = controlText(gApp.libraryQuery);
    std::optional<std::filesystem::path> folder;
    if (gApp.libraryFolderOnly && !gApp.demoFolder.empty()) {
        folder = gApp.demoFolder;
    }
    std::string error;
    gApp.libraryRows = gApp.demoIndex.search(
        toUtf8(query),
        selectedSearchField(gApp.libraryField),
        folder,
        gApp.libraryDuplicatesOnly,
        50000,
        error);
    if (!error.empty()) {
        if (reportStatus) {
            MessageBoxW(
                gApp.window,
                toWide(error).c_str(),
                L"Demo library search error",
                MB_OK | MB_ICONERROR);
        } else {
            setStatus(L"Demo library search error: " + toWide(error));
        }
        return;
    }
    populateLibraryResults();
    if (reportStatus) {
        setStatus(
            L"Demo library search returned " + std::to_wstring(gApp.libraryRows.size()) +
            L" row(s)" +
            (gApp.libraryDuplicatesOnly ? L" • duplicate groups only" : L""));
    }
}

void openSelectedLibraryDemo() {
    const int index = selectedListData(gApp.libraryList);
    if (index < 0 || index >= static_cast<int>(gApp.libraryRows.size())) return;
    const std::filesystem::path path =
        gApp.libraryRows[static_cast<std::size_t>(index)].path;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        MessageBoxW(
            gApp.window,
            L"The indexed demo no longer exists. Update its folder to remove stale records.",
            L"Demo file missing",
            MB_OK | MB_ICONWARNING);
        return;
    }
    showTab(0);
    loadDemo(path);
}

std::string csvCell(const std::string& value) {
    std::string safe = value;
    const std::size_t firstVisible = safe.find_first_not_of(" \t\r\n");
    if (firstVisible != std::string::npos &&
        (safe[firstVisible] == '=' || safe[firstVisible] == '+' ||
         safe[firstVisible] == '-' || safe[firstVisible] == '@')) {
        safe.insert(safe.begin(), '\'');
    }
    std::string escaped = "\"";
    for (const char character : safe) {
        if (character == '\"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '\"';
    return escaped;
}

std::string jsonString(const std::string& value) {
    std::ostringstream output;
    output << '\"';
    for (const unsigned char character : value) {
        switch (character) {
            case '\"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(character) << std::dec;
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    output << '\"';
    return output.str();
}

std::optional<std::filesystem::path> chooseExportPath() {
    std::wstring defaultName;
    switch (gApp.activeTab) {
        case 0: defaultName = L"etl-frag-finder-multikills.csv"; break;
        case 1: defaultName = L"etl-frag-finder-events.csv"; break;
        case 2: defaultName = L"etl-frag-finder-folder-results.csv"; break;
        case 3: defaultName = L"etl-frag-finder-highlights.csv"; break;
        default: defaultName = L"etl-frag-finder-demo-library.csv"; break;
    }
    std::wstring file(32768, L'\0');
    std::copy(defaultName.begin(), defaultName.end(), file.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = gApp.window;
    dialog.lpstrFilter =
        L"CSV spreadsheet (*.csv)\0*.csv\0JSON data (*.json)\0*.json\0\0";
    dialog.lpstrFile = file.data();
    dialog.nMaxFile = static_cast<DWORD>(file.size());
    dialog.lpstrTitle = L"Export current results";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&dialog)) {
        return std::nullopt;
    }
    std::filesystem::path path(file.c_str());
    if (path.extension().empty()) {
        path += dialog.nFilterIndex == 2 ? L".json" : L".csv";
    }
    return path;
}

bool currentViewHasExportRows() {
    if (gApp.activeTab == 0) return !gApp.runs.empty();
    if (gApp.activeTab == 1) return gApp.demoLoaded && visibleEventCount() > 0;
    if (gApp.activeTab == 2) return !gApp.folderRows.empty();
    if (gApp.activeTab == 3) return !gApp.highlights.empty();
    return !gApp.libraryRows.empty();
}

std::vector<std::size_t> listDataInDisplayOrder(HWND list) {
    std::vector<std::size_t> output;
    const int count = ListView_GetItemCount(list);
    output.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (int row = 0; row < count; ++row) {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (ListView_GetItem(list, &item) && item.lParam >= 0) {
            output.push_back(static_cast<std::size_t>(item.lParam));
        }
    }
    return output;
}

std::pair<etlfrag::DemoInfo, etlfrag::FragRun> folderRunForExport(
    const FolderRunResult& result) {
    etlfrag::DemoInfo demo;
    demo.path = result.demo.path;
    demo.mapName = result.demo.mapName;
    demo.povName = result.demo.povName;
    demo.kills = result.kills;
    etlfrag::FragRun run = result.run;
    run.killIndices.clear();
    for (std::size_t index = 0; index < demo.kills.size(); ++index) {
        run.killIndices.push_back(index);
    }
    return {std::move(demo), std::move(run)};
}

void writeRunCsv(
    std::ostream& output,
    const etlfrag::DemoInfo& demo,
    const etlfrag::FragRun& run,
    const std::filesystem::path& displayPath) {
    const etlfrag::KillEvent* first =
        !run.killIndices.empty() && run.killIndices.front() < demo.kills.size()
            ? &demo.kills[run.killIndices.front()]
            : nullptr;
    output << csvCell(toUtf8(displayPath.wstring())) << ','
           << csvCell(demo.mapName) << ','
           << csvCell(run.attackerName) << ','
           << csvCell(etlfrag::formatDuration(run.startDemoTimeMs)) << ','
           << csvCell(first != nullptr && first->matchRemainingMs >= 0
                          ? etlfrag::formatDuration(first->matchRemainingMs, false)
                          : std::string()) << ','
           << run.killIndices.size() << ','
           << run.headshotCount << ','
           << csvCell(etlfrag::formatDuration(run.endDemoTimeMs - run.startDemoTimeMs)) << ','
           << csvCell(toUtf8(describeRun(demo, run))) << "\r\n";
}

void writeRunJson(
    std::ostream& output,
    const etlfrag::DemoInfo& demo,
    const etlfrag::FragRun& run,
    const std::filesystem::path& displayPath) {
    const etlfrag::KillEvent* first =
        !run.killIndices.empty() && run.killIndices.front() < demo.kills.size()
            ? &demo.kills[run.killIndices.front()]
            : nullptr;
    output << "{\"demo_file\":" << jsonString(toUtf8(displayPath.wstring()))
           << ",\"map\":" << jsonString(demo.mapName)
           << ",\"recorded_pov\":" << jsonString(run.attackerName)
           << ",\"demo_time_ms\":" << run.startDemoTimeMs
           << ",\"match_remaining_ms\":"
           << (first != nullptr ? first->matchRemainingMs : -1)
           << ",\"kills\":" << run.killIndices.size()
           << ",\"headshots\":" << run.headshotCount
           << ",\"duration_ms\":" << run.endDemoTimeMs - run.startDemoTimeMs
           << ",\"events\":[";
    bool firstEvent = true;
    for (const std::size_t killIndex : run.killIndices) {
        if (killIndex >= demo.kills.size()) continue;
        const etlfrag::KillEvent& kill = demo.kills[killIndex];
        if (!firstEvent) output << ',';
        firstEvent = false;
        output << "{\"demo_time_ms\":" << kill.demoTimeMs
               << ",\"victim\":" << jsonString(kill.targetName)
               << ",\"weapon\":" << jsonString(etlfrag::weaponName(kill.weapon))
               << ",\"phase\":" << jsonString(etlfrag::matchPhaseName(kill.matchPhase))
               << ",\"headshot_kill\":" << (kill.headshot ? "true" : "false")
               << ",\"teamkill\":" << (kill.teamKill ? "true" : "false") << '}';
    }
    output << "]}";
}

void exportCurrentView() {
    if (!currentViewHasExportRows()) {
        MessageBoxW(
            gApp.window,
            L"There are no rows in the current view to export.",
            L"Nothing to export",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    const auto selectedPath = chooseExportPath();
    if (!selectedPath.has_value()) {
        return;
    }
    std::wstring extension = selectedPath->extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);
    const bool json = extension == L".json";
    std::ofstream output(*selectedPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(
            gApp.window,
            L"The export file could not be created.",
            L"Export error",
            MB_OK | MB_ICONERROR);
        return;
    }

    if (!json) {
        output.write("\xEF\xBB\xBF", 3);
    }

    if (gApp.activeTab == 0) {
        const std::vector<std::size_t> order = listDataInDisplayOrder(gApp.runList);
        if (json) {
            output << "{\"type\":\"multi_kills\",\"rows\":[";
            bool firstRow = true;
            for (const std::size_t index : order) {
                if (index >= gApp.runs.size()) continue;
                if (!firstRow) output << ',';
                writeRunJson(output, gApp.demo, gApp.runs[index], gApp.demo.path);
                firstRow = false;
            }
            output << "]}\n";
        } else {
            output << "Demo file,Map,Recorded POV,Demo time,Match clock,Kills,Headshots,Duration,Victims and weapons\r\n";
            for (const std::size_t index : order) {
                if (index < gApp.runs.size()) {
                    writeRunCsv(output, gApp.demo, gApp.runs[index], gApp.demo.path);
                }
            }
        }
    } else if (gApp.activeTab == 1) {
        if (json) output << "{\"type\":\"events\",\"rows\":[";
        else output << "Number,Demo file,Map,Demo time,Match clock,Attacker,Victim,Weapon,Headshot kill,Teamkill,Phase,Event type\r\n";
        bool firstExportedEvent = true;
        for (const std::size_t index : listDataInDisplayOrder(gApp.allEventList)) {
            if (index >= gApp.demo.kills.size()) continue;
            const etlfrag::KillEvent& kill = gApp.demo.kills[index];
            if (json) {
                if (!firstExportedEvent) output << ',';
                output << "{\"number\":" << index + 1
                       << ",\"demo_file\":" << jsonString(toUtf8(gApp.demo.path.wstring()))
                       << ",\"map\":" << jsonString(gApp.demo.mapName)
                       << ",\"demo_time_ms\":" << kill.demoTimeMs
                       << ",\"match_remaining_ms\":" << kill.matchRemainingMs
                       << ",\"attacker\":" << jsonString(kill.attackerName)
                       << ",\"victim\":" << jsonString(kill.targetName)
                       << ",\"weapon\":" << jsonString(etlfrag::weaponName(kill.weapon))
                       << ",\"headshot_kill\":" << (kill.headshot ? "true" : "false")
                       << ",\"teamkill\":" << (kill.teamKill ? "true" : "false")
                       << ",\"phase\":" << jsonString(etlfrag::matchPhaseName(kill.matchPhase))
                       << ",\"event_type\":" << jsonString(toUtf8(eventType(kill))) << '}';
            } else {
                output << index + 1 << ',' << csvCell(toUtf8(gApp.demo.path.wstring())) << ','
                       << csvCell(gApp.demo.mapName) << ','
                       << csvCell(etlfrag::formatDuration(kill.demoTimeMs)) << ','
                       << csvCell(kill.matchRemainingMs >= 0
                                      ? etlfrag::formatDuration(kill.matchRemainingMs, false)
                                      : std::string()) << ','
                       << csvCell(kill.attackerName) << ',' << csvCell(kill.targetName) << ','
                       << csvCell(etlfrag::weaponName(kill.weapon)) << ','
                       << (kill.headshot ? "true" : "false") << ','
                       << (kill.teamKill ? "true" : "false") << ','
                       << csvCell(etlfrag::matchPhaseName(kill.matchPhase)) << ','
                       << csvCell(toUtf8(eventType(kill))) << "\r\n";
            }
            firstExportedEvent = false;
        }
        if (json) output << "]}\n";
    } else if (gApp.activeTab == 2) {
        if (json) output << "{\"type\":\"folder_multi_kills\",\"rows\":[";
        else output << "Demo file,Map,Recorded POV,Demo time,Match clock,Kills,Headshots,Duration,Victims and weapons\r\n";
        bool firstRow = true;
        for (const std::size_t rowData : listDataInDisplayOrder(gApp.folderRunList)) {
            if (rowData >= gApp.folderRows.size()) continue;
            const std::size_t runIndex = gApp.folderRows[rowData];
            if (runIndex >= gApp.folderRuns.size()) continue;
            const FolderRunResult& result = gApp.folderRuns[runIndex];
            auto [demo, run] = folderRunForExport(result);
            if (json) {
                if (!firstRow) output << ',';
                writeRunJson(output, demo, run, relativeDemoPath(result.demo.path));
            } else {
                writeRunCsv(output, demo, run, relativeDemoPath(result.demo.path));
            }
            firstRow = false;
        }
        if (json) output << "]}\n";
    } else if (gApp.activeTab == 3) {
        if (json) output << "{\"type\":\"highlight_basket\",\"rows\":[";
        else output << "Demo file,Map,Recorded POV,Demo time,Match clock,Kills,Headshots,Duration,Victims and weapons\r\n";
        bool firstRow = true;
        for (const std::size_t index : listDataInDisplayOrder(gApp.highlightList)) {
            if (index >= gApp.highlights.size()) continue;
            const etlfrag::HighlightItem& highlight = gApp.highlights[index];
            if (json) {
                if (!firstRow) output << ',';
                output << "{\"demo_file\":" << jsonString(toUtf8(highlight.demoPath.wstring()))
                       << ",\"map\":" << jsonString(highlight.mapName)
                       << ",\"recorded_pov\":" << jsonString(highlight.povName)
                       << ",\"demo_time_ms\":" << highlight.startDemoTimeMs
                       << ",\"match_remaining_ms\":" << highlight.matchRemainingMs
                       << ",\"kills\":" << highlight.events.size()
                       << ",\"headshots\":" << highlight.headshotCount
                       << ",\"duration_ms\":"
                       << highlight.endDemoTimeMs - highlight.startDemoTimeMs
                       << ",\"description\":" << jsonString(highlight.description)
                       << ",\"events\":[";
                for (std::size_t eventIndex = 0; eventIndex < highlight.events.size(); ++eventIndex) {
                    const etlfrag::HighlightEvent& event = highlight.events[eventIndex];
                    if (eventIndex != 0) output << ',';
                    output << "{\"demo_time_ms\":" << event.demoTimeMs
                           << ",\"victim\":" << jsonString(event.victim)
                           << ",\"weapon\":" << jsonString(event.weapon)
                           << ",\"headshot_kill\":" << (event.headshot ? "true" : "false")
                           << ",\"teamkill\":" << (event.teamKill ? "true" : "false") << '}';
                }
                output << "]}";
            } else {
                output << csvCell(toUtf8(highlight.demoPath.wstring())) << ','
                       << csvCell(highlight.mapName) << ',' << csvCell(highlight.povName) << ','
                       << csvCell(etlfrag::formatDuration(highlight.startDemoTimeMs)) << ','
                       << csvCell(highlight.matchRemainingMs >= 0
                                      ? etlfrag::formatDuration(highlight.matchRemainingMs, false)
                                      : std::string()) << ','
                       << highlight.events.size() << ','
                       << highlight.headshotCount << ','
                       << csvCell(etlfrag::formatDuration(
                              highlight.endDemoTimeMs - highlight.startDemoTimeMs)) << ','
                       << csvCell(highlight.description) << "\r\n";
            }
            firstRow = false;
        }
        if (json) output << "]}\n";
    } else {
        if (json) output << "{\"type\":\"demo_library\",\"rows\":[";
        else output << "Demo file,Date,Map,Recorded POV,Players,Events,Duration,Duplicates,Full path\r\n";
        bool firstRow = true;
        for (const std::size_t index : listDataInDisplayOrder(gApp.libraryList)) {
            if (index >= gApp.libraryRows.size()) continue;
            const auto& demo = gApp.libraryRows[index];
            const std::int32_t duration =
                std::max<std::int32_t>(0, demo.lastServerTimeMs - demo.firstServerTimeMs);
            if (json) {
                if (!firstRow) output << ',';
                output << "{\"demo_file\":" << jsonString(demo.fileName)
                       << ",\"date\":" << jsonString(demo.recordedDate)
                       << ",\"map\":" << jsonString(demo.mapName)
                       << ",\"recorded_pov\":" << jsonString(demo.povName)
                       << ",\"players\":" << demo.playerCount
                       << ",\"events\":" << demo.eventCount
                       << ",\"duration_ms\":" << duration
                       << ",\"duplicates\":" << demo.duplicateCount
                       << ",\"path\":" << jsonString(toUtf8(demo.path.wstring())) << '}';
            } else {
                output << csvCell(demo.fileName) << ',' << csvCell(demo.recordedDate) << ','
                       << csvCell(demo.mapName) << ',' << csvCell(demo.povName) << ','
                       << demo.playerCount << ',' << demo.eventCount << ','
                       << csvCell(etlfrag::formatDuration(duration)) << ','
                       << demo.duplicateCount << ','
                       << csvCell(toUtf8(demo.path.wstring())) << "\r\n";
            }
            firstRow = false;
        }
        if (json) output << "]}\n";
    }

    output.close();
    if (!output) {
        MessageBoxW(
            gApp.window,
            L"The export could not be written completely.",
            L"Export error",
            MB_OK | MB_ICONERROR);
        return;
    }
    setStatus(L"Exported current results to " + selectedPath->wstring());
}

void setVisible(HWND control, bool visible) {
    ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

void showTab(int tab) {
    gApp.activeTab = std::clamp(tab, 0, 4);
    const bool multi = gApp.activeTab == 0;
    const bool events = gApp.activeTab == 1;
    const bool folder = gApp.activeTab == 2;
    const bool highlights = gApp.activeTab == 3;
    const bool library = gApp.activeTab == 4;
    gApp.timelineHoverMs = -1;
    setVisible(gApp.demoPath, multi || events);
    setVisible(gApp.openDemo, multi || events);
    setVisible(gApp.highlightInfo, highlights);
    for (HWND control : {
             gApp.playerLabel,
             gApp.player,
             gApp.minimumLabel,
             gApp.minimumKills,
             gApp.minimumHeadshotsLabel,
             gApp.minimumHeadshots,
             gApp.gapLabel,
             gApp.maximumGap,
             gApp.weaponLabel,
             gApp.weapon,
             gApp.teamKills,
             gApp.warmupKills,
             gApp.postDeathExplosives,
             gApp.postDeathWindowLabel,
             gApp.postDeathWindow,
             gApp.search,
             gApp.chooseEtl,
             gApp.playRun,
             gApp.addRunHighlight,
             gApp.runList,
             gApp.runKillList}) {
        setVisible(control, multi);
    }
    setVisible(gApp.allEventList, events);
    setVisible(gApp.eventPlayerLabel, events);
    setVisible(gApp.eventPlayer, events);
    setVisible(gApp.playEvent, events);
    setVisible(gApp.viewProtocolLog, events);
    for (HWND control : {
             gApp.folderPath,
             gApp.chooseFolder,
             gApp.folderMinimumLabel,
             gApp.folderMinimumKills,
             gApp.folderMinimumHeadshotsLabel,
             gApp.folderMinimumHeadshots,
             gApp.folderGapLabel,
             gApp.folderMaximumGap,
             gApp.folderWeaponLabel,
             gApp.folderWeapon,
             gApp.folderTeamKills,
             gApp.folderWarmupKills,
             gApp.folderPostDeathExplosives,
             gApp.folderPostDeathWindowLabel,
             gApp.folderPostDeathWindow,
             gApp.folderQueryLabel,
             gApp.folderQuery,
             gApp.folderFieldLabel,
             gApp.folderField,
             gApp.folderApplyFilters,
             gApp.folderScan,
             gApp.playFolderRun,
             gApp.addFolderHighlight,
             gApp.folderRunList,
             gApp.folderKillList,
             gApp.folderWatch}) {
        setVisible(control, folder);
    }
    for (HWND control : {
             gApp.playHighlight,
             gApp.removeHighlight,
             gApp.clearHighlights,
             gApp.highlightList}) {
        setVisible(control, highlights);
    }
    for (HWND control : {
             gApp.libraryQueryLabel,
             gApp.libraryQuery,
             gApp.libraryFieldLabel,
             gApp.libraryField,
             gApp.libraryScope,
             gApp.libraryDuplicates,
             gApp.librarySearch,
             gApp.libraryOpen,
             gApp.libraryList}) {
        setVisible(control, library);
    }
    setVisible(gApp.timeline, !library);
    setVisible(gApp.exportCurrent, true);
    RECT client{};
    GetClientRect(gApp.window, &client);
    const int timelineY = multi ? scale(380) : (folder ? scale(444) : scale(310));
    MoveWindow(
        gApp.timeline,
        scale(30),
        timelineY,
        std::max(scale(1), static_cast<int>(client.right) - scale(60)),
        scale(60),
        TRUE);
    updateWindowTitle();
    InvalidateRect(gApp.timeline, nullptr, FALSE);
    InvalidateRect(gApp.window, nullptr, FALSE);
}

void drawTextBlock(
    HDC dc,
    const std::wstring& text,
    RECT rect,
    HFONT font,
    COLORREF color,
    UINT format) {
    const HFONT previous = reinterpret_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, format);
    SelectObject(dc, previous);
}

void fillRoundedRect(HDC dc, const RECT& rect, COLORREF color, int radius) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ oldBrush = SelectObject(dc, brush);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void fillSolidRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

struct TimelineContext {
    const etlfrag::DemoInfo* demo = nullptr;
    const std::vector<etlfrag::FragRun>* runs = nullptr;
    const etlfrag::HighlightItem* highlight = nullptr;
    std::int64_t folderDemoId = -1;
    std::size_t selectedRunIndex = static_cast<std::size_t>(-1);
    int focusPlayerClientNum = -1;
    int focusPlayerSessionId = -1;
    bool onlyFocusPlayerEvents = false;
};

TimelineContext currentTimelineContext() {
    TimelineContext context;
    if (gApp.activeTab == 0 && gApp.demoLoaded) {
        context.demo = &gApp.demo;
        context.runs = &gApp.runs;
        const int selection = static_cast<int>(SendMessageW(gApp.player, CB_GETCURSEL, 0, 0));
        if (selection >= 0 && selection < static_cast<int>(gApp.playerIds.size())) {
            const PlayerSelection player = gApp.playerIds[static_cast<std::size_t>(selection)];
            context.focusPlayerClientNum = player.clientNum;
            context.focusPlayerSessionId = player.sessionId;
        }
        if (context.focusPlayerClientNum < 0) context.focusPlayerClientNum = gApp.demo.povClientNum;
    } else if (gApp.activeTab == 1 && gApp.demoLoaded) {
        context.demo = &gApp.demo;
        const PlayerSelection player = selectedEventPlayer();
        context.focusPlayerClientNum = player.clientNum;
        context.focusPlayerSessionId = player.sessionId;
        if (context.focusPlayerClientNum < 0) {
            context.focusPlayerClientNum = gApp.demo.povClientNum;
            context.runs = &gApp.runs;
        } else {
            context.onlyFocusPlayerEvents = true;
        }
    } else if (gApp.activeTab == 2) {
        if (gApp.folderTimelineDemoId >= 0) {
            context.demo = &gApp.folderTimelineDemo;
            context.runs = &gApp.folderTimelineRuns;
            context.folderDemoId = gApp.folderTimelineDemoId;
            context.selectedRunIndex = gApp.folderTimelineSelectedRun;
            context.focusPlayerClientNum = context.demo->povClientNum;
            context.onlyFocusPlayerEvents = true;
        }
    } else if (gApp.activeTab == 3) {
        const int highlightIndex = selectedListData(gApp.highlightList);
        if (highlightIndex >= 0 && highlightIndex < static_cast<int>(gApp.highlights.size())) {
            context.highlight = &gApp.highlights[static_cast<std::size_t>(highlightIndex)];
        }
    }
    return context;
}

struct TimelineRange {
    std::int32_t startMs = 0;
    std::int32_t endMs = 1;
};

TimelineRange timelineRange(const TimelineContext& context) {
    TimelineRange range;
    if (context.demo != nullptr) {
        if (context.demo->firstServerTimeMs >= 0 && context.demo->lastServerTimeMs >= 0) {
            range.endMs = context.demo->lastServerTimeMs - context.demo->firstServerTimeMs;
        }
        for (const etlfrag::KillEvent& kill : context.demo->kills) {
            range.endMs = std::max(range.endMs, kill.demoTimeMs);
        }
    } else if (context.highlight != nullptr) {
        range.startMs = std::max<std::int32_t>(0, context.highlight->startDemoTimeMs - 10000);
        range.endMs = std::max<std::int32_t>(
            range.startMs + 1,
            context.highlight->endDemoTimeMs + 10000);
    }
    range.endMs = std::max<std::int32_t>(range.endMs, range.startMs + 1);
    return range;
}

int timelineX(std::int32_t timeMs, const TimelineRange& range, int left, int right) {
    const double ratio = std::clamp(
        static_cast<double>(timeMs - range.startMs) /
            static_cast<double>(std::max<std::int32_t>(range.endMs - range.startMs, 1)),
        0.0,
        1.0);
    return left + static_cast<int>(std::lround(ratio * (right - left)));
}

void selectListItemByData(HWND list, LPARAM wantedData) {
    const int count = ListView_GetItemCount(list);
    for (int row = 0; row < count; ++row) {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = row;
        if (ListView_GetItem(list, &item) && item.lParam == wantedData) {
            ListView_SetItemState(
                list,
                row,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, row, FALSE);
            SetFocus(list);
            return;
        }
    }
}

void selectTimelineTime(std::int32_t timeMs) {
    const TimelineContext context = currentTimelineContext();
    if (gApp.activeTab == 0 && context.runs != nullptr && !context.runs->empty()) {
        std::size_t best = 0;
        std::int64_t distance = std::numeric_limits<std::int64_t>::max();
        for (std::size_t index = 0; index < context.runs->size(); ++index) {
            const std::int64_t current = std::llabs(
                static_cast<std::int64_t>((*context.runs)[index].startDemoTimeMs) - timeMs);
            if (current < distance) {
                distance = current;
                best = index;
            }
        }
        selectListItemByData(gApp.runList, static_cast<LPARAM>(best));
    } else if (gApp.activeTab == 1 && context.demo != nullptr && !context.demo->kills.empty()) {
        std::size_t best = 0;
        std::int64_t distance = std::numeric_limits<std::int64_t>::max();
        bool found = false;
        for (std::size_t index = 0; index < context.demo->kills.size(); ++index) {
            if (context.onlyFocusPlayerEvents &&
                !eventInvolvesPlayer(
                    context.demo->kills[index],
                    {context.focusPlayerClientNum, context.focusPlayerSessionId})) {
                continue;
            }
            const std::int64_t current = std::llabs(
                static_cast<std::int64_t>(context.demo->kills[index].demoTimeMs) - timeMs);
            if (current < distance) {
                distance = current;
                best = index;
                found = true;
            }
        }
        if (found) {
            selectListItemByData(gApp.allEventList, static_cast<LPARAM>(best));
        }
    } else if (gApp.activeTab == 2 && context.runs != nullptr && !context.runs->empty()) {
        std::size_t bestRun = 0;
        std::int64_t distance = std::numeric_limits<std::int64_t>::max();
        for (std::size_t index = 0; index < context.runs->size(); ++index) {
            const std::int64_t current = std::llabs(
                static_cast<std::int64_t>((*context.runs)[index].startDemoTimeMs) - timeMs);
            if (current < distance) {
                distance = current;
                bestRun = index;
            }
        }
        const etlfrag::FragRun& wanted = (*context.runs)[bestRun];
        for (std::size_t row = 0; row < gApp.folderRows.size(); ++row) {
            const std::size_t resultIndex = gApp.folderRows[row];
            if (resultIndex >= gApp.folderRuns.size()) continue;
            const FolderRunResult& candidate = gApp.folderRuns[resultIndex];
            if (candidate.demo.id == context.folderDemoId &&
                candidate.run.startDemoTimeMs == wanted.startDemoTimeMs &&
                candidate.run.endDemoTimeMs == wanted.endDemoTimeMs &&
                candidate.run.attackerSessionId == wanted.attackerSessionId) {
                selectListItemByData(gApp.folderRunList, static_cast<LPARAM>(row));
                break;
            }
        }
    }
    setStatus(L"Timeline position: " + durationText(timeMs));
    InvalidateRect(gApp.timeline, nullptr, FALSE);
}

void paintTimeline(HWND window, HDC target) {
    RECT client{};
    GetClientRect(window, &client);
    if (client.right <= 0 || client.bottom <= 0) {
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    const HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    fillSolidRect(dc, client, kControl);

    const TimelineContext context = currentTimelineContext();
    const TimelineRange range = timelineRange(context);
    const int left = scale(14);
    const int right = std::max(left + 1, static_cast<int>(client.right) - scale(14));
    const int axisY = client.bottom - scale(17);

    RECT titleRect{left, scale(4), right, scale(20)};
    if (gApp.timelineHoverMs >= 0) titleRect.right -= scale(135);
    std::wstring title = L"Timeline";
    if (context.demo != nullptr) {
        if (gApp.activeTab == 2) {
            title += L"  •  " + context.demo->path.filename().wstring() +
                     L"  •  selected demo only  •  green: POV kill  red: POV death  amber: warmup  blue: multi-kill";
        } else if (gApp.activeTab == 1 && context.onlyFocusPlayerEvents) {
            title += L"  •  " + playerDisplayName(
                         context.focusPlayerClientNum,
                         context.focusPlayerSessionId) +
                     L"  •  matching player events only  •  green: kill  red: death/teamkill  amber: warmup";
        } else {
            title += L"  •  " + toWide(context.demo->mapName) +
                     L"  •  green: POV kill  red: death  amber: warmup  blue: multi-kill";
        }
    } else if (context.highlight != nullptr) {
        title += L"  •  saved highlight range";
    } else {
        title += L"  •  select a result to display demo activity";
    }
    drawTextBlock(
        dc,
        title,
        titleRect,
        gApp.smallFont,
        kMuted,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (context.runs != nullptr) {
        for (std::size_t runIndex = 0; runIndex < context.runs->size(); ++runIndex) {
            const etlfrag::FragRun& run = (*context.runs)[runIndex];
            int start = timelineX(run.startDemoTimeMs, range, left, right);
            int end = timelineX(run.endDemoTimeMs, range, left, right);
            end = std::max(end, start + scale(3));
            const bool selectedFolderRun =
                gApp.activeTab == 2 && runIndex == context.selectedRunIndex;
            RECT runRect{
                start,
                selectedFolderRun ? scale(20) : scale(23),
                std::min(right, end),
                selectedFolderRun ? scale(31) : scale(28)};
            fillSolidRect(
                dc,
                runRect,
                selectedFolderRun ? RGB(124, 185, 245) : kAccent);
        }
    }

    if (context.highlight != nullptr) {
        const int start = timelineX(context.highlight->startDemoTimeMs, range, left, right);
        const int end = std::max(
            start + scale(4),
            timelineX(context.highlight->endDemoTimeMs, range, left, right));
        RECT highlightRect{start, scale(22), std::min(right, end), scale(31)};
        fillSolidRect(dc, highlightRect, kAccent);
        for (const etlfrag::HighlightEvent& event : context.highlight->events) {
            const int x = timelineX(event.demoTimeMs, range, left, right);
            RECT marker{x - scale(1), scale(30), x + scale(2), axisY};
            fillSolidRect(dc, marker, event.teamKill ? kDanger : kSuccess);
        }
    }

    if (context.demo != nullptr) {
        for (const etlfrag::KillEvent& kill : context.demo->kills) {
            if (context.onlyFocusPlayerEvents &&
                !eventInvolvesPlayer(
                    kill,
                    {context.focusPlayerClientNum, context.focusPlayerSessionId})) {
                continue;
            }
            const int x = timelineX(kill.demoTimeMs, range, left, right);
            COLORREF color = RGB(89, 98, 109);
            int top = scale(36);
            int halfWidth = 0;
            if (context.focusPlayerClientNum >= 0 &&
                kill.target == context.focusPlayerClientNum &&
                (context.focusPlayerSessionId < 0 ||
                 kill.targetSessionId == context.focusPlayerSessionId)) {
                color = kDanger;
                top = scale(29);
                halfWidth = scale(1);
            } else if (context.focusPlayerClientNum >= 0 &&
                       kill.attacker == context.focusPlayerClientNum &&
                       (context.focusPlayerSessionId < 0 ||
                        kill.attackerSessionId == context.focusPlayerSessionId) &&
                       !kill.suicide) {
                color = kill.teamKill ? kDanger : kSuccess;
                top = scale(31);
                halfWidth = scale(1);
            }
            if (kill.matchPhase == etlfrag::MatchPhase::Warmup) {
                color = kWarmup;
            }
            RECT marker{x - halfWidth, top, x + halfWidth + 1, axisY};
            fillSolidRect(dc, marker, color);
        }
    }

    std::int32_t selectedTimeMs = -1;
    if (gApp.activeTab == 0) {
        const int index = selectedListData(gApp.runList);
        if (index >= 0 && index < static_cast<int>(gApp.runs.size())) {
            selectedTimeMs = gApp.runs[static_cast<std::size_t>(index)].startDemoTimeMs;
        }
    } else if (gApp.activeTab == 1) {
        const int index = selectedListData(gApp.allEventList);
        if (index >= 0 && index < static_cast<int>(gApp.demo.kills.size())) {
            selectedTimeMs = gApp.demo.kills[static_cast<std::size_t>(index)].demoTimeMs;
        }
    } else if (gApp.activeTab == 2) {
        const int rowData = selectedListData(gApp.folderRunList);
        if (rowData >= 0 && rowData < static_cast<int>(gApp.folderRows.size())) {
            const std::size_t index = gApp.folderRows[static_cast<std::size_t>(rowData)];
            if (index < gApp.folderRuns.size()) selectedTimeMs = gApp.folderRuns[index].run.startDemoTimeMs;
        }
    } else if (context.highlight != nullptr) {
        selectedTimeMs = context.highlight->startDemoTimeMs;
    }
    if (selectedTimeMs >= range.startMs && selectedTimeMs <= range.endMs) {
        const int selectedX = timelineX(selectedTimeMs, range, left, right);
        HPEN selectedPen = CreatePen(PS_SOLID, scale(2), RGB(231, 237, 245));
        const HGDIOBJ oldSelectedPen = SelectObject(dc, selectedPen);
        MoveToEx(dc, selectedX, scale(20), nullptr);
        LineTo(dc, selectedX, axisY + scale(1));
        SelectObject(dc, oldSelectedPen);
        DeleteObject(selectedPen);
    }

    HPEN axisPen = CreatePen(PS_SOLID, 1, kBorder);
    const HGDIOBJ oldPen = SelectObject(dc, axisPen);
    MoveToEx(dc, left, axisY, nullptr);
    LineTo(dc, right, axisY);
    SelectObject(dc, oldPen);
    DeleteObject(axisPen);

    RECT startText{left, axisY + scale(2), left + scale(70), client.bottom};
    drawTextBlock(
        dc,
        durationText(range.startMs, false),
        startText,
        gApp.smallFont,
        kMuted,
        DT_LEFT | DT_SINGLELINE);
    RECT endText{right - scale(100), axisY + scale(2), right, client.bottom};
    drawTextBlock(
        dc,
        durationText(range.endMs, false),
        endText,
        gApp.smallFont,
        kMuted,
        DT_RIGHT | DT_SINGLELINE);

    if (gApp.timelineHoverMs >= 0) {
        const int hoverX = timelineX(gApp.timelineHoverMs, range, left, right);
        HPEN hoverPen = CreatePen(PS_SOLID, 1, RGB(223, 229, 237));
        const HGDIOBJ oldHoverPen = SelectObject(dc, hoverPen);
        MoveToEx(dc, hoverX, scale(20), nullptr);
        LineTo(dc, hoverX, axisY + scale(1));
        SelectObject(dc, oldHoverPen);
        DeleteObject(hoverPen);
        RECT hoverText{std::max(left, right - scale(125)), scale(4), right, scale(20)};
        drawTextBlock(
            dc,
            durationText(gApp.timelineHoverMs),
            hoverText,
            gApp.smallFont,
            kText,
            DT_RIGHT | DT_SINGLELINE);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

LRESULT CALLBACK timelineProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            paintTimeline(window, dc);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_MOUSEMOVE: {
            RECT client{};
            GetClientRect(window, &client);
            const int left = scale(14);
            const int right =
                std::max(left + 1, static_cast<int>(client.right) - scale(14));
            const int mouseX = std::clamp(GET_X_LPARAM(lParam), left, right);
            const TimelineRange range = timelineRange(currentTimelineContext());
            gApp.timelineHoverMs = static_cast<std::int32_t>(std::lround(
                range.startMs +
                static_cast<double>(mouseX - left) / static_cast<double>(right - left) *
                    (range.endMs - range.startMs)));
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = window;
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE:
            gApp.timelineHoverMs = -1;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            if (gApp.timelineHoverMs >= 0) {
                selectTimelineTime(gApp.timelineHoverMs);
            }
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void drawMetric(HDC dc, RECT rect, const wchar_t* label, const std::wstring& value) {
    rect.left += scale(16);
    rect.right -= scale(16);
    RECT labelRect = rect;
    labelRect.top += scale(10);
    labelRect.bottom = labelRect.top + scale(17);
    drawTextBlock(dc, label, labelRect, gApp.smallFont, kMuted, DT_LEFT | DT_SINGLELINE);
    RECT valueRect = rect;
    valueRect.top += scale(29);
    valueRect.bottom -= scale(8);
    drawTextBlock(
        dc,
        value,
        valueRect,
        gApp.labelFont,
        kText,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_VCENTER);
}

void drawListHeader(const DRAWITEMSTRUCT& item) {
    fillSolidRect(item.hDC, item.rcItem, kHeader);

    HPEN separatorPen = CreatePen(PS_SOLID, 1, kBorder);
    const HGDIOBJ oldPen = SelectObject(item.hDC, separatorPen);
    MoveToEx(item.hDC, item.rcItem.right - 1, item.rcItem.top, nullptr);
    LineTo(item.hDC, item.rcItem.right - 1, item.rcItem.bottom);
    MoveToEx(item.hDC, item.rcItem.left, item.rcItem.bottom - 1, nullptr);
    LineTo(item.hDC, item.rcItem.right, item.rcItem.bottom - 1);
    SelectObject(item.hDC, oldPen);
    DeleteObject(separatorPen);

    wchar_t text[256]{};
    HDITEMW headerItem{};
    headerItem.mask = HDI_TEXT | HDI_FORMAT;
    headerItem.pszText = text;
    headerItem.cchTextMax = static_cast<int>(std::size(text));
    Header_GetItem(
        item.hwndItem,
        static_cast<int>(item.itemID),
        &headerItem);

    RECT textRect = item.rcItem;
    textRect.left += scale(8);
    textRect.right -= scale(6);
    const bool sortedAscending = (headerItem.fmt & HDF_SORTUP) != 0;
    const bool sortedDescending = (headerItem.fmt & HDF_SORTDOWN) != 0;
    if (sortedAscending || sortedDescending) {
        textRect.right -= scale(17);
        const int centerX = item.rcItem.right - scale(11);
        const int centerY = (item.rcItem.top + item.rcItem.bottom) / 2;
        POINT arrow[3]{};
        if (sortedAscending) {
            arrow[0] = {centerX, centerY - scale(3)};
            arrow[1] = {centerX - scale(4), centerY + scale(3)};
            arrow[2] = {centerX + scale(4), centerY + scale(3)};
        } else {
            arrow[0] = {centerX - scale(4), centerY - scale(3)};
            arrow[1] = {centerX + scale(4), centerY - scale(3)};
            arrow[2] = {centerX, centerY + scale(3)};
        }
        HBRUSH arrowBrush = CreateSolidBrush(kAccent);
        HPEN arrowPen = CreatePen(PS_SOLID, 1, kAccent);
        const HGDIOBJ oldArrowBrush = SelectObject(item.hDC, arrowBrush);
        const HGDIOBJ oldArrowPen = SelectObject(item.hDC, arrowPen);
        Polygon(item.hDC, arrow, 3);
        SelectObject(item.hDC, oldArrowPen);
        SelectObject(item.hDC, oldArrowBrush);
        DeleteObject(arrowPen);
        DeleteObject(arrowBrush);
    }
    drawTextBlock(
        item.hDC,
        text,
        textRect,
        gApp.smallFont,
        RGB(194, 203, 214),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

LRESULT CALLBACK listViewSubclass(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData) {
    (void)wParam;
    (void)referenceData;
    if (message == WM_DRAWITEM) {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item != nullptr && item->CtlType == ODT_HEADER) {
            drawListHeader(*item);
            return TRUE;
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, listViewSubclass, subclassId);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void paintWindow(HDC target) {
    RECT client{};
    GetClientRect(gApp.window, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    const HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    FillRect(dc, &client, gApp.backgroundBrush);

    RECT headerRect{0, 0, client.right, scale(70)};
    HBRUSH headerBrush = CreateSolidBrush(kHeader);
    FillRect(dc, &headerRect, headerBrush);
    DeleteObject(headerBrush);

    RECT titleRect{scale(20), scale(8), client.right - scale(20), scale(41)};
    drawTextBlock(dc, kApplicationName, titleRect, gApp.titleFont, kText, DT_LEFT | DT_SINGLELINE);
    RECT subtitleRect{scale(21), scale(41), client.right - scale(20), scale(63)};
    drawTextBlock(
        dc,
        L"Index dm_84 collections, find multi-kills, build a highlight basket, and export results.",
        subtitleRect,
        gApp.smallFont,
        kMuted,
        DT_LEFT | DT_SINGLELINE);

    const int margin = scale(20);
    const int summaryY = scale(132);
    const int summaryHeight = scale(64);
    const int fieldWidth = (client.right - margin * 2) / 4;
    std::wstring firstLabel = L"Map";
    std::wstring secondLabel = L"Recorded POV";
    std::wstring thirdLabel = L"Demo duration";
    std::wstring fourthLabel = L"Indexed events";
    std::wstring firstValue = gApp.demoLoaded ? toWide(gApp.demo.mapName) : L"No demo loaded";
    std::wstring secondValue = gApp.demoLoaded ? toWide(gApp.demo.povName) : L"—";
    std::wstring thirdValue = gApp.demoLoaded
                                 ? durationText(
                                       gApp.demo.lastServerTimeMs - gApp.demo.firstServerTimeMs)
                                 : L"—";
    std::wstring fourthValue =
        gApp.demoLoaded ? std::to_wstring(gApp.demo.kills.size()) : L"—";
    if (gApp.activeTab == 2) {
        firstLabel = L"Folder";
        secondLabel = L"Demos found";
        thirdLabel = L"Indexed POV demos";
        fourthLabel = L"Visible multi-kills";
        if (gApp.demoFolder.empty()) {
            firstValue = L"No folder selected";
        } else {
            firstValue = gApp.demoFolder.filename().wstring();
            if (firstValue.empty()) {
                firstValue = gApp.demoFolder.wstring();
            }
        }
        secondValue = std::to_wstring(gApp.folderFilesFound);
        thirdValue = std::to_wstring(static_cast<std::size_t>(std::count_if(
            gApp.folderDemos.begin(), gApp.folderDemos.end(),
            [](const etlfrag::IndexedDemoSummary& demo) { return demo.povClientNum >= 0; })));
        fourthValue = std::to_wstring(gApp.folderRows.size());
    } else if (gApp.activeTab == 3) {
        firstLabel = L"Saved highlights";
        secondLabel = L"Total kills";
        thirdLabel = L"Demo files";
        fourthLabel = L"Storage";
        firstValue = std::to_wstring(gApp.highlights.size());
        std::size_t totalKills = 0;
        std::set<std::wstring> demoFiles;
        for (const etlfrag::HighlightItem& highlight : gApp.highlights) {
            totalKills += highlight.events.size();
            demoFiles.insert(highlight.demoPath.wstring());
        }
        secondValue = std::to_wstring(totalKills);
        thirdValue = std::to_wstring(demoFiles.size());
        fourthValue = L"Saved automatically";
    } else if (gApp.activeTab == 4) {
        firstLabel = L"Indexed demos";
        secondLabel = L"Search results";
        thirdLabel = L"Duplicate files";
        fourthLabel = L"Index storage";
        firstValue = std::to_wstring(gApp.indexedDemoCount);
        secondValue = std::to_wstring(gApp.libraryRows.size());
        thirdValue = std::to_wstring(static_cast<std::size_t>(std::count_if(
            gApp.libraryRows.begin(), gApp.libraryRows.end(),
            [](const etlfrag::IndexedDemoSummary& demo) { return demo.duplicateCount > 1; })));
        fourthValue = L"SQLite on disk";
    }
    RECT summary{margin, summaryY, client.right - margin, summaryY + summaryHeight};
    fillRoundedRect(dc, summary, kPanel, scale(4));
    drawMetric(
        dc,
        {margin, summaryY, margin + fieldWidth, summaryY + summaryHeight},
        firstLabel.c_str(),
        firstValue);
    drawMetric(
        dc,
        {margin + fieldWidth, summaryY,
         margin + fieldWidth * 2, summaryY + summaryHeight},
        secondLabel.c_str(),
        secondValue);
    drawMetric(
        dc,
        {margin + fieldWidth * 2, summaryY,
         margin + fieldWidth * 3, summaryY + summaryHeight},
        thirdLabel.c_str(),
        thirdValue);
    drawMetric(
        dc,
        {margin + fieldWidth * 3, summaryY,
         client.right - margin, summaryY + summaryHeight},
        fourthLabel.c_str(),
        fourthValue);
    HPEN dividerPen = CreatePen(PS_SOLID, 1, kBorder);
    const HGDIOBJ oldDividerPen = SelectObject(dc, dividerPen);
    for (int field = 1; field < 4; ++field) {
        const int x = margin + fieldWidth * field;
        MoveToEx(dc, x, summaryY + scale(12), nullptr);
        LineTo(dc, x, summaryY + summaryHeight - scale(12));
    }
    SelectObject(dc, oldDividerPen);
    DeleteObject(dividerPen);

    const int contentTop = scale(252);
    const int contentBottom = client.bottom - scale(38);
    RECT content{margin, contentTop, client.right - margin, contentBottom};
    fillRoundedRect(dc, content, kPanel, scale(4));

    if (gApp.activeTab == 1) {
        RECT eventTitle{
            margin + scale(630),
            contentTop + scale(16),
            client.right - scale(270),
            contentTop + scale(41)};
        drawTextBlock(
            dc,
            L"Chronological event log — warmup remains visible and labelled for manual review.",
            eventTitle,
            gApp.font,
            kMuted,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

bool isPrimaryButton(int id) {
    return id == IdOpenDemo || id == IdSearch || id == IdFolderScan ||
           id == IdExportCurrent || id == IdLibrarySearch;
}

void drawOwnerButton(const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(item.CtlID);
    const bool tab =
        id == IdTabMultiKills || id == IdTabAllEvents || id == IdTabFolderScan ||
        id == IdTabHighlights || id == IdTabLibrary;
    const bool activeTab = (id == IdTabMultiKills && gApp.activeTab == 0) ||
                           (id == IdTabAllEvents && gApp.activeTab == 1) ||
                           (id == IdTabFolderScan && gApp.activeTab == 2) ||
                           (id == IdTabHighlights && gApp.activeTab == 3) ||
                           (id == IdTabLibrary && gApp.activeTab == 4);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;

    if (id == IdTeamKills || id == IdFolderTeamKills ||
        id == IdWarmupKills || id == IdFolderWarmupKills ||
        id == IdPostDeathExplosives || id == IdFolderPostDeathExplosives ||
        id == IdFolderWatch || id == IdLibraryScope || id == IdLibraryDuplicates ||
        id == IdLaunchAsAdministrator || id == IdLaunchWithoutSeeking) {
        bool checked = false;
        const wchar_t* label = L"";
        if (id == IdTeamKills) {
            checked = gApp.includeTeamKills;
            label = L"Include teamkills";
        } else if (id == IdFolderTeamKills) {
            checked = gApp.folderIncludeTeamKills;
            label = L"Include teamkills";
        } else if (id == IdWarmupKills) {
            checked = gApp.includeWarmupKills;
            label = L"Include warmup kills";
        } else if (id == IdFolderWarmupKills) {
            checked = gApp.folderIncludeWarmupKills;
            label = L"Include warmup kills";
        } else if (id == IdPostDeathExplosives) {
            checked = gApp.postDeathExplosivesEnabled;
            label = L"Post-death explosives";
        } else if (id == IdFolderPostDeathExplosives) {
            checked = gApp.folderPostDeathExplosivesEnabled;
            label = L"Post-death explosives";
        } else if (id == IdFolderWatch) {
            checked = gApp.folderWatchEnabled;
            label = L"Auto-index new demos";
        } else if (id == IdLibraryScope) {
            checked = gApp.libraryFolderOnly;
            label = L"Selected folder only";
        } else if (id == IdLibraryDuplicates) {
            checked = gApp.libraryDuplicatesOnly;
            label = L"Duplicates only";
        } else if (id == IdLaunchAsAdministrator) {
            checked = gApp.launchEtlAsAdministrator;
            label = L"Launch ETL as administrator";
        } else {
            checked = gApp.launchWithoutSeeking;
            label = L"Launch without seeking";
        }
        fillSolidRect(item.hDC, item.rcItem, kPanel);
        const int boxSize = scale(16);
        const int boxTop = item.rcItem.top + (item.rcItem.bottom - item.rcItem.top - boxSize) / 2;
        RECT box{item.rcItem.left + scale(1), boxTop,
                 item.rcItem.left + scale(1) + boxSize, boxTop + boxSize};
        const COLORREF boxColor = checked ? kAccent : kControl;
        HBRUSH boxBrush = CreateSolidBrush(boxColor);
        HPEN boxPen = CreatePen(PS_SOLID, 1, checked ? kAccent : kBorder);
        const HGDIOBJ oldBoxBrush = SelectObject(item.hDC, boxBrush);
        const HGDIOBJ oldBoxPen = SelectObject(item.hDC, boxPen);
        Rectangle(item.hDC, box.left, box.top, box.right, box.bottom);
        SelectObject(item.hDC, oldBoxPen);
        SelectObject(item.hDC, oldBoxBrush);
        DeleteObject(boxPen);
        DeleteObject(boxBrush);

        if (checked) {
            HPEN checkPen = CreatePen(PS_SOLID, scale(2), RGB(255, 255, 255));
            const HGDIOBJ oldCheckPen = SelectObject(item.hDC, checkPen);
            MoveToEx(item.hDC, box.left + scale(4), box.top + scale(8), nullptr);
            LineTo(item.hDC, box.left + scale(7), box.top + scale(11));
            LineTo(item.hDC, box.left + scale(13), box.top + scale(4));
            SelectObject(item.hDC, oldCheckPen);
            DeleteObject(checkPen);
        }

        RECT labelRect{box.right + scale(9), item.rcItem.top, item.rcItem.right, item.rcItem.bottom};
        drawTextBlock(
            item.hDC,
            label,
            labelRect,
            gApp.font,
            disabled ? kMuted : kText,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    if (tab) {
        fillSolidRect(item.hDC, item.rcItem, pressed ? kControl : kBackground);
        if (activeTab) {
            RECT underline{item.rcItem.left, item.rcItem.bottom - scale(3),
                           item.rcItem.right, item.rcItem.bottom};
            fillSolidRect(item.hDC, underline, kAccent);
        }
        RECT textRect = item.rcItem;
        textRect.bottom -= scale(3);
        drawTextBlock(
            item.hDC,
            [&]() {
                const int length = GetWindowTextLengthW(item.hwndItem);
                std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
                if (length > 0) {
                    GetWindowTextW(item.hwndItem, text.data(), length + 1);
                }
                text.resize(static_cast<std::size_t>(length));
                return text;
            }(),
            textRect,
            gApp.labelFont,
            activeTab ? kText : kMuted,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    COLORREF background = kControl;
    COLORREF foreground = disabled ? kMuted : kText;
    COLORREF border = kBorder;
    if (disabled) {
        background = kControl;
    } else if (isPrimaryButton(id)) {
        background = pressed ? kAccentPressed : kAccent;
        foreground = RGB(255, 255, 255);
        border = background;
    } else if (pressed) {
        background = kControlPressed;
    }

    HBRUSH brush = CreateSolidBrush(background);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    const HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
    const HGDIOBJ oldPen = SelectObject(item.hDC, pen);
    RoundRect(
        item.hDC,
        item.rcItem.left,
        item.rcItem.top,
        item.rcItem.right,
        item.rcItem.bottom,
        scale(4),
        scale(4));
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    const int textLength = GetWindowTextLengthW(item.hwndItem);
    std::wstring text(static_cast<std::size_t>(textLength + 1), L'\0');
    if (textLength > 0) {
        GetWindowTextW(item.hwndItem, text.data(), textLength + 1);
    }
    text.resize(static_cast<std::size_t>(textLength));
    RECT textRect = item.rcItem;
    if (pressed) {
        OffsetRect(&textRect, 0, 1);
    }
    drawTextBlock(
        item.hDC,
        text,
        textRect,
        gApp.labelFont,
        foreground,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

LRESULT handleListCustomDraw(const NMLVCUSTOMDRAW& draw, HWND list) {
    if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw.nmcd.dwDrawStage != CDDS_ITEMPREPAINT) {
        return CDRF_DODEFAULT;
    }

    auto& mutableDraw = const_cast<NMLVCUSTOMDRAW&>(draw);
    mutableDraw.clrTextBk = kPanel;
    mutableDraw.clrText = kText;
    const int data = static_cast<int>(draw.nmcd.lItemlParam);
    if (list == gApp.allEventList || list == gApp.runKillList) {
        if (data >= 0 && data < static_cast<int>(gApp.demo.kills.size())) {
            const etlfrag::KillEvent& kill = gApp.demo.kills[static_cast<std::size_t>(data)];
            if (kill.teamKill || kill.suicide) {
                mutableDraw.clrText = kDanger;
            } else if (kill.matchPhase == etlfrag::MatchPhase::Warmup) {
                mutableDraw.clrText = kWarmup;
            } else if (kill.headshot) {
                mutableDraw.clrText = kSuccess;
            }
        }
    } else if (list == gApp.folderKillList && data >= 0 &&
               data < static_cast<int>(gApp.folderKillRows.size())) {
        const auto [runIndex, killIndex] =
            gApp.folderKillRows[static_cast<std::size_t>(data)];
        if (runIndex < gApp.folderRuns.size() &&
            killIndex < gApp.folderRuns[runIndex].kills.size()) {
            const etlfrag::KillEvent& kill =
                gApp.folderRuns[runIndex].kills[killIndex];
            if (kill.teamKill || kill.suicide) {
                mutableDraw.clrText = kDanger;
            } else if (kill.matchPhase == etlfrag::MatchPhase::Warmup) {
                mutableDraw.clrText = kWarmup;
            } else if (kill.headshot) {
                mutableDraw.clrText = kSuccess;
            }
        }
    } else if (list == gApp.runList && data >= 0 && data < static_cast<int>(gApp.runs.size())) {
        const etlfrag::FragRun& run = gApp.runs[static_cast<std::size_t>(data)];
        if (!run.killIndices.empty() && run.killIndices.front() < gApp.demo.kills.size() &&
            gApp.demo.kills[run.killIndices.front()].matchPhase == etlfrag::MatchPhase::Warmup) {
            mutableDraw.clrText = kWarmup;
        } else if (run.killIndices.size() >= 3) {
            mutableDraw.clrText = kSuccess;
        }
    } else if (list == gApp.folderRunList && data >= 0 &&
               data < static_cast<int>(gApp.folderRows.size())) {
        const std::size_t runIndex = gApp.folderRows[static_cast<std::size_t>(data)];
        if (runIndex < gApp.folderRuns.size()) {
            const FolderRunResult& result = gApp.folderRuns[runIndex];
            if (!result.kills.empty() &&
                result.kills.front().matchPhase == etlfrag::MatchPhase::Warmup) {
                mutableDraw.clrText = kWarmup;
            } else if (result.kills.size() >= 3) {
                mutableDraw.clrText = kSuccess;
            }
        }
    } else if (list == gApp.libraryList && data >= 0 &&
               data < static_cast<int>(gApp.libraryRows.size()) &&
               gApp.libraryRows[static_cast<std::size_t>(data)].duplicateCount > 1) {
        mutableDraw.clrText = kAccent;
    }
    return CDRF_DODEFAULT;
}

BOOL CALLBACK applyBaseFont(HWND control, LPARAM fontValue) {
    SendMessageW(control, WM_SETFONT, static_cast<WPARAM>(fontValue), TRUE);
    return TRUE;
}

void recreateDpiResources() {
    const HFONT oldFont = gApp.font;
    const HFONT oldSmallFont = gApp.smallFont;
    const HFONT oldLabelFont = gApp.labelFont;
    const HFONT oldTitleFont = gApp.titleFont;
    const HIMAGELIST oldImages = gApp.rowHeightImages;

    gApp.font = CreateFontW(
        -scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gApp.smallFont = CreateFontW(
        -scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gApp.labelFont = CreateFontW(
        -scale(14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
    gApp.titleFont = CreateFontW(
        -scale(25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
    gApp.rowHeightImages = ImageList_Create(1, scale(27), ILC_COLOR32, 1, 1);

    if (gApp.window != nullptr) {
        EnumChildWindows(
            gApp.window,
            applyBaseFont,
            reinterpret_cast<LPARAM>(gApp.font));
        for (HWND control : {
                 gApp.playerLabel, gApp.minimumLabel, gApp.minimumHeadshotsLabel,
                 gApp.gapLabel, gApp.weaponLabel,
                 gApp.postDeathWindowLabel, gApp.eventPlayerLabel,
                 gApp.folderMinimumLabel, gApp.folderMinimumHeadshotsLabel,
                 gApp.folderGapLabel, gApp.folderWeaponLabel,
                 gApp.folderPostDeathWindowLabel, gApp.folderQueryLabel,
                 gApp.folderFieldLabel, gApp.libraryQueryLabel, gApp.libraryFieldLabel,
                 gApp.status}) {
            if (control != nullptr) setFont(control, gApp.smallFont);
        }
        for (HWND control : {
                 gApp.openDemo, gApp.tabMultiKills, gApp.tabAllEvents, gApp.tabFolderScan,
                 gApp.tabHighlights, gApp.tabLibrary, gApp.exportCurrent, gApp.search,
                 gApp.chooseEtl, gApp.playRun, gApp.addRunHighlight, gApp.playEvent,
                 gApp.viewProtocolLog,
                 gApp.chooseFolder, gApp.folderApplyFilters, gApp.folderScan,
                 gApp.playFolderRun, gApp.addFolderHighlight, gApp.playHighlight,
                 gApp.removeHighlight, gApp.clearHighlights, gApp.librarySearch,
                 gApp.libraryOpen}) {
            if (control != nullptr) setFont(control, gApp.labelFont);
        }
        for (HWND list : {
                 gApp.runList, gApp.runKillList, gApp.allEventList, gApp.folderRunList,
                 gApp.folderKillList, gApp.highlightList, gApp.libraryList}) {
            if (list == nullptr) continue;
            ListView_SetImageList(list, gApp.rowHeightImages, LVSIL_SMALL);
            if (HWND header = ListView_GetHeader(list); header != nullptr) {
                setFont(header, gApp.smallFont);
            }
        }
    }
    for (HFONT font : {oldFont, oldSmallFont, oldLabelFont, oldTitleFont}) {
        if (font != nullptr) DeleteObject(font);
    }
    if (oldImages != nullptr) ImageList_Destroy(oldImages);
}

void rescaleFixedListColumns() {
    auto widths = [](HWND list, std::initializer_list<int> values) {
        if (list == nullptr) return;
        int column = 0;
        for (const int value : values) {
            ListView_SetColumnWidth(list, column++, scale(value));
        }
    };
    widths(gApp.runList, {95, 90, 165, 65, 90, 85, 620});
    widths(gApp.runKillList, {105, 100, 190, 190, 220});
    widths(gApp.allEventList, {55, 100, 100, 180, 180, 185, 230});
    widths(gApp.folderRunList, {230, 110, 140, 90, 85, 55, 90, 75, 500});
    widths(gApp.folderKillList, {105, 100, 190, 190, 220});
    widths(gApp.highlightList, {240, 110, 145, 95, 90, 60, 90, 80, 500});
    widths(gApp.libraryList, {260, 105, 125, 165, 70, 70, 95, 90, 450});
}

void layoutAllControls(int width, int height) {
    if (gApp.status == nullptr) {
        return;
    }
    const int margin = scale(20);
    const int gap = scale(12);
    const int fileY = scale(82);
    const int fileHeight = scale(38);
    const int openWidth = scale(150);
    const int statusHeight = scale(28);

    MoveWindow(
        gApp.launchAsAdministrator,
        width - margin - scale(245),
        scale(6),
        scale(235),
        scale(27),
        TRUE);
    MoveWindow(
        gApp.launchWithoutSeekingControl,
        width - margin - scale(245),
        scale(35),
        scale(235),
        scale(27),
        TRUE);

    MoveWindow(gApp.demoPath, margin, fileY, width - margin * 2 - openWidth - gap, fileHeight, TRUE);
    MoveWindow(gApp.openDemo, width - margin - openWidth, fileY, openWidth, fileHeight, TRUE);
    MoveWindow(gApp.folderPath, margin, fileY, width - margin * 2 - openWidth - gap, fileHeight, TRUE);
    MoveWindow(gApp.chooseFolder, width - margin - openWidth, fileY, openWidth, fileHeight, TRUE);
    MoveWindow(gApp.highlightInfo, margin, fileY, width - margin * 2, fileHeight, TRUE);

    const int tabY = scale(207);
    const int tabHeight = scale(35);
    const int exportWidth = scale(180);
    const int tabsRight = width - margin - exportWidth - gap;
    const int tabsWidth = std::max(scale(500), tabsRight - margin);
    const int tabWidth = tabsWidth / 5;
    MoveWindow(gApp.tabMultiKills, margin, tabY, tabWidth - scale(5), tabHeight, TRUE);
    MoveWindow(gApp.tabAllEvents, margin + tabWidth, tabY, tabWidth - scale(5), tabHeight, TRUE);
    MoveWindow(gApp.tabFolderScan, margin + tabWidth * 2, tabY, tabWidth - scale(5), tabHeight, TRUE);
    MoveWindow(gApp.tabHighlights, margin + tabWidth * 3, tabY, tabWidth - scale(5), tabHeight, TRUE);
    MoveWindow(gApp.tabLibrary, margin + tabWidth * 4, tabY, tabWidth - scale(5), tabHeight, TRUE);
    MoveWindow(
        gApp.exportCurrent,
        width - margin - exportWidth,
        tabY + scale(1),
        exportWidth,
        tabHeight - scale(2),
        TRUE);

    const int contentTop = scale(252);
    const int inputY = contentTop + scale(38);
    const int labelY = contentTop + scale(13);
    const int inputHeight = scale(32);
    int x = margin + scale(14);
    MoveWindow(gApp.playerLabel, x, labelY, scale(150), scale(18), TRUE);
    MoveWindow(gApp.player, x, inputY, scale(220), scale(300), TRUE);
    x += scale(232);
    MoveWindow(gApp.minimumLabel, x, labelY, scale(90), scale(18), TRUE);
    MoveWindow(gApp.minimumKills, x, inputY, scale(85), inputHeight, TRUE);
    x += scale(97);
    MoveWindow(gApp.gapLabel, x, labelY, scale(120), scale(18), TRUE);
    MoveWindow(gApp.maximumGap, x, inputY, scale(100), inputHeight, TRUE);
    x += scale(112);
    MoveWindow(gApp.weaponLabel, x, labelY, scale(120), scale(18), TRUE);
    MoveWindow(gApp.weapon, x, inputY, scale(185), scale(300), TRUE);
    x += scale(197);
    MoveWindow(gApp.teamKills, x, inputY + scale(3), scale(140), scale(28), TRUE);
    x += scale(150);
    MoveWindow(gApp.warmupKills, x, inputY + scale(3), scale(165), scale(28), TRUE);
    MoveWindow(gApp.search, width - margin - scale(164), inputY, scale(150), inputHeight, TRUE);

    const int actionY = contentTop + scale(82);
    MoveWindow(gApp.chooseEtl, margin + scale(14), actionY, scale(150), scale(34), TRUE);
    MoveWindow(gApp.playRun, margin + scale(176), actionY, scale(190), scale(34), TRUE);
    MoveWindow(gApp.addRunHighlight, margin + scale(378), actionY, scale(170), scale(34), TRUE);
    const int postDeathX = margin + scale(566);
    MoveWindow(
        gApp.postDeathExplosives,
        postDeathX,
        actionY + scale(3),
        scale(190),
        scale(28),
        TRUE);
    MoveWindow(
        gApp.postDeathWindowLabel,
        postDeathX + scale(200),
        actionY,
        scale(118),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.postDeathWindow,
        postDeathX + scale(326),
        actionY + scale(1),
        scale(92),
        scale(180),
        TRUE);
    MoveWindow(
        gApp.minimumHeadshotsLabel,
        postDeathX + scale(430),
        actionY + scale(8),
        scale(118),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.minimumHeadshots,
        postDeathX + scale(556),
        actionY + scale(1),
        scale(58),
        scale(32),
        TRUE);

    const int listsY = contentTop + scale(198);
    const int bottom = height - statusHeight - scale(14);
    const int available = std::max(scale(80), bottom - listsY);
    const int runHeight = std::clamp(
        available * 58 / 100,
        scale(55),
        std::max(scale(55), available - scale(45)));
    MoveWindow(gApp.runList, margin + scale(10), listsY, width - margin * 2 - scale(20), runHeight, TRUE);
    MoveWindow(
        gApp.runKillList,
        margin + scale(10),
        listsY + runHeight + scale(10),
        width - margin * 2 - scale(20),
        available - runHeight - scale(10),
        TRUE);
    ListView_SetColumnWidth(
        gApp.runList,
        6,
        std::max(scale(320), width - scale(675)));
    ListView_SetColumnWidth(
        gApp.runKillList,
        4,
        std::max(scale(180), width - scale(670)));

    const int eventActionY = contentTop + scale(10);
    MoveWindow(
        gApp.eventPlayerLabel,
        margin + scale(14),
        eventActionY,
        scale(72),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.eventPlayer,
        margin + scale(92),
        eventActionY + scale(1),
        scale(285),
        scale(300),
        TRUE);
    MoveWindow(
        gApp.playEvent,
        width - margin - scale(245),
        eventActionY,
        scale(230),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.viewProtocolLog,
        margin + scale(395),
        eventActionY,
        scale(220),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.allEventList,
        margin + scale(10),
        contentTop + scale(126),
        width - margin * 2 - scale(20),
        bottom - (contentTop + scale(126)),
        TRUE);
    ListView_SetColumnWidth(
        gApp.allEventList,
        6,
        std::max(scale(170), width - scale(885)));

    int folderX = margin + scale(14);
    MoveWindow(
        gApp.folderMinimumLabel,
        folderX,
        labelY,
        scale(90),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.folderMinimumKills,
        folderX,
        inputY,
        scale(90),
        inputHeight,
        TRUE);
    folderX += scale(102);
    MoveWindow(
        gApp.folderGapLabel,
        folderX,
        labelY,
        scale(110),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.folderMaximumGap,
        folderX,
        inputY,
        scale(105),
        inputHeight,
        TRUE);
    folderX += scale(117);
    MoveWindow(
        gApp.folderWeaponLabel,
        folderX,
        labelY,
        scale(120),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.folderWeapon,
        folderX,
        inputY,
        scale(185),
        scale(300),
        TRUE);
    folderX += scale(197);
    MoveWindow(
        gApp.folderTeamKills,
        folderX,
        inputY + scale(3),
        scale(135),
        scale(28),
        TRUE);
    MoveWindow(
        gApp.folderWarmupKills,
        folderX + scale(145),
        inputY + scale(3),
        scale(165),
        scale(28),
        TRUE);
    MoveWindow(
        gApp.folderWatch,
        folderX + scale(320),
        inputY + scale(3),
        scale(205),
        scale(28),
        TRUE);
    MoveWindow(
        gApp.folderScan,
        width - margin - scale(164),
        inputY,
        scale(150),
        inputHeight,
        TRUE);
    MoveWindow(
        gApp.playFolderRun,
        margin + scale(14),
        actionY,
        scale(190),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.addFolderHighlight,
        margin + scale(216),
        actionY,
        scale(175),
        scale(34),
        TRUE);
    const int folderPostDeathX = margin + scale(409);
    MoveWindow(
        gApp.folderPostDeathExplosives,
        folderPostDeathX,
        actionY + scale(3),
        scale(195),
        scale(28),
        TRUE);
    MoveWindow(
        gApp.folderPostDeathWindowLabel,
        folderPostDeathX + scale(205),
        actionY,
        scale(118),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.folderPostDeathWindow,
        folderPostDeathX + scale(331),
        actionY + scale(1),
        scale(92),
        scale(180),
        TRUE);
    MoveWindow(
        gApp.folderMinimumHeadshotsLabel,
        folderPostDeathX + scale(435),
        actionY + scale(8),
        scale(118),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.folderMinimumHeadshots,
        folderPostDeathX + scale(561),
        actionY + scale(1),
        scale(58),
        scale(32),
        TRUE);
    const int folderSearchX = margin + scale(14);
    const int folderSearchLabelY = contentTop + scale(126);
    const int folderSearchInputY = contentTop + scale(148);
    MoveWindow(
        gApp.folderQueryLabel,
        folderSearchX,
        folderSearchLabelY,
        scale(160),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.folderQuery,
        folderSearchX,
        folderSearchInputY,
        scale(360),
        inputHeight,
        TRUE);
    MoveWindow(
        gApp.folderFieldLabel,
        folderSearchX + scale(374),
        folderSearchLabelY,
        scale(130),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.folderField,
        folderSearchX + scale(374),
        folderSearchInputY,
        scale(175),
        scale(240),
        TRUE);
    MoveWindow(
        gApp.folderApplyFilters,
        width - margin - scale(210),
        folderSearchInputY,
        scale(196),
        scale(34),
        TRUE);
    const int folderListsY = contentTop + scale(262);
    const int folderAvailable = std::max(scale(80), bottom - folderListsY);
    const int folderRunHeight = std::clamp(
        folderAvailable * 58 / 100,
        scale(55),
        std::max(scale(55), folderAvailable - scale(45)));
    MoveWindow(
        gApp.folderRunList,
        margin + scale(10),
        folderListsY,
        width - margin * 2 - scale(20),
        folderRunHeight,
        TRUE);
    MoveWindow(
        gApp.folderKillList,
        margin + scale(10),
        folderListsY + folderRunHeight + scale(10),
        width - margin * 2 - scale(20),
        folderAvailable - folderRunHeight - scale(10),
        TRUE);
    ListView_SetColumnWidth(
        gApp.folderRunList,
        8,
        std::max(scale(230), width - scale(960)));
    ListView_SetColumnWidth(
        gApp.folderKillList,
        4,
        std::max(scale(180), width - scale(670)));

    const int highlightActionY = contentTop + scale(14);
    MoveWindow(
        gApp.playHighlight,
        margin + scale(14),
        highlightActionY,
        scale(210),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.removeHighlight,
        margin + scale(236),
        highlightActionY,
        scale(175),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.clearHighlights,
        margin + scale(423),
        highlightActionY,
        scale(155),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.highlightList,
        margin + scale(10),
        contentTop + scale(126),
        width - margin * 2 - scale(20),
        bottom - (contentTop + scale(126)),
        TRUE);
    ListView_SetColumnWidth(
        gApp.highlightList,
        8,
        std::max(scale(240), width - scale(940)));

    const int libraryQueryX = margin + scale(14);
    MoveWindow(gApp.libraryQueryLabel, libraryQueryX, labelY, scale(160), scale(18), TRUE);
    MoveWindow(gApp.libraryQuery, libraryQueryX, inputY, scale(360), inputHeight, TRUE);
    MoveWindow(
        gApp.libraryFieldLabel,
        libraryQueryX + scale(374),
        labelY,
        scale(130),
        scale(18),
        TRUE);
    MoveWindow(
        gApp.libraryField,
        libraryQueryX + scale(374),
        inputY,
        scale(175),
        scale(240),
        TRUE);
    MoveWindow(
        gApp.libraryScope,
        libraryQueryX + scale(565),
        inputY + scale(3),
        scale(190),
        scale(28),
        TRUE);
    MoveWindow(
        gApp.libraryDuplicates,
        libraryQueryX + scale(765),
        inputY + scale(3),
        scale(160),
        scale(28),
        TRUE);
    MoveWindow(
        gApp.librarySearch,
        width - margin - scale(164),
        inputY,
        scale(150),
        inputHeight,
        TRUE);
    MoveWindow(
        gApp.libraryOpen,
        libraryQueryX,
        actionY,
        scale(210),
        scale(34),
        TRUE);
    MoveWindow(
        gApp.libraryList,
        margin + scale(10),
        contentTop + scale(126),
        width - margin * 2 - scale(20),
        bottom - (contentTop + scale(126)),
        TRUE);
    ListView_SetColumnWidth(
        gApp.libraryList,
        8,
        std::max(scale(300), width - scale(1000)));

    const int timelineY =
        gApp.activeTab == 0
            ? contentTop + scale(128)
            : (gApp.activeTab == 2
                   ? contentTop + scale(192)
                   : contentTop + scale(58));
    MoveWindow(
        gApp.timeline,
        margin + scale(10),
        timelineY,
        width - margin * 2 - scale(20),
        scale(60),
        TRUE);

    MoveWindow(
        gApp.status,
        margin,
        height - statusHeight,
        std::max(1, width - margin * 2),
        statusHeight,
        TRUE);

    showTab(gApp.activeTab);
    InvalidateRect(gApp.window, nullptr, FALSE);
}

void createInterface(HWND window) {
    gApp.window = window;
    gApp.dpi = dpiForWindow(window);
    recreateDpiResources();
    gApp.backgroundBrush = CreateSolidBrush(kBackground);
    gApp.panelBrush = CreateSolidBrush(kPanel);
    gApp.controlBrush = CreateSolidBrush(kControl);

    gApp.demoPath = createControl(
        0,
        WC_EDITW,
        L"Drop a .dm_84 file here or select Open demo",
        ES_AUTOHSCROLL | ES_READONLY | WS_BORDER,
        IdDemoPath);
    gApp.openDemo = createControl(
        0, WC_BUTTONW, L"Open demo", BS_OWNERDRAW | WS_TABSTOP, IdOpenDemo, gApp.labelFont);
    gApp.folderPath = createControl(
        0,
        WC_EDITW,
        L"Choose a folder containing .dm_84 demos",
        ES_AUTOHSCROLL | ES_READONLY | WS_BORDER,
        IdFolderPath);
    gApp.chooseFolder = createControl(
        0,
        WC_BUTTONW,
        L"Choose folder",
        BS_OWNERDRAW | WS_TABSTOP,
        IdChooseFolder,
        gApp.labelFont);
    gApp.highlightInfo = createControl(
        0,
        WC_EDITW,
        L"Saved clip shortlist — double-click a highlight to play it. The basket is restored automatically.",
        ES_AUTOHSCROLL | ES_READONLY | WS_BORDER,
        IdHighlightInfo);
    gApp.tabMultiKills = createControl(
        0, WC_BUTTONW, L"Multi-kill finder", BS_OWNERDRAW | WS_TABSTOP, IdTabMultiKills, gApp.labelFont);
    gApp.tabAllEvents = createControl(
        0, WC_BUTTONW, L"All kills / events", BS_OWNERDRAW | WS_TABSTOP, IdTabAllEvents, gApp.labelFont);
    gApp.tabFolderScan = createControl(
        0, WC_BUTTONW, L"Folder scan", BS_OWNERDRAW | WS_TABSTOP, IdTabFolderScan, gApp.labelFont);
    gApp.tabHighlights = createControl(
        0, WC_BUTTONW, L"Highlights", BS_OWNERDRAW | WS_TABSTOP, IdTabHighlights, gApp.labelFont);
    gApp.tabLibrary = createControl(
        0, WC_BUTTONW, L"Demo library", BS_OWNERDRAW | WS_TABSTOP, IdTabLibrary, gApp.labelFont);
    gApp.exportCurrent = createControl(
        0,
        WC_BUTTONW,
        L"Export current view",
        BS_OWNERDRAW | WS_TABSTOP,
        IdExportCurrent,
        gApp.labelFont);
    gApp.launchAsAdministrator = createControl(
        0,
        WC_BUTTONW,
        L"Launch ETL as administrator",
        BS_OWNERDRAW | WS_TABSTOP,
        IdLaunchAsAdministrator);
    gApp.launchWithoutSeekingControl = createControl(
        0,
        WC_BUTTONW,
        L"Launch without seeking",
        BS_OWNERDRAW | WS_TABSTOP,
        IdLaunchWithoutSeeking);

    gApp.playerLabel = createControl(0, WC_STATICW, L"Player", SS_LEFT, IdPlayerLabel, gApp.smallFont);
    gApp.player = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IdPlayer);
    gApp.minimumLabel = createControl(0, WC_STATICW, L"Minimum kills", SS_LEFT, IdMinimumLabel, gApp.smallFont);
    gApp.minimumKills = createControl(
        0, WC_EDITW, L"2", ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL | WS_BORDER, IdMinimumKills);
    gApp.minimumHeadshotsLabel = createControl(
        0, WC_STATICW, L"Minimum headshots", SS_LEFT, IdMinimumHeadshotsLabel, gApp.smallFont);
    gApp.minimumHeadshots = createControl(
        0,
        WC_EDITW,
        L"0",
        ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL | WS_BORDER,
        IdMinimumHeadshots);
    gApp.gapLabel = createControl(0, WC_STATICW, L"Max. gap (seconds)", SS_LEFT, IdGapLabel, gApp.smallFont);
    gApp.maximumGap = createControl(
        0, WC_EDITW, L"8.0", ES_CENTER | ES_AUTOHSCROLL | WS_BORDER, IdMaximumGap);
    gApp.weaponLabel = createControl(0, WC_STATICW, L"Weapon", SS_LEFT, IdWeaponLabel, gApp.smallFont);
    gApp.weapon = createControl(0, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IdWeapon);
    gApp.teamKills = createControl(
        0, WC_BUTTONW, L"Include teamkills", BS_OWNERDRAW | WS_TABSTOP, IdTeamKills);
    gApp.warmupKills = createControl(
        0,
        WC_BUTTONW,
        L"Include warmup kills",
        BS_OWNERDRAW | WS_TABSTOP,
        IdWarmupKills);
    gApp.postDeathExplosives = createControl(
        0,
        WC_BUTTONW,
        L"Post-death explosives",
        BS_OWNERDRAW | WS_TABSTOP,
        IdPostDeathExplosives);
    gApp.postDeathWindowLabel = createControl(
        0,
        WC_STATICW,
        L"Window (seconds)",
        SS_LEFT | SS_CENTERIMAGE,
        IdPostDeathWindowLabel,
        gApp.smallFont);
    gApp.postDeathWindow = createControl(
        0,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
        IdPostDeathWindow);
    gApp.search = createControl(
        0, WC_BUTTONW, L"Find multi-kills", BS_OWNERDRAW | WS_TABSTOP, IdSearch, gApp.labelFont);
    gApp.chooseEtl = createControl(
        0, WC_BUTTONW, L"Locate etl.exe", BS_OWNERDRAW | WS_TABSTOP, IdChooseEtl, gApp.labelFont);
    gApp.playRun = createControl(
        0, WC_BUTTONW, L"Play selected  (−5s)", BS_OWNERDRAW | WS_TABSTOP, IdPlayRun, gApp.labelFont);
    EnableWindow(gApp.playRun, FALSE);
    gApp.addRunHighlight = createControl(
        0,
        WC_BUTTONW,
        L"Add to highlights",
        BS_OWNERDRAW | WS_TABSTOP,
        IdAddRunHighlight,
        gApp.labelFont);
    EnableWindow(gApp.addRunHighlight, FALSE);

    const DWORD listStyle = LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS;
    gApp.runList = createControl(0, WC_LISTVIEWW, L"", listStyle, IdRunList);
    configureListView(gApp.runList);
    addListColumn(gApp.runList, 0, 95, L"Demo time");
    addListColumn(gApp.runList, 1, 90, L"Match clock");
    addListColumn(gApp.runList, 2, 165, L"Player");
    addListColumn(gApp.runList, 3, 65, L"Kills");
    addListColumn(gApp.runList, 4, 90, L"Headshots");
    addListColumn(gApp.runList, 5, 85, L"Duration");
    addListColumn(gApp.runList, 6, 620, L"Victims and weapons");

    gApp.runKillList = createControl(0, WC_LISTVIEWW, L"", listStyle, IdRunKillList);
    configureListView(gApp.runKillList);
    addListColumn(gApp.runKillList, 0, 105, L"Demo time");
    addListColumn(gApp.runKillList, 1, 100, L"Match clock");
    addListColumn(gApp.runKillList, 2, 190, L"Victim");
    addListColumn(gApp.runKillList, 3, 190, L"Weapon");
    addListColumn(gApp.runKillList, 4, 220, L"Event type");

    gApp.eventPlayerLabel = createControl(
        0,
        WC_STATICW,
        L"Player log",
        SS_LEFT | SS_CENTERIMAGE,
        IdEventPlayerLabel,
        gApp.smallFont);
    gApp.eventPlayer = createControl(
        0,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        IdEventPlayer);
    gApp.playEvent = createControl(
        0, WC_BUTTONW, L"Play selected event  (−5s)", BS_OWNERDRAW | WS_TABSTOP, IdPlayEvent, gApp.labelFont);
    EnableWindow(gApp.playEvent, FALSE);
    gApp.viewProtocolLog = createControl(
        0,
        WC_BUTTONW,
        L"View full demo protocol",
        BS_OWNERDRAW | WS_TABSTOP,
        IdViewProtocolLog,
        gApp.labelFont);
    EnableWindow(gApp.viewProtocolLog, FALSE);
    gApp.allEventList = createControl(0, WC_LISTVIEWW, L"", listStyle, IdAllEventList);
    configureListView(gApp.allEventList);
    addListColumn(gApp.allEventList, 0, 55, L"#");
    addListColumn(gApp.allEventList, 1, 100, L"Demo time");
    addListColumn(gApp.allEventList, 2, 100, L"Match clock");
    addListColumn(gApp.allEventList, 3, 180, L"Attacker");
    addListColumn(gApp.allEventList, 4, 180, L"Victim");
    addListColumn(gApp.allEventList, 5, 185, L"Weapon");
    addListColumn(gApp.allEventList, 6, 230, L"Event type");

    gApp.folderMinimumLabel = createControl(
        0,
        WC_STATICW,
        L"Minimum kills",
        SS_LEFT,
        IdFolderMinimumLabel,
        gApp.smallFont);
    gApp.folderMinimumKills = createControl(
        0,
        WC_EDITW,
        L"2",
        ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL | WS_BORDER,
        IdFolderMinimumKills);
    gApp.folderMinimumHeadshotsLabel = createControl(
        0,
        WC_STATICW,
        L"Minimum headshots",
        SS_LEFT,
        IdFolderMinimumHeadshotsLabel,
        gApp.smallFont);
    gApp.folderMinimumHeadshots = createControl(
        0,
        WC_EDITW,
        L"0",
        ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL | WS_BORDER,
        IdFolderMinimumHeadshots);
    gApp.folderGapLabel = createControl(
        0,
        WC_STATICW,
        L"Max. gap (seconds)",
        SS_LEFT,
        IdFolderGapLabel,
        gApp.smallFont);
    gApp.folderMaximumGap = createControl(
        0,
        WC_EDITW,
        L"8.0",
        ES_CENTER | ES_AUTOHSCROLL | WS_BORDER,
        IdFolderMaximumGap);
    gApp.folderWeaponLabel = createControl(
        0,
        WC_STATICW,
        L"Weapon",
        SS_LEFT,
        IdFolderWeaponLabel,
        gApp.smallFont);
    gApp.folderWeapon = createControl(
        0,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_VSCROLL,
        IdFolderWeapon);
    gApp.folderTeamKills = createControl(
        0,
        WC_BUTTONW,
        L"Include teamkills",
        BS_OWNERDRAW | WS_TABSTOP,
        IdFolderTeamKills);
    gApp.folderWarmupKills = createControl(
        0,
        WC_BUTTONW,
        L"Include warmup kills",
        BS_OWNERDRAW | WS_TABSTOP,
        IdFolderWarmupKills);
    gApp.folderWatch = createControl(
        0,
        WC_BUTTONW,
        L"Auto-index new demos",
        BS_OWNERDRAW | WS_TABSTOP,
        IdFolderWatch);
    gApp.folderPostDeathExplosives = createControl(
        0,
        WC_BUTTONW,
        L"Post-death explosives",
        BS_OWNERDRAW | WS_TABSTOP,
        IdFolderPostDeathExplosives);
    gApp.folderPostDeathWindowLabel = createControl(
        0,
        WC_STATICW,
        L"Window (seconds)",
        SS_LEFT | SS_CENTERIMAGE,
        IdFolderPostDeathWindowLabel,
        gApp.smallFont);
    gApp.folderPostDeathWindow = createControl(
        0,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP,
        IdFolderPostDeathWindow);
    gApp.folderQueryLabel = createControl(
        0,
        WC_STATICW,
        L"Search indexed demos",
        SS_LEFT,
        IdFolderQueryLabel,
        gApp.smallFont);
    gApp.folderQuery = createControl(
        0,
        WC_EDITW,
        L"",
        ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
        IdFolderQuery);
    SendMessageW(gApp.folderQuery, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(
        L"Nickname, map, YYYY-MM-DD or demo filename"));
    gApp.folderFieldLabel = createControl(
        0,
        WC_STATICW,
        L"Search field",
        SS_LEFT,
        IdFolderFieldLabel,
        gApp.smallFont);
    gApp.folderField = createControl(
        0,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        IdFolderField);
    for (const wchar_t* field : {L"Everything", L"Nickname", L"Map", L"Date", L"Demo filename"}) {
        SendMessageW(gApp.folderField, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(field));
    }
    SendMessageW(gApp.folderField, CB_SETCURSEL, 0, 0);
    gApp.folderApplyFilters = createControl(
        0,
        WC_BUTTONW,
        L"Apply cached filters",
        BS_OWNERDRAW | WS_TABSTOP,
        IdFolderApplyFilters,
        gApp.labelFont);
    EnableWindow(gApp.folderApplyFilters, FALSE);
    gApp.folderScan = createControl(
        0,
        WC_BUTTONW,
        L"Update index",
        BS_OWNERDRAW | WS_TABSTOP,
        IdFolderScan,
        gApp.labelFont);
    gApp.playFolderRun = createControl(
        0,
        WC_BUTTONW,
        L"Play selected  (−5s)",
        BS_OWNERDRAW | WS_TABSTOP,
        IdPlayFolderRun,
        gApp.labelFont);
    EnableWindow(gApp.playFolderRun, FALSE);
    gApp.addFolderHighlight = createControl(
        0,
        WC_BUTTONW,
        L"Add to highlights",
        BS_OWNERDRAW | WS_TABSTOP,
        IdAddFolderHighlight,
        gApp.labelFont);
    EnableWindow(gApp.addFolderHighlight, FALSE);

    gApp.folderRunList = createControl(0, WC_LISTVIEWW, L"", listStyle, IdFolderRunList);
    configureListView(gApp.folderRunList);
    addListColumn(gApp.folderRunList, 0, 230, L"Demo file");
    addListColumn(gApp.folderRunList, 1, 110, L"Map");
    addListColumn(gApp.folderRunList, 2, 140, L"Recorded POV");
    addListColumn(gApp.folderRunList, 3, 90, L"Demo time");
    addListColumn(gApp.folderRunList, 4, 85, L"Match clock");
    addListColumn(gApp.folderRunList, 5, 55, L"Kills");
    addListColumn(gApp.folderRunList, 6, 90, L"Headshots");
    addListColumn(gApp.folderRunList, 7, 75, L"Duration");
    addListColumn(gApp.folderRunList, 8, 500, L"Victims and weapons");

    gApp.folderKillList = createControl(0, WC_LISTVIEWW, L"", listStyle, IdFolderKillList);
    configureListView(gApp.folderKillList);
    addListColumn(gApp.folderKillList, 0, 105, L"Demo time");
    addListColumn(gApp.folderKillList, 1, 100, L"Match clock");
    addListColumn(gApp.folderKillList, 2, 190, L"Victim");
    addListColumn(gApp.folderKillList, 3, 190, L"Weapon");
    addListColumn(gApp.folderKillList, 4, 220, L"Event type");

    gApp.timeline = createControl(
        0,
        kTimelineClass,
        L"",
        WS_TABSTOP,
        IdTimeline);

    gApp.playHighlight = createControl(
        0,
        WC_BUTTONW,
        L"Play selected  (−5s)",
        BS_OWNERDRAW | WS_TABSTOP,
        IdPlayHighlight,
        gApp.labelFont);
    gApp.removeHighlight = createControl(
        0,
        WC_BUTTONW,
        L"Remove selected",
        BS_OWNERDRAW | WS_TABSTOP,
        IdRemoveHighlight,
        gApp.labelFont);
    gApp.clearHighlights = createControl(
        0,
        WC_BUTTONW,
        L"Clear basket",
        BS_OWNERDRAW | WS_TABSTOP,
        IdClearHighlights,
        gApp.labelFont);
    gApp.highlightList = createControl(0, WC_LISTVIEWW, L"", listStyle, IdHighlightList);
    configureListView(gApp.highlightList);
    addListColumn(gApp.highlightList, 0, 240, L"Demo file");
    addListColumn(gApp.highlightList, 1, 110, L"Map");
    addListColumn(gApp.highlightList, 2, 145, L"Recorded POV");
    addListColumn(gApp.highlightList, 3, 95, L"Demo time");
    addListColumn(gApp.highlightList, 4, 90, L"Match clock");
    addListColumn(gApp.highlightList, 5, 60, L"Kills");
    addListColumn(gApp.highlightList, 6, 90, L"Headshots");
    addListColumn(gApp.highlightList, 7, 80, L"Duration");
    addListColumn(gApp.highlightList, 8, 500, L"Victims and weapons");
    EnableWindow(gApp.playHighlight, FALSE);
    EnableWindow(gApp.removeHighlight, FALSE);
    EnableWindow(gApp.clearHighlights, FALSE);

    gApp.libraryQueryLabel = createControl(
        0, WC_STATICW, L"Search indexed demos", SS_LEFT, IdLibraryQueryLabel, gApp.smallFont);
    gApp.libraryQuery = createControl(
        0,
        WC_EDITW,
        L"",
        ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
        IdLibraryQuery);
    SendMessageW(gApp.libraryQuery, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(
        L"Nickname, map, YYYY-MM-DD or demo filename"));
    gApp.libraryFieldLabel = createControl(
        0, WC_STATICW, L"Search field", SS_LEFT, IdLibraryFieldLabel, gApp.smallFont);
    gApp.libraryField = createControl(
        0,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        IdLibraryField);
    for (const wchar_t* field : {L"Everything", L"Nickname", L"Map", L"Date", L"Demo filename"}) {
        SendMessageW(gApp.libraryField, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(field));
    }
    SendMessageW(gApp.libraryField, CB_SETCURSEL, 0, 0);
    gApp.libraryScope = createControl(
        0,
        WC_BUTTONW,
        L"Selected folder only",
        BS_OWNERDRAW | WS_TABSTOP,
        IdLibraryScope);
    gApp.libraryDuplicates = createControl(
        0,
        WC_BUTTONW,
        L"Duplicates only",
        BS_OWNERDRAW | WS_TABSTOP,
        IdLibraryDuplicates);
    gApp.librarySearch = createControl(
        0,
        WC_BUTTONW,
        L"Search index",
        BS_OWNERDRAW | WS_TABSTOP,
        IdLibrarySearch,
        gApp.labelFont);
    gApp.libraryOpen = createControl(
        0,
        WC_BUTTONW,
        L"Open selected demo",
        BS_OWNERDRAW | WS_TABSTOP,
        IdLibraryOpen,
        gApp.labelFont);
    EnableWindow(gApp.libraryOpen, FALSE);
    gApp.libraryList = createControl(0, WC_LISTVIEWW, L"", listStyle, IdLibraryList);
    configureListView(gApp.libraryList);
    addListColumn(gApp.libraryList, 0, 260, L"Demo file");
    addListColumn(gApp.libraryList, 1, 105, L"Date");
    addListColumn(gApp.libraryList, 2, 125, L"Map");
    addListColumn(gApp.libraryList, 3, 165, L"Recorded POV");
    addListColumn(gApp.libraryList, 4, 70, L"Players");
    addListColumn(gApp.libraryList, 5, 70, L"Events");
    addListColumn(gApp.libraryList, 6, 95, L"Duration");
    addListColumn(gApp.libraryList, 7, 90, L"Duplicates");
    addListColumn(gApp.libraryList, 8, 450, L"Full path");

    gApp.status = createControl(
        0,
        WC_STATICW,
        L"Ready — open a demo or choose a folder containing .dm_84 files",
        SS_LEFT | SS_CENTERIMAGE,
        IdStatus,
        gApp.smallFont);

    for (HWND control : {
             gApp.demoPath,
             gApp.folderPath,
             gApp.highlightInfo,
             gApp.player,
             gApp.eventPlayer,
             gApp.minimumKills,
             gApp.minimumHeadshots,
             gApp.maximumGap,
             gApp.weapon,
             gApp.postDeathWindow,
             gApp.folderMinimumKills,
             gApp.folderMinimumHeadshots,
             gApp.folderMaximumGap,
             gApp.folderWeapon,
             gApp.folderPostDeathWindow,
             gApp.folderQuery,
             gApp.folderField,
             gApp.libraryQuery,
             gApp.libraryField}) {
        SetWindowTheme(control, L"DarkMode_CFD", nullptr);
    }
    DragAcceptFiles(window, TRUE);
    initializePersistentStorage();
    gApp.launchEtlAsAdministrator =
        GetPrivateProfileIntW(
            L"Playback", L"LaunchAsAdministrator", 0, gApp.iniPath.c_str()) != 0;
    gApp.launchWithoutSeeking =
        GetPrivateProfileIntW(
            L"Playback", L"LaunchWithoutSeeking", 0, gApp.iniPath.c_str()) != 0;
    gApp.folderWatchEnabled =
        GetPrivateProfileIntW(L"Folder", L"AutoIndex", 1, gApp.iniPath.c_str()) != 0;
    findEtlExecutable();
    populateFolderWeapons();
    populatePostDeathWindows(gApp.postDeathWindow);
    populatePostDeathWindows(gApp.folderPostDeathWindow);
    populateHighlightResults();
    updatePlaybackButtonLabels();
    searchLibrary(false);
    std::wstring savedFolder(32768, L'\0');
    const DWORD savedFolderLength = GetPrivateProfileStringW(
        L"Folder",
        L"Path",
        L"",
        savedFolder.data(),
        static_cast<DWORD>(savedFolder.size()),
        gApp.iniPath.c_str());
    savedFolder.resize(savedFolderLength);
    std::error_code savedFolderError;
    if (!savedFolder.empty() &&
        std::filesystem::is_directory(savedFolder, savedFolderError) && !savedFolderError) {
        setDemoFolder(savedFolder);
    }
    updateTabLabels();
    showTab(0);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            createInterface(window);
            return 0;
        case kFolderScanProgressMessage:
            if (gApp.folderScanRunning) {
                setStatus(
                    L"Scanning demo " + std::to_wstring(static_cast<std::size_t>(wParam)) +
                    L" of " + std::to_wstring(static_cast<std::size_t>(lParam)) +
                    L" — using each demo's recorded POV…");
            }
            return 0;
        case kFolderScanCompleteMessage: {
            std::unique_ptr<FolderScanOutcome> outcome(
                reinterpret_cast<FolderScanOutcome*>(lParam));
            if (outcome != nullptr) {
                finishFolderScan(std::move(outcome));
            }
            return 0;
        }
        case kFolderChangedMessage:
            if (gApp.folderWatchEnabled && !gApp.demoFolder.empty()) {
                SetTimer(window, kFolderWatchDebounceTimer, 1500, nullptr);
            }
            return 0;
        case WM_TIMER:
            if (wParam == kFolderWatchDebounceTimer) {
                KillTimer(window, kFolderWatchDebounceTimer);
                if (gApp.folderScanRunning) {
                    gApp.folderRescanPending = true;
                } else {
                    startFolderScan(true);
                }
                return 0;
            }
            break;
        case WM_SIZE:
            layoutAllControls(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_DPICHANGED: {
            const int newDpi = HIWORD(wParam);
            if (newDpi > 0 && newDpi != gApp.dpi) {
                gApp.dpi = newDpi;
                recreateDpiResources();
                rescaleFixedListColumns();
            }
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested != nullptr) {
                SetWindowPos(
                    window,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            RECT client{};
            GetClientRect(window, &client);
            layoutAllControls(client.right, client.bottom);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            int workWidth = scale(1180);
            int workHeight = scale(760);
            if (GetMonitorInfoW(monitor, &monitorInfo)) {
                workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
                workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
            }
            limits->ptMinTrackSize.x = std::min(scale(1180), workWidth);
            limits->ptMinTrackSize.y = std::min(scale(760), workHeight);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            paintWindow(dc);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DRAWITEM:
            drawOwnerButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMuted);
            if (reinterpret_cast<HWND>(lParam) == gApp.status) {
                return reinterpret_cast<LRESULT>(gApp.backgroundBrush);
            }
            return reinterpret_cast<LRESULT>(gApp.panelBrush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kControl);
            return reinterpret_cast<LRESULT>(gApp.controlBrush);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kText);
            return reinterpret_cast<LRESULT>(gApp.panelBrush);
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IdOpenDemo: {
                    const auto path = chooseDemo();
                    if (path.has_value()) {
                        loadDemo(*path);
                    }
                    return 0;
                }
                case IdTabMultiKills:
                    showTab(0);
                    return 0;
                case IdTabAllEvents:
                    showTab(1);
                    return 0;
                case IdTabFolderScan:
                    showTab(2);
                    return 0;
                case IdTabHighlights:
                    showTab(3);
                    return 0;
                case IdTabLibrary:
                    showTab(4);
                    return 0;
                case IdExportCurrent:
                    exportCurrentView();
                    return 0;
                case IdLaunchAsAdministrator:
                    gApp.launchEtlAsAdministrator = !gApp.launchEtlAsAdministrator;
                    InvalidateRect(gApp.launchAsAdministrator, nullptr, TRUE);
                    if (!savePlaybackSettings()) {
                        MessageBoxW(
                            gApp.window,
                            L"The playback preference could not be saved.",
                            L"Settings warning",
                            MB_OK | MB_ICONWARNING);
                    }
                    setStatus(
                        gApp.launchEtlAsAdministrator
                            ? L"ET: Legacy will request administrator privileges through Windows UAC when playback starts."
                            : L"ET: Legacy will use normal user privileges when playback starts.");
                    return 0;
                case IdLaunchWithoutSeeking:
                    gApp.launchWithoutSeeking = !gApp.launchWithoutSeeking;
                    InvalidateRect(gApp.launchWithoutSeekingControl, nullptr, TRUE);
                    updatePlaybackButtonLabels();
                    if (!savePlaybackSettings()) {
                        MessageBoxW(
                            gApp.window,
                            L"The playback preference could not be saved.",
                            L"Settings warning",
                            MB_OK | MB_ICONWARNING);
                    }
                    setStatus(
                        gApp.launchWithoutSeeking
                            ? L"Diagnostic playback enabled — demos will start from the beginning without an automatic seek."
                            : L"Automatic playback seek restored — selected actions start five seconds early.");
                    return 0;
                case IdEventPlayer:
                    if (HIWORD(wParam) == CBN_SELCHANGE && gApp.demoLoaded) {
                        populateAllEvents();
                        const int playerClientNum = selectedEventPlayerId();
                        if (playerClientNum < 0) {
                            setStatus(
                                L"Showing the complete demo log • " +
                                std::to_wstring(gApp.demo.kills.size()) + L" event(s)");
                        } else {
                            const PlayerSelection player = selectedEventPlayer();
                            setStatus(
                                L"Showing " + std::to_wstring(visibleEventCount()) +
                                L" event(s) involving " +
                                playerDisplayName(player.clientNum, player.sessionId) +
                                L" • kills, deaths, suicides and teamkills");
                        }
                    }
                    return 0;
                case IdTeamKills:
                    gApp.includeTeamKills = !gApp.includeTeamKills;
                    InvalidateRect(gApp.teamKills, nullptr, TRUE);
                    return 0;
                case IdWarmupKills:
                    gApp.includeWarmupKills = !gApp.includeWarmupKills;
                    InvalidateRect(gApp.warmupKills, nullptr, TRUE);
                    setStatus(
                        gApp.includeWarmupKills
                            ? L"Warmup kills will be included in the next multi-kill search."
                            : L"Warmup kills will be excluded from the next multi-kill search.");
                    return 0;
                case IdPostDeathExplosives:
                    gApp.postDeathExplosivesEnabled = !gApp.postDeathExplosivesEnabled;
                    InvalidateRect(gApp.postDeathExplosives, nullptr, TRUE);
                    setStatus(
                        gApp.postDeathExplosivesEnabled
                            ? L"Post-death explosive continuation enabled — edit Window (seconds) at any time."
                            : L"Post-death explosive continuation disabled — the saved window value remains editable.");
                    return 0;
                case IdChooseFolder: {
                    const auto folder = chooseDemoFolder();
                    if (folder.has_value()) {
                        setDemoFolder(*folder);
                    }
                    return 0;
                }
                case IdFolderTeamKills:
                    gApp.folderIncludeTeamKills = !gApp.folderIncludeTeamKills;
                    InvalidateRect(gApp.folderTeamKills, nullptr, TRUE);
                    if (!gApp.folderDemos.empty()) {
                        setStatus(L"Folder filters changed — select Apply cached filters. No rescan is required.");
                    }
                    return 0;
                case IdFolderWarmupKills:
                    gApp.folderIncludeWarmupKills = !gApp.folderIncludeWarmupKills;
                    InvalidateRect(gApp.folderWarmupKills, nullptr, TRUE);
                    if (!gApp.folderDemos.empty()) {
                        setStatus(L"Folder filters changed — select Apply cached filters. No rescan is required.");
                    }
                    return 0;
                case IdFolderPostDeathExplosives:
                    gApp.folderPostDeathExplosivesEnabled =
                        !gApp.folderPostDeathExplosivesEnabled;
                    InvalidateRect(gApp.folderPostDeathExplosives, nullptr, TRUE);
                    if (!gApp.folderDemos.empty()) {
                        setStatus(L"Folder filters changed — select Apply cached filters. No rescan is required.");
                    }
                    return 0;
                case IdFolderWatch:
                    gApp.folderWatchEnabled = !gApp.folderWatchEnabled;
                    InvalidateRect(gApp.folderWatch, nullptr, TRUE);
                    if (!WritePrivateProfileStringW(
                        L"Folder",
                        L"AutoIndex",
                        gApp.folderWatchEnabled ? L"1" : L"0",
                        gApp.iniPath.c_str())) {
                        MessageBoxW(
                            gApp.window,
                            L"The auto-index preference could not be saved.",
                            L"Settings warning",
                            MB_OK | MB_ICONWARNING);
                    }
                    if (gApp.folderWatchEnabled) {
                        startFolderWatcher();
                        setStatus(L"Folder watching enabled — new or changed demos will be indexed automatically.");
                    } else {
                        stopFolderWatcher();
                        setStatus(L"Folder watching disabled.");
                    }
                    return 0;
                case IdFolderApplyFilters:
                    applyFolderFilters(true);
                    return 0;
                case IdFolderScan:
                    startFolderScan();
                    return 0;
                case IdSearch:
                    searchRuns();
                    return 0;
                case IdChooseEtl:
                    chooseEtlExecutable();
                    return 0;
                case IdPlayRun:
                    playSelectedRun();
                    return 0;
                case IdAddRunHighlight:
                    addSelectedRunToHighlights();
                    return 0;
                case IdPlayEvent:
                    playSelectedEvent();
                    return 0;
                case IdViewProtocolLog:
                    openProtocolInspector();
                    return 0;
                case IdPlayFolderRun:
                    playSelectedFolderRun();
                    return 0;
                case IdAddFolderHighlight:
                    addSelectedFolderRunToHighlights();
                    return 0;
                case IdPlayHighlight:
                    playSelectedHighlight();
                    return 0;
                case IdRemoveHighlight:
                    removeSelectedHighlight();
                    return 0;
                case IdClearHighlights:
                    clearHighlights();
                    return 0;
                case IdLibraryScope:
                    gApp.libraryFolderOnly = !gApp.libraryFolderOnly;
                    InvalidateRect(gApp.libraryScope, nullptr, TRUE);
                    searchLibrary(true);
                    return 0;
                case IdLibraryDuplicates:
                    gApp.libraryDuplicatesOnly = !gApp.libraryDuplicatesOnly;
                    InvalidateRect(gApp.libraryDuplicates, nullptr, TRUE);
                    searchLibrary(true);
                    return 0;
                case IdLibrarySearch:
                    searchLibrary(true);
                    return 0;
                case IdLibraryOpen:
                    openSelectedLibraryDemo();
                    return 0;
            }
            if (!gApp.folderScanRunning && !gApp.folderDemos.empty()) {
                const int id = LOWORD(wParam);
                const int notification = HIWORD(wParam);
                const bool filterChanged =
                    ((id == IdFolderMinimumKills || id == IdFolderMinimumHeadshots ||
                      id == IdFolderMaximumGap) &&
                     notification == EN_CHANGE) ||
                    (id == IdFolderQuery && notification == EN_CHANGE) ||
                    (id == IdFolderWeapon && notification == CBN_SELCHANGE) ||
                    (id == IdFolderField && notification == CBN_SELCHANGE) ||
                    (id == IdFolderPostDeathWindow &&
                     (notification == CBN_SELCHANGE || notification == CBN_EDITCHANGE));
                if (filterChanged) {
                    setStatus(L"Folder search or multi-kill filters changed — select Apply cached filters. No rescan is required.");
                }
            }
            break;
        case WM_NOTIFY: {
            const NMHDR* notification = reinterpret_cast<NMHDR*>(lParam);
            if (notification->code == NM_CUSTOMDRAW &&
                (notification->idFrom == IdRunList || notification->idFrom == IdRunKillList ||
                 notification->idFrom == IdAllEventList ||
                 notification->idFrom == IdFolderRunList ||
                 notification->idFrom == IdFolderKillList ||
                 notification->idFrom == IdHighlightList ||
                 notification->idFrom == IdLibraryList)) {
                return handleListCustomDraw(
                    *reinterpret_cast<NMLVCUSTOMDRAW*>(lParam), notification->hwndFrom);
            }
            if (notification->code == LVN_COLUMNCLICK &&
                (notification->idFrom == IdRunList || notification->idFrom == IdRunKillList ||
                 notification->idFrom == IdAllEventList ||
                 notification->idFrom == IdFolderRunList ||
                 notification->idFrom == IdFolderKillList ||
                 notification->idFrom == IdHighlightList ||
                 notification->idFrom == IdLibraryList)) {
                const auto* columnClick = reinterpret_cast<NMLISTVIEW*>(lParam);
                sortListByColumn(notification->hwndFrom, columnClick->iSubItem);
                return 0;
            }
            if (notification->idFrom == IdRunList && notification->code == LVN_ITEMCHANGED) {
                const auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((changed->uNewState & LVIS_SELECTED) != 0) {
                    populateRunKillDetails(selectedListData(gApp.runList));
                    EnableWindow(gApp.playRun, TRUE);
                    EnableWindow(gApp.addRunHighlight, TRUE);
                    InvalidateRect(gApp.timeline, nullptr, FALSE);
                }
                return 0;
            }
            if (notification->idFrom == IdAllEventList && notification->code == LVN_ITEMCHANGED) {
                const auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((changed->uNewState & LVIS_SELECTED) != 0) {
                    EnableWindow(gApp.playEvent, TRUE);
                    InvalidateRect(gApp.timeline, nullptr, FALSE);
                }
                return 0;
            }
            if (notification->idFrom == IdFolderRunList &&
                notification->code == LVN_ITEMCHANGED) {
                const auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((changed->uNewState & LVIS_SELECTED) != 0) {
                    const int rowIndex = selectedListData(gApp.folderRunList);
                    populateFolderKillDetails(rowIndex);
                    EnableWindow(gApp.addFolderHighlight, TRUE);
                    InvalidateRect(gApp.timeline, nullptr, FALSE);
                    if (rowIndex >= 0 &&
                        rowIndex < static_cast<int>(gApp.folderRows.size())) {
                        const std::size_t runIndex =
                            gApp.folderRows[static_cast<std::size_t>(rowIndex)];
                        if (runIndex < gApp.folderRuns.size()) {
                            const FolderRunResult& result = gApp.folderRuns[runIndex];
                            setStatus(
                                L"Folder timeline: " +
                                result.demo.path.filename().wstring() +
                                L" only • selected multi-kill at " +
                                durationText(result.run.startDemoTimeMs));
                        }
                    }
                }
                return 0;
            }
            if (notification->idFrom == IdHighlightList &&
                notification->code == LVN_ITEMCHANGED) {
                const auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((changed->uNewState & LVIS_SELECTED) != 0) {
                    EnableWindow(gApp.playHighlight, TRUE);
                    EnableWindow(gApp.removeHighlight, TRUE);
                    InvalidateRect(gApp.timeline, nullptr, FALSE);
                }
                return 0;
            }
            if (notification->idFrom == IdLibraryList &&
                notification->code == LVN_ITEMCHANGED) {
                const auto* changed = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((changed->uNewState & LVIS_SELECTED) != 0) {
                    EnableWindow(gApp.libraryOpen, TRUE);
                }
                return 0;
            }
            if (notification->idFrom == IdRunList && notification->code == NM_DBLCLK) {
                playSelectedRun();
                return 0;
            }
            if (notification->idFrom == IdAllEventList && notification->code == NM_DBLCLK) {
                playSelectedEvent();
                return 0;
            }
            if (notification->idFrom == IdFolderRunList && notification->code == NM_DBLCLK) {
                playSelectedFolderRun();
                return 0;
            }
            if (notification->idFrom == IdHighlightList && notification->code == NM_DBLCLK) {
                playSelectedHighlight();
                return 0;
            }
            if (notification->idFrom == IdLibraryList && notification->code == NM_DBLCLK) {
                openSelectedLibraryDemo();
                return 0;
            }
            break;
        }
        case WM_DROPFILES: {
            const HDROP drop = reinterpret_cast<HDROP>(wParam);
            std::wstring file(32768, L'\0');
            const UINT length = DragQueryFileW(drop, 0, file.data(), static_cast<UINT>(file.size()));
            file.resize(length);
            DragFinish(drop);
            if (!file.empty()) {
                const std::filesystem::path path(file);
                std::error_code pathError;
                if (std::filesystem::is_directory(path, pathError) && !pathError) {
                    setDemoFolder(path);
                    showTab(2);
                } else {
                    showTab(0);
                    loadDemo(path);
                }
            }
            return 0;
        }
        case WM_DESTROY:
            if (gApp.protocolInspector != nullptr) {
                DestroyWindow(gApp.protocolInspector);
                gApp.protocolInspector = nullptr;
            }
            stopFolderWatcher();
            if (gApp.folderCancelRequested != nullptr) {
                gApp.folderCancelRequested->store(true);
            }
            if (gApp.folderThread != nullptr) {
                WaitForSingleObject(gApp.folderThread, 10000);
                CloseHandle(gApp.folderThread);
                gApp.folderThread = nullptr;
            }
            gApp.demoIndex.close();
            for (HFONT font : {gApp.font, gApp.smallFont, gApp.labelFont, gApp.titleFont}) {
                if (font != nullptr) {
                    DeleteObject(font);
                }
            }
            for (HBRUSH brush : {gApp.backgroundBrush, gApp.panelBrush, gApp.controlBrush}) {
                if (brush != nullptr) {
                    DeleteObject(brush);
                }
            }
            if (gApp.rowHeightImages != nullptr) {
                ImageList_Destroy(gApp.rowHeightImages);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void enableDarkTitleBar(HWND window) {
    const BOOL enabled = TRUE;
    if (FAILED(DwmSetWindowAttribute(window, 20, &enabled, sizeof(enabled)))) {
        DwmSetWindowAttribute(window, 19, &enabled, sizeof(enabled));
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT comInitialization =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW inspectorClass{};
    inspectorClass.cbSize = sizeof(inspectorClass);
    inspectorClass.style = CS_HREDRAW | CS_VREDRAW;
    inspectorClass.lpfnWndProc = protocolInspectorProcedure;
    inspectorClass.hInstance = instance;
    inspectorClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    inspectorClass.hIcon = LoadIconW(
        instance,
        MAKEINTRESOURCEW(kApplicationIconResource));
    if (inspectorClass.hIcon == nullptr) {
        inspectorClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    inspectorClass.hIconSm = inspectorClass.hIcon;
    inspectorClass.hbrBackground = nullptr;
    inspectorClass.lpszClassName = kProtocolInspectorClass;
    if (!RegisterClassExW(&inspectorClass)) {
        if (SUCCEEDED(comInitialization)) CoUninitialize();
        return 1;
    }

    WNDCLASSEXW timelineClass{};
    timelineClass.cbSize = sizeof(timelineClass);
    timelineClass.style = CS_HREDRAW | CS_VREDRAW;
    timelineClass.lpfnWndProc = timelineProcedure;
    timelineClass.hInstance = instance;
    timelineClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    timelineClass.hbrBackground = nullptr;
    timelineClass.lpszClassName = kTimelineClass;
    if (!RegisterClassExW(&timelineClass)) {
        if (SUCCEEDED(comInitialization)) {
            CoUninitialize();
        }
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(
        instance,
        MAKEINTRESOURCEW(kApplicationIconResource));
    if (windowClass.hIcon == nullptr) {
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    windowClass.hIconSm = reinterpret_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(kApplicationIconResource),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (windowClass.hIconSm == nullptr) {
        windowClass.hIconSm = windowClass.hIcon;
    }
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        if (SUCCEEDED(comInitialization)) {
            CoUninitialize();
        }
        return 1;
    }

    const int initialDpi = dpiForWindow(nullptr);
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workWidth = std::max(640, static_cast<int>(workArea.right - workArea.left));
    const int workHeight = std::max(480, static_cast<int>(workArea.bottom - workArea.top));
    const int initialWidth = std::min(MulDiv(1380, initialDpi, 96), workWidth);
    const int initialHeight = std::min(MulDiv(880, initialDpi, 96), workHeight);
    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        kApplicationName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        initialWidth,
        initialHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        if (SUCCEEDED(comInitialization)) {
            CoUninitialize();
        }
        return 1;
    }
    enableDarkTitleBar(window);
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments != nullptr) {
        if (argumentCount >= 2 && std::filesystem::is_regular_file(arguments[1])) {
            loadDemo(std::filesystem::path(arguments[1]));
        }
        LocalFree(arguments);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (SUCCEEDED(comInitialization)) {
        CoUninitialize();
    }
    return static_cast<int>(message.wParam);
}
