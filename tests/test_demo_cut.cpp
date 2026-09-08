// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_cut.hpp"
#include "etl_demo_parser.hpp"
#include "idtech3_message_writer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
static void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static std::string bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
int main(int argc, char** argv) {
    check(argc == 2, "fixture path required");
    const fs::path fixture = fs::u8path(argv[1]);
    const auto original = bytes(fixture);
    const fs::path dir = fs::temp_directory_path() /
        ("etl-cut-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove_all(p,ec); } } cleanup{dir};
    auto output = [&](const char* name) { return dir / name; };
    auto fail = [&](const fs::path& source, const fs::path& dest, etlfrag::DemoCutOptions options) {
        bool thrown = false;
        try { (void)etlfrag::cutDemo(source, dest, options); }
        catch (const std::exception&) { thrown = true; }
        check(thrown, "invalid cut was accepted");
    };
    etlfrag::DemoCutOptions options;
    options.actionStartMs = 2500; options.actionEndMs = 4500;
    options.beforeMs = 300; options.afterMs = 2200;
    int progress = -1;
    options.progress = [&](int p) { check(p >= progress && p <= 100, "invalid progress"); progress = p; };
    auto result = etlfrag::cutDemo(fixture, output("action.dm_84"), options);
    check(result.actualStartMs == 2200 && result.actualEndMs == 6700 && result.snapshots == 46, "wrong cut range");
    check(!result.clampedToDemoEnd && result.bytes == fs::file_size(output("action.dm_84")), "wrong cut result");
    const auto parsed = etlfrag::DemoParser{}.parse(output("action.dm_84"), {true});
    check(parsed.warnings.empty() && parsed.firstServerTimeMs == 102200 && parsed.lastServerTimeMs == 106700, "invalid standalone demo");
    check(parsed.mapName == "oasis" && parsed.povClientNum == 0 && parsed.kills.size() == 3, "lost demo metadata/events");
    check(parsed.kills[0].demoTimeMs == 300 && parsed.kills[1].demoTimeMs == 2300, "event positions changed");
    check(parsed.protocolLog.find("checksumFeed=123456789") != std::string::npos, "lost checksum feed");
    check(parsed.protocolLog.find("first-middle-last") != std::string::npos, "split configstring was lost");
    check(parsed.protocolLog.find("deltaDistance=0") != std::string::npos, "missing initial full snapshot");
    options.progress = {};
    // Existing files, including the source and hardlink aliases, cannot change.
    fail(fixture, fixture, options);
    fail(fixture, output("action.dm_84"), options);
    check(bytes(fixture) == original, "source modified");
    fail(fixture, output("action.mp4"), options);
    auto invalid = options; invalid.beforeMs = -1; fail(fixture, output("negative.dm_84"), invalid);
    invalid = options; invalid.actionEndMs = 100; fail(fixture, output("backwards.dm_84"), invalid);
    invalid = options; invalid.actionStartMs = 25000; invalid.actionEndMs = 26000;
    fail(fixture, output("missing.dm_84"), invalid);
    check(!fs::exists(output("missing.dm_84")), "failed cut left a demo");
    // Clamp a valid action's tail to the available demo, never silently lose its kills.
    options.actionStartMs = 6500; options.actionEndMs = 6500; options.beforeMs = 0; options.afterMs = 30000;
    result = etlfrag::cutDemo(fixture, output("tail.dm_84"), options);
    check(result.actualStartMs == 6500 && result.actualEndMs == 10000 && result.clampedToDemoEnd, "bad end clamp");
    // Exact action times, zero margins and boundaries not aligned to snapshots.
    options.actionStartMs = options.actionEndMs = 2500; options.beforeMs = options.afterMs = 0;
    result = etlfrag::cutDemo(fixture, output("zero.dm_84"), options);
    check(result.actualStartMs == 2500 && result.actualEndMs == 2600 && result.snapshots == 2, "zero margins not playable");
    options.beforeMs = 5000; options.afterMs = 33;
    result = etlfrag::cutDemo(fixture, output("beginning.dm_84"), options);
    check(result.actualStartMs == 0 && result.actualEndMs == 2600, "beginning or snapshot rounding is wrong");
    // A cut demo can itself be cut again using its new local demo clock.
    options.actionStartMs = 300; options.actionEndMs = 2300; options.beforeMs = 100; options.afterMs = 100;
    result = etlfrag::cutDemo(output("action.dm_84"), output("recut.dm_84"), options);
    check(result.actualStartMs == 200 && result.actualEndMs == 2400, "re-cut offsets are wrong");
    // Cancellation and a concurrent destination creation must leave no partial output.
    std::atomic_bool cancel{false}; options.cancel = &cancel;
    options.progress = [&](int) { cancel = true; };
    fail(fixture, output("cancelled.dm_84"), options);
    check(!fs::exists(output("cancelled.dm_84")), "cancelled output remains");
    options.cancel = nullptr;
    options.progress = [&](int) { if (!fs::exists(output("race.dm_84"))) std::ofstream(output("race.dm_84")) << "KEEP"; };
    fail(fixture, output("race.dm_84"), options);
    check(bytes(output("race.dm_84")) == "KEEP", "concurrent file overwritten");
    options.progress = {};
    // Truncation inside the requested range fails; it cannot produce a short success.
    const auto truncated = output("truncated.dm_84");
    { std::ofstream out(truncated,std::ios::binary); out.write(original.data(), original.size()/2); }
    options.actionStartMs = 6500; options.actionEndMs = 6500; options.beforeMs = 5000; options.afterMs = 5000;
    fail(truncated, output("bad.dm_84"), options);
    check(!fs::exists(output("bad.dm_84")), "corrupt output remains");
    // Unsupported opcodes must be rejected, never silently copied into a new demo.
    { etlfrag::detail::HuffmanDecoder codec; etlfrag::detail::MessageWriter m(codec);
      m.longValue(0); m.byte(99); m.byte(8); std::ofstream out(output("unknown.dm_84"),std::ios::binary); m.packet(out,1); }
    fail(output("unknown.dm_84"), output("unknown-cut.dm_84"), options);
    for (const auto& entry : fs::directory_iterator(dir)) check(!entry.is_directory(), "temporary cut folder leaked");
    check(bytes(fixture) == original, "source changed after error handling");
    std::cout << "Demo cut: ranges, metadata, events, fragmented configs, re-cut, corruption, cancellation and no-overwrite checks passed.\n";
}
