// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etl_demo_parser.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace etlfrag {

inline constexpr int kDemoIndexParserRevision = 4;

struct DemoFileFingerprint {
    std::uint64_t size = 0;
    std::int64_t modifiedTicks = 0;
};

struct IndexedDemo {
    std::filesystem::path path;
    DemoFileFingerprint fingerprint;
    DemoInfo demo;
};

class DemoIndex {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool save(const std::filesystem::path& path, std::string& error) const;

    const DemoInfo* findFresh(const std::filesystem::path& path) const;
    bool upsert(const std::filesystem::path& path, const DemoInfo& demo);
    std::size_t size() const noexcept;
    void clear();
    std::vector<IndexedDemo> allEntries() const;

private:
    std::unordered_map<std::string, IndexedDemo> entries_;
};

enum class DemoSearchField {
    All,
    Nickname,
    Map,
    Date,
    Filename,
};

struct IndexedDemoSummary {
    std::int64_t id = -1;
    std::filesystem::path path;
    std::string fileName;
    std::string recordedDate;
    std::string mapName;
    std::string gameName;
    std::string modVersion;
    std::string povName;
    int povClientNum = -1;
    std::int32_t firstServerTimeMs = -1;
    std::int32_t lastServerTimeMs = -1;
    std::uint64_t fileSize = 0;
    std::int64_t modifiedTicks = 0;
    std::string partialHash;
    std::size_t playerCount = 0;
    std::size_t eventCount = 0;
    std::size_t duplicateCount = 1;
    int parserRevision = 0;
};

// SQLite-backed persistent index. Demo payloads stay on disk and are loaded
// only when a filter, timeline or playback operation needs one specific demo.
class SqliteDemoIndex {
public:
    SqliteDemoIndex() = default;
    ~SqliteDemoIndex();
    SqliteDemoIndex(const SqliteDemoIndex&) = delete;
    SqliteDemoIndex& operator=(const SqliteDemoIndex&) = delete;

    bool open(const std::filesystem::path& path, std::string& error);
    void close();
    bool isOpen() const noexcept;

    std::optional<DemoInfo> findFresh(
        const std::filesystem::path& path,
        std::string& error) const;
    std::optional<DemoInfo> loadById(std::int64_t id, std::string& error) const;
    bool upsert(
        const std::filesystem::path& path,
        const DemoInfo& demo,
        std::string& error);
    bool remove(const std::filesystem::path& path, std::string& error);
    bool pruneMissingInFolder(
        const std::filesystem::path& folder,
        std::size_t& removed,
        std::string& error);

    std::vector<IndexedDemoSummary> listFolder(
        const std::filesystem::path& folder,
        std::string& error) const;
    std::vector<IndexedDemoSummary> search(
        const std::string& query,
        DemoSearchField field,
        const std::optional<std::filesystem::path>& folder,
        bool duplicatesOnly,
        std::size_t limit,
        std::string& error) const;
    std::size_t size(std::string& error) const;

    bool importLegacyIndex(
        const std::filesystem::path& legacyPath,
        std::size_t& imported,
        std::string& error);

private:
    ::sqlite3* database_ = nullptr;
    std::filesystem::path path_;
};

struct HighlightEvent {
    std::int32_t demoTimeMs = 0;
    std::string victim;
    std::string weapon;
    bool headshot = false; // Headshot killing blow, not aggregate hit count.
    bool teamKill = false;
};

struct HighlightItem {
    std::filesystem::path demoPath;
    std::string mapName;
    std::string povName;
    std::int32_t startDemoTimeMs = 0;
    std::int32_t endDemoTimeMs = 0;
    std::int32_t matchRemainingMs = -1;
    // Aggregate confirmed headshot hits in the action. HighlightEvent::headshot
    // remains the obituary flag for the individual killing blow.
    int headshotCount = 0;
    std::string description;
    std::vector<HighlightEvent> events;
};

bool sameHighlight(const HighlightItem& left, const HighlightItem& right);
bool loadHighlights(
    const std::filesystem::path& path,
    std::vector<HighlightItem>& highlights,
    std::string& error);
bool saveHighlights(
    const std::filesystem::path& path,
    const std::vector<HighlightItem>& highlights,
    std::string& error);

} // namespace etlfrag
