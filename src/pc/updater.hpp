/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The launcher's updater: looks for a newer release of this mod on GitHub
 * and, where it can, installs it over this copy and restarts.
 *
 * Windows: the release zip is downloaded next to the program, unpacked, and
 * each file swapped in (a running program and its DLLs can be renamed, not
 * overwritten, so the old ones step aside as *.update-old and are removed at
 * the next start). Linux AppImage: the new image replaces the old one. Other
 * builds (Linux tar.gz, macOS) open the release page instead. Mobile and the
 * browser do not look. Settings, replays and the user folder are never in a
 * release package, so an update leaves them as they are. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pc::updater {

enum class Status { Idle, Checking, UpToDate, Available, Downloading, Installing, Ready, Failed };

struct State {
    Status status = Status::Idle;
    std::string message;               /* for the status line */
    std::string latest;                /* the newest release's tag, as v1.0.11 */
    std::string release_url;           /* its page */
    bool can_install = false;          /* this copy can update itself; otherwise the page */
    std::uint64_t total = 0, done = 0; /* download bytes */
};

/* This build looks for updates at all (desktop, with a network library). */
bool supported();
/* Ask GitHub for the newest release, in the background. */
void check_async();
/* Download and install the newest release, in the background. Ready when it
 * is in place; restart() then starts it. */
void install_async();
State state();
/* Start the installed update and end this process. */
[[noreturn]] void restart();
/* Remove what the last update left behind (old files it moved aside). */
void cleanup_previous();
/* Stop any download and wait for the worker, for shutdown. */
void shutdown();

/* ---- exposed for tools/test_updater.cpp ---- */
struct Asset {
    std::string name, url, sha256; /* sha256: hex, or "" when GitHub gave none */
    std::uint64_t size = 0;
};
struct Release {
    std::string tag, html_url;
    std::vector<Asset> assets;
};
bool parse_release(std::string_view json, Release& out);
/* The package for this platform, or nullptr. */
const Asset* pick_asset(const Release& r, std::string_view platform, std::string_view arch);
/* Unpack a zip into `dest`, dropping each entry's first path component (the
 * package's own folder). False with `error` set on anything unexpected. */
bool extract_zip(
    const std::filesystem::path& zip, const std::filesystem::path& dest, std::string& error);
std::string sha256_file(const std::filesystem::path& path);
std::string sha256_hex(const void* data, std::size_t size);

}  // namespace pc::updater
