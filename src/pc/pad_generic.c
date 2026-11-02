/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Generic USB controllers SDL has no mapping for.
 *
 * Controllers reach the game through SDL's gamepad layer (aurora opens
 * gamepads only): Xbox, PlayStation, Switch Pro, the Switch 2 GameCube
 * controller, 8BitDo and the hundreds in resources/gamecontrollerdb.txt are
 * all gamepads already. A pad SDL does not recognise is only a "joystick",
 * so it never showed up in the game at all. For those, this builds a mapping
 * in the layout most generic DirectInput pads use (face buttons 1-4, then
 * shoulders, triggers, select, start, stick clicks; first hat the D-pad;
 * axes in pairs) and hands it to SDL, which then announces the pad as a
 * gamepad. Anything it gets wrong can be rebound in the F1 controller menu,
 * or fixed for good with a line in the user folder's gamecontrollerdb.txt
 * (a pad mapped there is a gamepad already, so this never touches it).
 *
 * Wheels, flight sticks, throttles and instrument controllers are left
 * alone, as is anything with too few controls to play with, and devices
 * that only look like joysticks (foot pedals, keyboards and mice with a
 * joystick interface).
 *
 * Also here: a port the raw GameCube adapter path has a controller in
 * belongs to that controller (claim_adapter_ports). SDL hands out
 * player numbers in plug-in order, so any other device (an N64 adapter, a
 * pedal) could land on port 1 and be merged into the GameCube controller's
 * input, and get that port's rumble too: a Raphnet N64 adapter answered the
 * rumble with junk stick and button reports, which played as random smash
 * attacks and turnarounds.
 */
#include "pc/pc.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PENDING_MAX 16

static void claim_adapter_ports(void);

static SDL_SpinLock s_lock;
static SDL_JoystickID s_pending[PENDING_MAX];
static int s_npending;

/* The mapping for a pad with this many controls (exposed for the test). */
int pc_pad_generic_mapping(
    char* out, size_t size, const char* guid, const char* name, int axes, int buttons, int hats) {
    char clean[64];
    size_t n = 0;
    int len;

    if (buttons < 4 || (axes < 2 && hats < 1)) {
        return 0; /* not enough to play with */
    }
    /* Commas end a mapping field. */
    for (size_t i = 0; name != NULL && name[i] != '\0' && n + 1 < sizeof(clean); i++) {
        clean[n++] = name[i] == ',' ? ' ' : name[i];
    }
    clean[n] = '\0';
    if (n == 0) {
        snprintf(clean, sizeof(clean), "Generic USB controller");
    }

    len = snprintf(out, size, "%s,%s,a:b1,b:b2,x:b0,y:b3", guid, clean);
#define ADD(...)                                                                                   \
    do {                                                                                           \
        if (len > 0 && (size_t)len < size)                                                         \
            len += snprintf(out + len, size - (size_t)len, __VA_ARGS__);                           \
    } while (0)
    if (buttons >= 10) {
        ADD(",leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,back:b8,start:b9");
        if (buttons >= 12) {
            ADD(",leftstick:b10,rightstick:b11");
        }
    } else if (buttons >= 7) {
        ADD(",leftshoulder:b4,rightshoulder:b5,start:b%d", buttons - 1);
    } else if (buttons >= 6) {
        ADD(",leftshoulder:b4,start:b5");
    } else {
        ADD(",start:b%d", buttons - 1);
    }
    if (axes >= 2) {
        ADD(",leftx:a0,lefty:a1");
    }
    if (axes >= 4) {
        ADD(",rightx:a2,righty:a3");
    }
    if (hats >= 1) {
        ADD(",dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8");
    } else if (axes < 2) {
        return 0;
    }
#undef ADD
    return len > 0 && (size_t)len < size ? len : 0;
}

/* Not controllers, whatever their HID descriptor says. */
static bool looks_like_a_controller(SDL_JoystickID id) {
    static const char* const k_not_pads[] = {
        "footswitch",
        "foot switch",
        "pedal",
        "keyboard",
        "mouse",
        "consumer control",
        "system control",
        "receiver",
    };
    const char* name = SDL_GetJoystickNameForID(id);
    char lower[128];
    size_t n = 0;
    /* QinHeng (WCH) USB chips: foot switches, macro pads, keyboards. */
    if (SDL_GetJoystickVendorForID(id) == 0x1a86) {
        return false;
    }
    for (; name != NULL && name[n] != '\0' && n + 1 < sizeof(lower); n++) {
        lower[n] = (char)SDL_tolower((unsigned char)name[n]);
    }
    lower[n] = '\0';
    for (size_t i = 0; i < sizeof(k_not_pads) / sizeof(k_not_pads[0]); i++) {
        if (SDL_strstr(lower, k_not_pads[i]) != NULL) {
            return false;
        }
    }
    return true;
}

static bool wanted(SDL_JoystickID id) {
    if (!looks_like_a_controller(id)) {
        return false;
    }
    switch (SDL_GetJoystickTypeForID(id)) {
    case SDL_JOYSTICK_TYPE_WHEEL:
    case SDL_JOYSTICK_TYPE_FLIGHT_STICK:
    case SDL_JOYSTICK_TYPE_THROTTLE:
    case SDL_JOYSTICK_TYPE_GUITAR:
    case SDL_JOYSTICK_TYPE_DRUM_KIT:
        return false;
    default:
        return !SDL_IsGamepad(id);
    }
}

static void map_one(SDL_JoystickID id) {
    char guid[33];
    char mapping[512];
    SDL_Joystick* joy;
    int axes, buttons, hats;

    if (!wanted(id)) {
        return;
    }
    joy = SDL_OpenJoystick(id);
    if (joy == NULL) {
        return;
    }
    axes = SDL_GetNumJoystickAxes(joy);
    buttons = SDL_GetNumJoystickButtons(joy);
    hats = SDL_GetNumJoystickHats(joy);
    SDL_GUIDToString(SDL_GetJoystickGUID(joy), guid, sizeof(guid));
    if (pc_pad_generic_mapping(
            mapping, sizeof(mapping), guid, SDL_GetJoystickName(joy), axes, buttons, hats) > 0)
    {
        /* SDL turns the connected joystick into a gamepad (and tells aurora)
         * as soon as it has a mapping for it. */
        if (SDL_AddGamepadMapping(mapping) >= 0) {
            pc_log_line("input: no mapping for \"%s\" (%d axes, %d buttons, %d hats): using a "
                        "generic layout, rebind in F1 if needed",
                SDL_GetJoystickName(joy) ? SDL_GetJoystickName(joy) : "?", axes, buttons, hats);
        }
    } else {
        pc_log_line("input: \"%s\" left alone (%d axes, %d buttons, %d hats)",
            SDL_GetJoystickName(joy) ? SDL_GetJoystickName(joy) : "?", axes, buttons, hats);
    }
    SDL_CloseJoystick(joy);
}

/* May run on whichever thread updates joysticks (the 1000 Hz input thread):
 * only note the id, the mapping is made on the main thread. */
static bool SDLCALL on_event(void* unused, SDL_Event* e) {
    (void)unused;
    if (e->type == SDL_EVENT_JOYSTICK_ADDED) {
        SDL_LockSpinlock(&s_lock);
        if (s_npending < PENDING_MAX) {
            s_pending[s_npending++] = e->jdevice.which;
        }
        SDL_UnlockSpinlock(&s_lock);
    }
    return true;
}

void pc_pad_generic_init(void) {
    int count = 0;
    SDL_JoystickID* ids;

    SDL_AddEventWatch(on_event, NULL);
    /* Pads plugged in before the watch went in. */
    ids = SDL_GetJoysticks(&count);
    if (ids != NULL) {
        for (int i = 0; i < count; i++) {
            map_one(ids[i]);
        }
        SDL_free(ids);
    }
}

void pc_pad_generic_frame(void) {
    SDL_JoystickID ids[PENDING_MAX];
    int n;

    SDL_LockSpinlock(&s_lock);
    n = s_npending;
    memcpy(ids, s_pending, sizeof(ids[0]) * (size_t)n);
    s_npending = 0;
    SDL_UnlockSpinlock(&s_lock);
    for (int i = 0; i < n; i++) {
        map_one(ids[i]);
    }
    claim_adapter_ports();
}

/* Once a frame: no SDL gamepad shares a port with a controller in the raw
 * GameCube adapter. One SDL put there moves to the first port with neither,
 * or to none. */
static void claim_adapter_ports(void) {
    bool adapter[4];
    for (int i = 0; i < 4; i++) {
        PADStatus st;
        adapter[i] = pc_gcadapter_status(i, &st);
    }
    for (int i = 0; i < 4; i++) {
        SDL_Gamepad* pad;
        int to = -1;
        if (!adapter[i] || (pad = SDL_GetGamepadFromPlayerIndex(i)) == NULL) {
            continue;
        }
        for (int j = 0; j < 4; j++) {
            if (!adapter[j] && SDL_GetGamepadFromPlayerIndex(j) == NULL) {
                to = j;
                break;
            }
        }
        SDL_SetGamepadPlayerIndex(pad, to);
        char where[16];
        if (to >= 0) {
            SDL_snprintf(where, sizeof(where), "port %d", to + 1);
        } else {
            SDL_snprintf(where, sizeof(where), "no port");
        }
        pc_log_line("input: \"%s\" moved off port %d (the GameCube adapter's controller is "
                    "there) to %s",
            SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "?", i + 1, where);
    }
}
