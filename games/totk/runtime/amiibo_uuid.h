/* The presented UUID of the virtual amiibo (applet_host.c), without the runtime, so tests/test_amiibo_uuid.c can
   check it. BotW and TotK let each amiibo be scanned once a day and key that rule on the tag's UUID, so presenting
   the dump's own UUID makes the same .bin unusable until tomorrow. A fresh random UUID per scan (emuiibo's
   randomized-UUID option, on by default here) reads as a new amiibo of the same type: the amiibo ID at 0x54.. in
   the dump, which GetModelInfo reports, is never touched. TOTK_AMIIBO_RANDOM_UID=0 presents the dump's own UUID
   instead, for games that save data to the amiibo and care which physical tag it is. */
#pragma once
#include <string.h>

/* On unless the value is exactly "0". */
static inline int amiibo_random_uuid_enabled(const char *e) { return !(e && !strcmp(e, "0")); }

/* Put uid (the 7-byte NTAG215 UID) into a dump's first pages, fixing the two check bytes the dump validation
   (and a real tag) carries: byte 3 is 0x88 ^ UID0..2, byte 8 is UID3..6 xor'd. Nothing else changes. */
static inline void amiibo_randomize_uuid(unsigned char *dump, const unsigned char uid[7]) {
    dump[0] = uid[0]; dump[1] = uid[1]; dump[2] = uid[2];
    dump[3] = (unsigned char)(0x88 ^ uid[0] ^ uid[1] ^ uid[2]);
    dump[4] = uid[3]; dump[5] = uid[4]; dump[6] = uid[5]; dump[7] = uid[6];
    dump[8] = (unsigned char)(uid[3] ^ uid[4] ^ uid[5] ^ uid[6]);
}
