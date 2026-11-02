/* Compile the real adapter implementation; section GC drops unused HID paths. */
#include "../src/pc/gcadapter.c"
#include <assert.h>
#include <pthread.h>
static int virtual_writes;
static PADStatus virtual_status[GC_SLOTS];
static bool virtual_present[GC_SLOTS];
HSD_RumbleData HSD_Rumble_804C22E0[GC_SLOTS];
void pc_log_line(const char* fmt, ...) {
    (void)fmt;
}
bool pc_is_rumble_enabled(void) {
    return true;
}
void PADSetVirtualStatus(u32 port, const PADStatus* status) {
    virtual_status[port] = *status;
    virtual_present[port] = true;
    virtual_writes++;
}
void PADClearVirtualStatus(u32 port) {
    virtual_present[port] = false;
    virtual_writes++;
}
static void* writer(void* unused) {
    (void)unused;
    for (int n = 0; n < 100000; n++) {
        const unsigned char value = (n & 1) ? 31 : 63;
        for (int i = 0; i < GC_SLOTS; i++) {
            memset(&s_status[i], value, sizeof(s_status[i]));
            s_present[i] = true;
        }
        publish_snapshot();
    }
    return NULL;
}

int main(void) {
    /* Motor byte: only "rumble" with rumble on runs a motor; the game's
     * starting state (0, hard stop) must not send the brake byte, which
     * clone adapters treat as "on". */
    assert(motor_byte(0, true) == 0);
    assert(motor_byte(1, true) == 0);
    assert(motor_byte(2, true) == 1);
    assert(motor_byte(2, false) == 0);
    assert(motor_byte(3, true) == 0);

    /* While rumbling, a one-report blip (A for one report, stick spike) is
     * filtered; a real press shows one report later. */
    {
        uint8_t r[9] = {GC_SLOT_WIRED | GC_SLOT_RUMBLE_POWER, 0, 0, 128, 128, 128, 128, 0, 0};
        PADStatus o;
        parse_slot(3, r, 10);
        s_motor_on_until[3] = 1000;
        parse_slot(3, r, 11);
        parse_slot(3, r, 12);
        r[1] = 1;   /* A */
        r[3] = 228; /* stick far right */
        parse_slot(3, r, 13);
        publish_snapshot();
        assert(pc_gcadapter_status(3, &o) && o.button == 0 && o.stickX == 0);
        r[1] = 0;
        r[3] = 128;
        parse_slot(3, r, 14);
        publish_snapshot();
        assert(pc_gcadapter_status(3, &o) && o.button == 0 && o.stickX == 0);
        assert(s_glitch[3][1] == 1);
        r[1] = 1;
        r[3] = 228;
        parse_slot(3, r, 15);
        parse_slot(3, r, 16);
        publish_snapshot();
        assert(pc_gcadapter_status(3, &o) && o.button == PAD_BUTTON_A && o.stickX == 100);
        /* Motor off: no filter, the report is taken as is. */
        r[1] = 0;
        r[3] = 128;
        parse_slot(3, r, 2000);
        publish_snapshot();
        assert(pc_gcadapter_status(3, &o) && o.button == 0 && o.stickX == 0);
        clear_slot(3);
        publish_snapshot();
    }
    uint8_t report[9] = {GC_SLOT_WIRED, 0, 0, 128, 128, 128, 128, 0, 0};
    parse_slot(1, report, 1);
    assert(virtual_writes == 0); /* polling thread must never mutate PADRead state */
    publish_snapshot();
    PADStatus out;
    assert(pc_gcadapter_status(1, &out) && out.button == 0 && out.stickX == 0);
    report[1] = 1;
    report[3] = 208;
    parse_slot(1, report, 2);
    /* Readers retain the last complete report until publication. */
    assert(pc_gcadapter_status(1, &out) && out.button == 0);
    publish_snapshot();
    assert(pc_gcadapter_status(1, &out) && out.button == PAD_BUTTON_A && out.stickX == 80);
    assert(virtual_writes == 0);
    HSD_Rumble_804C22E0[1].last_status = 2;
    pc_gcadapter_apply();
    assert(virtual_present[1] && virtual_status[1].button == PAD_BUTTON_A);
    assert(atomic_load(&s_motor_request[1]) == 2);
    virtual_writes = 0;
    clear_slot(1);
    assert(virtual_writes == 0);
    publish_snapshot();
    assert(!pc_gcadapter_status(1, &out));
    pc_gcadapter_apply();
    assert(!virtual_present[1]);
    /* Concurrent publication never exposes mixed fields from different reports. */
    pthread_t thread;
    assert(pthread_create(&thread, NULL, writer, NULL) == 0);
    for (int n = 0; n < 100000; n++) {
        if (pc_gcadapter_status(2, &out)) {
            assert(out.stickX == 31 || out.stickX == 63);
            assert(out.stickX == out.stickY && out.stickX == out.substickX);
            assert(out.stickX == out.triggerLeft && out.stickX == out.triggerRight);
            assert(out.button == (unsigned char)out.stickX * 257);
        }
    }
    assert(pthread_join(thread, NULL) == 0);
    return 0;
}
