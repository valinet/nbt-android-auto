/* nbt_carplay: wired CarPlay on the NBT, one iPhone session (started by native_service when an iPhone is plugged in;
 * docs/CARPLAY.md).
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Valentin Radu
 * (license and the section 7 permission for the head unit's libraries: NOTICE.md, "GPLv3 parts")
 *
 *   USB (cp_usb.c)        the iPhone's CarPlay configuration: multiplexor + NCM
 *   network (cp_ncm.c)    NCM <-> lwIP, IPv6 link-local (step a)
 *   lockdown, iAP2        pairing ("Trust", once), carkit service, identification, authentication with the car's
 *                         chip, CarPlayStartSession (cp_lockdown.c, cp_iap2.c)
 *   AirPlay (step b)      the iPhone connects to port 7000: pairing, auth-setup, streams (cp_airplay.c)
 *   screen (step c)       H.264 into nbt_decoder's FIFO (cp_screen.c)
 *   audio, microphone     the car's audio manager (cp_audio.c, cp_mic.c)
 *   keys                  nbt_knob's key lines -> knob / media HID, Siri (cp_input.c)
 *
 * Wireless (--wireless, docs/WIRELESS-BRIDGE.md): the bridge board (USB gadget 1d6b:0104) instead of the iPhone.
 * Its NCM function carries our lwIP link, bridged on the board to its Wi-Fi access point; the board's relay connects
 * to our port 7100, tells us its Bluetooth and Wi-Fi (text lines), gets what it publishes over mDNS for us, then
 * relays the iPhone's iAP2 RFCOMM bytes ("iap2 <address>" line, then raw). iAP2 runs over that (cp_iap2.c, Bluetooth
 * link), the iPhone joins the Wi-Fi and comes to the AirPlay receiver, and moves iAP2 into the AirPlay tunnel. One
 * phone session per run: it ends when the AirPlay session ends, when the relay ends before the iPhone came, or when
 * the screen stream is silent for 10 s (the iPhone went out of range: its connections just go quiet); native_service
 * starts the next run and the board reconnects when the iPhone is back.
 *
 * usage: nbt_carplay [--wireless] [--stop-after usb|iap2|net|airplay|video [--seconds N]] [--pcap FILE] [--dump FILE]
 *                    [--audio-dump DIR] [--no-audio] [--no-mic] [--fps N] [--knob-invert] [--state DIR]
 *                    [--fifo PATH|none] [--input PATH]
 *   --stop-after   end N seconds (default 15) after that step worked: usb = configuration and pipes, iap2 =
 *                  CarPlayStartSession sent, net = the iPhone connected to us over NCM, airplay = pairing and the
 *                  screen stream set up, video = 60 frames written
 * Environment (native_service): NBT_AA_MEDIA_OUT (default input_stereo_1), NBT_AA_PROMPT_OUT (prompts while another
 * source plays, e.g. input_stereo_3), NBT_AUX_SOURCE (our main source, default f6), NBT_KNOB_INVERT=1,
 * NBT_AA_BG_FOCUS=1 (no frames to nbt_decoder while the page is not on screen, cp_screen.c).
 * Exit status: 0 after a session, 1 when it could not start, 2 wrong arguments.
 */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "psa/crypto.h"
#include "cp.h"
#include "cp_lockdown.h"
#include "cp_usb.h"

struct cp_options g_opt;
volatile int g_stop;

static volatile long long g_reached_at;
static volatile int g_relay_up;   /* wireless: the board's relay is connected (an end of the AirPlay session ends us) */

void cp_reached(int step)
{
    if (g_opt.stop_after == step && !g_reached_at) {
        g_reached_at = ms_now();
        say("nbt_carplay: step reached; ending in %d s (--stop-after)", g_opt.seconds);
    }
}

static void* watch(void* arg)
{
    long long idle_since = 0;
    int seen = 0;
    (void)arg;
    while (!g_stop) {
        if (g_reached_at && ms_now() - g_reached_at >= g_opt.seconds * 1000LL) {
            say("nbt_carplay: --stop-after: done");
            g_stop = 1;
        }
        if (usb_gone())
            g_stop = 1;
        if (g_opt.wireless && g_relay_up && airplay_active() && screen_last_message() &&
            ms_now() - screen_last_message() > 10000) {   /* over Wi-Fi a vanished iPhone closes nothing */
            say("nbt_carplay: nothing from the iPhone for 10 s (out of range?): session over");
            g_stop = 1;
        }
        if (g_opt.wireless && g_relay_up) {   /* the session is over 5 s after the AirPlay session ended */
            if (airplay_active()) {
                seen = 1;
                idle_since = 0;
            } else if (seen && !idle_since)
                idle_since = ms_now();
            else if (seen && ms_now() - idle_since > 5000) {
                say("nbt_carplay: the AirPlay session is over");
                g_stop = 1;
            }
        }
        sleep_ms(200);
    }
    return 0;
}

static void stopped(int s)
{
    (void)s;
    g_stop = 1;
}

static struct usb g_usb;

static unsigned long long g_send_errors;

static int ncm_send(void* ctx, const unsigned char* block, unsigned n)
{
    unsigned actual = 0;
    const int rc = usb_xfer(&g_usb.ncm_out, (void*)block, n, 1000, &actual);
    (void)ctx;
    if (rc || actual != n) {
        if (++g_send_errors <= 5)
            say("ncm: send of %u bytes: result %d, status %08x, %u sent", n, rc, g_usb.ncm_out.status, actual);
        return -1;
    }
    return 0;
}

/* NCM in, with what the transfers end with (car 2026-11-23: no frame came from the iPhone; the reads may fail) */
static void* ncm_rx(void* arg)
{
    static unsigned char b[65536];
    unsigned long long reads = 0, empty = 0, bytes = 0, fin, fout;
    unsigned last_status = 0, logged = 0;
    long long next = ms_now() + 10000;
    (void)arg;
    while (!g_stop && !usb_gone()) {
        unsigned actual = 0;
        const int rc = usb_xfer(&g_usb.ncm_in, b, g_usb.ntb_in + 2048, 1000, &actual);
        if (actual) {
            if (!reads++)
                hexdump("ncm: first transfer in", b, actual < 48 ? actual : 48);
            bytes += actual;
            ncm_input(b, actual);
        } else {
            ++empty;
            if (g_usb.ncm_in.status != last_status && logged < 8) {
                ++logged;
                say("ncm: read ended without data: result %d, status %08x", rc, g_usb.ncm_in.status);
            }
            last_status = g_usb.ncm_in.status;
            if (rc && rc != ETIMEDOUT)
                sleep_ms(100);
        }
        if (ms_now() >= next) {
            next = ms_now() + 10000;
            ncm_counts(&fin, &fout);
            say("ncm: %llu transfers in (%llu bytes), %llu empty, frames %llu in / %llu out, %llu send errors", reads,
                bytes, empty, fin, fout, g_send_errors);
        }
    }
    return 0;
}

static void* ncm_notify(void* arg)
{
    unsigned char b[64];
    unsigned n = 0;
    (void)arg;
    while (!g_stop && !usb_gone()) {
        unsigned actual = 0;
        const int rc = usb_xfer(&g_usb.ncm_int, b, sizeof(b), 1000, &actual);
        if (actual && n++ < 10)
            hexdump("ncm: notification", b, actual);
        else if (!actual && rc && rc != ETIMEDOUT)
            sleep_ms(500);
    }
    return 0;
}

/* ---- wireless: the bridge board's relay ---- */

#define RELAY_PORT 7100

static struct {
    int s;
    unsigned char pre[4096];   /* read past the text lines */
    unsigned pren;
} g_relay = {-1, {0}, 0};

static int relay_write(void* ctx, const unsigned char* b, unsigned n)
{
    (void)ctx;
    return net_send(g_relay.s, b, (int)n) ? -1 : 0;
}

static int relay_read(void* ctx, unsigned char* b, unsigned n, unsigned ms)
{
    int r;
    (void)ctx;
    if (g_relay.pren) {
        r = (int)(g_relay.pren < n ? g_relay.pren : n);
        memcpy(b, g_relay.pre, (unsigned)r);
        memmove(g_relay.pre, g_relay.pre + r, g_relay.pren - (unsigned)r);
        g_relay.pren -= (unsigned)r;
        return r;
    }
    r = net_recv(g_relay.s, b, (int)n, (int)ms);
    return r == -2 ? 0 : r <= 0 ? -1 : r;
}

static const struct iap2_io RELAY_IO = {relay_write, relay_read, 0};

/* one text line from the relay, without its end; timeout_ms < 0: no limit. -1 = ended, too long, timed out */
static int relay_line(char* out, unsigned size, int timeout_ms)
{
    const long long until = ms_now() + timeout_ms;
    for (;;) {
        const unsigned char* nl = (const unsigned char*)memchr(g_relay.pre, '\n', g_relay.pren);
        int r;
        if (nl) {
            unsigned k = (unsigned)(nl - g_relay.pre);
            if (k >= size)
                return -1;
            memcpy(out, g_relay.pre, k);
            out[k] = 0;
            if (k && out[k - 1] == '\r')
                out[k - 1] = 0;
            memmove(g_relay.pre, nl + 1, g_relay.pren - k - 1);
            g_relay.pren -= k + 1;
            return 0;
        }
        if (g_relay.pren == sizeof(g_relay.pre) || g_stop || usb_gone() || (timeout_ms >= 0 && ms_now() > until))
            return -1;
        r = net_recv(g_relay.s, g_relay.pre + g_relay.pren, (int)(sizeof(g_relay.pre) - g_relay.pren), 1000);
        if (r == -2)
            continue;
        if (r <= 0)
            return -1;
        g_relay.pren += (unsigned)r;
    }
}

static int relay_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static int relay_printf(const char* fmt, ...)
{
    char b[512];
    va_list a;
    int n;
    va_start(a, fmt);
    n = vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    return n < 0 || n >= (int)sizeof(b) ? -1 : net_send(g_relay.s, b, n);
}

static int parse_mac(const char* s, unsigned char m[6])
{
    unsigned v[6], i;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return -1;
    for (i = 0; i < 6; ++i)
        m[i] = (unsigned char)v[i];
    return 0;
}

/* the relay's connection, its lines (bt, bssid, ssid, psk, channel, security, end), ours (what the board publishes
 * over mDNS: deviceid, features, pk, pi, srcvers, model, name, address, port, end), its "iap2" line, then iAP2 over
 * Bluetooth; afterwards (the iPhone moved to Wi-Fi) until the AirPlay session is over. 0 = an AirPlay session was. */
static int wireless(struct iap2_carplay* cp)
{
    static char ssid[64], psk[80], line[256];
    unsigned char bt[6], check[6];
    struct net_addr peer;
    char text[80];
    int ls, c = -1, have_bt = 0;
    long long until;
    unsigned short bound = 0;
    const unsigned long long f = airplay_features();
    ls = net_tcp_listen(RELAY_PORT, &bound);
    if (ls < 0) {
        say("wireless: port %u not available", RELAY_PORT);
        return 1;
    }
    say("wireless: waiting for the bridge board's relay on port %u", bound);
    while (!g_stop && !usb_gone()) {
        c = net_accept(ls, &peer, 1000);
        if (c >= 0)
            break;
        if (c == -1)
            sleep_ms(500);
    }
    net_close(ls);
    if (c < 0)
        return 1;
    g_relay.s = c;
    say("wireless: relay from %s", net_text(&peer, text, sizeof(text)));
    if (relay_line(line, sizeof(line), 10000) || strncmp(line, "nbt-bridge 1", 12)) {
        say("wireless: not the relay");
        return 1;
    }
    cp->channel = 36;
    cp->security = 2;
    while (!relay_line(line, sizeof(line), 10000) && strcmp(line, "end")) {
        const char* v = strchr(line, ' ');
        if (!v)
            continue;
        ++v;
        if (!strncmp(line, "bt ", 3) && !parse_mac(v, bt)) {
            have_bt = 1;
            snprintf(g_opt.bt_id, sizeof(g_opt.bt_id), "%s", v);
        } else if (!strncmp(line, "bssid ", 6) && !parse_mac(v, check))
            snprintf(g_opt.device_id, sizeof(g_opt.device_id), "%s", v);
        else if (!strncmp(line, "ssid ", 5))
            snprintf(ssid, sizeof(ssid), "%s", v);
        else if (!strncmp(line, "psk ", 4))
            snprintf(psk, sizeof(psk), "%s", v);
        else if (!strncmp(line, "channel ", 8))
            cp->channel = (unsigned)atoi(v);
        else if (!strncmp(line, "security ", 9))
            cp->security = (unsigned)atoi(v);
    }
    if (strcmp(line, "end") || !have_bt || !ssid[0] || !g_opt.device_id[0]) {
        say("wireless: the relay's lines are incomplete");
        return 1;
    }
    memcpy(cp->bt_mac, bt, 6);
    cp->wireless = 1;
    cp->ssid = ssid;
    cp->passphrase = psk;
    cp->mac = g_opt.device_id;
    airplay_wireless(cp);
    say("wireless: board Bluetooth %s, Wi-Fi \"%s\" (%s), channel %u", g_opt.bt_id, ssid, g_opt.device_id,
        cp->channel);
    if (relay_printf("deviceid %s\nfeatures 0x%x,0x%x\npk %s\npi %s\nsrcvers %s\nmodel NBT\nname BMW NBT\n"
                     "address %s\nport %u\nend\n",
                     g_opt.device_id, (unsigned)(f & 0xffffffffu), (unsigned)(f >> 32), cp->public_key,
                     airplay_pairing_id(), cp->source_version, cp->address, cp->port))
        return 1;
    g_relay_up = 1;
    do {
        if (relay_line(line, sizeof(line), -1)) {
            say("wireless: the relay ended before an iPhone came");
            return 1;
        }
    } while (strncmp(line, "iap2", 4));
    say("wireless: iPhone %s over Bluetooth", line + 4);
    if (iap2_run(&RELAY_IO, cp, IAP2_BLUETOOTH) && !airplay_sessions())
        return 1;
    until = ms_now() + 30000;   /* the iPhone on its way over Wi-Fi */
    while (!g_stop && !usb_gone() && !airplay_sessions() && ms_now() < until)
        sleep_ms(200);
    if (!airplay_sessions()) {
        say("wireless: the iPhone did not come over Wi-Fi");
        return 1;
    }
    while (!g_stop && !usb_gone())   /* watch() ends us after the AirPlay session */
        sleep_ms(200);
    return 0;
}

static int carkit_write(void* ctx, const unsigned char* b, unsigned n)
{
    return stream_write((struct stream*)ctx, b, n);
}

static int carkit_read(void* ctx, unsigned char* b, unsigned n, unsigned ms)
{
    return stream_read_some((struct stream*)ctx, b, n, ms);
}

static int usage(void)
{
    fprintf(stderr, "usage: nbt_carplay [--wireless] [--stop-after usb|iap2|net|airplay|video [--seconds N]] [--pcap FILE] "
                    "[--dump FILE] [--audio-dump DIR] [--no-audio] [--no-mic] [--fps N] [--knob-invert] "
                    "[--state DIR] [--fifo PATH|none] [--input PATH]\n");
    return 2;
}

int main(int argc, char** argv)
{
    static const char* steps[] = {"", "usb", "iap2", "net", "airplay", "video"};
    struct iap2_carplay cp;
    struct stream* carkit;
    char mac[20];
    pthread_t t;
    int i, r;
    setvbuf(stdout, 0, _IOLBF, 0);
    g_opt.width = 1280;
    g_opt.height = 480;
    g_opt.fps = 60;
    g_opt.width_mm = 243;   /* 10.25 inch, 8:3 */
    g_opt.height_mm = 91;
    g_opt.seconds = 15;
    g_opt.state_dir = "/var/opt/hmi/data/rw/NbtAndroidAutoRT/carplay";
    g_opt.fifo = "/fs/ram3/nbt-aa-native/annexb.fifo";
    g_opt.input = "/fs/ram3/nbt-aa-native/input.fifo";
    g_opt.audio = g_opt.mic = 1;
    g_opt.media_out = getenv("NBT_AA_MEDIA_OUT") ? getenv("NBT_AA_MEDIA_OUT") : "input_stereo_1";
    g_opt.prompt_out = getenv("NBT_AA_PROMPT_OUT");
    g_opt.aux_source = getenv("NBT_AUX_SOURCE") ? (unsigned)strtoul(getenv("NBT_AUX_SOURCE"), 0, 16) : 0xf6;
    g_opt.knob_invert = getenv("NBT_KNOB_INVERT") && !strcmp(getenv("NBT_KNOB_INVERT"), "1");
    for (i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : 0;
        if (!strcmp(a, "--stop-after") && v) {
            unsigned k;
            for (k = 1; k < sizeof(steps) / sizeof(steps[0]) && strcmp(v, steps[k]); ++k) {
            }
            if (k == sizeof(steps) / sizeof(steps[0]))
                return usage();
            g_opt.stop_after = (int)k;
            ++i;
        } else if (!strcmp(a, "--seconds") && v) {
            g_opt.seconds = atoi(v);
            ++i;
        } else if (!strcmp(a, "--pcap") && v) {
            g_opt.pcap = v;
            ++i;
        } else if (!strcmp(a, "--dump") && v) {
            g_opt.dump = v;
            ++i;
        } else if (!strcmp(a, "--audio-dump") && v) {
            g_opt.audio_dump = v;
            ++i;
        } else if (!strcmp(a, "--state") && v) {
            g_opt.state_dir = v;
            ++i;
        } else if (!strcmp(a, "--fifo") && v) {
            g_opt.fifo = strcmp(v, "none") ? v : 0;
            ++i;
        } else if (!strcmp(a, "--input") && v) {
            g_opt.input = v;
            ++i;
        } else if (!strcmp(a, "--fps") && v) {
            g_opt.fps = atoi(v);
            ++i;
        } else if (!strcmp(a, "--no-audio"))
            g_opt.audio = g_opt.mic = 0;
        else if (!strcmp(a, "--no-mic"))
            g_opt.mic = 0;
        else if (!strcmp(a, "--knob-invert"))
            g_opt.knob_invert = 1;
        else if (!strcmp(a, "--wireless"))
            g_opt.wireless = 1;
        else
            return usage();
    }
    {
        const char* bg = getenv("NBT_AA_BG_FOCUS");
        g_opt.pause_hidden = bg && !strcmp(bg, "1");
    }
    if (g_opt.fps < 15 || g_opt.fps > 60 || g_opt.seconds < 0)
        return usage();
    signal(SIGTERM, stopped);
    signal(SIGINT, stopped);
    signal(SIGPIPE, SIG_IGN);
    say("nbt_carplay: %dx%d at %d fps, state in %s%s%s%s", g_opt.width, g_opt.height, g_opt.fps, g_opt.state_dir,
        g_opt.wireless ? ", wireless" : "", g_opt.stop_after ? ", stop after " : "",
        g_opt.stop_after ? steps[g_opt.stop_after] : "");
    if (cp_random_init() || psa_crypto_init() != PSA_SUCCESS) {
        say("nbt_carplay: no random numbers");
        return 1;
    }
    if (acp_open()) {
        say("nbt_carplay: the authentication chip does not answer");
        return 1;
    }
    airplay_identity();
    if (pthread_create(&t, 0, watch, 0))
        return 1;
    if (usb_open(&g_usb, 60))
        return 1;
    if (g_opt.pcap)
        ncm_pcap(g_opt.pcap);
    if (net_init() || ncm_start(g_usb.mac, ncm_send, 0) || pthread_create(&t, 0, ncm_rx, 0))
        return 1;
    if (g_usb.have_int)
        pthread_create(&t, 0, ncm_notify, 0);
    cp_reached(STOP_USB);
    if (airplay_start(AIRPLAY_PORT))
        return 1;
    if (g_opt.audio)
        audio_start();
    input_start();
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", g_usb.mac[0], g_usb.mac[1], g_usb.mac[2],
             g_usb.mac[3], g_usb.mac[4], g_usb.mac[5]);
    memset(&cp, 0, sizeof(cp));
    cp.address = ncm_address();
    cp.port = AIRPLAY_PORT;
    cp.mac = mac;
    cp.public_key = airplay_public_hex();
    cp.source_version = SOURCE_VERSION;
    cp.ncm_interface = g_usb.ncm_ctrl;
    {   /* the NBT's own Bluetooth address, "aa:bb:cc:dd:ee:ff" (see send_identification in cp_iap2.c) */
        char text[40];
        const int n = state_read("car-bt", text, sizeof(text) - 1);
        if (n > 0) {
            text[n] = 0;
            cp.have_car_bt = !parse_mac(text, cp.car_bt_mac);
            say("nbt_carplay: the NBT's Bluetooth %s%s", text, cp.have_car_bt ? "" : " (not an address, not used)");
        }
    }
    r = -1;
    if (g_opt.wireless) {
        r = g_stop ? 1 : wireless(&cp);
        g_stop = 1;
        net_close(g_relay.s);
    } else if (!g_stop && !carkit_connect(&g_usb, &carkit)) {
        struct iap2_io io = {carkit_write, carkit_read, 0};
        io.ctx = carkit;
        r = iap2_run(&io, &cp, IAP2_WIRED);
    }
    g_stop = 1;
    carkit_close();
    audio_stop();
    usb_close(&g_usb);
    say("nbt_carplay: over");
    return r ? 1 : 0;
}
