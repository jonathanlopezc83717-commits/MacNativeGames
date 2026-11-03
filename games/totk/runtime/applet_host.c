/* Host side of applet, telemetry, NFC and error-display services that main reaches through IPC.
   Client-side SDK code (PlayReport encoding, keyboard presets, diag logging) runs unchanged; only the
   calls that would talk to system services are taken here. */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* ---------- nn::oe ---------- */
static float balance[2] = {1.0f, 1.0f};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t message_cv = PTHREAD_COND_INITIALIZER;
static uint32_t messages[32]; static unsigned head, tail;
static uint64_t start_ns;
__attribute__((constructor)) static void started(void) { start_ns = runtime_now_ns(); }

void runtime_oe_push_message(uint32_t message) {
    pthread_mutex_lock(&lock);
    if (tail - head < 32) { messages[tail++ % 32] = message; pthread_cond_broadcast(&message_cv); }
    pthread_mutex_unlock(&lock);
}
static void get_balance(float *l, float *r) { pthread_mutex_lock(&lock); if (l) *l = balance[0]; if (r) *r = balance[1]; pthread_mutex_unlock(&lock); }
static void set_balance(float l, float r) {
    if (l < 0 || l > 1 || r < 0 || r > 1) runtime_fail("volume balance out of range");
    pthread_mutex_lock(&lock); balance[0] = l; balance[1] = r; pthread_mutex_unlock(&lock);
}
static int64_t total_active_time(void) { return (int64_t)(runtime_now_ns() - start_ns); }   /* TimeSpan in ns */
/* The host never sleeps the console; the extended inactivity window has no further effect. */
static void inactivity_extended(unsigned char on) { (void)on; }
/* No system startup logo is drawn on the host, so there is nothing to dismiss. */
static void finish_startup_logo(void) { fprintf(stderr, "[totk] game finished startup logo\n"); }
/* The single native executable was launched directly; it has no predecessor
 * in an applet ExecuteProgram chain. -1 keeps the game's normal user-open path. */
static int previous_program_index(void) { return -1; }
/* A stable, nonzero resource id for this process's one application. Host input
 * and audio share that application; there is no separate system applet client. */
static uint64_t applet_resource_user_id(void) { return 1; }
static unsigned char try_pop_launch_parameter(uint64_t *size,void *buffer,uint64_t capacity) {
    (void)buffer;(void)capacity;if(size)*size=0;return 0;
}
static unsigned char try_pop(uint32_t *out) {
    pthread_mutex_lock(&lock); int ok = head != tail; if (ok && out) *out = messages[head++ % 32]; pthread_mutex_unlock(&lock); return (unsigned char)ok;
}
int runtime_core_away(void); void runtime_core_back(int *away);
static uint32_t pop(void) {
    __attribute__((cleanup(runtime_core_back))) int away = runtime_core_away();   /* blocks like a Horizon wait */
    pthread_mutex_lock(&lock); while (head == tail) pthread_cond_wait(&message_cv, &lock);
    uint32_t m = messages[head++ % 32]; pthread_mutex_unlock(&lock); return m;
}

/* ---------- nn::prepo: play reports are not transmitted (no Nintendo servers) ---------- */
static uint64_t reports;
static unsigned report_save(const void *report) { (void)report; if (!(reports++ % 100)) fprintf(stderr, "[totk] play report kept local (not transmitted), count %llu\n", (unsigned long long)reports); return 0; }
static unsigned report_save_uid(const void *report, const void *uid) { (void)uid; return report_save(report); }
static unsigned immediate_transmission(void) { return 0; }
static unsigned connection_status(void *report, const char *a, const char *b, const char *c) { (void)report; (void)a; (void)b; (void)c; return 0; }

/* ---------- nn::nfp: one virtual NTAG215 tag from a .bin dump (key 8 picks it; amiibo.txt next to controls.txt) ---------- */
#define NFP_DEVICE_NOT_FOUND 0x8073u   /* nfp module 115, description 64 */
void hle_signal_system_event(void *);   /* threads.c: the write end hle_bind_system_event created */
int setting_read(const char *name, char *word, size_t size, char *path, size_t path_size);   /* gamepad.m, un-static for amiibo.txt */
void setting_write(const char *name, const char *word, const char *what);
static int nfp_state;                   /* nn::nfp::State: 0 NonInitialized, 1 Initialized */
static int nfp_dev;                     /* nn::nfp::DeviceState: 0 Initialized, 1 SearchingForTag, 2 TagFound, 4 TagMounted */
static void *nfp_ev_activate, *nfp_ev_deactivate;
static unsigned char nfp_dump[540];
static int nfp_dump_ok;
/* amiibo.txt holds one line: the absolute path of an NTAG215 .bin dump. Re-read at Initialize and at every StartDetection,
   so a dump picked (or hand-edited) since the last scan is what the next scan presents. */
static void nfp_load_dump(void) {
    char dir[4096], path[4096] = {0};
    nfp_dump_ok = 0;
    if (!setting_read("amiibo.txt", path, sizeof path, dir, sizeof dir)) return;
    path[strcspn(path, "\r\n")] = 0;
    if (!*path) return;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[totk] amiibo: cannot read %s\n", path); return; }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return; }
    long size = ftell(f);
    if (fseek(f, 0, SEEK_SET)) { fclose(f); return; }
    if (size != 540 && size != 532 && size != 572) {
        fprintf(stderr, "[totk] amiibo: %s is %ld bytes, need a 540-byte NTAG215 dump\n", path, size);
        fclose(f); return;
    }
    size_t got = fread(nfp_dump, 1, size < 540 ? (size_t)size : 540, f);
    fclose(f);
    if (got < 0x5C || nfp_dump[3] != (unsigned char)(0x88 ^ nfp_dump[0] ^ nfp_dump[1] ^ nfp_dump[2]) ||
        nfp_dump[8] != (nfp_dump[4] ^ nfp_dump[5] ^ nfp_dump[6] ^ nfp_dump[7]) || nfp_dump[0x5B] != 0x02) {
        fprintf(stderr, "[totk] amiibo: %s failed the NTAG215 checks (BCC or byte 0x5B)\n", path);
        return;
    }
    nfp_dump_ok = 1;
    fprintf(stderr, "[totk] amiibo: %s\n", path);
}
void runtime_amiibo_set_path(const char *path) { setting_write("amiibo.txt", path, "amiibo path"); }
static unsigned nfp_initialize(void) { nfp_state = 1; nfp_dev = 0; nfp_load_dump(); return 0; }
static unsigned nfp_finalize(void) {
    nfp_state = 0; nfp_dev = 0;
    nfp_ev_activate = nfp_ev_deactivate = NULL;   /* the game owns the SystemEvent objects and may destroy them with Finalize */
    return 0;
}
static int nfp_get_state(void) { return nfp_state; }
static unsigned nfp_list(void *handles, int *count, int max) {
    if (max > 0 && handles) memset(handles, 0, 8);   /* the one device, handle 0 */
    if (count) *count = 1;
    return 0;
}
/* The availability event is bound already signaled: the host always has the one reader, and a cleared event would hang
   the game's wait. Manual clear keeps it up; the signal is level state, not an edge. */
static unsigned nfp_attach_availability(void *event) {
    if (!event) runtime_fail("nfp availability event");
    hle_bind_system_event(event, 0);
    hle_signal_system_event(event);
    return 0;
}
static unsigned nfp_attach_activate(void *event, const void *h) {
    (void)h;
    if (!event) runtime_fail("nfp activate event");
    nfp_ev_activate = event;
    hle_bind_system_event(event, 0);   /* manual clear: the signal below stays up while a tag is presented */
    if (nfp_dev == 2 || nfp_dev == 4) hle_signal_system_event(event);   /* a tag is already presented (StartDetection first) */
    return 0;
}
static unsigned nfp_attach_deactivate(void *event, const void *h) {
    (void)h;
    if (!event) runtime_fail("nfp deactivate event");
    nfp_ev_deactivate = event;
    hle_bind_system_event(event, 0);
    return 0;
}
static unsigned nfp_get_npad_id(uint32_t *out, const void *h) { (void)h; if (out) *out = 0; return 0; }   /* Player 1 */
static unsigned nfp_start_detection(const void *h) {
    (void)h;
    nfp_load_dump();
    if (nfp_dump_ok) {
        nfp_dev = 2;   /* TagFound */
        if (nfp_ev_activate) hle_signal_system_event(nfp_ev_activate);
    } else nfp_dev = 1;   /* SearchingForTag: the game waits, as for a tag that has not arrived yet */
    return 0;
}
static unsigned nfp_stop_detection(const void *h) {
    (void)h;
    if (nfp_ev_deactivate) hle_signal_system_event(nfp_ev_deactivate);
    nfp_dev = 0;   /* Initialized */
    return 0;
}
static unsigned nfp_mount(const void *h, unsigned model, unsigned target) { (void)h; (void)model; (void)target; nfp_dev = 4; return 0; }
static int nfp_device_state(const void *h) { (void)h; return nfp_dev; }   /* the DeviceState enum, read as-is by the game */
static unsigned nfp_get_tag_info(void *info, const void *h) {
    (void)h;
    if (!info) runtime_fail("nfp tag info");
    memset(info, 0, 0x58);
    if ((nfp_dev != 2 && nfp_dev != 4) || !nfp_dump_ok) return NFP_DEVICE_NOT_FOUND;
    memcpy(info, nfp_dump, 9);        /* UUID: UID0..2, BCC0, UID3..6, BCC1 */
    ((unsigned char *)info)[0xA] = 9; /* uuid_length */
    uint32_t protocol = 1, type = 2;  /* NFC, NTAG215 (yuzu's values) */
    memcpy((unsigned char *)info + 0x20, &protocol, 4);
    memcpy((unsigned char *)info + 0x24, &type, 4);
    return 0;
}
static unsigned nfp_get_model_info(void *info, const void *h) {
    (void)h;
    if (!info) runtime_fail("nfp model info");
    memset(info, 0, 0x40);
    unsigned char *o = info;
    const unsigned char *d = nfp_dump;
    o[0] = d[0x54]; o[1] = d[0x55];   /* CharacterId */
    o[2] = d[0x56]; o[3] = d[0x57];   /* variant, type */
    o[4] = d[0x59]; o[5] = d[0x58];   /* amiibo number: stored big-endian at 0x58..0x59 in the dump */
    o[6] = d[0x5A];
    return 0;
}
#ifdef GAME_BOTW
/* Breath of the Wild additionally reads CommonInfo / RegisterInfo, opens the application area (access id 0x1019C800) and
   runs the restorer; all absent from the TotK imports. */
static unsigned nfp_get_common_info(void *info, const void *h) {
    (void)h;
    if (!info) runtime_fail("nfp common info");
    memset(info, 0, 0x40);            /* CommonInfo is 0x40: the game places RegisterInfo at +0x40 */
    ((unsigned char *)info)[0] = 1;    /* amiibo_initialized */
    uint32_t area = 0xD8;              /* ApplicationAreaSize: the capacity the game passes to GetApplicationArea */
    memcpy((unsigned char *)info + 8, &area, 4);
    return 0;
}
static unsigned nfp_get_register_info(void *info, const void *h) {
    (void)h;
    if (!info) runtime_fail("nfp register info");
    memset(info, 0, 0x100);
    return 0;
}
static unsigned nfp_open_application_area(const void *h, unsigned access) { (void)h; (void)access; return 0; }
static unsigned nfp_get_application_area(void *buf, size_t *size, const void *h, size_t capacity) {
    (void)h;
    if (buf) memset(buf, 0, capacity < 0xD8u ? capacity : 0xD8u);   /* the game keeps its application area zeroed */
    if (size) *size = 0xD8;
    return 0;
}
static unsigned nfp_start_restorer(void *handle, const void *param, const void *tag) {
    (void)handle; (void)param; (void)tag;
    return 0;   /* nothing is restored: the host never writes tag data back */
}
static unsigned nfp_mount_model(const void *h, unsigned model) { (void)h; (void)model; nfp_dev = 4; return 0; }
static unsigned nfp_unmount(const void *h) { (void)h; nfp_dev = 2; return 0; }
#endif

/* ---------- nn::err (nn::swkbd is in runtime/swkbd_host.c) ---------- */
static void show_error(uint32_t result) {
    fprintf(stderr, "[totk] game error dialog: %04u-%04u (result %#x)\n", 2000 + (result & 0x1ff), (result >> 9) & 0x1fff, result);
}

#ifdef GAME_BOTW
/* SDK 7 (Breath of the Wild 1.6.0, botw.md). Exit requests are handled by the window (the section brackets change nothing);
   video recording has no Mac counterpart; the DLC shop and the DLC-version error screen are console applets. */
static void exit_section(void) {}
static void enable_recording(void) {}
static void unacceptable_aoc_version(void) { fprintf(stderr, "[botw] the game reported a DLC version it cannot use\n"); }
static uint32_t show_aoc_shop(uint64_t application, const void *user) { (void)application; (void)user; fprintf(stderr, "[botw] eShop DLC page: not available\n"); return 0; }
#endif
void *runtime_applet_override(const char *name) {
    static const struct { const char *name; void *fn; } map[] = {
        {"_ZN2nn6applet23GetPreviousProgramIndexEv", previous_program_index},
        {"_ZN2nn6applet23GetAppletResourceUserIdEv", applet_resource_user_id},
        {"_ZN2nn2am23GetAppletResourceUserIdEv", applet_resource_user_id},
        {"_ZN2nn2oe21TryPopLaunchParameterEPmPvm", try_pop_launch_parameter},
        {"_ZN2nn2oe24GetExpectedVolumeBalanceEPfS1_", get_balance}, {"_ZN2nn2oe24SetExpectedVolumeBalanceEff", set_balance},
        {"_ZN2nn2oe25GetProgramTotalActiveTimeEv", total_active_time}, {"_ZN2nn2oe38SetUserInactivityDetectionTimeExtendedEb", inactivity_extended},
        {"_ZN2nn2oe44SetUserInactivityDetectionTimeExtendedUnsafeEb", inactivity_extended},
        {"_ZN2nn2oe17FinishStartupLogoEv", finish_startup_logo}, {"_ZN2nn2oe25TryPopNotificationMessageEPj", try_pop},
        {"_ZN2nn2oe22PopNotificationMessageEv", pop},
        {"_ZN2nn5prepo10PlayReport4SaveEv", report_save}, {"_ZN2nn5prepo10PlayReport4SaveERKNS_7account3UidE", report_save_uid},
        {"_ZN2nn5prepo28RequestImmediateTransmissionEv", immediate_transmission},
        {"_ZN2nn5prepo27AddInternetConnectionStatusEPNS0_10PlayReportEPKcS4_S4_", connection_status},
        {"_ZN2nn3nfp10InitializeEv", nfp_initialize}, {"_ZN2nn3nfp8FinalizeEv", nfp_finalize}, {"_ZN2nn3nfp8GetStateEv", nfp_get_state},
        {"_ZN2nn3nfp11ListDevicesEPNS0_12DeviceHandleEPii", nfp_list},
        {"_ZN2nn3nfp29AttachAvailabilityChangeEventEPNS_2os15SystemEventTypeE", nfp_attach_availability},
        {"_ZN2nn3nfp14GetDeviceStateERKNS0_12DeviceHandleE", nfp_device_state},
        {"_ZN2nn3nfp5MountERKNS0_12DeviceHandleENS0_9ModelTypeENS0_11MountTargetE", nfp_mount},
        {"_ZN2nn3nfp9GetNpadIdEPjRKNS0_12DeviceHandleE", nfp_get_npad_id},
        {"_ZN2nn3nfp19AttachActivateEventEPNS_2os15SystemEventTypeERKNS0_12DeviceHandleE", nfp_attach_activate},
        {"_ZN2nn3nfp21AttachDeactivateEventEPNS_2os15SystemEventTypeERKNS0_12DeviceHandleE", nfp_attach_deactivate},
        {"_ZN2nn3nfp14StartDetectionERKNS0_12DeviceHandleE", nfp_start_detection}, {"_ZN2nn3nfp13StopDetectionERKNS0_12DeviceHandleE", nfp_stop_detection},
        {"_ZN2nn3nfp10GetTagInfoEPNS0_7TagInfoERKNS0_12DeviceHandleE", nfp_get_tag_info},
        {"_ZN2nn3nfp12GetModelInfoEPNS0_9ModelInfoERKNS0_12DeviceHandleE", nfp_get_model_info},
        {"_ZN2nn3err9ShowErrorENS_6ResultE", show_error},
#ifdef GAME_BOTW
        {"_ZN2nn2oe31EnterExitRequestHandlingSectionEv", exit_section}, {"_ZN2nn2oe31LeaveExitRequestHandlingSectionEv", exit_section},
        {"_ZN2nn2oe15EnableRecordingEv", enable_recording},
        {"_ZN2nn3err40ShowUnacceptableAddOnContentVersionErrorEv", unacceptable_aoc_version},
        {"_ZN2nn2ec24ShowShopAddOnContentListENS_13ApplicationIdERKNS_7account10UserHandleE", show_aoc_shop},
        /* Breath of the Wild's seven extra nn::nfp imports (1.6.0 main, mangled names extracted from the image) */
        {"_ZN2nn3nfp5MountERKNS0_12DeviceHandleENS0_9ModelTypeE", nfp_mount_model},
        {"_ZN2nn3nfp7UnmountERKNS0_12DeviceHandleE", nfp_unmount},
        {"_ZN2nn3nfp13GetCommonInfoEPNS0_10CommonInfoERKNS0_12DeviceHandleE", nfp_get_common_info},
        {"_ZN2nn3nfp15GetRegisterInfoEPNS0_12RegisterInfoERKNS0_12DeviceHandleE", nfp_get_register_info},
        {"_ZN2nn3nfp19OpenApplicationAreaERKNS0_12DeviceHandleEj", nfp_open_application_area},
        {"_ZN2nn3nfp18GetApplicationAreaEPvPmRKNS0_12DeviceHandleEm", nfp_get_application_area},
        {"_ZN2nn3nfp13StartRestorerEPNS0_12DeviceHandleERKNS0_24AmiiboSettingsStartParamERKNS0_7TagInfoE", nfp_start_restorer},
#endif
    };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(name, map[i].name)) return map[i].fn;
    return NULL;
}
