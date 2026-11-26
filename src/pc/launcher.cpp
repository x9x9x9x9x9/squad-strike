/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "launcher.h"
#include "textures.h"
#include "widescreen.h"
#include "launcher_data.hpp"
#include "discfont.h"
#include "version.hpp"
#include "net_match.h"
#include "net_identity.h"
#include "net.h"
#include "pc.h"
#include "updater.hpp"
#include <melee/gm/gmstats.h>
#include "replay.h"
#include "video_export.h"
#include "android_hooks.h"
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <aurora/dvd.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <aurora/rmlui.hpp>
#include <aurora/input.hpp>
/* TargetConditionals.h defines TARGET_OS_IPHONE on macOS too, as 0, so this
 * has to test its value: defined() alone pulled the iOS-only dialog into the
 * macOS build and left the link short a symbol. */
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include "ios_dialog.h"
#endif
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <vector>

namespace {
launcher::Preferences prefs;
std::filesystem::path config_path;

bool set_fullscreen(SDL_Window* window, bool fullscreen) {
#ifdef __ANDROID__
    return pc_android_set_fullscreen(fullscreen);
#else
    return SDL_SetWindowFullscreen(window, fullscreen);
#endif
}
const char* display_mode() {
#ifdef __ANDROID__
    return prefs.fullscreen ? "Fullscreen" : "System bars shown";
#else
    return prefs.fullscreen ? "Fullscreen" : "Windowed";
#endif
}

/* Aurora's input router no longer feeds RmlUi itself: the host registers a
 * layer that forwards events to aurora::rmlui::process_input. Without it the
 * launcher and F1 menu get no mouse, touch or keyboard input at all. */
void ensure_rmlui_input_layer() {
    static aurora::input::LayerId layer = aurora::input::kInvalidLayerId;
    if (layer != aurora::input::kInvalidLayerId)
        return;
    layer = aurora::input::register_layer({
        .label = "melee.rmlui",
        .priority = aurora::input::kRmlUiLayerPriority,
        .onEvent =
            [](const aurora::input::InputEvent& event, void*) {
                return aurora::rmlui::process_input(event).handled ?
                           aurora::input::EventResult::Consume :
                           aurora::input::EventResult::Pass;
            },
    });
}

void refresh_online(Rml::ElementDocument* doc) {
    pc_net_set_input_delay(prefs.net_delay);
    if (auto* e = dynamic_cast<Rml::ElementFormControl*>(doc->GetElementById("net-target")))
        e->SetValue(prefs.net_target);
    if (auto* e = doc->GetElementById("net-delay"))
        e->SetInnerRML(prefs.net_delay < 0 ? "Auto" : std::to_string(prefs.net_delay) + " frames");
    if (auto* e = doc->GetElementById("show-ping"))
        e->SetInnerRML(prefs.show_ping ? "On" : "Off");
    if (auto* e = doc->GetElementById("net-two-players"))
        e->SetInnerRML(prefs.net_two_players ? "2" : "1");
    /* The code in force: the one chosen, or the default this profile was
     * given until one is. Not while it is being typed in. */
    if (auto* e = dynamic_cast<Rml::ElementFormControl*>(doc->GetElementById("net-code"));
        e && doc->GetFocusLeafNode() != e)
        e->SetValue(prefs.net_code.empty() ? pc_net_match_local_code() : prefs.net_code);
}
bool change_online(Rml::Event& event) {
    auto* target = event.GetTargetElement();
    if (!target)
        return false;
    auto id = target->GetId();
    if (id == "net-code") {
        /* Taken once it is a whole code (CHRS#123, any case, spaces or a
         * leading zero allowed); half-typed text is left in the box. */
        auto value = event.GetParameter<Rml::String>("value", "");
        char code[PC_CODE_SIZE];
        if (pc_identity_normalize_code(value.c_str(), code))
            prefs.net_code = code;
        return true;
    }
    if (id != "net-name" && id != "net-target")
        return false;
    auto value = event.GetParameter<Rml::String>("value", "");
    const size_t limit = id == "net-name" ? 8 : PC_CODE_LEN_MAX;
    if (value.size() > limit)
        return true;
    for (char& c : value) {
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (id == "net-target" && c == '#')))
            return true;
    }
    if (id == "net-name") {
        if (!value.empty())
            prefs.net_name = value;
    } else
        prefs.net_target = value;
    return true;
}

const char* filter_name(int mode) {
    // Mode 0 is the plain present blit -- no effect at all, so it is the
    // "off" setting; the bilinear filter it uses is just the scaler.
    static const char* const names[] = {"Off", "Area sampling", "CRT scanlines", "Vibrant"};
    return names[mode & 3];
}

const char* backend_name(int mode) {
#if defined(__APPLE__)
    return "Metal";
#else
    static const char* const names[] = {"Auto", "Direct3D 12", "Vulkan", "Direct3D 11"};
    if (mode < 0 || mode > 3)
        return "Auto";
    return names[mode];
#endif
}

bool load_menu_fonts(const std::filesystem::path& resources) {
    if (!Rml::LoadFontFace((resources / "font.ttf").string(), true)) {
        SDL_Log("Menus: could not load font from %s", resources.c_str());
        return false;
    }
    Rml::LoadFontFace((resources / "font-bold.ttf").string());
    /* The menus' face (Noto Sans SemiCondensed, cut to Latin), with its
     * ExtraBold for the plates' names. Liberation covers what it lacks. */
    for (const char* face : {"menu.ttf", "menu-bold.ttf"})
        if (!Rml::LoadFontFace((resources / face).string()))
            SDL_Log("Menus: could not load %s", face);
    if (!Rml::LoadFontFace((resources / "menu-heavy.ttf").string(), false,
            static_cast<Rml::Style::FontWeight>(800)))
        SDL_Log("Menus: could not load menu-heavy.ttf");
    return true;
}

/* The two Direct3D entries only exist on Windows, so elsewhere the cycle goes
 * straight from Auto to Vulkan: offering a backend that can only ever fail
 * over leaves the button showing a label for something the run is not using.
 * ponytail: indices stay identical on every platform so the prefs file and
 * the BACKEND_* mapping below need no per-platform cases. */
int backend_next(int mode, int direction = 1) {
#if defined(_WIN32)
    return (mode + direction + 4) % 4;
#else
    return mode == 0 ? 2 : 0;
#endif
}

constexpr const char* tab_ids[] = {
    "tab-graphics", "tab-audio", "tab-cheats", "tab-controls", "tab-online", "tab-launcher"};

template <size_t N>
int index_of(const char* const (&ids)[N], const Rml::String& id) {
    const auto it = std::find(std::begin(ids), std::end(ids), id);
    return it == std::end(ids) ? -1 : static_cast<int>(it - std::begin(ids));
}
std::string resolution_name(float scale) {
    return scale == 0 ? std::string("Auto") : std::to_string(int(scale)) + "x";
}

std::string identity(const std::string& path) {
    if (path.rfind("content://", 0) == 0)
        return path;
    struct stat s = {};
    if (stat(path.c_str(), &s) != 0)
        return {};
#ifdef _WIN32
    return std::to_string(s.st_dev) + ":" + std::to_string(s.st_ino) + ":" +
           std::to_string(s.st_size) + ":" + std::to_string(s.st_mtime) + ":" +
           std::to_string(s.st_ctime);
#elif defined(__APPLE__)
    return std::to_string(s.st_dev) + ":" + std::to_string(s.st_ino) + ":" +
           std::to_string(s.st_size) + ":" + std::to_string(s.st_mtimespec.tv_sec) + ":" +
           std::to_string(s.st_mtimespec.tv_nsec) + ":" + std::to_string(s.st_ctimespec.tv_sec) +
           ":" + std::to_string(s.st_ctimespec.tv_nsec);
#else
    return std::to_string(s.st_dev) + ":" + std::to_string(s.st_ino) + ":" +
           std::to_string(s.st_size) + ":" + std::to_string(s.st_mtim.tv_sec) + ":" +
           std::to_string(s.st_mtim.tv_nsec) + ":" + std::to_string(s.st_ctim.tv_sec) + ":" +
           std::to_string(s.st_ctim.tv_nsec);
#endif
}

// The native dialog can finish after the window closes. Its callback owns a
// shared state reference and never touches the document or SDL window.
struct DialogResult {
    std::mutex mutex;
    bool ready = false;
    std::string path, error;
};
void dialog_done(void* userdata, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<DialogResult>> owner(
        static_cast<std::shared_ptr<DialogResult>*>(userdata));
    std::lock_guard lock((*owner)->mutex);
    if (!files)
        (*owner)->error = SDL_GetError();
    else if (files[0])
        (*owner)->path = files[0];
    (*owner)->ready = true;
}

/* Aurora compiles the known pipeline configs ahead of use -- the ~12k-config
 * seed on desktop, on Android the ones this device has built before -- on
 * worker threads from startup, or, without workers (Android), in this loop's
 * idle time. Until a config is compiled, every draw that needs it is skipped
 * (issue #46), so the drain finishing before a match is the difference between
 * a match that pops and one that does not. The launcher is the one screen
 * where the player is already waiting; spend it there. */
static uint32_t pending_pipelines() {
    return aurora_wait_pipelines(0);
}

/* The platform, as a class on the launcher's <body>: launcher.rcss hides
 * what it has no use for (.no-mobile: the window mode, the keyboard). */
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr bool is_mobile = true;
#else
constexpr bool is_mobile = false;
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
constexpr const char* disc_note =
    "Use Choose Disc, or place a disc image in Files > On My iPhone > Melee.";
#elif defined(__ANDROID__)
constexpr const char* disc_note = "Use Choose Disc to pick a disc image.";
#else
constexpr const char* disc_note = "Drop a disc image here, or use Choose Disc.";
#endif
/* Melee colours the menu frame after the entry under the cursor. */
constexpr const char* main_ids[] = {
    "play", "replays", "choose", "verify", "settings", "discord", "quit"};
constexpr const char* replay_action_ids[] = {"replay-watch", "replay-export", "replay-stats",
    "replay-delete", "replay-folder", "replay-back"};
/* The stats page's categories, each a section of the panel beside them. */
/* One game's page (A on a replay): its sections, then Watch and Back. */
constexpr const char* game_tab_ids[] = {"game-summary", "game-stocks"};
constexpr const char* stats_tab_ids[] = {
    "stats-overview", "stats-characters", "stats-opponents", "stats-stages"};

/* Names for the replay list. */
const char* replay_fighter(int ckind) {
    static const char* const names[] = {"Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch",
        "Kirby", "Bowser", "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu",
        "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Young Link",
        "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
    return ckind >= 0 && ckind < int(std::size(names)) ? names[ckind] : "?";
}
std::string replay_stage(const PcReplayInfo& r) {
    if (r.stkind == 0x35) {
        static const char* const ace[] = {
            "ACE stage", "Metal Cavern", "WarioWare", "Realgam Colosseum"};
        return r.ace_pick < std::size(ace) ? ace[r.ace_pick] : ace[0];
    }
    static const char* const names[] = {"", "Test", "Fountain of Dreams", "Pok\xc3\xa9mon Stadium",
        "Princess Peach's Castle", "Kongo Jungle", "Brinstar", "Corneria", "Yoshi's Story", "Onett",
        "Mute City", "Rainbow Cruise", "Jungle Japes", "Great Bay", "Hyrule Temple",
        "Brinstar Depths", "Yoshi's Island", "Green Greens", "Fourside", "Mushroom Kingdom",
        "Mushroom Kingdom II", "Akaneia", "Venom", "Pok\xc3\xa9 Floats", "Big Blue",
        "Icicle Mountain", "Icetop", "Flat Zone", "Dream Land", "Yoshi's Island (N64)",
        "Kongo Jungle (N64)", "Battlefield", "Final Destination"};
    return r.stkind < std::size(names) ? names[r.stkind] : "Stage " + std::to_string(r.stkind);
}
std::string rml_escape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else
            out += c;
    }
    return out;
}

class Launcher final : public Rml::EventListener {
    SDL_Window* window;
    Rml::ElementDocument* document;
    std::shared_ptr<DialogResult> dialog;
    std::future<launcher::DiscInfo> inspection;
    std::future<launcher::Verification> verification;
    std::atomic_bool cancel{false};
    std::atomic_uint progress{0};
    std::string pending_path, verification_identity, selected_identity;
    bool supported = false, settings = false, quiet = false;
    // The replay list (Replays on the main menu).
    bool replays = false, delete_armed = false;
    // The update notice on the main menu (updater.cpp).
    bool update_dismissed = false, update_card_shown = false;
    pc::updater::Status last_update_status = pc::updater::Status::Idle;
    uint64_t last_update_done = ~0ull;
    // The stats page (Stats on the replay list), over the replay list.
    bool stats = false;
    // One game's page, over the replay list.
    bool game = false;
    int game_tab = 0;
    std::string game_rml[std::size(game_tab_ids)];
    int stats_tab = 0;
    std::string stats_rml[std::size(stats_tab_ids)];
    std::vector<PcReplayInfo> replay_list;
    std::vector<std::string> replay_rows;
    int replay_sel = -1;
    bool open_replays_after_inspect = false;
    std::string replay_status; /* what a replay export said, for the list */
    uint64_t last_identity_check = 0, last_axis = 0, last_axis_x = 0;
    unsigned last_progress = 101;
    std::vector<std::string> focus_ids;
    int result = -2, tab = 0;
    // Set once the player asks to play while the pipeline queue is still
    // draining: the game starts by itself when the queue empties, and a second
    // press starts it immediately.
    bool shaders_waited = false;
    uint32_t last_pending = 0, last_shader_pending = 0;
    // The settings row holding the focus, lit as Melee lights its options.
    Rml::Element* current_row = nullptr;
    // Where the pointer last moved, so only a real move steers the cursor.
    int pointer_x = -1, pointer_y = -1;

    Rml::Element* element(const char* id) {
        if (std::strcmp(id, "fullscreen") == 0) {
            auto* e = document->GetElementById("display");
            if (e)
                return e;
        }
        if (std::strcmp(id, "vsync") == 0) {
            auto* e = document->GetElementById("sync");
            if (e)
                return e;
        }
        return document->GetElementById(id);
    }
    void text(const char* id, const std::string& value) {
        auto* e = element(id);
        if (!e)
            return;
        e->SetInnerRML("");
        auto node = document->CreateTextNode(value);
        e->AppendChild(std::move(node));
    }
    void enabled(const char* id, bool value) {
        auto* e = element(id);
        if (!e)
            return;
        e->SetPseudoClass("disabled", !value);
        e->SetProperty("focus", value ? "auto" : "none");
        e->SetProperty("tab-index", value ? "auto" : "none");
        if (!value)
            e->Blur();
    }
    bool busy() const { return dialog || inspection.valid() || verification.valid(); }
    void status(const std::string& value, bool error = false) {
        text("status", value);
        auto* e = element("status");
        if (e)
            e->SetClass("error", error);
        // The disc panel is hidden behind the settings; say it there too.
        if (settings)
            note(value);
    }
    void save() {
        std::string error;
        if (!launcher::save_preferences(config_path, prefs, error))
            status(error, true);
    }
    std::vector<std::string> page_focus() const {
        switch (tab) {
        case 0:
            return {"display", "sync", "resolution", "aspect", "hud-mode", "magnifier", "aa",
                "filter", "filter-mode", "fps", "custom-textures", "backend", "performance"};
        case 1:
            return {"volume", "music-volume", "sfx-volume", "mute", "reverb"};
        case 2:
            return {"unlock-all", "free-camera", "ucf", "rumble", "results-screen", "save-replays"};
        case 4:
            return {"net-code", "net-target", "net-delay", "show-ping", "net-two-players"};
        case 5:
            return {"scale", "check-updates", "check-now", "settings-discord", "export-logs"};
        default:
            return {};
        }
    }
    static bool shown(Rml::Element* e) { return e && e->IsVisible(true); }
    static bool typing(Rml::Element* e) {
        return e && e->GetTagName() == "input" && !is_slider(e->GetId());
    }
    bool within(Rml::Element* e, const char* id) const {
        for (; e && e != document; e = e->GetParentNode())
            if (e->GetId() == id)
                return true;
        return false;
    }
    bool in_rows(Rml::Element* e) const { return within(e, "settings-content"); }
    /* What Up and Down walk through: the main menu's plates, or in the
     * settings whichever column holds the focus, the categories or the rows
     * of the open page. */
    std::vector<std::string> column() {
        if (game) {
            std::vector<std::string> ids(std::begin(game_tab_ids), std::end(game_tab_ids));
            ids.push_back("game-watch");
            ids.push_back("game-back");
            return ids;
        }
        if (stats) {
            std::vector<std::string> ids(std::begin(stats_tab_ids), std::end(stats_tab_ids));
            ids.push_back("stats-back");
            return ids;
        }
        if (replays) {
            if (within(document->GetContext()->GetFocusElement(), "replay-list"))
                return replay_rows;
            return {std::begin(replay_action_ids), std::end(replay_action_ids)};
        }
        if (!settings && within(document->GetContext()->GetFocusElement(), "update-card"))
            return {"update-action", "update-browser", "update-later"};
        if (!settings)
            return focus_ids;
        if (in_rows(document->GetContext()->GetFocusElement()))
            return page_focus();
        std::vector<std::string> ids(std::begin(tab_ids), std::end(tab_ids));
        ids.push_back("back");
        return ids;
    }
    void set_theme(const char* name) {
        for (const char* t : main_ids)
            document->SetClass(std::string("theme-") + t, std::strcmp(t, name) == 0);
    }
    /* The description bar says what the focused thing does, as Melee's does,
     * and its row lights up. */
    void describe(Rml::Element* e) {
        Rml::Element* row = nullptr;
        Rml::String desc;
        for (auto* p = e; p && p != document; p = p->GetParentNode()) {
            if (!row && p->IsClassSet("row"))
                row = p;
            if (desc.empty())
                desc = p->GetAttribute<Rml::String>("data-desc", "");
        }
        if (row != current_row) {
            if (current_row)
                current_row->SetClass("current", false);
            if (row)
                row->SetClass("current", true);
            current_row = row;
        }
        if (!desc.empty())
            text("desc", desc);
        if (!e)
            return;
        // Only one plate lit: the open page's gives way to Back's.
        document->SetClass("on-back",
            e->GetId() == "back" || e->GetId() == "stats-back" || e->GetId() == "game-back");
        if (game) {
            if (const int i = index_of(game_tab_ids, e->GetId()); i >= 0)
                select_game_tab(i);
        } else if (stats) {
            if (const int i = index_of(stats_tab_ids, e->GetId()); i >= 0)
                select_stats_tab(i);
        } else if (replays) {
            const auto id = e->GetId();
            if (id.rfind("replay-row-", 0) == 0)
                select_replay(std::atoi(id.c_str() + 11));
        } else if (!settings) {
            for (const char* t : main_ids)
                if (e->GetId() == t)
                    set_theme(t);
        } else if (const int i = index_of(tab_ids, e->GetId()); i >= 0 && i != tab)
            select_tab(i);  // the cursor on a category opens it, as Melee previews
    }
    /* A pointer moving onto a control moves the cursor there, so the mouse,
     * the Quest's laser and the pad share one highlight. Only a move: when the
     * layout shifts under a resting pointer (a fullscreen toggle, a page
     * change) RmlUi hovers whatever is now beneath it, and taking that as
     * pointing pulled the focus away from the keys. Not while a text field
     * holds the focus either: that would pull it out from under the typing.
     * Not onto the settings categories: focusing one opens its page, so the
     * pointer crossing them on its way to the rows flipped pages. They only
     * light up under it (launcher.rcss), and a click opens one. */
    void hover(Rml::Event& event) {
        const int x = event.GetParameter<int>("mouse_x", -1);
        const int y = event.GetParameter<int>("mouse_y", -1);
        if (x == pointer_x && y == pointer_y)
            return;
        pointer_x = x;
        pointer_y = y;
        auto* e = event.GetTargetElement();
        while (e && e != document && e->GetTagName() != "button" &&
               !(e->GetTagName() == "input" && is_slider(e->GetId())))
            e = e->GetParentNode();
        if (!e || e == document || e->IsPseudoClassSet("disabled"))
            return;
        if (settings && within(e, "categories"))
            return;
        auto* focus = document->GetContext()->GetFocusElement();
        if (e != focus && !typing(focus))
            e->Focus();
    }
    void controls() {
        enabled("play", supported && !busy());
        enabled("choose", !busy());
        enabled("verify", verification.valid() || (supported && !busy()));
        enabled("settings", !busy());
        enabled("discord", !busy());
        enabled("replays", !busy());
        text("verify-label", verification.valid() ? "Cancel Verify" : "Verify Disc");
        focus_ids.assign(std::begin(main_ids), std::end(main_ids));
    }
    // Opens a page without moving the focus (describe() calls it whenever a
    // category takes the focus, by keys or by pointer).
    void select_tab(int next) {
        const int count = std::size(tab_ids);
        tab = (next + count) % count;
        for (int i = 0; i < count; ++i) {
            if (auto* e = element(tab_ids[i]))
                e->SetClass("selected", i == tab);
            const auto page_id = "page-" + Rml::String(tab_ids[i]).substr(4);
            if (auto* e = element(page_id.c_str()))
                e->SetClass("selected", i == tab);
        }
        if (auto* e = element("settings-content"))
            e->SetScrollTop(0);
    }
    void show_tab(int next) {
        select_tab(next);
        if (auto* e = element(tab_ids[tab])) {
            e->Focus();
            e->ScrollIntoView();
        }
    }
    // Into the open page's first row (Right or A on a category).
    void enter_rows() {
        document->UpdateDocument();  // the page's display just changed
        for (const auto& id : page_focus()) {
            auto* e = element(id.c_str());
            if (shown(e) && !e->IsPseudoClassSet("disabled")) {
                e->Focus();
                e->ScrollIntoView();
                return;
            }
        }
    }
    // B or Escape: rows to categories, categories to the main menu.
    void back() {
        if (game) {
            show_game(false);
            return;
        }
        if (stats) {
            show_stats(false);
            return;
        }
        if (replays) {
            show_replays(false);
            return;
        }
        if (!settings) {
            if (verification.valid())
                cancel = true;
        } else if (in_rows(document->GetContext()->GetFocusElement()))
            show_tab(tab);
        else
            show_settings(false);
    }
    void focus_step(int direction) {
        auto* focus = document->GetContext()->GetFocusElement();
        const auto ids = column();
        const int count = static_cast<int>(ids.size());
        int index = direction > 0 ? -1 : 0;
        for (int i = 0; i < count; ++i)
            if (element(ids[i].c_str()) == focus)
                index = i;
        for (int i = 0; i < count; ++i) {
            index = (index + direction + count) % count;
            auto* e = element(ids[index].c_str());
            if (shown(e) && !e->IsPseudoClassSet("disabled")) {
                e->Focus();
                e->ScrollIntoView();
                break;
            }
        }
    }
    void activate_focus() {
        auto* e = document->GetContext()->GetFocusElement();
        if (!e || e == document || e->IsPseudoClassSet("disabled"))
            return;
        const auto id = e->GetId();
        if (settings && index_of(tab_ids, id) >= 0) {
            show_tab(index_of(tab_ids, id));
            enter_rows();
        } else if (e->GetTagName() == "button" || is_slider(id))
            action(id);
    }
    /* Left and Right: Melee's arrows. They step a value either way, move a
     * slider, or cross between the categories and the rows. */
    void horizontal(int direction) {
        auto* focus = document->GetContext()->GetFocusElement();
        if (!focus || focus->IsPseudoClassSet("disabled"))
            return;
        const auto id = focus->GetId();
        if (game || stats) {
            /* Up and Down pick a section; Left and Right scroll it. */
            if (auto* panel = element(game ? "game-panel" : "stats-panel"))
                panel->SetScrollTop(std::max(0.0f, panel->GetScrollTop() + direction * 160.0f));
            return;
        }
        if (replays) {
            if (direction < 0 && within(focus, "replay-list"))
                element("replay-watch")->Focus();
            else if (direction > 0 && !within(focus, "replay-list") && !replay_rows.empty()) {
                const int i = replay_sel >= 0 ? replay_sel : 0;
                if (auto* row = element(replay_rows[i].c_str())) {
                    row->Focus();
                    row->ScrollIntoView();
                }
            }
            return;
        }
        if (!settings) {
            /* The main menu: Right to the update notice beside it, Left back. */
            if (within(focus, "update-card")) {
                if (direction < 0)
                    element(supported ? "play" : "choose")->Focus();
            } else if (direction > 0 && update_card_shown)
                element("update-action")->Focus();
            return;
        }
        if (settings && index_of(tab_ids, id) >= 0) {
            if (direction > 0)
                enter_rows();
        } else if (is_slider(id))
            step_slider(direction);
        else if (focus->IsClassSet("value") && !focus->IsClassSet("action")) {
            if (!cycle(id, direction))
                action(id);  // two states: either arrow flips it
        } else if (settings && direction < 0)
            show_tab(tab);
    }
    /* The settings with more than two values, stepped either way. */
    bool cycle(const std::string& id, int direction) {
        auto wrap = [](int value, int count) { return (value % count + count) % count; };
        if (id == "backend") {
            action(id, direction);
            return true;
        } else if (id == "aspect")
            prefs.widescreen = wrap(prefs.widescreen + direction, 3);
        else if (id == "filter") {
            int power = 0;
            while (power < 4 && (1 << power) < prefs.anisotropy)
                ++power;
            prefs.anisotropy = 1 << wrap(power + direction, 5);
        } else if (id == "filter-mode") {
            prefs.filter_mode = wrap(prefs.filter_mode + direction, 4);
            aurora_set_resampler(static_cast<AuroraSampler>(prefs.filter_mode));
        } else if (id == "net-delay")
            prefs.net_delay = wrap(prefs.net_delay + 1 + direction, 6) - 1;  // Auto, 0-4
        else if (id == "show-ping")
            prefs.show_ping = !prefs.show_ping;
        else if (id == "net-two-players")
            prefs.net_two_players = !prefs.net_two_players;
        else
            return false;
        save();
        refresh_settings();
        return true;
    }
    void slider(const char* id, float value) {
        auto* e = document->GetElementById(id);
        if (e)
            e->SetAttribute("value", value);
    }
    static bool is_slider(const Rml::String& id) {
        return id == "volume" || id == "resolution" || id == "scale" || id == "music-volume" ||
               id == "sfx-volume";
    }
    // Writing a slider's value fires Change; `quiet` keeps that from looping
    // back into another save.
    void refresh_settings() {
        if (!settings)
            return;
        quiet = true;
        refresh_online(document);
        text("display", display_mode());
        const char* override = std::getenv("MELEE_VSYNC");
        const bool effective_vsync = override ? override[0] != '0' : prefs.vsync;
        text("sync",
            std::string(effective_vsync ? "On" : "Off") + (override ? " (environment)" : ""));
        slider("resolution", prefs.render_scale);
        text("resolution-val", resolution_name(prefs.render_scale));
        text("aspect", prefs.widescreen == 0 ? "Original (73:60)" :
                       prefs.widescreen == 1 ? "Widescreen 16:9" :
                                               "Auto (window aspect)");
        text("aa", prefs.msaa == 1 ? "Off" : std::to_string(prefs.msaa) + "x MSAA");
        text("filter", std::to_string(prefs.anisotropy) + "x");
        text("filter-mode", filter_name(prefs.filter_mode));
        text("custom-textures", prefs.custom_textures ? "Enabled" : "Disabled");
        text("hud-mode", prefs.hud_mode == 0 ? "Classic (4:3)" : "Wide (16:9)");
        text("magnifier", prefs.magnifier ? "On" : "Widescreen (top only)");
        text("frozen-stadium", prefs.frozen_stadium ? "Hazardless" : "Normal");
        text("squad-strike", prefs.squad_strike ? "On" : "Off");
        text("stage-expansion", prefs.stage_expansion ? "On" : "Off");
        text("sd-remix", prefs.sd_remix ? "On" : "Off");
        text("free-camera", prefs.free_camera ? "Free" : "Normal");
        text("results-screen", prefs.results_screen ? "On" : "Off");
        text("save-replays", prefs.save_replays ? "On" : "Off");
        text("check-updates", prefs.check_updates ? "On" : "Off");
        {
            const auto st = pc::updater::state();
            text("check-status",
                st.message.empty() ? "Version " + pc::get_app_version() : st.message);
        }
        text("ucf", std::getenv("MELEE_UCF") ? "Environment override" : prefs.ucf ? "On" : "Off");
        text("rumble", prefs.rumble ? "On" : "Off");
        text("unlock-all", prefs.unlock_all ? "Unlocked" : "Normal");
        text("backend", backend_name(prefs.backend));
        slider("volume", prefs.volume * 100.0f);
        text("volume-val", std::to_string(int(prefs.volume * 100 + 0.5f)) + "%");
        slider("music-volume", prefs.music_volume * 100.0f);
        text("music-volume-val", std::to_string(int(prefs.music_volume * 100 + 0.5f)) + "%");
        slider("sfx-volume", prefs.sfx_volume * 100.0f);
        text("sfx-volume-val", std::to_string(int(prefs.sfx_volume * 100 + 0.5f)) + "%");
        text("mute", prefs.mute ? "On" : "Off");
        text("reverb", prefs.reverb ? "On" : "Off");
        text("fps", prefs.fps ? "On" : "Off");
        slider("scale", prefs.scale * 100.0f);
        text("scale-val", std::to_string(int(prefs.scale * 100 + 0.5f)) + "%");
        notice();
        quiet = false;
    }
    // The line under the settings rows, shown only while it has something
    // to say (settings save as they change, so that goes without saying).
    void note(const std::string& value) {
        text("settings-status", value);
        if (auto* e = element("settings-status"))
            e->SetProperty("display", value.empty() ? "none" : "block");
    }
    void notice() {
        note(prefs.render_scale >= 6 ?
                 resolution_name(prefs.render_scale) +
                     " native needs a lot of video memory; drop it if the game stutters." :
                 std::string());
    }
    void show_settings(bool show) {
        settings = show;
        text("title-sub", "/ Settings");
        document->SetClass("in-settings", show);
        refresh_settings();
        if (show) {
            set_theme("settings");
            show_tab(tab);
            return;
        }
        controls();
        element("settings")->Focus();
    }
    /* ---- Replays ---- */
    void show_replays(bool show) {
        replays = show;
        delete_armed = false;
        document->SetClass("in-replays", show);
        if (show) {
            text("title-sub", "/ Replays");
            set_theme("replays");
            refresh_replays();
            if (!replay_rows.empty()) {
                if (auto* row = element(replay_rows[0].c_str()))
                    row->Focus();
            } else
                element("replay-back")->Focus();
            return;
        }
        controls();
        element("replays")->Focus();
    }
    void refresh_replays() {
        replay_list.resize(1000);
        const int n = pc_replay_list(replay_list.data(), int(replay_list.size()));
        replay_list.resize(std::max(n, 0));
        replay_rows.clear();
        std::string rml;
        for (size_t i = 0; i < replay_list.size(); ++i) {
            const auto& r = replay_list[i];
            const auto id = "replay-row-" + std::to_string(i);
            replay_rows.push_back(id);
            rml += "<button id=\"" + id + "\" class=\"replay" + (r.playable ? "" : " unplayable") +
                   "\" data-desc=\"" + rml_escape(replay_desc(r)) + "\">" +
                   rml_escape(replay_title(r)) + "<span class=\"detail\">" +
                   rml_escape(replay_detail(r)) + "</span></button>";
        }
        if (rml.empty())
            rml = "<p class=\"note\">No replays yet. Every match you play is saved here "
                  "(Settings, Gameplay, Save replays).</p>";
        if (auto* list = element("replay-list"))
            list->SetInnerRML(rml);
        replay_sel = replay_list.empty() ? -1 : 0;
        update_replay_buttons();
    }
    static std::string fighters(const PcReplayPlayer& p) {
        std::string s = replay_fighter(p.ckind);
        for (int k = 0; k < 3; ++k)
            if (p.squad[k] >= 0)
                s += std::string(" + ") + replay_fighter(p.squad[k]);
        if (p.cpu)
            s += " (CPU)";
        return s;
    }
    std::string replay_title(const PcReplayInfo& r) const {
        if (!r.playable)
            return r.file;
        std::string s;
        for (const auto& p : r.players) {
            if (p.ckind < 0)
                continue;
            if (!s.empty())
                s += "  vs  ";
            s += fighters(p);
        }
        return s.empty() ? r.file : s;
    }
    std::string replay_detail(const PcReplayInfo& r) const {
        std::string s;
        SDL_DateTime dt;
        if (SDL_TimeToDateTime(SDL_Time(r.created) * SDL_NS_PER_SECOND, &dt, true)) {
            static const char* const months[] = {
                "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            const int hour = dt.hour % 12 == 0 ? 12 : dt.hour % 12;
            char when[64];
            std::snprintf(when, sizeof when, "%s %d, %d  %d:%02d %s", months[(dt.month + 11) % 12],
                dt.day, dt.year, hour, dt.minute, dt.hour < 12 ? "AM" : "PM");
            s = when;
        }
        if (!r.playable)
            return s + "  \xc2\xb7  made by " + r.version + ", can't be played on this version";
        const unsigned secs = r.frames / 60;
        s += "  \xc2\xb7  " + replay_stage(r) + "  \xc2\xb7  " + std::to_string(secs / 60) + ":" +
             (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
        if (r.online)
            s += "  \xc2\xb7  Online";
        if (r.sdremix)
            s += "  \xc2\xb7  SD Remix";
        if (r.has_stats) {
            if (!r.has_result)
                s += "  \xc2\xb7  No result";
            else {
                std::string who;
                for (int i = 0; i < 4; ++i)
                    if ((r.winners_mask >> i) & 1u)
                        who += (who.empty() ? "P" : " and P") + std::to_string(i + 1);
                s += "  \xc2\xb7  " + who + " won";
            }
        } else if (r.stock_match && !r.is_teams) {
            /* Before results were kept: the only side with stocks left. */
            int alive = 0, port = -1;
            for (int i = 0; i < 4; ++i)
                if (r.players[i].ckind >= 0 && r.players[i].end_stocks > 0) {
                    ++alive;
                    port = i;
                }
            if (alive == 1)
                s += "  \xc2\xb7  P" + std::to_string(port + 1) + " won";
        }
        return s;
    }
    std::string replay_desc(const PcReplayInfo& r) const {
        return std::string(r.file) + "  \xc2\xb7  " + std::to_string(r.bytes / 1024 + 1) +
               " KB  \xc2\xb7  " + r.version;
    }
    void select_replay(int i) {
        if (i < 0 || i >= int(replay_list.size()) || i == replay_sel)
            return;
        if (replay_sel >= 0 && replay_sel < int(replay_rows.size()))
            if (auto* e = element(replay_rows[replay_sel].c_str()))
                e->SetClass("selected", false);
        replay_sel = i;
        delete_armed = false;
        if (auto* e = element(replay_rows[i].c_str()))
            e->SetClass("selected", true);
        update_replay_buttons();
    }
    void update_replay_buttons() {
        const bool any = replay_sel >= 0 && replay_sel < int(replay_list.size());
        const bool playable = any && replay_list[replay_sel].playable;
        enabled("replay-watch", playable);
        enabled("replay-export", playable);
        enabled("replay-delete", any);
        if (any && replay_sel < int(replay_rows.size()))
            if (auto* e = element(replay_rows[replay_sel].c_str()))
                e->SetClass("selected", true);
    }
    void replay_action(const std::string& id) {
        if (id == "replay-back") {
            show_replays(false);
            return;
        }
        if (id == "replay-stats") {
            show_stats(true);
            return;
        }
        if (id == "replay-folder") {
            std::string url = "file://";
            const char* dir = pc_replay_folder();
            if (dir[0] != '/')
                url += "/";
            url += dir;
            SDL_OpenURL(url.c_str());
            return;
        }
        if (id.rfind("replay-row-", 0) == 0) {
            select_replay(std::atoi(id.c_str() + 11));
            /* A on a match opens its page (stats, stocks, Watch). */
            if (replay_sel >= 0 && replay_list[replay_sel].playable)
                show_game(true);
            return;
        }
        if (replay_sel < 0 || replay_sel >= int(replay_list.size()))
            return;
        const auto& r = replay_list[replay_sel];
        if (id == "replay-delete") {
            if (!delete_armed) {
                delete_armed = true;
                status("Press Delete again to delete " + std::string(r.file) + ".");
                return;
            }
            std::error_code ec;
            std::filesystem::remove(r.path, ec);
            status(ec ? "Could not delete " + std::string(r.file) + "." :
                        "Deleted " + std::string(r.file) + ".",
                bool(ec));
            const int keep = replay_sel;
            refresh_replays();
            if (!replay_rows.empty()) {
                const int i = std::min(keep, int(replay_rows.size()) - 1);
                if (auto* row = element(replay_rows[i].c_str()))
                    row->Focus();
            } else
                element("replay-back")->Focus();
            return;
        }
        if (id == "replay-watch") {
            if (!supported) {
                status("Choose your Melee disc first: replays play from it.", true);
                return;
            }
            pc_replay_request(r.path, nullptr);
            SDL_Log("Launcher: watching %s", r.path);
            begin_game();
            return;
        }
        if (id == "replay-export") {
            start_export(r);
            return;
        }
    }
    void start_export(const PcReplayInfo& r) {
        if (!pc_video_available()) {
#if defined(_WIN32)
            status("Video export is not available here.", true);
#else
            status("Video export needs ffmpeg installed (it is built in on Windows).", true);
#endif
            return;
        }
        if (!supported) {
            status("Choose your Melee disc first: replays play from it.", true);
            return;
        }
        /* Videos go to the system's Videos folder, under Melee Replays. */
        std::filesystem::path dir;
        if (const char* videos = SDL_GetUserFolder(SDL_FOLDER_VIDEOS))
            dir = std::filesystem::u8path(videos) / "Melee Replays";
        else
            dir = std::filesystem::u8path(pc_replay_folder());
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        auto out = dir / std::filesystem::u8path(r.file);
        out.replace_extension(".mp4");
        const auto u8 = out.u8string();
        const std::string out_utf8(u8.begin(), u8.end());
        pc_replay_request(r.path, out_utf8.c_str());
        SDL_Log("Launcher: exporting %s to %s", r.path, out_utf8.c_str());
        begin_game();
    }
    /* The update notice: shown from the moment a newer release is found until
     * Later, and the restart once it is installed. */
    void refresh_update(bool force = false) {
        using pc::updater::Status;
        const auto st = pc::updater::state();
        if (!force && st.status == last_update_status && st.done == last_update_done)
            return;
        last_update_status = st.status;
        last_update_done = st.done;
        if (st.status == Status::Ready) {
            status(st.message);
            if (aurora_begin_frame()) /* show it before the restart */
                aurora_end_frame();
            pc::updater::restart();
        }
        const bool busy_update =
            st.status == Status::Downloading || st.status == Status::Installing;
        const bool show =
            !update_dismissed && !st.latest.empty() &&
            (st.status == Status::Available || busy_update || st.status == Status::Failed);
        if (auto* card = element("update-card"))
            card->SetProperty("display", show ? "block" : "none");
        if (!show && update_card_shown &&
            within(document->GetContext()->GetFocusElement(), "update-card"))
            element(supported ? "play" : "choose")->Focus();
        update_card_shown = show;
        if (settings)
            refresh_settings();
        if (!show)
            return;
        text("update-tag", st.latest);
        text("update-desc",
            st.status == Status::Failed || busy_update ? st.message :
            st.can_install ? "Update now downloads it and restarts. Your settings, replays and "
                             "disc stay as they are." :
                             "Get it from the release page.");
        text("update-action", st.status == Status::Failed ? "Try again" :
                              st.can_install              ? "Update now" :
                                                            "Download");
        enabled("update-action", !busy_update);
        enabled("update-later", !busy_update);
        if (auto* row = element("update-progress-row"))
            row->SetProperty("display", busy_update ? "flex" : "none");
        if (busy_update) {
            const int pct = st.total ? int(std::min<uint64_t>(100, st.done * 100 / st.total)) : 0;
            if (auto* fill = element("update-progress-fill"))
                fill->SetProperty("width", std::to_string(pct) + "%");
            text("update-progress-text",
                st.status == Status::Installing ? "..." : std::to_string(pct) + "%");
        }
    }
    /* ---- Stats: worked out from the replay files ---- */
    struct Record {
        int games = 0, wins = 0, losses = 0;
        void add(int result) {
            ++games;
            wins += result > 0;
            losses += result < 0;
        }
    };
    static std::string percent(const Record& r) {
        const int decided = r.wins + r.losses;
        return decided ? std::to_string((r.wins * 100 + decided / 2) / decided) + "%" : "-";
    }
    static std::string duration(uint64_t frames) {
        const uint64_t mins = frames / 3600;
        if (mins < 60)
            return std::to_string(mins) + " min";
        return std::to_string(mins / 60) + " h " + std::to_string(mins % 60) + " min";
    }
    static std::string stat_tile(const std::string& value, const std::string& label) {
        return "<div class=\"stat-tile\"><div class=\"stat-value\">" + rml_escape(value) +
               "</div><div class=\"stat-label\">" + rml_escape(label) + "</div></div>";
    }
    /* A section's table, most played first. */
    static std::string stat_table(
        const std::map<std::string, Record>& records, const char* heading, const char* empty) {
        std::vector<std::pair<std::string, Record>> rows(records.begin(), records.end());
        std::stable_sort(rows.begin(), rows.end(),
            [](const auto& a, const auto& b) { return a.second.games > b.second.games; });
        std::string rml = "<div class=\"stat-row stat-head\"><span class=\"stat-name\">" +
                          std::string(heading) +
                          "</span><span class=\"stat-num\">Games</span><span "
                          "class=\"stat-num\">W - L</span><span class=\"stat-num\">No "
                          "result</span><span class=\"stat-num\">Win rate</span></div>";
        for (const auto& [name, r] : rows)
            rml += "<div class=\"stat-row\"><span class=\"stat-name\">" + rml_escape(name) +
                   "</span><span class=\"stat-num\">" + std::to_string(r.games) +
                   "</span><span class=\"stat-num\">" + std::to_string(r.wins) + " - " +
                   std::to_string(r.losses) + "</span><span class=\"stat-num\">" +
                   std::to_string(r.games - r.wins - r.losses) +
                   "</span><span class=\"stat-num\">" + percent(r) + "</span></div>";
        if (rows.empty())
            rml += std::string("<p class=\"note\">") + empty + "</p>";
        return rml;
    }
    void compute_stats() {
        std::vector<PcReplayInfo> list(5000);
        const int n = pc_replay_list(list.data(), int(list.size()));
        list.resize(std::max(n, 0));
        Record you;
        std::map<std::string, Record> characters, opponents, against, stages;
        std::map<std::string, int> stage_games;
        int total = 0, online = 0, known = 0, streak = 0;
        char streak_kind = 0;
        bool streak_open = true;
        uint64_t frames = 0;
        std::string recent;
        for (const auto& r : list) {  // newest first
            if (!r.playable)
                continue;
            ++total;
            online += r.online;
            frames += r.frames;
            ++stage_games[replay_stage(r)];
            /* The winning side of a stock match: the one side with stocks
             * left (a team, or a port). A time match, or one left early,
             * has none. */
            auto side = [&](int port) { return r.is_teams ? int(r.players[port].team) : port; };
            int winner = -1;
            if (r.stock_match) {
                for (int i = 0; i < 4; ++i) {
                    if (r.players[i].ckind < 0 || r.players[i].end_stocks <= 0)
                        continue;
                    if (winner == -1)
                        winner = side(i);
                    else if (winner != side(i))
                        winner = -2;
                }
            }
            const int me = r.local_port;
            if (me < 0 || me > 3 || r.players[me].ckind < 0)
                continue;
            ++known;
            /* The game's own result where the replay kept it (time-outs,
             * Squad Strike and team battles included); else the one side
             * with stocks left. */
            int result = winner < 0 ? 0 : winner == side(me) ? 1 : -1;
            if (r.has_stats)
                result = !r.has_result ? 0 : (r.winners_mask >> me) & 1u ? 1 : -1;
            you.add(result);
            if (result != 0) {
                if (recent.size() < 10)
                    recent += result > 0 ? 'W' : 'L';
                const char kind = result > 0 ? 'W' : 'L';
                if (streak_open && (streak_kind == 0 || streak_kind == kind)) {
                    streak_kind = kind;
                    ++streak;
                } else
                    streak_open = false;
            }
            std::set<std::string> mine, theirs, them;
            auto fighters_of = [](const PcReplayPlayer& p, std::set<std::string>& into) {
                into.insert(replay_fighter(p.ckind));
                for (int k = 0; k < 3; ++k)
                    if (p.squad[k] >= 0)
                        into.insert(replay_fighter(p.squad[k]));
            };
            fighters_of(r.players[me], mine);
            for (int i = 0; i < 4; ++i) {
                const auto& p = r.players[i];
                if (i == me || p.ckind < 0 || side(i) == side(me))
                    continue;
                fighters_of(p, theirs);
                them.insert(p.cpu    ? std::string("CPU") :
                            r.online ? (r.opponent[0] ? std::string(r.opponent) :
                                                        std::string("Online player")) :
                                       "Player " + std::to_string(i + 1));
            }
            for (const auto& c : mine)
                characters[c].add(result);
            for (const auto& c : theirs)
                against[c].add(result);
            for (const auto& o : them)
                opponents[o].add(result);
            stages[replay_stage(r)].add(result);
        }
        std::string most_played = "-", favourite_stage = "-";
        int best = 0;
        for (const auto& [name, rec] : characters)
            if (rec.games > best) {
                best = rec.games;
                most_played = name;
            }
        best = 0;
        for (const auto& [name, games] : stage_games)
            if (games > best) {
                best = games;
                favourite_stage = name;
            }
        std::string form;
        for (char c : recent)
            form += std::string("<span class=\"form ") + (c == 'W' ? "win" : "loss") + "\">" + c +
                    "</span>";
        auto& overview = stats_rml[0];
        overview = "<div class=\"stat-tiles\">" + stat_tile(std::to_string(total), "Matches") +
                   stat_tile(std::to_string(you.wins) + " - " + std::to_string(you.losses),
                       "Your wins - losses") +
                   stat_tile(percent(you), "Win rate") +
                   stat_tile(duration(frames), "Time played") +
                   stat_tile(std::to_string(online), "Online matches") +
                   stat_tile(streak ? std::to_string(streak) + " " +
                                          (streak_kind == 'W' ? (streak == 1 ? "win" : "wins") :
                                                                (streak == 1 ? "loss" : "losses")) :
                                      std::string("-"),
                       "Current streak") +
                   stat_tile(most_played, "Most played") +
                   stat_tile(favourite_stage, "Most played stage") + "</div>";
        overview += "<div class=\"stat-form\"><span class=\"stat-label\">Last 10</span>" +
                    (form.empty() ? std::string("<span class=\"stat-label\"> -</span>") : form) +
                    "</div>";
        overview += "<p class=\"note\">From your " + std::to_string(total) +
                    " saved replays. Wins and losses count matches where the replay knows which "
                    "player you were (online matches, and offline ones with a single human player) "
                    "and the match had a winner. No result: a match left early or lost to a "
                    "disconnect, or one from before results were saved where it could not tell" +
                    (known < total ? " (" + std::to_string(total - known) + " could not tell)" :
                                     std::string()) +
                    ".</p>";
        stats_rml[1] = stat_table(characters, "Your fighters", "No matches yet.") + "<br/>" +
                       stat_table(against, "Against", "");
        stats_rml[2] = stat_table(opponents, "Opponent", "No matches yet.");
        stats_rml[3] = stat_table(stages, "Stage", "No matches yet.");
    }
    void select_stats_tab(int i) {
        stats_tab = i;
        for (int k = 0; k < int(std::size(stats_tab_ids)); ++k)
            if (auto* e = element(stats_tab_ids[k]))
                e->SetClass("selected", k == i);
        if (auto* panel = element("stats-panel")) {
            panel->SetInnerRML(stats_rml[i]);
            panel->SetScrollTop(0);
        }
    }
    void show_stats(bool show) {
        stats = show;
        document->SetClass("in-stats", show);
        if (show) {
            text("title-sub", "/ Stats");
            compute_stats();
            select_stats_tab(0);
            element(stats_tab_ids[0])->Focus();
            return;
        }
        text("title-sub", "/ Replays");
        element("replay-stats")->Focus();
    }
    /* ---- One game's page ---- */
    static std::string clock(uint32_t frames) {
        const unsigned secs = frames / 60;
        return std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") +
               std::to_string(secs % 60);
    }
    static std::string fixed1(double v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f", v);
        return buf;
    }
    /* A player by port and connect code: not by fighter, which Squad
     * Strike changes mid-game. */
    std::string player_name(const PcReplayInfo& r, int i) const {
        std::string s = "P" + std::to_string(i + 1);
        if (r.players[i].cpu)
            return s + " \xc2\xb7 CPU";
        if (i == r.local_port && r.name[0] && std::strcmp(r.name, "PLAYER") != 0)
            return s + " \xc2\xb7 " + r.name;
        if (i != r.local_port && r.online && r.opponent[0])
            return s + " \xc2\xb7 " + r.opponent;
        return s;
    }
    void build_game(const PcReplayInfo& r) {
        GmMatchStats st{};
        const bool have = pc_replay_read_stats(r.path, &st);
        std::vector<int> ports;
        for (int i = 0; i < 4; ++i)
            if (r.players[i].ckind >= 0)
                ports.push_back(i);
        std::string head = "<p class=\"game-head\">" + rml_escape(replay_detail(r)) + "</p>";
        if (!have) {
            game_rml[0] = head +
                          "<p class=\"note\">Detailed stats are kept for games played on "
                          "v1.0.11 and later. This one has only how many stocks each player had "
                          "left:</p>";
            for (int i : ports)
                game_rml[0] += "<div class=\"stat-row\"><span class=\"stat-name\">" +
                               rml_escape(player_name(r, i)) + "</span><span class=\"stat-num\">" +
                               std::to_string(std::max(0, int(r.players[i].end_stocks))) +
                               "</span></div>";
            game_rml[1] = game_rml[0];
            return;
        }
        const double minutes = st.frames / 3600.0;
        auto row = [&](const char* label, auto cell) {
            std::string out = "<div class=\"stat-row\"><span class=\"stat-name\">" +
                              std::string(label) + "</span>";
            for (int i : ports)
                out += "<span class=\"game-num\">" + rml_escape(cell(st.p[i], i)) + "</span>";
            return out + "</div>";
        };
        std::string t = head + "<div class=\"stat-row stat-head\"><span class=\"stat-name\">"
                               "</span>";
        for (int i : ports)
            t += "<span class=\"game-num\">" + rml_escape(player_name(r, i)) + "</span>";
        t += "</div>";
        t += row("Result", [&](const GmMatchStatsPlayer&, int i) -> std::string {
            if (!st.has_result || st.winners_mask == 0)
                return "No result";
            return ((st.winners_mask >> i) & 1u) ? "Win" : "Loss";
        });
        t += row("Kills", [](const GmMatchStatsPlayer& p, int) { return std::to_string(p.kos); });
        t +=
            row("Deaths", [](const GmMatchStatsPlayer& p, int) { return std::to_string(p.falls); });
        t += row("Self-destructs",
            [](const GmMatchStatsPlayer& p, int) { return std::to_string(p.sds); });
        t += row("Damage dealt",
            [](const GmMatchStatsPlayer& p, int) { return fixed1(p.damage_dealt) + "%"; });
        t += row("Damage taken",
            [](const GmMatchStatsPlayer& p, int) { return fixed1(p.damage_taken) + "%"; });
        t += row("Openings",
            [](const GmMatchStatsPlayer& p, int) { return std::to_string(p.openings); });
        t += row("Damage per opening", [](const GmMatchStatsPlayer& p, int) -> std::string {
            return p.openings ? fixed1(p.opening_damage / p.openings) + "%" : "-";
        });
        t += row("Openings per kill", [](const GmMatchStatsPlayer& p, int) -> std::string {
            return p.kos ? fixed1(double(p.openings) / p.kos) : "-";
        });
        t += row("Neutral wins",
            [](const GmMatchStatsPlayer& p, int) { return std::to_string(p.neutral_wins); });
        t += row("Best punish",
            [](const GmMatchStatsPlayer& p, int) { return fixed1(p.best_punish) + "%"; });
        t += row("L-cancels", [](const GmMatchStatsPlayer& p, int) -> std::string {
            if (!p.lcancel_total)
                return "-";
            return std::to_string(p.lcancel_ok * 100 / p.lcancel_total) + "% (" +
                   std::to_string(p.lcancel_ok) + "/" + std::to_string(p.lcancel_total) + ")";
        });
        t += row("Ledge grabs",
            [](const GmMatchStatsPlayer& p, int) { return std::to_string(p.ledge_grabs); });
        t += row("Actions per minute", [&](const GmMatchStatsPlayer& p, int) -> std::string {
            return minutes > 0.1 ? std::to_string(int(p.actions / minutes + 0.5)) : "-";
        });
        t += "<p class=\"note\">An opening is a punish: hits on an opponent until they go "
             "45 frames without being hit. A neutral win is one started while you were not "
             "being punished yourself.</p>";
        game_rml[0] = t;

        std::string k = head;
        for (int i : ports) {
            const auto& p = st.p[i];
            k += "<div class=\"stat-row stat-head\"><span class=\"stat-name\">" +
                 rml_escape(player_name(r, i)) +
                 "</span><span class=\"game-num\">Time</span>"
                 "<span class=\"game-num\">Damage</span><span class=\"game-num\">By</span></div>";
            if (p.n_deaths == 0)
                k += "<p class=\"note\">Lost no stocks.</p>";
            for (int d = 0; d < p.n_deaths && d < GMSTATS_DEATHS; ++d) {
                const auto& death = p.deaths[d];
                std::string by = death.by >= 0 && death.by < 4 && death.by != i ?
                                     player_name(r, death.by) :
                                     std::string("Self-destruct");
                k += "<div class=\"stat-row\"><span class=\"stat-name\">Stock " +
                     std::to_string(d + 1) + " \xc2\xb7 " + replay_fighter(death.ckind) +
                     "</span><span class=\"game-num\">" + clock(death.frame) +
                     "</span><span class=\"game-num\">" + fixed1(death.percent) +
                     "%</span><span class=\"game-num\">" + rml_escape(by) + "</span></div>";
            }
            k += "<br/>";
        }
        game_rml[1] = k;
    }
    void select_game_tab(int i) {
        game_tab = i;
        for (int k = 0; k < int(std::size(game_tab_ids)); ++k)
            if (auto* e = element(game_tab_ids[k]))
                e->SetClass("selected", k == i);
        if (auto* panel = element("game-panel")) {
            panel->SetInnerRML(game_rml[i]);
            panel->SetScrollTop(0);
        }
    }
    void show_game(bool show) {
        game = show;
        document->SetClass("in-game", show);
        if (show) {
            build_game(replay_list[replay_sel]);
            text("title-sub", "/ Game");
            select_game_tab(0);
            element(game_tab_ids[0])->Focus();
            return;
        }
        text("title-sub", "/ Replays");
        if (replay_sel >= 0 && replay_sel < int(replay_rows.size()))
            if (auto* row = element(replay_rows[replay_sel].c_str()))
                row->Focus();
    }
    // Left/right on a slider.
    void step_slider(int direction) {
        auto* focus = document->GetContext()->GetFocusElement();
        if (!focus)
            return;
        auto id = focus->GetId();
        if (id == "volume")
            prefs.volume = std::clamp(prefs.volume + direction * 0.05f, 0.0f, 1.0f);
        else if (id == "music-volume")
            prefs.music_volume = std::clamp(prefs.music_volume + direction * 0.05f, 0.0f, 1.0f);
        else if (id == "sfx-volume")
            prefs.sfx_volume = std::clamp(prefs.sfx_volume + direction * 0.05f, 0.0f, 1.0f);
        else if (id == "resolution")
            prefs.render_scale = std::clamp(prefs.render_scale + direction, 0.0f, 10.0f);
        else if (id == "scale") {
            prefs.scale = std::clamp(prefs.scale + direction * 0.25f, 0.75f, 1.5f);
            aurora::rmlui::set_ui_scale(prefs.scale);
        } else
            return;
        refresh_settings();
        save();
    }
    /* Open the selected disc and leave for the game. False when the disc will
     * not open, in which case the status line says why and we stay here. */
    bool begin_game() {
        auto check = launcher::inspect_disc(prefs.disc);
        if (!check.supported) {
            supported = false;
            status(check.message, true);
            controls();
            return false;
        }
        if (!aurora_dvd_open(prefs.disc.c_str())) {
            aurora_dvd_close();
            status("Could not load this disc. Choose another image or verify it.", true);
            return false;
        }
        pc_load_disc_fonts(prefs.disc.c_str());
        save();
        result = 1;
        return true;
    }
    void inspect(const std::string& path) {
        pending_path =
            (path.rfind("content://", 0) == 0) ? path : std::filesystem::absolute(path).string();
        status("Checking disc image...");
        inspection =
            std::async(std::launch::async, [path] { return launcher::inspect_disc(path); });
        controls();
    }
    void action(const std::string& id, int direction = 1) {
        if (id == "replays" && !busy()) {
            show_replays(true);
            return;
        }
        if (game) {
            if (id == "game-back")
                show_game(false);
            else if (id == "game-watch") {
                show_game(false);
                replay_action("replay-watch");
            } else if (const int i = index_of(game_tab_ids, id); i >= 0)
                select_game_tab(i);
            return;
        }
        if (stats) {
            if (id == "stats-back")
                show_stats(false);
            else if (const int i = index_of(stats_tab_ids, id); i >= 0)
                select_stats_tab(i);
            return;
        }
        if (replays && id.rfind("replay-", 0) == 0) {
            replay_action(id);
            return;
        }
        if (id == "export-logs") {
            pc_android_export_logs();
            return;
        }
        if (id == "net-delay" || id == "show-ping" || id == "net-two-players" || id == "aspect" ||
            id == "filter" || id == "filter-mode")
        {
            cycle(id, 1);
            return;
        }
        if (id == "quit") {
            result = 0;
            cancel = true;
            return;
        }
        if (id == "update-action") {
            const auto st = pc::updater::state();
            if (st.can_install) {
                pc::updater::install_async();
            } else {
                status("Opening the " + st.latest + " release page...");
                SDL_OpenURL(st.release_url.c_str());
            }
            return;
        }
        if (id == "update-browser") {
            const auto st = pc::updater::state();
            SDL_OpenURL(st.release_url.empty() ?
                            "https://github.com/x9x9x9x9x9/squad-strike/releases" :
                            st.release_url.c_str());
            return;
        }
        if (id == "update-later") {
            update_dismissed = true;
            refresh_update(true);
            element(supported ? "play" : "choose")->Focus();
            return;
        }
        if (id == "check-updates") {
            prefs.check_updates = !prefs.check_updates;
            save();
            refresh_settings();
            element("check-updates")->Focus();
            return;
        }
        if (id == "check-now") {
            update_dismissed = false;
            pc::updater::check_async();
            refresh_settings();
            return;
        }
        if (id == "discord" || id == "settings-discord") {
            status("Opening Discord in browser...");
            SDL_OpenURL("https://discord.gg/aurt34svq");
            return;
        }
        if (index_of(tab_ids, id) >= 0) {
            show_tab(index_of(tab_ids, id));
            return;
        }
        if (id == "verify" && verification.valid()) {
            cancel = true;
            status("Canceling verification...");
            return;
        }
        if (busy())
            return;
        if (id == "choose") {
            static const SDL_DialogFileFilter filters[] = {
                {"GameCube disc images", "iso;gcm;ciso;rvz;gcz;wia"}, {"All files", "*"}};
            dialog = std::make_shared<DialogResult>();
#if defined(__APPLE__) && TARGET_OS_IPHONE
            ios_show_open_file_dialog(
                dialog_done, new std::shared_ptr<DialogResult>(dialog), window);
#else
            SDL_ShowOpenFileDialog(dialog_done, new std::shared_ptr<DialogResult>(dialog), window,
                filters, 2, prefs.disc.empty() ? nullptr : prefs.disc.c_str(), false);
#endif
            controls();
        } else if (id == "play" && supported) {
            /* The pipeline queue may still be draining. Take the wait here,
             * where the player is already looking at a screen, rather than
             * during the first match; pressing Play again skips it. */
            if (const uint32_t pending = pending_pipelines(); !shaders_waited && pending > 0) {
                SDL_Log("Launcher: Play waits for %u pipelines", pending);
                shaders_waited = true;
                last_pending = 0;
                return;
            }
            SDL_Log("Launcher: starting the game");
            begin_game();
        } else if (id == "verify" && supported) {
            cancel = false;
            progress = 0;
            last_progress = 101;
            verification_identity = identity(prefs.disc);
            auto path = prefs.disc;
            verification = std::async(std::launch::async,
                [this, path] { return launcher::verify_disc(path, cancel, progress); });
            controls();
            element("verify")->Focus();
        } else if (id == "settings" || id == "back")
            show_settings(id == "settings");
        else if (id == "display" || id == "fullscreen") {
            if (set_fullscreen(window, !prefs.fullscreen))
                prefs.fullscreen = !prefs.fullscreen;
            else
                status(std::string("Could not change display mode: ") + SDL_GetError(), true);
            save();
            refresh_settings();
            element("display")->Focus();
        } else if (id == "sync" || id == "vsync") {
            if (!std::getenv("MELEE_VSYNC")) {
                prefs.vsync = !prefs.vsync;
                aurora_enable_vsync(prefs.vsync);
                save();
            }
            refresh_settings();
            if (std::getenv("MELEE_VSYNC"))
                status("VSync is controlled by the MELEE_VSYNC environment setting.");
            element("sync")->Focus();
        } else if (id == "resolution") {
            prefs.render_scale = prefs.render_scale >= 10 ? 0 : int(prefs.render_scale) + 1;
            save();
            refresh_settings();
            element("resolution")->Focus();
        } else if (id == "aa") {
            prefs.msaa = prefs.msaa == 1 ? 4 : 1;
            save();
            refresh_settings();
            element("aa")->Focus();
        } else if (id == "backend") {
#if defined(__APPLE__)
            status("Graphics backend is Metal (hardware accelerated).");
#else
            if (std::getenv("MELEE_BACKEND")) {
                status("Backend is controlled by the MELEE_BACKEND environment setting.");
            } else {
                prefs.backend = backend_next(prefs.backend, direction);
                save();
                refresh_settings();
                status("Graphics backend set to " + std::string(backend_name(prefs.backend)) +
                       " (takes effect on restart).");
            }
#endif
            element("backend")->Focus();
        } else if (id == "custom-textures") {
            prefs.custom_textures = !prefs.custom_textures;
            save();
            refresh_settings();
            element("custom-textures")->Focus();
        } else if (id == "hud-mode") {
            prefs.hud_mode = (prefs.hud_mode + 1) % 2;
            save();
            refresh_settings();
            element("hud-mode")->Focus();
        } else if (id == "magnifier") {
            prefs.magnifier = !prefs.magnifier;
            save();
            refresh_settings();
            element("magnifier")->Focus();
        } else if (id == "results-screen") {
            prefs.results_screen = !prefs.results_screen;
            save();
            refresh_settings();
            element("results-screen")->Focus();
        } else if (id == "save-replays") {
            prefs.save_replays = !prefs.save_replays;
            save();
            refresh_settings();
            element("save-replays")->Focus();
        } else if (id == "frozen-stadium") {
            prefs.frozen_stadium = !prefs.frozen_stadium;
            save();
            refresh_settings();
            element("frozen-stadium")->Focus();
        } else if (id == "squad-strike") {
            prefs.squad_strike = !prefs.squad_strike;
            save();
            refresh_settings();
            element("squad-strike")->Focus();
        } else if (id == "stage-expansion") {
            prefs.stage_expansion = !prefs.stage_expansion;
            save();
            refresh_settings();
            element("stage-expansion")->Focus();
        } else if (id == "sd-remix") {
            prefs.sd_remix = !prefs.sd_remix;
            save();
            refresh_settings();
            element("sd-remix")->Focus();
        } else if (id == "free-camera") {
            prefs.free_camera = !prefs.free_camera;
            save();
            refresh_settings();
            element("free-camera")->Focus();
        } else if (id == "ucf") {
            prefs.ucf = !prefs.ucf;
            save();
            refresh_settings();
            element("ucf")->Focus();
        } else if (id == "rumble") {
            prefs.rumble = !prefs.rumble;
            save();
            refresh_settings();
            element("rumble")->Focus();
        } else if (id == "unlock-all") {
            prefs.unlock_all = !prefs.unlock_all;
            save();
            refresh_settings();
            element("unlock-all")->Focus();
        } else if (id == "volume") {
            int step = int(prefs.volume * 10 + 0.5f);
            prefs.volume = (step >= 10 ? 0 : step + 1) / 10.0f;
            save();
            refresh_settings();
            element("volume")->Focus();
        } else if (id == "music-volume") {
            int step = int(prefs.music_volume * 10 + 0.5f);
            prefs.music_volume = (step >= 10 ? 0 : step + 1) / 10.0f;
            save();
            refresh_settings();
            element("music-volume")->Focus();
        } else if (id == "sfx-volume") {
            int step = int(prefs.sfx_volume * 10 + 0.5f);
            prefs.sfx_volume = (step >= 10 ? 0 : step + 1) / 10.0f;
            save();
            refresh_settings();
            element("sfx-volume")->Focus();
        } else if (id == "mute") {
            prefs.mute = !prefs.mute;
            save();
            refresh_settings();
            element("mute")->Focus();
        } else if (id == "reverb") {
            prefs.reverb = !prefs.reverb;
            save();
            refresh_settings();
            element("reverb")->Focus();
        } else if (id == "performance") {
            // The cheapest settings for a slow GPU or CPU, in one press.
            prefs.render_scale = 1;
            prefs.msaa = 1;
            prefs.anisotropy = 1;
            prefs.reverb = false;
#if defined(__ANDROID__)
            // Mailbox reduces presentation stalls; simulation still paces to 60 Hz.
            if (!std::getenv("MELEE_VSYNC")) {
                prefs.vsync = false;
                aurora_enable_vsync(false);
            }
#endif
            save();
            refresh_settings();
            note("Performance preset: internal resolution 1x, anti-aliasing off, filtering 1x "
                 "and reverb off. Anti-aliasing and filtering apply after a restart.");
            element("performance")->Focus();
        } else if (id == "fps") {
            prefs.fps = !prefs.fps;
            save();
            refresh_settings();
            element("fps")->Focus();
        } else if (id == "scale") {
            prefs.scale = prefs.scale >= 1.5f ? 0.75f : prefs.scale + 0.25f;
            aurora::rmlui::set_ui_scale(prefs.scale);
            save();
            refresh_settings();
            element("scale")->Focus();
        }
    }

public:
    Launcher(SDL_Window* w, Rml::ElementDocument* d) : window(w), document(d) {
        document->AddEventListener(Rml::EventId::Click, this);
        document->AddEventListener(Rml::EventId::Keydown, this);
        document->AddEventListener(Rml::EventId::Change, this);
        // Focus does not bubble; the capture pass still reaches the document.
        document->AddEventListener(Rml::EventId::Focus, this, true);
        document->AddEventListener(Rml::EventId::Mousemove, this);
        /* MELEE_LAUNCHER_PLATFORM=mobile lays the launcher out as
         * another platform does, to check a phone's from a desktop. Only the
         * layout follows it. */
        const char* platform = std::getenv("MELEE_LAUNCHER_PLATFORM");
        if (is_mobile || (platform && std::strcmp(platform, "mobile") == 0))
            document->SetClass("mobile", true);
#if defined(__ANDROID__)
        document->SetClass("android", true);
#endif
        document->Show();
        text("app-version", pc::get_app_version());
        text("disc-note", disc_note);
        controls();
        element("choose")->Focus();
    }
    ~Launcher() override {
        cancel = true;
        if (verification.valid())
            verification.wait();
        if (inspection.valid())
            inspection.wait();
        document->RemoveEventListener(Rml::EventId::Click, this);
        document->RemoveEventListener(Rml::EventId::Keydown, this);
        document->RemoveEventListener(Rml::EventId::Change, this);
        document->RemoveEventListener(Rml::EventId::Focus, this, true);
        document->RemoveEventListener(Rml::EventId::Mousemove, this);
        auto* context = document->GetContext();
        document->Close();
        context->Update();
    }
    void ProcessEvent(Rml::Event& event) override {
        if (event.GetId() == Rml::EventId::Focus) {
            describe(event.GetTargetElement());
        } else if (event.GetId() == Rml::EventId::Mousemove) {
            hover(event);
        } else if (event.GetId() == Rml::EventId::Click) {
            auto* target = event.GetTargetElement();
            while (target && target != document) {
                if (target->GetTagName() == "button") {
                    if (!target->IsPseudoClassSet("disabled"))
                        action(target->GetId());
                    break;
                }
                target = target->GetParentNode();
            }
        } else if (event.GetId() == Rml::EventId::Change) {
            auto* target = event.GetTargetElement();
            if (!target || quiet)
                return;
            if (change_online(event)) {
                save();
                return;
            }
            auto id = target->GetId();
            if (id == "volume") {
                prefs.volume =
                    std::clamp(event.GetParameter<float>("value", 100.0f) / 100.0f, 0.0f, 1.0f);
                text("volume-val", std::to_string(int(prefs.volume * 100 + 0.5f)) + "%");
            } else if (id == "music-volume") {
                prefs.music_volume =
                    std::clamp(event.GetParameter<float>("value", 100.0f) / 100.0f, 0.0f, 1.0f);
                text(
                    "music-volume-val", std::to_string(int(prefs.music_volume * 100 + 0.5f)) + "%");
            } else if (id == "sfx-volume") {
                prefs.sfx_volume =
                    std::clamp(event.GetParameter<float>("value", 100.0f) / 100.0f, 0.0f, 1.0f);
                text("sfx-volume-val", std::to_string(int(prefs.sfx_volume * 100 + 0.5f)) + "%");
            } else if (id == "resolution") {
                prefs.render_scale = std::clamp(
                    float(int(event.GetParameter<float>("value", 0.0f) + 0.5f)), 0.0f, 10.0f);
                text("resolution-val", resolution_name(prefs.render_scale));
                notice();
            } else if (id == "scale") {
                prefs.scale =
                    std::clamp(event.GetParameter<float>("value", 100.0f) / 100.0f, 0.75f, 1.5f);
                text("scale-val", std::to_string(int(prefs.scale * 100 + 0.5f)) + "%");
                aurora::rmlui::set_ui_scale(prefs.scale);
            } else
                return;
            save();
        } else if (event.GetId() == Rml::EventId::Keydown) {
            auto key = event.GetParameter<int>("key_identifier", 0);
            if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN) {
                focus_step(key == Rml::Input::KI_UP ? -1 : 1);
                event.StopPropagation();
            } else if (key == Rml::Input::KI_RETURN) {
                // Stopping it here also stops RmlUi's own click on Enter.
                auto* focus = document->GetContext()->GetFocusElement();
                if (focus && (is_slider(focus->GetId()) ||
                                 (settings && index_of(tab_ids, focus->GetId()) >= 0)))
                {
                    activate_focus();
                    event.StopPropagation();
                }
            } else if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) {
                // A text field keeps its arrows for the caret.
                if (!typing(document->GetContext()->GetFocusElement())) {
                    horizontal(key == Rml::Input::KI_LEFT ? -1 : 1);
                    event.StopPropagation();
                }
            } else if (key == Rml::Input::KI_ESCAPE) {
                back();
                event.StopPropagation();
            }
        }
    }
    int run(const std::string& initial_error, bool start_requested = false) {
        /* Back from the game (pc_relaunch_to): from a replay, its list; from
         * Back to launcher, the main menu. Either way not straight back into
         * the game, as a disc on the command line would. */
        if (const char* page = std::getenv("MELEE_LAUNCHER_PAGE"); page && page[0] != '\0') {
            start_requested = false;
            open_replays_after_inspect = std::strcmp(page, "replays") == 0;
            SDL_unsetenv_unsafe("MELEE_LAUNCHER_PAGE");
        }
        if (const char* msg = std::getenv("MELEE_LAUNCHER_STATUS")) {
            replay_status = msg;
            SDL_unsetenv_unsafe("MELEE_LAUNCHER_STATUS");
        }
        if (!prefs.disc.empty())
            inspect(prefs.disc);
        if (!initial_error.empty())
            status(initial_error, true);
        pc::updater::cleanup_previous();
        if (!pc::updater::supported()) {
            for (const char* id : {"check-updates", "check-now"})
                if (auto* e = element(id))
                    if (auto* row = e->GetParentNode())
                        row->SetProperty("display", "none");
        } else if (prefs.check_updates)
            pc::updater::check_async();
        while (result == -2) {
            if (pc_exit_requested) { /* SIGINT/SIGTERM, src/pc/main.c */
                result = 0;
                cancel = true;
                break;
            }
            auto* events = aurora_update();
            for (auto* e = events; e && e->type != AURORA_NONE; ++e) {
                if (e->type == AURORA_EXIT) {
                    result = 0;
                    cancel = true;
                }
                if (e->type != AURORA_SDL_EVENT)
                    continue;
                if (e->sdl.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    auto button = e->sdl.gbutton.button;
                    if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
                        focus_step(1);
                    else if (button == SDL_GAMEPAD_BUTTON_DPAD_UP)
                        focus_step(-1);
                    else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                        horizontal(-1);
                    else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                        horizontal(1);
                    else if (button == SDL_GAMEPAD_BUTTON_SOUTH ||
                             button == SDL_GAMEPAD_BUTTON_START)
                        activate_focus();
                    else if (button == SDL_GAMEPAD_BUTTON_EAST)
                        back();
                } else if (e->sdl.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                    if (e->sdl.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                        auto value = e->sdl.gaxis.value;
                        auto now = SDL_GetTicks();
                        if (std::abs(value) > 18000 && now - last_axis > 200) {
                            focus_step(value > 0 ? 1 : -1);
                            last_axis = now;
                        }
                    } else if (e->sdl.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                        auto value = e->sdl.gaxis.value;
                        auto now = SDL_GetTicks();
                        if (std::abs(value) > 18000 && now - last_axis_x > 150) {
                            horizontal(value > 0 ? 1 : -1);
                            last_axis_x = now;
                        }
                    }
                } else if (e->sdl.type == SDL_EVENT_DROP_FILE && !busy() && e->sdl.drop.data) {
                    inspect(e->sdl.drop.data);
                }
            }
            if (dialog) {
                bool ready;
                std::string path, error;
                {
                    std::lock_guard lock(dialog->mutex);
                    ready = dialog->ready;
                    path = dialog->path;
                    error = dialog->error;
                }
                if (ready) {
                    dialog.reset();
                    if (!error.empty())
                        status("File chooser failed: " + error, true);
                    else if (!path.empty())
                        inspect(path);
                    controls();
                    element("choose")->Focus();
                }
            }
            if (inspection.valid() &&
                inspection.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto info = inspection.get();
                if (info.supported) {
                    prefs.disc = pending_path;
                    supported = true;
                    selected_identity = identity(prefs.disc);
                    std::string display_name;
                    if (prefs.disc.rfind("content://", 0) == 0) {
                        auto last_slash = prefs.disc.find_last_of('/');
                        display_name = (last_slash != std::string::npos) ?
                                           prefs.disc.substr(last_slash + 1) :
                                           prefs.disc;
                        if (auto col = display_name.find_last_of("%3A"); col != std::string::npos)
                            display_name = display_name.substr(col + 1);
                        if (auto col = display_name.find_last_of(':'); col != std::string::npos)
                            display_name = display_name.substr(col + 1);
                    } else {
                        display_name = std::filesystem::path(prefs.disc).filename().string();
                    }
                    text("disc-name", display_name);
                    text("disc-path", prefs.disc);
                    text("disc-info", info.message);
                    /* Verifying is optional (Play works either way); a disc
                     * verified before, unchanged since, says so instead of
                     * "not verified", which read as a step to do each time. */
                    status(
                        !prefs.verified_disc.empty() && prefs.verified_disc == selected_identity ?
                            "Ready to play / Disc verified." :
                            "Ready to play.");
                    save();
                } else {
                    if (prefs.disc == pending_path) {
                        supported = false;
                        prefs.disc.clear();
                        save();
                    }
                    status(info.message, true);
                }
                controls();
                element(supported ? "play" : "choose")->Focus();
                if (open_replays_after_inspect) {
                    open_replays_after_inspect = false;
                    show_replays(true);
                    if (!replay_status.empty())
                        status(replay_status, replay_status.rfind("Video saved", 0) != 0);
                }
                if (start_requested) {
                    start_requested = false;
                    if (supported)
                        action("play");
                }
            }
            if (verification.valid()) {
                auto percent = progress.load();
                if (percent != last_progress && !cancel) {
                    status("Verifying disc / " + std::to_string(percent) + "%");
                    last_progress = percent;
                }
                if (verification.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    auto checked = verification.get();
                    if (checked.state == launcher::VerifyState::Error)
                        supported = false;
                    if (identity(prefs.disc) != verification_identity)
                        status("Disc changed during verification. Verify it again.", true);
                    else {
                        status(checked.message, checked.state == launcher::VerifyState::Mismatch ||
                                                    checked.state == launcher::VerifyState::Error);
                        /* Remembered until the image changes, so the next
                         * launch says "Disc verified." without asking again. */
                        prefs.verified_disc = checked.state == launcher::VerifyState::Verified ?
                                                  verification_identity :
                                                  std::string();
                        save();
                    }
                    controls();
                    element("verify")->Focus();
                }
            }
            if (supported && !busy() && SDL_GetTicks() - last_identity_check > 1000) {
                last_identity_check = SDL_GetTicks();
                if (identity(prefs.disc) != selected_identity) {
                    supported = false;
                    status("Disc changed or was removed. Choose the image again.", true);
                    controls();
                }
            }
            {
                const uint32_t pending = pending_pipelines();
                if (pending != last_shader_pending) {
                    last_shader_pending = pending;
                    text("shader-status",
                        pending ?
                            "Preparing cached shaders (" + std::to_string(pending) + " left)..." :
                            "Cached shaders ready. New effects may compile during play.");
                }
            }
            if (shaders_waited && result == -2) {
                const uint32_t pending = pending_pipelines();
                if (pending == 0) {
                    SDL_Log("Launcher: pipelines built, starting the game");
                    shaders_waited = false;
                    begin_game(); /* reports its own failure in the status line */
                } else if (pending != last_pending) {
                    last_pending = pending;
                    status("Compiling shaders (" + std::to_string(pending) +
                           " left). Press Play again to start now.");
                }
            }
            refresh_update();
            if (result != -2)
                break;
            if (aurora_begin_frame())
                aurora_end_frame();
            /* Spend the idle time until the next frame on the pipeline queue:
             * with worker threads this only waits, as a plain delay did;
             * without them (Android) it builds queued pipelines here, the only
             * place they are built before a match. */
            const uint64_t idle_start = SDL_GetTicks();
            aurora_wait_pipelines(8);
            const uint64_t idle = SDL_GetTicks() - idle_start;
            if (idle < 8)
                SDL_Delay(uint32_t(8 - idle));
        }
        return result;
    }
};
}  // namespace

extern "C" void pc_launcher_configure(AuroraConfig* config) {
    config_path = std::filesystem::path(config->userPath ? config->userPath : ".") / "launcher.cfg";
    prefs = launcher::load_preferences(config_path);
    pc_net_set_input_delay(prefs.net_delay);
    if (prefs.install_id == 0) {
        prefs.install_id = std::random_device{}() | uint64_t(std::random_device{}()) << 32 | 1;
        std::string error;
        if (!launcher::save_preferences(config_path, prefs, error))
            SDL_Log("%s", error.c_str());
    }
#if defined(__APPLE__) || defined(TARGET_OS_IPHONE)
    auto file_accessible = [](const std::filesystem::path& path) -> bool {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec)
            return true;
        FILE* f = fopen(path.string().c_str(), "rb");
        if (f) {
            fclose(f);
            return true;
        }
        return false;
    };
    if (prefs.disc.empty() || !file_accessible(prefs.disc)) {
        const char* home = std::getenv("HOME");
        const char* candidates[] = {"Documents/melee.ciso", "Documents/melee.iso",
            "Documents/game.ciso", "Documents/game.iso", "melee.ciso", "melee.iso", nullptr};
        for (int p = 0; candidates[p] != nullptr; p++) {
            if (home && home[0] != '\0') {
                std::filesystem::path full = std::filesystem::path(home) / candidates[p];
                if (file_accessible(full)) {
                    prefs.disc = full.string();
                    break;
                }
            }
            if (file_accessible(candidates[p])) {
                prefs.disc = std::filesystem::absolute(candidates[p]).string();
                break;
            }
        }
        // If not found in standard candidate names, scan Documents directory for any disc
        if ((prefs.disc.empty() || !file_accessible(prefs.disc)) && home && home[0] != '\0') {
            std::filesystem::path docs = std::filesystem::path(home) / "Documents";
            std::error_code dir_ec;
            if (std::filesystem::exists(docs, dir_ec) &&
                std::filesystem::is_directory(docs, dir_ec))
            {
                for (const auto& entry : std::filesystem::directory_iterator(docs, dir_ec)) {
                    if (entry.is_regular_file(dir_ec)) {
                        auto ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".iso" || ext == ".ciso" || ext == ".rvz" || ext == ".gcm" ||
                            ext == ".gcz" || ext == ".wia")
                        {
                            auto info = launcher::inspect_disc(entry.path().string());
                            if (info.supported) {
                                prefs.disc = entry.path().string();
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
#endif
    config->startFullscreen = prefs.fullscreen;
#ifdef __ANDROID__
    pc_android_set_fullscreen(prefs.fullscreen);
#endif
    config->msaa = prefs.msaa;
    config->maxTextureAnisotropy = prefs.anisotropy;
    if (!std::getenv("MELEE_VSYNC"))
        config->vsync = prefs.vsync;
    if (!std::getenv("MELEE_BACKEND")) {
#if defined(__APPLE__)
        config->desiredBackend = BACKEND_METAL;
#else
        if (prefs.backend == 1)
            config->desiredBackend = BACKEND_D3D12;
        else if (prefs.backend == 2)
            config->desiredBackend = BACKEND_VULKAN;
        else if (prefs.backend == 3)
            config->desiredBackend = BACKEND_D3D11;
        else
            config->desiredBackend = BACKEND_AUTO;
#endif
    }
}

static std::filesystem::path pc_resources_path() {
#if defined(__ANDROID__)
    return "";
#elif defined(__APPLE__)
    const char* base = SDL_GetBasePath();
    std::filesystem::path p = std::filesystem::path(base ? base : ".");
    std::error_code ec;
    if (std::filesystem::exists(p / "resources", ec) && !ec)
        return p / "resources";
    return p;
#else
    const char* base = SDL_GetBasePath();
    return std::filesystem::path(base ? base : ".") / "resources";
#endif
}

extern "C" int pc_launcher_run(const char* command_line_disc, SDL_Window* window) {
    try {
        std::string error;
        bool start_requested = false;
        if (command_line_disc) {
            auto info = launcher::inspect_disc(command_line_disc);
            if (info.supported && aurora_dvd_open(command_line_disc)) {
                pc_load_disc_fonts(command_line_disc);
                prefs.disc = (std::string(command_line_disc).rfind("content://", 0) == 0) ?
                                 command_line_disc :
                                 std::filesystem::absolute(command_line_disc).string();
                if (!launcher::save_preferences(config_path, prefs, error))
                    SDL_Log("%s", error.c_str());
                // Direct disc launches must use the same shader wait as Play.
                start_requested = true;
            } else {
                aurora_dvd_close();
                error = info.supported ?
                            "Could not load the requested disc. Choose another image." :
                            info.message;
            }
        }
        ensure_rmlui_input_layer();
        auto* context = aurora::rmlui::get_context();
        if (!context) {
            SDL_Log("Launcher: RmlUi context is unavailable.");
            return -1;
        }
        auto resources = pc_resources_path();
        if (!load_menu_fonts(resources))
            return -1;
        aurora::rmlui::set_ui_scale(prefs.scale);
        aurora_set_resampler(static_cast<AuroraSampler>(prefs.filter_mode));
        auto* document = context->LoadDocument((resources / "launcher.rml").string());
        if (!document) {
            SDL_Log("Launcher: could not load launcher.rml");
            return -1;
        }
        Launcher launcher(window, document);
        return launcher.run(error, start_requested);
    } catch (const std::exception& e) {
        SDL_Log("Launcher failed: %s", e.what());
        return -1;
    }
}

// In-game settings use the same preferences and RmlUi context as the launcher.
#include <dolphin/vi.h>
#include <dolphin/pad.h>
extern "C" void pc_audio_set_volume(float volume);
extern "C" void pc_audio_set_music_volume(float volume);
extern "C" void pc_audio_set_sfx_volume(float volume);
extern "C" void pc_audio_set_reverb(bool on);
// Melee's own menu bank: 0 is back/cancel, 1 confirm, 2 the cursor tick.
extern "C" void lbAudioAx_80024030(int);
enum { SFX_BACK = 0, SFX_CONFIRM = 1, SFX_MOVE = 2 };
namespace {
class PortMenu final : public Rml::EventListener {
public:
    Rml::ElementDocument* document = nullptr;
    Rml::ElementDocument* counter = nullptr;
    Rml::ElementDocument* replay_hud = nullptr;
    std::string replay_line;
    SDL_Window* window = nullptr;
    bool open = false;
    // Set while the menu writes its own widgets: suppresses the Change and
    // Focus events that echo back, which would otherwise loop or chatter.
    bool quiet = false;
    uint64_t last_fps = 0, last_axis_y = 0, last_axis_x = 0;
    int tab = 0;
    u32 pad_port = 0;
    // -1 when idle, otherwise the mapping slot waiting for input. Axis slots
    // index the axis table instead of the button table.
    int binding = -1;
    bool binding_axis = false;
    // The press that opened the prompt has to be released first, or the A
    // that selected the row binds itself immediately.
    bool binding_armed = false;
    std::vector<std::string> focus_ids;
    Rml::Element* current_row = nullptr;
    // Resizing the framebuffer allocates a fresh, empty texture. While the
    // overlay pauses the game nothing redraws it, so the frozen frame behind
    // the menu would go black: hold the change until the menu closes.
    bool resize_pending = false;

    /* Below the categories: Resume, Back to launcher (desktop only) and Quit. */
    std::vector<const char*> exits() const {
        if (pc_can_relaunch())
            return {"resume", "to-launcher", "quit-game"};
        return {"resume", "quit-game"};
    }
    static bool is_exit(const Rml::String& id) {
        return id == "resume" || id == "to-launcher" || id == "quit-game";
    }
    static bool is_sidebar(const Rml::String& id) {
        return index_of(tab_ids, id) >= 0 || is_exit(id);
    }
    static bool is_slider(const Rml::String& id) {
        return id == "volume" || id == "resolution" || id == "scale" || id == "music-volume" ||
               id == "sfx-volume";
    }
    void label(const char* id, const std::string& value) {
        auto* e = document->GetElementById(id);
        if (!e)
            return;
        e->SetInnerRML("");
        e->AppendChild(document->CreateTextNode(value));
    }
    void slider(const char* id, float value) {
        auto* e = document->GetElementById(id);
        if (e)
            e->SetAttribute("value", value);
    }
    std::vector<std::string> page_focus() const {
        switch (tab) {
        case 0:
            return {"display", "sync", "resolution", "aspect", "hud-mode", "magnifier", "aa",
                "filter", "filter-mode", "custom-textures", "backend"};
        case 1:
            return {"volume", "music-volume", "sfx-volume", "mute", "reverb", "fps", "scale"};
        case 2:
            return {"unlock-all", "free-camera", "ucf", "rumble", "results-screen", "save-replays"};
        case 4:
            return {"net-code", "net-target", "net-delay", "show-ping", "net-two-players"};
        default: {
            std::vector<std::string> ids{"pad-port"};
            for (int i = 0; i < PAD_BUTTON_COUNT; ++i)
                ids.push_back("bind-" + std::to_string(i));
            for (int i = 0; i < PAD_AXIS_COUNT; ++i)
                ids.push_back("axis-" + std::to_string(i));
            ids.push_back("bind-reset");
            ids.push_back("menu-binding");
            return ids;
        }
        }
    }
    void show_tab(int next) {
        // The Launcher category only exists before starting the game.
        const int count = std::size(tab_ids) - 1;
        tab = (next + count) % count;
        for (int i = 0; i < count; ++i) {
            if (auto* e = document->GetElementById(tab_ids[i]))
                e->SetClass("selected", i == tab);
            const auto page_id = "page-" + Rml::String(tab_ids[i]).substr(4);
            if (auto* e = document->GetElementById(page_id))
                e->SetClass("selected", i == tab);
        }
        focus_ids = {tab_ids[tab]};
        for (const auto& id : page_focus())
            focus_ids.push_back(id);
        for (const char* id : exits())
            focus_ids.push_back(id);
        quiet = true;
        if (auto* e = document->GetElementById(tab_ids[tab])) {
            e->Focus();
            e->ScrollIntoView();
        }
        quiet = false;
    }
    void enter_rows() {
        document->UpdateDocument();
        for (const auto& id : page_focus()) {
            auto* e = document->GetElementById(id);
            if (e && e->IsVisible(true)) {
                e->Focus();
                e->ScrollIntoView();
                return;
            }
        }
    }
    void back() {
        auto* focus = document->GetContext()->GetFocusElement();
        if (focus && !is_sidebar(focus->GetId()))
            show_tab(tab);
        else
            toggle();
    }
    void focus_step(int direction) {
        auto* focus = document->GetContext()->GetFocusElement();
        const auto id = focus ? focus->GetId() : Rml::String();
        if (is_sidebar(id)) {
            const int count = std::size(tab_ids) - 1;
            const auto ex = exits();
            const int total = count + int(ex.size());
            int current = tab;
            for (size_t i = 0; i < ex.size(); ++i)
                if (id == ex[i])
                    current = count + int(i);
            const int next = (current + direction + total) % total;
            if (next < count)
                show_tab(next);
            else if (auto* e = document->GetElementById(ex[next - count]))
                e->Focus();
            return;
        }
        int index = direction > 0 ? -1 : 0;
        for (size_t i = 0; i < focus_ids.size(); ++i)
            if (document->GetElementById(focus_ids[i]) == focus)
                index = static_cast<int>(i);
        index = (index + direction + static_cast<int>(focus_ids.size())) %
                static_cast<int>(focus_ids.size());
        if (auto* e = document->GetElementById(focus_ids[index])) {
            e->Focus();
            e->ScrollIntoView();
        }
    }
    void activate_focus() {
        auto* focus = document->GetContext()->GetFocusElement();
        if (focus && index_of(tab_ids, focus->GetId()) >= 0)
            enter_rows();
        else if (focus)
            apply(focus->GetId());
    }
    bool is_back_bound() const {
        for (u32 p = 0; p < 4; ++p) {
            u32 count = 0;
            const PADButtonMapping* bmap = PADGetButtonMappings(p, &count);
            for (u32 i = 0; bmap && i < count; ++i) {
                if (bmap[i].nativeButton == SDL_GAMEPAD_BUTTON_BACK)
                    return true;
            }
            u32 axes = 0;
            const PADAxisMapping* amap = PADGetAxisMappings(p, &axes);
            for (u32 i = 0; amap && i < axes; ++i) {
                if (amap[i].nativeButton == SDL_GAMEPAD_BUTTON_BACK)
                    return true;
            }
        }
        return false;
    }
    static constexpr u16 key_buttons[] = {PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y,
        PAD_BUTTON_START, PAD_TRIGGER_Z, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_UP,
        PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT};
    void refresh_keyboard() {
        label("pad-port", "Keyboard");
        label("pad-name", "Player 1 · Escape cancels · Delete clears a binding");
        for (int i = 0; i < int(prefs.keys.size()); ++i) {
            const bool axis = i >= PAD_BUTTON_COUNT;
            const int slot = axis ? i - PAD_BUTTON_COUNT : i;
            const std::string prefix = axis ? "axis-" : "bind-";
            const char* name = axis ? PADGetAxisName(slot) : PADGetButtonName(key_buttons[i]);
            label((prefix + "name-" + std::to_string(slot)).c_str(), name ? name : "-");
            const char* key =
                prefs.keys[i] ? SDL_GetScancodeName(SDL_Scancode(prefs.keys[i])) : "Unbound";
            label((prefix + std::to_string(slot)).c_str(),
                binding == slot && binding_axis == axis ? "Press a key..." : key);
        }
    }
    void refresh_bindings() {
        label("menu-binding",
            pad_port == 4 ? "F1 (keyboard)" :
            binding == PAD_BUTTON_COUNT && !binding_axis ?
                            "Press a button..." :
                            SDL_GetGamepadStringForButton(SDL_GamepadButton(prefs.menu_button)));
        if (pad_port == 4) {
            refresh_keyboard();
            return;
        }
        label("pad-port", "Port " + std::to_string(pad_port + 1));
        const char* name = PADGetName(pad_port);
        label("pad-name", name && *name ? name : "No controller in this port.");
        u32 count = 0;
        const PADButtonMapping* map = PADGetButtonMappings(pad_port, &count);
        u32 axes = 0;
        const PADAxisMapping* amap = PADGetAxisMappings(pad_port, &axes);
        for (int i = 0; i < PAD_BUTTON_COUNT; ++i) {
            const auto row = std::to_string(i);
            const char* gc = map && u32(i) < count ? PADGetButtonName(map[i].padButton) : nullptr;
            const char* native =
                map && u32(i) < count && map[i].nativeButton != PAD_NATIVE_BUTTON_INVALID ?
                    PADGetNativeButtonName(map[i].nativeButton) :
                    nullptr;
            if (!native && map && u32(i) < count && amap &&
                (map[i].padButton == PAD_TRIGGER_L || map[i].padButton == PAD_TRIGGER_R))
            {
                const PADAxis triggerAxis =
                    map[i].padButton == PAD_TRIGGER_L ? PAD_AXIS_TRIGGER_L : PAD_AXIS_TRIGGER_R;
                for (u32 a = 0; a < axes; ++a) {
                    if (amap[a].padAxis == triggerAxis && amap[a].nativeAxis.nativeAxis >= 0) {
                        native = PADGetNativeAxisName(amap[a].nativeAxis);
                        break;
                    }
                }
            }
            label(("bind-name-" + row).c_str(), gc ? gc : "-");
            label(("bind-" + row).c_str(), binding == i && !binding_axis ? "Press a button..." :
                                           native                        ? native :
                                                                           "Unbound");
        }
        for (int i = 0; i < PAD_AXIS_COUNT; ++i) {
            const auto row = std::to_string(i);
            const char* gc = amap && u32(i) < axes ? PADGetAxisName(amap[i].padAxis) : nullptr;
            std::string bound = "Unbound";
            if (amap && u32(i) < axes) {
                if (amap[i].nativeAxis.nativeAxis >= 0) {
                    const char* a = PADGetNativeAxisName(amap[i].nativeAxis);
                    bound = std::string(a ? a : "axis") +
                            (amap[i].nativeAxis.sign == AXIS_SIGN_NEGATIVE ? " -" : " +");
                } else if (amap[i].nativeButton >= 0) {
                    const char* b = PADGetNativeButtonName(u32(amap[i].nativeButton));
                    if (b)
                        bound = b;
                }
            }
            label(("axis-name-" + row).c_str(), gc ? gc : "-");
            label(
                ("axis-" + row).c_str(), binding == i && binding_axis ? "Move a stick..." : bound);
        }
    }
    void refresh() {
        quiet = true;
        refresh_online(document);
        label("display", display_mode());
        label("sync", std::getenv("MELEE_VSYNC") ? "Environment override" :
                      prefs.vsync                ? "On" :
                                                   "Off");
        slider("resolution", prefs.render_scale);
        label("resolution-val", resolution_name(prefs.render_scale));
        label("aspect", prefs.widescreen == 0 ? "Original (73:60)" :
                        prefs.widescreen == 1 ? "Widescreen 16:9" :
                                                "Auto (window aspect)");
        label("aa", prefs.msaa == 1 ? "Off" : std::to_string(prefs.msaa) + "x MSAA");
        label("filter", std::to_string(prefs.anisotropy) + "x");
        label("filter-mode", filter_name(prefs.filter_mode));
        label("custom-textures", prefs.custom_textures ? "Enabled" : "Disabled");
        label("hud-mode", prefs.hud_mode == 0 ? "Classic (4:3)" : "Wide (16:9)");
        label("magnifier", prefs.magnifier ? "On" : "Widescreen (top only)");
        label("frozen-stadium", prefs.frozen_stadium ? "Hazardless" : "Normal");
        label("squad-strike", prefs.squad_strike ? "On" : "Off");
        label("stage-expansion", prefs.stage_expansion ? "On" : "Off");
        /* Taken at boot (sdremix.cpp): say so when the switch differs. */
        label("sd-remix", prefs.sd_remix == pc_sdremix_active() ? (prefs.sd_remix ? "On" : "Off") :
                          prefs.sd_remix                        ? "On (after restart)" :
                                                                  "Off (after restart)");
        label("free-camera", prefs.free_camera ? "Free" : "Normal");
        label("results-screen", prefs.results_screen ? "On" : "Off");
        label("save-replays", prefs.save_replays ? "On" : "Off");
        label("ucf", std::getenv("MELEE_UCF") ? "Environment override" : prefs.ucf ? "On" : "Off");
        label("rumble", prefs.rumble ? "On" : "Off");
        label("unlock-all", prefs.unlock_all ? "Unlocked" : "Normal");
        label("backend", backend_name(prefs.backend));
        slider("volume", prefs.volume * 100.0f);
        label("volume-val", std::to_string(int(prefs.volume * 100 + 0.5f)) + "%");
        slider("music-volume", prefs.music_volume * 100.0f);
        label("music-volume-val", std::to_string(int(prefs.music_volume * 100 + 0.5f)) + "%");
        slider("sfx-volume", prefs.sfx_volume * 100.0f);
        label("sfx-volume-val", std::to_string(int(prefs.sfx_volume * 100 + 0.5f)) + "%");
        label("mute", prefs.mute ? "On" : "Off");
        label("reverb", prefs.reverb ? "On" : "Off");
        label("fps", prefs.fps ? "On" : "Off");
        slider("scale", prefs.scale * 100.0f);
        label("scale-val", std::to_string(int(prefs.scale * 100 + 0.5f)) + "%");
        label("menu-version", pc::get_app_version());
        refresh_bindings();
        quiet = false;
    }
    void saved() {
        std::string error;
        label("menu-status",
            launcher::save_preferences(config_path, prefs, error) ? "Saved." : error);
        refresh();
    }
    void toggle() {
        if (!document)
            return;
        open = !open;
        binding = -1;
        PADBlockInput(open);
        pc_android_set_launcher_active(open);
        lbAudioAx_80024030(open ? SFX_CONFIRM : SFX_BACK);
        if (open) {
            refresh();
            document->Show();
            show_tab(tab);
        } else {
            document->Hide();
            // Both reallocate the framebuffer; each is a no-op when the value
            // has not actually moved.
            if (resize_pending) {
                pc_widescreen_set_mode(prefs.widescreen);
                VISetFrameBufferScale(prefs.render_scale);
                resize_pending = false;
            }
        }
    }
    void start_binding(int slot, bool axis) {
        u32 count = 0;
        if (pad_port != 4 && (!PADGetButtonMappings(pad_port, &count) || count == 0)) {
            label("menu-status", "No controller in port " + std::to_string(pad_port + 1) + ".");
            return;
        }
        binding = slot;
        binding_axis = axis;
        binding_armed = false;
        label("menu-status",
            pad_port == 4 ? "Press a key. Escape cancels; Delete clears." :
            axis          ? "Move a stick or press a button. Escape cancels; Delete clears." :
                            "Press a button on the controller. Escape cancels; Delete clears.");
        refresh_bindings();
    }
    // Runs once a frame while a bind prompt is up.
    void poll_binding() {
        if (binding < 0 || pad_port == 4)
            return;
        constexpr uint32_t kNativeButtonAxisBit = 0x40000000u;
        const int native = PADGetNativeButtonPressed(pad_port);
        const PADSignedNativeAxis axis = PADGetNativeAxisPulled(pad_port);
        if (!binding_armed) {
            binding_armed = native < 0 && axis.nativeAxis < 0;
            return;
        }
        if (native < 0 && axis.nativeAxis < 0)
            return;
        if (binding == PAD_BUTTON_COUNT && !binding_axis) {
            if (native < 0)
                return;
            prefs.menu_button = native;
            binding = -1;
            saved();
            refresh_bindings();
            return;
        }
        u32 count = 0;
        if (binding_axis) {
            const PADAxisMapping* map = PADGetAxisMappings(pad_port, &count);
            if (map && u32(binding) < count) {
                // One source or the other, never neither: aurora reads the
                // axis when nativeAxis is set and asserts on the button
                // otherwise.
                PADSetAxisMapping(pad_port,
                    axis.nativeAxis >= 0 ?
                        PADAxisMapping{axis, SDL_GAMEPAD_BUTTON_INVALID, map[binding].padAxis} :
                        PADAxisMapping{{-1, AXIS_SIGN_POSITIVE}, native, map[binding].padAxis});
                PADSerializeMappings();
                label("menu-status", "Bound.");
            }
        } else {
            const PADButtonMapping* map = PADGetButtonMappings(pad_port, &count);
            if (map && u32(binding) < count) {
                const u32 encoded =
                    native >= 0 ? u32(native) :
                                  (kNativeButtonAxisBit | static_cast<uint32_t>(axis.nativeAxis) |
                                      (axis.sign == AXIS_SIGN_NEGATIVE ? 0x20000000u : 0));
                PADSetButtonMapping(pad_port, {encoded, map[binding].padButton});
                if (native < 0 && axis.nativeAxis >= 0 &&
                    (map[binding].padButton == PAD_TRIGGER_L ||
                        map[binding].padButton == PAD_TRIGGER_R))
                {
                    const PADAxis triggerAxis = map[binding].padButton == PAD_TRIGGER_L ?
                                                    PAD_AXIS_TRIGGER_L :
                                                    PAD_AXIS_TRIGGER_R;
                    PADSetAxisMapping(
                        pad_port, PADAxisMapping{axis, SDL_GAMEPAD_BUTTON_INVALID, triggerAxis});
                }
                PADSerializeMappings();
                label("menu-status", "Bound.");
            }
        }
        binding = -1;
        lbAudioAx_80024030(SFX_CONFIRM);
        refresh_bindings();
    }
    void cancel_binding() {
        if (binding < 0)
            return;
        binding = -1;
        lbAudioAx_80024030(SFX_BACK);
        label("menu-status", "Canceled.");
        refresh_bindings();
    }
    void clear_binding() {
        if (binding < 0 || pad_port == 4)
            return;
        if (binding == PAD_BUTTON_COUNT && !binding_axis) {
            label("menu-status", "The menu button must remain bound. Escape cancels.");
            return;
        }
        u32 count = 0;
        if (binding_axis) {
            const auto* map = PADGetAxisMappings(pad_port, &count);
            if (map && u32(binding) < count)
                PADSetAxisMapping(pad_port,
                    {{-1, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID, map[binding].padAxis});
        } else {
            const auto* map = PADGetButtonMappings(pad_port, &count);
            if (map && u32(binding) < count) {
                const auto button = map[binding].padButton;
                PADSetButtonMapping(pad_port, {PAD_NATIVE_BUTTON_INVALID, button});
                if (button == PAD_TRIGGER_L || button == PAD_TRIGGER_R)
                    PADSetAxisMapping(pad_port,
                        {{-1, AXIS_SIGN_POSITIVE}, SDL_GAMEPAD_BUTTON_INVALID,
                            static_cast<PADAxis>(button == PAD_TRIGGER_L ? PAD_AXIS_TRIGGER_L :
                                                                           PAD_AXIS_TRIGGER_R)});
            }
        }
        PADSerializeMappings();
        binding = -1;
        label("menu-status", "Binding cleared.");
        refresh_bindings();
    }
    void apply(const Rml::String& id, int direction = 1) {
        if (id == "net-delay") {
            prefs.net_delay = (prefs.net_delay + 1 + direction + 6) % 6 - 1;
            saved();
            refresh();
            return;
        }
        if (id == "show-ping") {
            prefs.show_ping = !prefs.show_ping;
            saved();
            refresh();
            return;
        }
        if (id == "net-two-players") {
            prefs.net_two_players = !prefs.net_two_players;
            saved();
            refresh();
            return;
        }
        if (id == "quit-game") {
            // Leave through the same shutdown path as a normal window close.
            pc_exit_requested = true;
            toggle();
            return;
        }
        if (id == "to-launcher") {
            // The same way out, then the program starts again on the launcher
            // (pc_relaunch_requested, src/pc/vi.c).
            pc_relaunch_requested = true;
            pc_exit_requested = true;
            toggle();
            return;
        }
        if (id == "resume") {
            toggle();
            return;
        }
        if (index_of(tab_ids, id) >= 0) {
            lbAudioAx_80024030(SFX_CONFIRM);
            show_tab(index_of(tab_ids, id));
            return;
        }
        if (id == "menu-binding") {
            if (pad_port == 4)
                label("menu-status", "F1 opens settings on the keyboard.");
            else
                start_binding(PAD_BUTTON_COUNT, false);
            return;
        }
        if (id == "pad-port") {
            pad_port = (pad_port + direction + 5) % 5;
            binding = -1;
            lbAudioAx_80024030(SFX_CONFIRM);
            refresh_bindings();
            return;
        }
        if (id == "bind-reset") {
            if (pad_port == 4) {
                prefs.keys = launcher::default_keys;
                saved();
                binding = -1;
                refresh_bindings();
                return;
            }
            PADRestoreDefaultMapping(pad_port);
            prefs.menu_button = SDL_GAMEPAD_BUTTON_BACK;
            saved();
            PADSerializeMappings();
            binding = -1;
            lbAudioAx_80024030(SFX_CONFIRM);
            label("menu-status", "Defaults restored.");
            refresh_bindings();
            return;
        }
        if (id.compare(0, 5, "bind-") == 0 || id.compare(0, 5, "axis-") == 0) {
            lbAudioAx_80024030(SFX_CONFIRM);
            start_binding(std::atoi(id.c_str() + 5), id[0] == 'a');
            return;
        }
        if (id == "display") {
            const bool fullscreen = !prefs.fullscreen;
            if (!set_fullscreen(window, fullscreen)) {
                label("menu-status", SDL_GetError());
                return;
            }
            // SDL applies the window change asynchronously; save the requested mode.
            prefs.fullscreen = fullscreen;
        } else if (id == "sync" && !std::getenv("MELEE_VSYNC")) {
            prefs.vsync = !prefs.vsync;
            aurora_enable_vsync(prefs.vsync);
        } else if (id == "resolution") {
            prefs.render_scale = prefs.render_scale >= 10 ? 0 : int(prefs.render_scale) + 1;
            resize_pending = true;
        } else if (id == "aspect") {
            prefs.widescreen = (prefs.widescreen + direction + 3) % 3;
            resize_pending = true;
        } else if (id == "aa")
            prefs.msaa = prefs.msaa == 1 ? 4 : 1;
        else if (id == "filter")
            prefs.anisotropy = direction > 0 ? (prefs.anisotropy >= 16 ? 1 : prefs.anisotropy * 2) :
                                               (prefs.anisotropy <= 1 ? 16 : prefs.anisotropy / 2);
        else if (id == "filter-mode") {
            prefs.filter_mode = (prefs.filter_mode + direction + 4) % 4;
            aurora_set_resampler(static_cast<AuroraSampler>(prefs.filter_mode));
        } else if (id == "custom-textures") {
            prefs.custom_textures = !prefs.custom_textures;
            pc_textures_reload();
        } else if (id == "hud-mode") {
            prefs.hud_mode = (prefs.hud_mode + 1) % 2;
        } else if (id == "magnifier") {
            prefs.magnifier = !prefs.magnifier;
        } else if (id == "results-screen") {
            prefs.results_screen = !prefs.results_screen;
        } else if (id == "save-replays") {
            prefs.save_replays = !prefs.save_replays;
        } else if (id == "frozen-stadium") {
            prefs.frozen_stadium = !prefs.frozen_stadium;
        } else if (id == "squad-strike") {
            prefs.squad_strike = !prefs.squad_strike;
        } else if (id == "stage-expansion") {
            prefs.stage_expansion = !prefs.stage_expansion;
        } else if (id == "sd-remix") {
            prefs.sd_remix = !prefs.sd_remix;
        } else if (id == "free-camera") {
            prefs.free_camera = !prefs.free_camera;
        } else if (id == "ucf") {
            prefs.ucf = !prefs.ucf;
        } else if (id == "rumble") {
            prefs.rumble = !prefs.rumble;
        } else if (id == "unlock-all") {
            prefs.unlock_all = !prefs.unlock_all;
        } else if (id == "backend") {
#if defined(__APPLE__)
            label("menu-status", "Graphics backend is Metal (hardware accelerated).");
#else
            if (std::getenv("MELEE_BACKEND")) {
                label("menu-status", "Backend set by MELEE_BACKEND environment.");
            } else {
                prefs.backend = backend_next(prefs.backend, direction);
                label("menu-status", "Backend set to " + std::string(backend_name(prefs.backend)) +
                                         " (restart required).");
            }
#endif
        } else if (id == "volume") {
            int step = int(prefs.volume * 10 + 0.5f);
            prefs.volume = (step >= 10 ? 0 : step + 1) / 10.0f;
        } else if (id == "music-volume") {
            int step = int(prefs.music_volume * 10 + 0.5f);
            prefs.music_volume = (step >= 10 ? 0 : step + 1) / 10.0f;
        } else if (id == "sfx-volume") {
            int step = int(prefs.sfx_volume * 10 + 0.5f);
            prefs.sfx_volume = (step >= 10 ? 0 : step + 1) / 10.0f;
        } else if (id == "mute")
            prefs.mute = !prefs.mute;
        else if (id == "reverb")
            prefs.reverb = !prefs.reverb;
        else if (id == "fps")
            prefs.fps = !prefs.fps;
        else if (id == "scale") {
            prefs.scale = prefs.scale >= 1.5f ? 0.75f : prefs.scale + 0.25f;
            aurora::rmlui::set_ui_scale(prefs.scale);
        } else
            return;
        pc_audio_set_volume(prefs.mute ? 0 : prefs.volume);
        pc_audio_set_music_volume(prefs.music_volume);
        pc_audio_set_sfx_volume(prefs.sfx_volume);
        pc_audio_set_reverb(prefs.reverb);
        lbAudioAx_80024030(SFX_CONFIRM);
        saved();
    }
    void drag(const Rml::String& id, float value) {
        if (id == "volume")
            prefs.volume = std::clamp(value / 100.0f, 0.0f, 1.0f);
        else if (id == "music-volume")
            prefs.music_volume = std::clamp(value / 100.0f, 0.0f, 1.0f);
        else if (id == "sfx-volume")
            prefs.sfx_volume = std::clamp(value / 100.0f, 0.0f, 1.0f);
        else if (id == "resolution") {
            prefs.render_scale = std::clamp(float(int(value + 0.5f)), 0.0f, 10.0f);
            resize_pending = true;
        } else if (id == "scale") {
            prefs.scale = std::clamp(value / 100.0f, 0.75f, 1.5f);
            aurora::rmlui::set_ui_scale(prefs.scale);
        } else
            return;
        pc_audio_set_volume(prefs.mute ? 0 : prefs.volume);
        pc_audio_set_music_volume(prefs.music_volume);
        pc_audio_set_sfx_volume(prefs.sfx_volume);
        lbAudioAx_80024030(SFX_MOVE);
        saved();
    }
    // Right enters a category; within a page, the arrows adjust its values.
    void step(int direction) {
        auto* focus = document->GetContext()->GetFocusElement();
        if (!focus)
            return;
        const auto id = focus->GetId();
        if (index_of(tab_ids, id) >= 0) {
            if (direction > 0)
                enter_rows();
            return;
        }
        if (focus->GetTagName() == "input" && !is_slider(id))
            return;  // Leave the caret's arrow keys to the text field.
        if (focus->IsClassSet("value") && !focus->IsClassSet("action")) {
            apply(id, direction);
            return;
        }
        if (id == "volume")
            prefs.volume = std::clamp(prefs.volume + direction * 0.05f, 0.0f, 1.0f);
        else if (id == "music-volume")
            prefs.music_volume = std::clamp(prefs.music_volume + direction * 0.05f, 0.0f, 1.0f);
        else if (id == "sfx-volume")
            prefs.sfx_volume = std::clamp(prefs.sfx_volume + direction * 0.05f, 0.0f, 1.0f);
        else if (id == "resolution") {
            prefs.render_scale = std::clamp(prefs.render_scale + direction, 0.0f, 10.0f);
            resize_pending = true;
        } else if (id == "scale") {
            prefs.scale = std::clamp(prefs.scale + direction * 0.25f, 0.75f, 1.5f);
            aurora::rmlui::set_ui_scale(prefs.scale);
        } else {
            if (direction < 0)
                show_tab(tab);
            return;
        }
        pc_audio_set_volume(prefs.mute ? 0 : prefs.volume);
        pc_audio_set_music_volume(prefs.music_volume);
        pc_audio_set_sfx_volume(prefs.sfx_volume);
        lbAudioAx_80024030(SFX_MOVE);
        saved();
    }
    // Gamepad drives the overlay directly: while it is open PADBlockInput
    // keeps the game from seeing any of this.
    void gamepad(const SDL_Event& e) {
        if (!document)
            return;
        if (open && binding >= 0 && pad_port != 4 && e.type == SDL_EVENT_KEY_DOWN &&
            e.key.scancode == SDL_SCANCODE_DELETE)
        {
            clear_binding();
            return;
        }
        if (pad_port == 4 && open && binding >= 0 &&
            (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP))
        {
            if (e.type == SDL_EVENT_KEY_UP) {
                binding_armed = true;
                return;
            }
            if (!binding_armed || e.key.repeat || e.key.scancode == SDL_SCANCODE_ESCAPE ||
                e.key.scancode == SDL_SCANCODE_F1)
                return;
            const int slot = binding + (binding_axis ? PAD_BUTTON_COUNT : 0);
            const int code = e.key.scancode == SDL_SCANCODE_DELETE ? 0 : e.key.scancode;
            prefs.keys[slot] = code;
            // Digital and analog trigger rows describe the same keyboard key.
            if (slot == 6 || slot == 7)
                prefs.keys[slot + 14] = code;
            if (slot == 20 || slot == 21)
                prefs.keys[slot - 14] = code;
            binding = -1;
            saved();
            refresh_bindings();
            return;
        }
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            const auto button = e.gbutton.button;
            if (binding < 0 && (button == SDL_GAMEPAD_BUTTON_GUIDE ||
                                   (button == prefs.menu_button &&
                                       (open || prefs.menu_button != SDL_GAMEPAD_BUTTON_BACK ||
                                           !is_back_bound()))))
            {
                toggle();
                return;
            }
            if (!open || binding >= 0)
                return;  // a bind prompt owns every button
            switch (button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP:
                focus_step(-1);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                focus_step(1);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                step(-1);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                step(1);
                break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                show_tab(tab - 1);
                break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
                show_tab(tab + 1);
                break;
            case SDL_GAMEPAD_BUTTON_SOUTH:
                activate_focus();
                break;
            case SDL_GAMEPAD_BUTTON_EAST:
                back();
                break;
            case SDL_GAMEPAD_BUTTON_START:
                toggle();
                break;
            default:
                break;
            }
        } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && open && binding < 0) {
            const auto value = e.gaxis.value;
            const auto now = SDL_GetTicks();
            if (std::abs(value) <= 18000)
                return;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY && now - last_axis_y > 200) {
                focus_step(value > 0 ? 1 : -1);
                last_axis_y = now;
            } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX && now - last_axis_x > 200) {
                step(value > 0 ? 1 : -1);
                last_axis_x = now;
            }
        }
    }
    void ProcessEvent(Rml::Event& event) override {
        /* Hide() leaves RmlUi's focus inside the document it just hid, and
         * RmlUi keeps delivering key events to that focus, so the closed menu
         * went on eating input -- Return synthesises a Click on the hidden
         * control, which plays the select sound (#84). While closed, nothing
         * the menu could do is wanted. */
        if (!open)
            return;
        if (event.GetId() == Rml::EventId::Focus) {
            auto* target = event.GetTargetElement();
            Rml::Element* row = nullptr;
            Rml::String description;
            for (auto* e = target; e && e != document; e = e->GetParentNode()) {
                if (!row && e->IsClassSet("row"))
                    row = e;
                if (description.empty())
                    description = e->GetAttribute<Rml::String>("data-desc", "");
            }
            if (current_row)
                current_row->SetClass("current", false);
            current_row = row;
            if (current_row)
                current_row->SetClass("current", true);
            document->SetClass("on-back", target && is_exit(target->GetId()));
            if (!description.empty())
                label("menu-status", description);
            if (!quiet)
                lbAudioAx_80024030(SFX_MOVE);
            return;
        }
        if (event.GetId() == Rml::EventId::Keydown) {
            const auto key = event.GetParameter<int>("key_identifier", 0);
            if (key == Rml::Input::KI_ESCAPE) {
                if (binding >= 0)
                    cancel_binding();
                else
                    back();
                event.StopPropagation();
                return;
            }
            if (binding >= 0)
                return;
            auto* focus = document->GetContext()->GetFocusElement();
            const auto id = focus ? focus->GetId() : Rml::String();
            if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN) {
                focus_step(key == Rml::Input::KI_UP ? -1 : 1);
                event.StopPropagation();
            } else if (key == Rml::Input::KI_RETURN && focus &&
                       (is_slider(id) || index_of(tab_ids, id) >= 0))
            {
                activate_focus();
                event.StopPropagation();
            } else if ((key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) && focus &&
                       (index_of(tab_ids, id) >= 0 || is_slider(id) || focus->IsClassSet("value")))
            {
                step(key == Rml::Input::KI_LEFT ? -1 : 1);
                event.StopPropagation();
            }
            return;
        }
        if (event.GetId() == Rml::EventId::Change) {
            auto* target = event.GetTargetElement();
            if (target && !quiet && change_online(event)) {
                saved();
                return;
            }
            if (target && !quiet)
                drag(target->GetId(), event.GetParameter<float>("value", 0.0f));
            return;
        }
        if (binding >= 0)
            return;
        auto* target = event.GetTargetElement();
        while (target && target != document && target->GetTagName() != "button")
            target = target->GetParentNode();
        if (!target || target == document)
            return;
        apply(target->GetId());
    }
};
PortMenu port_menu;
}  // namespace
extern "C" bool pc_menu_is_open(void) {
    return port_menu.open;
}
extern "C" void pc_menu_init(SDL_Window* window) {
    pc_widescreen_set_mode(prefs.widescreen);
    VISetFrameBufferScale(prefs.render_scale);
    pc_audio_set_volume(prefs.mute ? 0 : prefs.volume);
    pc_audio_set_music_volume(prefs.music_volume);
    pc_audio_set_sfx_volume(prefs.sfx_volume);
    pc_audio_set_reverb(prefs.reverb);
    ensure_rmlui_input_layer();
    auto* context = aurora::rmlui::get_context();
    if (!context) {
        SDL_Log("F1 menu: RmlUi context unavailable");
        return;
    }
    auto resources = pc_resources_path();
    if (!load_menu_fonts(resources))
        return;
    aurora::rmlui::set_ui_scale(prefs.scale);
    port_menu.window = window;
    port_menu.document = context->LoadDocument((resources / "port-menu.rml").string());
    port_menu.counter = context->LoadDocument((resources / "fps.rml").string());
    if (pc_replay_playing())
        port_menu.replay_hud = context->LoadDocument((resources / "replay-hud.rml").string());
    if (!port_menu.document) {
        SDL_Log("F1 menu: could not load port-menu.rml");
        return;
    }
    if (!pc_can_relaunch())
        if (auto* e = port_menu.document->GetElementById("to-launcher"))
            e->SetProperty("display", "none");
    port_menu.document->AddEventListener(Rml::EventId::Click, &port_menu);
    port_menu.document->AddEventListener(Rml::EventId::Keydown, &port_menu);
    port_menu.document->AddEventListener(Rml::EventId::Change, &port_menu);
    // Focus does not bubble; the capture pass still reaches the document.
    port_menu.document->AddEventListener(Rml::EventId::Focus, &port_menu, true);
    aurora_set_resampler(static_cast<AuroraSampler>(prefs.filter_mode));
}
extern "C" void pc_menu_toggle(void) {
    port_menu.toggle();
}
extern "C" void pc_menu_event(const SDL_Event* event) {
    if (event)
        port_menu.gamepad(*event);
}
extern "C" void pc_menu_update(void) {
    if (port_menu.counter) {
        if (prefs.fps) {
            if (!port_menu.counter->IsVisible())
                port_menu.counter->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            if (SDL_GetTicks() - port_menu.last_fps >= 500) {
                /* Say when the pipeline queue is still draining: a draw whose
                 * pipeline is not built yet is skipped, and that is what a pop
                 * in the first minutes of a fresh install actually is. */
                std::string text = std::to_string(int(aurora_get_fps() + 0.5f)) + " FPS";
                const uint32_t pending = pending_pipelines();
                if (pending > 0)
                    text += " \xc2\xb7 " + std::to_string(pending) + " shaders";
                port_menu.counter->GetElementById("count")->SetInnerRML(text);
                port_menu.last_fps = SDL_GetTicks();
            }
        } else if (port_menu.counter->IsVisible())
            port_menu.counter->Hide();
    }
    if (port_menu.replay_hud) {
        char line[256];
        if (pc_replay_hud(line, sizeof line)) {
            if (!port_menu.replay_hud->IsVisible())
                port_menu.replay_hud->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            if (port_menu.replay_line != line) {
                port_menu.replay_line = line;
                port_menu.replay_hud->GetElementById("line")->SetInnerRML(line);
            }
        } else if (port_menu.replay_hud->IsVisible())
            port_menu.replay_hud->Hide();
    }
    port_menu.poll_binding();
}

extern "C" bool pc_is_custom_textures_enabled(void) {
    return prefs.custom_textures;
}
extern "C" bool pc_net_rules(bool* unlock_all, bool* frozen_stadium);
extern "C" bool pc_is_unlock_all_enabled(void) {
    bool unlock_all, frozen, replay;
    if (pc_replay_unlock_all(&replay)) {
        return replay;
    }
    return pc_net_rules(&unlock_all, &frozen) ? unlock_all : prefs.unlock_all;
}
extern "C" bool pc_net_rule_flags(unsigned* flags);
/* Squad Strike and Stage Expansion change the simulation, so in a netplay
 * session the host's settings (carried in RULES) decide for both peers. The
 * environment overrides the launcher's preference offline. */
extern "C" bool pc_is_squad_strike_enabled(void) {
    unsigned flags;
    if (pc_net_rule_flags(&flags)) {
        return (flags & PC_RULE_SQUAD_STRIKE) != 0;
    }
    static const char* env = std::getenv("MELEE_SQUAD_STRIKE");
    return env ? env[0] != '0' : prefs.squad_strike;
}
extern "C" bool pc_is_stage_expansion_enabled(void) {
    unsigned flags;
    if (pc_net_rule_flags(&flags)) {
        return (flags & PC_RULE_STAGE_EXPANSION) != 0;
    }
    static const char* env = std::getenv("MELEE_STAGE_EXPANSION");
    return env ? env[0] != '0' : prefs.stage_expansion;
}
extern "C" bool pc_ace_stage_offered(int stage) {
    /* 3, Realgam Colosseum: withdrawn (pc_ace_local_bits). */
    if (stage < 1 || stage > 2) {
        return false;
    }
    unsigned flags;
    if (pc_net_rule_flags(&flags)) {
        return (flags >> (PC_RULE_ACE_STAGE_SHIFT + stage - 1) & 1) != 0;
    }
    return (pc_ace_local_bits() >> (stage - 1) & 1) != 0;
}
extern "C" bool pc_is_replay_saving_enabled(void) {
    return prefs.save_replays;
}
extern "C" bool pc_sdremix_wanted(void) {
    bool replay;
    if (pc_replay_sdremix(&replay)) {
        return replay; /* as the replay was recorded */
    }
    static const char* env = std::getenv("MELEE_SD_REMIX");
    return env ? env[0] != '0' : prefs.sd_remix;
}
extern "C" bool pc_is_frozen_stadium_enabled(void) {
    bool unlock_all, frozen;
    unsigned replay_flags;
    if (pc_replay_rule_flags(&replay_flags)) {
        return (replay_flags & PC_RULE_FROZEN_STADIUM) != 0;
    }
    if (pc_net_rules(&unlock_all, &frozen)) {
        return frozen;
    }
    /* MELEE_FROZEN_STADIUM=0|1 overrides the preference, so a test can reach
     * the transformations whatever the machine's launcher says. The host's
     * value is what RULES carries, so setting it on the host is enough. */
    static const char* env = std::getenv("MELEE_FROZEN_STADIUM");
    return env ? env[0] != '0' : prefs.frozen_stadium;
}
/* Free camera and UCF are read by the simulation (camera.c's pause camera
 * feeds offscreen damage; UCF decides dashbacks and shield drops), so a
 * local preference must not reach it while the run has to reproduce on
 * another machine: two peers with different settings would desync on the
 * first dashback UCF changes. Such a run plays the competitive standard,
 * which is Slippi's too: UCF on, free camera off. Both peers run the same
 * build (the handshake binds it), so forcing here keeps them agreed without
 * a RULES field. */
extern "C" bool pc_is_free_camera_enabled(void) {
    bool replay;
    if (pc_replay_free_camera(&replay)) {
        return replay;
    }
    return !pc_net_deterministic() && prefs.free_camera;
}
/* True once per install (per launcher.cfg): this build's Random Stage list
 * replaces whatever the save holds, the first time it runs. */
extern "C" bool pc_take_stage_list_reset(void) {
    if (prefs.stage_list >= 1) {
        return false;
    }
    prefs.stage_list = 1;
    std::string error;
    if (!launcher::save_preferences(config_path, prefs, error))
        SDL_Log("%s", error.c_str());
    return true;
}
extern "C" uint64_t pc_install_id(void) {
    return prefs.install_id;
}
extern "C" const char* pc_app_rev(void) {
    const char* env = std::getenv("MELEE_APP_REV");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return pc::get_app_version().c_str();
}
extern "C" bool pc_is_ucf_enabled(void) {
    bool replay;
    if (pc_replay_ucf(&replay)) {
        return replay;
    }
    if (pc_net_deterministic()) {
        return true;
    }
    static const char* env = std::getenv("MELEE_UCF");
    return env ? env[0] != '0' : prefs.ucf;
}
/* The off-screen magnifying glass: everywhere, or (widescreen) only above
 * the top of the screen. Display only (the bubble's
 * state, which off-screen damage reads, keeps updating), so it is a local
 * choice even in netplay. */
/* The results screen after a match. Online it is the host's choice (RULES),
 * since both peers leave the scene together. */
extern "C" bool pc_is_results_screen_enabled(void) {
    unsigned flags;
    if (pc_net_rule_flags(&flags)) {
        return (flags & PC_RULE_NO_RESULTS) == 0;
    }
    static const char* env = std::getenv("MELEE_RESULTS_SCREEN");
    return env ? env[0] != '0' : prefs.results_screen;
}
extern "C" bool pc_is_magnifier_enabled(void) {
    static const char* env = std::getenv("MELEE_MAGNIFIER");
    return env ? env[0] != '0' : prefs.magnifier;
}
extern "C" int pc_get_hud_mode(void) {
    return prefs.hud_mode;
}
extern "C" float pc_get_music_volume(void) {
    return prefs.music_volume;
}
extern "C" float pc_get_sfx_volume(void) {
    return prefs.sfx_volume;
}

extern "C" const char* pc_get_net_name(void) {
    return prefs.net_name.c_str();
}
/* Presentation only: whether a netplay match shows its ping (ifnet.c). */
/* Hardware output only: the game's rumble state still runs (and rolls back)
 * as usual; this just keeps the motors still (net.c, gcadapter.c). */
extern "C" bool pc_is_rumble_enabled(void) {
    return prefs.rumble;
}
extern "C" bool pc_is_ping_display_enabled(void) {
    return prefs.show_ping;
}
/* Input only: whether this computer brings a second player to a LAN or
 * Direct session (net.c puts that player's pad on the wire or not; what
 * the simulation sees is the wire, the same on both peers). */
extern "C" bool pc_is_net_two_players_enabled(void) {
    return prefs.net_two_players;
}
extern "C" const char* pc_get_net_code(void) {
    return prefs.net_code.c_str();
}
extern "C" const char* pc_get_net_target(void) {
    return prefs.net_target.c_str();
}
/* The lobby's own code entry (gmonlinemode.c) writes back here so the next
 * session and the F1 menu field agree with what the player just typed. */
extern "C" void pc_set_net_target(const char* code) {
    prefs.net_target = code ? code : "";
    std::string error;
    launcher::save_preferences(config_path, prefs, error);
    if (port_menu.document) {
        if (auto* e = dynamic_cast<Rml::ElementFormControl*>(
                port_menu.document->GetElementById("net-target")))
            e->SetValue(prefs.net_target);
    }
}
extern "C" int pc_get_net_port(void) {
    const char* env_port = getenv("MELEE_NET_PORT");
    if (env_port && *env_port) {
        int p = std::atoi(env_port);
        if (p > 0 && p < 65536)
            return p;
    }
    return prefs.net_port;
}
extern "C" int pc_get_net_delay(void) {
    return prefs.net_delay;
}

extern "C" int pc_get_key_binding(int slot) {
    return slot >= 0 && slot < int(prefs.keys.size()) ? prefs.keys[slot] : 0;
}
