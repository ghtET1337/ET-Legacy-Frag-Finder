// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_cut.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace etlfrag {
namespace detail {
DemoCutResult writeDemoCut(const std::filesystem::path&, std::ostream&, const DemoCutOptions&);
}
DemoCutResult cutDemo(const std::filesystem::path& source,
                      const std::filesystem::path& destination,
                      const DemoCutOptions& options) {
    if (options.actionStartMs < 0 || options.actionEndMs < options.actionStartMs ||
        options.beforeMs < 0 || options.afterMs < 0 ||
        std::int64_t(options.actionEndMs) + options.afterMs > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("Invalid cut time range");
    auto extension = [](const std::filesystem::path& p) {
        std::string value = p.extension().u8string();
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    };
    if (extension(source) != ".dm_84" || extension(destination) != ".dm_84")
        throw std::runtime_error("Cut demo supports .dm_84 client demos only");
    if (!std::filesystem::is_regular_file(source)) throw std::runtime_error("Source demo does not exist");
    if (std::filesystem::exists(destination))
        throw std::runtime_error("The output file already exists. Choose a new name; existing demos are never overwritten.");
    const auto parent = std::filesystem::absolute(destination).parent_path();
    if (!std::filesystem::is_directory(parent)) throw std::runtime_error("The destination folder does not exist");
    // Reserve an exclusive temporary directory on the destination filesystem.
    std::filesystem::path tempDir;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = parent / (".etl-cut-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) { tempDir = candidate; break; }
        if (ec) throw std::runtime_error("Could not create the cut output (check folder permissions)");
    }
    if (tempDir.empty()) throw std::runtime_error("Could not reserve a temporary cut file");
    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    } cleanup{tempDir};
    const auto tempFile = tempDir / "clip.dm_84";
    DemoCutResult result;
    {
        std::ofstream output(tempFile, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Could not open the cut output");
        result = detail::writeDemoCut(source, output, options);
        output.flush();
        if (!output) throw std::runtime_error("Could not save the cut demo (check free disk space)");
        output.close();
        if (!output) throw std::runtime_error("Could not close the cut output");
    }
    if (options.cancel && options.cancel->load()) throw std::runtime_error("Demo cut cancelled");
    result.bytes = std::filesystem::file_size(tempFile);
#ifdef _WIN32
    // Flags=0 deliberately refuses an existing destination, even if another
    // process created it after the initial check. Also works on FAT/exFAT.
    if (!MoveFileExW(tempFile.c_str(), destination.c_str(), 0))
        throw std::runtime_error("Could not commit the cut demo. Choose a new filename and a writable folder.");
#else
    std::error_code ec;
    std::filesystem::create_hard_link(tempFile, destination, ec);
    if (ec) throw std::runtime_error("Could not commit the cut demo: " + ec.message());
#endif
    return result;
}
} // namespace etlfrag
