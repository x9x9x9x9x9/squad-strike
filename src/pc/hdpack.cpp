/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hdpack.hpp"
#include "paths.h"
#include "updater.hpp"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <mutex>
#include <regex>
#include <thread>

namespace pc::hdpack {
namespace fs = std::filesystem;
namespace {

/* The pack's page on MediaFire, as the Melee HD release post links it. */
constexpr const char* kPage = "https://www.mediafire.com/file/4embgcgt2wcc6q0/"
                              "Melee_HD_Texture_Pack_for_Dolphin_and_Slippi_-_Full_Pack.zip/file";
constexpr const char* kMarker = ".installed"; /* installed() looks for it */

std::mutex g_mutex;
State g_state;
bool g_busy = false;
std::thread g_worker;
std::atomic_bool g_cancel{false};
/* A worker still running at exit is stopped and joined, never left to
 * std::terminate in the thread's destructor. */
struct JoinAtExit {
    ~JoinAtExit() {
        g_cancel = true;
        if (g_worker.joinable())
            g_worker.join();
    }
} g_join_at_exit;

void set(Status status, std::string message, std::uint64_t total = 0, std::uint64_t done = 0) {
    std::lock_guard lock(g_mutex);
    g_state.status = status;
    g_state.message = std::move(message);
    g_state.total = total;
    g_state.done = done;
}

fs::path pref_dir() {
    char* pref = pc_get_pref_path();
    fs::path out = pref ? fs::u8path(pref) : fs::path();
    SDL_free(pref);
    return out;
}

std::string base64_decode(const std::string& in) {
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        int d;
        if (c >= 'A' && c <= 'Z')
            d = c - 'A';
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            d = c - '0' + 52;
        else if (c == '+' || c == '-')
            d = 62;
        else if (c == '/' || c == '_')
            d = 63;
        else
            continue;
        val = (val << 6) + d;
        bits += 6;
        if (bits >= 0) {
            out.push_back(char((val >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

/* The page's "Download (789.49MB)", in bytes, or 0. */
std::uint64_t find_size(const std::string& html) {
    static const std::regex re(R"(Download\s*\(\s*([0-9.]+)\s*([KMG])B\s*\))");
    std::smatch m;
    if (!std::regex_search(html, m, re))
        return 0;
    const double n = std::atof(m[1].str().c_str());
    const char unit = m[2].str()[0];
    const double scale = unit == 'G' ? 1024.0 * 1024 * 1024 : unit == 'M' ? 1024.0 * 1024 : 1024.0;
    return std::uint64_t(n * scale);
}

std::size_t count_textures(const fs::path& root) {
    std::size_t n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().filename().string().rfind("tex1_", 0) == 0)
            ++n;
    return n;
}

/* Unpack beside the pack's folder, then swap it in, so a failed install
 * leaves the old one. */
bool install_from(const fs::path& zip, std::string& error) {
    const fs::path dest = dir();
    fs::path staged = dest;
    staged += ".new";
    std::error_code ec;
    fs::remove_all(staged, ec);
    if (!updater::extract_zip(zip, staged, error)) {
        fs::remove_all(staged, ec);
        return false;
    }
    const std::size_t n = count_textures(staged);
    if (n == 0) {
        fs::remove_all(staged, ec);
        error = "that zip has no Dolphin textures (tex1_ files)";
        return false;
    }
    fs::remove_all(dest, ec);
    fs::rename(staged, dest, ec);
    if (ec) {
        error = "could not move the pack into place: " + ec.message();
        return false;
    }
    std::ofstream(dest / kMarker) << n << " textures\n";
    SDL_Log("hdpack: installed %zu textures in %s", n, dir_utf8().c_str());
    return true;
}

void download_worker() {
    std::string html, error;
    set(Status::Downloading, "Finding the download...");
    SDL_Log("hdpack: reading %s", page_url());
    bool ok = updater::fetch(
        page_url(),
        [&](const char* p, std::size_t n) {
            html.append(p, n);
            return !g_cancel && html.size() < (8u << 20);
        },
        error, "MediaFire");
    const std::string link = ok ? find_download_link(html) : "";
    if (ok && link.empty()) {
        ok = false;
        error = "MediaFire's page has no download link. Use Open page and Install zip";
    }
    const fs::path zip = pref_dir() / "hd-textures-download.zip";
    if (ok) {
        const std::uint64_t total = find_size(html);
        SDL_Log("hdpack: downloading %s", link.c_str());
        std::ofstream out(zip, std::ios::binary | std::ios::trunc);
        std::uint64_t done = 0;
        ok = out.good() && updater::fetch(
                               link,
                               [&](const char* p, std::size_t n) {
                                   out.write(p, std::streamsize(n));
                                   done += n;
                                   {
                                       std::lock_guard lock(g_mutex);
                                       g_state.total = total;
                                       g_state.done = done;
                                   }
                                   return out.good() && !g_cancel;
                               },
                               error, "MediaFire");
        out.close();
        if (g_cancel)
            error = "Canceled";
    }
    if (ok) {
        set(Status::Installing, "Installing the HD textures...");
        ok = install_from(zip, error);
        if (!ok && error == "not a zip file")
            error = "MediaFire sent a page, not the zip. Use Open page and Install zip";
    }
    std::error_code ec;
    fs::remove(zip, ec);
    if (ok)
        set(Status::Installed, "HD textures installed.");
    else {
        SDL_Log("hdpack: download failed: %s", error.c_str());
        set(Status::Failed, "HD textures: " + error + ".");
    }
}

void run(std::function<void()> fn) {
    {
        std::lock_guard lock(g_mutex);
        if (g_busy)
            return;
        g_busy = true;
    }
    if (g_worker.joinable())
        g_worker.join();
    g_cancel = false;
    g_worker = std::thread([fn = std::move(fn)] {
        fn();
        std::lock_guard lock(g_mutex);
        g_busy = false;
    });
}

}  // namespace

const char* page_url() {
    /* MELEE_HDPACK_PAGE: another page, for testing the download. */
    const char* page = std::getenv("MELEE_HDPACK_PAGE");
    return page && page[0] ? page : kPage;
}

std::string find_download_link(const std::string& html) {
    static const std::regex plain(R"(https://download[0-9]+\.mediafire\.com/[^"'\s<>]+)");
    std::smatch m;
    if (std::regex_search(html, m, plain))
        return m[0].str();
    /* Newer pages keep it base64 in data-scrambled-url. */
    static const std::regex scrambled(R"re(data-scrambled-url="([A-Za-z0-9+/=_-]+)")re");
    if (std::regex_search(html, m, scrambled)) {
        const std::string url = base64_decode(m[1].str());
        if (url.rfind("http", 0) == 0)
            return url;
    }
    return "";
}

void download_async() {
    run(download_worker);
}

void install_zip_async(const fs::path& zip) {
    run([zip] {
        set(Status::Installing, "Installing the HD textures...");
        std::string error;
        if (install_from(zip, error))
            set(Status::Installed, "HD textures installed.");
        else
            set(Status::Failed, "HD textures: " + error + ".");
    });
}

void cancel() {
    g_cancel = true;
}

State state() {
    std::lock_guard lock(g_mutex);
    if (!g_busy && g_state.status != Status::Failed)
        g_state.status = installed() ? Status::Installed : Status::Missing;
    return g_state;
}

void shutdown() {
    g_cancel = true;
    if (g_worker.joinable())
        g_worker.join();
}

}  // namespace pc::hdpack
