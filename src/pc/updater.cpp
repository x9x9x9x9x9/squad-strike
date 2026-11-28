/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "updater.hpp"
#include "pc.h"
#include "version.hpp"

#include <SDL3/SDL.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#elif defined(MELEE_USE_CURL)
#include <curl/curl.h>
#endif
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace fs = std::filesystem;

namespace pc::updater {
namespace {

constexpr const char* kLatestApi =
    "https://api.github.com/repos/x9x9x9x9x9/squad-strike/releases/latest";
constexpr const char* kReleasesPage = "https://github.com/x9x9x9x9x9/squad-strike/releases";
constexpr const char* kUserAgent = "SquadStrike-Updater";
constexpr const char* kGitHubAccept = "application/vnd.github+json, application/octet-stream";
/* Moved-aside files from the previous update; removed at the next start. */
constexpr const char* kOldSuffix = ".update-old";

std::mutex g_mutex;
State g_state;
Release g_release;
std::atomic_bool g_cancel{false};
std::thread g_worker;
fs::path g_appimage_new; /* AppImage: the downloaded image, before the swap */
/* A worker still running at exit is stopped and joined, never left to
 * std::terminate in the thread's destructor. */
struct JoinAtExit {
    ~JoinAtExit() {
        g_cancel = true;
        if (g_worker.joinable())
            g_worker.join();
    }
} g_join_at_exit;

std::string utf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

void set_state(const std::function<void(State&)>& change) {
    std::lock_guard lock(g_mutex);
    change(g_state);
}

/* ---- JSON: just enough for one GitHub release ------------------------- */

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
    const Json* get(std::string_view key) const {
        for (const auto& [k, v] : fields)
            if (k == key)
                return &v;
        return nullptr;
    }
    std::string str(std::string_view key) const {
        const Json* v = get(key);
        return v && v->kind == String ? v->text : std::string();
    }
};

class JsonReader {
    std::string_view s;
    size_t i = 0;
    int depth = 0;
    void space() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size())
                return false;
            char e = s[i++];
            switch (e) {
            case 'n':
                out += '\n';
                break;
            case 't':
                out += '\t';
                break;
            case 'r':
                out += '\r';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'u': {
                if (i + 4 > s.size())
                    return false;
                unsigned code = 0;
                for (int k = 0; k < 4; ++k) {
                    char h = s[i++];
                    code <<= 4;
                    if (h >= '0' && h <= '9')
                        code |= unsigned(h - '0');
                    else if (h >= 'a' && h <= 'f')
                        code |= unsigned(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F')
                        code |= unsigned(h - 'A' + 10);
                    else
                        return false;
                }
                /* Names and URLs here are ASCII; anything else reads as '?'. */
                out += code < 0x80 ? char(code) : '?';
                break;
            }
            default:
                out += e;
            }
        }
        return false;
    }

public:
    explicit JsonReader(std::string_view text) : s(text) {}
    bool value(Json& out) {
        if (++depth > 64)
            return false;
        space();
        if (i >= s.size())
            return false;
        bool ok = true;
        const char c = s[i];
        if (c == '"') {
            out.kind = Json::String;
            ok = string(out.text);
        } else if (c == '{') {
            out.kind = Json::Object;
            ++i;
            space();
            if (i < s.size() && s[i] == '}')
                ++i;
            else
                while (ok) {
                    space();
                    std::string key;
                    Json v;
                    ok = string(key);
                    space();
                    ok = ok && i < s.size() && s[i++] == ':' && value(v);
                    if (!ok)
                        break;
                    out.fields.emplace_back(std::move(key), std::move(v));
                    space();
                    if (i < s.size() && s[i] == ',')
                        ++i;
                    else {
                        ok = i < s.size() && s[i++] == '}';
                        break;
                    }
                }
        } else if (c == '[') {
            out.kind = Json::Array;
            ++i;
            space();
            if (i < s.size() && s[i] == ']')
                ++i;
            else
                while (ok) {
                    Json v;
                    ok = value(v);
                    if (!ok)
                        break;
                    out.items.push_back(std::move(v));
                    space();
                    if (i < s.size() && s[i] == ',')
                        ++i;
                    else {
                        ok = i < s.size() && s[i++] == ']';
                        break;
                    }
                }
        } else if (s.substr(i, 4) == "true" || s.substr(i, 4) == "null") {
            out.kind = s[i] == 't' ? Json::Bool : Json::Null;
            i += 4;
        } else if (s.substr(i, 5) == "false") {
            out.kind = Json::Bool;
            i += 5;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = i;
            while (i < s.size() && std::strchr("+-.eE0123456789", s[i]))
                ++i;
            out.kind = Json::Number;
            out.number = std::strtod(std::string(s.substr(start, i - start)).c_str(), nullptr);
        } else
            ok = false;
        --depth;
        return ok;
    }
};

/* ---- SHA-256 ------------------------------------------------------------ */

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
        0x1f83d9ab, 0x5be0cd19};
    uint8_t block[64];
    size_t used = 0;
    uint64_t bits = 0;
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress(const uint8_t* p) {
        static const uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
            0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6,
            0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d,
            0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585,
            0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa,
            0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int t = 0; t < 16; ++t)
            w[t] = uint32_t(p[t * 4]) << 24 | uint32_t(p[t * 4 + 1]) << 16 |
                   uint32_t(p[t * 4 + 2]) << 8 | p[t * 4 + 3];
        for (int t = 16; t < 64; ++t) {
            uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
            uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 64; ++t) {
            uint32_t t1 =
                hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[t] + w[t];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    void update(const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        bits += uint64_t(n) * 8;
        while (n > 0) {
            size_t take = std::min(n, sizeof block - used);
            std::memcpy(block + used, p, take);
            used += take;
            p += take;
            n -= take;
            if (used == sizeof block) {
                compress(block);
                used = 0;
            }
        }
    }
    std::string hex() {
        const uint64_t total = bits;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 56)
            update(&zero, 1);
        uint8_t len[8];
        for (int k = 0; k < 8; ++k)
            len[k] = uint8_t(total >> (56 - 8 * k));
        update(len, 8);
        std::string out;
        char buf[9];
        for (uint32_t v : h) {
            std::snprintf(buf, sizeof buf, "%08x", v);
            out += buf;
        }
        return out;
    }
};

/* ---- HTTPS GET ---------------------------------------------------------- */

using Sink = std::function<bool(const char*, size_t)>; /* false: stop */

#if defined(_WIN32)
std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
bool http_get(const std::string& url, const Sink& sink, std::string& error,
    const char* accept = kGitHubAccept, const char* who = "GitHub") {
    std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    wchar_t host[256], path[2048];
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        error = "bad address";
        return false;
    }
    HINTERNET session = WinHttpOpen(widen(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY is Windows 8.1+ */
        session = WinHttpOpen(widen(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "no internet access (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
    HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connect ?
                            WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) :
                            nullptr;
    bool ok = request &&
              WinHttpSendRequest(request, widen(std::string("Accept: ") + accept + "\r\n").c_str(),
                  DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (!ok)
        error = std::string("could not reach ") + who + " (" + std::to_string(GetLastError()) + ")";
    if (ok) {
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        if (code < 200 || code >= 300) {
            error = std::string(who) + " answered " + std::to_string(code);
            ok = false;
        }
    }
    std::vector<char> buf(1 << 16);
    while (ok) {
        DWORD got = 0;
        if (!WinHttpReadData(request, buf.data(), DWORD(buf.size()), &got)) {
            error = "the connection dropped (" + std::to_string(GetLastError()) + ")";
            ok = false;
        } else if (got == 0)
            break;
        else if (!sink(buf.data(), got)) {
            error = "canceled";
            ok = false;
        }
    }
    if (request)
        WinHttpCloseHandle(request);
    if (connect)
        WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}
#elif defined(MELEE_USE_CURL)
size_t curl_sink(char* p, size_t size, size_t n, void* user) {
    return (*static_cast<const Sink*>(user))(p, size * n) ? size * n : 0;
}
bool http_get(const std::string& url, const Sink& sink, std::string& error,
    const char* accept = kGitHubAccept, const char* who = "GitHub") {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "no network library";
        return false;
    }
    curl_slist* headers = curl_slist_append(nullptr, (std::string("Accept: ") + accept).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_sink);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;  // NOLINT: libcurl's own type
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        error = g_cancel ? "canceled" : curl_easy_strerror(res);
        return false;
    }
    if (code < 200 || code >= 300) {
        error = std::string(who) + " answered " + std::to_string(code);
        return false;
    }
    return true;
}
#else
bool http_get(const std::string&, const Sink&, std::string& error, const char* = nullptr,
    const char* = nullptr) {
    error = "this build cannot download";
    return false;
}
#endif

/* ---- where this copy lives -------------------------------------------- */

fs::path base_dir() {
    const char* base = SDL_GetBasePath();
    return base ? fs::u8path(base) : fs::path();
}
std::string_view platform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__) && !defined(__ANDROID__)
    return "Linux";
#else
    return "";
#endif
}
std::string_view architecture() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "";
#endif
}
const char* appimage() {
#if defined(__linux__) && !defined(__ANDROID__)
    return std::getenv("APPIMAGE");
#else
    return nullptr;
#endif
}
/* Where this copy can replace itself: a Windows folder it may write to, or
 * an AppImage. */
bool can_self_install(const Asset* asset) {
    if (!asset)
        return false;
#if defined(_WIN32)
    if (asset->name.size() < 4 || asset->name.compare(asset->name.size() - 4, 4, ".zip") != 0)
        return false;
    const fs::path probe = base_dir() / "update-check.tmp";
    std::ofstream f(probe, std::ios::binary);
    const bool ok = f.good();
    f.close();
    std::error_code ec;
    fs::remove(probe, ec);
    return ok;
#else
    const char* image = appimage();
    if (!image || asset->name.find(".AppImage") == std::string::npos)
        return false;
    return access(fs::path(image).parent_path().c_str(), W_OK) == 0;
#endif
}

void fail(const std::string& why) {
    SDL_Log("updater: %s", why.c_str());
    set_state([&](State& s) {
        s.status = Status::Failed;
        s.message = why;
    });
}

/* ---- the work ----------------------------------------------------------- */

void check_worker() {
    std::string body, error;
    const bool ok = http_get(
        kLatestApi,
        [&](const char* p, size_t n) {
            body.append(p, n);
            return !g_cancel && body.size() < (4u << 20);
        },
        error);
    if (g_cancel)
        return;
    Release r;
    if (!ok || !parse_release(body, r)) {
        fail("Could not check for updates: " + (ok ? std::string("unexpected answer") : error) +
             ".");
        return;
    }
    const Asset* asset = pick_asset(r, platform(), architecture());
    /* MELEE_UPDATE_FROM=<version>: act as that version, to try an update. */
    const char* from = std::getenv("MELEE_UPDATE_FROM");
    const std::string current = from && *from ? from : get_app_version();
    const bool newer = is_update_available(current, r.tag);
    const bool install = can_self_install(asset);
    {
        std::lock_guard lock(g_mutex);
        g_release = r;
        g_state.latest = r.tag;
        g_state.release_url = r.html_url.empty() ? kReleasesPage : r.html_url;
        g_state.can_install = install;
        g_state.total = asset ? asset->size : 0;
        g_state.status = newer ? Status::Available : Status::UpToDate;
        g_state.message = newer ? r.tag + " is out." : "Up to date (" + current + ").";
    }
    SDL_Log("updater: newest %s, this %s%s", r.tag.c_str(), current.c_str(),
        newer ? (install ? ", can install" : ", release page only") : "");
}

/* A rename, tried for a few seconds: on Windows an antivirus scan of a file
 * just written holds it open for a moment. */
void rename_retry(const fs::path& from, const fs::path& to, std::error_code& ec) {
    for (int attempt = 0; attempt < 15; ++attempt) {
        fs::rename(from, to, ec);
        if (!ec)
            return;
        SDL_Delay(200);
    }
}

/* Move every file under `staged` into `base`, setting each replaced one aside
 * as <name>.update-old. On any failure the files already moved go back. */
bool swap_in(const fs::path& staged, const fs::path& base, std::string& error) {
    struct Moved {
        fs::path target, old;
        bool had_old;
    };
    std::vector<Moved> moved;
    std::error_code ec;
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(staged, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec))
            files.push_back(fs::relative(it->path(), staged, ec));
    if (ec) {
        error = "could not read the unpacked update";
        return false;
    }
    bool ok = true;
    for (const auto& rel : files) {
        const fs::path target = base / rel,
                       old = fs::path(target.native() + fs::path(kOldSuffix).native());
        fs::create_directories(target.parent_path(), ec);
        fs::remove(old, ec);
        const bool had = fs::exists(target, ec);
        if (had) {
            rename_retry(target, old, ec);
            if (ec) {
                error = "could not replace " + utf8(rel) + " (" + ec.message() + ")";
                ok = false;
                break;
            }
        }
        rename_retry(staged / rel, target, ec);
        if (ec) {
            if (had)
                rename_retry(old, target, ec);
            error = "could not install " + utf8(rel);
            ok = false;
            break;
        }
        moved.push_back({target, old, had});
    }
    if (!ok) {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            fs::remove(it->target, ec);
            if (it->had_old)
                fs::rename(it->old, it->target, ec);
        }
    }
    return ok;
}

void install_worker() {
    Asset asset;
    std::string tag;
    {
        std::lock_guard lock(g_mutex);
        const Asset* a = pick_asset(g_release, platform(), architecture());
        if (!a) {
            g_state.status = Status::Failed;
            g_state.message = "This release has no package for this computer.";
            return;
        }
        asset = *a;
        tag = g_release.tag;
    }
    const fs::path base = base_dir();
    const char* image = appimage();
    const fs::path work = image ? fs::path(image).parent_path() : base / "update";
    const fs::path download = image ? fs::path(std::string(image) + ".new") : work / asset.name;
    std::error_code ec;
    if (!image) {
        fs::remove_all(work, ec);
        fs::create_directories(work, ec);
    }
    {
        std::ofstream out(download, std::ios::binary | std::ios::trunc);
        if (!out) {
            fail("Could not save the update next to the game.");
            return;
        }
        Sha256 sha;
        uint64_t done = 0;
        uint32_t last_ms = 0;
        std::string error;
        const bool ok = http_get(
            asset.url,
            [&](const char* p, size_t n) {
                out.write(p, std::streamsize(n));
                sha.update(p, n);
                done += n;
                if (SDL_GetTicks() - last_ms > 100) {
                    last_ms = uint32_t(SDL_GetTicks());
                    set_state([&](State& s) { s.done = done; });
                }
                return out.good() && !g_cancel;
            },
            error);
        out.close();
        if (!ok || g_cancel) {
            fs::remove(download, ec);
            if (g_cancel)
                set_state([](State& s) {
                    s.status = Status::Available;
                    s.message = "Update canceled.";
                });
            else
                fail("Download failed: " + error + ".");
            return;
        }
        if (asset.size && done != asset.size) {
            fs::remove(download, ec);
            fail("Download incomplete. Try again.");
            return;
        }
        if (!asset.sha256.empty() && sha.hex() != asset.sha256) {
            fs::remove(download, ec);
            fail("Download damaged (checksum mismatch). Try again.");
            return;
        }
        set_state([&](State& s) {
            s.done = done;
            s.status = Status::Installing;
            s.message = "Installing " + tag + "...";
        });
    }
    if (image) {
#if !defined(_WIN32)
        chmod(download.c_str(), 0755);
#endif
        std::lock_guard lock(g_mutex);
        g_appimage_new = download;
        g_state.status = Status::Ready;
        g_state.message = "Restarting into " + tag + "...";
        return;
    }
    std::string error;
    const fs::path staged = work / "new";
    if (!extract_zip(download, staged, error)) {
        fs::remove_all(work, ec);
        fail("Could not unpack the update: " + error + ".");
        return;
    }
    fs::remove(download, ec);
#if defined(_WIN32)
    const char* program = "melee.exe";
#else
    const char* program = "melee";
#endif
    if (!fs::exists(staged / program, ec)) {
        fs::remove_all(work, ec);
        fail("The update package was not what was expected.");
        return;
    }
    if (!swap_in(staged, base, error)) {
        fs::remove_all(work, ec);
        fail("Could not install the update: " + error +
             ". Close other copies of the game "
             "and try again.");
        return;
    }
    fs::remove_all(work, ec);
    SDL_Log("updater: installed %s", tag.c_str());
    set_state([&](State& s) {
        s.status = Status::Ready;
        s.message = "Restarting into " + tag + "...";
    });
}

void run_worker(void (*fn)()) {
    if (g_worker.joinable())
        g_worker.join();
    g_cancel = false;
    g_worker = std::thread(fn);
}

}  // namespace

bool fetch(const std::string& url, const std::function<bool(const char*, std::size_t)>& sink,
    std::string& error, const char* who) {
    return http_get(url, sink, error, "*/*", who);
}

/* ---- public ------------------------------------------------------------- */

bool supported() {
#if (defined(_WIN32) || defined(MELEE_USE_CURL)) && !defined(__ANDROID__) &&                       \
    !defined(__EMSCRIPTEN__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    return !platform().empty() && !architecture().empty();
#else
    return false;
#endif
}

void check_async() {
    if (!supported())
        return;
    {
        std::lock_guard lock(g_mutex);
        if (g_state.status == Status::Checking || g_state.status == Status::Downloading ||
            g_state.status == Status::Installing || g_state.status == Status::Ready)
            return;
        g_state.status = Status::Checking;
        g_state.message = "Checking for updates...";
    }
    run_worker(check_worker);
}

void install_async() {
    {
        std::lock_guard lock(g_mutex);
        if (g_state.status != Status::Available && g_state.status != Status::Failed)
            return;
        if (g_release.tag.empty())
            return;
        g_state.status = Status::Downloading;
        g_state.done = 0;
        g_state.message = "Downloading " + g_release.tag + "...";
    }
    run_worker(install_worker);
}

State state() {
    std::lock_guard lock(g_mutex);
    return g_state;
}

void restart() {
    std::string tag;
    fs::path image_new;
    {
        std::lock_guard lock(g_mutex);
        tag = g_state.latest;
        image_new = g_appimage_new;
    }
    if (g_worker.joinable())
        g_worker.join();
    const std::string status = "Updated to " + tag + ".";
#if !defined(_WIN32)
    if (const char* image = appimage(); image && !image_new.empty()) {
        std::error_code ec;
        fs::rename(image_new, image, ec);
        if (!ec) {
            setenv("MELEE_LAUNCHER_PAGE", "home", 1);
            setenv("MELEE_LAUNCHER_STATUS", status.c_str(), 1);
            /* The image's own runtime, not /proc/self/exe: that is the old
             * image's mount. */
            execl(image, image, static_cast<char*>(nullptr));
        }
    }
#endif
    pc_relaunch_to("home", status.c_str());
    std::exit(0); /* pc_relaunch_to does not return */
}

void cleanup_previous() {
#if defined(_WIN32)
    const fs::path base = base_dir();
    if (base.empty())
        return;
    std::error_code ec;
    fs::remove_all(base / "update", ec);
    std::vector<fs::path> old;
    for (auto it = fs::recursive_directory_iterator(base, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (it.depth() > 4) {
            it.disable_recursion_pending();
            continue;
        }
        const auto name = it->path().filename().native();
        const auto suffix = fs::path(kOldSuffix).native();
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            old.push_back(it->path());
    }
    for (const auto& p : old)
        fs::remove(p, ec);
#endif
}

void shutdown() {
    g_cancel = true;
    if (g_worker.joinable())
        g_worker.join();
}

bool parse_release(std::string_view json, Release& out) {
    Json root;
    JsonReader reader(json);
    if (!reader.value(root) || root.kind != Json::Object)
        return false;
    out = {};
    out.tag = root.str("tag_name");
    out.html_url = root.str("html_url");
    if (const Json* assets = root.get("assets"); assets && assets->kind == Json::Array)
        for (const auto& a : assets->items) {
            if (a.kind != Json::Object)
                continue;
            Asset asset;
            asset.name = a.str("name");
            asset.url = a.str("browser_download_url");
            if (const Json* size = a.get("size"); size && size->kind == Json::Number)
                asset.size = uint64_t(size->number);
            const std::string digest = a.str("digest");
            if (digest.rfind("sha256:", 0) == 0)
                asset.sha256 = digest.substr(7);
            if (!asset.name.empty() && asset.url.rfind("https://", 0) == 0)
                out.assets.push_back(std::move(asset));
        }
    return SemVer::parse(out.tag).valid;
}

const Asset* pick_asset(const Release& r, std::string_view os, std::string_view arch) {
    std::vector<std::string> names;
    const std::string a(arch);
    if (os == "Windows" || os == "macOS")
        names = {"Melee-" + std::string(os) + "-" + a + ".zip"};
    else if (os == "Linux") {
        const std::string linux_arch = arch == "arm64" ? "aarch64" : a;
        names = {"Melee-" + linux_arch + ".AppImage", "melee-linux-" + linux_arch + ".tar.gz"};
    }
    /* An AppImage installs an AppImage; a folder copy gets its tarball. */
    if (os == "Linux" && !appimage())
        std::reverse(names.begin(), names.end());
    for (const auto& name : names)
        for (const auto& asset : r.assets)
            if (asset.name == name)
                return &asset;
    return nullptr;
}

std::string sha256_hex(const void* data, size_t size) {
    Sha256 sha;
    sha.update(data, size);
    return sha.hex();
}

std::string sha256_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    Sha256 sha;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), std::streamsize(buf.size()));
        sha.update(buf.data(), size_t(in.gcount()));
    }
    return sha.hex();
}

/* ---- zip ---------------------------------------------------------------- */

namespace {
uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0] | p[1] << 8);
}
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
/* An entry's path below the package folder, or empty when it must not be
 * written (outside the folder, absolute, a drive, or the folder itself). */
fs::path safe_entry(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    const size_t slash = name.find('/');
    if (slash == std::string::npos)
        return {};
    name = name.substr(slash + 1);
    if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos)
        return {};
    fs::path out;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos)
            end = name.size();
        const std::string part = name.substr(start, end - start);
        if (part == "..")
            return {};
        if (!part.empty() && part != ".")
            out /= fs::u8path(part);
        start = end + 1;
    }
    return out;
}
}  // namespace

bool extract_zip(const fs::path& zip, const fs::path& dest, std::string& error) {
    std::ifstream in(zip, std::ios::binary);
    if (!in) {
        error = "cannot open the download";
        return false;
    }
    in.seekg(0, std::ios::end);
    const uint64_t size = uint64_t(in.tellg());
    /* The end-of-central-directory record: within the last 64 KiB + 22. */
    const uint64_t tail = std::min<uint64_t>(size, 65557);
    std::vector<uint8_t> end(tail);
    in.seekg(std::streamoff(size - tail));
    in.read(reinterpret_cast<char*>(end.data()), std::streamsize(tail));
    int64_t eocd = -1;
    for (int64_t i = int64_t(tail) - 22; i >= 0; --i)
        if (le32(&end[size_t(i)]) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        error = "not a zip file";
        return false;
    }
    const uint8_t* e = &end[size_t(eocd)];
    const uint16_t count = le16(e + 10);
    const uint32_t cd_size = le32(e + 12), cd_offset = le32(e + 16);
    if (count == 0xffff || cd_offset == 0xffffffffu || uint64_t(cd_offset) + cd_size > size) {
        error = "unsupported zip (zip64)";
        return false;
    }
    std::vector<uint8_t> cd(cd_size);
    in.seekg(cd_offset);
    in.read(reinterpret_cast<char*>(cd.data()), std::streamsize(cd_size));
    if (!in) {
        error = "damaged zip";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dest, ec);
    size_t at = 0;
    std::vector<uint8_t> inbuf(1 << 16), outbuf(1 << 16);
    for (unsigned n = 0; n < count; ++n) {
        if (at + 46 > cd.size() || le32(&cd[at]) != 0x02014b50) {
            error = "damaged zip directory";
            return false;
        }
        const uint8_t* c = &cd[at];
        const uint16_t method = le16(c + 10);
        const uint32_t crc = le32(c + 16), packed = le32(c + 20), unpacked = le32(c + 24);
        const uint16_t name_len = le16(c + 28), extra_len = le16(c + 30),
                       comment_len = le16(c + 32);
        const uint32_t local = le32(c + 42);
        if (at + 46 + name_len > cd.size()) {
            error = "damaged zip directory";
            return false;
        }
        const std::string name(reinterpret_cast<const char*>(c + 46), name_len);
        at += 46 + size_t(name_len) + extra_len + comment_len;
        if (packed == 0xffffffffu || unpacked == 0xffffffffu) {
            error = "unsupported zip (zip64)";
            return false;
        }
        const fs::path rel = safe_entry(name);
        if (rel.empty())
            continue; /* the package folder itself, or an entry we refuse */
        const fs::path out_path = dest / rel;
        if (!name.empty() && (name.back() == '/' || name.back() == '\\')) {
            fs::create_directories(out_path, ec);
            continue;
        }
        if (method != 0 && method != 8) {
            error = "unsupported compression in " + name;
            return false;
        }
        uint8_t lh[30];
        in.seekg(local);
        in.read(reinterpret_cast<char*>(lh), sizeof lh);
        if (!in || le32(lh) != 0x04034b50) {
            error = "damaged zip entry " + name;
            return false;
        }
        in.seekg(std::streamoff(local) + 30 + le16(lh + 26) + le16(lh + 28));
        fs::create_directories(out_path.parent_path(), ec);
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + utf8(rel);
            return false;
        }
        uLong check = crc32(0, nullptr, 0);
        uint64_t left = packed, written = 0;
        z_stream z{};
        if (method == 8 && inflateInit2(&z, -MAX_WBITS) != Z_OK) {
            error = "inflate failed";
            return false;
        }
        bool ok = true;
        int zr = Z_OK;
        while (ok && left > 0) {
            const size_t take = size_t(std::min<uint64_t>(left, inbuf.size()));
            in.read(reinterpret_cast<char*>(inbuf.data()), std::streamsize(take));
            if (size_t(in.gcount()) != take) {
                ok = false;
                break;
            }
            left -= take;
            if (method == 0) {
                out.write(reinterpret_cast<char*>(inbuf.data()), std::streamsize(take));
                check = crc32(check, inbuf.data(), uInt(take));
                written += take;
                continue;
            }
            z.next_in = inbuf.data();
            z.avail_in = uInt(take);
            do {
                z.next_out = outbuf.data();
                z.avail_out = uInt(outbuf.size());
                zr = inflate(&z, Z_NO_FLUSH);
                /* Z_BUF_ERROR: the output filled exactly as this input ran out,
                 * so the next call had nothing to do; read on. (2026-11-28: the
                 * Melee HD pack failed to install as "damaged".) The size and
                 * CRC checks below catch real damage. */
                if (zr == Z_BUF_ERROR)
                    break;
                if (zr != Z_OK && zr != Z_STREAM_END) {
                    ok = false;
                    break;
                }
                const size_t got = outbuf.size() - z.avail_out;
                out.write(reinterpret_cast<char*>(outbuf.data()), std::streamsize(got));
                check = crc32(check, outbuf.data(), uInt(got));
                written += got;
            } while (z.avail_out == 0 && zr != Z_STREAM_END);
        }
        if (method == 8)
            inflateEnd(&z);
        out.close();
        if (!ok || !out || written != unpacked || check != crc) {
            error = "damaged file " + utf8(rel) + " in the zip";
            return false;
        }
#if !defined(_WIN32)
        /* Unix permissions, when the zip came from a Unix tool. */
        if ((le16(c + 4) >> 8) == 3) {
            const uint32_t mode = le32(c + 38) >> 16;
            if (mode & 0111)
                chmod(out_path.c_str(), 0755);
        }
#endif
    }
    return true;
}

}  // namespace pc::updater
