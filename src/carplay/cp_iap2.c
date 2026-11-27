/* nbt_carplay: iAP2: identification, authentication with the car's chip, CarPlay start.
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Valentin Radu
 *
 * As nbt_carplay_probe (car 2026-11-23: identification accepted, authentication succeeded with the 2.0B chip,
 * CarPlayAvailability came), then as xcertplay's wired control client: PowerSourceUpdate, the subscriptions,
 * StartHID (media playback remote), and CarPlayStartSession (4301: our IPv6 address on the NCM link, the AirPlay port,
 * our MAC, our Ed25519 key, the source version) for each CarPlayAvailability (4300). The iPhone then connects to the
 * AirPlay receiver (cp_airplay.c). NowPlayingUpdate (5001) gives the playback state for nbt_knob
 * (/fs/ram3/nbt-aa-media: 2 playing, 3 paused).
 * Link layer: marker FF 55 02 00 EE 10 and a SYN from us; packets FF 5A, length, control (80 SYN, 40 ACK, 10 RST),
 * sequence, acknowledgement, session, header checksum, payload, payload checksum. Session 10 carries the control
 * messages: 40 40, length, id, parameters (length, id, data; groups nest).
 *
 * Three kinds of link (one iap2_run each, several may run at once):
 *   wired      the carkit service; we send marker and SYN, no acknowledgements (retransmission timeout 0)
 *   Bluetooth  RFCOMM through the bridge board (docs/WIRELESS-BRIDGE.md), as xcertplay's wireless client: our marker
 *              until the iPhone answers with its own, then our SYN; acknowledgements on (4 s retransmission, 500 ms
 *              acknowledgement timeout, 4 outstanding): we acknowledge every packet and send no more than the
 *              iPhone's window. Identification with the Bluetooth (17) and wireless CarPlay (24) transports instead of
 *              USB, no power; the iPhone asks for the Wi-Fi (5702) and gets the board's access point (5703, again
 *              after DeviceTransportIdentifierNotification 4e0e); CarPlayStartSession carries the Wi-Fi group (1)
 *   tunnel     the AirPlay stream 130 (cp_airplay.c) after the iPhone came over Wi-Fi: as wired (marker and SYN from
 *              us, no acknowledgements), with the wireless identification
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cp.h"

enum { CONTROL = 10, QUEUE = 64 };

/* the route guidance display's component identifier: 1 as car-tested wired (F07, iOS 27.0.1); wireless 2, as 1 is the
 * wireless CarPlay transport there */
#define ROUTE_GUIDANCE_ID(cp) ((cp)->wireless ? 2u : 1u)
/* the NBT's own Bluetooth as one more component (after the route guidance display's identifier) */
#define CAR_BT_ID(cp) ((cp)->wireless ? 3u : 2u)
/* the iPhone rejected the identification with it: left out from then on (this program's run) */
static int g_no_car_bt;

struct link {
    const struct iap2_io* io;
    const struct iap2_carplay* cp;
    int mode;
    const char* name;
    unsigned char in[140000];
    unsigned inn;
    unsigned sent_seq, last_rx, synced, peer_maxlen, peer_acks, peer_window, normal;
    unsigned wire_seq, acked, marker_seen, syn_sent;
    long long syn_at, marker_at;
    unsigned char csm[140000];
    unsigned csmn;
    unsigned char frame[70000], msg[70000];
    unsigned char* queue[QUEUE];   /* Bluetooth: whole packets waiting for the iPhone's window */
    unsigned queue_n[QUEUE], queued;
    int identified, authenticated, failed, started, carplay_sent, wifi_sent, wifi_after;
};

static pthread_mutex_t g_chip = PTHREAD_MUTEX_INITIALIZER;   /* one chip, several links */

static unsigned char csum(const unsigned char* b, unsigned n)
{
    unsigned s = 0, i;
    for (i = 0; i < n; ++i)
        s += b[i];
    return (unsigned char)(0x100 - (s & 0xff));
}

static int outstanding(const struct link* l)
{
    return (int)((l->wire_seq - l->acked) & 0xff);
}

static int wire(struct link* l, const unsigned char* f, unsigned n)
{
    return l->io->write(l->io->ctx, f, n);
}

/* the queued packets the iPhone's window has room for */
static int flush(struct link* l)
{
    unsigned k = 0;
    int r = 0;
    while (k < l->queued && outstanding(l) < (int)l->peer_window) {
        r |= wire(l, l->queue[k], l->queue_n[k]);
        l->wire_seq = l->queue[k][5];
        free(l->queue[k]);
        ++k;
    }
    memmove(l->queue, l->queue + k, (l->queued - k) * sizeof(l->queue[0]));
    memmove(l->queue_n, l->queue_n + k, (l->queued - k) * sizeof(l->queue_n[0]));
    l->queued -= k;
    return r;
}

static int link_packet(struct link* l, unsigned control, unsigned seq, unsigned session, const unsigned char* p,
                       unsigned n)
{
    unsigned char* f = l->frame;
    const unsigned len = 9 + (n ? n + 1 : 0);
    if (len > sizeof(l->frame))
        return -1;
    f[0] = 0xff;
    f[1] = 0x5a;
    put16(f + 2, len);
    f[4] = (unsigned char)control;
    f[5] = (unsigned char)seq;
    f[6] = (unsigned char)l->last_rx;
    f[7] = (unsigned char)session;
    f[8] = csum(f, 8);
    if (n) {
        memcpy(f + 9, p, n);
        f[9 + n] = csum(p, n);
    }
    if (l->mode == IAP2_BLUETOOTH && n && control == 0x40) {   /* data: within the iPhone's window */
        unsigned char* c;
        if (l->queued == QUEUE || !(c = (unsigned char*)malloc(len))) {
            say("%s: send queue full", l->name);
            return -1;
        }
        memcpy(c, f, len);
        l->queue[l->queued] = c;
        l->queue_n[l->queued++] = len;
        return flush(l);
    }
    return wire(l, f, len);
}

static int csm_send(struct link* l, unsigned id, const unsigned char* params, unsigned n)
{
    unsigned char* m = l->msg;
    unsigned off = 0;
    const unsigned chunk = l->peer_maxlen > 10 ? l->peer_maxlen - 10 : 1000;
    if (n + 6 > sizeof(l->msg))
        return -1;
    m[0] = 0x40;
    m[1] = 0x40;
    put16(m + 2, n + 6);
    put16(m + 4, id);
    memcpy(m + 6, params, n);
    say("%s: -> %04x (%u bytes)", l->name, id, n + 6);
    while (off < n + 6) {
        const unsigned k = n + 6 - off < chunk ? n + 6 - off : chunk;
        l->sent_seq = (l->sent_seq + 1) & 0xff;
        if (link_packet(l, 0x40, l->sent_seq, CONTROL, m + off, k))
            return -1;
        off += k;
    }
    return 0;
}

/* ---- parameters ---- */

static unsigned prm(unsigned char* b, unsigned at, unsigned id, const void* data, unsigned n)
{
    put16(b + at, n + 4);
    put16(b + at + 2, id);
    if (n)
        memcpy(b + at + 4, data, n);
    return at + 4 + n;
}

static unsigned prm_str(unsigned char* b, unsigned at, unsigned id, const char* s)
{
    return prm(b, at, id, s, (unsigned)strlen(s) + 1);
}

static unsigned prm_u8(unsigned char* b, unsigned at, unsigned id, unsigned v)
{
    const unsigned char x = (unsigned char)v;
    return prm(b, at, id, &x, 1);
}

static unsigned prm_u16(unsigned char* b, unsigned at, unsigned id, unsigned v)
{
    unsigned char x[2];
    put16(x, v);
    return prm(b, at, id, x, 2);
}

static unsigned prm_u32(unsigned char* b, unsigned at, unsigned id, unsigned v)
{
    unsigned char x[4];
    put32(x, v);
    return prm(b, at, id, x, 4);
}

static unsigned prm_u16s(unsigned char* b, unsigned at, unsigned id, const unsigned short* v, unsigned count)
{
    unsigned char x[128];
    unsigned i;
    for (i = 0; i < count; ++i)
        put16(x + 2 * i, v[i]);
    return prm(b, at, id, x, 2 * count);
}

static const unsigned char* prm_find(const unsigned char* p, unsigned n, unsigned id, unsigned* len)
{
    unsigned at = 0;
    while (at + 4 <= n) {
        const unsigned l = get16(p + at), i = get16(p + at + 2);
        if (l < 4 || at + l > n)
            return 0;
        if (i == id) {
            *len = l - 4;
            return p + at + 4;
        }
        at += l;
    }
    return 0;
}

/* IdentificationInformation (1D01) as the probe's (accepted on the car): identity strings, the messages we send and
 * take, power, an external accessory protocol, English, the USB host transport (the NCM control interface) and a
 * media playback HID remote. Wireless (xcertplay's wireless identification): no PowerSourceUpdate but the Wi-Fi
 * configuration (5703) sent, the wireless messages (4e0d, 4e0e, 5702) taken, no power, the Bluetooth transport
 * (the board's address) and the wireless CarPlay transport (the access point's name) */
static int send_identification(struct link* l)
{
    static const unsigned short sent[] = {0x5000, 0x5002, 0x5200, 0x5203, 0xae00, 0xae02, 0x4157, 0x4159,
                                          0x4154, 0x4156, 0xae03, 0x4301, 0x6800, 0x6802, 0x6803};
    static const unsigned short sent_wireless[] = {0x5000, 0x5002, 0x5200, 0x5203, 0xae00, 0xae02, 0x4157, 0x4159,
                                                   0x4154, 0x4156, 0x4301, 0x6800, 0x6802, 0x6803, 0x5703};
    static const unsigned short taken[] = {0xea00, 0xea01, 0x5001, 0x5201, 0x5202, 0xae01, 0x4158, 0x4155,
                                           0x4300, 0x6801, 0x4e0d, 0x4e0e, 0x5702};
    const struct iap2_carplay* cp = l->cp;
    const int wl = cp->wireless;
    unsigned char b[2048], g[256];
    unsigned at = 0, gn;
    at = prm_str(b, at, 0, "BMW NBT");
    at = prm_str(b, at, 1, "NBT");
    at = prm_str(b, at, 2, "BMW");
    at = prm_str(b, at, 3, "nbt-android-auto");
    at = prm_str(b, at, 4, "1.0");
    at = prm_str(b, at, 5, "1.0");
    if (wl)
        at = prm_u16s(b, at, 6, sent_wireless, sizeof(sent_wireless) / sizeof(sent_wireless[0]));
    else
        at = prm_u16s(b, at, 6, sent, sizeof(sent) / sizeof(sent[0]));
    at = prm_u16s(b, at, 7, taken, sizeof(taken) / sizeof(taken[0]) - (wl ? 0 : 3));
    at = prm_u8(b, at, 8, wl ? 0 : 2);
    at = prm_u16(b, at, 9, 20);
    gn = prm_u8(g, 0, 0, 1);
    gn = prm_str(g, gn, 1, "com.nbtaa.carplay");
    gn = prm_u8(g, gn, 2, 0);
    at = prm(b, at, 10, g, gn);
    at = prm_str(b, at, 12, "en");
    at = prm_str(b, at, 13, "en");
    if (!wl) {
        gn = prm_u16(g, 0, 0, 0);
        gn = prm_str(g, gn, 1, "USBHostTransport");
        gn = prm(g, gn, 2, 0, 0);
        gn = prm_u8(g, gn, 3, cp->ncm_interface);
        gn = prm(g, gn, 4, 0, 0);
        at = prm(b, at, 16, g, gn);
    } else {
        gn = prm_u16(g, 0, 0, 0);
        gn = prm_str(g, gn, 1, "blue");
        gn = prm(g, gn, 2, 0, 0);
        gn = prm(g, gn, 3, cp->bt_mac, 6);
        gn = prm_str(g, gn, 4, "blue");
        gn = prm(g, gn, 5, 0, 0);
        at = prm(b, at, 17, g, gn);
    }
    gn = prm_u16(g, 0, 0, 0);
    gn = prm_str(g, gn, 1, "Media Playback Remote");
    gn = prm_u8(g, gn, 2, 1);
    at = prm(b, at, 18, g, gn);
    /* A route guidance display (iOS 27.0.1, F07, 2026-11-24): without one in the identification the iPhone accepts
     * 5200 but never sends 5201 / 5202. Group 30: 0 identifier, 1 name, 2..5 the longest road / destination /
     * road-after / description text, 6 how many manoeuvres it keeps. */
    gn = prm_u16(g, 0, 0, ROUTE_GUIDANCE_ID(cp));
    gn = prm_str(g, gn, 1, "Instrument Cluster");
    gn = prm_u16(g, gn, 2, 64);
    gn = prm_u16(g, gn, 3, 64);
    gn = prm_u16(g, gn, 4, 64);
    gn = prm_u16(g, gn, 5, 64);
    gn = prm_u16(g, gn, 6, 8);
    at = prm(b, at, 30, g, gn);
    /* The NBT's own Bluetooth (user 2026-11-27: after a call over it the iPhone kept playing music to it, while the car
     * listened to CarPlay): a Bluetooth transport component with its address, without "supports iAP2" (iAP2 does not
     * run over it). A factory CarPlay car announces its Bluetooth this way, as far as known here, and the iPhone then
     * treats that Bluetooth device and the CarPlay car as one. Not confirmed on this car (2026-11-27). Only with the
     * address in the state directory's file car-bt; rejected = sent again without it (0x1d03). */
    if (cp->have_car_bt && !g_no_car_bt) {
        gn = prm_u16(g, 0, 0, CAR_BT_ID(cp));
        gn = prm_str(g, gn, 1, "BMW NBT Bluetooth");
        gn = prm(g, gn, 3, cp->car_bt_mac, 6);
        at = prm(b, at, 17, g, gn);
    }
    if (wl) {
        gn = prm_u16(g, 0, 0, 1);
        gn = prm_str(g, gn, 1, cp->ssid);
        gn = prm(g, gn, 2, 0, 0);
        gn = prm_u16(g, gn, 3, 1);
        gn = prm(g, gn, 4, 0, 0);
        gn = prm(g, gn, 5, 0, 0);
        at = prm(b, at, 24, g, gn);
    }
    return csm_send(l, 0x1d01, b, at);
}

static const unsigned char MEDIA_REMOTE[] = {0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
                                             0x95, 0x04, 0x09, 0xb0, 0x09, 0xb1, 0x09, 0xb5, 0x09, 0xb6, 0x81, 0x02,
                                             0x75, 0x04, 0x95, 0x01, 0x81, 0x03, 0xc0};

/* after authentication: power (wired), subscriptions, the HID media remote (as xcertplay's control clients) */
static void start_control(struct link* l)
{
    unsigned char b[512], g[64];
    unsigned at, gn;
    if (!l->cp->wireless) {
        at = prm_u16(b, 0, 0, 1000);   /* 1000 mA for the iPhone */
        at = prm_u8(b, at, 1, 1);      /* charging */
        csm_send(l, 0xae03, b, at);
    }
    gn = prm(g, 0, 1, 0, 0);       /* NowPlaying: title, duration, album, artist, artwork id */
    gn = prm(g, gn, 4, 0, 0);
    gn = prm(g, gn, 6, 0, 0);
    gn = prm(g, gn, 12, 0, 0);
    gn = prm(g, gn, 26, 0, 0);
    at = prm(b, 0, 0, g, gn);
    gn = prm(g, 0, 0, 0, 0);       /* playback: status, elapsed, queue index, queue count, app name */
    gn = prm(g, gn, 1, 0, 0);
    gn = prm(g, gn, 2, 0, 0);
    gn = prm(g, gn, 3, 0, 0);
    gn = prm(g, gn, 7, 0, 0);
    at = prm(b, at, 1, g, gn);
    csm_send(l, 0x5000, b, at);
    at = prm_u16(b, 0, 0, ROUTE_GUIDANCE_ID(l->cp));   /* route guidance for the display named in the identification */
    csm_send(l, 0x5200, b, at);
    at = prm(b, 0, 4, 0, 0);
    at = prm(b, at, 5, 0, 0);
    at = prm(b, at, 6, 0, 0);
    csm_send(l, 0xae00, b, at);
    at = prm(b, 0, 0, 0, 0);
    at = prm(b, at, 4, 0, 0);
    at = prm(b, at, 5, 0, 0);
    csm_send(l, 0x4157, b, at);
    at = prm(b, 0, 0, 0, 0);
    at = prm(b, at, 1, 0, 0);
    at = prm(b, at, 2, 0, 0);
    at = prm(b, at, 3, 0, 0);
    at = prm(b, at, 4, 0, 0);
    at = prm(b, at, 11, 0, 0);
    csm_send(l, 0x4154, b, at);
    at = prm_u16(b, 0, 0, 0);
    at = prm_u16(b, at, 1, 2);
    at = prm_u16(b, at, 2, 1);
    at = prm(b, at, 4, MEDIA_REMOTE, sizeof(MEDIA_REMOTE));
    csm_send(l, 0x6800, b, at);
    l->started = 1;
}

/* AccessoryWiFiConfigurationInformation (5703): SSID, passphrase, security, channel (no BSSID) */
static void wifi_configuration(struct link* l)
{
    const struct iap2_carplay* cp = l->cp;
    unsigned char b[256];
    unsigned at = prm_str(b, 0, 1, cp->ssid);
    at = prm_str(b, at, 2, cp->passphrase);
    at = prm_u8(b, at, 3, cp->security);
    at = prm_u8(b, at, 4, cp->channel);
    say("%s: Wi-Fi \"%s\", channel %u, security %u", l->name, cp->ssid, cp->channel, cp->security);
    csm_send(l, 0x5703, b, at);
}

static void carplay_start(struct link* l)
{
    unsigned char b[512], g[256];
    unsigned at, gn;
    const struct iap2_carplay* cp = l->cp;
    if (cp->wireless) {
        gn = prm_str(g, 0, 0, cp->ssid);
        gn = prm_str(g, gn, 1, cp->passphrase);
        gn = prm_u8(g, gn, 2, cp->channel);
        gn = prm_str(g, gn, 3, cp->address);
        gn = prm_u8(g, gn, 4, cp->security);
        at = prm(b, 0, 1, g, gn);
    } else {
        gn = prm_str(g, 0, 0, cp->address);
        at = prm(b, 0, 0, g, gn);
    }
    at = prm_u32(b, at, 2, cp->port);
    at = prm_str(b, at, 3, cp->mac);
    at = prm_str(b, at, 4, cp->public_key);
    at = prm_str(b, at, 5, cp->source_version);
    say("%s: CarPlayStartSession: [%s]:%u, %s%s", l->name, cp->address, cp->port, cp->mac,
        cp->wireless ? ", over Wi-Fi" : "");
    csm_send(l, 0x4301, b, at);
    l->carplay_sent++;
    cp_reached(STOP_IAP2);
}

/* NowPlayingUpdate: the media item (0: title 1, ...) and the playback (1: status 0, elapsed ms 1, queue index 2).
 * A new track (another title or queue index, or an elapsed time near 0) ends the quiet after next / previous */
static void now_playing(struct link* l, const unsigned char* p, unsigned n)
{
    static char title[128];
    static unsigned index = ~0u;
    unsigned len, l2;
    const unsigned char* g = prm_find(p, n, 1, &len);
    const unsigned char* item = prm_find(p, n, 0, &l2);
    const unsigned char *st, *v;
    int track = 0;
    if (item && (v = prm_find(item, l2, 1, &l2)) != 0 && l2 && strncmp(title, (const char*)v, sizeof(title) - 1)) {
        snprintf(title, sizeof(title), "%.*s", (int)l2, (const char*)v);
        track = 1;
    }
    if (g && (v = prm_find(g, len, 2, &l2)) != 0 && l2 == 4 && get32(v) != index) {
        index = get32(v);
        track = 1;
    }
    if (g && (v = prm_find(g, len, 1, &l2)) != 0 && l2 == 4 && get32(v) < 1500)
        track = 1;
    if (track)
        audio_track_changed();
    if (!g || !(st = prm_find(g, len, 0, &l2)) || l2 < 1)
        return;
    say("%s: playback state %u", l->name, st[0]);
    audio_set_playing(st[0] == 1);
    flag_set("/fs/ram3/nbt-aa-media", st[0] == 1 ? "2\n" : "3\n");
}

static void on_message(struct link* l, unsigned id, const unsigned char* p, unsigned n)
{
    unsigned char out[1400], sig[128];
    unsigned at, len, sl;
    const unsigned char* v;
    if (id != 0x5001 && id != 0xae01)
        say("%s: <- %04x (%u bytes)", l->name, id, n + 6);
    switch (id) {
    case 0x1d00:
        send_identification(l);
        break;
    case 0x1d02:
        say("%s: identification accepted", l->name);
        l->identified = 1;
        break;
    case 0x1d03: {
        unsigned a = 0;
        say("%s: identification REJECTED", l->name);
        while (a + 4 <= n) {
            const unsigned k = get16(p + a);
            say("%s:   parameter %u", l->name, get16(p + a + 2));
            if (k < 4)
                break;
            a += k;
        }
        if (l->cp->have_car_bt && !g_no_car_bt) {   /* the NBT's Bluetooth component: once more without it */
            g_no_car_bt = 1;
            say("%s: identification again, without the NBT's Bluetooth", l->name);
            send_identification(l);
            break;
        }
        l->failed = 1;
        break;
    }
    case 0xaa00: {
        const unsigned char* cert;
        unsigned cn;
        int r;
        pthread_mutex_lock(&g_chip);
        r = acp_cert(&cert, &cn);
        pthread_mutex_unlock(&g_chip);
        if (r) {
            l->failed = 1;
            break;
        }
        at = prm(out, 0, 0, cert, cn);
        csm_send(l, 0xaa01, out, at);
        break;
    }
    case 0xaa02: {
        int r = -1;
        v = prm_find(p, n, 0, &len);
        if (v) {
            pthread_mutex_lock(&g_chip);
            r = acp_sign(v, len, sig, &sl);
            pthread_mutex_unlock(&g_chip);
        }
        if (r) {
            l->failed = 1;
            break;
        }
        at = prm(out, 0, 0, sig, sl);
        csm_send(l, 0xaa03, out, at);
        break;
    }
    case 0xaa04:
        say("%s: authentication FAILED", l->name);
        l->failed = 1;
        break;
    case 0xaa05:
        say("%s: authentication succeeded", l->name);
        l->authenticated = 1;
        if (!l->started)
            start_control(l);
        break;
    case 0x4300: {
        unsigned l2;
        const unsigned char* a;
        if ((v = prm_find(p, n, 0, &len)) != 0 && (a = prm_find(v, len, 0, &l2)) != 0 && l2)
            say("%s: CarPlay available over USB: %s", l->name, a[0] ? "yes" : "no");
        if ((v = prm_find(p, n, 1, &len)) != 0 && (a = prm_find(v, len, 0, &l2)) != 0 && l2)
            say("%s: CarPlay available over Wi-Fi: %s", l->name, a[0] ? "yes" : "no");
        carplay_start(l);
        break;
    }
    case 0x5702:   /* RequestAccessoryWiFiConfigurationInformation */
        if (!l->cp->wireless || l->wifi_sent >= 5)
            break;
        ++l->wifi_sent;
        wifi_configuration(l);
        break;
    case 0x4e0d:
        v = prm_find(p, n, 0, &len);
        say("%s: wireless CarPlay %savailable", l->name, v && len && v[0] ? "" : "not ");
        break;
    case 0x4e0e:   /* DeviceTransportIdentifierNotification: the Wi-Fi once more, as xcertplay */
        if (!l->cp->wireless || l->wifi_after >= 2)
            break;
        ++l->wifi_after;
        wifi_configuration(l);
        break;
    case 0x5001:
        now_playing(l, p, n);
        break;
    default:
        break;
    }
}

static void csm_in(struct link* l, const unsigned char* b, unsigned n)
{
    if (n > sizeof(l->csm) - l->csmn)
        l->csmn = 0;
    memcpy(l->csm + l->csmn, b, n);
    l->csmn += n;
    for (;;) {
        unsigned skip = 0, len;
        while (skip + 1 < l->csmn && !(l->csm[skip] == 0x40 && l->csm[skip + 1] == 0x40))
            ++skip;
        if (skip) {
            memmove(l->csm, l->csm + skip, l->csmn - skip);
            l->csmn -= skip;
        }
        if (l->csmn < 6)
            return;
        len = get16(l->csm + 2);
        if (len < 6) {
            memmove(l->csm, l->csm + 1, --l->csmn);
            continue;
        }
        if (l->csmn < len)
            return;
        on_message(l, get16(l->csm + 4), l->csm + 6, len - 6);
        memmove(l->csm, l->csm + len, l->csmn - len);
        l->csmn -= len;
    }
}

/* our SYN: version 1, 4 outstanding, 65535 bytes, retransmission and acknowledgement timeouts, retransmissions,
 * cumulative acknowledgements, then the sessions: control (10, v2), external accessory (11), file transfer (12, v2).
 * Wired and tunnel: no acknowledgements; Bluetooth: 4000 ms, 500 ms, 4, 3 (xcertplay) */
static int send_syn(struct link* l)
{
    static const unsigned char wired[19] = {1, 4, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 10, 0, 2, 11, 2, 1, 12, 1, 2};
    static const unsigned char bt[19] = {1, 4, 0xff, 0xff, 0x0f, 0xa0, 0x01, 0xf4, 4, 3, 10, 0, 2, 11, 2, 1, 12, 1, 2};
    l->syn_sent++;
    l->syn_at = ms_now();
    return link_packet(l, 0x80, l->sent_seq, 0, l->mode == IAP2_BLUETOOTH ? bt : wired, 19);
}

static void link_in(struct link* l)
{
    for (;;) {
        unsigned skip = 0, len, control, seq, session, pn;
        const unsigned char* pl;
        while (skip + 1 < l->inn && !(l->in[skip] == 0xff && l->in[skip + 1] == 0x5a)) {
            if (l->in[skip] == 0xff && skip + 6 <= l->inn && l->in[skip + 1] == 0x55) {
                skip += 6;   /* the iPhone's marker */
                if (!l->marker_seen) {
                    l->marker_seen = 1;
                    say("%s: the iPhone's marker", l->name);
                    if (l->mode == IAP2_BLUETOOTH && !l->syn_sent)
                        send_syn(l);
                }
                continue;
            }
            ++skip;
        }
        if (skip) {
            memmove(l->in, l->in + skip, l->inn - skip);
            l->inn -= skip;
        }
        if (l->inn < 9)
            return;
        if (csum(l->in, 8) != l->in[8]) {
            memmove(l->in, l->in + 1, --l->inn);
            continue;
        }
        len = get16(l->in + 2);
        control = l->in[4];
        seq = l->in[5];
        session = l->in[7];
        if (len < 9) {
            memmove(l->in, l->in + 1, --l->inn);
            continue;
        }
        if (l->inn < len)
            return;
        pl = l->in + 9;
        pn = len > 9 ? len - 10 : 0;
        if (len > 9 && csum(pl, pn) != pl[pn])
            say("%s: packet %u with a bad payload checksum (ignored)", l->name, seq);
        else if (control & 0x10) {
            say("%s: the iPhone reset the link", l->name);
            l->failed = 1;
        } else {
            if (control & 0x80) {
                if (pn >= 10) {
                    l->peer_window = pl[1] ? pl[1] : 1;
                    l->peer_maxlen = get16(pl + 2);
                    l->peer_acks = pl[9];
                    say("%s: the iPhone's link: %u outstanding, %u bytes, retransmission %u ms, acknowledgement %u ms, "
                        "%u cumulative", l->name, pl[1], l->peer_maxlen, get16(pl + 4), get16(pl + 6), pl[9]);
                }
                l->last_rx = seq;
                l->synced = 1;
                link_packet(l, 0x40, l->sent_seq, 0, 0, 0);
            }
            if (control & 0x40) {
                l->acked = l->in[6];
                if (l->queued)
                    flush(l);
            }
            if ((control & 0x40) && !l->normal && l->synced) {
                l->normal = 1;
                say("%s: link up", l->name);
            }
            if (control == 0x40 && pn) {
                if (seq == ((l->last_rx + 1) & 0xff)) {
                    l->last_rx = seq;
                    if (session == CONTROL)
                        csm_in(l, pl, pn);
                }
                if (l->peer_acks)
                    link_packet(l, 0x40, l->sent_seq, 0, 0, 0);
            }
        }
        memmove(l->in, l->in + len, l->inn - len);
        l->inn -= len;
    }
}

int iap2_run(const struct iap2_io* io, const struct iap2_carplay* cp, int mode)
{
    static const unsigned char marker[6] = {0xff, 0x55, 0x02, 0x00, 0xee, 0x10};
    struct link* l = (struct link*)calloc(1, sizeof(struct link));
    const long long t0 = ms_now();
    int r;
    if (!l)
        return -1;
    l->io = io;
    l->cp = cp;
    l->mode = mode;
    l->name = mode == IAP2_BLUETOOTH ? "iap2-bt" : mode == IAP2_TUNNEL ? "iap2-tunnel" : "iap2";
    l->sent_seq = l->wire_seq = l->acked = 99;
    l->peer_window = 1;
    l->marker_at = t0;
    if (wire(l, marker, sizeof(marker)) || (mode != IAP2_BLUETOOTH && send_syn(l))) {
        free(l);
        return -1;
    }
    while (!g_stop && !l->failed && !usb_gone()) {
        unsigned char b[16384];
        const int n = io->read(io->ctx, b, sizeof(b), 300);
        const long long now = ms_now();
        if (n < 0) {
            say("%s: the connection ended", l->name);
            break;
        }
        if (n > 0) {
            if ((unsigned)n > sizeof(l->in) - l->inn)
                l->inn = 0;
            memcpy(l->in + l->inn, b, (unsigned)n);
            l->inn += (unsigned)n;
            link_in(l);
        }
        if (mode == IAP2_BLUETOOTH && !l->marker_seen && now - l->marker_at >= 1000) {
            l->marker_at = now;
            wire(l, marker, sizeof(marker));
        }
        if (!l->synced && l->syn_sent && l->syn_sent < 10 &&
            now - l->syn_at > (mode == IAP2_BLUETOOTH ? 1000 : 5000)) {
            send_syn(l);
            say("%s: SYN again", l->name);
        }
        if (!l->identified && now - t0 > 60000) {
            say("%s: no identification within 60 s", l->name);
            break;
        }
    }
    say("%s: over (identification %s, authentication %s, %d CarPlay start(s))", l->name,
        l->identified ? "accepted" : "not accepted", l->authenticated ? "succeeded" : "not succeeded",
        l->carplay_sent);
    if (mode != IAP2_BLUETOOTH)
        flag_clear("/fs/ram3/nbt-aa-media");
    r = l->authenticated ? 0 : -1;
    while (l->queued)
        free(l->queue[--l->queued]);
    free(l);
    return r;
}
