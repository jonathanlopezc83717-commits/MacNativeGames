/* A Wii U GamePad as a generic macOS HID gamepad: the MiisendU JSON's VPAD hold bitmask and the two
   normalized sticks (-1..1) become the bytes of one HID input report (16 buttons, hat switch, four
   8-bit axes), plus the small scan that tells the UDP side when the buffered datagrams hold a
   complete JSON value (MiisendU's udp.cpp splits messages longer than 1400 bytes across datagrams).
   VPAD button masks are devkitPro/wut's, from include/vpad/input.h (enum VPADButtons):
   https://github.com/devkitPro/wut/blob/master/include/vpad/input.h
   A 0x8000, B 0x4000, X 0x2000, Y 0x1000, LEFT 0x0800, RIGHT 0x0400, UP 0x0200, DOWN 0x0100, ZL 0x0080,
   ZR 0x0040, L 0x0020, R 0x0010, PLUS 0x0008, MINUS 0x0004, HOME 0x0002, SYNC 0x0001, TV 0x00010000,
   STICK_R 0x00020000, STICK_L 0x00040000. Plain C11, shared by tools/wiiu-gamepad-bridge/wiiu_bridge.m
   and tests/wiiu_bridge_probe.c. */
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

enum {
    WIIU_VPAD_SYNC = 0x0001, WIIU_VPAD_HOME = 0x0002, WIIU_VPAD_MINUS = 0x0004, WIIU_VPAD_PLUS = 0x0008,
    WIIU_VPAD_R = 0x0010, WIIU_VPAD_L = 0x0020, WIIU_VPAD_ZR = 0x0040, WIIU_VPAD_ZL = 0x0080,
    WIIU_VPAD_DOWN = 0x0100, WIIU_VPAD_UP = 0x0200, WIIU_VPAD_RIGHT = 0x0400, WIIU_VPAD_LEFT = 0x0800,
    WIIU_VPAD_Y = 0x1000, WIIU_VPAD_X = 0x2000, WIIU_VPAD_B = 0x4000, WIIU_VPAD_A = 0x8000,
    WIIU_VPAD_TV = 0x00010000, WIIU_VPAD_STICK_R = 0x00020000, WIIU_VPAD_STICK_L = 0x00040000
};

/* The input report: buttons 1..16 as a little-endian bitfield (bit 0 = button 1), the hat switch in
   the low nibble of byte 2 (0..7 clockwise from up, 8 = released), then X, Y (left stick) and Z, Rz
   (right stick), each 0..255 with 128 at rest. HID Y grows downwards, the GamePad's Y grows
   upwards, so the Y axes are inverted on the way in. */
enum { WIIU_REPORT_SIZE = 7, WIIU_HAT_NONE = 8 };

/* The IOHIDUserDevice report descriptor: no report ID, 16 buttons, a null-capable 4-bit hat switch
   with 4 bits of padding, then the four 8-bit axes. X/Y/Z/Rz and buttons 1..15 are exactly what the
   runtime's generic HID pad reader (games/totk/runtime/gamepad.m) consumes. */
static const uint8_t wiiu_hid_descriptor[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop) */
    0x09, 0x05,        /* Usage (Game Pad) */
    0xA1, 0x01,        /* Collection (Application) */
    0x05, 0x09,        /*   Usage Page (Button) */
    0x19, 0x01,        /*   Usage Minimum (1) */
    0x29, 0x10,        /*   Usage Maximum (16) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x01,        /*   Logical Maximum (1) */
    0x75, 0x01,        /*   Report Size (1) */
    0x95, 0x10,        /*   Report Count (16) */
    0x81, 0x02,        /*   Input (Data,Var,Abs) */
    0x05, 0x01,        /*   Usage Page (Generic Desktop) */
    0x09, 0x39,        /*   Usage (Hat switch) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x25, 0x07,        /*   Logical Maximum (7) */
    0x75, 0x04,        /*   Report Size (4) */
    0x95, 0x01,        /*   Report Count (1) */
    0x81, 0x42,        /*   Input (Data,Var,Abs,Null) */
    0x75, 0x04,        /*   Report Size (4) */
    0x95, 0x01,        /*   Report Count (1) */
    0x81, 0x03,        /*   Input (Const) — padding */
    0x09, 0x30,        /*   Usage (X) */
    0x09, 0x31,        /*   Usage (Y) */
    0x09, 0x32,        /*   Usage (Z) */
    0x09, 0x35,        /*   Usage (Rz) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
    0x75, 0x08,        /*   Report Size (8) */
    0x95, 0x04,        /*   Report Count (4) */
    0x81, 0x02,        /*   Input (Data,Var,Abs) */
    0xC0               /* End Collection */
};

/* The HID button bits for a VPAD hold bitmask: A..Y to buttons 1..4, ZL/ZR/L/R to 5..8, MINUS/PLUS/
   HOME/SYNC to 9..12, the stick buttons to 13..14, TV to 15; button 16 stays unused and the dpad
   goes to the hat, not to buttons. */
static inline uint16_t wiiu_buttons(uint32_t hold) {
    static const struct { uint32_t vpad; uint8_t bit; } map[] = {
        {WIIU_VPAD_A, 0}, {WIIU_VPAD_B, 1}, {WIIU_VPAD_X, 2}, {WIIU_VPAD_Y, 3},
        {WIIU_VPAD_ZL, 4}, {WIIU_VPAD_ZR, 5}, {WIIU_VPAD_L, 6}, {WIIU_VPAD_R, 7},
        {WIIU_VPAD_MINUS, 8}, {WIIU_VPAD_PLUS, 9}, {WIIU_VPAD_HOME, 10}, {WIIU_VPAD_SYNC, 11},
        {WIIU_VPAD_STICK_L, 12}, {WIIU_VPAD_STICK_R, 13}, {WIIU_VPAD_TV, 14}
    };
    uint16_t buttons = 0;
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (hold & map[i].vpad) buttons |= (uint16_t)1 << map[i].bit;
    return buttons;
}

/* The hat value for the dpad: 0..7 clockwise from up, WIIU_HAT_NONE (8) when nothing is pressed or
   two opposite directions cancel. */
static inline uint8_t wiiu_hat(uint32_t hold) {
    static const uint8_t hats[16] = {8, 2, 6, 8, 4, 3, 5, 8, 0, 1, 7, 8, 8, 8, 8, 8};
    unsigned bits = (!!(hold & WIIU_VPAD_UP) << 3) | (!!(hold & WIIU_VPAD_DOWN) << 2) |
                    (!!(hold & WIIU_VPAD_LEFT) << 1) | !!(hold & WIIU_VPAD_RIGHT);
    return hats[bits];
}

/* One stick axis: clamped to -1..1, then 128 at rest and 0/255 at the ends; invert turns the Y axis
   around (HID grows downwards, the GamePad's grows upwards). */
static inline uint8_t wiiu_axis(float v, int invert) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    if (v > -0.05f && v < 0.05f) v = 0.0f;   /* ponytail: fixed 0.05 deadzone, per-axis calibration if drift matters */
    if (invert) v = -v;
    return (uint8_t)lroundf(v * 127.5f + 127.5f);
}

/* The HID input report for one GamePad state. */
static inline void wiiu_bridge_report(uint32_t hold, float lx, float ly, float rx, float ry,
                                      uint8_t out[WIIU_REPORT_SIZE]) {
    uint16_t buttons = wiiu_buttons(hold);
    out[0] = (uint8_t)buttons;
    out[1] = (uint8_t)(buttons >> 8);
    out[2] = wiiu_hat(hold);
    out[3] = wiiu_axis(lx, 0);
    out[4] = wiiu_axis(ly, 1);
    out[5] = wiiu_axis(rx, 0);
    out[6] = wiiu_axis(ry, 1);
}

/* 1: s[0..n) holds a complete JSON value (braces/brackets balanced outside strings), so the UDP
   side may parse it; 0: more bytes are needed. Anything that does not open a JSON value also
   returns 1, to hand it to the parser, which rejects it and drops the buffer. */
static inline int wiiu_json_complete(const char *s, size_t n) {
    int depth = 0, started = 0, in_string = 0, escaped = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') in_string = 1;
        else if (c == '{' || c == '[') { depth++; started = 1; }
        else if (c == '}' || c == ']') {
            if (!started) return 1;
            if (--depth <= 0) return 1;
        } else if (!started && c != ' ' && c != '\t' && c != '\n' && c != '\r') return 1;
    }
    return 0;
}
