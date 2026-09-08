// SPDX-License-Identifier: GPL-3.0-or-later
// Internal protocol-84 writer. Included after the parser's wire-state types so
// the reader and writer use the same ET netfield layout. See THIRD_PARTY_NOTICES.
class DemoCutWriter {
public:
    DemoCutWriter(std::ostream& output, const DemoCutOptions& options,
                  const detail::HuffmanDecoder& codec)
        : output_(output), options_(options), codec_(codec) {
        start_ = std::max<std::int64_t>(0, std::int64_t(options.actionStartMs) - options.beforeMs);
        end_ = std::int64_t(options.actionEndMs) + options.afterMs;
    }
    void beginMessage(int ack) {
        ack_ = ack;
        message_.reset();
        if (started()) makeMessage();
    }
    void finishMessage() {
        if (message_) {
            message_->byte(kSvcEof);
            message_->packet(output_, sequence_++);
            message_.reset();
        }
    }
    void serverCommand(const std::string& command) {
        if (started()) writeCommand(command);
        else {
            const auto verb = command.substr(0, command.find(' '));
            if (verb == "bcs0") pendingBig_.clear();
            if (verb == "bcs0" || verb == "bcs1") pendingBig_.push_back(command);
            if (verb == "bcs2") pendingBig_.clear();
            if (pendingBig_.size() > 64) throw std::runtime_error("Too many fragmented configstring commands");
        }
    }
    void reset() {
        if (started()) throw std::runtime_error("The cut crosses a map/gamestate change. Select an action in one map.");
        pendingBig_.clear();
        haveGameState_ = true;
    }
    bool started() const { return result_.snapshots != 0; }
    bool done() const { return done_; }
    const DemoCutOptions& options() const { return options_; }
    void snapshot(const Snapshot& snap,
                  const std::array<std::string, kMaxConfigStrings>& strings,
                  const std::array<EntityState, kMaxEntities>& baselines,
                  int client, int checksum) {
        if (!haveGameState_) throw std::runtime_error("No gamestate before the first snapshot");
        if (firstSourceTime_ < 0) firstSourceTime_ = snap.serverTime;
        if (lastSourceTime_ >= 0 && snap.serverTime < lastSourceTime_)
            throw std::runtime_error("Server time restarts within this demo. Cut a single-map demo instead.");
        lastSourceTime_ = snap.serverTime;
        const auto time = std::int64_t(snap.serverTime) - firstSourceTime_;
        if (time < start_) return;
        if (!started() && (snap.flags & 2)) return; // SNAPFLAG_NOT_ACTIVE
        // A complete snapshot starts the new recording; no hidden source prefix.
        if (!started()) {
            detail::MessageWriter gs(codec_);
            gs.longValue(ack_);
            gs.byte(kSvcGamestate);
            gs.longValue(0);
            std::size_t stateBytes = 1;
            for (std::size_t i = 0; i < strings.size(); ++i) {
                if (strings[i].empty()) continue;
                stateBytes += strings[i].size() + 1;
                if (strings[i].size() >= 8192 || stateBytes > 16000)
                    throw std::runtime_error("Cut gamestate exceeds protocol-84 configstring limits");
                gs.byte(kSvcConfigString);
                gs.shortValue(static_cast<int>(i));
                gs.string(strings[i]);
            }
            for (const auto& baseline : baselines) {
                if (!baseline.number && std::all_of(baseline.values.begin(), baseline.values.end(),
                    [](std::int32_t value) { return value == 0; })) continue;
                gs.byte(kSvcBaseline);
                entity(gs, EntityState{}, &baseline, true);
            }
            gs.byte(kSvcEof);
            gs.longValue(client);
            gs.longValue(checksum);
            gs.byte(kSvcEof);
            gs.packet(output_, sequence_++);
            makeMessage();
            // bcs0/bcs1 may precede the cut while bcs2 follows it. Replay only
            // the incomplete configstring fragments with fresh reliable IDs.
            for (const auto& command : pendingBig_) writeCommand(command);
            pendingBig_.clear();
            result_.actualStartMs = static_cast<std::int32_t>(time);
        }
        const Snapshot* old = previous_ ? &*previous_ : nullptr;
        const int distance = sequence_ - previousSequence_;
        if (distance <= 0 || distance >= kPacketBackup) old = nullptr;
        message_->byte(kSvcSnapshot);
        message_->longValue(snap.serverTime);
        message_->byte(old ? distance : 0);
        message_->byte(snap.flags);
        message_->byte(static_cast<int>(snap.areaMask.size()));
        for (auto value : snap.areaMask) message_->byte(value);
        player(*message_, old ? old->playerState : PlayerState{}, snap.playerState);
        std::size_t a = 0, b = 0;
        const std::vector<EntityState> empty;
        const auto& previousEntities = old ? old->entities : empty;
        while (a < previousEntities.size() || b < snap.entities.size()) {
            const int oldNum = a < previousEntities.size() ? previousEntities[a].number : kMaxEntities;
            const int newNum = b < snap.entities.size() ? snap.entities[b].number : kMaxEntities;
            if (oldNum == newNum) {
                entity(*message_, previousEntities[a++], &snap.entities[b++], false);
            } else if (oldNum < newNum) {
                entity(*message_, previousEntities[a++], nullptr, true);
            } else {
                entity(*message_, baselines[newNum], &snap.entities[b++], true);
            }
        }
        message_->bits(kEntityNone, kEntityNumberBits);
        previous_ = snap;
        previousSequence_ = sequence_;
        ++result_.snapshots;
        result_.actualEndMs = static_cast<std::int32_t>(time);
        if (time >= end_ && result_.snapshots >= 2) done_ = true;
    }
    DemoCutResult finish() {
        if (options_.cancel && options_.cancel->load()) throw std::runtime_error("Demo cut cancelled");
        if (result_.snapshots < 2)
            throw std::runtime_error("This range contains fewer than two playable snapshots. Increase the before/after time.");
        if (result_.actualStartMs > options_.actionStartMs || result_.actualEndMs < options_.actionEndMs)
            throw std::runtime_error("The selected action is outside the available demo data. Scan the demo again.");
        result_.clampedToDemoEnd = std::int64_t(result_.actualEndMs) < end_;
        detail::MessageWriter::littleLong(output_, -1);
        detail::MessageWriter::littleLong(output_, -1);
        if (!output_) throw std::runtime_error("Could not finish writing the demo");
        return result_;
    }
private:
    void makeMessage() {
        message_ = std::make_unique<detail::MessageWriter>(codec_);
        message_->longValue(ack_);
        commandsInMessage_ = 0;
    }
    void writeCommand(const std::string& command) {
        // The engine's reliable ring must never be overwritten before a snapshot.
        if (++commandsInMessage_ > 64 || command.size() >= 1024)
            throw std::runtime_error("Server commands exceed protocol-84 playback limits");
        message_->byte(kSvcServerCommand);
        message_->longValue(++commandSequence_);
        message_->string(command);
    }
    template<std::size_t N>
    static int lastChange(const std::array<std::int32_t, N>& a,
                          const std::array<std::int32_t, N>& b) {
        int last = 0;
        for (std::size_t i = 0; i < N; ++i) if (a[i] != b[i]) last = static_cast<int>(i + 1);
        return last;
    }
    static void floating(detail::MessageWriter& out, std::int32_t value) {
        float f = 0;
        std::memcpy(&f, &value, sizeof(f));
        if (std::isfinite(f) && f >= -4096 && f <= 4095 &&
            f == std::trunc(f) && !(f == 0 && std::signbit(f))) {
            out.bits(0, 1);
            out.bits(static_cast<int>(f) + kFloatIntBias, kFloatIntBits);
        } else {
            out.bits(1, 1);
            out.bits(value, 32);
        }
    }
    static void entity(detail::MessageWriter& out, const EntityState& from,
                       const EntityState* to, bool force) {
        if (!to) {
            out.bits(from.number, kEntityNumberBits);
            out.bits(1, 1);
            return;
        }
        const int last = lastChange(from.values, to->values);
        if (!last && !force) return;
        out.bits(to->number, kEntityNumberBits);
        out.bits(0, 1);
        out.bits(last ? 1 : 0, 1);
        if (!last) return;
        out.byte(last);
        for (int i = 0; i < last; ++i) {
            const bool changed = from.values[i] != to->values[i];
            out.bits(changed, 1);
            if (!changed) continue;
            const auto value = to->values[i];
            out.bits(value != 0, 1);
            if (!value) continue;
            if (!kEntityFields[i].bits) floating(out, value);
            else out.bits(value, kEntityFields[i].bits);
        }
    }
    template<std::size_t N>
    static unsigned mask(const std::array<std::int32_t, N>& a,
                         const std::array<std::int32_t, N>& b, int offset = 0) {
        unsigned result = 0;
        for (int i = 0; i < 16; ++i) if (a[offset+i] != b[offset+i]) result |= 1u << i;
        return result;
    }
    template<std::size_t N>
    static void array(detail::MessageWriter& out, const std::array<std::int32_t, N>& a,
                      const std::array<std::int32_t, N>& b, int offset = 0, int width = 16) {
        const unsigned changed = mask(a, b, offset);
        out.bits(changed != 0, 1);
        if (!changed) return;
        out.shortValue(changed);
        for (int i = 0; i < 16; ++i) if (changed & (1u << i)) out.bits(b[offset+i], width);
    }
    static void player(detail::MessageWriter& out, const PlayerState& from, const PlayerState& to) {
        const int last = lastChange(from.values, to.values);
        out.byte(last);
        for (int i = 0; i < last; ++i) {
            const bool changed = from.values[i] != to.values[i];
            out.bits(changed, 1);
            if (!changed) continue;
            if (!kPlayerFields[i].bits) floating(out, to.values[i]);
            else out.bits(to.values[i], kPlayerFields[i].bits);
        }
        const bool arraysChanged = from.stats != to.stats || from.persistent != to.persistent ||
                                   from.holdable != to.holdable || from.powerups != to.powerups;
        out.bits(arraysChanged, 1);
        if (arraysChanged) {
            array(out, from.stats, to.stats);
            array(out, from.persistent, to.persistent);
            array(out, from.holdable, to.holdable);
            array(out, from.powerups, to.powerups, 0, 32);
        }
        const bool ammoChanged = from.ammo != to.ammo;
        out.bits(ammoChanged, 1);
        if (ammoChanged) for (int g = 0; g < 4; ++g) array(out, from.ammo, to.ammo, g * 16);
        for (int g = 0; g < 4; ++g) array(out, from.ammoClip, to.ammoClip, g * 16);
    }
    std::ostream& output_;
    const DemoCutOptions& options_;
    const detail::HuffmanDecoder& codec_;
    std::unique_ptr<detail::MessageWriter> message_;
    std::optional<Snapshot> previous_;
    std::vector<std::string> pendingBig_;
    DemoCutResult result_;
    std::int64_t start_ = 0, end_ = 0;
    int firstSourceTime_ = -1, lastSourceTime_ = -1;
    int ack_ = 0, sequence_ = 1, commandSequence_ = 0, previousSequence_ = -1;
    int commandsInMessage_ = 0;
    bool done_ = false, haveGameState_ = false;
};
