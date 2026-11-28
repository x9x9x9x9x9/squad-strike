/* SPDX-License-Identifier: GPL-3.0-or-later */
/* "Other HD textures": the Melee HD texture pack (assets.melee.tv, by the
 * Melee HD team), a Dolphin pack of HD menus, HUD, effects, stages and some
 * character textures. It is not ours to ship, so the launcher downloads it
 * from its own MediaFire page when the player asks, or installs a zip the
 * player downloaded, into <prefs>/hd-textures. textures.cpp loads it there,
 * under the player's own custom textures. */
#pragma once
#include "paths.h"
#include <SDL3/SDL_stdinc.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pc::hdpack {

enum class Status { Missing, Installed, Downloading, Installing, Failed };

struct State {
    Status status = Status::Missing;
    std::string message;               /* for the status line */
    std::uint64_t total = 0, done = 0; /* download bytes; total 0 when unknown */
};

/* The pack's download page, for the browser. */
const char* page_url();
/* Where the pack is unpacked: <prefs>/hd-textures. Inline, with installed(),
 * so textures.cpp needs no downloader (the browser build has none). */
inline std::filesystem::path dir() {
    char* pref = pc_get_pref_path();
    std::filesystem::path out = pref ? std::filesystem::u8path(pref) : std::filesystem::path();
    SDL_free(pref);
    return out / "hd-textures";
}
/* dir() as UTF-8, for logs and URLs. */
inline std::string dir_utf8() {
    const auto u = dir().u8string();
    return std::string(u.begin(), u.end());
}
/* An install finished there: install writes this marker last. */
inline bool installed() {
    std::error_code ec;
    return std::filesystem::exists(dir() / ".installed", ec);
}
/* Download the pack from its page and install it, in the background. */
void download_async();
/* Install a pack zip the player downloaded, in the background. */
void install_zip_async(const std::filesystem::path& zip);
/* Stop a download. */
void cancel();
State state();
/* Stop any download and wait for the worker, for shutdown. */
void shutdown();

/* ---- exposed for tools/test_updater.cpp ---- */
/* The direct download link in a MediaFire file page, or "". */
std::string find_download_link(const std::string& html);

}  // namespace pc::hdpack
