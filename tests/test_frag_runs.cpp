// SPDX-License-Identifier: GPL-3.0-or-later
#include "etl_demo_parser.hpp"
#include "app_storage.hpp"
#include "../third_party/sqlite/sqlite3.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

etlfrag::KillEvent kill(
    int time,
    int attacker,
    int target,
    int weapon,
    const char* attackerName,
    const char* targetName,
    bool teamKill = false,
    int attackerSessionId = -1,
    int targetSessionId = -1,
    etlfrag::MatchPhase phase = etlfrag::MatchPhase::Playing,
    bool headshot = false) {
    etlfrag::KillEvent event;
    event.serverTimeMs = time;
    event.demoTimeMs = time;
    event.attacker = attacker;
    event.target = target;
    event.attackerSessionId = attackerSessionId;
    event.targetSessionId = targetSessionId;
    event.weapon = weapon;
    event.attackerName = attackerName;
    event.targetName = targetName;
    event.teamKill = teamKill;
    event.suicide = attacker == target;
    event.headshot = headshot;
    event.matchPhase = phase;
    return event;
}

etlfrag::HitEvent headshot(
    int time,
    int attacker,
    int target,
    int weapon,
    int attackerSessionId,
    int targetSessionId) {
    etlfrag::HitEvent event;
    event.serverTimeMs = time;
    event.demoTimeMs = time;
    event.attacker = attacker;
    event.target = target;
    event.attackerSessionId = attackerSessionId;
    event.targetSessionId = targetSessionId;
    event.weapon = weapon;
    event.headshot = true;
    event.matchPhase = etlfrag::MatchPhase::Playing;
    return event;
}

} // namespace

int main() {
    etlfrag::DemoInfo demo;
    demo.players = {
        {1, 0, "^1Player", "Player", 1},
        {4, 1, "Enemy", "Enemy", 2},
    };
    demo.kills = {
        kill(1000, 1, 2, 3, "Player", "A", false, 0, 20),
        kill(2000, 1, 3, 8, "Player", "B", false, 0, 21),
        kill(3000, 4, 1, 8, "Enemy", "Player", false, 1, 0),
        kill(4000, 1, 5, 3, "Player", "C", false, 0, 22),
        kill(4500, 1, 6, 3, "Player", "Mate", true, 0, 23),
        kill(13000, 1, 7, 3, "Player", "D", false, 0, 24),
    };

    etlfrag::RunFilter filter;
    filter.playerClientNum = 1;
    filter.minimumKills = 2;
    filter.maximumGapMs = 8000;
    auto runs = etlfrag::findFragRuns(demo, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 2);
    assert(runs.front().startDemoTimeMs == 1000);
    assert(runs.front().endDemoTimeMs == 2000);

    filter.maximumGapMs = 0;
    runs = etlfrag::findFragRuns(demo, filter);
    assert(runs.size() == 2);
    assert(runs[0].killIndices.size() == 2);
    assert(runs[1].killIndices.size() == 2);

    filter.weapon = 3;
    runs = etlfrag::findFragRuns(demo, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 2);
    assert(runs.front().startDemoTimeMs == 4000);
    assert(runs.front().endDemoTimeMs == 13000);

    filter.includeTeamKills = true;
    runs = etlfrag::findFragRuns(demo, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 3);

    etlfrag::DemoInfo headshotAction;
    headshotAction.players = {{1, 0, "Player", "Player", 1}};
    headshotAction.kills = {
        // The previous death is inside the five-second playback lead-in.
        kill(9400, 5, 1, 3, "Enemy", "Player", false, 5, 0,
             etlfrag::MatchPhase::Playing, false),
        kill(10000, 1, 2, 3, "Player", "A", false, 0, 20,
             etlfrag::MatchPhase::Playing, true),
        kill(10200, 1, 3, 3, "Player", "B", false, 0, 21,
             etlfrag::MatchPhase::Playing, false),
        kill(10400, 1, 4, 3, "Player", "C", false, 0, 22,
             etlfrag::MatchPhase::Playing, true),
    };
    headshotAction.hits = {
        // Even though it is inside the lead-in, this hit belongs to the
        // previous life and must be excluded.
        headshot(9300, 1, 2, 3, 0, 20),
        // A hit leading directly to the first frag is part of the action.
        headshot(9500, 1, 2, 3, 0, 20),
        headshot(10000, 1, 2, 3, 0, 20),
        headshot(10100, 1, 3, 3, 0, 21),
        headshot(10200, 1, 3, 3, 0, 21),
        // The target survives and is not one of this action's victims.
        headshot(10300, 1, 9, 3, 0, 29),
        headshot(10400, 1, 4, 3, 0, 22),
    };
    filter = {};
    filter.playerClientNum = 1;
    filter.minimumKills = 3;
    filter.minimumHeadshots = 5;
    runs = etlfrag::findFragRuns(headshotAction, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 3);
    assert(runs.front().headshotCount == 5);
    filter.minimumHeadshots = 6;
    runs = etlfrag::findFragRuns(headshotAction, filter);
    assert(runs.empty());

    // Regression for 2026-08-01-000359-bremen_b3.dm_84. The first two
    // confirmed headshots precede the first obituary by 440 ms and 300 ms.
    etlfrag::DemoInfo reportedHeadshotAction;
    reportedHeadshotAction.players = {{11, 11, "GOAT/ght!", "GOAT/ght!", 1}};
    reportedHeadshotAction.kills = {
        kill(799760, 11, 4, 8, "GOAT/ght!", "o Hornet", false, 11, 4),
        kill(802100, 11, 7, 8, "GOAT/ght!", "o telminho", false, 11, 7),
        kill(806420, 11, 9, 7, "GOAT/ght!", "o fishy'", false, 11, 9),
        kill(809000, 11, 3, 7, "GOAT/ght!", "o Kimi", false, 11, 3),
    };
    reportedHeadshotAction.hits = {
        headshot(799320, 11, 4, 8, 11, 4),
        headshot(799460, 11, 4, 8, 11, 4),
        headshot(801660, 11, 7, 8, 11, 7),
        headshot(802100, 11, 7, 8, 11, 7),
        headshot(803360, 11, 9, 8, 11, 9),
        headshot(806140, 11, 9, 7, 11, 9),
        headshot(809000, 11, 3, 7, 11, 3),
    };
    filter = {};
    filter.playerClientNum = 11;
    filter.playerSessionId = 11;
    filter.minimumKills = 4;
    filter.minimumHeadshots = 7;
    filter.maximumGapMs = 18000;
    runs = etlfrag::findFragRuns(reportedHeadshotAction, filter);
    assert(runs.size() == 1);
    assert(runs.front().headshotCount == 7);
    filter.minimumHeadshots = 8;
    runs = etlfrag::findFragRuns(reportedHeadshotAction, filter);
    assert(runs.empty());

    etlfrag::DemoInfo simultaneous;
    simultaneous.players = {{3, 0, "Player", "Player", 2}};
    simultaneous.kills = {
        kill(346260, 3, 9, 53, "Player", "Baczo", false, 0, 20),
        kill(346260, 3, 3, 53, "Player", "Player", false, 0, 0),
        kill(346260, 3, 0, 53, "Player", "dooPPiE", false, 0, 21),
        kill(346260, 3, 7, 53, "Player", "skillz", false, 0, 22),
    };
    filter = {};
    filter.playerClientNum = 3;
    filter.minimumKills = 3;
    runs = etlfrag::findFragRuns(simultaneous, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 3);
    assert(runs.front().startDemoTimeMs == 346260);
    assert(runs.front().endDemoTimeMs == 346260);

    etlfrag::DemoInfo delayedExplosives;
    delayedExplosives.players = {
        {1, 0, "Player", "Player", 1},
        {4, 1, "Enemy", "Enemy", 2},
    };
    delayedExplosives.kills = {
        kill(1000, 1, 2, 26, "Player", "Mine A", false, 0, 20),
        kill(2000, 1, 3, 26, "Player", "Mine B", false, 0, 21),
        kill(2500, 4, 1, 3, "Enemy", "Player", false, 1, 0),
        kill(6500, 1, 5, 26, "Player", "Mine C", false, 0, 22),
        kill(7000, 1, 6, 26, "Player", "Mine D", false, 0, 23),
    };
    filter = {};
    filter.playerClientNum = 1;
    runs = etlfrag::findFragRuns(delayedExplosives, filter);
    assert(runs.size() == 2);
    assert(runs[0].killIndices.size() == 2);
    assert(runs[1].killIndices.size() == 2);

    filter.postDeathExplosiveWindowMs = 5000;
    runs = etlfrag::findFragRuns(delayedExplosives, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 4);

    filter.postDeathExplosiveWindowMs = 3000;
    runs = etlfrag::findFragRuns(delayedExplosives, filter);
    assert(runs.size() == 2);

    etlfrag::DemoInfo directFire;
    directFire.players = delayedExplosives.players;
    directFire.kills = {
        kill(1000, 1, 2, 3, "Player", "MP40 A", false, 0, 20),
        kill(2000, 1, 3, 3, "Player", "MP40 B", false, 0, 21),
        kill(2500, 4, 1, 3, "Enemy", "Player", false, 1, 0),
        kill(4000, 1, 5, 3, "Player", "MP40 C", false, 0, 22),
        kill(4500, 1, 6, 3, "Player", "MP40 D", false, 0, 23),
    };
    filter.postDeathExplosiveWindowMs = 8000;
    runs = etlfrag::findFragRuns(directFire, filter);
    assert(runs.size() == 2);
    assert(runs[0].killIndices.size() == 2);
    assert(runs[1].killIndices.size() == 2);

    etlfrag::DemoInfo reusedSlot;
    reusedSlot.players = {
        {1, 10, "^1Alpha", "Alpha", 1},
        {1, 11, "^2Bravo", "Bravo", 1},
    };
    reusedSlot.kills = {
        kill(1000, 1, 2, 3, "Alpha", "A", false, 10, 20),
        kill(1200, 1, 3, 3, "Alpha", "B", false, 10, 21),
        // No death event is required to keep a reused slot separate: the
        // session identity comes from the player config-string lifecycle.
        kill(2000, 1, 4, 3, "Bravo", "C", false, 11, 22),
        kill(2200, 1, 5, 3, "Bravo", "D", false, 11, 23),
    };
    filter = {};
    filter.playerClientNum = -1;
    runs = etlfrag::findFragRuns(reusedSlot, filter);
    assert(runs.size() == 2);
    assert(runs[0].attackerName == "Alpha");
    assert(runs[0].attackerSessionId == 10);
    assert(runs[1].attackerName == "Bravo");
    assert(runs[1].attackerSessionId == 11);
    filter.playerClientNum = 1;
    filter.playerSessionId = -1;
    runs = etlfrag::findFragRuns(reusedSlot, filter);
    assert(runs.size() == 2);
    assert(runs[0].attackerSessionId == 10);
    assert(runs[1].attackerSessionId == 11);
    filter.playerSessionId = 11;
    runs = etlfrag::findFragRuns(reusedSlot, filter);
    assert(runs.size() == 1);
    assert(runs.front().attackerName == "Bravo");

    etlfrag::DemoInfo warmupAndMatch;
    warmupAndMatch.players = {{1, 0, "Player", "Player", 1}};
    warmupAndMatch.kills = {
        kill(1000, 1, 2, 3, "Player", "Warmup A", false, 0, 20,
             etlfrag::MatchPhase::Warmup),
        kill(1200, 1, 3, 3, "Player", "Warmup B", false, 0, 21,
             etlfrag::MatchPhase::Warmup),
        kill(2000, 1, 4, 3, "Player", "Match A", false, 0, 22,
             etlfrag::MatchPhase::Playing),
        kill(2200, 1, 5, 3, "Player", "Match B", false, 0, 23,
             etlfrag::MatchPhase::Playing),
    };
    filter = {};
    filter.playerClientNum = 1;
    filter.maximumGapMs = 0;
    runs = etlfrag::findFragRuns(warmupAndMatch, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 2);
    assert(runs.front().startDemoTimeMs == 2000);
    filter.includeWarmupKills = true;
    runs = etlfrag::findFragRuns(warmupAndMatch, filter);
    assert(runs.size() == 2);
    assert(runs[0].killIndices.size() == 2);
    assert(runs[0].startDemoTimeMs == 1000);
    assert(runs[1].killIndices.size() == 2);
    assert(runs[1].startDemoTimeMs == 2000);

    etlfrag::DemoInfo unknownPhase;
    unknownPhase.players = warmupAndMatch.players;
    unknownPhase.kills = {
        kill(1000, 1, 2, 3, "Player", "Unknown A", false, 0, 20,
             etlfrag::MatchPhase::Unknown),
        kill(1200, 1, 3, 3, "Player", "Unknown B", false, 0, 21,
             etlfrag::MatchPhase::Unknown),
    };
    filter = {};
    filter.playerClientNum = 1;
    runs = etlfrag::findFragRuns(unknownPhase, filter);
    assert(runs.size() == 1);
    assert(runs.front().killIndices.size() == 2);
    assert(etlfrag::matchPhaseName(etlfrag::MatchPhase::Warmup) == "Warmup");
    assert(etlfrag::matchPhaseFromGameState(-1) == etlfrag::MatchPhase::Unknown);
    assert(etlfrag::matchPhaseFromGameState(0) == etlfrag::MatchPhase::Playing);
    assert(etlfrag::matchPhaseFromGameState(1) == etlfrag::MatchPhase::Warmup);
    assert(etlfrag::matchPhaseFromGameState(2) == etlfrag::MatchPhase::Warmup);
    assert(etlfrag::matchPhaseFromGameState(3) == etlfrag::MatchPhase::Intermission);
    assert(etlfrag::matchPhaseFromGameState(4) == etlfrag::MatchPhase::Warmup);
    assert(etlfrag::matchPhaseFromGameState(5) == etlfrag::MatchPhase::Warmup);

    assert(etlfrag::stripEtColors("^1Red^7 White") == "Red White");
    assert(etlfrag::formatDuration(291380) == "4:51.380");
    assert(etlfrag::formatDuration(624000, false) == "10:24");

    const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::filesystem::path storageFolder =
        std::filesystem::current_path() / "build" /
        ("etl-frag-storage-test-" + std::to_string(unique));
    std::filesystem::create_directories(storageFolder);

    const std::filesystem::path demoPath = storageFolder / "2026-08-03-sample.dm_84";
    {
        std::ofstream file(demoPath, std::ios::binary);
        file << "demo fingerprint";
    }
    delayedExplosives.path = demoPath;
    delayedExplosives.mapName = "storage_test";
    delayedExplosives.povName = "Player";
    delayedExplosives.povClientNum = 1;
    delayedExplosives.warnings = {
        "The demo ends with an incomplete final message; complete events were recovered."};

    etlfrag::DemoIndex index;
    assert(index.upsert(demoPath, delayedExplosives));
    assert(index.size() == 1);
    const std::filesystem::path indexPath = storageFolder / "demo-index.bin";
    std::string storageError;
    assert(index.save(indexPath, storageError));

    etlfrag::DemoIndex restoredIndex;
    assert(restoredIndex.load(indexPath, storageError));
    const etlfrag::DemoInfo* restoredDemo = restoredIndex.findFresh(demoPath);
    assert(restoredDemo != nullptr);
    assert(restoredDemo->mapName == "storage_test");
    assert(restoredDemo->kills.size() == delayedExplosives.kills.size());
    assert(restoredDemo->warnings == delayedExplosives.warnings);
    {
        std::ofstream file(demoPath, std::ios::binary | std::ios::app);
        file << " changed";
    }
    assert(restoredIndex.findFresh(demoPath) == nullptr);

    // Restore a stable file for the SQLite-backed production index tests.
    {
        std::ofstream file(demoPath, std::ios::binary | std::ios::trunc);
        file << "demo fingerprint";
    }
    delayedExplosives.path = demoPath;
    delayedExplosives.hits = {
        headshot(1000, 1, 2, 3, 0, 20),
        headshot(1800, 1, 3, 3, 0, 21),
    };

    // A 1.7.0 database already has the demos table but no parser_revision
    // column or headshot_hits table. Verify that opening it performs the
    // in-place migration and that newly reparsed per-hit data is usable.
    const std::filesystem::path migrationPath =
        storageFolder / "demo-index-v3-migration.sqlite3";
    sqlite3* legacyDatabase = nullptr;
    assert(sqlite3_open(migrationPath.string().c_str(), &legacyDatabase) == SQLITE_OK);
    const char* legacySchema =
        "CREATE TABLE demos("
        "id INTEGER PRIMARY KEY,path TEXT NOT NULL,path_key TEXT NOT NULL UNIQUE,"
        "file_name TEXT NOT NULL,recorded_date TEXT NOT NULL,file_size INTEGER NOT NULL,"
        "modified_ticks INTEGER NOT NULL,partial_hash TEXT NOT NULL,map_name TEXT NOT NULL,"
        "game_name TEXT NOT NULL,mod_version TEXT NOT NULL,pov_name TEXT NOT NULL,"
        "pov_client_num INTEGER NOT NULL,first_server_time_ms INTEGER NOT NULL,"
        "last_server_time_ms INTEGER NOT NULL,level_start_time_ms INTEGER NOT NULL,"
        "time_limit_minutes REAL NOT NULL,player_count INTEGER NOT NULL,"
        "event_count INTEGER NOT NULL,indexed_unix INTEGER NOT NULL);";
    char* legacyError = nullptr;
    assert(sqlite3_exec(legacyDatabase, legacySchema, nullptr, nullptr, &legacyError) == SQLITE_OK);
    sqlite3_free(legacyError);
    assert(sqlite3_close(legacyDatabase) == SQLITE_OK);
    etlfrag::SqliteDemoIndex migratedIndex;
    assert(migratedIndex.open(migrationPath, storageError));
    assert(migratedIndex.upsert(demoPath, delayedExplosives, storageError));
    const auto migratedDemo = migratedIndex.findFresh(demoPath, storageError);
    assert(storageError.empty());
    assert(migratedDemo.has_value());
    assert(migratedDemo->hits.size() == 2);
    migratedIndex.close();

    etlfrag::SqliteDemoIndex sqliteIndex;
    const std::filesystem::path sqlitePath = storageFolder / "demo-index.sqlite3";
    assert(sqliteIndex.open(sqlitePath, storageError));
    assert(sqliteIndex.upsert(demoPath, delayedExplosives, storageError));
    const auto freshSqliteDemo = sqliteIndex.findFresh(demoPath, storageError);
    assert(storageError.empty());
    assert(freshSqliteDemo.has_value());
    assert(freshSqliteDemo->players.front().sessionId == 0);
    assert(freshSqliteDemo->kills.front().matchPhase == etlfrag::MatchPhase::Playing);
    assert(freshSqliteDemo->hits.size() == 2);
    assert(freshSqliteDemo->hits.front().attackerSessionId == 0);
    assert(freshSqliteDemo->hits.front().targetSessionId == 20);
    assert(freshSqliteDemo->warnings == delayedExplosives.warnings);
    etlfrag::RunFilter cachedHeadshotFilter;
    cachedHeadshotFilter.playerClientNum = 1;
    cachedHeadshotFilter.playerSessionId = 0;
    cachedHeadshotFilter.minimumKills = 2;
    cachedHeadshotFilter.minimumHeadshots = 2;
    const auto cachedHeadshotRuns =
        etlfrag::findFragRuns(*freshSqliteDemo, cachedHeadshotFilter);
    assert(cachedHeadshotRuns.size() == 1);
    assert(cachedHeadshotRuns.front().headshotCount == 2);
    const auto originalSqliteTimestamp = std::filesystem::last_write_time(demoPath);
    {
        std::ofstream file(demoPath, std::ios::binary | std::ios::trunc);
        file << "changed content!"; // Same 16-byte size as "demo fingerprint".
    }
    std::filesystem::last_write_time(demoPath, originalSqliteTimestamp);
    assert(!sqliteIndex.findFresh(demoPath, storageError).has_value());
    assert(storageError.empty());
    {
        std::ofstream file(demoPath, std::ios::binary | std::ios::trunc);
        file << "demo fingerprint";
    }
    assert(sqliteIndex.upsert(demoPath, delayedExplosives, storageError));

    auto searchResults = sqliteIndex.search(
        "Player", etlfrag::DemoSearchField::Nickname, std::nullopt, false, 100, storageError);
    assert(storageError.empty() && searchResults.size() == 1);
    searchResults = sqliteIndex.search(
        "storage_test", etlfrag::DemoSearchField::Map, std::nullopt, false, 100, storageError);
    assert(storageError.empty() && searchResults.size() == 1);
    searchResults = sqliteIndex.search(
        "2026-08-03", etlfrag::DemoSearchField::Date, std::nullopt, false, 100, storageError);
    assert(storageError.empty() && searchResults.size() == 1);
    searchResults = sqliteIndex.search(
        "sample.dm_84", etlfrag::DemoSearchField::Filename, std::nullopt, false, 100, storageError);
    assert(storageError.empty() && searchResults.size() == 1);

    // Folder Scan reuses these same indexed fields, but must never leak matches
    // from another selected folder.
    const std::filesystem::path otherStorageFolder =
        std::filesystem::current_path() / "build" /
        ("etl-frag-storage-other-" + std::to_string(unique));
    std::filesystem::create_directories(otherStorageFolder);
    const std::filesystem::path otherDemoPath =
        otherStorageFolder / "2026-08-03-sample.dm_84";
    {
        std::ofstream file(otherDemoPath, std::ios::binary);
        file << "different demo fingerprint";
    }
    etlfrag::DemoInfo otherDemo = delayedExplosives;
    otherDemo.path = otherDemoPath;
    assert(sqliteIndex.upsert(otherDemoPath, otherDemo, storageError));
    for (const auto& [query, field] : {
             std::pair{"Player", etlfrag::DemoSearchField::Nickname},
             std::pair{"storage_test", etlfrag::DemoSearchField::Map},
             std::pair{"2026-08-03", etlfrag::DemoSearchField::Date},
             std::pair{"sample.dm_84", etlfrag::DemoSearchField::Filename}}) {
        searchResults = sqliteIndex.search(
            query, field, storageFolder, false, 100, storageError);
        assert(storageError.empty() && searchResults.size() == 1);
        assert(searchResults.front().path == demoPath);
    }
    assert(sqliteIndex.remove(otherDemoPath, storageError));
    std::filesystem::remove_all(otherStorageFolder);

    const std::filesystem::path duplicatePath =
        storageFolder / "2026-08-03-sample-copy.dm_84";
    std::filesystem::copy_file(demoPath, duplicatePath);
    etlfrag::DemoInfo duplicateDemo = delayedExplosives;
    duplicateDemo.path = duplicatePath;
    assert(sqliteIndex.upsert(duplicatePath, duplicateDemo, storageError));
    searchResults = sqliteIndex.search(
        "", etlfrag::DemoSearchField::All, storageFolder, true, 100, storageError);
    assert(storageError.empty() && searchResults.size() == 2);
    assert(searchResults[0].duplicateCount == 2);
    assert(searchResults[1].duplicateCount == 2);
    std::filesystem::remove(duplicatePath);
    std::size_t pruned = 0;
    assert(sqliteIndex.pruneMissingInFolder(storageFolder, pruned, storageError));
    assert(pruned == 1);
    assert(sqliteIndex.size(storageError) == 1);
    sqliteIndex.close();

    etlfrag::PersistentStateValues savedState{
        {"ui.active_tab", "2"},
        {"ui.last_demo", demoPath.string()},
        {"folder.query", "storage_test"},
        {"sort.folder_runs.column", "5"},
        {"sort.folder_runs.ascending", "0"},
    };
    assert(etlfrag::savePersistentState(sqlitePath, savedState, storageError));
    etlfrag::PersistentStateValues restoredState;
    assert(etlfrag::loadPersistentState(sqlitePath, restoredState, storageError));
    assert(restoredState == savedState);

    etlfrag::PersistentQueueJob interruptedJob;
    interruptedJob.id = 1001;
    interruptedJob.position = 0;
    interruptedJob.demoPath = demoPath;
    interruptedJob.label = L"Player 3K";
    interruptedJob.actionStartMs = 12000;
    interruptedJob.actionEndMs = 15000;
    interruptedJob.status = 2;
    interruptedJob.detail = L"Rendering";

    etlfrag::PersistentQueueJob completedJob = interruptedJob;
    completedJob.id = 1002;
    completedJob.position = 1;
    completedJob.status = 3;
    completedJob.detail = L"Completed";
    completedJob.outputPath = storageFolder / "finished.mp4";
    completedJob.logPath = storageFolder / "finished.log.txt";
    assert(etlfrag::savePersistentQueue(
        sqlitePath, "offline-render", {interruptedJob, completedJob}, storageError));

    etlfrag::PersistentQueueJob independentJob = interruptedJob;
    independentJob.id = 2001;
    independentJob.position = 0;
    assert(etlfrag::savePersistentQueue(
        sqlitePath, "independent-test-queue", {independentJob}, storageError));

    std::vector<etlfrag::PersistentQueueJob> restoredQueue;
    assert(etlfrag::loadPersistentQueue(
        sqlitePath, "offline-render", restoredQueue, storageError));
    assert(restoredQueue.size() == 2);
    assert(restoredQueue[0].id == interruptedJob.id);
    assert(restoredQueue[0].label == interruptedJob.label);
    assert(restoredQueue[0].status == interruptedJob.status);
    assert(restoredQueue[1].id == completedJob.id);
    assert(restoredQueue[1].outputPath == completedJob.outputPath);
    assert(restoredQueue[1].logPath == completedJob.logPath);

    assert(etlfrag::savePersistentQueue(
        sqlitePath, "offline-render", {}, storageError));
    assert(etlfrag::loadPersistentQueue(
        sqlitePath, "offline-render", restoredQueue, storageError));
    assert(restoredQueue.empty());
    assert(etlfrag::loadPersistentQueue(
        sqlitePath, "independent-test-queue", restoredQueue, storageError));
    assert(restoredQueue.size() == 1 && restoredQueue.front().id == independentJob.id);

    etlfrag::HighlightItem savedHighlight;
    savedHighlight.demoPath = demoPath;
    savedHighlight.mapName = "storage_test";
    savedHighlight.povName = "Player";
    savedHighlight.startDemoTimeMs = 1000;
    savedHighlight.endDemoTimeMs = 2000;
    savedHighlight.matchRemainingMs = 599000;
    savedHighlight.headshotCount = 3;
    savedHighlight.description = "Mine A (Landmine), Mine B (Landmine)";
    savedHighlight.events = {
        {1000, "Mine A", "Landmine", false, false},
        {2000, "Mine B", "Landmine", false, false},
    };
    const std::filesystem::path highlightsPath = storageFolder / "highlights.bin";
    assert(etlfrag::saveHighlights(highlightsPath, {savedHighlight}, storageError));
    std::vector<etlfrag::HighlightItem> restoredHighlights;
    assert(etlfrag::loadHighlights(highlightsPath, restoredHighlights, storageError));
    assert(restoredHighlights.size() == 1);
    assert(etlfrag::sameHighlight(savedHighlight, restoredHighlights.front()));
    assert(restoredHighlights.front().events.size() == 2);
    assert(restoredHighlights.front().headshotCount == 3);

    std::filesystem::remove_all(storageFolder);
    return 0;
}
