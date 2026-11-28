/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The updater's offline parts: the release JSON, picking this platform's
 * package, SHA-256, and unpacking a zip (tools/make_test_zip.py writes it). */
#include "pc/hdpack.hpp"
#include "pc/updater.hpp"
#include "pc/version.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace pc::updater;

/* The updater calls back into the program to restart; never here. */
extern "C" void pc_relaunch_to(const char*, const char*) {
    std::abort();
}

/* hdpack.cpp's folder; not used here. */
extern "C" char* pc_get_pref_path(void) {
    return nullptr;
}

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    /* The HD pack's MediaFire page: the plain link, and the base64 one
     * newer pages keep in data-scrambled-url. */
    assert(
        pc::hdpack::find_download_link(
            R"(<a class="input popsok" aria-label="Download file" href="https://download2278.mediafire.com/abc_DEF/4embgcgt2wcc6q0/Melee+HD.zip" id="downloadButton">)") ==
        "https://download2278.mediafire.com/abc_DEF/4embgcgt2wcc6q0/Melee+HD.zip");
    assert(
        pc::hdpack::find_download_link(
            R"(<a id="downloadButton" href="#" data-scrambled-url="aHR0cHM6Ly9kb3dubG9hZDEubWVkaWFmaXJlLmNvbS94L3kuemlw">)") ==
        "https://download1.mediafire.com/x/y.zip");
    assert(pc::hdpack::find_download_link("<html>nothing</html>").empty());
    const char* json = R"({"url":"x","html_url":"https://github.com/o/r/releases/tag/v1.0.11",
        "tag_name":"v1.0.11","name":"v1.0.11","draft":false,"prerelease":false,
        "body":"notes with \"quotes\" and é","assets":[
        {"name":"Melee-Windows-x86_64.zip","size":123456,"digest":"sha256:abc",
         "browser_download_url":"https://github.com/o/r/releases/download/v1.0.11/Melee-Windows-x86_64.zip"},
        {"name":"Melee-x86_64.AppImage","size":5,"digest":null,
         "browser_download_url":"https://github.com/o/r/releases/download/v1.0.11/Melee-x86_64.AppImage"},
        {"name":"melee-linux-x86_64.tar.gz","size":7,
         "browser_download_url":"https://github.com/o/r/releases/download/v1.0.11/melee-linux-x86_64.tar.gz"},
        {"name":"evil.zip","size":1,"browser_download_url":"http://insecure.example/evil.zip"}]})";
    Release r;
    assert(parse_release(json, r));
    assert(r.tag == "v1.0.11");
    assert(r.assets.size() == 3); /* the http:// one is dropped */
    const Asset* win = pick_asset(r, "Windows", "x86_64");
    assert(win && win->name == "Melee-Windows-x86_64.zip" && win->size == 123456 &&
           win->sha256 == "abc");
    assert(!pick_asset(r, "Windows", "arm64"));
    const Asset* lin = pick_asset(r, "Linux", "x86_64");
    assert(lin && lin->name == "melee-linux-x86_64.tar.gz"); /* no $APPIMAGE here */
    assert(!parse_release("{\"tag_name\":\"latest\"}", r));
    assert(!parse_release("not json", r));

    assert(pc::is_update_available("v1.0.10", "v1.0.11"));
    assert(pc::is_update_available("v1.0.10-test", "v1.0.10"));
    assert(!pc::is_update_available("v1.0.10", "v1.0.10"));
    assert(!pc::is_update_available("v1.0.11", "v1.0.10"));
    assert(pc::is_update_available("v1.0.12", "v1.1.01"));

    assert(sha256_hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(
        sha256_hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string million(1000000, 'a');
    assert(sha256_hex(million.data(), million.size()) ==
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    if (argc > 1) {
        /* argv[1]: a zip of pkg/melee.exe, pkg/resources/a.txt (deflated),
         * pkg/empty/ and ../escape.txt, which must not be written. */
        const fs::path out = fs::temp_directory_path() / "updater_test_out";
        fs::remove_all(out);
        std::string error;
        assert(extract_zip(argv[1], out, error));
        assert(slurp(out / "melee.exe") == "program");
        assert(slurp(out / "resources" / "a.txt") == std::string(5000, 'z'));
        /* A deflated file whose output fills the inflate buffer exactly as an
         * input chunk ends (make_test_zip.py): inflate's Z_BUF_ERROR then is
         * no damage. */
        {
            const std::string edge = slurp(out / "resources" / "edge.bin");
            assert(edge.size() == 65641 + 150000);
            assert(edge.compare(0, 65641, std::string(65641, '\0')) == 0);
        }
        assert(fs::is_directory(out / "empty"));
        assert(!fs::exists(out.parent_path() / "escape.txt"));
        assert(sha256_file(out / "melee.exe") == sha256_hex("program", 7));
        fs::remove_all(out);
        assert(!extract_zip(argv[0], out, error)); /* not a zip */
        fs::remove_all(out);
    }
    std::puts("updater: ok");
    return 0;
}
