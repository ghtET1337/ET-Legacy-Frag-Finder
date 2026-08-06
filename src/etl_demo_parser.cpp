// SPDX-License-Identifier: GPL-3.0-or-later
#include "etl_demo_parser.hpp"

#include "idtech3_huffman.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace etlfrag {
namespace {

constexpr int kMaxClients = 64;
constexpr int kMaxEntities = 1024;
constexpr int kEntityNumberBits = 10;
constexpr int kEntityNone = kMaxEntities - 1;
constexpr int kEntityWorld = kMaxEntities - 2;
constexpr int kPacketBackup = 32;
constexpr int kMaxConfigStrings = 1024;
constexpr int kConfigServerInfo = 0;
constexpr int kConfigWarmup = 5;
constexpr int kConfigLevelStartTime = 11;
constexpr int kConfigIntermission = 12;
constexpr int kConfigWolfInfo = 21;
constexpr int kConfigPlayers = 689;
constexpr int kEtEvents = 62;
constexpr int kEvBullet = 61;
constexpr int kEvObituary = 68;
constexpr int kHitHeadshot = 2;
constexpr int kEventBits = 0x300;
// Playback opens five seconds before the first kill. Treat confirmed hits in
// that same lead-in as part of the action when they are against a player who
// is subsequently killed in the run. Never cross the attacker's last death.
constexpr std::int32_t kActionLeadInMs = 5000;
constexpr int kSvcNop = 1;
constexpr int kSvcGamestate = 2;
constexpr int kSvcConfigString = 3;
constexpr int kSvcBaseline = 4;
constexpr int kSvcServerCommand = 5;
constexpr int kSvcDownload = 6;
constexpr int kSvcSnapshot = 7;
constexpr int kSvcEof = 8;
constexpr std::int16_t kDownloadTypeWww = -1;
constexpr int kFloatIntBits = 13;
constexpr int kFloatIntBias = 1 << (kFloatIntBits - 1);

struct NetField {
    const char* name;
    int bits;
};

constexpr std::array<NetField, 71> kEntityFields = {{
    {"eType", 8}, {"eFlags", 24}, {"pos.trType", 8}, {"pos.trTime", 32},
    {"pos.trDuration", 32}, {"pos.trBase[0]", 0}, {"pos.trBase[1]", 0},
    {"pos.trBase[2]", 0}, {"pos.trDelta[0]", 0}, {"pos.trDelta[1]", 0},
    {"pos.trDelta[2]", 0}, {"apos.trType", 8}, {"apos.trTime", 32},
    {"apos.trDuration", 32}, {"apos.trBase[0]", 0}, {"apos.trBase[1]", 0},
    {"apos.trBase[2]", 0}, {"apos.trDelta[0]", 0}, {"apos.trDelta[1]", 0},
    {"apos.trDelta[2]", 0}, {"time", 32}, {"time2", 32}, {"origin[0]", 0},
    {"origin[1]", 0}, {"origin[2]", 0}, {"origin2[0]", 0}, {"origin2[1]", 0},
    {"origin2[2]", 0}, {"angles[0]", 0}, {"angles[1]", 0}, {"angles[2]", 0},
    {"angles2[0]", 0}, {"angles2[1]", 0}, {"angles2[2]", 0},
    {"otherEntityNum", kEntityNumberBits}, {"otherEntityNum2", kEntityNumberBits},
    {"groundEntityNum", kEntityNumberBits}, {"loopSound", 8}, {"constantLight", 32},
    {"dl_intensity", 32}, {"modelindex", 9}, {"modelindex2", 9}, {"frame", 16},
    {"clientNum", 8}, {"solid", 24}, {"event", 10}, {"eventParm", 8},
    {"eventSequence", 8}, {"events[0]", 8}, {"events[1]", 8}, {"events[2]", 8},
    {"events[3]", 8}, {"eventParms[0]", 8}, {"eventParms[1]", 8},
    {"eventParms[2]", 8}, {"eventParms[3]", 8}, {"powerups", 16}, {"weapon", 8},
    {"legsAnim", 10}, {"torsoAnim", 10}, {"density", 10}, {"dmgFlags", 32},
    {"onFireStart", 32}, {"onFireEnd", 32}, {"nextWeapon", 8}, {"teamNum", 8},
    {"effect1Time", 32}, {"effect2Time", 32}, {"effect3Time", 32},
    {"animMovetype", 4}, {"aiState", 2},
}};

constexpr std::array<NetField, 77> kPlayerFields = {{
    {"commandTime", 32}, {"pm_type", 8}, {"bobCycle", 8}, {"pm_flags", 16},
    {"pm_time", -16}, {"origin[0]", 0}, {"origin[1]", 0}, {"origin[2]", 0},
    {"velocity[0]", 0}, {"velocity[1]", 0}, {"velocity[2]", 0},
    {"weaponTime", -16}, {"weaponDelay", -16}, {"grenadeTimeLeft", -16},
    {"gravity", 16}, {"leanf", 0}, {"speed", 16}, {"delta_angles[0]", 16},
    {"delta_angles[1]", 16}, {"delta_angles[2]", 16},
    {"groundEntityNum", kEntityNumberBits}, {"legsTimer", 16}, {"torsoTimer", 16},
    {"legsAnim", 10}, {"torsoAnim", 10}, {"movementDir", 8}, {"eFlags", 24},
    {"eventSequence", 8}, {"events[0]", 8}, {"events[1]", 8}, {"events[2]", 8},
    {"events[3]", 8}, {"eventParms[0]", 8}, {"eventParms[1]", 8},
    {"eventParms[2]", 8}, {"eventParms[3]", 8}, {"clientNum", 8},
    {"weapons[0]", 32}, {"weapons[1]", 32}, {"weapon", 7}, {"weaponstate", 4},
    {"weapAnim", 10}, {"viewangles[0]", 0}, {"viewangles[1]", 0},
    {"viewangles[2]", 0}, {"viewheight", -8}, {"damageEvent", 8}, {"damageYaw", 8},
    {"damagePitch", 8}, {"damageCount", 8}, {"mins[0]", 0}, {"mins[1]", 0},
    {"mins[2]", 0}, {"maxs[0]", 0}, {"maxs[1]", 0}, {"maxs[2]", 0},
    {"crouchMaxZ", 0}, {"crouchViewHeight", 0}, {"standViewHeight", 0},
    {"deadViewHeight", 0}, {"runSpeedScale", 0}, {"sprintSpeedScale", 0},
    {"crouchSpeedScale", 0}, {"friction", 0}, {"viewlocked", 8},
    {"viewlocked_entNum", 16}, {"nextWeapon", 8}, {"teamNum", 8},
    {"onFireStart", 32}, {"curWeapHeat", 8}, {"aimSpreadScale", 8},
    {"serverCursorHint", 8}, {"serverCursorHintVal", 8}, {"classWeaponTime", 32},
    {"identifyClient", 8}, {"identifyClientHealth", 8}, {"aiState", 2},
}};

constexpr std::size_t kEsEType = 0;
constexpr std::size_t kEsOtherEntityNum = 34;
constexpr std::size_t kEsOtherEntityNum2 = 35;
constexpr std::size_t kEsLoopSound = 37;
constexpr std::size_t kEsModelIndex = 40;
constexpr std::size_t kEsEvent = 45;
constexpr std::size_t kEsEventParm = 46;
constexpr std::size_t kEsWeapon = 57;

class MessageReader {
public:
    MessageReader(const std::vector<std::uint8_t>& data, const detail::HuffmanDecoder& decoder)
        : data_(data), decoder_(decoder) {}

    std::int32_t readBits(int requestedBits) {
        if (readCount_ > static_cast<int>(data_.size())) {
            throw std::runtime_error("Read past the end of a demo message");
        }

        const bool signedValue = requestedBits < 0;
        const int width = std::abs(requestedBits);
        int remainingBits = width;
        int rawBits = 0;
        std::uint32_t value = 0;

        if ((remainingBits & 7) != 0) {
            rawBits = remainingBits & 7;
            if (bitOffset_ + rawBits > static_cast<int>(data_.size() * 8)) {
                throw std::runtime_error("Truncated raw bit field in demo message");
            }
            for (int i = 0; i < rawBits; ++i) {
                const int bit = (data_[static_cast<std::size_t>(bitOffset_ >> 3)] >>
                                 (bitOffset_ & 7)) & 1;
                value |= static_cast<std::uint32_t>(bit) << i;
                ++bitOffset_;
            }
            remainingBits -= rawBits;
        }

        for (int i = 0; i < remainingBits; i += 8) {
            const int decoded = decoder_.receive(
                data_.data(), bitOffset_, static_cast<int>(data_.size() * 8));
            if (bitOffset_ > static_cast<int>(data_.size() * 8)) {
                throw std::runtime_error("Truncated Huffman field in demo message");
            }
            value |= static_cast<std::uint32_t>(decoded) << (i + rawBits);
        }

        readCount_ = (bitOffset_ >> 3) + 1;
        if (signedValue && width > 0 && width < 32 &&
            (value & (std::uint32_t{1} << (width - 1))) != 0) {
            value |= ~((std::uint32_t{1} << width) - 1);
        }

        std::int32_t result = 0;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    int readByte() { return static_cast<std::uint8_t>(readBits(8)); }
    std::int16_t readShort() { return static_cast<std::int16_t>(readBits(16)); }
    std::int32_t readLong() { return readBits(32); }

    std::string readString(std::size_t maximumLength = 8192) {
        std::string result;
        result.reserve(std::min<std::size_t>(maximumLength, 256));
        while (true) {
            const int value = readByte();
            if (value == 0) {
                return result;
            }
            if (result.size() < maximumLength) {
                result.push_back(value == '%' ? '.' : static_cast<char>(value));
            }
        }
    }

    void skipBytes(int count) {
        if (count < 0) {
            throw std::runtime_error("Negative byte count in demo message");
        }
        for (int i = 0; i < count; ++i) {
            (void)readByte();
        }
    }

private:
    const std::vector<std::uint8_t>& data_;
    const detail::HuffmanDecoder& decoder_;
    int bitOffset_ = 0;
    int readCount_ = 0;
};

struct EntityState {
    int number = 0;
    std::array<std::int32_t, kEntityFields.size()> values{};
};

struct PlayerState {
    std::array<std::int32_t, kPlayerFields.size()> values{};
    std::array<std::int32_t, 16> stats{};
    std::array<std::int32_t, 16> persistent{};
    std::array<std::int32_t, 16> holdable{};
    std::array<std::int32_t, 16> powerups{};
    std::array<std::int32_t, 64> ammo{};
    std::array<std::int32_t, 64> ammoClip{};
};

struct Snapshot {
    bool valid = false;
    int messageNumber = 0;
    int deltaNumber = -1;
    std::int32_t serverTime = 0;
    PlayerState playerState{};
    std::vector<EntityState> entities;
};

std::int32_t floatBits(float value) {
    std::int32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

std::unordered_map<std::string, std::string> parseInfoString(std::string_view text) {
    std::unordered_map<std::string, std::string> result;
    std::size_t position = (!text.empty() && text.front() == '\\') ? 1 : 0;
    while (position < text.size()) {
        const std::size_t keyEnd = text.find('\\', position);
        if (keyEnd == std::string_view::npos) {
            break;
        }
        const std::size_t valueStart = keyEnd + 1;
        const std::size_t valueEnd = text.find('\\', valueStart);
        result.emplace(
            std::string(text.substr(position, keyEnd - position)),
            std::string(text.substr(valueStart, valueEnd == std::string_view::npos
                                                    ? text.size() - valueStart
                                                    : valueEnd - valueStart)));
        if (valueEnd == std::string_view::npos) {
            break;
        }
        position = valueEnd + 1;
    }
    return result;
}

int parseInteger(std::string_view value, int fallback = 0) {
    int result = fallback;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} ? result : fallback;
}

std::string unquoteCommandArgument(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r' || value.front() == '\n')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '\r' || value.back() == '\n')) {
        value.remove_suffix(1);
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return std::string(value);
}

class ParserState {
public:
    ParserState(std::filesystem::path path, DemoParseOptions options)
        : options_(options) {
        info_.path = std::move(path);
        resetGameState();
    }

    DemoInfo run() {
        std::ifstream input(info_.path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Could not open demo file: " + info_.path.string());
        }

        if (options_.collectProtocolLog) {
            std::error_code sizeError;
            const std::uintmax_t fileSize = std::filesystem::file_size(info_.path, sizeError);
            if (!sizeError && fileSize <=
                    static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max() / 6)) {
                info_.protocolLog.reserve(static_cast<std::size_t>(fileSize) * 6 + 4096);
            }
            appendProtocolLine(
                "FILE",
                "path=\"" + escapeProtocolText(info_.path.u8string()) + "\"; bytes=" +
                    (sizeError ? std::string("unknown") : std::to_string(fileSize)) +
                    "; format=id Tech 3 demo protocol 84");
            appendProtocolLine(
                "INFO",
                "Decoded records are followed by complete raw message hex rows. "
                "Binary download payloads remain recoverable from those RAW rows.");
        }

        while (true) {
            bool truncatedField = false;
            const std::optional<std::int32_t> sequence =
                readLittleLong(input, truncatedField);
            if (!sequence.has_value()) {
                if (truncatedField) {
                    addTruncatedDemoWarning("an incomplete message header");
                }
                break;
            }
            const std::optional<std::int32_t> length =
                readLittleLong(input, truncatedField);
            if (!length.has_value()) {
                addTruncatedDemoWarning("an incomplete message header");
                break;
            }
            if (*length == -1) {
                appendProtocolLine(
                    "END",
                    "terminal message marker; sequence=" + std::to_string(*sequence));
                break;
            }
            if (*length < 0 || *length > 2 * 1024 * 1024) {
                throw std::runtime_error("Invalid demo message length");
            }

            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(*length));
            input.read(reinterpret_cast<char*>(bytes.data()), *length);
            if (input.gcount() != *length) {
                addTruncatedDemoWarning("an incomplete final message");
                break;
            }
            parseMessage(*sequence, bytes);
        }

        finalizePlayers();
        if (info_.firstServerTimeMs < 0) {
            throw std::runtime_error("The demo does not contain valid snapshots");
        }
        std::stable_sort(info_.kills.begin(), info_.kills.end(), [](const KillEvent& a, const KillEvent& b) {
            return a.serverTimeMs < b.serverTimeMs;
        });
        appendProtocolLine(
            "SUMMARY",
            "map=\"" + escapeProtocolText(info_.mapName) + "\"; game=\"" +
                escapeProtocolText(info_.gameName) + "\"; modVersion=\"" +
                escapeProtocolText(info_.modVersion) + "\"; povClient=" +
                std::to_string(info_.povClientNum) + "; pov=\"" +
                escapeProtocolText(info_.povName) + "\"; players=" +
                std::to_string(info_.players.size()) + "; obituaryEvents=" +
                std::to_string(info_.kills.size()) + "; warnings=" +
                std::to_string(info_.warnings.size()));
        return std::move(info_);
    }

private:
    static std::string escapeProtocolText(std::string_view value) {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string escaped;
        escaped.reserve(value.size() + 16);
        for (const unsigned char character : value) {
            switch (character) {
                case '\\': escaped += "\\\\"; break;
                case '"': escaped += "\\\""; break;
                case '\r': escaped += "\\r"; break;
                case '\n': escaped += "\\n"; break;
                case '\t': escaped += "\\t"; break;
                default:
                    if (character < 0x20 || character == 0x7f) {
                        escaped += "\\x";
                        escaped.push_back(digits[(character >> 4) & 0x0f]);
                        escaped.push_back(digits[character & 0x0f]);
                    } else {
                        escaped.push_back(static_cast<char>(character));
                    }
                    break;
            }
        }
        return escaped;
    }

    void appendProtocolLine(std::string_view category, const std::string& detail) {
        if (!options_.collectProtocolLog) {
            return;
        }
        std::ostringstream line;
        line << std::setfill('0') << std::setw(8) << ++protocolLineNumber_
             << " | msg=";
        if (currentMessageSequence_ >= 0) {
            line << currentMessageSequence_;
        } else {
            line << '-';
        }
        line << " | server=";
        if (currentServerTimeMs_ >= 0) {
            line << currentServerTimeMs_;
        } else {
            line << '-';
        }
        line << " | " << category << " | " << detail << "\r\n";
        info_.protocolLog += line.str();
    }

    void appendRawMessage(const std::vector<std::uint8_t>& bytes) {
        if (!options_.collectProtocolLog) {
            return;
        }
        static constexpr char digits[] = "0123456789ABCDEF";
        constexpr std::size_t bytesPerRow = 24;
        for (std::size_t offset = 0; offset < bytes.size(); offset += bytesPerRow) {
            const std::size_t count = std::min(bytesPerRow, bytes.size() - offset);
            std::string hex;
            std::string ascii;
            hex.reserve(count * 3);
            ascii.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                const unsigned char value = bytes[offset + index];
                if (!hex.empty()) hex.push_back(' ');
                hex.push_back(digits[(value >> 4) & 0x0f]);
                hex.push_back(digits[value & 0x0f]);
                ascii.push_back(value >= 0x20 && value < 0x7f
                                    ? static_cast<char>(value)
                                    : '.');
            }
            std::ostringstream detail;
            detail << "offset=0x" << std::uppercase << std::hex << std::setfill('0')
                   << std::setw(8) << offset << "; hex=" << hex << "; ascii=\""
                   << escapeProtocolText(ascii) << '"';
            appendProtocolLine("RAW", detail.str());
        }
    }

    static std::string configStringLabel(int index) {
        if (index == kConfigServerInfo) return "CS_SERVERINFO";
        if (index == kConfigWarmup) return "CS_WARMUP";
        if (index == kConfigLevelStartTime) return "CS_LEVEL_START_TIME";
        if (index == kConfigIntermission) return "CS_INTERMISSION";
        if (index == kConfigWolfInfo) return "CS_WOLFINFO";
        if (index >= kConfigPlayers && index < kConfigPlayers + kMaxClients) {
            return "CS_PLAYERS+" + std::to_string(index - kConfigPlayers);
        }
        return "CS_" + std::to_string(index);
    }

    static float valueAsFloat(std::int32_t bits) {
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void appendEntityState(std::string_view category, const EntityState& entity) {
        if (!options_.collectProtocolLog) return;
        std::ostringstream detail;
        detail << "number=" << entity.number;
        for (std::size_t index = 0; index < kEntityFields.size(); ++index) {
            const NetField& field = kEntityFields[index];
            detail << "; " << field.name << '=' << entity.values[index];
            if (field.bits == 0) {
                detail << "(float=" << valueAsFloat(entity.values[index]) << ')';
            }
        }
        appendProtocolLine(category, detail.str());
    }

    template <std::size_t Size>
    static void appendNumericArray(
        std::ostringstream& detail,
        const char* label,
        const std::array<std::int32_t, Size>& values) {
        detail << "; " << label << "=[";
        for (std::size_t index = 0; index < Size; ++index) {
            if (index != 0) detail << ',';
            detail << values[index];
        }
        detail << ']';
    }

    void appendPlayerState(const PlayerState& state) {
        if (!options_.collectProtocolLog) return;
        std::ostringstream detail;
        for (std::size_t index = 0; index < kPlayerFields.size(); ++index) {
            if (index != 0) detail << "; ";
            const NetField& field = kPlayerFields[index];
            detail << field.name << '=' << state.values[index];
            if (field.bits == 0) {
                detail << "(float=" << valueAsFloat(state.values[index]) << ')';
            }
        }
        appendNumericArray(detail, "stats", state.stats);
        appendNumericArray(detail, "persistent", state.persistent);
        appendNumericArray(detail, "holdable", state.holdable);
        appendNumericArray(detail, "powerups", state.powerups);
        appendNumericArray(detail, "ammo", state.ammo);
        appendNumericArray(detail, "ammoClip", state.ammoClip);
        appendProtocolLine("PLAYERSTATE", detail.str());
    }

    static std::optional<std::int32_t> readLittleLong(
        std::istream& input,
        bool& truncatedField) {
        truncatedField = false;
        std::array<std::uint8_t, 4> bytes{};
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (input.gcount() == 0) {
            return std::nullopt;
        }
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            truncatedField = true;
            return std::nullopt;
        }
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[0]) |
                                    (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                    (static_cast<std::uint32_t>(bytes[2]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[3]) << 24);
        std::int32_t result = 0;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    void addTruncatedDemoWarning(std::string_view detail) {
        const std::string warning =
            "The demo ends with " + std::string(detail) +
            "; all complete snapshots and events before it were recovered.";
        info_.warnings.push_back(warning);
        appendProtocolLine("WARNING", warning);
    }

    void parseMessage(int sequence, const std::vector<std::uint8_t>& bytes) {
        currentMessageSequence_ = sequence;
        appendProtocolLine(
            "MESSAGE",
            "sequence=" + std::to_string(sequence) + "; payloadBytes=" +
                std::to_string(bytes.size()));
        appendRawMessage(bytes);
        MessageReader message(bytes, decoder_);
        const std::int32_t reliableAcknowledgement = message.readLong();
        appendProtocolLine(
            "ACK",
            "reliableAcknowledgement=" + std::to_string(reliableAcknowledgement));

        int illegibleCommands = 0;
        while (true) {
            const int command = message.readByte();
            if (command == kSvcEof) {
                appendProtocolLine("SVC_EOF", "end of decoded message");
                break;
            }
            switch (command) {
                case kSvcNop:
                    appendProtocolLine("SVC_NOP", "no operation");
                    break;
                case kSvcGamestate:
                    appendProtocolLine("SVC_GAMESTATE", "begin");
                    parseGameState(message);
                    break;
                case kSvcServerCommand: {
                    const std::int32_t commandSequence = message.readLong();
                    const std::string serverCommand = message.readString();
                    appendProtocolLine(
                        "SVC_SERVERCOMMAND",
                        "sequence=" + std::to_string(commandSequence) + "; text=\"" +
                            escapeProtocolText(serverCommand) + "\"");
                    parseServerCommand(serverCommand);
                    break;
                }
                case kSvcSnapshot:
                    parseSnapshot(sequence, message);
                    break;
                case kSvcDownload:
                    parseDownload(message);
                    break;
                default:
                    if (++illegibleCommands > 1) {
                        throw std::runtime_error("Unknown protocol command in demo message: " +
                                                 std::to_string(command));
                    }
                    break;
            }
        }
    }

    void parseDownload(MessageReader& message) {
        const std::int16_t block = message.readShort();

        // ET's WWW redirect message contains a URL followed by its expected
        // size and flags. Downloads are irrelevant to frag indexing, but the
        // complete payload must still be consumed to keep the bitstream aligned.
        if (block == kDownloadTypeWww) {
            const std::string url = message.readString();
            const std::int32_t expectedSize = message.readLong();
            const std::int32_t flags = message.readLong();
            appendProtocolLine(
                "SVC_DOWNLOAD",
                "type=WWW; url=\"" + escapeProtocolText(url) + "\"; expectedBytes=" +
                    std::to_string(expectedSize) + "; flags=" + std::to_string(flags));
            return;
        }

        // Block zero prefixes the first binary chunk with the complete file
        // size. A negative size is followed by a server-provided error string
        // and terminates the download command.
        if (block == 0) {
            const std::int32_t downloadSize = message.readLong();
            if (downloadSize < 0) {
                const std::string error = message.readString();
                appendProtocolLine(
                    "SVC_DOWNLOAD",
                    "block=0; error=\"" + escapeProtocolText(error) + "\"");
                return;
            }
            appendProtocolLine(
                "SVC_DOWNLOAD",
                "block=0; completeFileBytes=" + std::to_string(downloadSize));
        }

        const int chunkSize = message.readShort();
        if (chunkSize < 0) {
            throw std::runtime_error("Invalid download chunk size in demo message");
        }
        appendProtocolLine(
            "SVC_DOWNLOAD",
            "block=" + std::to_string(block) + "; chunkBytes=" +
                std::to_string(chunkSize) +
                "; payload is present verbatim in the preceding RAW rows");
        message.skipBytes(chunkSize);
    }

    void resetGameState() {
        configStrings_.fill({});
        baselines_.fill({});
        for (auto& snapshot : snapshots_) {
            snapshot.reset();
        }
        currentPlayers_.fill({});
        currentPlayerPresent_.fill(false);
        eventSignatures_.clear();
        bigConfigString_.clear();
        currentMatchPhase_ = MatchPhase::Unknown;
        currentServerTimeMs_ = -1;
    }

    void parseGameState(MessageReader& message) {
        resetGameState();
        appendProtocolLine("GAMESTATE", "state reset");
        const std::int32_t serverCommandSequence = message.readLong();
        appendProtocolLine(
            "GAMESTATE",
            "serverCommandSequence=" + std::to_string(serverCommandSequence));
        while (true) {
            const int command = message.readByte();
            if (command == kSvcEof) {
                break;
            }
            if (command == kSvcConfigString) {
                const int index = static_cast<std::uint16_t>(message.readShort());
                if (index < 0 || index >= kMaxConfigStrings) {
                    throw std::runtime_error("Configstring index is out of range");
                }
                setConfigString(index, message.readString(16384));
            } else if (command == kSvcBaseline) {
                const int entityNumber = message.readBits(kEntityNumberBits);
                if (entityNumber < 0 || entityNumber >= kMaxEntities) {
                    throw std::runtime_error("Baseline entity number is out of range");
                }
                bool removed = false;
                baselines_[static_cast<std::size_t>(entityNumber)] =
                    readDeltaEntity(message, EntityState{}, entityNumber, removed);
                appendEntityState(
                    removed ? "BASELINE_REMOVED" : "BASELINE",
                    baselines_[static_cast<std::size_t>(entityNumber)]);
            } else {
                throw std::runtime_error("Invalid gamestate command: " +
                                         std::to_string(command));
            }
        }

        info_.povClientNum = message.readLong();
        const std::int32_t checksumFeed = message.readLong();
        appendProtocolLine(
            "GAMESTATE",
            "povClient=" + std::to_string(info_.povClientNum) +
                "; checksumFeed=" + std::to_string(checksumFeed) + "; end");
        refreshServerMetadata();
        if (info_.povClientNum >= 0 && info_.povClientNum < kMaxClients &&
            currentPlayerPresent_[static_cast<std::size_t>(info_.povClientNum)]) {
            info_.povName = currentPlayers_[static_cast<std::size_t>(info_.povClientNum)].cleanName;
        }
    }

    static EntityState readDeltaEntity(
        MessageReader& message,
        const EntityState& from,
        int number,
        bool& removed) {
        EntityState to = from;
        removed = false;
        if (message.readBits(1) != 0) {
            to = EntityState{};
            to.number = kEntityNone;
            removed = true;
            return to;
        }
        if (message.readBits(1) == 0) {
            to.number = number;
            return to;
        }

        const int lastChangedField = message.readByte();
        if (lastChangedField < 0 ||
            lastChangedField > static_cast<int>(kEntityFields.size())) {
            throw std::runtime_error("Invalid entityState field count");
        }
        to.number = number;
        for (int i = 0; i < lastChangedField; ++i) {
            if (message.readBits(1) == 0) {
                continue;
            }
            const NetField& field = kEntityFields[static_cast<std::size_t>(i)];
            if (field.bits == 0) {
                if (message.readBits(1) == 0) {
                    to.values[static_cast<std::size_t>(i)] = 0;
                } else if (message.readBits(1) == 0) {
                    const int truncated = message.readBits(kFloatIntBits) - kFloatIntBias;
                    to.values[static_cast<std::size_t>(i)] = floatBits(static_cast<float>(truncated));
                } else {
                    to.values[static_cast<std::size_t>(i)] = message.readBits(32);
                }
            } else if (message.readBits(1) == 0) {
                to.values[static_cast<std::size_t>(i)] = 0;
            } else {
                to.values[static_cast<std::size_t>(i)] = message.readBits(field.bits);
            }
        }
        return to;
    }

    static PlayerState readDeltaPlayerState(
        MessageReader& message,
        const PlayerState* from) {
        PlayerState to = from != nullptr ? *from : PlayerState{};
        const int lastChangedField = message.readByte();
        if (lastChangedField < 0 ||
            lastChangedField > static_cast<int>(kPlayerFields.size())) {
            throw std::runtime_error("Invalid playerState field count");
        }

        for (int i = 0; i < lastChangedField; ++i) {
            if (message.readBits(1) == 0) {
                continue;
            }
            const NetField& field = kPlayerFields[static_cast<std::size_t>(i)];
            if (field.bits == 0) {
                if (message.readBits(1) == 0) {
                    const int truncated = message.readBits(kFloatIntBits) - kFloatIntBias;
                    to.values[static_cast<std::size_t>(i)] = floatBits(static_cast<float>(truncated));
                } else {
                    to.values[static_cast<std::size_t>(i)] = message.readBits(32);
                }
            } else {
                to.values[static_cast<std::size_t>(i)] = message.readBits(field.bits);
            }
        }

        if (message.readBits(1) != 0) {
            readShortArrayDelta(message, to.stats);
            readShortArrayDelta(message, to.persistent);
            readShortArrayDelta(message, to.holdable);
            if (message.readBits(1) != 0) {
                const std::uint16_t mask = static_cast<std::uint16_t>(message.readShort());
                for (int i = 0; i < 16; ++i) {
                    if ((mask & (1u << i)) != 0) {
                        to.powerups[static_cast<std::size_t>(i)] = message.readLong();
                    }
                }
            }
        }

        if (message.readBits(1) != 0) {
            for (int group = 0; group < 4; ++group) {
                if (message.readBits(1) != 0) {
                    readAmmoGroup(message, to.ammo, group);
                }
            }
        }
        for (int group = 0; group < 4; ++group) {
            if (message.readBits(1) != 0) {
                readAmmoGroup(message, to.ammoClip, group);
            }
        }
        return to;
    }

    template <std::size_t Size>
    static void readShortArrayDelta(
        MessageReader& message,
        std::array<std::int32_t, Size>& values) {
        if (message.readBits(1) == 0) {
            return;
        }
        const std::uint16_t mask = static_cast<std::uint16_t>(message.readShort());
        for (std::size_t i = 0; i < Size; ++i) {
            if ((mask & (1u << i)) != 0) {
                values[i] = message.readShort();
            }
        }
    }

    static void readAmmoGroup(
        MessageReader& message,
        std::array<std::int32_t, 64>& values,
        int group) {
        const std::uint16_t mask = static_cast<std::uint16_t>(message.readShort());
        for (int i = 0; i < 16; ++i) {
            if ((mask & (1u << i)) != 0) {
                values[static_cast<std::size_t>(group * 16 + i)] = message.readShort();
            }
        }
    }

    void parseSnapshot(int messageNumber, MessageReader& message) {
        Snapshot snapshot;
        snapshot.messageNumber = messageNumber;
        snapshot.serverTime = message.readLong();
        currentServerTimeMs_ = snapshot.serverTime;
        const int deltaDistance = message.readByte();
        snapshot.deltaNumber = deltaDistance == 0 ? -1 : messageNumber - deltaDistance;
        const int snapFlags = message.readByte();

        const Snapshot* old = nullptr;
        if (snapshot.deltaNumber <= 0) {
            snapshot.valid = true;
        } else {
            const auto& candidate = snapshots_[static_cast<std::size_t>(snapshot.deltaNumber &
                                                                        (kPacketBackup - 1))];
            if (candidate.has_value() && candidate->valid &&
                candidate->messageNumber == snapshot.deltaNumber) {
                old = &*candidate;
                snapshot.valid = true;
            }
        }

        const int areaMaskLength = message.readByte();
        if (areaMaskLength < 0 || areaMaskLength > 32) {
            throw std::runtime_error("Invalid area mask size");
        }
        std::ostringstream areaMask;
        areaMask << std::uppercase << std::hex << std::setfill('0');
        for (int index = 0; index < areaMaskLength; ++index) {
            if (index != 0) areaMask << ' ';
            areaMask << std::setw(2) << message.readByte();
        }
        appendProtocolLine(
            "SVC_SNAPSHOT",
            "messageNumber=" + std::to_string(messageNumber) +
                "; serverTime=" + std::to_string(snapshot.serverTime) +
                "; deltaDistance=" + std::to_string(deltaDistance) +
                "; deltaMessage=" + std::to_string(snapshot.deltaNumber) +
                "; snapFlags=" + std::to_string(snapFlags) +
                "; valid=" + (snapshot.valid ? std::string("true") : std::string("false")) +
                "; areaMaskBytes=" + std::to_string(areaMaskLength) +
                "; areaMaskHex=\"" + areaMask.str() + "\"");
        snapshot.playerState = readDeltaPlayerState(
            message, old != nullptr ? &old->playerState : nullptr);
        appendPlayerState(snapshot.playerState);

        std::vector<EntityState> changedEntities;
        snapshot.entities = parsePacketEntities(message, old, changedEntities);
        if (!snapshot.valid) {
            appendProtocolLine(
                "SNAPSHOT_SKIPPED",
                "delta base was unavailable; decoded data was not indexed");
            return;
        }

        if (info_.firstServerTimeMs < 0) {
            info_.firstServerTimeMs = snapshot.serverTime;
        }
        info_.lastServerTimeMs = snapshot.serverTime;
        for (const EntityState& entity : changedEntities) {
            appendEntityState("ENTITY", entity);
            processEntityEvent(entity, snapshot.serverTime);
        }
        appendProtocolLine(
            "SNAPSHOT_COMPLETE",
            "entities=" + std::to_string(snapshot.entities.size()) +
                "; changedEntities=" + std::to_string(changedEntities.size()));
        snapshots_[static_cast<std::size_t>(messageNumber & (kPacketBackup - 1))] =
            std::move(snapshot);
    }

    std::vector<EntityState> parsePacketEntities(
        MessageReader& message,
        const Snapshot* old,
        std::vector<EntityState>& changed) {
        const std::vector<EntityState>* oldEntities = old != nullptr ? &old->entities : nullptr;
        std::size_t oldIndex = 0;
        std::vector<EntityState> result;
        result.reserve(oldEntities != nullptr ? oldEntities->size() + 16 : 64);

        auto oldNumber = [&]() -> int {
            if (oldEntities == nullptr || oldIndex >= oldEntities->size()) {
                return kMaxEntities;
            }
            return (*oldEntities)[oldIndex].number;
        };

        while (true) {
            const int newNumber = message.readBits(kEntityNumberBits);
            if (newNumber >= kEntityNone) {
                break;
            }

            while (oldNumber() < newNumber) {
                result.push_back((*oldEntities)[oldIndex++]);
            }

            bool removed = false;
            if (oldNumber() == newNumber) {
                EntityState decoded = readDeltaEntity(
                    message, (*oldEntities)[oldIndex], newNumber, removed);
                ++oldIndex;
                if (removed) {
                    eventSignatures_.erase(newNumber);
                    appendProtocolLine(
                        "ENTITY_REMOVE",
                        "number=" + std::to_string(newNumber));
                } else {
                    result.push_back(decoded);
                    changed.push_back(std::move(decoded));
                }
            } else {
                EntityState decoded = readDeltaEntity(
                    message, baselines_[static_cast<std::size_t>(newNumber)], newNumber, removed);
                if (removed) {
                    appendProtocolLine(
                        "ENTITY_REMOVE",
                        "number=" + std::to_string(newNumber));
                } else {
                    result.push_back(decoded);
                    changed.push_back(std::move(decoded));
                }
            }
        }

        if (oldEntities != nullptr) {
            while (oldIndex < oldEntities->size()) {
                result.push_back((*oldEntities)[oldIndex++]);
            }
        }
        return result;
    }

    void processEntityEvent(const EntityState& entity, std::int32_t serverTime) {
        const int entityType = entity.values[kEsEType];
        const int event = entityType >= kEtEvents
                              ? entityType - kEtEvents
                              : (entity.values[kEsEvent] & ~kEventBits);
        if (event != kEvObituary && event != kEvBullet) {
            return;
        }

        if (event == kEvBullet) {
            const int attacker = entity.values[kEsOtherEntityNum];
            const int target = entity.values[kEsOtherEntityNum2];
            const int hitType = entity.values[kEsModelIndex];
            if (attacker < 0 || attacker >= kMaxClients ||
                target < 0 || target >= kMaxClients ||
                hitType != kHitHeadshot) {
                return;
            }

            const std::string signature = std::to_string(entityType) + ":" +
                                          std::to_string(entity.values[kEsEvent]) + ":" +
                                          std::to_string(attacker) + ":" +
                                          std::to_string(target) + ":" +
                                          std::to_string(entity.values[kEsWeapon]) + ":" +
                                          std::to_string(hitType);
            auto& previousSignature = eventSignatures_[entity.number];
            if (previousSignature == signature) {
                return;
            }
            previousSignature = signature;

            const Player* attackerPlayer = currentPlayer(attacker);
            const Player* targetPlayer = currentPlayer(target);
            HitEvent hit;
            hit.serverTimeMs = serverTime;
            hit.demoTimeMs = info_.firstServerTimeMs >= 0
                                 ? serverTime - info_.firstServerTimeMs
                                 : 0;
            hit.attacker = attacker;
            hit.target = target;
            hit.weapon = entity.values[kEsWeapon];
            hit.headshot = true;
            hit.matchPhase = currentMatchPhase_;
            hit.attackerSessionId = attackerPlayer != nullptr
                                        ? attackerPlayer->sessionId
                                        : -1;
            hit.targetSessionId = targetPlayer != nullptr
                                      ? targetPlayer->sessionId
                                      : -1;
            hit.attackerName = attackerPlayer != nullptr
                                   ? attackerPlayer->cleanName
                                   : "#" + std::to_string(attacker);
            hit.targetName = targetPlayer != nullptr
                                 ? targetPlayer->cleanName
                                 : "#" + std::to_string(target);
            appendProtocolLine(
                "HEADSHOT_HIT",
                "attacker=" + std::to_string(hit.attacker) +
                    "; attackerSession=" + std::to_string(hit.attackerSessionId) +
                    "; attackerName=\"" + escapeProtocolText(hit.attackerName) +
                    "\"; target=" + std::to_string(hit.target) +
                    "; targetSession=" + std::to_string(hit.targetSessionId) +
                    "; targetName=\"" + escapeProtocolText(hit.targetName) +
                    "\"; weapon=" + std::to_string(hit.weapon) + " (" +
                    weaponName(hit.weapon) + "); phase=" +
                    matchPhaseName(hit.matchPhase));
            info_.hits.push_back(std::move(hit));
            return;
        }

        const int target = entity.values[kEsOtherEntityNum];
        const int rawAttacker = entity.values[kEsOtherEntityNum2];
        const int attacker = rawAttacker >= 0 && rawAttacker < kMaxClients ? rawAttacker : -1;
        const int meansOfDeath = entity.values[kEsEventParm];
        const int weapon = entity.values[kEsWeapon];
        const bool headshot = entity.values[kEsLoopSound] != 0;

        const std::string signature = std::to_string(entityType) + ":" +
                                      std::to_string(entity.values[kEsEvent]) + ":" +
                                      std::to_string(target) + ":" + std::to_string(rawAttacker) +
                                      ":" + std::to_string(meansOfDeath) + ":" +
                                      std::to_string(weapon) + ":" + (headshot ? "1" : "0");
        auto& previousSignature = eventSignatures_[entity.number];
        if (previousSignature == signature) {
            return;
        }
        previousSignature = signature;

        if (target < 0 || target >= kMaxClients) {
            const std::string warning =
                "Skipped obituary with invalid target " + std::to_string(target);
            info_.warnings.push_back(warning);
            appendProtocolLine("WARNING", warning);
            return;
        }

        KillEvent kill;
        kill.serverTimeMs = serverTime;
        kill.demoTimeMs = info_.firstServerTimeMs >= 0 ? serverTime - info_.firstServerTimeMs : 0;
        kill.attacker = attacker;
        kill.target = target;
        kill.weapon = weapon;
        kill.meansOfDeath = meansOfDeath;
        kill.headshot = headshot;
        kill.suicide = attacker == target || rawAttacker == target;
        kill.matchPhase = currentMatchPhase_;

        const Player* attackerPlayer = currentPlayer(attacker);
        const Player* targetPlayer = currentPlayer(target);
        kill.attackerName = attackerPlayer != nullptr
                                ? attackerPlayer->cleanName
                                : (rawAttacker == kEntityWorld ? "World" : "#" + std::to_string(rawAttacker));
        kill.targetName = targetPlayer != nullptr
                              ? targetPlayer->cleanName
                              : "#" + std::to_string(target);
        kill.attackerTeam = attackerPlayer != nullptr ? attackerPlayer->team : 0;
        kill.targetTeam = targetPlayer != nullptr ? targetPlayer->team : 0;
        kill.attackerSessionId = attackerPlayer != nullptr ? attackerPlayer->sessionId : -1;
        kill.targetSessionId = targetPlayer != nullptr ? targetPlayer->sessionId : -1;
        kill.teamKill = !kill.suicide && attacker >= 0 && kill.attackerTeam != 0 &&
                        kill.attackerTeam == kill.targetTeam;
        if (info_.levelStartTimeMs >= 0) {
            kill.matchElapsedMs = serverTime - info_.levelStartTimeMs;
            if (info_.timeLimitMinutes > 0.0) {
                kill.matchRemainingMs = static_cast<std::int32_t>(
                    std::llround(info_.timeLimitMinutes * 60000.0)) - kill.matchElapsedMs;
            }
        }
        appendProtocolLine(
            "OBITUARY",
            "attacker=" + std::to_string(kill.attacker) + "; attackerSession=" +
                std::to_string(kill.attackerSessionId) + "; attackerName=\"" +
                escapeProtocolText(kill.attackerName) + "\"; target=" +
                std::to_string(kill.target) + "; targetSession=" +
                std::to_string(kill.targetSessionId) + "; targetName=\"" +
                escapeProtocolText(kill.targetName) + "\"; weapon=" +
                std::to_string(kill.weapon) + " (" + weaponName(kill.weapon) +
                "); meansOfDeath=" + std::to_string(kill.meansOfDeath) + " (" +
                meansOfDeathName(kill.meansOfDeath) + "); teamKill=" +
                (kill.teamKill ? std::string("true") : std::string("false")) +
                "; suicide=" + (kill.suicide ? std::string("true") : std::string("false")) +
                "; headshotKill=" + (kill.headshot ? std::string("true") : std::string("false")) +
                "; phase=" + matchPhaseName(kill.matchPhase) +
                "; matchElapsedMs=" + std::to_string(kill.matchElapsedMs) +
                "; matchRemainingMs=" + std::to_string(kill.matchRemainingMs));
        info_.kills.push_back(std::move(kill));
    }

    const Player* currentPlayer(int clientNum) const {
        if (clientNum < 0 || clientNum >= kMaxClients ||
            !currentPlayerPresent_[static_cast<std::size_t>(clientNum)]) {
            return nullptr;
        }
        return &currentPlayers_[static_cast<std::size_t>(clientNum)];
    }

    void parseServerCommand(const std::string& command) {
        const std::size_t firstSpace = command.find(' ');
        const std::string_view verb = std::string_view(command).substr(0, firstSpace);
        std::string_view rest = firstSpace == std::string::npos
                                    ? std::string_view{}
                                    : std::string_view(command).substr(firstSpace + 1);

        if (verb == "cs") {
            applyConfigCommand(rest);
            return;
        }
        if (verb == "bcs0" || verb == "bcs1" || verb == "bcs2") {
            const std::size_t split = rest.find(' ');
            if (split == std::string_view::npos) {
                return;
            }
            const std::string index(rest.substr(0, split));
            std::string part = unquoteCommandArgument(rest.substr(split + 1));
            if (verb == "bcs0") {
                bigConfigString_ = index + " " + part;
            } else if (verb == "bcs1") {
                bigConfigString_ += part;
            } else {
                bigConfigString_ += part;
                applyConfigCommand(bigConfigString_);
                bigConfigString_.clear();
            }
        }
    }

    void applyConfigCommand(std::string_view rest) {
        const std::size_t split = rest.find(' ');
        if (split == std::string_view::npos) {
            return;
        }
        const int index = parseInteger(rest.substr(0, split), -1);
        if (index < 0 || index >= kMaxConfigStrings) {
            return;
        }
        setConfigString(index, unquoteCommandArgument(rest.substr(split + 1)));
    }

    void setConfigString(int index, std::string value) {
        appendProtocolLine(
            "CONFIGSTRING",
            "index=" + std::to_string(index) + "; name=" +
                configStringLabel(index) + "; value=\"" +
                escapeProtocolText(value) + "\"");
        configStrings_[static_cast<std::size_t>(index)] = std::move(value);
        if (index == kConfigServerInfo || index == kConfigLevelStartTime) {
            refreshServerMetadata();
        }
        if (index == kConfigWarmup || index == kConfigIntermission ||
            index == kConfigWolfInfo) {
            refreshMatchPhase();
        }
        if (index >= kConfigPlayers && index < kConfigPlayers + kMaxClients) {
            refreshPlayer(index - kConfigPlayers);
        }
    }

    void refreshPlayer(int clientNum) {
        const std::string& config = configStrings_[static_cast<std::size_t>(kConfigPlayers + clientNum)];
        if (config.empty()) {
            currentPlayerPresent_[static_cast<std::size_t>(clientNum)] = false;
            return;
        }
        const auto values = parseInfoString(config);
        const auto name = values.find("n");
        std::string playerName;
        if (name != values.end() && !name->second.empty()) {
            playerName = name->second;
        } else if (currentPlayerPresent_[static_cast<std::size_t>(clientNum)]) {
            playerName = currentPlayers_[static_cast<std::size_t>(clientNum)].name;
        } else {
            playerName = "#" + std::to_string(clientNum);
        }
        const std::string cleanName = stripEtColors(playerName);
        const auto team = values.find("t");
        const int playerTeam = team != values.end() ? parseInteger(team->second) : 0;

        const bool hadCurrent = currentPlayerPresent_[static_cast<std::size_t>(clientNum)];
        const bool sameOccupant =
            hadCurrent && currentPlayers_[static_cast<std::size_t>(clientNum)].cleanName == cleanName;
        Player player = sameOccupant
                            ? currentPlayers_[static_cast<std::size_t>(clientNum)]
                            : Player{};
        player.clientNum = clientNum;
        if (!sameOccupant) {
            player.sessionId = nextPlayerSessionId_++;
        }
        player.name = std::move(playerName);
        player.cleanName = cleanName;
        player.team = playerTeam;
        currentPlayers_[static_cast<std::size_t>(clientNum)] = std::move(player);
        currentPlayerPresent_[static_cast<std::size_t>(clientNum)] = true;

        const Player& current = currentPlayers_[static_cast<std::size_t>(clientNum)];
        const auto known = std::find_if(
            knownPlayerSessions_.begin(),
            knownPlayerSessions_.end(),
            [&](const Player& candidate) { return candidate.sessionId == current.sessionId; });
        if (known == knownPlayerSessions_.end()) {
            knownPlayerSessions_.push_back(current);
        } else {
            *known = current;
        }
    }

    void refreshServerMetadata() {
        const auto values = parseInfoString(configStrings_[kConfigServerInfo]);
        auto assign = [&](const char* key, std::string& destination) {
            const auto found = values.find(key);
            if (found != values.end()) {
                destination = found->second;
            }
        };
        assign("mapname", info_.mapName);
        assign("gamename", info_.gameName);
        assign("mod_version", info_.modVersion);
        const auto timeLimit = values.find("timelimit");
        if (timeLimit != values.end()) {
            try {
                info_.timeLimitMinutes = std::stod(timeLimit->second);
            } catch (...) {
                info_.timeLimitMinutes = 0.0;
            }
        }
        info_.levelStartTimeMs = parseInteger(configStrings_[kConfigLevelStartTime], -1);
    }

    void refreshMatchPhase() {
        const auto wolfInfo = parseInfoString(configStrings_[kConfigWolfInfo]);
        const auto gameState = wolfInfo.find("gamestate");
        if (gameState != wolfInfo.end() && !gameState->second.empty()) {
            currentMatchPhase_ = matchPhaseFromGameState(
                parseInteger(gameState->second, -1));
            return;
        }

        // Compatibility fallback for demos whose mod omits CS_WOLFINFO.
        if (parseInteger(configStrings_[kConfigIntermission], 0) != 0) {
            currentMatchPhase_ = MatchPhase::Intermission;
        } else if (parseInteger(configStrings_[kConfigWarmup], 0) > 0) {
            currentMatchPhase_ = MatchPhase::Warmup;
        } else {
            currentMatchPhase_ = MatchPhase::Unknown;
        }
    }

    void finalizePlayers() {
        info_.players = knownPlayerSessions_;
        if (info_.povClientNum >= 0 && info_.povClientNum < kMaxClients) {
            const Player* player = currentPlayer(info_.povClientNum);
            if (player != nullptr) {
                info_.povName = player->cleanName;
            } else {
                const auto known = std::find_if(
                    knownPlayerSessions_.rbegin(),
                    knownPlayerSessions_.rend(),
                    [&](const Player& candidate) {
                        return candidate.clientNum == info_.povClientNum;
                    });
                if (known != knownPlayerSessions_.rend()) {
                    info_.povName = known->cleanName;
                }
            }
        }
    }

    static const detail::HuffmanDecoder decoder_;
    DemoParseOptions options_;
    DemoInfo info_;
    std::array<std::string, kMaxConfigStrings> configStrings_{};
    std::array<EntityState, kMaxEntities> baselines_{};
    std::array<std::optional<Snapshot>, kPacketBackup> snapshots_{};
    std::array<Player, kMaxClients> currentPlayers_{};
    std::array<bool, kMaxClients> currentPlayerPresent_{};
    std::vector<Player> knownPlayerSessions_;
    int nextPlayerSessionId_ = 0;
    std::unordered_map<int, std::string> eventSignatures_;
    std::string bigConfigString_;
    MatchPhase currentMatchPhase_ = MatchPhase::Unknown;
    int currentMessageSequence_ = -1;
    std::int32_t currentServerTimeMs_ = -1;
    std::size_t protocolLineNumber_ = 0;
};

const detail::HuffmanDecoder ParserState::decoder_{};

int countHeadshotHitsForRun(
    const DemoInfo& demo,
    const FragRun& run,
    const RunFilter& filter) {
    if (run.killIndices.empty() || demo.hits.empty()) {
        return 0;
    }

    const std::size_t firstKillIndex = run.killIndices.front();
    if (firstKillIndex >= demo.kills.size()) {
        return 0;
    }
    const KillEvent& firstKill = demo.kills[firstKillIndex];

    // A frag is the first obituary in an action, not necessarily its first
    // combat event. Include the same five-second lead-in that the user sees
    // when playing a selected clip. This captures headshots that directly
    // lead to the first kill instead of starting the counter too late.
    std::int32_t windowStart = run.startDemoTimeMs > kActionLeadInMs
                                   ? run.startDemoTimeMs - kActionLeadInMs
                                   : 0;

    // A previous life can overlap the lead-in. Headshots made before the
    // selected player died must never be attributed to the new action.
    for (const KillEvent& kill : demo.kills) {
        if (kill.demoTimeMs < windowStart) {
            continue;
        }
        if (kill.demoTimeMs >= run.startDemoTimeMs) {
            break;
        }
        if (kill.target == run.attacker &&
            (run.attackerSessionId < 0 || kill.targetSessionId < 0 ||
             kill.targetSessionId == run.attackerSessionId)) {
            windowStart = kill.demoTimeMs + 1;
        }
    }

    const auto first = std::lower_bound(
        demo.hits.begin(),
        demo.hits.end(),
        windowStart,
        [](const HitEvent& hit, std::int32_t time) {
            return hit.demoTimeMs < time;
        });
    int count = 0;
    for (auto hit = first;
         hit != demo.hits.end() && hit->demoTimeMs <= run.endDemoTimeMs;
         ++hit) {
        if (!hit->headshot || hit->attacker != run.attacker ||
            (run.attackerSessionId >= 0 &&
             hit->attackerSessionId != run.attackerSessionId) ||
            (filter.weapon >= 0 && hit->weapon != filter.weapon) ||
            (firstKill.matchPhase != MatchPhase::Unknown &&
             hit->matchPhase != MatchPhase::Unknown &&
             hit->matchPhase != firstKill.matchPhase)) {
            continue;
        }

        // Count headshots only against players who are actually victims of
        // this action. Hits on an unrelated opponent who survives the action
        // must not inflate its Headshots value.
        const bool actionVictim = std::any_of(
            run.killIndices.begin(),
            run.killIndices.end(),
            [&](std::size_t killIndex) {
                if (killIndex >= demo.kills.size()) {
                    return false;
                }
                const KillEvent& kill = demo.kills[killIndex];
                return kill.target == hit->target &&
                       (kill.targetSessionId < 0 || hit->targetSessionId < 0 ||
                        kill.targetSessionId == hit->targetSessionId);
            });
        if (actionVictim) {
            ++count;
        }
    }
    return count;
}

void finishRun(
    std::vector<FragRun>& output,
    FragRun& run,
    const DemoInfo& demo,
    const RunFilter& filter) {
    run.headshotCount = countHeadshotHitsForRun(demo, run, filter);
    if (run.killIndices.size() >= static_cast<std::size_t>(filter.minimumKills) &&
        run.headshotCount >= filter.minimumHeadshots) {
        output.push_back(run);
    }
    run.killIndices.clear();
    run.headshotCount = 0;
    run.startDemoTimeMs = 0;
    run.endDemoTimeMs = 0;
}

bool isPostDeathExplosiveKill(const KillEvent& kill) {
    switch (kill.weapon) {
        case 4:  // Axis Grenade
        case 5:  // Panzerfaust
        case 9:  // Allied Grenade
        case 13: // Artillery
        case 15: // Dynamite
        case 16: // Smoketrail
        case 17: // Map Mortar
        case 18: // Explosion
        case 20: // Binoculars
        case 22: // Airstrike Marker
        case 26: // Landmine
        case 27: // Satchel Charge
        case 28: // Satchel Detonator
        case 34: // Allied Mortar
        case 37: // GPG40
        case 38: // M7
        case 43: // Deployed Mortar
        case 51: // Axis Mortar
        case 52: // Deployed Axis Mortar
        case 53: // Bazooka
        case 55: // Airstrike
            return true;
        default:
            break;
    }

    // Some mods report the delivery item differently but retain ET's
    // means-of-death value, so use both fields for the delayed-explosive set.
    switch (kill.meansOfDeath) {
        case 4:  // MOD_GRENADE
        case 15: // MOD_PANZERFAUST
        case 16: // MOD_GRENADE_LAUNCHER
        case 18: // MOD_GRENADE_PINEAPPLE
        case 19: // MOD_MAPMORTAR
        case 20: // MOD_MAPMORTAR_SPLASH
        case 22: // MOD_DYNAMITE
        case 23: // MOD_AIRSTRIKE
        case 26: // MOD_ARTY
        case 36: // MOD_EXPLOSIVE
        case 39: // MOD_GPG40
        case 40: // MOD_M7
        case 41: // MOD_LANDMINE
        case 42: // MOD_SATCHEL
        case 52: // MOD_MORTAR
        case 63: // MOD_MORTAR2
        case 64: // MOD_BAZOOKA
            return true;
        default:
            return false;
    }
}

std::vector<FragRun> findRunsForPlayer(
    const DemoInfo& demo,
    const RunFilter& filter,
    int clientNum,
    int sessionId,
    std::string playerName) {
    std::vector<FragRun> output;
    FragRun current;
    current.attacker = clientNum;
    current.attackerSessionId = sessionId;
    current.attackerName = std::move(playerName);
    bool postDeathWindowActive = false;
    std::int64_t postDeathDeadlineMs = 0;
    const auto attackerMatches = [clientNum, sessionId](const KillEvent& kill) {
        return kill.attacker == clientNum &&
               (sessionId < 0 || kill.attackerSessionId == sessionId);
    };
    const auto targetMatches = [clientNum, sessionId](const KillEvent& kill) {
        return kill.target == clientNum &&
               (sessionId < 0 || kill.targetSessionId == sessionId);
    };
    const auto eventAllowed = [&filter](const KillEvent& kill) {
        return filter.includeWarmupKills || kill.matchPhase != MatchPhase::Warmup;
    };
    std::optional<MatchPhase> previousObservedPhase;

    for (std::size_t groupBegin = 0; groupBegin < demo.kills.size();) {
        std::size_t groupEnd = groupBegin + 1;
        while (groupEnd < demo.kills.size() &&
               demo.kills[groupEnd].serverTimeMs == demo.kills[groupBegin].serverTimeMs) {
            ++groupEnd;
        }

        const MatchPhase groupPhase = demo.kills[groupBegin].matchPhase;
        if (previousObservedPhase.has_value() && *previousObservedPhase != groupPhase) {
            finishRun(output, current, demo, filter);
            postDeathWindowActive = false;
        }
        previousObservedPhase = groupPhase;

        const auto firstAllowed = std::find_if(
            demo.kills.begin() + static_cast<std::ptrdiff_t>(groupBegin),
            demo.kills.begin() + static_cast<std::ptrdiff_t>(groupEnd),
            eventAllowed);
        if (firstAllowed == demo.kills.begin() + static_cast<std::ptrdiff_t>(groupEnd)) {
            finishRun(output, current, demo, filter);
            postDeathWindowActive = false;
            groupBegin = groupEnd;
            continue;
        }

        const std::int32_t groupDemoTimeMs = firstAllowed->demoTimeMs;
        if (postDeathWindowActive &&
            static_cast<std::int64_t>(groupDemoTimeMs) > postDeathDeadlineMs) {
            finishRun(output, current, demo, filter);
            postDeathWindowActive = false;
        }

        // A direct-fire frag proves that the player is active in a new life.
        // Close a pending post-death explosive sequence before processing the
        // whole snapshot so obituary ordering cannot change the result.
        if (postDeathWindowActive) {
            const bool directFireKillInSnapshot = std::any_of(
                demo.kills.begin() + static_cast<std::ptrdiff_t>(groupBegin),
                demo.kills.begin() + static_cast<std::ptrdiff_t>(groupEnd),
                [&](const KillEvent& kill) {
                    return eventAllowed(kill) && attackerMatches(kill) && !kill.suicide &&
                           !isPostDeathExplosiveKill(kill);
                });
            if (directFireKillInSnapshot) {
                finishRun(output, current, demo, filter);
                postDeathWindowActive = false;
            }
        }

        bool playerDiedInSnapshot = false;
        for (std::size_t index = groupBegin; index < groupEnd; ++index) {
            const KillEvent& kill = demo.kills[index];
            if (!eventAllowed(kill)) {
                continue;
            }
            playerDiedInSnapshot = playerDiedInSnapshot || targetMatches(kill);
            if (!attackerMatches(kill) || kill.suicide ||
                (!filter.includeTeamKills && kill.teamKill) ||
                (filter.weapon >= 0 && kill.weapon != filter.weapon)) {
                continue;
            }

            if (!current.killIndices.empty() && filter.maximumGapMs > 0 &&
                kill.demoTimeMs - current.endDemoTimeMs > filter.maximumGapMs) {
                finishRun(output, current, demo, filter);
                postDeathWindowActive = false;
            }
            if (current.killIndices.empty()) {
                current.startDemoTimeMs = kill.demoTimeMs;
                current.attackerName = kill.attackerName;
            }
            current.killIndices.push_back(index);
            current.endDemoTimeMs = kill.demoTimeMs;
        }

        // Obituaries from one snapshot can be interleaved. Count every kill made
        // at that instant before a simultaneous death closes the player's life.
        if (playerDiedInSnapshot) {
            if (!current.killIndices.empty() && filter.postDeathExplosiveWindowMs > 0) {
                postDeathWindowActive = true;
                postDeathDeadlineMs =
                    static_cast<std::int64_t>(groupDemoTimeMs) +
                    filter.postDeathExplosiveWindowMs;
            } else {
                finishRun(output, current, demo, filter);
                postDeathWindowActive = false;
            }
        }
        groupBegin = groupEnd;
    }
    finishRun(output, current, demo, filter);
    return output;
}

} // namespace

DemoInfo DemoParser::parse(
    const std::filesystem::path& path,
    const DemoParseOptions& options) const {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension != ".dm_84") {
        throw std::invalid_argument("Only ET: Legacy .dm_84 demo files are supported");
    }
    return ParserState(path, options).run();
}

std::vector<FragRun> findFragRuns(const DemoInfo& demo, const RunFilter& filter) {
    if (filter.minimumKills < 1) {
        throw std::invalid_argument("minimumKills must be greater than zero");
    }
    if (filter.minimumHeadshots < 0) {
        throw std::invalid_argument("minimumHeadshots cannot be negative");
    }
    std::vector<FragRun> result;
    if (filter.playerClientNum >= 0) {
        if (filter.playerSessionId < 0) {
            bool foundSession = false;
            for (const Player& player : demo.players) {
                if (player.clientNum != filter.playerClientNum) {
                    continue;
                }
                foundSession = true;
                auto runs = findRunsForPlayer(
                    demo,
                    filter,
                    player.clientNum,
                    player.sessionId,
                    player.cleanName);
                result.insert(result.end(), runs.begin(), runs.end());
            }
            if (foundSession) {
                std::sort(result.begin(), result.end(), [](const FragRun& a, const FragRun& b) {
                    return a.startDemoTimeMs < b.startDemoTimeMs;
                });
                return result;
            }
        }
        std::string name = "#" + std::to_string(filter.playerClientNum);
        const auto player = std::find_if(demo.players.begin(), demo.players.end(), [&](const Player& p) {
            return p.clientNum == filter.playerClientNum &&
                   (filter.playerSessionId < 0 || p.sessionId == filter.playerSessionId);
        });
        if (player != demo.players.end()) {
            name = player->cleanName;
        }
        return findRunsForPlayer(
            demo,
            filter,
            filter.playerClientNum,
            filter.playerSessionId,
            std::move(name));
    }

    for (const Player& player : demo.players) {
        auto runs = findRunsForPlayer(
            demo,
            filter,
            player.clientNum,
            player.sessionId,
            player.cleanName);
        result.insert(result.end(), runs.begin(), runs.end());
    }
    std::sort(result.begin(), result.end(), [](const FragRun& a, const FragRun& b) {
        return a.startDemoTimeMs < b.startDemoTimeMs;
    });
    return result;
}

std::string stripEtColors(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '^' && i + 1 < value.size()) {
            ++i;
            continue;
        }
        result.push_back(value[i]);
    }
    return result;
}

std::string formatDuration(std::int32_t milliseconds, bool withMilliseconds) {
    const bool negative = milliseconds < 0;
    std::int64_t absolute = negative ? -static_cast<std::int64_t>(milliseconds) : milliseconds;
    const std::int64_t hours = absolute / 3600000;
    absolute %= 3600000;
    const std::int64_t minutes = absolute / 60000;
    absolute %= 60000;
    const std::int64_t seconds = absolute / 1000;
    const std::int64_t millis = absolute % 1000;

    std::ostringstream text;
    if (negative) {
        text << '-';
    }
    if (hours > 0) {
        text << hours << ':' << std::setw(2) << std::setfill('0') << minutes << ':';
    } else {
        text << minutes << ':';
    }
    text << std::setw(2) << std::setfill('0') << seconds;
    if (withMilliseconds) {
        text << '.' << std::setw(3) << std::setfill('0') << millis;
    }
    return text.str();
}

std::string weaponName(int weapon) {
    static constexpr std::array<const char*, 56> names = {{
        "None", "Knife", "Luger", "MP40", "Axis Grenade", "Panzerfaust",
        "Flamethrower", "Colt", "Thompson", "Allied Grenade", "Sten",
        "Syringe", "Ammo Pack", "Artillery", "Silenced Luger", "Dynamite",
        "Smoketrail", "Map Mortar", "Explosion", "Medkit", "Binoculars", "Pliers",
        "Airstrike Marker", "Kar98", "Carbine", "Garand", "Landmine", "Satchel Charge",
        "Satchel Detonator", "Smoke Bomb", "Mobile MG42", "K43", "FG42", "MG42 Dummy",
        "Allied Mortar", "Akimbo Colt", "Akimbo Luger", "GPG40", "M7",
        "Silenced Colt", "Scoped Garand", "Scoped K43", "Scoped FG42",
        "Deployed Mortar", "Adrenaline", "Akimbo Silenced Colt",
        "Akimbo Silenced Luger", "Deployed MG42", "Kabar", "Mobile Browning",
        "Deployed Browning", "Axis Mortar", "Deployed Axis Mortar", "Bazooka",
        "MP34", "Airstrike",
    }};
    if (weapon >= 0 && weapon < static_cast<int>(names.size())) {
        return names[static_cast<std::size_t>(weapon)];
    }
    return "Weapon #" + std::to_string(weapon);
}

std::string meansOfDeathName(int meansOfDeath) {
    static constexpr std::array<const char*, 67> names = {{
        "MOD_UNKNOWN", "MOD_MACHINEGUN", "MOD_BROWNING", "MOD_MG42", "MOD_GRENADE",
        "MOD_KNIFE", "MOD_LUGER", "MOD_COLT", "MOD_MP40", "MOD_THOMPSON", "MOD_STEN",
        "MOD_GARAND", "MOD_SILENCER", "MOD_FG42", "MOD_FG42SCOPE", "MOD_PANZERFAUST",
        "MOD_GRENADE_LAUNCHER", "MOD_FLAMETHROWER", "MOD_GRENADE_PINEAPPLE",
        "MOD_MAPMORTAR", "MOD_MAPMORTAR_SPLASH", "MOD_KICKED", "MOD_DYNAMITE",
        "MOD_AIRSTRIKE", "MOD_SYRINGE", "MOD_AMMO", "MOD_ARTY", "MOD_WATER", "MOD_SLIME",
        "MOD_LAVA", "MOD_CRUSH", "MOD_TELEFRAG", "MOD_FALLING", "MOD_SUICIDE",
        "MOD_TARGET_LASER", "MOD_TRIGGER_HURT", "MOD_EXPLOSIVE", "MOD_CARBINE", "MOD_KAR98",
        "MOD_GPG40", "MOD_M7", "MOD_LANDMINE", "MOD_SATCHEL", "MOD_SMOKEBOMB",
        "MOD_MOBILE_MG42", "MOD_SILENCED_COLT", "MOD_GARAND_SCOPE", "MOD_CRUSH_CONSTRUCTION",
        "MOD_CRUSH_CONSTRUCTIONDEATH", "MOD_CRUSH_CONSTRUCTIONDEATH_NOATTACKER", "MOD_K43",
        "MOD_K43_SCOPE", "MOD_MORTAR", "MOD_AKIMBO_COLT", "MOD_AKIMBO_LUGER",
        "MOD_AKIMBO_SILENCEDCOLT", "MOD_AKIMBO_SILENCEDLUGER", "MOD_SMOKEGRENADE",
        "MOD_SWAP_PLACES", "MOD_SWITCHTEAM", "MOD_SHOVE", "MOD_KNIFE_KABAR",
        "MOD_MOBILE_BROWNING", "MOD_MORTAR2", "MOD_BAZOOKA", "MOD_BACKSTAB", "MOD_MP34",
    }};
    if (meansOfDeath >= 0 && meansOfDeath < static_cast<int>(names.size())) {
        return names[static_cast<std::size_t>(meansOfDeath)];
    }
    return "MOD_#" + std::to_string(meansOfDeath);
}

std::string teamName(int team) {
    switch (team) {
        case 1: return "Axis";
        case 2: return "Allies";
        case 3: return "Spectator";
        default: return "None";
    }
}

MatchPhase matchPhaseFromGameState(int gameState) {
    // ET: Legacy gamestate_t values from q_shared.h. Waiting and reset are
    // pre-match states and therefore belong to the warmup bucket.
    switch (gameState) {
        case 0: return MatchPhase::Playing;
        case 1: // GS_WARMUP_COUNTDOWN
        case 2: // GS_WARMUP
        case 4: // GS_WAITING_FOR_PLAYERS
        case 5: // GS_RESET
            return MatchPhase::Warmup;
        case 3: return MatchPhase::Intermission;
        default: return MatchPhase::Unknown;
    }
}

std::string matchPhaseName(MatchPhase phase) {
    switch (phase) {
        case MatchPhase::Playing: return "Playing";
        case MatchPhase::Warmup: return "Warmup";
        case MatchPhase::Intermission: return "Intermission";
        default: return "Unknown";
    }
}

} // namespace etlfrag
