// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace etlfrag {

struct Player {
    int clientNum = -1;
    // A client slot may be reused after disconnect/reconnect. sessionId keeps
    // those occupants separate while clientNum remains the protocol slot.
    int sessionId = -1;
    std::string name;
    std::string cleanName;
    int team = 0;
};

enum class MatchPhase : int {
    Unknown = 0,
    Playing = 1,
    Warmup = 2,
    Intermission = 3,
};

struct KillEvent {
    std::int32_t serverTimeMs = 0;
    std::int32_t demoTimeMs = 0;
    int attacker = -1;
    int target = -1;
    int attackerSessionId = -1;
    int targetSessionId = -1;
    int attackerTeam = 0;
    int targetTeam = 0;
    int weapon = 0;
    int meansOfDeath = 0;
    bool teamKill = false;
    bool suicide = false;
    bool headshot = false;
    MatchPhase matchPhase = MatchPhase::Unknown;
    std::string attackerName;
    std::string targetName;
    std::int32_t matchElapsedMs = -1;
    std::int32_t matchRemainingMs = -1;
};

struct DemoInfo {
    std::filesystem::path path;
    std::string mapName;
    std::string gameName;
    std::string modVersion;
    std::string povName;
    int povClientNum = -1;
    std::int32_t firstServerTimeMs = -1;
    std::int32_t lastServerTimeMs = -1;
    std::int32_t levelStartTimeMs = -1;
    double timeLimitMinutes = 0.0;
    std::vector<Player> players;
    std::vector<KillEvent> kills;
    std::vector<std::string> warnings;
};

struct FragRun {
    int attacker = -1;
    int attackerSessionId = -1;
    std::string attackerName;
    std::vector<std::size_t> killIndices;
    std::int32_t startDemoTimeMs = 0;
    std::int32_t endDemoTimeMs = 0;
};

struct RunFilter {
    int playerClientNum = -1;
    // -1 selects every observed occupant of playerClientNum. A non-negative
    // value selects one concrete slot/name session.
    int playerSessionId = -1;
    int minimumKills = 2;
    std::int32_t maximumGapMs = 8000; // 0 disables the time-gap split.
    int weapon = -1;                  // -1 means every weapon.
    bool includeTeamKills = false;
    // Warmup is excluded by default. Unknown phases remain eligible so demos
    // from compatible third-party mods do not silently lose events.
    bool includeWarmupKills = false;
    // 0 preserves the normal rule that a death ends the current sequence.
    // A positive value lets delayed explosive kills extend the sequence for
    // this many milliseconds after the selected player dies.
    std::int32_t postDeathExplosiveWindowMs = 0;
};

class DemoParser {
public:
    DemoInfo parse(const std::filesystem::path& path) const;
};

std::vector<FragRun> findFragRuns(const DemoInfo& demo, const RunFilter& filter);

std::string stripEtColors(const std::string& value);
std::string formatDuration(std::int32_t milliseconds, bool withMilliseconds = true);
std::string weaponName(int weapon);
std::string meansOfDeathName(int meansOfDeath);
std::string teamName(int team);
MatchPhase matchPhaseFromGameState(int gameState);
std::string matchPhaseName(MatchPhase phase);

} // namespace etlfrag
