// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace etlfrag {

enum class RealtimeCaptureEncoder {
    Auto,
    NvidiaNvenc,
    AmdAmf,
    IntelQsv,
    SoftwareX264,
};

enum class RealtimeCaptureQuality {
    Maximum,
    High,
    Balanced,
};

enum class RealtimeCapturePacing {
    // Produces a constant-rate recording for predictable playback. Missing
    // desktop updates are repeated by FFmpeg and reported as paced frames.
    SmoothCfr,
    // Keeps only timestamps delivered by Desktop Duplication. This is useful
    // for measuring the real source cadence but some players handle VFR poorly.
    SourceVfr,
};

struct RealtimeAudioInput {
    std::wstring pipeName;
    std::wstring sampleFormat = L"f32le";
    int sampleRate = 48000;
    int channels = 2;
};

struct RealtimeCaptureSettings {
    int displayIndex = 0;
    int frameRate = 250;
    bool drawCursor = false;
    bool captureSystemAudio = true;
    RealtimeCaptureEncoder encoder = RealtimeCaptureEncoder::Auto;
    RealtimeCaptureQuality quality = RealtimeCaptureQuality::High;
    RealtimeCapturePacing pacing = RealtimeCapturePacing::SmoothCfr;
    std::filesystem::path outputFolder;
    std::filesystem::path ffmpegExecutable;
};

std::wstring realtimeCaptureEncoderName(RealtimeCaptureEncoder encoder);
std::wstring realtimeCaptureQualityName(RealtimeCaptureQuality quality);
std::wstring realtimeCapturePacingName(RealtimeCapturePacing pacing);
std::wstring realtimeCaptureEncoderCodec(RealtimeCaptureEncoder encoder);
bool isRealtimeDesktopDuplicationLoss(std::string_view logText);
std::optional<std::wstring> validateRealtimeCaptureSettings(
    const RealtimeCaptureSettings& settings);
std::wstring buildRealtimeCaptureArguments(
    const RealtimeCaptureSettings& settings,
    RealtimeCaptureEncoder resolvedEncoder,
    const std::optional<RealtimeAudioInput>& audio,
    const std::filesystem::path& temporaryMkv);
std::wstring buildRealtimeRemuxArguments(
    const std::filesystem::path& temporaryMkv,
    const std::filesystem::path& finalMp4);
std::wstring buildRealtimeEncoderProbeArguments(RealtimeCaptureEncoder encoder);

} // namespace etlfrag
