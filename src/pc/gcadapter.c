/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Wii U / Switch GameCube controller adapter (WUP-028, and everything that
 * presents as one: Mayflash and other 4-port adapters in Wii U / Switch
 * mode, the many WUP-028 clones), read directly so the game gets the
 * controller's raw 8-bit values. Adapters in PC mode (Mayflash / EVORETRO /
 * DragonRise, 0079:1843/1844/1846) are left to SDL's GameCube driver, and
 * everything else (Switch 2 GameCube controller, 8BitDo, Xbox, PlayStation,
 * generic USB pads) to SDL's gamepad layer; see also pad_generic.c.
 *
 * SDL3's hidapi GameCube driver rescales every axis from an assumed 128+-88
 * range that then grows to whatever it observes (SDL_hidapi_gamecube.c
 * ResetAxisRange / READ_AXIS -> HIDAPI_RemapVal), and aurora divides that back
 * to 8 bits: a stick at raw +80 reaches the game as +116, a trigger reads 0
 * until half travel, and the scale moves mid-session. Melee's dash, tilt and
 * light-shield thresholds are all in raw units, so the adapter is read here:
 * 37-byte 0x21 reports through SDL's hidapi (SDL_hid_*, no new dependency;
 * SDL routes this VID/PID to its libusb backend on every platform, see
 * SDL_hidapi.c SDL_libusb_required[]), drained from the 1000 Hz input thread,
 * origin-relative like the GC SDK's PADRead, published as virtual pad status.
 * Adapter slot N is always PAD port N.
 *
 * MELEE_GC_ADAPTER=0 hands the adapter back to SDL's driver.
 */
#include "pc/pc.h"

#include <dolphin/pad.h>
/* rumble.h references DiscU16 (on-disc rumble stream); disc.h defines it for
 * this C TU, which is not force-included with compat.h like the game code. */
#include "pc/disc.h"
#include <sysdolphin/baselib/rumble.h>

#include <SDL3/SDL.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define GC_VID 0x057E
#define GC_PID 0x0337
#define GC_SLOTS 4
#define GC_REPORT 37 /* 0x21 + 4 x 9 slot bytes */
#define GC_SLOT_WIRED 0x10
#define GC_SLOT_WIRELESS 0x20
#define GC_SLOT_RUMBLE_POWER 0x04

/* Game-side motor state (sysdolphin/baselib/rumble.c). last_status is what
 * the game last asked PADControlMotor for: 0 hard stop, 1 stop, 2 rumble.
 * aurora's PADControlMotor only talks to SDL gamepads, so the raw path reads
 * the same state here instead of threading a new hook through aurora. */
extern HSD_RumbleData HSD_Rumble_804C22E0[GC_SLOTS];

static bool s_enabled;
static SDL_hid_device* s_dev;
static int s_retry_ms = 999;
/* SDL_hid_device_change_count() at the last look for the adapter; 0 = never. */
static Uint32 s_hid_changes;
/* Looks left at an adapter that enumerates but will not open yet. */
static int s_open_retries;
static bool s_warned_open;
static uint8_t s_rumble[1 + GC_SLOTS] = {0x11};

#if !defined(_WIN32) && !defined(__APPLE__)
#include <dirent.h>
#include <stdio.h>
static bool check_usb_device_attached(uint16_t vid, uint16_t pid) {
    DIR* dir = opendir("/sys/bus/usb/devices");
    if (dir == NULL) {
        return false;
    }
    struct dirent* ent;
    char path[256];
    char buf[16];
    char target_vid[8], target_pid[8];
    snprintf(target_vid, sizeof(target_vid), "%04x", vid);
    snprintf(target_pid, sizeof(target_pid), "%04x", pid);
    bool found = false;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor", ent->d_name);
        FILE* f = fopen(path, "r");
        if (f == NULL) {
            continue;
        }
        if (fgets(buf, sizeof(buf), f) && strncmp(buf, target_vid, 4) == 0) {
            fclose(f);
            snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct", ent->d_name);
            f = fopen(path, "r");
            if (f != NULL && fgets(buf, sizeof(buf), f) && strncmp(buf, target_pid, 4) == 0) {
                found = true;
                fclose(f);
                break;
            }
        }
        if (f != NULL) {
            fclose(f);
        }
    }
    closedir(dir);
    return found;
}
#endif

/* Poll-thread state. */
static PADStatus s_status[GC_SLOTS];
static bool s_present[GC_SLOTS];
static uint8_t s_origin[GC_SLOTS][6];
static uint64_t s_combo_since[GC_SLOTS]; /* X+Y+Start held since (ns), 0 = not held */

/* Rumble glitch filter. A rumble motor starting draws a burst of current;
 * on adapters powered from one USB cable (the RP2040 ones among them) that
 * corrupts the odd controller report while it runs: a stick spike or a
 * button for one report, which the game read as a random smash attack,
 * aerial or turnaround. While a port's motor runs (and a little after), each
 * value is the middle of its last three reports and each button the
 * majority: a one-report blip never gets through, a real change arrives one
 * report (~1 ms) later. With the motor off nothing is filtered. */
#define GC_HIST 3
#define GC_FILTER_TAIL_NS 150000000ull       /* keep filtering 150 ms after a stop */
static uint8_t s_hist[GC_SLOTS][GC_HIST][8]; /* buttons x2, sticks, triggers */
static int s_hist_n[GC_SLOTS];
static uint64_t s_motor_on_until[GC_SLOTS]; /* filter while now < this */
/* Glitches seen (one-report spikes / flickers), with the motor on and off
 * (tools/test_gcadapter.c checks the detector with these). */
static unsigned s_glitch[GC_SLOTS][2];

/* Publish a complete report under a short copy-only lock. Device I/O and
 * parsing remain worker-owned; virtual pads are written only by the main thread. */
static atomic_flag s_snapshot_lock = ATOMIC_FLAG_INIT;
static PADStatus s_published_status[GC_SLOTS];
static bool s_published_present[GC_SLOTS];
static _Atomic unsigned char s_motor_request[GC_SLOTS];

static void lock_snapshot(void) {
    while (atomic_flag_test_and_set_explicit(&s_snapshot_lock, memory_order_acquire)) {
    }
}

static void unlock_snapshot(void) {
    atomic_flag_clear_explicit(&s_snapshot_lock, memory_order_release);
}

static void publish_snapshot(void) {
    lock_snapshot();
    memcpy(s_published_status, s_status, sizeof(s_status));
    memcpy(s_published_present, s_present, sizeof(s_present));
    unlock_snapshot();
}

/* Cross-thread snapshot for the HUD: raw[6] | present<<48 | wireless<<49. */
static _Atomic uint64_t s_raw[GC_SLOTS];
static _Atomic uint64_t s_reports;

void pc_gcadapter_init(void) {
    const char* env = getenv("MELEE_GC_ADAPTER");
    const char* n64 = getenv("MELEE_ALLOW_N64");
    const char* prev;
    char list[512];
    size_t len = 0;

    s_enabled = !(env != NULL && env[0] == '0');
    /* Set before SDL_Init(SDL_INIT_JOYSTICK): devices SDL's gamepad layer
     * must leave alone (raw HID enumeration still sees them). Appended to
     * any list the user set in the environment. */
    prev = SDL_GetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES);
    list[0] = '\0';
    if (prev != NULL && prev[0] != '\0') {
        len = (size_t)SDL_snprintf(list, sizeof(list), "%s", prev);
    }
#define IGNORE(vid, pid)                                                                           \
    do {                                                                                           \
        if (len < sizeof(list))                                                                    \
            len += (size_t)SDL_snprintf(                                                           \
                list + len, sizeof(list) - len, "%s0x%04x/0x%04x", len ? "," : "", (vid), (pid));  \
    } while (0)
    if (s_enabled) {
        /* The WUP-028 (and everything presenting as one) is read raw here;
         * SDL's rescaled copy would win the virtual-pad merge. Only that
         * device: SDL's GameCube driver stays on for PC-mode adapters. */
        IGNORE(GC_VID, GC_PID);
    }
    if (!(n64 != NULL && n64[0] == '1')) {
        /* raphnet N64-to-USB adapter: an N64 controller is no use here, and
         * the adapter answers rumble with junk reports (random inputs on
         * whichever port it sat on). MELEE_ALLOW_N64=1 keeps it. */
        IGNORE(0x289b, 0x0063);
    }
#undef IGNORE
    if (list[0] != '\0') {
        SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES, list, SDL_HINT_OVERRIDE);
    }
}

/* The adapter's motor byte for what the game asks (HSD last_status: 0 hard
 * stop, 1 stop, 2 rumble). The adapter knows 0 off and 1 on; 2 is a "brake"
 * that only the official adapter honours (SDL leaves it off by default),
 * and clones run the motor for any nonzero byte. The game's state starts at
 * 0, so sending the brake for it ran every motor from launch. */
static uint8_t motor_byte(unsigned char request, bool enabled) {
    return request == 2 && enabled ? 1 : 0;
}

/* All motors off now, whatever the last report said. */
static void stop_all_motors(void) {
    if (s_dev == NULL) {
        return;
    }
    memset(s_rumble + 1, 0, GC_SLOTS);
    if (SDL_hid_write(s_dev, s_rumble, sizeof(s_rumble)) < 0) {
        pc_log_line("GC adapter: rumble stop failed: %s", SDL_GetError());
    }
}

static uint8_t median3(uint8_t a, uint8_t b, uint8_t c) {
    const uint8_t lo = a < b ? a : b, hi = a < b ? b : a;
    return c < lo ? lo : c > hi ? hi : c;
}

/* One report into the history; counts it as a glitch if the report before
 * it stood out from both its neighbours. */
static void push_history(int i, const uint8_t* v, bool motor) {
    if (s_hist_n[i] < GC_HIST) {
        memcpy(s_hist[i][s_hist_n[i]++], v, 8);
        return;
    }
    memmove(s_hist[i][0], s_hist[i][1], sizeof(s_hist[i][0]) * (GC_HIST - 1));
    memcpy(s_hist[i][GC_HIST - 1], v, 8);
    const uint8_t* a = s_hist[i][0];
    const uint8_t* b = s_hist[i][1];
    const uint8_t* c = s_hist[i][2];
    bool glitch = false;
    for (int k = 0; k < 2; k++) {
        /* A button on (or off) for exactly one report. */
        glitch |= ((b[k] ^ a[k]) & (b[k] ^ c[k]) & ~(a[k] ^ c[k])) != 0;
    }
    for (int k = 2; k < 8; k++) {
        const int da = (int)b[k] - (int)a[k], dc = (int)b[k] - (int)c[k];
        const int ac = (int)a[k] - (int)c[k];
        glitch |= ((da >= 30 && dc >= 30) || (da <= -30 && dc <= -30)) && ac < 10 && ac > -10;
    }
    if (glitch) {
        s_glitch[i][motor ? 1 : 0]++;
    }
}

static void clear_slot(int i) {
    s_hist_n[i] = 0;
    s_motor_on_until[i] = 0;
    s_present[i] = false;
    memset(&s_status[i], 0, sizeof(s_status[i]));
    s_combo_since[i] = 0;
    atomic_store_explicit(&s_raw[i], 0, memory_order_relaxed);
}

static void try_open(void) {
    /* Serialise against SDL's own hidapi enumeration (same udev/libusb
     * contexts, and the udev monitor the change count drains), which runs
     * under the joystick lock. */
    SDL_LockJoysticks();
    /* Enumerating opens every HID interface on Windows to read its ids: with
     * an Xbox Wireless Adapter controller attached that is ~380 ms a pass, all
     * under the joystick lock PADRead needs while the game thread holds the
     * interrupt lock the audio thread waits on, so a once-a-second retry froze
     * sound and picture every second (issue #90). Look only when a device came
     * or went, plus a few seconds more for an adapter that enumerates but will
     * not open yet (udev may still be applying its permissions), and only
     * enumerate once: SDL_hid_open is a second enumeration of its own. */
    const Uint32 changes = SDL_hid_device_change_count();
    if (changes != s_hid_changes) {
        s_hid_changes = changes;
        s_open_retries = 5;
    } else if (s_open_retries == 0) {
        SDL_UnlockJoysticks();
        return;
    }
    SDL_hid_device_info* info = SDL_hid_enumerate(GC_VID, GC_PID);
    s_dev = info != NULL ? SDL_hid_open(GC_VID, GC_PID, NULL) : NULL;
    SDL_UnlockJoysticks();
    s_open_retries = info != NULL ? s_open_retries - 1 : 0;
    if (s_dev == NULL) {
        if (info != NULL && !s_warned_open) {
            s_warned_open = true;
            pc_log_line("GC adapter: WUP-028 attached but could not be opened: %s"
#if defined(__APPLE__)
                        " (check GCAdapterDriver / USB permissions if macOS IOHIDFamily claimed "
                        "the adapter)"
#elif !defined(_WIN32)
                        " (udev rule: SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"057e\", "
                        "ATTRS{idProduct}==\"0337\", MODE=\"0666\")"
#endif
                ,
                SDL_GetError());
        }
#if !defined(_WIN32) && !defined(__APPLE__)
        else if (info == NULL && !s_warned_open && check_usb_device_attached(GC_VID, GC_PID))
        {
            s_warned_open = true;
            pc_log_line(
                "GC adapter: WUP-028 detected on USB bus, but not accessible through hidapi (%s)"
                " (ensure libusb-1.0 is installed and udev rules grant access)",
                SDL_GetError());
        }
#endif
    }
    SDL_hid_free_enumeration(info);
    if (s_dev == NULL) {
        return;
    }
    /* Start streaming (same init as SDL's driver). */
    const uint8_t init = 0x13;
    if (SDL_hid_write(s_dev, &init, 1) != 1) {
        pc_log_line("GC adapter: init write failed: %s", SDL_GetError());
        SDL_hid_close(s_dev);
        s_dev = NULL;
        return;
    }
    /* A motor left running (a crash, another program) stops on open. */
    stop_all_motors();
    s_warned_open = false;
    pc_log_line(
        "GC adapter: WUP-028 opened, raw 1000 Hz path active (MELEE_GC_ADAPTER=0 to use SDL)");
}

static void close_dev(const char* why) {
    pc_log_line("GC adapter: %s", why);
    stop_all_motors(); /* fails harmlessly if it was unplugged */
    SDL_hid_close(s_dev);
    s_dev = NULL;
    for (int i = 0; i < GC_SLOTS; i++) {
        clear_slot(i);
    }
    publish_snapshot();
}

static s8 rel8(uint8_t v, uint8_t origin) {
    const int d = (int)v - (int)origin;
    return (s8)(d < -128 ? -128 : d > 127 ? 127 : d);
}

static u8 relu8(uint8_t v, uint8_t origin) {
    return v > origin ? (u8)(v - origin) : 0;
}

/* One 9-byte slot: [status][buttons][buttons][sx][sy][cx][cy][L][R]. */
static void parse_slot(int i, const uint8_t* s, uint64_t now_ns) {
    const bool present = (s[0] & (GC_SLOT_WIRED | GC_SLOT_WIRELESS)) != 0;
    if (!present) {
        if (s_present[i]) {
            pc_log_line("GC adapter: port %d controller removed", i + 1);
            clear_slot(i);
        }
        return;
    }
    if (!s_present[i]) {
        /* A real controller captures its origin at power-on; the adapter does
         * not forward it, so take the first report, as Dolphin does. */
        memcpy(s_origin[i], s + 3, 6);
        s_present[i] = true;
        pc_log_line("GC adapter: port %d %s controller (status 0x%02x, rumble power %s), origin "
                    "%u,%u c %u,%u L %u R %u",
            i + 1, s[0] & GC_SLOT_WIRELESS ? "wireless" : "wired", s[0],
            s[0] & GC_SLOT_RUMBLE_POWER ? "yes" : "no", s[3], s[4], s[5], s[6], s[7], s[8]);
    }

    /* s[1..8] as the game will see them: filtered while rumbling. */
    const bool filter = now_ns < s_motor_on_until[i];
    uint8_t v[9];
    v[0] = s[0];
    push_history(i, s + 1, filter);
    if (filter && s_hist_n[i] == GC_HIST) {
        const uint8_t* h0 = s_hist[i][0];
        const uint8_t* h1 = s_hist[i][1];
        const uint8_t* h2 = s_hist[i][2];
        for (int k = 0; k < 2; k++) {
            v[1 + k] = (uint8_t)((h0[k] & h1[k]) | (h0[k] & h2[k]) | (h1[k] & h2[k]));
        }
        for (int k = 2; k < 8; k++) {
            v[1 + k] = median3(h0[k], h1[k], h2[k]);
        }
        s = v;
    }

    u16 b = 0;
    if (s[1] & 0x01)
        b |= PAD_BUTTON_A;
    if (s[1] & 0x02)
        b |= PAD_BUTTON_B;
    if (s[1] & 0x04)
        b |= PAD_BUTTON_X;
    if (s[1] & 0x08)
        b |= PAD_BUTTON_Y;
    if (s[1] & 0x10)
        b |= PAD_BUTTON_LEFT;
    if (s[1] & 0x20)
        b |= PAD_BUTTON_RIGHT;
    if (s[1] & 0x40)
        b |= PAD_BUTTON_DOWN;
    if (s[1] & 0x80)
        b |= PAD_BUTTON_UP;
    if (s[2] & 0x01)
        b |= PAD_BUTTON_START;
    if (s[2] & 0x02)
        b |= PAD_TRIGGER_Z;
    if (s[2] & 0x04)
        b |= PAD_TRIGGER_R;
    if (s[2] & 0x08)
        b |= PAD_TRIGGER_L;

    /* X+Y+Start for 3 s recentres, the combo the controller itself honours. */
    const u16 combo = PAD_BUTTON_X | PAD_BUTTON_Y | PAD_BUTTON_START;
    if ((b & combo) == combo) {
        if (s_combo_since[i] == 0) {
            s_combo_since[i] = now_ns;
        } else if (now_ns - s_combo_since[i] >= 3000000000ull) {
            memcpy(s_origin[i], s + 3, 6);
            s_combo_since[i] = now_ns;
            pc_log_line("GC adapter: port %d recentred", i + 1);
        }
    } else {
        s_combo_since[i] = 0;
    }

    PADStatus* st = &s_status[i];
    st->button = b;
    st->stickX = rel8(s[3], s_origin[i][0]);
    st->stickY = rel8(s[4], s_origin[i][1]);
    st->substickX = rel8(s[5], s_origin[i][2]);
    st->substickY = rel8(s[6], s_origin[i][3]);
    st->triggerLeft = relu8(s[7], s_origin[i][4]);
    st->triggerRight = relu8(s[8], s_origin[i][5]);
    st->err = PAD_ERR_NONE;

    uint64_t snap = 0;
    for (int k = 0; k < 6; k++) {
        snap |= (uint64_t)s[3 + k] << (8 * k);
    }
    snap |= 1ull << 48;
    if (s[0] & GC_SLOT_WIRELESS) {
        snap |= 1ull << 49;
    }
    atomic_store_explicit(&s_raw[i], snap, memory_order_relaxed);
}

static void update_rumble(const uint8_t* slots, uint64_t now_ns) {
    /* Only a wired pad on a slot with the second USB cable powered can
     * rumble. */
    bool changed = false;
    for (int i = 0; i < GC_SLOTS; i++) {
        const uint8_t* s = slots + 9 * i;
        uint8_t v = 0;
        const unsigned char motor = atomic_load_explicit(&s_motor_request[i], memory_order_relaxed);
        if (s_present[i] && (s[0] & GC_SLOT_RUMBLE_POWER) && !(s[0] & GC_SLOT_WIRELESS)) {
            v = motor_byte(motor, true);
        }
        if (v != 0) {
            s_motor_on_until[i] = now_ns + GC_FILTER_TAIL_NS;
        }
        if (s_rumble[1 + i] != v) {
            s_rumble[1 + i] = v;
            changed = true;
        }
    }
    if (changed && SDL_hid_write(s_dev, s_rumble, sizeof(s_rumble)) < 0) {
        pc_log_line("GC adapter: rumble write failed: %s", SDL_GetError());
    }
}

void pc_gcadapter_poll(void) {
    if (!s_enabled) {
        return;
    }
    if (s_dev == NULL) {
        /* Hot-plug: once a second, check whether a device came or went. */
        if (++s_retry_ms >= 1000) {
            s_retry_ms = 0;
            try_open();
        }
        return;
    }

    static uint8_t last[GC_REPORT];
    uint8_t pkt[64];
    int n;
    bool got = false;
    while ((n = SDL_hid_read_timeout(s_dev, pkt, sizeof(pkt), 0)) > 0) {
        if (n >= GC_REPORT && pkt[0] == 0x21) {
            memcpy(last, pkt, GC_REPORT);
            got = true;
            atomic_fetch_add_explicit(&s_reports, 1, memory_order_relaxed);
        }
    }
    if (n < 0) {
        close_dev("adapter removed");
        return;
    }
    if (got) {
        const uint64_t now = SDL_GetTicksNS();
        for (int i = 0; i < GC_SLOTS; i++) {
            parse_slot(i, last + 1 + 9 * i, now);
        }
        publish_snapshot();
        update_rumble(last + 1, now);
    }
}

/* Called at the main-thread input boundary before PADRead. Port 1 is merged
 * with keyboard/touch by keyboard.c; this function owns ports 2 through 4. */
void pc_gcadapter_apply(void) {
    PADStatus status[GC_SLOTS];
    bool present[GC_SLOTS];
    lock_snapshot();
    memcpy(status, s_published_status, sizeof(status));
    memcpy(present, s_published_present, sizeof(present));
    unlock_snapshot();
    for (int i = 0; i < GC_SLOTS; i++) {
        /* Rumble switched off (launcher / F1): ask for "stop" (1) where the
         * game asks for "rumble" (2). */
        unsigned char want = HSD_Rumble_804C22E0[i].last_status;
        if (motor_byte(want, pc_is_rumble_enabled()) == 0) {
            want = 1;
        }
        atomic_store_explicit(&s_motor_request[i], want, memory_order_relaxed);
        if (i != 0) {
            if (present[i]) {
                PADSetVirtualStatus((u32)i, &status[i]);
            } else {
                PADClearVirtualStatus((u32)i);
            }
        }
    }
}

bool pc_gcadapter_status(int port, PADStatus* out) {
    if (port < 0 || port >= GC_SLOTS || out == NULL) {
        return false;
    }
    lock_snapshot();
    const bool present = s_published_present[port];
    if (present) {
        *out = s_published_status[port];
    }
    unlock_snapshot();
    return present;
}

bool pc_gcadapter_raw(int port, uint8_t raw[6], bool* wireless) {
    if (port < 0 || port >= GC_SLOTS) {
        return false;
    }
    const uint64_t snap = atomic_load_explicit(&s_raw[port], memory_order_relaxed);
    if (!(snap & (1ull << 48))) {
        return false;
    }
    for (int k = 0; k < 6; k++) {
        raw[k] = (uint8_t)(snap >> (8 * k));
    }
    if (wireless != NULL) {
        *wireless = (snap & (1ull << 49)) != 0;
    }
    return true;
}

uint64_t pc_gcadapter_report_count(void) {
    return atomic_load_explicit(&s_reports, memory_order_relaxed);
}

/* Exit: stop every motor and let go of the adapter. The adapter keeps the
 * last motor state for as long as it has power, so without this a pad left
 * rumbling kept rumbling after the game closed. Runs after the input thread
 * (the only other user of the device) has been joined. */
void pc_gcadapter_shutdown(void) {
    if (s_dev == NULL) {
        return;
    }
    stop_all_motors();
    SDL_hid_close(s_dev);
    s_dev = NULL;
    pc_log_line("GC adapter: closed, motors stopped");
}
