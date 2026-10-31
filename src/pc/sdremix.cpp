/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * SD Remix characters (sdremix.games): the balance mod's fighter files,
 * overlaid on the disc's when the setting is on, plus its code changes
 * (the ftSdr_* / pc_sdremix_active() checks in src/melee).
 *
 * The files are SD Remix's own (its repository's chars/ folder, for NTSC
 * 1.02): PlCa.dat, PlDk.dat, ... and the Kirby copy files. They are not
 * part of this repository; the player puts them in an "sdremix" folder,
 * looked for in:
 *   MELEE_SDREMIX_DIR (melee-env.txt)
 *   <user data folder>/sdremix   (%APPDATA%\melee-pc\sdremix on Windows)
 *   next to melee.exe, or one or two folders up from it
 *
 * The setting is taken once, at boot (pc_sdremix_init, before the game
 * loads anything): fighter files are cached and preloaded, so switching
 * mid-run would mix the two. In netplay both peers must agree; the
 * handshake compares it (net_handshake.c, unlock_hash_now).
 */
#include "paths.h"
#include "pc.h"

#include <aurora/dvd.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

/* Every fighter file SD Remix changes (its chars/ folder). */
const char* const k_files[] = {
    "PlCa.dat",
    "PlCl.dat",
    "PlCo.dat",
    "PlDk.dat",
    "PlDr.dat",
    "PlFe.dat",
    "PlGn.dat",
    "PlGw.dat",
    "PlKb.dat",
    "PlKbCpCl.dat",
    "PlKbCpLg.dat",
    "PlKbCpLk.dat",
    "PlKbCpNs.dat",
    "PlKbCpPc.dat",
    "PlKbCpSs.dat",
    "PlKp.dat",
    "PlLg.dat",
    "PlLk.dat",
    "PlMr.dat",
    "PlMs.dat",
    "PlMt.dat",
    "PlNn.dat",
    "PlNs.dat",
    "PlPc.dat",
    "PlPk.dat",
    "PlPp.dat",
    "PlSk.dat",
    "PlSs.dat",
    "PlYs.dat",
    "PlZd.dat",
};

struct Overlay {
    std::string disc_name; /* "/PlCa.dat" */
    std::string host_path;
};

std::vector<Overlay> s_overlays;
bool s_active = false;

void* overlay_open(void* userdata) {
    const auto* ov = static_cast<const Overlay*>(userdata);
    return std::fopen(ov->host_path.c_str(), "rb");
}

void overlay_close(void* handle) {
    if (handle != nullptr) {
        std::fclose(static_cast<FILE*>(handle));
    }
}

int64_t overlay_read(void* handle, uint8_t* buf, size_t len) {
    if (handle == nullptr) {
        return -1;
    }
    return static_cast<int64_t>(std::fread(buf, 1, len, static_cast<FILE*>(handle)));
}

int64_t overlay_seek(void* handle, int64_t offset, int32_t whence) {
    auto* f = static_cast<FILE*>(handle);
    /* fseek takes a long; the fighter files are well under 2 GB. */
    if (f == nullptr || std::fseek(f, static_cast<long>(offset), whence) != 0) {  // NOLINT
        return -1;
    }
    return std::ftell(f);
}

bool file_size(const std::string& path, size_t* size) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
        return false;
    }
    *size = static_cast<size_t>(info.size);
    return true;
}

bool is_dir(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::string find_folder() {
    std::vector<std::string> candidates;
    const char* env = std::getenv("MELEE_SDREMIX_DIR");
    if (env != nullptr && env[0] != '\0') {
        candidates.emplace_back(env);
    }
    if (char* pref = pc_get_pref_path()) {
        candidates.push_back(std::string(pref) + "sdremix");
        SDL_free(pref);
    }
    if (const char* base = SDL_GetBasePath()) {
        candidates.push_back(std::string(base) + "sdremix");
        candidates.push_back(std::string(base) + "../sdremix");
        candidates.push_back(std::string(base) + "../../sdremix");
    }
    for (const std::string& dir : candidates) {
        if (is_dir(dir)) {
            return dir;
        }
    }
    return "";
}

}  // namespace

extern "C" bool pc_sdremix_wanted(void);
extern "C" void ftSdr_PatchTables(void); /* src/melee/ft/ftsdremix.c */

extern "C" void pc_sdremix_init(void) {
    s_active = false;
    s_overlays.clear();
    if (!pc_sdremix_wanted()) {
        return;
    }
    const std::string dir = find_folder();
    if (dir.empty()) {
        pc_log_line("sdremix: on, but no sdremix folder was found; playing vanilla characters");
        return;
    }
    for (const char* name : k_files) {
        std::string path = dir + "/" + name;
        size_t size = 0;
        if (file_size(path, &size) && size > 0) {
            s_overlays.push_back({std::string("/") + name, path});
        }
    }
    if (s_overlays.empty()) {
        pc_log_line(
            "sdremix: %s has none of the fighter files; playing vanilla characters", dir.c_str());
        return;
    }
    std::vector<AuroraOverlayFile> files;
    files.reserve(s_overlays.size());
    for (Overlay& ov : s_overlays) {
        size_t size = 0;
        file_size(ov.host_path, &size);
        files.push_back({ov.disc_name.c_str(), &ov, size});
    }
    static const AuroraOverlayCallbacks callbacks = {
        overlay_open, overlay_close, overlay_read, overlay_seek};
    aurora_dvd_overlay_callbacks(&callbacks);
    std::vector<s32> entry_nums(files.size(), -1);
    aurora_dvd_overlay_files(files.data(), files.size(), entry_nums.data());
    size_t taken = 0;
    for (size_t i = 0; i < entry_nums.size(); ++i) {
        if (entry_nums[i] >= 0) {
            ++taken;
        } else {
            pc_log_line("sdremix: %s was not accepted", s_overlays[i].disc_name.c_str());
        }
    }
    s_active = taken > 0;
    ftSdr_PatchTables();
    pc_log_line("sdremix: %zu of %zu fighter files from %s", taken,
        sizeof k_files / sizeof *k_files, dir.c_str());
}

extern "C" bool pc_sdremix_active(void) {
    return s_active;
}
