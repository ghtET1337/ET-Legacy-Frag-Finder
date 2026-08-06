// SPDX-License-Identifier: GPL-3.0-or-later
#include "etl_demo_parser.hpp"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string jsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20) {
                    static constexpr char digits[] = "0123456789abcdef";
                    result += "\\u00";
                    result.push_back(digits[(character >> 4) & 0x0f]);
                    result.push_back(digits[character & 0x0f]);
                } else {
                    result.push_back(static_cast<char>(character));
                }
        }
    }
    return result;
}

void printJson(const etlfrag::DemoInfo& demo) {
    std::cout << "{\n"
              << "  \"map\": \"" << jsonEscape(demo.mapName) << "\",\n"
              << "  \"game\": \"" << jsonEscape(demo.gameName) << "\",\n"
              << "  \"modVersion\": \"" << jsonEscape(demo.modVersion) << "\",\n"
              << "  \"povClientNum\": " << demo.povClientNum << ",\n"
              << "  \"povName\": \"" << jsonEscape(demo.povName) << "\",\n"
              << "  \"firstServerTimeMs\": " << demo.firstServerTimeMs << ",\n"
              << "  \"lastServerTimeMs\": " << demo.lastServerTimeMs << ",\n"
              << "  \"warnings\": [";
    for (std::size_t i = 0; i < demo.warnings.size(); ++i) {
        std::cout << (i == 0 ? "\n" : ",\n")
                  << "    \"" << jsonEscape(demo.warnings[i]) << "\"";
    }
    std::cout << (demo.warnings.empty() ? "]" : "\n  ]")
              << ",\n  \"players\": [\n";
    for (std::size_t i = 0; i < demo.players.size(); ++i) {
        const auto& player = demo.players[i];
        std::cout << "    {\"clientNum\": " << player.clientNum
                  << ", \"sessionId\": " << player.sessionId
                  << ", \"name\": \"" << jsonEscape(player.name)
                  << "\", \"cleanName\": \"" << jsonEscape(player.cleanName)
                  << "\", \"team\": " << player.team << "}"
                  << (i + 1 == demo.players.size() ? "\n" : ",\n");
    }
    std::cout << "  ],\n  \"kills\": [\n";
    for (std::size_t i = 0; i < demo.kills.size(); ++i) {
        const auto& kill = demo.kills[i];
        std::cout << "    {\"serverTimeMs\": " << kill.serverTimeMs
                  << ", \"demoTimeMs\": " << kill.demoTimeMs
                  << ", \"attacker\": " << kill.attacker
                  << ", \"attackerSessionId\": " << kill.attackerSessionId
                  << ", \"attackerName\": \"" << jsonEscape(kill.attackerName)
                  << "\", \"target\": " << kill.target
                  << ", \"targetSessionId\": " << kill.targetSessionId
                  << ", \"targetName\": \"" << jsonEscape(kill.targetName)
                  << "\", \"weapon\": " << kill.weapon
                  << ", \"weaponName\": \"" << jsonEscape(etlfrag::weaponName(kill.weapon))
                  << "\", \"meansOfDeath\": " << kill.meansOfDeath
                  << ", \"matchElapsedMs\": " << kill.matchElapsedMs
                  << ", \"matchRemainingMs\": " << kill.matchRemainingMs
                  << ", \"phase\": \"" << jsonEscape(etlfrag::matchPhaseName(kill.matchPhase)) << "\""
                  << ", \"teamKill\": " << (kill.teamKill ? "true" : "false")
                  << ", \"suicide\": " << (kill.suicide ? "true" : "false")
                  << ", \"headshotKill\": " << (kill.headshot ? "true" : "false") << "}"
                  << (i + 1 == demo.kills.size() ? "\n" : ",\n");
    }
    std::cout << "  ],\n  \"headshotHits\": [\n";
    for (std::size_t i = 0; i < demo.hits.size(); ++i) {
        const auto& hit = demo.hits[i];
        std::cout << "    {\"serverTimeMs\": " << hit.serverTimeMs
                  << ", \"demoTimeMs\": " << hit.demoTimeMs
                  << ", \"attacker\": " << hit.attacker
                  << ", \"attackerSessionId\": " << hit.attackerSessionId
                  << ", \"attackerName\": \"" << jsonEscape(hit.attackerName)
                  << "\", \"target\": " << hit.target
                  << ", \"targetSessionId\": " << hit.targetSessionId
                  << ", \"targetName\": \"" << jsonEscape(hit.targetName)
                  << "\", \"weapon\": " << hit.weapon
                  << ", \"weaponName\": \"" << jsonEscape(etlfrag::weaponName(hit.weapon))
                  << "\", \"phase\": \"" << jsonEscape(etlfrag::matchPhaseName(hit.matchPhase))
                  << "\"}"
                  << (i + 1 == demo.hits.size() ? "\n" : ",\n");
    }
    std::cout << "  ]\n}\n";
}

void printText(const etlfrag::DemoInfo& demo) {
    std::cout << "Map: " << demo.mapName << "\n"
              << "Mod: " << demo.gameName << " " << demo.modVersion << "\n"
              << "POV: " << demo.povName << " (#" << demo.povClientNum << ")\n"
              << "Duration: " << etlfrag::formatDuration(demo.lastServerTimeMs - demo.firstServerTimeMs)
              << "\nPlayers: " << demo.players.size() << "\n"
              << "Events: " << demo.kills.size() << "\n"
              << "Headshot hits: " << demo.hits.size() << "\n";
    for (const std::string& warning : demo.warnings) {
        std::cout << "Warning: " << warning << "\n";
    }
    std::cout << "\n";
    for (const auto& kill : demo.kills) {
        std::cout << etlfrag::formatDuration(kill.demoTimeMs) << "  "
                  << kill.attackerName << " -> " << kill.targetName << "  ["
                  << etlfrag::weaponName(kill.weapon) << "]"
                  << " " << etlfrag::matchPhaseName(kill.matchPhase)
                  << (kill.teamKill ? " TEAMKILL" : "")
                  << (kill.suicide ? " SELF" : "")
                  << (kill.headshot ? " HEADSHOT KILL" : "") << '\n';
    }
}

void printRuns(const etlfrag::DemoInfo& demo, const etlfrag::RunFilter& filter) {
    const auto runs = etlfrag::findFragRuns(demo, filter);
    std::cout << "Map: " << demo.mapName << " | multi-kill sequences: " << runs.size() << "\n\n";
    for (const auto& run : runs) {
        const auto& first = demo.kills[run.killIndices.front()];
        std::cout << etlfrag::formatDuration(run.startDemoTimeMs) << "  ";
        if (first.matchRemainingMs >= 0) {
            std::cout << "clock " << etlfrag::formatDuration(first.matchRemainingMs, false) << "  ";
        }
        std::cout << run.attackerName << "  " << run.killIndices.size() << " kills, "
                  << run.headshotCount << " headshots  ("
                  << etlfrag::formatDuration(run.endDemoTimeMs - run.startDemoTimeMs) << ")\n";
        for (const std::size_t killIndex : run.killIndices) {
            const auto& kill = demo.kills[killIndex];
            std::cout << "  " << etlfrag::formatDuration(kill.demoTimeMs) << " -> "
                      << kill.targetName << " [" << etlfrag::weaponName(kill.weapon) << "]"
                      << (kill.headshot ? " HEADSHOT KILL" : "")
                      << (kill.teamKill ? " TEAMKILL" : "") << '\n';
        }
    }
}

void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  etl-frag-cli <file.dm_84> [--json]\n"
        << "  etl-frag-cli <file.dm_84> --runs [--player ID] [--session ID] [--min N] "
           "[--min-headshots N] [--gap SECONDS] [--weapon ID] [--teamkills] "
           "[--include-warmup] [--post-death-explosives SECONDS]\n";
}

int parseIntegerOption(
    const std::string& text,
    const std::string& option,
    int minimum,
    int maximum) {
    std::size_t consumed = 0;
    long long value = 0;
    try {
        value = std::stoll(text, &consumed, 10);
    } catch (...) {
        throw std::invalid_argument(option + " requires a whole number");
    }
    if (consumed != text.size() || value < minimum || value > maximum) {
        throw std::invalid_argument(
            option + " must be from " + std::to_string(minimum) + " to " +
            std::to_string(maximum));
    }
    return static_cast<int>(value);
}

std::int32_t parseSecondsOption(
    const std::string& text,
    const std::string& option,
    double minimum,
    double maximum) {
    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &consumed);
    } catch (...) {
        throw std::invalid_argument(option + " requires a number of seconds");
    }
    if (consumed != text.size() || !std::isfinite(value) || value < minimum || value > maximum) {
        throw std::invalid_argument(
            option + " must be a finite value from " + std::to_string(minimum) +
            " to " + std::to_string(maximum) + " seconds");
    }
    return static_cast<std::int32_t>(value * 1000.0 + 0.5);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return EXIT_FAILURE;
    }
    try {
        bool json = false;
        bool runs = false;
        etlfrag::RunFilter filter;
        for (int index = 2; index < argc; ++index) {
            const std::string option = argv[index];
            auto requireValue = [&]() -> std::string {
                if (++index >= argc) {
                    throw std::invalid_argument("Missing value after option " + option);
                }
                return argv[index];
            };
            if (option == "--json") {
                json = true;
            } else if (option == "--runs") {
                runs = true;
            } else if (option == "--player") {
                filter.playerClientNum =
                    parseIntegerOption(requireValue(), option, -1, 63);
            } else if (option == "--session") {
                filter.playerSessionId =
                    parseIntegerOption(requireValue(), option, -1, 1000000);
            } else if (option == "--min") {
                filter.minimumKills =
                    parseIntegerOption(requireValue(), option, 1, 99);
            } else if (option == "--min-headshots") {
                filter.minimumHeadshots =
                    parseIntegerOption(requireValue(), option, 0, 99);
            } else if (option == "--gap") {
                filter.maximumGapMs =
                    parseSecondsOption(requireValue(), option, 0.0, 3600.0);
            } else if (option == "--weapon") {
                filter.weapon =
                    parseIntegerOption(requireValue(), option, -1, 255);
            } else if (option == "--teamkills") {
                filter.includeTeamKills = true;
            } else if (option == "--include-warmup") {
                filter.includeWarmupKills = true;
            } else if (option == "--post-death-explosives") {
                filter.postDeathExplosiveWindowMs =
                    parseSecondsOption(requireValue(), option, 0.0, 600.0);
            } else if (option == "--help" || option == "-h") {
                printUsage();
                return EXIT_SUCCESS;
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }
        if (json && runs) {
            throw std::invalid_argument("--json and --runs cannot be used together");
        }
        const etlfrag::DemoInfo demo = etlfrag::DemoParser{}.parse(argv[1]);
        if (json) {
            printJson(demo);
        } else if (runs) {
            printRuns(demo, filter);
        } else {
            printText(demo);
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
