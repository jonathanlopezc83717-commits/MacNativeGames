/* games/totk/runtime/amiibo_uuid.h without the game: a fresh UUID per scan (so BotW/TotK read the same .bin as a new
   amiibo of the same type and do not apply their once-a-day rule), with the opt-out. */
#include "../games/totk/runtime/amiibo_uuid.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    CHECK(amiibo_random_uuid_enabled(NULL), "unset: on by default");
    CHECK(amiibo_random_uuid_enabled(""), "empty: on by default");
    CHECK(amiibo_random_uuid_enabled("1"), "\"1\": on");
    CHECK(!amiibo_random_uuid_enabled("0"), "\"0\": off, the dump's own UUID is presented");

    unsigned char dump[540], before[540];
    for (int i = 0; i < 540; i++) dump[i] = (unsigned char)i;
    dump[3] = (unsigned char)(0x88 ^ dump[0] ^ dump[1] ^ dump[2]);
    dump[8] = (unsigned char)(dump[4] ^ dump[5] ^ dump[6] ^ dump[7]);
    memcpy(before, dump, sizeof dump);

    const unsigned char uid[7] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    amiibo_randomize_uuid(dump, uid);
    CHECK(!memcmp(dump, uid, 3) && !memcmp(dump + 4, uid + 3, 4), "the 7-byte UID is presented");
    CHECK(dump[3] == (unsigned char)(0x88 ^ dump[0] ^ dump[1] ^ dump[2]), "BCC0 still checks out");
    CHECK(dump[8] == (unsigned char)(dump[4] ^ dump[5] ^ dump[6] ^ dump[7]), "BCC1 still checks out");
    CHECK(!memcmp(dump + 9, before + 9, sizeof dump - 9), "nothing past the UUID bytes changes");
    CHECK(dump[0x54] == before[0x54] && dump[0x5B] == before[0x5B], "the amiibo type (0x54..) is unchanged");

    const unsigned char uid2[7] = {0x04, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44};
    unsigned char dump2[540];
    memcpy(dump2, before, sizeof dump2);
    amiibo_randomize_uuid(dump2, uid2);
    CHECK(memcmp(dump, dump2, 9), "a different random UID presents as a different amiibo");

    if (!failures) printf("amiibo uuid ok\n");
    return failures != 0;
}
