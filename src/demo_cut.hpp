// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace etlfrag {

struct DemoCutOptions {
    std::int32_t actionStartMs = 0;
    std::int32_t actionEndMs = 0;
    std::int32_t beforeMs = 5000;
    std::int32_t afterMs = 3000;
    const std::atomic_bool* cancel = nullptr;
    std::function<void(int)> progress;
};

struct DemoCutResult {
    std::int32_t actualStartMs = 0;
    std::int32_t actualEndMs = 0;
    std::uint64_t snapshots = 0;
    std::uintmax_t bytes = 0;
    bool clampedToDemoEnd = false;
};

// Creates a NEW protocol-84 client demo. Never replaces an existing file,
// including the source. Throws on cancellation, corruption or unsupported data.
// Output is committed only after a complete cut has been written successfully.
DemoCutResult cutDemo(const std::filesystem::path& source,
                      const std::filesystem::path& destination,
                      const DemoCutOptions& options);

} // namespace etlfrag
