/* tools/wiiu-gamepad-bridge/bridge_map.h: VPAD hold bits and stick floats become the HID report
   bytes, and the buffered-datagram scan says when the JSON is complete. */
#include "../tools/wiiu-gamepad-bridge/bridge_map.h"
#include <stdio.h>
#include <string.h>

static int failures, checked;
static void check(int ok, const char *what) { checked++; if (!ok) { failures++; fprintf(stderr, "FAIL: %s\n", what); } }

/* The report for one state, byte for byte. */
static void check_report(uint32_t hold, float lx, float ly, float rx, float ry,
                         const uint8_t want[WIIU_REPORT_SIZE], const char *what) {
    uint8_t got[WIIU_REPORT_SIZE];
    wiiu_bridge_report(hold, lx, ly, rx, ry, got);
    check(!memcmp(got, want, WIIU_REPORT_SIZE), what);
    if (memcmp(got, want, WIIU_REPORT_SIZE)) {
        fprintf(stderr, "  want:");
        for (int i = 0; i < WIIU_REPORT_SIZE; i++) fprintf(stderr, " %02x", want[i]);
        fprintf(stderr, "\n  got: ");
        for (int i = 0; i < WIIU_REPORT_SIZE; i++) fprintf(stderr, " %02x", got[i]);
        fprintf(stderr, "\n");
    }
}

int main(void) {
    /* neutral: no buttons, hat released, every stick centred */
    const uint8_t idle[7] = {0x00, 0x00, 0x08, 0x80, 0x80, 0x80, 0x80};
    check_report(0, 0, 0, 0, 0, idle, "no input is a centred idle report");

    /* face buttons and shoulders to bits 0..7 of byte 0 (masks cited in bridge_map.h from wut) */
    const uint8_t face_base[7] = {0x00, 0x00, 0x08, 0x80, 0x80, 0x80, 0x80};
    uint8_t want[7];
    memcpy(want, face_base, 7); want[0] = 0x01; check_report(WIIU_VPAD_A, 0, 0, 0, 0, want, "A is HID button 1");
    memcpy(want, face_base, 7); want[0] = 0x02; check_report(WIIU_VPAD_B, 0, 0, 0, 0, want, "B is HID button 2");
    memcpy(want, face_base, 7); want[0] = 0x04; check_report(WIIU_VPAD_X, 0, 0, 0, 0, want, "X is HID button 3");
    memcpy(want, face_base, 7); want[0] = 0x08; check_report(WIIU_VPAD_Y, 0, 0, 0, 0, want, "Y is HID button 4");
    memcpy(want, face_base, 7); want[0] = 0x10; check_report(WIIU_VPAD_ZL, 0, 0, 0, 0, want, "ZL is HID button 5");
    memcpy(want, face_base, 7); want[0] = 0x20; check_report(WIIU_VPAD_ZR, 0, 0, 0, 0, want, "ZR is HID button 6");
    memcpy(want, face_base, 7); want[0] = 0x30; check_report(WIIU_VPAD_ZL | WIIU_VPAD_ZR, 0, 0, 0, 0, want, "ZL and ZR together");
    memcpy(want, face_base, 7); want[0] = 0x01; want[1] = 0x02;
    check_report(WIIU_VPAD_A | WIIU_VPAD_PLUS, 0, 0, 0, 0, want, "A and PLUS in the two button bytes");

    /* the dpad is the hat, in byte 2's low nibble: 0 up, 1 up/right, ..., 8 released */
    static const struct { uint32_t hold; uint8_t hat; } hats[] = {
        {WIIU_VPAD_UP, 0}, {WIIU_VPAD_UP | WIIU_VPAD_RIGHT, 1}, {WIIU_VPAD_RIGHT, 2},
        {WIIU_VPAD_DOWN | WIIU_VPAD_RIGHT, 3}, {WIIU_VPAD_DOWN, 4}, {WIIU_VPAD_DOWN | WIIU_VPAD_LEFT, 5},
        {WIIU_VPAD_LEFT, 6}, {WIIU_VPAD_UP | WIIU_VPAD_LEFT, 7},
        {WIIU_VPAD_UP | WIIU_VPAD_DOWN, 8},   /* opposite directions cancel to released */
    };
    for (size_t i = 0; i < sizeof hats / sizeof *hats; i++) {
        memcpy(want, face_base, 7); want[2] = hats[i].hat;
        check_report(hats[i].hold, 0, 0, 0, 0, want, "dpad direction to hat value");
    }

    /* sticks: the float maps to 0..255, 128 centred; Y is inverted (HID grows downwards) */
    check_report(0, 1.0f, 0, 0, 0, (const uint8_t[]){0x00, 0x00, 0x08, 0xFF, 0x80, 0x80, 0x80}, "left stick full right");
    check_report(0, -1.0f, 0, 0, 0, (const uint8_t[]){0x00, 0x00, 0x08, 0x00, 0x80, 0x80, 0x80}, "left stick full left");
    check_report(0, 0, 1.0f, 0, 0, (const uint8_t[]){0x00, 0x00, 0x08, 0x80, 0x00, 0x80, 0x80}, "left stick up is Y 0");
    check_report(0, 0, -1.0f, 0, 0, (const uint8_t[]){0x00, 0x00, 0x08, 0x80, 0xFF, 0x80, 0x80}, "left stick down is Y 255");
    check_report(0, 0, 0, 1.0f, -1.0f, (const uint8_t[]){0x00, 0x00, 0x08, 0x80, 0x80, 0xFF, 0xFF}, "right stick to the top right corners of Z and Rz");
    check_report(0, 0.049f, -0.049f, 0.03f, -0.03f, idle, "inside the deadzone reads as centre");
    check_report(0, 5.0f, -5.0f, 0, 0, (const uint8_t[]){0x00, 0x00, 0x08, 0xFF, 0xFF, 0x80, 0x80}, "out-of-range floats clamp to the ends");
    check_report(0, 0.5f, 0, 0, 0, (const uint8_t[]){0x00, 0x00, 0x08, 0xBF, 0x80, 0x80, 0x80}, "halfway left stick is 191");

    /* buttons and sticks in one report */
    check_report(WIIU_VPAD_A | WIIU_VPAD_ZR | WIIU_VPAD_UP, 1.0f, 0, 0, -1.0f,
                 (const uint8_t[]){0x21, 0x00, 0x00, 0xFF, 0x80, 0x80, 0xFF}, "buttons, hat and both sticks together");

    /* the report descriptor packs to the report: 16 bits + 4 hat + 4 pad + 4 bytes = 7 */
    check(WIIU_REPORT_SIZE == 7 && sizeof wiiu_hid_descriptor > 40 && wiiu_hid_descriptor[0] == 0x05 &&
              wiiu_hid_descriptor[1] == 0x01 && wiiu_hid_descriptor[2] == 0x09 && wiiu_hid_descriptor[3] == 0x05,
          "the descriptor opens with a Generic Desktop Game Pad");

    /* the datagram chain is parsed only when the JSON is balanced (MiisendU split messages) */
    check(!wiiu_json_complete("", 0) && !wiiu_json_complete("{", 1), "no braces yet: wait");
    check(!wiiu_json_complete("{\"wiiUGamePad\":{\"hold\":32768", sizeof "{\"wiiUGamePad\":{\"hold\":32768" - 1),
          "a cut-off message: wait");
    check(wiiu_json_complete("{\"wiiUGamePad\":{\"hold\":32768}}", sizeof "{\"wiiUGamePad\":{\"hold\":32768}}" - 1),
          "the whole message: parse");
    check(wiiu_json_complete(" {\"a\":1}\n", sizeof " {\"a\":1}\n" - 1), "surrounding whitespace still balances");
    check(wiiu_json_complete("{\"a\":\"}\"}", sizeof "{\"a\":\"}\"}" - 1),
          "a brace inside a string does not close the message early");
    check(wiiu_json_complete("{\"a\":\"}\"}extra", sizeof "{\"a\":\"}\"}extra" - 1),
          "balanced is complete even with trailing bytes");
    check(wiiu_json_complete("not json", sizeof "not json" - 1),
          "garbage goes to the parser, which drops it");

    printf("{\"status\":\"%s\",\"probe\":\"wiiu-bridge\",\"checked\":%d,\"failures\":%d}\n",
           failures ? "fail" : "pass", checked, failures);
    return failures != 0;
}
