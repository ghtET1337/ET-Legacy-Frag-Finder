// SPDX-License-Identifier: GPL-3.0-or-later
#include "clip_export.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    etlfrag::ClipSource source;
    source.demoPath = std::filesystem::path(L"C:/demos/test demo.dm_84");
    source.label = L"GOAT/^1ght! 4K";
    source.actionStartMs = 13000;
    source.actionEndMs = 17250;

    etlfrag::ClipExportSettings settings;
    settings.preRollMs = 5000;
    settings.postRollMs = 3000;
    settings.frameRate = 60;
    settings.outputFolder = L"C:/clips";
    settings.etlHomeFolder = L"C:/Users/test/Documents/ETLegacy";

    const etlfrag::ClipRange range = etlfrag::calculateClipRange(source, settings);
    check(range.startMs == 8000, "pre-roll must be subtracted from action start");
    check(range.endMs == 20250, "post-roll must be added to action end");
    check(range.frameCount == 735, "frame count must cover the exact range at 60 FPS");

    const std::wstring base = etlfrag::makeSafeClipBaseName(source, range, 42);
    check(base.find(L' ') == std::wstring::npos, "clip name must not contain spaces");
    check(base.find(L';') == std::wstring::npos, "clip name must be command-safe");

    const std::wstring compatible = etlfrag::buildRangeAction(base, range, settings);
    check(compatible.find(L"seek 8.000") != std::wstring::npos,
          "compatible action must seek to the range start");
    check(compatible.find(L"set timescale 1; set cl_aviFrameRate 60; video-pipe") !=
              std::wstring::npos,
          "compatible action must force normal demo time and FPS after loading");
    check(compatible.find(L"wait 1470") != std::wstring::npos,
          "compatible action must use two command-buffer waits per captured frame");
    check(compatible.find(L"stopvideo; quit") != std::wstring::npos,
          "compatible action must close the pipe and ETL");

    settings.engineMode = etlfrag::ClipEngineMode::NativeVideoPipeRange;
    const std::wstring native = etlfrag::buildRangeAction(base, range, settings);
    check(native.find(L"video-pipe-range ") == 0,
          "native action must call video-pipe-range");
    check(native.find(L" 8.000 20.250") != std::wstring::npos,
          "native action must pass start and end seconds");

    const std::wstring args = etlfrag::buildEtlClipArguments(source, base, range, settings);
    check(args.find(L"+set s_initsound 1") != std::wstring::npos,
          "audio capture must force the SDL-compatible sound mode");
    check(args.find(L"+set s_muteWhenUnfocused 0") != std::wstring::npos,
          "audio must not be muted when the render window loses focus");
    check(args.find(L"+set cl_aviPipeFormat") != std::wstring::npos,
          "ffmpeg encoding settings must be passed to ETL");
    check(args.find(L"+set timescale 1") != std::wstring::npos,
          "clip rendering must use normal demo time");
    const std::wstring pipeFormat = etlfrag::buildPipeFormat(settings);
    check(pipeFormat.find(L"-movflags faststart") != std::wstring::npos,
          "MP4 fast-start must remain enabled");
    check(pipeFormat.find(L"+faststart") == std::wstring::npos,
          "pipe format must not contain a plus command separator");
    check(args.find(L"+set r_customwidth 1920") != std::wstring::npos,
          "resolution must be passed to ETL");
    check(args.find(
              L"+set fs_homepath \"C:/Users/test/Documents/ETLegacy\"") !=
              std::wstring::npos,
          "render launch must explicitly use the selected ETL user-data folder");

    const std::wstring configuredArgs = etlfrag::buildEtlClipArguments(
        source,
        base,
        range,
        settings,
        L"fragmovie");
    const std::size_t profilePosition = configuredArgs.find(
        L"+set cl_profile \"fragmovie\"");
    const std::size_t renderPosition = configuredArgs.find(L"+set s_initsound 1");
    const std::size_t homePosition = configuredArgs.find(L"+set fs_homepath");
    const std::size_t gamePosition = configuredArgs.find(L"+set fs_game");
    check(homePosition != std::wstring::npos && gamePosition > homePosition &&
              profilePosition > gamePosition && renderPosition > profilePosition,
          "the selected profile must load before render-specific overrides");
    check(configuredArgs.find(L"+exec") == std::wstring::npos &&
              configuredArgs.find(L"ff_render_") == std::wstring::npos,
          "rendering must use the selected profile directly without +exec or generated profiles");
    check(configuredArgs.find(L"+set fs_game \"legacy\"") != std::wstring::npos,
          "clip launch must select the mod that owns the source demo");
    check(configuredArgs.find(L"+vid_restart +set activeAction") != std::wstring::npos,
          "render overrides must be latched before the demo action starts");

    const std::wstring playbackArgs = etlfrag::buildEtlPlaybackArguments(
        source.demoPath,
        settings.etlHomeFolder,
        L"destiny",
        8000);
    const std::size_t playbackHome = playbackArgs.find(L"+set fs_homepath");
    const std::size_t playbackGame = playbackArgs.find(L"+set fs_game");
    const std::size_t playbackProfile = playbackArgs.find(L"+set cl_profile");
    const std::size_t playbackSeek = playbackArgs.find(
        L"+set activeAction \"seek 8.000\"");
    const std::size_t playbackDemo = playbackArgs.find(L"+demo");
    check(playbackHome != std::wstring::npos && playbackGame > playbackHome &&
              playbackProfile > playbackGame && playbackSeek > playbackProfile &&
              playbackDemo > playbackSeek,
          "automatic seek must retain fs_homepath, mod and the selected profile");
    check(playbackArgs.find(L"+exec") == std::wstring::npos &&
              playbackArgs.find(L"ff_play_") == std::wstring::npos,
          "playback must not stage +exec CFGs or create generated profile names");

    const std::wstring noSeekArgs = etlfrag::buildEtlPlaybackArguments(
        source.demoPath,
        settings.etlHomeFolder,
        L"destiny",
        std::nullopt);
    check(noSeekArgs.find(L"activeAction") == std::wstring::npos &&
              noSeekArgs.find(L"+set cl_profile \"destiny\"") !=
                  std::wstring::npos,
          "no-seek playback must still retain the selected profile");

    const std::filesystem::path launchFixture =
        std::filesystem::path("build") / "etl-launch-fixture";
    const std::filesystem::path home = launchFixture / "ETLegacy";
    const std::filesystem::path sourceProfile =
        home / "legacy" / "profiles" / "destiny";
    const std::filesystem::path fixtureDemo =
        home / "legacy" / "demos" / "2026-08" / "fixture.dm_84";
    const std::filesystem::path fixtureConfig = launchFixture / "movie.cfg";
    std::error_code fixtureError;
    std::filesystem::remove_all(launchFixture, fixtureError);
    fixtureError.clear();
    std::filesystem::create_directories(sourceProfile, fixtureError);
    std::filesystem::create_directories(
        home / "legacy" / "profiles" / "empty-profile", fixtureError);
    std::filesystem::create_directories(
        home / "legacy" / "profiles" / "ff_play_old_1234", fixtureError);
    std::filesystem::create_directories(
        home / "legacy" / "profiles" / "ff_render_old_5678", fixtureError);
    std::filesystem::create_directories(fixtureDemo.parent_path(), fixtureError);
    {
        std::ofstream profileConfig(sourceProfile / "etconfig.cfg");
        profileConfig << "seta cg_fov 100\n";
    }
    {
        std::ofstream movieConfig(fixtureConfig);
        movieConfig << "seta r_picmip 0\n";
    }
    check(etlfrag::inferDemoModName(fixtureDemo) == L"legacy",
          "demo mod detection must support dated subfolders below demos");
    const std::vector<etlfrag::EtlProfileInfo> profiles =
        etlfrag::discoverEtlProfiles(home);
    check(profiles.size() == 1 && profiles.front().name == L"destiny" &&
              profiles.front().modName == L"legacy",
          "profile discovery must require etconfig.cfg and find valid mod profiles");
    const std::filesystem::path mistakenInstallHome = launchFixture / "etl-install";
    std::filesystem::create_directories(
        mistakenInstallHome / "legacy", fixtureError);
    check(etlfrag::selectEtlUserDataFolder(
              {}, mistakenInstallHome, home) == home,
          "a legacy installation-folder setting must migrate to Documents when profiles live there");
    const std::filesystem::path explicitCustomHome = launchFixture / "custom-user-data";
    check(etlfrag::selectEtlUserDataFolder(
              explicitCustomHome, mistakenInstallHome, home) == explicitCustomHome,
          "an explicit user-data setting must remain authoritative");
    std::wstring launchProfile;
    std::filesystem::path previousConfigBackup;
    std::wstring preparationError;
    check(etlfrag::prepareEtlLaunchProfile(
              home,
              fixtureDemo,
              sourceProfile,
              fixtureConfig,
              launchProfile,
              previousConfigBackup,
              preparationError),
          "selected profile and CFG preparation must succeed");
    check(launchProfile == L"destiny",
          "ETL must launch the exact profile selected by the user");
    check(previousConfigBackup.filename().wstring().find(
              L"etconfigBEFOREfragfinder-") == 0 &&
              std::filesystem::is_regular_file(previousConfigBackup),
          "the previous etconfig.cfg must receive a timestamped backup");
    check(readFile(previousConfigBackup) == "seta cg_fov 100\n",
          "the timestamped backup must preserve the previous profile config");
    check(readFile(sourceProfile / "etconfig.cfg") == "seta r_picmip 0\n",
          "the selected movie CFG must become the profile's etconfig.cfg");
    const std::filesystem::path firstBackup = previousConfigBackup;
    check(etlfrag::prepareEtlLaunchProfile(
              home,
              fixtureDemo,
              sourceProfile,
              fixtureConfig,
              launchProfile,
              previousConfigBackup,
              preparationError) && previousConfigBackup.empty(),
          "an identical already-installed CFG must not create another backup");
    check(std::filesystem::is_regular_file(firstBackup),
          "the original timestamped backup must remain available");
    check(!etlfrag::prepareEtlLaunchProfile(
              home,
              fixtureDemo,
              {},
              fixtureConfig,
              launchProfile,
              previousConfigBackup,
              preparationError) &&
              preparationError.find(L"Select an ETL profile") != std::wstring::npos,
          "a CFG replacement must require an explicitly selected profile");
    const std::filesystem::path wrongModProfile =
        home / "nitmod" / "profiles" / "destiny";
    std::filesystem::create_directories(wrongModProfile, fixtureError);
    {
        std::ofstream wrongConfig(wrongModProfile / "etconfig.cfg");
        wrongConfig << "seta cg_fov 90\n";
    }
    check(!etlfrag::prepareEtlLaunchProfile(
              home,
              fixtureDemo,
              wrongModProfile,
              {},
              launchProfile,
              previousConfigBackup,
              preparationError) &&
              preparationError.find(L"requires the 'legacy' mod") != std::wstring::npos,
          "a profile from another mod must not be used for the demo");
    const std::vector<std::filesystem::path> generatedProfiles =
        etlfrag::discoverLegacyFragFinderProfiles(home);
    check(generatedProfiles.size() == 2,
          "only old ff_play and ff_render profile folders should be offered for cleanup");
    fixtureError.clear();
    std::filesystem::remove_all(launchFixture, fixtureError);

    etlfrag::ClipSource early = source;
    early.actionStartMs = 2000;
    early.actionEndMs = 2500;
    const etlfrag::ClipRange earlyRange = etlfrag::calculateClipRange(early, settings);
    check(earlyRange.startMs == 0, "pre-roll must clamp at demo start");

    etlfrag::ClipSource reportedAction;
    reportedAction.demoPath = source.demoPath;
    reportedAction.label = L"bv~ght! 3K";
    reportedAction.actionStartMs = 84940;
    reportedAction.actionEndMs = 90860;
    settings.engineMode = etlfrag::ClipEngineMode::CompatibleVideoPipe;
    settings.preRollMs = 5000;
    settings.postRollMs = 3000;
    settings.frameRate = 60;
    const etlfrag::ClipRange reportedRange =
        etlfrag::calculateClipRange(reportedAction, settings);
    check(reportedRange.startMs == 79940 && reportedRange.endMs == 93860,
          "reported 3K must retain its complete pre/action/post range");
    check(reportedRange.frameCount == 836,
          "13.920 seconds at 60 FPS must require 836 output frames");
    const std::wstring reportedCommand =
        etlfrag::buildRangeAction(L"reported_3K", reportedRange, settings);
    check(reportedCommand.find(L"set cl_aviFrameRate 60") != std::wstring::npos &&
              reportedCommand.find(L"wait 1672") != std::wstring::npos,
          "reported 3K controller must wait two command passes for every output frame");

    const std::filesystem::path completeMp4 =
        std::filesystem::path("build") / "clip-export-complete.mp4";
    const std::filesystem::path incompleteMp4 =
        std::filesystem::path("build") / "clip-export-incomplete.mp4";
    const unsigned char completeBoxes[] = {
        0, 0, 0, 8, 'f', 't', 'y', 'p',
        0, 0, 0, 8, 'm', 'o', 'o', 'v',
        0, 0, 0, 8, 'm', 'd', 'a', 't'};
    const unsigned char incompleteBoxes[] = {
        0, 0, 0, 8, 'f', 't', 'y', 'p',
        0, 0, 0, 16, 'm', 'd', 'a', 't',
        1, 2, 3, 4, 5, 6, 7, 8};
    {
        std::ofstream output(completeMp4, std::ios::binary);
        output.write(reinterpret_cast<const char*>(completeBoxes), sizeof(completeBoxes));
    }
    {
        std::ofstream output(incompleteMp4, std::ios::binary);
        output.write(reinterpret_cast<const char*>(incompleteBoxes), sizeof(incompleteBoxes));
    }
    check(etlfrag::isCompleteMp4(completeMp4),
          "a finalized MP4 must contain top-level ftyp, moov and mdat boxes");
    check(!etlfrag::isCompleteMp4(incompleteMp4),
          "an mdat-only ffmpeg output must not be treated as a playable MP4");

    const std::wstring discordArguments = etlfrag::buildDiscordCopyArguments(
        L"C:/clips/master 120fps.mp4", L"C:/clips/master 120fps-discord.mp4");
    check(discordArguments.find(L"-map 0:a?") != std::wstring::npos,
          "Discord conversion must preserve optional game audio");
    check(discordArguments.find(L"scale=1920:1080") != std::wstring::npos &&
              discordArguments.find(L"fps=60") != std::wstring::npos &&
              discordArguments.find(L"-fps_mode cfr") != std::wstring::npos,
          "Discord conversion must create a 1080p constant-60-FPS stream");
    check(discordArguments.find(L"-c:v libx264") != std::wstring::npos &&
              discordArguments.find(L"-pix_fmt yuv420p") != std::wstring::npos &&
              discordArguments.find(L"-profile:v high -level:v 4.2") != std::wstring::npos,
          "Discord conversion must use a broadly compatible H.264 profile");
    check(discordArguments.find(L"-c:a aac") != std::wstring::npos &&
              discordArguments.find(L"-ar 48000") != std::wstring::npos &&
              discordArguments.find(L"-movflags faststart") != std::wstring::npos,
          "Discord conversion must use AAC 48 kHz and MP4 fast-start");
    check(discordArguments.find(L"\"C:/clips/master 120fps.mp4\"") != std::wstring::npos &&
              discordArguments.find(L"\"C:/clips/master 120fps-discord.mp4\"") !=
                  std::wstring::npos,
          "Discord conversion must quote input and output paths");
    std::error_code cleanupError;
    std::filesystem::remove(completeMp4, cleanupError);
    cleanupError.clear();
    std::filesystem::remove(incompleteMp4, cleanupError);

    if (const char* path = std::getenv("ETL_TEST_COMPLETE_MP4")) {
        check(etlfrag::isCompleteMp4(path),
              "the supplied real completed MP4 must pass structural validation");
    }
    if (const char* path = std::getenv("ETL_TEST_INCOMPLETE_MP4")) {
        check(!etlfrag::isCompleteMp4(path),
              "the supplied real interrupted MP4 must fail structural validation");
    }

    if (failures == 0) {
        std::cout << "All clip export tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
