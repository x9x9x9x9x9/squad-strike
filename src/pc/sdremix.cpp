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
 *
 * The same boot step adds the ACE stages' files (src/melee/gr/grace.h) from
 * an "ace" folder, looked for in the same places (MELEE_ACE_DIR first), then
 * in resources/ace, which the builds ship: aurora takes one overlay set, so
 * both go in together.
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

std::string find_folder(const char* env_name, const char* name) {
    std::vector<std::string> candidates;
    const char* env = std::getenv(env_name);
    if (env != nullptr && env[0] != '\0') {
        candidates.emplace_back(env);
    }
    if (char* pref = pc_get_pref_path()) {
        candidates.push_back(std::string(pref) + name);
        SDL_free(pref);
    }
    if (const char* base = SDL_GetBasePath()) {
        candidates.push_back(std::string(base) + name);
        candidates.push_back(std::string(base) + "../" + name);
        candidates.push_back(std::string(base) + "../../" + name);
        /* The copy the release ships (resources/ace), after any the player
         * put next to the exe. */
        candidates.push_back(std::string(base) + "resources/" + name);
    }
    for (const std::string& dir : candidates) {
        if (is_dir(dir)) {
            return dir;
        }
    }
    return "";
}

/* The ACE stages' files: the stage files at the disc's root, the music in
 * audio/. Only these are taken from the folder, whatever else it holds. */
struct AceFile {
    const char* name;
    const char* disc_dir;
};
const AceFile k_ace_files[] = {
    {"GrOMc.dat", "/"},
    {"GrWw.dat", "/"},
    {"GrColosseum.dat", "/"},
    {"mcavern.hps", "/audio/"},
    {"wahwareinc.hps", "/audio/"},
    {"normalbattle.hps", "/audio/"},
    {"cipherpeon.hps", "/audio/"},
    {"battlemode1.hps", "/audio/"},
};

std::string s_ace_dir;
std::vector<std::string> s_ace_found;

/* The ACE folder's files: as named, or as ACE ships some music
 * ("CipherPeon.hps"); the folder may hold them either way. */
void find_ace_files() {
    s_ace_dir = find_folder("MELEE_ACE_DIR", "ace");
    s_ace_found.clear();
    if (s_ace_dir.empty()) {
        pc_log_line("ace: no ace folder; the ACE stages are not offered");
        return;
    }
    for (const AceFile& f : k_ace_files) {
        std::string path = s_ace_dir + "/" + f.name;
        size_t size = 0;
        if (!file_size(path, &size)) {
            /* ACE's own capitalisation of these two. */
            std::string alt = f.name;
            if (alt == "cipherpeon.hps") {
                alt = "CipherPeon.hps";
            } else if (alt == "battlemode1.hps") {
                alt = "BattleMode1.hps";
            }
            path = s_ace_dir + "/" + alt;
        }
        if (file_size(path, &size) && size > 0) {
            s_overlays.push_back({std::string(f.disc_dir) + f.name, path});
            s_ace_found.emplace_back(f.name);
        }
    }
    pc_log_line("ace: %zu of %zu files from %s", s_ace_found.size(),
        sizeof k_ace_files / sizeof *k_ace_files, s_ace_dir.c_str());
}

}  // namespace

extern "C" bool pc_sdremix_wanted(void);
extern "C" void ftSdr_PatchTables(void); /* src/melee/ft/ftsdremix.c */

extern "C" void pc_sdremix_init(void) {
    s_active = false;
    s_overlays.clear();
    size_t sdremix_count = 0;
    std::string dir;
    if (pc_sdremix_wanted()) {
        dir = find_folder("MELEE_SDREMIX_DIR", "sdremix");
        if (dir.empty()) {
            pc_log_line("sdremix: on, but no sdremix folder was found; playing vanilla characters");
        } else {
            for (const char* name : k_files) {
                std::string path = dir + "/" + name;
                size_t size = 0;
                if (file_size(path, &size) && size > 0) {
                    s_overlays.push_back({std::string("/") + name, path});
                }
            }
            sdremix_count = s_overlays.size();
            if (sdremix_count == 0) {
                pc_log_line("sdremix: %s has none of the fighter files; playing vanilla characters",
                    dir.c_str());
            }
        }
    }
    find_ace_files();
    if (s_overlays.empty()) {
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
        if (entry_nums[i] < 0) {
            pc_log_line("overlay: %s was not accepted", s_overlays[i].disc_name.c_str());
            if (i >= sdremix_count) {
                /* An ACE file the disc layer refused is not there to load. */
                const std::string& disc = s_overlays[i].disc_name;
                const std::string base = disc.substr(disc.find_last_of('/') + 1);
                for (auto it = s_ace_found.begin(); it != s_ace_found.end(); ++it) {
                    if (*it == base) {
                        s_ace_found.erase(it);
                        break;
                    }
                }
            }
        } else if (i < sdremix_count) {
            ++taken;
        }
    }
    if (sdremix_count > 0) {
        s_active = taken > 0;
        ftSdr_PatchTables();
        pc_log_line("sdremix: %zu of %zu fighter files from %s", taken,
            sizeof k_files / sizeof *k_files, dir.c_str());
    }
}

extern "C" bool pc_ace_has_file(const char* name) {
    for (const std::string& f : s_ace_found) {
        if (SDL_strcasecmp(f.c_str(), name) == 0) {
            return true;
        }
    }
    return false;
}

/* Bit (stage - 1) for each ACE stage (GrAceStage order) whose file is here;
 * net.h's PC_RULE_ACE_STAGE carries them shifted. */
extern "C" unsigned pc_ace_local_bits(void) {
    static const char* const k_stage_files[] = {"GrOMc.dat", "GrWw.dat", "GrColosseum.dat"};
    unsigned bits = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (pc_ace_has_file(k_stage_files[i])) {
            bits |= 1u << i;
        }
    }
    return bits;
}

extern "C" const char* pc_ace_dir(void) {
    return s_ace_dir.c_str();
}

extern "C" bool pc_sdremix_active(void) {
    return s_active;
}
