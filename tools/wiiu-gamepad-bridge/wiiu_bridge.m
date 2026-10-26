/* Wii U GamePad bridge for macOS: listen for MiisendU's JSON datagrams (UDP 4242 by default,
   Crayon2000/MiisendU-Wii-U: one JSON object per datagram at ~100 Hz, messages over 1400 bytes
   split across datagrams from the same sender), map the wiiUGamePad state through bridge_map.h,
   and publish it as a virtual HID gamepad with IOHIDUserDevice. Vendor/product are deliberately
   not Nintendo's, so runtime paths that match 0x057e devices ignore this pad. */
#import <Foundation/Foundation.h>
#import <IOKit/hidsystem/IOHIDUserDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include "bridge_map.h"
#include <mach/mach_time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

enum { BUFFERED = 8192, SENDERS = 4 };   /* BUFFERED: two MiisendU 1400-byte chunks with slack */

typedef struct { struct sockaddr_storage addr; socklen_t len; size_t n; uint64_t stamp; char text[BUFFERED]; } Sender;

static volatile sig_atomic_t running = 1;
static void on_sigint(int sig) { (void)sig; running = 0; }

static int verbose, print_reports;

/* The sender's slot: its own while it has one, an empty one when new, or the least recently used
   one if every slot is taken (ponytail: fixed 4 slots; one GamePad is the real case). */
static Sender *sender_slot(Sender *table, const struct sockaddr_storage *addr, socklen_t len) {
    static uint64_t clock;
    for (int i = 0; i < SENDERS; i++) {
        Sender *s = &table[i];
        if (s->stamp && s->len == len && !memcmp(&s->addr, addr, len)) { s->stamp = ++clock; return s; }
    }
    Sender *slot = NULL;
    for (int i = 0; i < SENDERS && !slot; i++)
        if (!table[i].stamp) slot = &table[i];
    if (!slot) {   /* every slot has a partial chain: evict the oldest one */
        slot = &table[0];
        for (int i = 1; i < SENDERS; i++)
            if (table[i].stamp < slot->stamp) slot = &table[i];
    }
    slot->addr = *addr; slot->len = len; slot->n = 0; slot->stamp = ++clock;
    return slot;
}

static void handle_datagram(IOHIDUserDeviceRef device, Sender *s, const void *bytes, size_t n) {
    if (n > BUFFERED - s->n) {   /* a broken chain: drop it rather than overflow */
        fprintf(stderr, "wiiu_bridge: sender buffer full (%zu bytes), dropping\n", s->n + n);
        s->n = 0;
        return;
    }
    memcpy(s->text + s->n, bytes, n);
    s->n += n;
    if (!wiiu_json_complete(s->text, s->n)) return;   /* MiisendU split the message: wait for more */

    @autoreleasepool {
        NSData *data = [NSData dataWithBytes:s->text length:(NSInteger)s->n];
        s->n = 0;
        NSError *error = nil;
        NSDictionary *root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
        if (![root isKindOfClass:[NSDictionary class]]) {
            if (verbose)
                fprintf(stderr, "wiiu_bridge: bad JSON: %s\n",
                        error ? error.localizedDescription.UTF8String : "not a JSON object");
            return;
        }
        NSDictionary *pad = root[@"wiiUGamePad"];   /* other root keys (wiiRemotes & co): not v1 */
        if (![pad isKindOfClass:[NSDictionary class]]) return;
        uint32_t hold = (uint32_t)[pad[@"hold"] unsignedLongLongValue];
        float lx = (float)[pad[@"lStickX"] doubleValue], ly = (float)[pad[@"lStickY"] doubleValue];
        float rx = (float)[pad[@"rStickX"] doubleValue], ry = (float)[pad[@"rStickY"] doubleValue];
        uint8_t report[WIIU_REPORT_SIZE];
        wiiu_bridge_report(hold, lx, ly, rx, ry, report);
        if (print_reports) {
            for (int i = 0; i < WIIU_REPORT_SIZE; i++) printf("%02x", report[i]);
            printf("\n");
            fflush(stdout);
        }
        if (device) IOHIDUserDeviceHandleReportWithTimeStamp(device, mach_absolute_time(), report, sizeof report);
        if (verbose) fprintf(stderr, "wiiu_bridge: hold=%#x sticks=%.2f %.2f %.2f %.2f\n", hold, lx, ly, rx, ry);
    }
}

int main(int argc, char **argv) {
    int port = 4242;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--print-reports")) print_reports = 1;
        else { fprintf(stderr, "usage: wiiu_bridge [--port N] [--verbose] [--print-reports]\n"); return 2; }
    }

    @autoreleasepool {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) { perror("wiiu_bridge: socket"); return 1; }
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof sin);
        sin.sin_family = AF_INET;
        sin.sin_port = htons((uint16_t)port);
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, (struct sockaddr *)&sin, sizeof sin) < 0) { perror("wiiu_bridge: bind"); return 1; }

        NSDictionary *properties = @{
            @kIOHIDReportDescriptorKey: [NSData dataWithBytes:wiiu_hid_descriptor
                                                       length:sizeof wiiu_hid_descriptor],
            @kIOHIDVendorIDKey: @0x1201,
            @kIOHIDProductIDKey: @0x0001,
            @kIOHIDProductKey: @"Wii U GamePad Bridge",
            @kIOHIDManufacturerKey: @"MacNativeGames",
        };
        IOHIDUserDeviceRef device = IOHIDUserDeviceCreateWithProperties(kCFAllocatorDefault,
                                                                        (CFDictionaryRef)properties, 0);
        /* Observed on macOS 26.6: IOServiceOpen fails kIOReturnNotPrivileged (0xe00002c2) unless the
           process carries the restricted com.apple.developer.hid.virtual.device entitlement, so keep
           receiving and printing reports without publishing until the bridge is built entitled. */
        if (!device)
            fprintf(stderr, "wiiu_bridge: IOHIDUserDeviceCreateWithProperties failed "
                            "(kIOReturnNotPrivileged 0xe00002c2: com.apple.developer.hid.virtual.device "
                            "entitlement required); receiving without publishing\n");
        fprintf(stderr, "wiiu_bridge: listening on UDP port %d\n", port);

        /* no SA_RESTART: Darwin's signal() would restart recvfrom after SIGINT and hang the loop */
        struct sigaction action;
        memset(&action, 0, sizeof action);
        action.sa_handler = on_sigint;
        sigaction(SIGINT, &action, NULL);

        Sender table[SENDERS];
        memset(table, 0, sizeof table);
        while (running) {
            struct sockaddr_storage addr; socklen_t len = sizeof addr;
            char bytes[2048];
            memset(&addr, 0, sizeof addr);   /* only len bytes come back: zero the rest for a stable compare */
            ssize_t n = recvfrom(fd, bytes, sizeof bytes, 0, (struct sockaddr *)&addr, &len);
            if (n < 0) {
                if (!running) break;
                continue;   /* a stray signal: keep listening */
            }
            handle_datagram(device, sender_slot(table, &addr, len), bytes, (size_t)n);
        }
        close(fd);
        if (device) CFRelease(device);
    }
    fprintf(stderr, "wiiu_bridge: stopped\n");
    return 0;
}
