// SPDX-License-Identifier: GPL-3.0-or-later
#include "etl_demo_parser.hpp"
#include "app_storage.hpp"

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
    etlfrag::MatchPhase phase = etlfrag::MatchPhase::Playing) {
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
    event.matchPhase = phase;
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
    etlfrag::SqliteDemoIndex sqliteIndex;
    const std::filesystem::path sqlitePath = storageFolder / "demo-index.sqlite3";
    assert(sqliteIndex.open(sqlitePath, storageError));
    assert(sqliteIndex.upsert(demoPath, delayedExplosives, storageError));
    const auto freshSqliteDemo = sqliteIndex.findFresh(demoPath, storageError);
    assert(storageError.empty());
    assert(freshSqliteDemo.has_value());
    assert(freshSqliteDemo->players.front().sessionId == 0);
    assert(freshSqliteDemo->kills.front().matchPhase == etlfrag::MatchPhase::Playing);
    assert(freshSqliteDemo->warnings == delayedExplosives.warnings);
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

    etlfrag::HighlightItem savedHighlight;
    savedHighlight.demoPath = demoPath;
    savedHighlight.mapName = "storage_test";
    savedHighlight.povName = "Player";
    savedHighlight.startDemoTimeMs = 1000;
    savedHighlight.endDemoTimeMs = 2000;
    savedHighlight.matchRemainingMs = 599000;
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

    std::filesystem::remove_all(storageFolder);
    return 0;
}
