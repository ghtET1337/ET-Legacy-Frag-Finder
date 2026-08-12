// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_storage.hpp"

#include "../third_party/sqlite/sqlite3.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#endif

namespace etlfrag {
namespace {

constexpr std::array<char, 8> kIndexMagic{{'E', 'T', 'L', 'I', 'D', 'X', '1', '3'}};
constexpr std::array<char, 8> kLegacyHighlightMagic{{'E', 'T', 'L', 'H', 'I', 'L', '1', '3'}};
constexpr std::array<char, 8> kHighlightMagic{{'E', 'T', 'L', 'H', 'I', 'L', '1', '4'}};
constexpr std::uint32_t kStorageVersion = 1;
constexpr std::uint32_t kMaximumStringBytes = 16U * 1024U * 1024U;
constexpr std::uint32_t kMaximumEntries = 250000U;
constexpr std::uint32_t kMaximumChildren = 2000000U;

template <typename T>
bool writeScalar(std::ostream& stream, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

template <typename T>
bool readScalar(std::istream& stream, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(stream);
}

bool writeBool(std::ostream& stream, bool value) {
    const std::uint8_t stored = value ? 1U : 0U;
    return writeScalar(stream, stored);
}

bool readBool(std::istream& stream, bool& value) {
    std::uint8_t stored = 0;
    if (!readScalar(stream, stored) || stored > 1U) {
        return false;
    }
    value = stored != 0;
    return true;
}

bool writeString(std::ostream& stream, const std::string& value) {
    if (value.size() > kMaximumStringBytes) {
        return false;
    }
    const auto size = static_cast<std::uint32_t>(value.size());
    if (!writeScalar(stream, size)) {
        return false;
    }
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(stream);
}

bool readString(std::istream& stream, std::string& value) {
    std::uint32_t size = 0;
    if (!readScalar(stream, size) || size > kMaximumStringBytes) {
        return false;
    }
    value.assign(size, '\0');
    if (size != 0U) {
        stream.read(value.data(), static_cast<std::streamsize>(size));
    }
    return static_cast<bool>(stream);
}

std::string pathBytes(const std::filesystem::path& path) {
#if defined(_WIN32)
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), count, nullptr, nullptr);
    return result;
#else
    return path.u8string();
#endif
}

std::filesystem::path pathFromBytes(const std::string& value) {
#if defined(_WIN32)
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return std::filesystem::path(result);
#else
    return std::filesystem::u8path(value);
#endif
}

std::string wideBytes(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int count = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), count, nullptr, nullptr);
    return result;
#else
    std::string result;
    result.reserve(wide.size());
    for (const wchar_t value : wide) {
        const std::uint32_t character = static_cast<std::uint32_t>(value);
        if (character <= 0x7fU) {
            result.push_back(static_cast<char>(character));
        } else if (character <= 0x7ffU) {
            result.push_back(static_cast<char>(0xc0U | (character >> 6U)));
            result.push_back(static_cast<char>(0x80U | (character & 0x3fU)));
        } else if (character <= 0xffffU) {
            result.push_back(static_cast<char>(0xe0U | (character >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((character >> 6U) & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | (character & 0x3fU)));
        } else {
            result.push_back(static_cast<char>(0xf0U | (character >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((character >> 12U) & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | ((character >> 6U) & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | (character & 0x3fU)));
        }
    }
    return result;
#endif
}

std::wstring wideFromBytes(const std::string& value) {
    if (value.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int count = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
#else
    std::wstring result;
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        std::uint32_t character = 0;
        std::size_t length = 1;
        if ((first & 0x80U) == 0U) {
            character = first;
        } else if ((first & 0xe0U) == 0xc0U) {
            character = first & 0x1fU;
            length = 2;
        } else if ((first & 0xf0U) == 0xe0U) {
            character = first & 0x0fU;
            length = 3;
        } else if ((first & 0xf8U) == 0xf0U) {
            character = first & 0x07U;
            length = 4;
        } else {
            ++index;
            continue;
        }
        if (index + length > value.size()) {
            break;
        }
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xc0U) != 0x80U) {
                valid = false;
                break;
            }
            character = (character << 6U) | (continuation & 0x3fU);
        }
        if (valid) {
            result.push_back(static_cast<wchar_t>(character));
        }
        index += valid ? length : 1;
    }
    return result;
#endif
}

std::string normalizedPathKey(const std::filesystem::path& input) {
    std::error_code error;
    std::filesystem::path path = std::filesystem::absolute(input, error);
    if (error) {
        path = input;
    }
    path = path.lexically_normal();
    std::string key = pathBytes(path);
#ifdef _WIN32
    std::transform(
        key.begin(), key.end(), key.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
#endif
    return key;
}

bool fingerprintFor(
    const std::filesystem::path& path,
    DemoFileFingerprint& fingerprint) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return false;
    }
    fingerprint.size = static_cast<std::uint64_t>(size);
    fingerprint.modifiedTicks = static_cast<std::int64_t>(modified.time_since_epoch().count());
    return true;
}

bool sameFingerprint(
    const DemoFileFingerprint& left,
    const DemoFileFingerprint& right) {
    return left.size == right.size && left.modifiedTicks == right.modifiedTicks;
}

bool writePlayer(std::ostream& stream, const Player& player) {
    return writeScalar(stream, player.clientNum) &&
           writeString(stream, player.name) &&
           writeString(stream, player.cleanName) &&
           writeScalar(stream, player.team);
}

bool readPlayer(std::istream& stream, Player& player) {
    return readScalar(stream, player.clientNum) &&
           readString(stream, player.name) &&
           readString(stream, player.cleanName) &&
           readScalar(stream, player.team);
}

bool writeKill(std::ostream& stream, const KillEvent& kill) {
    return writeScalar(stream, kill.serverTimeMs) &&
           writeScalar(stream, kill.demoTimeMs) &&
           writeScalar(stream, kill.attacker) &&
           writeScalar(stream, kill.target) &&
           writeScalar(stream, kill.attackerTeam) &&
           writeScalar(stream, kill.targetTeam) &&
           writeScalar(stream, kill.weapon) &&
           writeScalar(stream, kill.meansOfDeath) &&
           writeBool(stream, kill.teamKill) &&
           writeBool(stream, kill.suicide) &&
           writeBool(stream, kill.headshot) &&
           writeString(stream, kill.attackerName) &&
           writeString(stream, kill.targetName) &&
           writeScalar(stream, kill.matchElapsedMs) &&
           writeScalar(stream, kill.matchRemainingMs);
}

bool readKill(std::istream& stream, KillEvent& kill) {
    return readScalar(stream, kill.serverTimeMs) &&
           readScalar(stream, kill.demoTimeMs) &&
           readScalar(stream, kill.attacker) &&
           readScalar(stream, kill.target) &&
           readScalar(stream, kill.attackerTeam) &&
           readScalar(stream, kill.targetTeam) &&
           readScalar(stream, kill.weapon) &&
           readScalar(stream, kill.meansOfDeath) &&
           readBool(stream, kill.teamKill) &&
           readBool(stream, kill.suicide) &&
           readBool(stream, kill.headshot) &&
           readString(stream, kill.attackerName) &&
           readString(stream, kill.targetName) &&
           readScalar(stream, kill.matchElapsedMs) &&
           readScalar(stream, kill.matchRemainingMs);
}

template <typename T, typename Writer>
bool writeVector(std::ostream& stream, const std::vector<T>& values, Writer writer) {
    if (values.size() > kMaximumChildren) {
        return false;
    }
    const auto count = static_cast<std::uint32_t>(values.size());
    if (!writeScalar(stream, count)) {
        return false;
    }
    for (const T& value : values) {
        if (!writer(stream, value)) {
            return false;
        }
    }
    return true;
}

template <typename T, typename Reader>
bool readVector(std::istream& stream, std::vector<T>& values, Reader reader) {
    std::uint32_t count = 0;
    if (!readScalar(stream, count) || count > kMaximumChildren) {
        return false;
    }
    values.clear();
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        T value;
        if (!reader(stream, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool writeDemo(std::ostream& stream, const DemoInfo& demo) {
    return writeString(stream, demo.mapName) &&
           writeString(stream, demo.gameName) &&
           writeString(stream, demo.modVersion) &&
           writeString(stream, demo.povName) &&
           writeScalar(stream, demo.povClientNum) &&
           writeScalar(stream, demo.firstServerTimeMs) &&
           writeScalar(stream, demo.lastServerTimeMs) &&
           writeScalar(stream, demo.levelStartTimeMs) &&
           writeScalar(stream, demo.timeLimitMinutes) &&
           writeVector(stream, demo.players, writePlayer) &&
           writeVector(stream, demo.kills, writeKill) &&
           writeVector(
               stream,
               demo.warnings,
               [](std::ostream& target, const std::string& warning) {
                   return writeString(target, warning);
               });
}

bool readDemo(std::istream& stream, DemoInfo& demo) {
    return readString(stream, demo.mapName) &&
           readString(stream, demo.gameName) &&
           readString(stream, demo.modVersion) &&
           readString(stream, demo.povName) &&
           readScalar(stream, demo.povClientNum) &&
           readScalar(stream, demo.firstServerTimeMs) &&
           readScalar(stream, demo.lastServerTimeMs) &&
           readScalar(stream, demo.levelStartTimeMs) &&
           readScalar(stream, demo.timeLimitMinutes) &&
           readVector(stream, demo.players, readPlayer) &&
           readVector(stream, demo.kills, readKill) &&
           readVector(
               stream,
               demo.warnings,
               [](std::istream& source, std::string& warning) {
                   return readString(source, warning);
               });
}

bool writeHighlightEvent(std::ostream& stream, const HighlightEvent& event) {
    return writeScalar(stream, event.demoTimeMs) &&
           writeString(stream, event.victim) &&
           writeString(stream, event.weapon) &&
           writeBool(stream, event.headshot) &&
           writeBool(stream, event.teamKill);
}

bool readHighlightEvent(std::istream& stream, HighlightEvent& event) {
    return readScalar(stream, event.demoTimeMs) &&
           readString(stream, event.victim) &&
           readString(stream, event.weapon) &&
           readBool(stream, event.headshot) &&
           readBool(stream, event.teamKill);
}

bool writeHighlight(std::ostream& stream, const HighlightItem& highlight) {
    return writeString(stream, pathBytes(highlight.demoPath)) &&
           writeString(stream, highlight.mapName) &&
           writeString(stream, highlight.povName) &&
           writeScalar(stream, highlight.startDemoTimeMs) &&
           writeScalar(stream, highlight.endDemoTimeMs) &&
           writeScalar(stream, highlight.matchRemainingMs) &&
           writeScalar(stream, highlight.headshotCount) &&
           writeString(stream, highlight.description) &&
           writeVector(stream, highlight.events, writeHighlightEvent);
}

bool readHighlight(std::istream& stream, HighlightItem& highlight) {
    std::string path;
    if (!readString(stream, path)) {
        return false;
    }
    highlight.demoPath = pathFromBytes(path);
    return readString(stream, highlight.mapName) &&
           readString(stream, highlight.povName) &&
           readScalar(stream, highlight.startDemoTimeMs) &&
           readScalar(stream, highlight.endDemoTimeMs) &&
           readScalar(stream, highlight.matchRemainingMs) &&
           readScalar(stream, highlight.headshotCount) &&
           readString(stream, highlight.description) &&
           readVector(stream, highlight.events, readHighlightEvent);
}

bool readLegacyHighlight(std::istream& stream, HighlightItem& highlight) {
    std::string path;
    if (!readString(stream, path)) {
        return false;
    }
    highlight.demoPath = pathFromBytes(path);
    if (!readString(stream, highlight.mapName) ||
        !readString(stream, highlight.povName) ||
        !readScalar(stream, highlight.startDemoTimeMs) ||
        !readScalar(stream, highlight.endDemoTimeMs) ||
        !readScalar(stream, highlight.matchRemainingMs) ||
        !readString(stream, highlight.description) ||
        !readVector(stream, highlight.events, readHighlightEvent)) {
        return false;
    }
    // Version 1.7.0 only persisted the headshot-kill boolean. Preserve the
    // best available value for legacy basket rows; newly saved rows carry the
    // exact aggregate hit count.
    highlight.headshotCount = static_cast<int>(std::count_if(
        highlight.events.begin(),
        highlight.events.end(),
        [](const HighlightEvent& event) { return event.headshot; }));
    return true;
}

bool replaceFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string& error) {
#ifdef _WIN32
    if (MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = "Could not replace the storage file (Windows error " +
            std::to_string(GetLastError()) + ").";
    return false;
#else
    std::error_code renameError;
    std::filesystem::rename(temporary, destination, renameError);
    if (!renameError) {
        return true;
    }
    std::error_code removeError;
    std::filesystem::remove(destination, removeError);
    renameError.clear();
    std::filesystem::rename(temporary, destination, renameError);
    if (!renameError) {
        return true;
    }
    error = "Could not replace the storage file: " + renameError.message();
    return false;
#endif
}

template <typename Writer>
bool saveAtomically(
    const std::filesystem::path& path,
    const std::array<char, 8>& magic,
    Writer writer,
    std::string& error) {
    std::error_code directoryError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), directoryError);
    }
    if (directoryError) {
        error = "Could not create the application data folder: " + directoryError.message();
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "Could not open the temporary storage file.";
        return false;
    }
    stream.write(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!writeScalar(stream, kStorageVersion) || !writer(stream)) {
        error = "Could not write the complete storage file.";
        stream.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    stream.flush();
    stream.close();
    if (!stream) {
        error = "Could not flush the storage file.";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    if (!replaceFile(temporary, path, error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool readHeader(
    std::istream& stream,
    const std::array<char, 8>& expected,
    std::string& error) {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!stream || magic != expected || !readScalar(stream, version) ||
        version != kStorageVersion) {
        error = "The storage file is invalid or belongs to an unsupported version.";
        return false;
    }
    return true;
}

} // namespace

bool DemoIndex::load(const std::filesystem::path& path, std::string& error) {
    entries_.clear();
    error.clear();
    std::error_code existsError;
    if (!std::filesystem::exists(path, existsError)) {
        return !existsError;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Could not open the demo index.";
        return false;
    }
    if (!readHeader(stream, kIndexMagic, error)) {
        return false;
    }
    std::uint32_t count = 0;
    if (!readScalar(stream, count) || count > kMaximumEntries) {
        error = "The demo index contains an invalid entry count.";
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string storedPath;
        IndexedDemo entry;
        if (!readString(stream, storedPath) ||
            !readScalar(stream, entry.fingerprint.size) ||
            !readScalar(stream, entry.fingerprint.modifiedTicks) ||
            !readDemo(stream, entry.demo)) {
            entries_.clear();
            error = "The demo index is truncated or corrupted.";
            return false;
        }
        entry.path = pathFromBytes(storedPath);
        entry.demo.path = entry.path;
        entries_[normalizedPathKey(entry.path)] = std::move(entry);
    }
    return true;
}

bool DemoIndex::save(const std::filesystem::path& path, std::string& error) const {
    error.clear();
    if (entries_.size() > kMaximumEntries) {
        error = "The demo index is too large to save.";
        return false;
    }
    return saveAtomically(
        path,
        kIndexMagic,
        [this](std::ostream& stream) {
            const auto count = static_cast<std::uint32_t>(entries_.size());
            if (!writeScalar(stream, count)) {
                return false;
            }
            std::vector<const IndexedDemo*> ordered;
            ordered.reserve(entries_.size());
            for (const auto& pair : entries_) {
                ordered.push_back(&pair.second);
            }
            std::sort(
                ordered.begin(), ordered.end(),
                [](const IndexedDemo* left, const IndexedDemo* right) {
                    return normalizedPathKey(left->path) < normalizedPathKey(right->path);
                });
            for (const IndexedDemo* entry : ordered) {
                if (!writeString(stream, pathBytes(entry->path)) ||
                    !writeScalar(stream, entry->fingerprint.size) ||
                    !writeScalar(stream, entry->fingerprint.modifiedTicks) ||
                    !writeDemo(stream, entry->demo)) {
                    return false;
                }
            }
            return true;
        },
        error);
}

const DemoInfo* DemoIndex::findFresh(const std::filesystem::path& path) const {
    const auto iterator = entries_.find(normalizedPathKey(path));
    if (iterator == entries_.end()) {
        return nullptr;
    }
    DemoFileFingerprint current;
    if (!fingerprintFor(path, current) || !sameFingerprint(current, iterator->second.fingerprint)) {
        return nullptr;
    }
    return &iterator->second.demo;
}

bool DemoIndex::upsert(const std::filesystem::path& path, const DemoInfo& demo) {
    DemoFileFingerprint fingerprint;
    if (!fingerprintFor(path, fingerprint)) {
        return false;
    }
    IndexedDemo entry;
    std::error_code error;
    entry.path = std::filesystem::absolute(path, error).lexically_normal();
    if (error) {
        entry.path = path.lexically_normal();
    }
    entry.fingerprint = fingerprint;
    entry.demo = demo;
    entry.demo.path = entry.path;
    entries_[normalizedPathKey(entry.path)] = std::move(entry);
    return true;
}

std::size_t DemoIndex::size() const noexcept {
    return entries_.size();
}

void DemoIndex::clear() {
    entries_.clear();
}

std::vector<IndexedDemo> DemoIndex::allEntries() const {
    std::vector<IndexedDemo> result;
    result.reserve(entries_.size());
    for (const auto& pair : entries_) {
        result.push_back(pair.second);
    }
    return result;
}

namespace {

constexpr std::size_t kHashBlockBytes = 64U * 1024U;

bool sqliteExec(sqlite3* database, const char* sql, std::string& error) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    if (result == SQLITE_OK) {
        return true;
    }
    error = message != nullptr ? message : sqlite3_errmsg(database);
    sqlite3_free(message);
    return false;
}

class SqliteStatement {
public:
    SqliteStatement(sqlite3* database, const char* sql, std::string& error)
        : database_(database) {
        if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(database);
        }
    }

    ~SqliteStatement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    explicit operator bool() const noexcept { return statement_ != nullptr; }
    sqlite3_stmt* get() const noexcept { return statement_; }
    sqlite3* database() const noexcept { return database_; }

private:
    sqlite3* database_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
};

bool bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    return sqlite3_bind_text(
               statement,
               index,
               value.data(),
               static_cast<int>(value.size()),
               SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string sqliteText(sqlite3_stmt* statement, int column) {
    const unsigned char* value = sqlite3_column_text(statement, column);
    if (value == nullptr) {
        return {};
    }
    const int bytes = sqlite3_column_bytes(statement, column);
    return std::string(reinterpret_cast<const char*>(value), static_cast<std::size_t>(bytes));
}

bool sqliteDone(SqliteStatement& statement, std::string& error) {
    if (sqlite3_step(statement.get()) == SQLITE_DONE) {
        return true;
    }
    error = sqlite3_errmsg(statement.database());
    return false;
}

bool ensurePersistentSchema(sqlite3* database, std::string& error) {
    return sqliteExec(
        database,
        "CREATE TABLE IF NOT EXISTS app_state("
        "state_key TEXT PRIMARY KEY,"
        "state_value TEXT NOT NULL,"
        "updated_unix INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS queue_jobs("
        "queue_name TEXT NOT NULL,"
        "position INTEGER NOT NULL,"
        "job_id INTEGER NOT NULL,"
        "demo_path TEXT NOT NULL,"
        "label TEXT NOT NULL,"
        "action_start_ms INTEGER NOT NULL,"
        "action_end_ms INTEGER NOT NULL,"
        "job_status INTEGER NOT NULL,"
        "detail TEXT NOT NULL,"
        "output_path TEXT NOT NULL,"
        "log_path TEXT NOT NULL,"
        "PRIMARY KEY(queue_name,position)"
        ");"
        "CREATE INDEX IF NOT EXISTS queue_jobs_name_idx "
        "ON queue_jobs(queue_name,position);",
        error);
}

class ScopedSqliteDatabase {
public:
    ~ScopedSqliteDatabase() {
        if (database_ != nullptr) {
            sqlite3_close_v2(database_);
        }
    }

    bool open(const std::filesystem::path& path, std::string& error) {
        std::error_code directoryError;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), directoryError);
        }
        if (directoryError) {
            error = "Could not create the application data folder: " + directoryError.message();
            return false;
        }
        const std::string bytes = pathBytes(path);
        if (sqlite3_open_v2(
                bytes.c_str(),
                &database_,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                nullptr) != SQLITE_OK) {
            error = database_ != nullptr ? sqlite3_errmsg(database_) : "Could not open SQLite state.";
            return false;
        }
        sqlite3_busy_timeout(database_, 5000);
        return ensurePersistentSchema(database_, error);
    }

    sqlite3* get() const noexcept { return database_; }

private:
    sqlite3* database_ = nullptr;
};

std::string folderPrefixKey(const std::filesystem::path& input) {
    std::error_code error;
    std::filesystem::path folder = std::filesystem::absolute(input, error);
    if (error) {
        folder = input;
    }
    folder = folder.lexically_normal();
    std::string key = pathBytes(folder);
#ifdef _WIN32
    std::transform(
        key.begin(), key.end(), key.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    constexpr char separator = '\\';
#else
    constexpr char separator = '/';
#endif
    if (!key.empty() && key.back() != '/' && key.back() != '\\') {
        key.push_back(separator);
    }
    return key;
}

std::string partialFileHash(
    const std::filesystem::path& path,
    std::uint64_t fileSize,
    std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Could not open the demo while calculating its partial hash.";
        return {};
    }

    std::uint64_t first = 1469598103934665603ULL;
    std::uint64_t second = 1099511628211ULL ^ fileSize;
    auto mix = [&](std::uint8_t byte) {
        first ^= byte;
        first *= 1099511628211ULL;
        second += byte + 0x9e3779b97f4a7c15ULL + (second << 6U) + (second >> 2U);
        second ^= second >> 29U;
        second *= 0xbf58476d1ce4e5b9ULL;
    };
    for (int shift = 0; shift < 64; shift += 8) {
        mix(static_cast<std::uint8_t>((fileSize >> shift) & 0xffU));
    }

    std::set<std::uint64_t> offsets;
    offsets.insert(0);
    if (fileSize > kHashBlockBytes) {
        offsets.insert(fileSize / 2U > kHashBlockBytes / 2U
                           ? fileSize / 2U - kHashBlockBytes / 2U
                           : 0U);
        offsets.insert(fileSize - kHashBlockBytes);
    }

    std::vector<char> buffer(kHashBlockBytes);
    for (const std::uint64_t offset : offsets) {
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream) {
            error = "Could not seek in the demo while calculating its partial hash.";
            return {};
        }
        const std::uint64_t remaining = fileSize > offset ? fileSize - offset : 0U;
        const std::size_t requested = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, buffer.size()));
        if (requested != 0U) {
            stream.read(buffer.data(), static_cast<std::streamsize>(requested));
            if (stream.gcount() != static_cast<std::streamsize>(requested)) {
                error = "Could not read the demo while calculating its partial hash.";
                return {};
            }
        }
        for (int shift = 0; shift < 64; shift += 8) {
            mix(static_cast<std::uint8_t>((offset >> shift) & 0xffU));
        }
        for (std::size_t index = 0; index < requested; ++index) {
            mix(static_cast<std::uint8_t>(buffer[index]));
        }
    }

    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << first
         << std::setw(16) << second;
    return text.str();
}

std::string dateFromFilenameOrTimestamp(
    const std::filesystem::path& path,
    const std::filesystem::file_time_type& modified) {
    const std::string filename = pathBytes(path.filename());
    for (std::size_t index = 0; index + 10U <= filename.size(); ++index) {
        const auto digit = [&](std::size_t position) {
            return std::isdigit(static_cast<unsigned char>(filename[index + position])) != 0;
        };
        if (digit(0) && digit(1) && digit(2) && digit(3) && filename[index + 4] == '-' &&
            digit(5) && digit(6) && filename[index + 7] == '-' && digit(8) && digit(9)) {
            return filename.substr(index, 10);
        }
    }

    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        modified - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(systemTime);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &timestamp);
#else
    localtime_r(&timestamp, &local);
#endif
    std::ostringstream text;
    text << std::put_time(&local, "%Y-%m-%d");
    return text.str();
}

IndexedDemoSummary readSummary(sqlite3_stmt* statement) {
    IndexedDemoSummary summary;
    summary.id = sqlite3_column_int64(statement, 0);
    summary.path = pathFromBytes(sqliteText(statement, 1));
    summary.fileName = sqliteText(statement, 2);
    summary.recordedDate = sqliteText(statement, 3);
    summary.mapName = sqliteText(statement, 4);
    summary.gameName = sqliteText(statement, 5);
    summary.modVersion = sqliteText(statement, 6);
    summary.povName = sqliteText(statement, 7);
    summary.povClientNum = sqlite3_column_int(statement, 8);
    summary.firstServerTimeMs = static_cast<std::int32_t>(sqlite3_column_int(statement, 9));
    summary.lastServerTimeMs = static_cast<std::int32_t>(sqlite3_column_int(statement, 10));
    summary.fileSize = static_cast<std::uint64_t>(sqlite3_column_int64(statement, 11));
    summary.modifiedTicks = sqlite3_column_int64(statement, 12);
    summary.partialHash = sqliteText(statement, 13);
    summary.playerCount = static_cast<std::size_t>(sqlite3_column_int64(statement, 14));
    summary.eventCount = static_cast<std::size_t>(sqlite3_column_int64(statement, 15));
    summary.duplicateCount = static_cast<std::size_t>(sqlite3_column_int64(statement, 16));
    summary.parserRevision = sqlite3_column_int(statement, 17);
    return summary;
}

constexpr const char* kSummaryColumns =
    "d.id,d.path,d.file_name,d.recorded_date,d.map_name,d.game_name,d.mod_version,"
    "d.pov_name,d.pov_client_num,d.first_server_time_ms,d.last_server_time_ms,"
    "d.file_size,d.modified_ticks,d.partial_hash,d.player_count,d.event_count,"
    "(SELECT COUNT(*) FROM demos AS duplicate WHERE duplicate.file_size=d.file_size "
    "AND duplicate.partial_hash=d.partial_hash AND d.partial_hash<>''),d.parser_revision";

std::string escapedLikePattern(const std::string& query) {
    std::string pattern = "%";
    for (const char character : query) {
        if (character == '%' || character == '_' || character == '\\') {
            pattern.push_back('\\');
        }
        pattern.push_back(character);
    }
    pattern.push_back('%');
    return pattern;
}

} // namespace

SqliteDemoIndex::~SqliteDemoIndex() {
    close();
}

bool SqliteDemoIndex::open(const std::filesystem::path& path, std::string& error) {
    close();
    error.clear();
    std::error_code directoryError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), directoryError);
    }
    if (directoryError) {
        error = "Could not create the application data folder: " + directoryError.message();
        return false;
    }
    const std::string bytes = pathBytes(path);
    if (sqlite3_open_v2(
            bytes.c_str(),
            &database_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr) != SQLITE_OK) {
        error = database_ != nullptr ? sqlite3_errmsg(database_) : "Could not open SQLite index.";
        close();
        return false;
    }
    path_ = path;
    sqlite3_busy_timeout(database_, 5000);
    const char* schema =
        "PRAGMA foreign_keys=ON;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS demos("
        "id INTEGER PRIMARY KEY,"
        "path TEXT NOT NULL,"
        "path_key TEXT NOT NULL UNIQUE,"
        "file_name TEXT NOT NULL,"
        "recorded_date TEXT NOT NULL,"
        "file_size INTEGER NOT NULL,"
        "modified_ticks INTEGER NOT NULL,"
        "partial_hash TEXT NOT NULL,"
        "map_name TEXT NOT NULL,"
        "game_name TEXT NOT NULL,"
        "mod_version TEXT NOT NULL,"
        "pov_name TEXT NOT NULL,"
        "pov_client_num INTEGER NOT NULL,"
        "first_server_time_ms INTEGER NOT NULL,"
        "last_server_time_ms INTEGER NOT NULL,"
        "level_start_time_ms INTEGER NOT NULL,"
        "time_limit_minutes REAL NOT NULL,"
        "player_count INTEGER NOT NULL,"
        "event_count INTEGER NOT NULL,"
        "parser_revision INTEGER NOT NULL DEFAULT 0,"
        "indexed_unix INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS demos_date_idx ON demos(recorded_date);"
        "CREATE INDEX IF NOT EXISTS demos_map_idx ON demos(map_name);"
        "CREATE INDEX IF NOT EXISTS demos_hash_idx ON demos(file_size,partial_hash);"
        "CREATE TABLE IF NOT EXISTS players("
        "demo_id INTEGER NOT NULL REFERENCES demos(id) ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL,"
        "client_num INTEGER NOT NULL,"
        "session_id INTEGER NOT NULL,"
        "name TEXT NOT NULL,"
        "clean_name TEXT NOT NULL,"
        "team INTEGER NOT NULL,"
        "PRIMARY KEY(demo_id,ordinal)"
        ");"
        "CREATE INDEX IF NOT EXISTS players_name_idx ON players(clean_name);"
        "CREATE TABLE IF NOT EXISTS kills("
        "demo_id INTEGER NOT NULL REFERENCES demos(id) ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL,"
        "server_time_ms INTEGER NOT NULL,"
        "demo_time_ms INTEGER NOT NULL,"
        "attacker INTEGER NOT NULL,"
        "target INTEGER NOT NULL,"
        "attacker_session_id INTEGER NOT NULL,"
        "target_session_id INTEGER NOT NULL,"
        "attacker_team INTEGER NOT NULL,"
        "target_team INTEGER NOT NULL,"
        "weapon INTEGER NOT NULL,"
        "means_of_death INTEGER NOT NULL,"
        "team_kill INTEGER NOT NULL,"
        "suicide INTEGER NOT NULL,"
        "headshot INTEGER NOT NULL,"
        "match_phase INTEGER NOT NULL,"
        "attacker_name TEXT NOT NULL,"
        "target_name TEXT NOT NULL,"
        "match_elapsed_ms INTEGER NOT NULL,"
        "match_remaining_ms INTEGER NOT NULL,"
        "PRIMARY KEY(demo_id,ordinal)"
        ");"
        "CREATE INDEX IF NOT EXISTS kills_demo_time_idx ON kills(demo_id,demo_time_ms);"
        "CREATE TABLE IF NOT EXISTS headshot_hits("
        "demo_id INTEGER NOT NULL REFERENCES demos(id) ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL,"
        "server_time_ms INTEGER NOT NULL,"
        "demo_time_ms INTEGER NOT NULL,"
        "attacker INTEGER NOT NULL,"
        "target INTEGER NOT NULL,"
        "attacker_session_id INTEGER NOT NULL,"
        "target_session_id INTEGER NOT NULL,"
        "weapon INTEGER NOT NULL,"
        "match_phase INTEGER NOT NULL,"
        "attacker_name TEXT NOT NULL,"
        "target_name TEXT NOT NULL,"
        "PRIMARY KEY(demo_id,ordinal)"
        ");"
        "CREATE INDEX IF NOT EXISTS headshot_hits_demo_time_idx "
        "ON headshot_hits(demo_id,demo_time_ms);"
        "CREATE TABLE IF NOT EXISTS warnings("
        "demo_id INTEGER NOT NULL REFERENCES demos(id) ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL,"
        "message TEXT NOT NULL,"
        "PRIMARY KEY(demo_id,ordinal)"
        ");"
        ;
    if (!sqliteExec(database_, schema, error)) {
        close();
        return false;
    }
    if (!ensurePersistentSchema(database_, error)) {
        close();
        return false;
    }

    // 1.7.0 used this same database path but did not store per-hit data. Add a
    // parser revision marker in place, preserve the existing index, and make
    // unchanged legacy rows stale so the next Update index pass reparses them.
    bool hasParserRevision = false;
    {
        SqliteStatement columns(database_, "PRAGMA table_info(demos);", error);
        if (!columns) {
            close();
            return false;
        }
        while (sqlite3_step(columns.get()) == SQLITE_ROW) {
            if (sqliteText(columns.get(), 1) == "parser_revision") {
                hasParserRevision = true;
                break;
            }
        }
    }
    if (!hasParserRevision &&
        !sqliteExec(
            database_,
            "ALTER TABLE demos ADD COLUMN parser_revision INTEGER NOT NULL DEFAULT 0;",
            error)) {
        close();
        return false;
    }
    if (!sqliteExec(database_, "PRAGMA user_version=4;", error)) {
        close();
        return false;
    }
    return true;
}

void SqliteDemoIndex::close() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }
    path_.clear();
}

bool SqliteDemoIndex::isOpen() const noexcept {
    return database_ != nullptr;
}

std::optional<DemoInfo> SqliteDemoIndex::findFresh(
    const std::filesystem::path& path,
    std::string& error) const {
    error.clear();
    if (database_ == nullptr) {
        error = "The SQLite demo index is not open.";
        return std::nullopt;
    }
    DemoFileFingerprint current;
    if (!fingerprintFor(path, current)) {
        return std::nullopt;
    }
    SqliteStatement statement(
        database_,
        "SELECT id,file_size,modified_ticks,partial_hash,parser_revision "
        "FROM demos WHERE path_key=?1;",
        error);
    if (!statement || !bindText(statement.get(), 1, normalizedPathKey(path))) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return std::nullopt;
    }
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    if (sqlite3_column_int(statement.get(), 4) != kDemoIndexParserRevision) {
        return std::nullopt;
    }
    const std::uint64_t storedSize =
        static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 1));
    const std::int64_t storedModified = sqlite3_column_int64(statement.get(), 2);
    if (storedSize != current.size || storedModified != current.modifiedTicks) {
        return std::nullopt;
    }
    const std::string storedHash = sqliteText(statement.get(), 3);
    const std::string currentHash = partialFileHash(path, current.size, error);
    if (!error.empty() || currentHash != storedHash) {
        return std::nullopt;
    }
    return loadById(sqlite3_column_int64(statement.get(), 0), error);
}

std::optional<DemoInfo> SqliteDemoIndex::loadById(
    std::int64_t id,
    std::string& error) const {
    error.clear();
    if (database_ == nullptr) {
        error = "The SQLite demo index is not open.";
        return std::nullopt;
    }
    SqliteStatement demoStatement(
        database_,
        "SELECT path,map_name,game_name,mod_version,pov_name,pov_client_num,"
        "first_server_time_ms,last_server_time_ms,level_start_time_ms,time_limit_minutes "
        "FROM demos WHERE id=?1;",
        error);
    if (!demoStatement) return std::nullopt;
    sqlite3_bind_int64(demoStatement.get(), 1, id);
    if (sqlite3_step(demoStatement.get()) != SQLITE_ROW) {
        if (sqlite3_errcode(database_) != SQLITE_OK && sqlite3_errcode(database_) != SQLITE_DONE) {
            error = sqlite3_errmsg(database_);
        }
        return std::nullopt;
    }

    DemoInfo demo;
    demo.path = pathFromBytes(sqliteText(demoStatement.get(), 0));
    demo.mapName = sqliteText(demoStatement.get(), 1);
    demo.gameName = sqliteText(demoStatement.get(), 2);
    demo.modVersion = sqliteText(demoStatement.get(), 3);
    demo.povName = sqliteText(demoStatement.get(), 4);
    demo.povClientNum = sqlite3_column_int(demoStatement.get(), 5);
    demo.firstServerTimeMs = sqlite3_column_int(demoStatement.get(), 6);
    demo.lastServerTimeMs = sqlite3_column_int(demoStatement.get(), 7);
    demo.levelStartTimeMs = sqlite3_column_int(demoStatement.get(), 8);
    demo.timeLimitMinutes = sqlite3_column_double(demoStatement.get(), 9);

    SqliteStatement playerStatement(
        database_,
        "SELECT client_num,session_id,name,clean_name,team FROM players "
        "WHERE demo_id=?1 ORDER BY ordinal;",
        error);
    if (!playerStatement) return std::nullopt;
    sqlite3_bind_int64(playerStatement.get(), 1, id);
    while (sqlite3_step(playerStatement.get()) == SQLITE_ROW) {
        Player player;
        player.clientNum = sqlite3_column_int(playerStatement.get(), 0);
        player.sessionId = sqlite3_column_int(playerStatement.get(), 1);
        player.name = sqliteText(playerStatement.get(), 2);
        player.cleanName = sqliteText(playerStatement.get(), 3);
        player.team = sqlite3_column_int(playerStatement.get(), 4);
        demo.players.push_back(std::move(player));
    }
    if (sqlite3_errcode(database_) != SQLITE_OK && sqlite3_errcode(database_) != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return std::nullopt;
    }

    SqliteStatement killStatement(
        database_,
        "SELECT server_time_ms,demo_time_ms,attacker,target,attacker_session_id,"
        "target_session_id,attacker_team,target_team,weapon,means_of_death,team_kill,"
        "suicide,headshot,match_phase,attacker_name,target_name,match_elapsed_ms,match_remaining_ms "
        "FROM kills WHERE demo_id=?1 ORDER BY ordinal;",
        error);
    if (!killStatement) return std::nullopt;
    sqlite3_bind_int64(killStatement.get(), 1, id);
    while (sqlite3_step(killStatement.get()) == SQLITE_ROW) {
        KillEvent kill;
        kill.serverTimeMs = sqlite3_column_int(killStatement.get(), 0);
        kill.demoTimeMs = sqlite3_column_int(killStatement.get(), 1);
        kill.attacker = sqlite3_column_int(killStatement.get(), 2);
        kill.target = sqlite3_column_int(killStatement.get(), 3);
        kill.attackerSessionId = sqlite3_column_int(killStatement.get(), 4);
        kill.targetSessionId = sqlite3_column_int(killStatement.get(), 5);
        kill.attackerTeam = sqlite3_column_int(killStatement.get(), 6);
        kill.targetTeam = sqlite3_column_int(killStatement.get(), 7);
        kill.weapon = sqlite3_column_int(killStatement.get(), 8);
        kill.meansOfDeath = sqlite3_column_int(killStatement.get(), 9);
        kill.teamKill = sqlite3_column_int(killStatement.get(), 10) != 0;
        kill.suicide = sqlite3_column_int(killStatement.get(), 11) != 0;
        kill.headshot = sqlite3_column_int(killStatement.get(), 12) != 0;
        const int phase = sqlite3_column_int(killStatement.get(), 13);
        kill.matchPhase = phase >= static_cast<int>(MatchPhase::Unknown) &&
                                  phase <= static_cast<int>(MatchPhase::Intermission)
                              ? static_cast<MatchPhase>(phase)
                              : MatchPhase::Unknown;
        kill.attackerName = sqliteText(killStatement.get(), 14);
        kill.targetName = sqliteText(killStatement.get(), 15);
        kill.matchElapsedMs = sqlite3_column_int(killStatement.get(), 16);
        kill.matchRemainingMs = sqlite3_column_int(killStatement.get(), 17);
        demo.kills.push_back(std::move(kill));
    }
    if (sqlite3_errcode(database_) != SQLITE_OK && sqlite3_errcode(database_) != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return std::nullopt;
    }

    SqliteStatement hitStatement(
        database_,
        "SELECT server_time_ms,demo_time_ms,attacker,target,attacker_session_id,"
        "target_session_id,weapon,match_phase,attacker_name,target_name "
        "FROM headshot_hits WHERE demo_id=?1 ORDER BY ordinal;",
        error);
    if (!hitStatement) return std::nullopt;
    sqlite3_bind_int64(hitStatement.get(), 1, id);
    while (sqlite3_step(hitStatement.get()) == SQLITE_ROW) {
        HitEvent hit;
        hit.serverTimeMs = sqlite3_column_int(hitStatement.get(), 0);
        hit.demoTimeMs = sqlite3_column_int(hitStatement.get(), 1);
        hit.attacker = sqlite3_column_int(hitStatement.get(), 2);
        hit.target = sqlite3_column_int(hitStatement.get(), 3);
        hit.attackerSessionId = sqlite3_column_int(hitStatement.get(), 4);
        hit.targetSessionId = sqlite3_column_int(hitStatement.get(), 5);
        hit.weapon = sqlite3_column_int(hitStatement.get(), 6);
        const int phase = sqlite3_column_int(hitStatement.get(), 7);
        hit.matchPhase = phase >= static_cast<int>(MatchPhase::Unknown) &&
                                 phase <= static_cast<int>(MatchPhase::Intermission)
                             ? static_cast<MatchPhase>(phase)
                             : MatchPhase::Unknown;
        hit.attackerName = sqliteText(hitStatement.get(), 8);
        hit.targetName = sqliteText(hitStatement.get(), 9);
        hit.headshot = true;
        demo.hits.push_back(std::move(hit));
    }
    if (sqlite3_errcode(database_) != SQLITE_OK && sqlite3_errcode(database_) != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return std::nullopt;
    }

    SqliteStatement warningStatement(
        database_,
        "SELECT message FROM warnings WHERE demo_id=?1 ORDER BY ordinal;",
        error);
    if (!warningStatement) return std::nullopt;
    sqlite3_bind_int64(warningStatement.get(), 1, id);
    while (sqlite3_step(warningStatement.get()) == SQLITE_ROW) {
        demo.warnings.push_back(sqliteText(warningStatement.get(), 0));
    }
    return demo;
}

bool SqliteDemoIndex::upsert(
    const std::filesystem::path& path,
    const DemoInfo& demo,
    std::string& error) {
    error.clear();
    if (database_ == nullptr) {
        error = "The SQLite demo index is not open.";
        return false;
    }
    DemoFileFingerprint fingerprint;
    if (!fingerprintFor(path, fingerprint) ||
        fingerprint.size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        error = "Could not fingerprint the demo file.";
        return false;
    }
    std::error_code modifiedError;
    const auto modified = std::filesystem::last_write_time(path, modifiedError);
    if (modifiedError) {
        error = "Could not read the demo modification time.";
        return false;
    }
    const std::string hash = partialFileHash(path, fingerprint.size, error);
    if (hash.empty() && !error.empty()) {
        return false;
    }

    if (!sqliteExec(database_, "BEGIN IMMEDIATE;", error)) return false;
    bool committed = false;
    auto rollback = [&]() {
        if (!committed) {
            std::string ignored;
            sqliteExec(database_, "ROLLBACK;", ignored);
        }
    };

    SqliteStatement demoStatement(
        database_,
        "INSERT INTO demos(path,path_key,file_name,recorded_date,file_size,modified_ticks,"
        "partial_hash,map_name,game_name,mod_version,pov_name,pov_client_num,"
        "first_server_time_ms,last_server_time_ms,level_start_time_ms,time_limit_minutes,"
        "player_count,event_count,parser_revision,indexed_unix) VALUES("
        "?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20) "
        "ON CONFLICT(path_key) DO UPDATE SET path=excluded.path,file_name=excluded.file_name,"
        "recorded_date=excluded.recorded_date,file_size=excluded.file_size,"
        "modified_ticks=excluded.modified_ticks,partial_hash=excluded.partial_hash,"
        "map_name=excluded.map_name,game_name=excluded.game_name,mod_version=excluded.mod_version,"
        "pov_name=excluded.pov_name,pov_client_num=excluded.pov_client_num,"
        "first_server_time_ms=excluded.first_server_time_ms,"
        "last_server_time_ms=excluded.last_server_time_ms,"
        "level_start_time_ms=excluded.level_start_time_ms,"
        "time_limit_minutes=excluded.time_limit_minutes,player_count=excluded.player_count,"
        "event_count=excluded.event_count,parser_revision=excluded.parser_revision,"
        "indexed_unix=excluded.indexed_unix;",
        error);
    if (!demoStatement) {
        rollback();
        return false;
    }
    std::error_code absoluteError;
    std::filesystem::path absolutePath = std::filesystem::absolute(path, absoluteError);
    if (absoluteError) absolutePath = path;
    absolutePath = absolutePath.lexically_normal();
    const std::string absoluteBytes = pathBytes(absolutePath);
    const std::time_t now = std::time(nullptr);
    const bool bound =
        bindText(demoStatement.get(), 1, absoluteBytes) &&
        bindText(demoStatement.get(), 2, normalizedPathKey(absolutePath)) &&
        bindText(demoStatement.get(), 3, pathBytes(absolutePath.filename())) &&
        bindText(demoStatement.get(), 4, dateFromFilenameOrTimestamp(absolutePath, modified)) &&
        sqlite3_bind_int64(demoStatement.get(), 5, static_cast<sqlite3_int64>(fingerprint.size)) == SQLITE_OK &&
        sqlite3_bind_int64(demoStatement.get(), 6, fingerprint.modifiedTicks) == SQLITE_OK &&
        bindText(demoStatement.get(), 7, hash) && bindText(demoStatement.get(), 8, demo.mapName) &&
        bindText(demoStatement.get(), 9, demo.gameName) && bindText(demoStatement.get(), 10, demo.modVersion) &&
        bindText(demoStatement.get(), 11, demo.povName) &&
        sqlite3_bind_int(demoStatement.get(), 12, demo.povClientNum) == SQLITE_OK &&
        sqlite3_bind_int(demoStatement.get(), 13, demo.firstServerTimeMs) == SQLITE_OK &&
        sqlite3_bind_int(demoStatement.get(), 14, demo.lastServerTimeMs) == SQLITE_OK &&
        sqlite3_bind_int(demoStatement.get(), 15, demo.levelStartTimeMs) == SQLITE_OK &&
        sqlite3_bind_double(demoStatement.get(), 16, demo.timeLimitMinutes) == SQLITE_OK &&
        sqlite3_bind_int64(demoStatement.get(), 17, static_cast<sqlite3_int64>(demo.players.size())) == SQLITE_OK &&
        sqlite3_bind_int64(demoStatement.get(), 18, static_cast<sqlite3_int64>(demo.kills.size())) == SQLITE_OK &&
        sqlite3_bind_int(demoStatement.get(), 19, kDemoIndexParserRevision) == SQLITE_OK &&
        sqlite3_bind_int64(demoStatement.get(), 20, static_cast<sqlite3_int64>(now)) == SQLITE_OK;
    if (!bound || !sqliteDone(demoStatement, error)) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        rollback();
        return false;
    }

    SqliteStatement idStatement(
        database_, "SELECT id FROM demos WHERE path_key=?1;", error);
    if (!idStatement || !bindText(idStatement.get(), 1, normalizedPathKey(absolutePath)) ||
        sqlite3_step(idStatement.get()) != SQLITE_ROW) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        rollback();
        return false;
    }
    const sqlite3_int64 demoId = sqlite3_column_int64(idStatement.get(), 0);

    for (const char* table : {"players", "kills", "headshot_hits", "warnings"}) {
        const std::string sql = std::string("DELETE FROM ") + table + " WHERE demo_id=?1;";
        SqliteStatement deletion(database_, sql.c_str(), error);
        if (!deletion) {
            rollback();
            return false;
        }
        sqlite3_bind_int64(deletion.get(), 1, demoId);
        if (!sqliteDone(deletion, error)) {
            rollback();
            return false;
        }
    }

    SqliteStatement playerInsert(
        database_,
        "INSERT INTO players(demo_id,ordinal,client_num,session_id,name,clean_name,team) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7);",
        error);
    if (!playerInsert) {
        rollback();
        return false;
    }
    for (std::size_t index = 0; index < demo.players.size(); ++index) {
        const Player& player = demo.players[index];
        sqlite3_reset(playerInsert.get());
        sqlite3_clear_bindings(playerInsert.get());
        const bool playerBound =
            sqlite3_bind_int64(playerInsert.get(), 1, demoId) == SQLITE_OK &&
            sqlite3_bind_int64(playerInsert.get(), 2, static_cast<sqlite3_int64>(index)) == SQLITE_OK &&
            sqlite3_bind_int(playerInsert.get(), 3, player.clientNum) == SQLITE_OK &&
            sqlite3_bind_int(playerInsert.get(), 4, player.sessionId) == SQLITE_OK &&
            bindText(playerInsert.get(), 5, player.name) &&
            bindText(playerInsert.get(), 6, player.cleanName) &&
            sqlite3_bind_int(playerInsert.get(), 7, player.team) == SQLITE_OK;
        if (!playerBound || sqlite3_step(playerInsert.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(database_);
            rollback();
            return false;
        }
    }

    SqliteStatement killInsert(
        database_,
        "INSERT INTO kills(demo_id,ordinal,server_time_ms,demo_time_ms,attacker,target,"
        "attacker_session_id,target_session_id,attacker_team,target_team,weapon,means_of_death,"
        "team_kill,suicide,headshot,match_phase,attacker_name,target_name,match_elapsed_ms,match_remaining_ms) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20);",
        error);
    if (!killInsert) {
        rollback();
        return false;
    }
    for (std::size_t index = 0; index < demo.kills.size(); ++index) {
        const KillEvent& kill = demo.kills[index];
        sqlite3_reset(killInsert.get());
        sqlite3_clear_bindings(killInsert.get());
        bool killBound =
            sqlite3_bind_int64(killInsert.get(), 1, demoId) == SQLITE_OK &&
            sqlite3_bind_int64(killInsert.get(), 2, static_cast<sqlite3_int64>(index)) == SQLITE_OK;
        const std::array<int, 14> integers{{
            kill.serverTimeMs, kill.demoTimeMs, kill.attacker, kill.target,
            kill.attackerSessionId, kill.targetSessionId, kill.attackerTeam,
            kill.targetTeam, kill.weapon, kill.meansOfDeath,
            kill.teamKill ? 1 : 0, kill.suicide ? 1 : 0, kill.headshot ? 1 : 0,
            static_cast<int>(kill.matchPhase),
        }};
        for (std::size_t integer = 0; integer < integers.size(); ++integer) {
            killBound = killBound &&
                        sqlite3_bind_int(
                            killInsert.get(),
                            static_cast<int>(integer) + 3,
                            integers[integer]) == SQLITE_OK;
        }
        killBound = killBound && bindText(killInsert.get(), 17, kill.attackerName) &&
                    bindText(killInsert.get(), 18, kill.targetName) &&
                    sqlite3_bind_int(killInsert.get(), 19, kill.matchElapsedMs) == SQLITE_OK &&
                    sqlite3_bind_int(killInsert.get(), 20, kill.matchRemainingMs) == SQLITE_OK;
        if (!killBound || sqlite3_step(killInsert.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(database_);
            rollback();
            return false;
        }
    }

    SqliteStatement hitInsert(
        database_,
        "INSERT INTO headshot_hits(demo_id,ordinal,server_time_ms,demo_time_ms,"
        "attacker,target,attacker_session_id,target_session_id,weapon,match_phase,"
        "attacker_name,target_name) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12);",
        error);
    if (!hitInsert) {
        rollback();
        return false;
    }
    for (std::size_t index = 0; index < demo.hits.size(); ++index) {
        const HitEvent& hit = demo.hits[index];
        sqlite3_reset(hitInsert.get());
        sqlite3_clear_bindings(hitInsert.get());
        const bool hitBound =
            sqlite3_bind_int64(hitInsert.get(), 1, demoId) == SQLITE_OK &&
            sqlite3_bind_int64(
                hitInsert.get(), 2, static_cast<sqlite3_int64>(index)) == SQLITE_OK &&
            sqlite3_bind_int(hitInsert.get(), 3, hit.serverTimeMs) == SQLITE_OK &&
            sqlite3_bind_int(hitInsert.get(), 4, hit.demoTimeMs) == SQLITE_OK &&
            sqlite3_bind_int(hitInsert.get(), 5, hit.attacker) == SQLITE_OK &&
            sqlite3_bind_int(hitInsert.get(), 6, hit.target) == SQLITE_OK &&
            sqlite3_bind_int(hitInsert.get(), 7, hit.attackerSessionId) == SQLITE_OK &&
            sqlite3_bind_int(hitInsert.get(), 8, hit.targetSessionId) == SQLITE_OK &&
            sqlite3_bind_int(hitInsert.get(), 9, hit.weapon) == SQLITE_OK &&
            sqlite3_bind_int(
                hitInsert.get(), 10, static_cast<int>(hit.matchPhase)) == SQLITE_OK &&
            bindText(hitInsert.get(), 11, hit.attackerName) &&
            bindText(hitInsert.get(), 12, hit.targetName);
        if (!hitBound || sqlite3_step(hitInsert.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(database_);
            rollback();
            return false;
        }
    }

    SqliteStatement warningInsert(
        database_,
        "INSERT INTO warnings(demo_id,ordinal,message) VALUES(?1,?2,?3);",
        error);
    if (!warningInsert) {
        rollback();
        return false;
    }
    for (std::size_t index = 0; index < demo.warnings.size(); ++index) {
        sqlite3_reset(warningInsert.get());
        sqlite3_clear_bindings(warningInsert.get());
        if (sqlite3_bind_int64(warningInsert.get(), 1, demoId) != SQLITE_OK ||
            sqlite3_bind_int64(warningInsert.get(), 2, static_cast<sqlite3_int64>(index)) != SQLITE_OK ||
            !bindText(warningInsert.get(), 3, demo.warnings[index]) ||
            sqlite3_step(warningInsert.get()) != SQLITE_DONE) {
            error = sqlite3_errmsg(database_);
            rollback();
            return false;
        }
    }

    if (!sqliteExec(database_, "COMMIT;", error)) {
        rollback();
        return false;
    }
    committed = true;
    return true;
}

bool SqliteDemoIndex::remove(const std::filesystem::path& path, std::string& error) {
    error.clear();
    if (database_ == nullptr) {
        error = "The SQLite demo index is not open.";
        return false;
    }
    SqliteStatement statement(database_, "DELETE FROM demos WHERE path_key=?1;", error);
    if (!statement || !bindText(statement.get(), 1, normalizedPathKey(path))) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return false;
    }
    return sqliteDone(statement, error);
}

bool SqliteDemoIndex::pruneMissingInFolder(
    const std::filesystem::path& folder,
    std::size_t& removed,
    std::string& error) {
    removed = 0;
    const auto summaries = listFolder(folder, error);
    if (!error.empty()) return false;
    for (const IndexedDemoSummary& summary : summaries) {
        std::error_code existsError;
        if (!std::filesystem::is_regular_file(summary.path, existsError) || existsError) {
            if (!remove(summary.path, error)) return false;
            ++removed;
        }
    }
    return true;
}

std::vector<IndexedDemoSummary> SqliteDemoIndex::listFolder(
    const std::filesystem::path& folder,
    std::string& error) const {
    error.clear();
    std::vector<IndexedDemoSummary> output;
    if (database_ == nullptr) {
        error = "The SQLite demo index is not open.";
        return output;
    }
    const std::string sql = std::string("SELECT ") + kSummaryColumns +
                            " FROM demos AS d WHERE instr(d.path_key,?1)=1 "
                            "ORDER BY d.recorded_date DESC,d.file_name COLLATE NOCASE;";
    SqliteStatement statement(database_, sql.c_str(), error);
    if (!statement || !bindText(statement.get(), 1, folderPrefixKey(folder))) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return output;
    }
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
        output.push_back(readSummary(statement.get()));
    }
    if (sqlite3_errcode(database_) != SQLITE_OK && sqlite3_errcode(database_) != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        output.clear();
    }
    return output;
}

std::vector<IndexedDemoSummary> SqliteDemoIndex::search(
    const std::string& query,
    DemoSearchField field,
    const std::optional<std::filesystem::path>& folder,
    bool duplicatesOnly,
    std::size_t limit,
    std::string& error) const {
    error.clear();
    std::vector<IndexedDemoSummary> output;
    if (database_ == nullptr) {
        error = "The SQLite demo index is not open.";
        return output;
    }
    std::string sql = std::string("SELECT ") + kSummaryColumns + " FROM demos AS d WHERE 1=1";
    int parameter = 1;
    int folderParameter = 0;
    int queryParameter = 0;
    if (folder.has_value()) {
        folderParameter = parameter++;
        sql += " AND instr(d.path_key,?" + std::to_string(folderParameter) + ")=1";
    }
    if (duplicatesOnly) {
        sql += " AND d.partial_hash<>'' AND (SELECT COUNT(*) FROM demos AS dupe "
               "WHERE dupe.file_size=d.file_size AND dupe.partial_hash=d.partial_hash)>1";
    }
    if (!query.empty()) {
        queryParameter = parameter++;
        const std::string marker = "?" + std::to_string(queryParameter);
        switch (field) {
            case DemoSearchField::Nickname:
                sql += " AND (d.pov_name LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE OR EXISTS("
                       "SELECT 1 FROM players AS p WHERE p.demo_id=d.id AND p.clean_name LIKE " +
                       marker + " ESCAPE '\\' COLLATE NOCASE))";
                break;
            case DemoSearchField::Map:
                sql += " AND d.map_name LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE";
                break;
            case DemoSearchField::Date:
                sql += " AND d.recorded_date LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE";
                break;
            case DemoSearchField::Filename:
                sql += " AND d.file_name LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE";
                break;
            case DemoSearchField::All:
                sql += " AND (d.file_name LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE OR "
                       "d.recorded_date LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE OR "
                       "d.map_name LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE OR "
                       "d.pov_name LIKE " + marker + " ESCAPE '\\' COLLATE NOCASE OR EXISTS("
                       "SELECT 1 FROM players AS p WHERE p.demo_id=d.id AND p.clean_name LIKE " +
                       marker + " ESCAPE '\\' COLLATE NOCASE))";
                break;
        }
    }
    const int limitParameter = parameter;
    sql += " ORDER BY d.recorded_date DESC,d.file_name COLLATE NOCASE LIMIT ?" +
           std::to_string(limitParameter) + ';';

    SqliteStatement statement(database_, sql.c_str(), error);
    if (!statement) return output;
    if (folderParameter != 0 &&
        !bindText(statement.get(), folderParameter, folderPrefixKey(*folder))) {
        error = sqlite3_errmsg(database_);
        return output;
    }
    if (queryParameter != 0 &&
        !bindText(statement.get(), queryParameter, escapedLikePattern(query))) {
        error = sqlite3_errmsg(database_);
        return output;
    }
    sqlite3_bind_int64(
        statement.get(),
        limitParameter,
        static_cast<sqlite3_int64>(std::min<std::size_t>(limit, 100000U)));
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
        output.push_back(readSummary(statement.get()));
    }
    if (sqlite3_errcode(database_) != SQLITE_OK && sqlite3_errcode(database_) != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        output.clear();
    }
    return output;
}

std::size_t SqliteDemoIndex::size(std::string& error) const {
    error.clear();
    if (database_ == nullptr) {
        error = "The SQLite demo index is not open.";
        return 0;
    }
    SqliteStatement statement(database_, "SELECT COUNT(*) FROM demos;", error);
    if (!statement || sqlite3_step(statement.get()) != SQLITE_ROW) {
        if (error.empty()) error = sqlite3_errmsg(database_);
        return 0;
    }
    return static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 0));
}

bool SqliteDemoIndex::importLegacyIndex(
    const std::filesystem::path& legacyPath,
    std::size_t& imported,
    std::string& error) {
    imported = 0;
    error.clear();
    std::error_code existsError;
    if (!std::filesystem::exists(legacyPath, existsError)) {
        return !existsError;
    }
    DemoIndex legacy;
    if (!legacy.load(legacyPath, error)) {
        return false;
    }
    for (const IndexedDemo& entry : legacy.allEntries()) {
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(entry.path, fileError) || fileError) {
            continue;
        }
        if (!upsert(entry.path, entry.demo, error)) {
            return false;
        }
        ++imported;
    }
    return true;
}

bool loadPersistentState(
    const std::filesystem::path& databasePath,
    PersistentStateValues& values,
    std::string& error) {
    values.clear();
    error.clear();
    ScopedSqliteDatabase database;
    if (!database.open(databasePath, error)) {
        return false;
    }
    SqliteStatement statement(
        database.get(),
        "SELECT state_key,state_value FROM app_state;",
        error);
    if (!statement) {
        return false;
    }
    for (;;) {
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            return true;
        }
        if (result != SQLITE_ROW) {
            error = sqlite3_errmsg(database.get());
            values.clear();
            return false;
        }
        values[sqliteText(statement.get(), 0)] = sqliteText(statement.get(), 1);
    }
}

bool savePersistentState(
    const std::filesystem::path& databasePath,
    const PersistentStateValues& values,
    std::string& error) {
    error.clear();
    ScopedSqliteDatabase database;
    if (!database.open(databasePath, error)) {
        return false;
    }
    if (!sqliteExec(database.get(), "BEGIN IMMEDIATE;", error)) {
        return false;
    }
    bool committed = false;
    auto rollback = [&]() {
        if (!committed) {
            std::string ignored;
            sqliteExec(database.get(), "ROLLBACK;", ignored);
        }
    };
    SqliteStatement statement(
        database.get(),
        "INSERT INTO app_state(state_key,state_value,updated_unix) "
        "VALUES(?1,?2,?3) ON CONFLICT(state_key) DO UPDATE SET "
        "state_value=excluded.state_value,updated_unix=excluded.updated_unix;",
        error);
    if (!statement) {
        rollback();
        return false;
    }
    const auto now = static_cast<sqlite3_int64>(std::time(nullptr));
    for (const auto& [key, value] : values) {
        sqlite3_reset(statement.get());
        sqlite3_clear_bindings(statement.get());
        if (!bindText(statement.get(), 1, key) ||
            !bindText(statement.get(), 2, value) ||
            sqlite3_bind_int64(statement.get(), 3, now) != SQLITE_OK ||
            !sqliteDone(statement, error)) {
            rollback();
            return false;
        }
    }
    if (!sqliteExec(database.get(), "COMMIT;", error)) {
        rollback();
        return false;
    }
    committed = true;
    return true;
}

bool loadPersistentQueue(
    const std::filesystem::path& databasePath,
    const std::string& queueName,
    std::vector<PersistentQueueJob>& jobs,
    std::string& error) {
    jobs.clear();
    error.clear();
    ScopedSqliteDatabase database;
    if (!database.open(databasePath, error)) {
        return false;
    }
    SqliteStatement statement(
        database.get(),
        "SELECT position,job_id,demo_path,label,action_start_ms,action_end_ms,"
        "job_status,detail,output_path,log_path FROM queue_jobs "
        "WHERE queue_name=?1 ORDER BY position;",
        error);
    if (!statement || !bindText(statement.get(), 1, queueName)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database.get());
        }
        return false;
    }
    for (;;) {
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            return true;
        }
        if (result != SQLITE_ROW) {
            error = sqlite3_errmsg(database.get());
            jobs.clear();
            return false;
        }
        PersistentQueueJob job;
        job.position = sqlite3_column_int(statement.get(), 0);
        job.id = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 1));
        job.demoPath = pathFromBytes(sqliteText(statement.get(), 2));
        job.label = wideFromBytes(sqliteText(statement.get(), 3));
        job.actionStartMs = sqlite3_column_int(statement.get(), 4);
        job.actionEndMs = sqlite3_column_int(statement.get(), 5);
        job.status = sqlite3_column_int(statement.get(), 6);
        job.detail = wideFromBytes(sqliteText(statement.get(), 7));
        job.outputPath = pathFromBytes(sqliteText(statement.get(), 8));
        job.logPath = pathFromBytes(sqliteText(statement.get(), 9));
        jobs.push_back(std::move(job));
    }
}

bool savePersistentQueue(
    const std::filesystem::path& databasePath,
    const std::string& queueName,
    const std::vector<PersistentQueueJob>& jobs,
    std::string& error) {
    error.clear();
    ScopedSqliteDatabase database;
    if (!database.open(databasePath, error)) {
        return false;
    }
    if (!sqliteExec(database.get(), "BEGIN IMMEDIATE;", error)) {
        return false;
    }
    bool committed = false;
    auto rollback = [&]() {
        if (!committed) {
            std::string ignored;
            sqliteExec(database.get(), "ROLLBACK;", ignored);
        }
    };
    SqliteStatement deletion(
        database.get(), "DELETE FROM queue_jobs WHERE queue_name=?1;", error);
    if (!deletion || !bindText(deletion.get(), 1, queueName) || !sqliteDone(deletion, error)) {
        rollback();
        return false;
    }
    SqliteStatement insertion(
        database.get(),
        "INSERT INTO queue_jobs(queue_name,position,job_id,demo_path,label,"
        "action_start_ms,action_end_ms,job_status,detail,output_path,log_path) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11);",
        error);
    if (!insertion) {
        rollback();
        return false;
    }
    for (std::size_t index = 0; index < jobs.size(); ++index) {
        const PersistentQueueJob& job = jobs[index];
        sqlite3_reset(insertion.get());
        sqlite3_clear_bindings(insertion.get());
        const int position = job.position >= 0 ? job.position : static_cast<int>(index);
        const bool bound =
            bindText(insertion.get(), 1, queueName) &&
            sqlite3_bind_int(insertion.get(), 2, position) == SQLITE_OK &&
            sqlite3_bind_int64(
                insertion.get(), 3, static_cast<sqlite3_int64>(job.id)) == SQLITE_OK &&
            bindText(insertion.get(), 4, pathBytes(job.demoPath)) &&
            bindText(insertion.get(), 5, wideBytes(job.label)) &&
            sqlite3_bind_int(insertion.get(), 6, job.actionStartMs) == SQLITE_OK &&
            sqlite3_bind_int(insertion.get(), 7, job.actionEndMs) == SQLITE_OK &&
            sqlite3_bind_int(insertion.get(), 8, job.status) == SQLITE_OK &&
            bindText(insertion.get(), 9, wideBytes(job.detail)) &&
            bindText(insertion.get(), 10, pathBytes(job.outputPath)) &&
            bindText(insertion.get(), 11, pathBytes(job.logPath));
        if (!bound || !sqliteDone(insertion, error)) {
            if (error.empty()) {
                error = sqlite3_errmsg(database.get());
            }
            rollback();
            return false;
        }
    }
    if (!sqliteExec(database.get(), "COMMIT;", error)) {
        rollback();
        return false;
    }
    committed = true;
    return true;
}

bool sameHighlight(const HighlightItem& left, const HighlightItem& right) {
    return normalizedPathKey(left.demoPath) == normalizedPathKey(right.demoPath) &&
           left.startDemoTimeMs == right.startDemoTimeMs &&
           left.endDemoTimeMs == right.endDemoTimeMs;
}

bool loadHighlights(
    const std::filesystem::path& path,
    std::vector<HighlightItem>& highlights,
    std::string& error) {
    highlights.clear();
    error.clear();
    std::error_code existsError;
    if (!std::filesystem::exists(path, existsError)) {
        return !existsError;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Could not open the highlight basket.";
        return false;
    }
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!stream || !readScalar(stream, version) || version != kStorageVersion ||
        (magic != kHighlightMagic && magic != kLegacyHighlightMagic)) {
        error = "The highlight basket is invalid or belongs to an unsupported version.";
        return false;
    }
    const bool legacyFormat = magic == kLegacyHighlightMagic;
    std::uint32_t count = 0;
    if (!readScalar(stream, count) || count > kMaximumEntries) {
        error = "The highlight basket contains an invalid item count.";
        return false;
    }
    highlights.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        HighlightItem item;
        if (!(legacyFormat ? readLegacyHighlight(stream, item)
                           : readHighlight(stream, item))) {
            highlights.clear();
            error = "The highlight basket is truncated or corrupted.";
            return false;
        }
        highlights.push_back(std::move(item));
    }
    return true;
}

bool saveHighlights(
    const std::filesystem::path& path,
    const std::vector<HighlightItem>& highlights,
    std::string& error) {
    error.clear();
    if (highlights.size() > kMaximumEntries) {
        error = "The highlight basket is too large to save.";
        return false;
    }
    return saveAtomically(
        path,
        kHighlightMagic,
        [&highlights](std::ostream& stream) {
            const auto count = static_cast<std::uint32_t>(highlights.size());
            if (!writeScalar(stream, count)) {
                return false;
            }
            for (const HighlightItem& highlight : highlights) {
                if (!writeHighlight(stream, highlight)) {
                    return false;
                }
            }
            return true;
        },
        error);
}

} // namespace etlfrag
