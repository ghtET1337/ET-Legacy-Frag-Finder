// SPDX-License-Identifier: GPL-3.0-or-later
#include "realtime_capture.hpp"

#include <algorithm>
#include <sstream>

namespace etlfrag {
namespace {

std::wstring quoted(const std::filesystem::path& path) {
    const std::wstring value = path.wstring();
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'\"');
    std::size_t slashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'\"') {
            escaped.append(slashes * 2 + 1, L'\\');
            escaped.push_back(L'\"');
            slashes = 0;
            continue;
        }
        escaped.append(slashes, L'\\');
        slashes = 0;
        escaped.push_back(character);
    }
    escaped.append(slashes * 2, L'\\');
    escaped.push_back(L'\"');
    return escaped;
}

std::wstring qualityVideoArguments(
    RealtimeCaptureEncoder encoder,
    RealtimeCaptureQuality quality,
    int frameRate) {
    const int gop = std::max(30, frameRate * 2);
    const bool highFrameRate = frameRate >= 240;
    std::wostringstream arguments;
    switch (encoder) {
        case RealtimeCaptureEncoder::NvidiaNvenc:
            if (highFrameRate && quality == RealtimeCaptureQuality::Maximum) {
                arguments << L"-preset p4 -tune hq -rc vbr -cq 13 -b:v 0 "
                             L"-multipass qres -spatial_aq 1 -rc-lookahead 8 -bf 2 ";
            } else if (highFrameRate && quality == RealtimeCaptureQuality::High) {
                arguments << L"-preset p3 -tune hq -rc vbr -cq 16 -b:v 0 "
                             L"-spatial_aq 1 -rc-lookahead 0 -bf 0 ";
            } else if (highFrameRate) {
                arguments << L"-preset p2 -tune hq -rc vbr -cq 20 -b:v 0 "
                             L"-rc-lookahead 0 -bf 0 ";
            } else if (quality == RealtimeCaptureQuality::Maximum) {
                arguments << L"-preset p6 -tune hq -rc vbr -cq 12 -b:v 0 "
                             L"-multipass fullres -spatial_aq 1 -temporal_aq 1 "
                             L"-rc-lookahead 32 -bf 3 ";
            } else if (quality == RealtimeCaptureQuality::High) {
                arguments << L"-preset p5 -tune hq -rc vbr -cq 16 -b:v 0 "
                             L"-multipass qres -spatial_aq 1 -temporal_aq 1 "
                             L"-rc-lookahead 20 -bf 3 ";
            } else {
                arguments << L"-preset p4 -tune hq -rc vbr -cq 20 -b:v 0 "
                             L"-spatial_aq 1 -bf 2 ";
            }
            break;
        case RealtimeCaptureEncoder::AmdAmf:
            arguments << (highFrameRate ? L"-quality speed -rc cqp "
                                        : L"-quality quality -rc cqp ");
            if (quality == RealtimeCaptureQuality::Maximum) {
                arguments << L"-qp_i 12 -qp_p 14 -qp_b 16 ";
            } else if (quality == RealtimeCaptureQuality::High) {
                arguments << L"-qp_i 16 -qp_p 18 -qp_b 20 ";
            } else {
                arguments << L"-qp_i 20 -qp_p 22 -qp_b 24 ";
            }
            break;
        case RealtimeCaptureEncoder::IntelQsv:
            if (highFrameRate) {
                arguments << (quality == RealtimeCaptureQuality::Maximum
                                  ? L"-preset medium -global_quality 15 "
                                  : quality == RealtimeCaptureQuality::High
                                        ? L"-preset faster -global_quality 18 "
                                        : L"-preset veryfast -global_quality 22 ");
            } else {
                arguments << (quality == RealtimeCaptureQuality::Maximum
                                  ? L"-preset slower -global_quality 13 -look_ahead 1 "
                                  : quality == RealtimeCaptureQuality::High
                                        ? L"-preset slow -global_quality 17 -look_ahead 1 "
                                        : L"-preset medium -global_quality 21 ");
            }
            break;
        case RealtimeCaptureEncoder::SoftwareX264:
            if (highFrameRate) {
                arguments << (quality == RealtimeCaptureQuality::Maximum
                                  ? L"-preset ultrafast -crf 14 "
                                  : quality == RealtimeCaptureQuality::High
                                        ? L"-preset ultrafast -crf 17 "
                                        : L"-preset ultrafast -crf 21 ");
            } else {
                arguments << (quality == RealtimeCaptureQuality::Maximum
                                  ? L"-preset fast -crf 12 "
                                  : quality == RealtimeCaptureQuality::High
                                        ? L"-preset veryfast -crf 16 "
                                        : L"-preset superfast -crf 20 ");
            }
            break;
        case RealtimeCaptureEncoder::Auto:
            break;
    }
    arguments << L"-g " << gop << L" -keyint_min " << frameRate
              << L" -profile:v high";
    return arguments.str();
}

int audioBitrate(RealtimeCaptureQuality quality) {
    switch (quality) {
        case RealtimeCaptureQuality::Maximum: return 320;
        case RealtimeCaptureQuality::High: return 256;
        case RealtimeCaptureQuality::Balanced: return 192;
    }
    return 256;
}

} // namespace

std::wstring realtimeCaptureEncoderName(RealtimeCaptureEncoder encoder) {
    switch (encoder) {
        case RealtimeCaptureEncoder::Auto: return L"Auto (NVENC / AMF / Quick Sync / x264)";
        case RealtimeCaptureEncoder::NvidiaNvenc: return L"NVIDIA NVENC";
        case RealtimeCaptureEncoder::AmdAmf: return L"AMD AMF";
        case RealtimeCaptureEncoder::IntelQsv: return L"Intel Quick Sync";
        case RealtimeCaptureEncoder::SoftwareX264: return L"Software x264";
    }
    return L"Unknown";
}

std::wstring realtimeCaptureQualityName(RealtimeCaptureQuality quality) {
    switch (quality) {
        case RealtimeCaptureQuality::Maximum: return L"Maximum quality";
        case RealtimeCaptureQuality::High: return L"High quality";
        case RealtimeCaptureQuality::Balanced: return L"Balanced";
    }
    return L"High quality";
}

std::wstring realtimeCapturePacingName(RealtimeCapturePacing pacing) {
    switch (pacing) {
        case RealtimeCapturePacing::SmoothCfr:
            return L"Smooth constant FPS (recommended)";
        case RealtimeCapturePacing::SourceVfr:
            return L"Only new desktop frames (VFR)";
    }
    return L"Smooth constant FPS (recommended)";
}

std::wstring realtimeCaptureEncoderCodec(RealtimeCaptureEncoder encoder) {
    switch (encoder) {
        case RealtimeCaptureEncoder::NvidiaNvenc: return L"h264_nvenc";
        case RealtimeCaptureEncoder::AmdAmf: return L"h264_amf";
        case RealtimeCaptureEncoder::IntelQsv: return L"h264_qsv";
        case RealtimeCaptureEncoder::SoftwareX264: return L"libx264";
        case RealtimeCaptureEncoder::Auto: return {};
    }
    return {};
}

bool isRealtimeDesktopDuplicationLoss(std::string_view logText) {
    return logText.find("AcquireNextFrame failed: 887a0026") !=
               std::string_view::npos ||
           logText.find("AcquireNextFrame failed: 887A0026") !=
               std::string_view::npos;
}

std::optional<std::wstring> validateRealtimeCaptureSettings(
    const RealtimeCaptureSettings& settings) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(settings.ffmpegExecutable, error) || error) {
        return L"Locate a valid ffmpeg.exe before starting real-time capture.";
    }
    if (settings.outputFolder.empty()) {
        return L"Choose an output folder for real-time recordings.";
    }
    if (settings.displayIndex < 0 || settings.displayIndex > 31) {
        return L"The selected DXGI display index is invalid.";
    }
    if (settings.frameRate < 30 || settings.frameRate > 1000) {
        return L"Capture FPS must be between 30 and 1000.";
    }
    return std::nullopt;
}

std::wstring buildRealtimeCaptureArguments(
    const RealtimeCaptureSettings& settings,
    RealtimeCaptureEncoder resolvedEncoder,
    const std::optional<RealtimeAudioInput>& audio,
    const std::filesystem::path& temporaryMkv) {
    const std::wstring codec = realtimeCaptureEncoderCodec(resolvedEncoder);
    std::wostringstream arguments;
    arguments << L"-hide_banner -nostats -stats_period 1 -progress pipe:2 -y ";
    if (audio.has_value()) {
        arguments << L"-thread_queue_size 8192 -f " << audio->sampleFormat
                  << L" -ar " << audio->sampleRate
                  << L" -ac " << audio->channels
                  << L" -i " << quoted(std::filesystem::path(audio->pipeName)) << L' ';
    }
    arguments << L"-filter_complex \"ddagrab=output_idx=" << settings.displayIndex
              << L":draw_mouse=" << (settings.drawCursor ? 1 : 0)
              << L":framerate=" << settings.frameRate
              << L":dup_frames=0";
    if (resolvedEncoder != RealtimeCaptureEncoder::NvidiaNvenc) {
        arguments << L",hwdownload,format=bgra";
        if (resolvedEncoder == RealtimeCaptureEncoder::SoftwareX264) {
            arguments << L",format=yuv420p";
        } else {
            arguments << L",format=nv12";
        }
    }
    arguments << L",setpts=PTS-STARTPTS[v]\" -map \"[v]\" ";
    if (audio.has_value()) arguments << L"-map 0:a:0 ";
    arguments << L"-c:v " << codec << L' '
              << qualityVideoArguments(resolvedEncoder, settings.quality, settings.frameRate);
    if (settings.pacing == RealtimeCapturePacing::SmoothCfr) {
        // Keep ddagrab source-faithful so FFmpeg's CFR stage can count every
        // paced duplicate in its progress telemetry. This makes playback
        // stable without pretending those repeated frames came from the game.
        arguments << L" -r " << settings.frameRate << L" -fps_mode cfr ";
    } else {
        arguments << L" -fps_mode passthrough ";
    }
    if (audio.has_value()) {
        arguments << L"-c:a aac -b:a " << audioBitrate(settings.quality)
                  << L"k -ar 48000 -ac 2 "
                     L"-af \"asetpts=PTS-STARTPTS,aresample=async=1:first_pts=0\" ";
    }
    arguments << L"-max_interleave_delta 0 -flush_packets 1 -f matroska "
              << quoted(temporaryMkv);
    return arguments.str();
}

std::wstring buildRealtimeRemuxArguments(
    const std::filesystem::path& temporaryMkv,
    const std::filesystem::path& finalMp4) {
    return L"-hide_banner -loglevel warning -y -i " + quoted(temporaryMkv) +
           L" -map 0 -c copy -movflags +faststart " + quoted(finalMp4);
}

std::wstring buildRealtimeEncoderProbeArguments(RealtimeCaptureEncoder encoder) {
    return L"-hide_banner -loglevel error -f lavfi -i "
           L"\"color=c=black:s=640x360:r=60\" -frames:v 1 -c:v " +
           realtimeCaptureEncoderCodec(encoder) + L" -f null NUL";
}

} // namespace etlfrag
