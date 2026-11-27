/* nbt_decoder: Android Auto's picture through the car's own video path (2026-11-19).
 *
 * The phone's H.264 (Annex B, from native_service's FIFO) is decoded on the CPU with FFmpeg 2.8 (H.264 only, with its
 * SIMD assembly, scripts/ffmpeg_qnx.py) and handed to libnmsdk's INvVideo, which converts every picture into its own
 * NV24 surfaces and composites the stream into iDrive's EARLYVIDEO layer; libwal_nvidia places, raises and shows it.
 *
 *   nbt_decoder --input FIFO [--ready FILE] [--size WxH] [--fps 30|60]
 *   nbt_decoder --bench FILE         decode only, as fast as possible, and print the rate (no display needed)
 *
 *   --input  Annex B stream; one writer per Android Auto session, the program stays for the next one
 *   --ready  created whenever the program waits for a writer (native_service starts the session after it)
 *   --size   window size on the display (default 1280x480; split screen 800x480)
 *   --fps    stream rate given to the car (default 30): picture timestamps and how long the car holds each picture
 *   --rows   show only the top N rows of the phone's picture (1280x720 canvas with the interface in its top 480 rows:
 *            --rows 480 shows them 1:1 on the 1280x480 display)
 *   --hw     decode with the car's own hardware H.264 decoder (test, 2026-11-22): FFmpeg only parses (hwaccel
 *            h264_vdpau), libnmsdk's VideoDecode decodes into the car's surfaces, VideoRenderFrame shows them without
 *            any copy on the CPU (nbt_hwdec.h). If the car refuses the decoder at the start, the CPU path takes over;
 *            if hardware decoding keeps failing later, the program writes decoder.hwfail into the current folder and
 *            ends (native_service then starts the next session without --hw)
 * Files: decoder.stale in the current folder (ms without a new picture before the window is hidden, default 5000,
 *   0 = never); in /var/opt/hmi/data/rw/NbtAndroidAutoRT: decoder.timeout (NV_NMSDK_TASK_TIMEOUT_MS, default 3000),
 *   decoder.repeat (ms without a new picture before the last one is handed to the car again, default 50, 0 = never).
 *
 * Threads
 *   main, inside NvKdLaunch: reads the FIFO, cuts it into access units, decodes, writes the picture as NV12 into a
 *     three-picture mailbox (the newest picture replaces one not yet taken); runs the window policy every <= 100 ms
 *   display: takes the newest picture and, while the window is shown, uploads it (VideoFrame) and presents it
 *     (VideoRenderFrame); with nothing new for decoder.repeat ms, the last picture again (the car fades the window out
 *     when no picture comes for a short while: still screens such as YouTube Music flickered)
 * Window policy: shown while the Android Auto page is in front (no /fs/ram3/nbt-aa-hidden), the session runs
 *   (/fs/ram3/nbt-aa-state says state=running and is at most 5 s old; a stop counts after 1.5 s) and pictures keep
 *   coming (decoder.stale). Hiding waits 400 ms (short page changes do not blink); the window is set again every 2 s.
 *   Nothing is shown before the session's first picture.
 *
 * The car's interface, as libnmsdk.so (2014) exports it (CNvVideoBase with full C++ signatures):
 *   INvVideo::GetNvVideo() -> object; virtual methods 0 Init, 2 VideoOpen(void** h, ICallback*, INvAvSync*,
 *   CConfDecoder*), 4 VideoConfigure(h, const CConfVideo&), 5 VideoFrame(h, EVideoFormat, const uint8_t** planes,
 *   uint32_t* pitches, int64_t pts, EPictureFormat, void*), 12/13 SetVideoSource/SetVideoDestination(h, const CRect&),
 *   14 VideoBufferCreate(void** b, w, h, float, bool), 16 VideoRenderFrame(buffer, h, CVideoRenderFrameParams*),
 *   17 VideoStreamCreate(h, void** buffers, n); 3 VideoClose(h), 15 VideoBufferDestroy(b), 20 VideoStreamDestroy(h).
 *   All calls are queued to the library's worker thread and wait for it
 *   (NV_NMSDK_TASK_TIMEOUT_MS).
 *   - CConfVideo (OnVideoConfigure): width, height, two floats (the second the frame rate: below 32 each picture is
 *     held for two display refreshes, so 30 caps the stream at 30 pictures/s), decoder type in word 5 (0 none:
 *     pictures come with VideoFrame), a 64-bit value in words 9-10, surface count (1-20) in word 11, stream id in
 *     word 12 (stream property 0x1789 of kdCreateStreamNV)
 *   - VideoFrame: EVideoFormat 1 = NV12 (2/3 YV12, 4/5 packed 4:2:2); the library takes a free NV24 surface (it
 *     waits for one, and surfaces come back only once the window is composited) and converts into it on the CPU
 *   - VideoRenderFrame params: word 0 picture structure (3 = frame), surface 0 = the one VideoFrame just filled
 *   - window layers (libwal_nvidia LayerId): 1024 EARLYVIDEO, 1052 VIDEO; the stream with id -28 lands in EARLYVIDEO
 *     (1052 - 28), which iDrive leaves alone while Android Auto is in front (car 2026-11-19)
 * The program ends with _exit(): returning through NvKdLaunch crashed in the libraries' cleanup (car 2026-11-19).
 */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/vdpau.h"
#include "libavutil/cpu.h"
#include "libavutil/pixdesc.h"
#include "nbt_hwdec.h"
#include "nbt_panel_font.h"

#define RT_DIR "/var/opt/hmi/data/rw/NbtAndroidAutoRT"
#define HIDDEN_FILE "/fs/ram3/nbt-aa-hidden"
#define STATE_FILE "/fs/ram3/nbt-aa-state"
#define LAYER_VIDEO 1052
#define STREAM_ID (-28)   /* -> layer 1024, EARLYVIDEO */

/* ---- the car's interfaces ---- */

struct conf_video {   /* INvVideo::CConfVideo, 13 words */
    uint32_t width, height;
    float aspect, fps;
    uint32_t word4, decoder, word6, word7, word8, value_lo, value_hi, surfaces;
    int32_t stream_id;
};
struct rect {
    uint32_t left, top, right, bottom;
};

typedef int (*nv_init_fn)(void*);
typedef int (*nv_open_fn)(void*, void**, void*, void*, void*);
typedef int (*nv_configure_fn)(void*, void*, const struct conf_video*);
typedef int (*nv_frame_fn)(void*, void*, int, const uint8_t* const*, const uint32_t*, int64_t, int, void*);
typedef int (*nv_rect_fn)(void*, void*, const struct rect*);
typedef int (*nv_buffer_fn)(void*, void**, uint32_t, uint32_t, float, int);
typedef int (*nv_render_fn)(void*, void*, void*, uint8_t*);
typedef int (*nv_stream_fn)(void*, void*, void**, uint32_t);
typedef int (*nv_handle_fn)(void*, void*);   /* VideoClose(h), VideoBufferDestroy(b), VideoStreamDestroy(h) */
/* VideoDecode(h, CBitStreamList* ({data, size} pairs), count, pts, bool, nvh264bspPictureDecode*, reference table
 * (8 bytes per row, the surface first), rows, surface in/out: -1 = take a free one and keep it, else decode into it) */
typedef int (*nv_decode_fn)(void*, void*, const void*, uint32_t, int64_t, int, void*, void**, uint32_t, void**);
typedef void* (*wal_create_fn)(void);
typedef void (*wal_place_fn)(void*, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
typedef void (*wal_order_fn)(void*, const void*, unsigned long);
typedef void (*wal_visible_fn)(void*, unsigned long, int);

enum { NV_INIT = 0, NV_OPEN = 2, NV_CLOSE = 3, NV_CONFIGURE = 4, NV_FRAME = 5, NV_DECODE = 6, NV_SOURCE = 12,
       NV_DEST = 13, NV_BUFFER = 14, NV_BUFFER_DESTROY = 15, NV_RENDER = 16, NV_STREAM = 17, NV_STREAM_DESTROY = 20 };
enum { FORMAT_NV12 = 1, PICTURE_FRAME = 3, BUFFERS = 3, SURFACES = 4 };
/* hardware decoding: CConfVideo decoder type 3 = H.264 (NVVADecoderCreate with 16 reference frames), the library's
 * most surfaces (1-20); our handles 1..HW_HANDLES stand for them in FFmpeg's VDPAU frames */
enum { DECODER_H264 = 3, HW_HANDLES = 20, HW_GIVE_UP = 10 };

static void* nv_method(void* obj, int index) { return (*(void***)obj)[index]; }

/* ICallback for VideoOpen: nothing to report with decoder type 0, every method returns 0 */
static int callback_nothing(void) { return 0; }
static void* g_callback_methods[32];
static struct { void** methods; } g_callback = {g_callback_methods};

/* ---- state ---- */

static struct {
    const char* input;
    const char* ready;
    int width, height, fps, bench;
    long stale_ms, repeat_ms;
    int rows;   /* 0 = the whole picture */
    int hw;     /* --hw */
    int hwscale; /* --hw-scale: hardware decoding with --rows into stream buffers of the whole coded height (scaled twice) */
} g_opt = {0, 0, 1280, 480, 30, 0, 5000, 50, 0, 0, 0};

/* rows of a decoded picture that are shown */
static int shown_rows(int height) { return g_opt.rows && height > g_opt.rows ? g_opt.rows : height; }

static volatile sig_atomic_t g_quit;

static struct {
    void* lib;   /* libnmsdk.so */
    void* nv;
    void* stream;
    void* buffers[BUFFERS];
    int w, h, out_h, ready;
    void* wal;
    wal_place_fn place;
    wal_order_fn order;
    wal_visible_fn visible;
    unsigned long window;
} g_car;

/* three NV12 pictures: one being written, the newest finished one, one being shown; with --hw the decoded surface
 * instead (the frame is held so FFmpeg does not decode into its surface while it may still be shown) */
struct picture {
    uint8_t* y;
    uint8_t* uv;
    AVFrame* hw;
    void* surface;
    unsigned long number;
};
static struct {
    struct picture pic[3];
    int writing, newest, showing, fresh;
    pthread_mutex_t lock;
    pthread_cond_t posted;
} g_box = {.writing = 0, .newest = 1, .showing = 2, .lock = PTHREAD_MUTEX_INITIALIZER,
           .posted = PTHREAD_COND_INITIALIZER};

static struct {
    volatile int shown;   /* -1 not set yet */
    long long refresh_at, hide_from, last_picture, state_read, stopped_since;
    int running, stale_said;
} g_win = {-1, 0, 0, 0, 0, 0, 1, 0};

/* statistics per interval (the display thread's part under g_box.lock), logged every 5 s: received = access units
 * from the FIFO (pictures the phone sent), decoded = pictures the decoder returned (counted when the call, VideoDecode
 * with --hw included, has come back), shown = new pictures the car took (VideoFrame / VideoRenderFrame succeeded),
 * repeated = the last picture again, replaced = decoded but never shown (a newer one came first) */
static struct {
    long long since;
    unsigned long received, decoded, errors, replaced, shown, hidden, repeated, present_errors;
    long long decode_us, decode_max, upload_us, upload_max;
    unsigned long total;
} g_stat;

static AVCodecContext* g_dec;
static AVFrame* g_frame;

static struct {
    int active;                       /* this stream is decoded by the car */
    int failed;                       /* not for this program any more: the CPU path */
    AVVDPAUContext* ctx;
    void* surf[HW_HANDLES + 1];       /* the car's surface for each handle, 0 = none yet */
    void* table[2 * (HW_HANDLES + 1)];/* reference table for VideoDecode: row = handle */
    int in_use[HW_HANDLES + 1];
    uint8_t token[HW_HANDLES + 1];
    uint8_t bsp[BSP_SIZE];
    unsigned long pictures, errors, in_a_row;
    int64_t pts;
} g_hw;

/* ---- helpers ---- */

static long long now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static long long now_ms(void) { return now_us() / 1000; }

static void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* fmt, ...)
{
    char line[400];
    va_list a;
    va_start(a, fmt);
    vsnprintf(line, sizeof(line), fmt, a);
    va_end(a);
    const long long ms = now_ms();
    printf("%5lld.%03lld %s\n", ms / 1000 % 100000, ms % 1000, line);
    fflush(stdout);
}

static long read_number(const char* path, long fallback)
{
    long v = fallback;
    FILE* f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%ld", &v) != 1)
            v = fallback;
        fclose(f);
    }
    return v;
}

static void on_signal(int sig)
{
    (void)sig;
    g_quit = 1;
}

/* ---- car: video and window ---- */

static int car_open(void)
{
    void* lib = dlopen("libnmsdk.so", RTLD_NOW | RTLD_GLOBAL);
    void* (*get)(void) = lib ? (void* (*)(void))dlsym(lib, "_ZN8INvVideo10GetNvVideoEv") : 0;
    g_car.lib = lib;
    g_car.nv = get ? get() : 0;
    if (!g_car.nv) {
        say("video: libnmsdk.so not usable (%s)", lib ? "no INvVideo" : dlerror());
        return -1;
    }
    const int r = ((nv_init_fn)nv_method(g_car.nv, NV_INIT))(g_car.nv);
    if (r < 0) {
        say("video: Init failed 0x%08x", r);
        return -1;
    }

    void* wal = dlopen("/opt/sys/lib/libwal_nvidia.so", RTLD_NOW | RTLD_GLOBAL);
    if (!wal)
        wal = dlopen("libwal_nvidia.so", RTLD_NOW | RTLD_GLOBAL);
    wal_create_fn create = wal ? (wal_create_fn)dlsym(wal, "_ZN3wal15WalHmiInterface9createPtrEv") : 0;
    if (create) {
        g_car.place = (wal_place_fn)dlsym(wal, "_ZN3wal15WalHmiInterface24setWindowPositionAndSizeEmmmmm");
        g_car.order = (wal_order_fn)dlsym(wal, "_ZN3wal15WalHmiInterface9setZOrderEPKNS0_12OrderTypeDefEm");
        g_car.visible = (wal_visible_fn)dlsym(wal, "_ZN3wal15WalHmiInterface10setVisibleEmb");
        if (g_car.place && g_car.order && g_car.visible)
            g_car.wal = create();
    }
    if (!g_car.wal)
        say("window: libwal_nvidia.so not usable, the picture cannot be shown");
    g_car.window = (unsigned long)(LAYER_VIDEO + STREAM_ID);
    return 0;
}

static void panel_open(void);
static void sub_mixer(int w, int h, int out_h);

/* the stream, sized like the first picture: decoder 0 = pictures from VideoFrame, DECODER_H264 = from VideoDecode;
 * source = the part of the picture shown, destination = where it goes in the stream's buffers (both 0 = the whole
 * picture); out_h = the height of those buffers (the window shows a whole buffer), h for the whole picture */
static void car_release(int streamed);

static int car_stream_with(int w, int h, uint32_t decoder, uint32_t surfaces, const struct rect* source,
                           const struct rect* destination, int out_h)
{
    for (int i = 0; i < 32; ++i)
        g_callback_methods[i] = (void*)callback_nothing;
    static uint8_t decoder_conf[32];
    void* nv = g_car.nv;
    int r = ((nv_open_fn)nv_method(nv, NV_OPEN))(nv, &g_car.stream, &g_callback, 0, decoder_conf);
    if (r < 0 || !g_car.stream) {
        say("video: VideoOpen failed 0x%08x", r);
        return -1;
    }
    const struct conf_video conf = {(uint32_t)w, (uint32_t)h, 1.0f, (float)g_opt.fps, 0, decoder, 0, 0, 0, 50000000u, 0,
                                    surfaces, STREAM_ID};
    r = ((nv_configure_fn)nv_method(nv, NV_CONFIGURE))(nv, g_car.stream, &conf);
    if (r < 0) {
        say("video: VideoConfigure failed 0x%08x", r);
        car_release(0);
        return -1;
    }
    sub_mixer(w, h, out_h);
    for (int i = 0; i < BUFFERS; ++i) {
        r = ((nv_buffer_fn)nv_method(nv, NV_BUFFER))(nv, &g_car.buffers[i], (uint32_t)w, (uint32_t)out_h, 0.0f, 0);
        if (r < 0) {
            say("video: VideoBufferCreate failed 0x%08x", r);
            car_release(0);
            return -1;
        }
    }
    r = ((nv_stream_fn)nv_method(nv, NV_STREAM))(nv, g_car.stream, g_car.buffers, BUFFERS);
    if (r < 0) {
        say("video: VideoStreamCreate failed 0x%08x", r);
        car_release(0);
        return -1;
    }
    const struct rect all = {0, 0, (uint32_t)w, (uint32_t)h};
    ((nv_rect_fn)nv_method(nv, NV_SOURCE))(nv, g_car.stream, source ? source : &all);
    ((nv_rect_fn)nv_method(nv, NV_DEST))(nv, g_car.stream, destination ? destination : &all);
    g_car.w = w;
    g_car.h = h;
    g_car.out_h = out_h;
    g_car.ready = 1;
    {   /* for native_service: a car stream was set up in this boot (another nbt_decoder would need a reboot) */
        const int fd = open("decoder.streamed", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
            close(fd);
    }
    say("video: stream %dx%d (buffers %dx%d) at %d fps, id %d, %s (window %lu, %dx%d on the display)", w, h, w, out_h,
        g_opt.fps, STREAM_ID, decoder ? "decoded by the car" : "pictures from the CPU", g_car.window, g_opt.width,
        g_opt.height);
    panel_open();
    return 0;
}

static int car_stream(int w, int h) { return car_stream_with(w, h, 0, SURFACES, 0, 0, h); }

static void window_set(int show)
{
    if (!g_car.wal)
        return;
    if (show) {
        /* OrderTypeDef (libwal_nvidia setZOrder): window id, then the z value as 16 bits; sent to WalServer. z 64, not 0:
         * another NBT project (2026-11-26) found z 0 has side effects in WalServer and uses 64 */
        const struct { uint32_t id; uint16_t z, pad; } order = {(uint32_t)g_car.window, 64, 0};
        g_car.place(g_car.wal, g_car.window, 0, 0, (unsigned long)g_opt.width, (unsigned long)g_opt.height);
        g_car.order(g_car.wal, &order, 1);
    }
    g_car.visible(g_car.wal, g_car.window, show);
}

/* the stream, its buffers and the decoder handed back (2026-11-24). The stream lives in the window server
 * (kdCreateStreamNV, buffers in our memory) and, as it looks, does not end with the process: car, unplug the iPhone,
 * restart the service, plug it in: the next nbt_decoder's stream (same id) showed garbage and the head unit went
 * down. streamed: VideoStreamCreate worked. */
static void car_release(int streamed)
{
    void* nv = g_car.nv;
    int r1 = 0, r2 = 0, r3 = 0;
    if (streamed)
        r1 = ((nv_handle_fn)nv_method(nv, NV_STREAM_DESTROY))(nv, g_car.stream);
    for (int i = 0; i < BUFFERS; ++i)
        if (g_car.buffers[i]) {
            const int r = ((nv_handle_fn)nv_method(nv, NV_BUFFER_DESTROY))(nv, g_car.buffers[i]);
            if (r < 0)
                r2 = r;
            g_car.buffers[i] = 0;
        }
    if (g_car.stream)
        r3 = ((nv_handle_fn)nv_method(nv, NV_CLOSE))(nv, g_car.stream);
    g_car.stream = 0;
    g_car.ready = 0;
    say("video: stream closed (stream 0x%08x, buffers 0x%08x, close 0x%08x)", r1, r2, r3);
}

/* the trip computer box is in the stream's mixer (panel code), and a VideoRenderFrame failed while it was */
static volatile int g_sub_live, g_sub_failed;

static pthread_t g_display;
static int g_display_started;

/* before the program ends: the display thread stopped, the window hidden, the stream closed */
static void car_close(void)
{
    g_quit = 1;
    if (g_display_started) {
        pthread_mutex_lock(&g_box.lock);
        pthread_cond_broadcast(&g_box.posted);
        pthread_mutex_unlock(&g_box.lock);
        pthread_join(g_display, 0);
        g_display_started = 0;
    }
    if (!g_car.ready)
        return;
    window_set(0);
    car_release(1);
}

static unsigned long g_turn;

/* --hw: show a surface the car decoded (no copy) */
static void car_present_surface(const struct picture* p, int repeat)
{
    uint8_t params[64];
    memset(params, 0, sizeof(params));
    params[0] = PICTURE_FRAME;
    memcpy(params + 0xc, &p->surface, sizeof(p->surface));   /* the surface to show (0 = the one VideoFrame filled) */
    params[0x14] = 1;
    const long long t0 = now_us();
    const int r = ((nv_render_fn)nv_method(g_car.nv, NV_RENDER))(g_car.nv, g_car.buffers[g_turn++ % BUFFERS], g_car.stream,
                                                                params);
    const long long dt = now_us() - t0;
    if (r < 0)
        say("video: VideoRenderFrame failed 0x%08x (picture %lu, surface %p)", r, p->number, p->surface);
    if (r < 0 && g_sub_live)
        g_sub_failed = 1;
    pthread_mutex_lock(&g_box.lock);
    if (r < 0)
        ++g_stat.present_errors;
    else if (repeat)
        ++g_stat.repeated;
    else
        ++g_stat.shown;
    g_stat.upload_us += dt;
    if (dt > g_stat.upload_max)
        g_stat.upload_max = dt;
    pthread_mutex_unlock(&g_box.lock);
}

/* upload and present one picture; the buffer turns over with every picture the car took */
static void car_present(const struct picture* p, int repeat)
{
    static unsigned long presented;
    if (p->surface) {
        car_present_surface(p, repeat);
        return;
    }
    const uint8_t* planes[3] = {p->y, p->uv, 0};
    const uint32_t pitches[3] = {(uint32_t)g_car.w, (uint32_t)g_car.w, 0};
    const int64_t pts = (int64_t)++presented * 1000000 / g_opt.fps;   /* every hand-over, repeats too */
    const long long t0 = now_us();
    int r = ((nv_frame_fn)nv_method(g_car.nv, NV_FRAME))(g_car.nv, g_car.stream, FORMAT_NV12, planes, pitches, pts,
                                                         PICTURE_FRAME, 0);
    const long long dt = now_us() - t0;
    if (r < 0) {
        say("video: VideoFrame failed 0x%08x after %lld ms (picture %lu)", r, dt / 1000, p->number);
        pthread_mutex_lock(&g_box.lock);
        ++g_stat.present_errors;
        pthread_mutex_unlock(&g_box.lock);
        return;
    }
    uint8_t params[64];
    memset(params, 0, sizeof(params));
    params[0] = PICTURE_FRAME;
    params[0x14] = 1;   /* set on the car since the first working picture; its meaning is not traced in the library */
    r = ((nv_render_fn)nv_method(g_car.nv, NV_RENDER))(g_car.nv, g_car.buffers[g_turn++ % BUFFERS], g_car.stream, params);
    if (r < 0)
        say("video: VideoRenderFrame failed 0x%08x (picture %lu)", r, p->number);
    if (r < 0 && g_sub_live)
        g_sub_failed = 1;
    pthread_mutex_lock(&g_box.lock);
    if (r < 0)
        ++g_stat.present_errors;
    else if (repeat)
        ++g_stat.repeated;
    else
        ++g_stat.shown;
    g_stat.upload_us += dt;
    if (dt > g_stat.upload_max)
        g_stat.upload_max = dt;
    pthread_mutex_unlock(&g_box.lock);
}


/* ---- trip computer box (2026-11-23; shown per iDrive's "Trip computer" option and long NAV) ----
 * User: instead of iDrive's split screen, a box over Android Auto at x 880, y 100, 380 x 136 with the trip computer (20 px from the right edge, icons before the values).
 * Car 2026-11-23, a stream of its own for the box: (1) in this process it took over Android Auto's window (only its
 * top-left 280 x 100 was left, as the reference decoder's 64x64 stream created first was taken over by the real one);
 * (2) in a process of its own it never showed, with stream id -27 or 0 (slog: window 1052=VIDEO placed at 1000/120,
 * 280/100, visible, z 1, nothing on screen): a window shows the stream its owner accepts (kdAcceptClientStreamNV), and
 * 1052 belongs to another application (conn 151586_2).
 * Then a WAL sprite (2026-11-23 to 11-26, a process of its own, nbt_decoder --panel). Removed: the head unit went down
 * on 2026-11-26 19:40 with "NVRM: PUSHER RESERVED CMD ERROR", Fifo Parse Error on channel 1 (WalServer); in
 * WalServer's GPU command ring (NVshm_44) a later command batch had been written into the middle of our sprite's
 * pixel upload (the 2D engine's SIFC data, ~60 % of the ring each second): two writers in one ring.
 * Now libnmsdk's subpicture (2026-11-26): our own stream's video mixer blends it into every picture in
 * VideoRenderFrame, in our process's GPU channel; the WalServer only sees the finished buffers, as before. Traced in
 * libnmsdk.so (2014):
 *   - SubPictureConfigure (method 21, {width, height}): an NVVA output surface of that size for the stream
 *   - SubPicturePalette (22): 16 entries of Y, Cr, Cb bytes, converted to RGB with alpha 255
 *   - SubPictureFrame (23, h, format (unused), bits, start, end, 0): NVVAOutputSurfacePutBitsIndexed of the whole
 *     surface, one byte per pixel, alpha in the high 4 bits and the palette index in the low 4, pitch = width; sets
 *     the stream's subpicture flag. VideoRenderFrame adds the surface as a layer over the whole picture (no source or
 *     destination rectangle) while start <= the stream's time <= end (unsigned 64 bit; without a clock the start
 *     itself)
 *   - SubPictureAbort (24) clears the flag; SubPictureEnd / Display / RegisterSubPictureAsync only trace (unsupported)
 *   - but the stream's mixer is created without room for a layer: see sub_mixer, which replaces it
 * So the subpicture is as large as the stream's buffers (1280 x 480 for the full window, one byte per pixel, 600 KB
 * per upload) with the box in it and the rest transparent; uploaded only when the box's picture changed (at most once
 * a second; the video itself is 0.9 MB per picture on the CPU path). It shows and hides with Android Auto's window by
 * itself, and also with hardware decoding. No box if the buffers are not the window's size (it would be scaled).
 * Position in the window: decoder.panelx / decoder.panely (default 875 + 384 - the box's width / 96).
 * Data: /fs/ram3/nbt-aa-tripkombi (nbt_knob, from the cluster: trip start, duration, distance, averages) and
 * /fs/ram3/nbt-aa-trip (HMI plugin, CDS: range, tank, temperatures, instantaneous consumption, averages as fallback).
 *   12:04        44 min        30 km
 *   7.4 L/100km           42.1 km/h
 *   833 km        52 L        0.7 L/h (stationary) / 6.3 L/100km (moving)
 *   11.5°C    E 80°    O 73°    B 85%
 * White text on a translucent black background. Shown while Android Auto's window is shown full width and
 * /fs/ram3/nbt-aa-panel-off does not exist (long NAV in nbt_knob). */
#define PANEL_OFF_FILE "/fs/ram3/nbt-aa-panel-off"
#define TRIP_KOMBI_FILE "/fs/ram3/nbt-aa-tripkombi"
#define TRIP_CDS_FILE "/fs/ram3/nbt-aa-trip"
/* user 2026-11-23: icons before the values, 22 px font, 4 px between lines (380 x 136, x 880, 20 px from the right edge, 12 px inner padding); at y 100, corners rounded like Android Auto's (radius 12), 20 px font
 * at full precision ("-12.5°C  E 105°  O 120°  B 100%" and "1036 km  57 L  112.5 L/100km" fit), inner padding 16 px
 * left / right and 8 px top / bottom: 356 x 112 */
enum { PANEL_PAD_X = 12, PANEL_PAD_Y = 8 };
enum { PANEL_LINE = PANEL_FONT_HEIGHT + 4 };   /* line pitch: 31 px at 22 px */
enum { PANEL_X = 875, PANEL_Y = 96, PANEL_W = 384, PANEL_H = 2 * 8 + 4 * PANEL_LINE + PANEL_FONT_HEIGHT, PANEL_BG = 24, PANEL_FG = 235, PANEL_RADIUS = 16 };
/* small box (user 2026-11-23): "Widget size" in iDrive's Android Auto page, decoder.panelsize in RT_DIR (read when
 * the session starts): "full" = the full box; none (the default) or "small" = a third narrower (256 px, the right edge where the full box's
 * is, so x 1003), two pages of a title and four lines ("Trip": start, duration, distance, average consumption, average
 * speed; "Car": range, tank, instantaneous consumption, temperatures, battery), as tall as the full box, long NAV
 * (nbt_knob) cycles page 1, page 2, off (/fs/ram3/nbt-aa-panel-page2 = the second page); "trip" / "car" = the same
 * box with only that page, long NAV on / off. At the end of the title: the short-term average speed while it runs,
 * else (two pages only) two dots for the page (as Android Auto shows pages: top right, same size, the current one
 * white, the other grey). */
#define PANEL_PAGE2_FILE "/fs/ram3/nbt-aa-panel-page2"
enum { PANEL_PAGED_W = 256, PANEL_PAGED_H = PANEL_H, PANEL_H_MAX = PANEL_H };

static struct {
    int ok, have;
    volatile int shown;
    int w, h, paged, pages, page;          /* size (PANEL_W x PANEL_H, or small), pages 1 or 2, page 1 or 2 */
    uint8_t draw[PANEL_W * PANEL_H_MAX];   /* coverage over PANEL_BG, rows of w */
    long long text_at;
} g_panel;

/* our own icons (scripts/gen_panel_font.py), characters 1..12 in the strings */
#define I_CLOCK "\x01"
#define I_STOPWATCH "\x02"
#define I_ROAD "\x03"
#define I_PUMP "\x04"
#define I_GAUGE "\x05"
#define I_RANGE "\x06"
#define I_CAN "\x07"
#define I_DROP "\x08"
#define I_THERMO "\x09"
#define I_COOLANT "\x0a"
#define I_OIL "\x0b"
#define I_BATTERY "\x0c"
static int panel_glyph(unsigned char c)
{
    return c >= 32 && c <= 126 ? c - 32 : c == 0xb0 ? 95 : c >= 1 && c <= 12 ? 95 + c : '?' - 32;
}

static int panel_width(const char* t)
{
    int w = 0;
    for (; *t; ++t)
        w += panel_glyphs[panel_glyph((unsigned char)*t)].advance;
    return w;
}

/* align: 0 left at x, 1 centred on x, 2 right at x */
static void panel_text_at(const char* t, int x, int top, int align)
{
    x -= align == 1 ? panel_width(t) / 2 : align == 2 ? panel_width(t) : 0;
    for (; *t; ++t) {
        const int g = panel_glyph((unsigned char)*t);
        const unsigned char* bits = panel_bits + panel_glyphs[g].offset;
        const int gw = panel_glyphs[g].width, gx = x + panel_glyphs[g].left;
        for (int row = 0; row < PANEL_FONT_HEIGHT; ++row) {
            const int y = top + row;
            if (y < 0 || y >= g_panel.h)
                continue;
            for (int col = 0; col < gw; ++col) {
                const int px = gx + col;
                const unsigned a = bits[row * gw + col];
                if (px < 0 || px >= g_panel.w || !a)
                    continue;
                uint8_t* d = &g_panel.draw[y * g_panel.w + px];
                const unsigned v = PANEL_BG + (PANEL_FG - PANEL_BG) * a / 255;
                if (v > *d)
                    *d = (uint8_t)v;
            }
        }
        x += panel_glyphs[g].advance;
    }
}

static int panel_read(const char* path, char* buf, size_t size)
{
    buf[0] = 0;
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    const ssize_t n = read(fd, buf, size - 1);
    close(fd);
    buf[n > 0 ? n : 0] = 0;
    return n > 0;
}

/* "key=value" line of nbt_knob's file */
static int kombi_value(const char* text, const char* key, int* out)
{
    char k[24];
    snprintf(k, sizeof(k), "%s=", key);
    for (const char* p = text; (p = strstr(p, k)); ++p)
        if (p == text || p[-1] == '\n')
            return sscanf(p + strlen(k), "%d", out) == 1 && *out >= 0;
    return 0;
}

/* a number after "key": on the CDS line that starts with name */
static int cds_value(const char* text, const char* name, const char* key, double* out)
{
    const size_t n = strlen(name);
    for (const char* line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : 0) {
        if (strncmp(line, name, n) || line[n] != ' ')
            continue;
        const char* end = strchr(line, '\n');
        char k[40];
        snprintf(k, sizeof(k), "\"%s\":", key);
        const char* p = strstr(line, k);
        if (!p || (end && p > end))
            return 0;
        return sscanf(p + strlen(k), "%lf", out) == 1;
    }
    return 0;
}

/* a filled circle (4 x 4 samples per pixel) in the drawing, level = brightness in percent of the text's */
static void panel_dot(double cx, double cy, double r, int level)
{
    for (int y = (int)(cy - r) - 1; y <= (int)(cy + r) + 1; ++y)
        for (int x = (int)(cx - r) - 1; x <= (int)(cx + r) + 1; ++x) {
            if (y < 0 || y >= g_panel.h || x < 0 || x >= g_panel.w)
                continue;
            int in = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const double dx = x + (sx + 0.5) / 4 - cx, dy = y + (sy + 0.5) / 4 - cy;
                    in += dx * dx + dy * dy <= r * r;
                }
            const unsigned v = PANEL_BG + (PANEL_FG - PANEL_BG) * level / 100 * in / 16;
            uint8_t* d = &g_panel.draw[y * g_panel.w + x];
            if (v > *d)
                *d = (uint8_t)v;
        }
}

/* paged box: the page's four lines (two values: one left, one right).
 * v = start, duration, distance, average consumption, average speed, range, tank, instantaneous consumption, outside,
 * coolant, oil, battery */
static void panel_pages(char v[12][40], const int top[4], int L, int R)
{
    static const signed char page[2][4][2] = {
        {{0, 1}, {2, -1}, {3, -1}, {4, -1}},    /* Trip */
        {{5, 6}, {7, -1}, {8, 11}, {9, 10}},    /* Car */
    };
    const int p = g_panel.page == 2;
    for (int line = 0; line < 4; ++line) {
        panel_text_at(v[(int)page[p][line][0]], R, top[line], 0);
        if (page[p][line][1] >= 0)
            panel_text_at(v[(int)page[p][line][1]], L, top[line], 2);
    }
}

static void panel_compose(void)
{
    char kombi[256], cds[1200];
    const int have_kombi = panel_read(TRIP_KOMBI_FILE, kombi, sizeof(kombi));
    const int have_cds = panel_read(TRIP_CDS_FILE, cds, sizeof(cds));
    if (!have_kombi && !have_cds)
        return;
    char a[40], b[40], c[40];
    char pg[12][40];   /* paged box: the values, in the order they appear on the pages */
    const int paged = g_panel.paged;
    int v, w;
    double x;
    memset(g_panel.draw, PANEL_BG, sizeof(g_panel.draw));
    const int L = g_panel.w - PANEL_PAD_X, M = g_panel.w / 2, R = PANEL_PAD_X;
    /* title line (user 2026-11-23): "Trip computer" and a rule from after it to the right padding, 2 px, at the height
     * of the lower-case letters' middle, a little dimmer than the text; the four value lines below it */
    {
        /* short-term average speed (user 2026-11-23): a long roller push (nbt_knob) creates /fs/ram3/nbt-aa-avgspeed
         * with its start time and removes it on the next one. From a new start on, driving.speedActual (CDS, km/h) is
         * integrated over time once a second (standing time counts, as in the trip computer's average) and shown at
         * the end of the title line, the rule ending before it. */
        char avg[40] = "";
        {
            static char started[32];
            static double km, secs;
            static long long last;
            char mark[32];
            if (panel_read("/fs/ram3/nbt-aa-avgspeed", mark, sizeof(mark))) {
                const long long now = now_ms();
                double speed;
                if (strcmp(mark, started)) {   /* a new start */
                    snprintf(started, sizeof(started), "%s", mark);
                    km = secs = 0;
                } else if (now - last > 0 && now - last < 5000) {
                    const double dt = (now - last) / 1000.0;
                    secs += dt;
                    if (cds_value(cds, "driving.speedActual", "speedActual", &speed) && speed > 0)
                        km += speed * dt / 3600.0;
                }
                last = now;
                if (secs >= 1)
                    snprintf(avg, sizeof(avg), I_GAUGE "%.1f km/h", km / secs * 3600.0);
                else
                    strcpy(avg, I_GAUGE "-- km/h");
            } else
                started[0] = 0;
        }
        const char* const title = !paged ? "Trip computer" : g_panel.page == 2 ? "Car" : "Trip";
        panel_text_at(title, R, PANEL_PAD_Y, 0);
        if (avg[0])
            panel_text_at(avg, L, PANEL_PAD_Y, 2);
        const int x0 = R + panel_width(title) + 10, y = PANEL_PAD_Y + PANEL_FONT_ASCENT - 7;
        int x1 = avg[0] ? L - panel_width(avg) - 10 : L;
        if (paged && g_panel.pages == 2 && !avg[0]) {   /* the page dots, right-aligned, centred on the rule */
            panel_dot(L - 4 - 16, y + 1, 4, g_panel.page == 2 ? 50 : 100);
            panel_dot(L - 4, y + 1, 4, g_panel.page == 2 ? 100 : 50);
            x1 = L - 24 - 10;
        }
        for (int row = y; row < y + 2; ++row)
            for (int col = x0; col < x1; ++col)
                g_panel.draw[row * g_panel.w + col] = (uint8_t)(PANEL_BG + (PANEL_FG - PANEL_BG) * 6 / 10);
    }
    const int top[4] = {PANEL_PAD_Y + PANEL_LINE, PANEL_PAD_Y + 2 * PANEL_LINE, PANEL_PAD_Y + 3 * PANEL_LINE,
                        PANEL_PAD_Y + 4 * PANEL_LINE};
    /* 1: trip start, duration, distance (cluster) */
    if (strstr(kombi, "start=") && sscanf(strstr(kombi, "start=") + 6, "%d:%d", &v, &w) == 2 && v >= 0)
        snprintf(a, sizeof(a), I_CLOCK "%d:%02d", v, w);
    else
        strcpy(a, I_CLOCK "--:--");
    int hours = 0, minutes;
    kombi_value(kombi, "hours", &hours);
    if (kombi_value(kombi, "minutes", &minutes) && hours > 0)
        snprintf(b, sizeof(b), I_STOPWATCH "%d:%02d h", hours, minutes);
    else if (kombi_value(kombi, "minutes", &minutes))
        snprintf(b, sizeof(b), I_STOPWATCH "%d min", minutes);
    else
        strcpy(b, I_STOPWATCH "-- min");
    if (kombi_value(kombi, "km", &v))
        snprintf(c, sizeof(c), I_ROAD "%d km", v);
    else
        strcpy(c, I_ROAD "-- km");
    if (paged) {
        strcpy(pg[0], a);
        strcpy(pg[1], b);
        strcpy(pg[2], c);
    } else {
        panel_text_at(a, R, top[0], 0);
        panel_text_at(b, M, top[0], 1);
        panel_text_at(c, L, top[0], 2);
    }
    /* 2: average consumption and speed (cluster, x0.1; CDS averageConsumption2 / averageSpeed2 otherwise) */
    if (kombi_value(kombi, "cons", &v) && v < 4095)
        snprintf(a, sizeof(a), I_PUMP "%d.%d L/100km", v / 10, v % 10);
    else if (cds_value(cds, "driving.averageConsumption", "averageConsumption2", &x))
        snprintf(a, sizeof(a), I_PUMP "%.1f L/100km", x);
    else
        strcpy(a, I_PUMP "-- L/100km");
    if (kombi_value(kombi, "speed", &v) && v < 4095)
        snprintf(b, sizeof(b), I_GAUGE "%d.%d km/h", v / 10, v % 10);
    else if (cds_value(cds, "driving.averageSpeed", "averageSpeed2", &x))
        snprintf(b, sizeof(b), I_GAUGE "%.1f km/h", x);
    else
        strcpy(b, I_GAUGE "-- km/h");
    if (paged) {
        strcpy(pg[3], a);
        strcpy(pg[4], b);
    } else {
        panel_text_at(a, R, top[1], 0);
        panel_text_at(b, L, top[1], 2);
    }
    /* 3: range, tank, instantaneous consumption. CDS engine.consumption is a counter, not a rate (car 2026-11-23: it
     * rises with the engine idling and wraps at 65536). Its unit is a guess: microlitres of fuel per count
     * (decoder.fuelscale: microlitres per count x 1000, default 1000), so L/h = counts per second x 0.0036. The rate is
     * taken over the last 5 s (one sample a second, each step unwrapped on its own); from 5 km/h on
     * (driving.speedActual) it is shown as L/100km. */
    if (cds_value(cds, "sensors.fuel", "range", &x))
        snprintf(a, sizeof(a), I_RANGE "%.0f km", x);
    else
        strcpy(a, I_RANGE "-- km");
    if (cds_value(cds, "sensors.fuel", "tanklevel", &x))
        snprintf(b, sizeof(b), I_CAN "%.0f L", x);
    else
        strcpy(b, I_CAN "-- L");
    {
        enum { SAMPLES = 6 };
        static long long when[SAMPLES];
        static long count[SAMPLES];
        static int n;
        static long scale = -1;
        if (scale < 0)
            scale = read_number(RT_DIR "/decoder.fuelscale", 1000);
        strcpy(c, I_DROP "-- L/h");
        if (cds_value(cds, "engine.consumption", "consumption", &x)) {
            if (n == SAMPLES) {
                memmove(when, when + 1, sizeof(when[0]) * (SAMPLES - 1));
                memmove(count, count + 1, sizeof(count[0]) * (SAMPLES - 1));
                --n;
            }
            when[n] = now_ms();
            count[n] = (long)x;
            ++n;
            long counts = 0;
            for (int i = 1; i < n; ++i)
                counts += (count[i] - count[i - 1]) & 0xffff;
            const double secs = n > 1 ? (when[n - 1] - when[0]) / 1000.0 : 0;
            if (secs >= 2.0) {
                const double lph = counts / secs * 0.0036 * scale / 1000.0;
                double speed = 0;
                if (cds_value(cds, "driving.speedActual", "speedActual", &speed) && speed >= 5)
                    snprintf(c, sizeof(c), lph / speed >= 0.9995 ? I_DROP "%.0f L/100km" : I_DROP "%.1f L/100km",
                             lph / speed * 100.0);   /* 100 and more (pulling away) without the decimal: fits */
                else
                    snprintf(c, sizeof(c), I_DROP "%.1f L/h", lph);
            }
        }
    }
    if (paged) {
        strcpy(pg[5], a);
        strcpy(pg[6], b);
        strcpy(pg[7], c);
    } else {
        /* "833 km  52 L  8.8 L/100km" does not fit at 380 px: the instantaneous consumption reads "L/100" then */
        if (R + panel_width(a) + 16 + panel_width(b) + 12 > L - panel_width(c)) {
            char* km = strstr(c, "L/100km");
            if (km)
                km[5] = 0;
        }
        panel_text_at(a, R, top[2], 0);
        panel_text_at(b, R + panel_width(a) + 16, top[2], 0);   /* tank right after range */
        panel_text_at(c, L, top[2], 2);
    }
    /* 4: outside, coolant, oil temperature, starter battery charge (CDS sensors.battery {"battery":N}, percent as
     * BimmerGestalt's apps read it, "ACC battery"; 255 = not known). Four items spread evenly over the line. */
    char d[40];
    double out_value = 0;
    const int have_out_value = cds_value(cds, "sensors.temperatureExterior", "temperatureExterior", &out_value);
    x = out_value;
    if (have_out_value)
        snprintf(a, sizeof(a), I_THERMO "%.1f\xb0" "C", x);
    else
        strcpy(a, I_THERMO "--\xb0" "C");
    if (cds_value(cds, "engine.temperature", "engine", &x))
        snprintf(b, sizeof(b), I_COOLANT "%.0f\xb0", x);
    else
        strcpy(b, I_COOLANT "--\xb0");
    if (cds_value(cds, "engine.temperature", "oil", &x))
        snprintf(c, sizeof(c), I_OIL "%.0f\xb0", x);
    else
        strcpy(c, I_OIL "--\xb0");
    if (cds_value(cds, "sensors.battery", "battery", &x) && x >= 0 && x < 255)
        snprintf(d, sizeof(d), I_BATTERY "%.0f%%", x);
    else
        strcpy(d, I_BATTERY "--%");
    if (paged) {
        strcpy(pg[8], a);
        strcpy(pg[9], b);
        strcpy(pg[10], c);
        strcpy(pg[11], d);
        panel_pages(pg, top, L, R);
        g_panel.have = 1;
        return;
    }
    const char* items[4] = {a, b, c, d};
    int total = 0;
    for (int i = 0; i < 4; ++i)
        total += panel_width(items[i]);
    if (L - R - total < 3 * 6 && have_out_value) {   /* hot engine and cold outside at 380 px: "-12°C" */
        snprintf(a, sizeof(a), I_THERMO "%.0f\xb0" "C", out_value);
        total = 0;
        for (int i = 0; i < 4; ++i)
            total += panel_width(items[i]);
    }
    {
        const int gap = (L - R - total) / 3;
        int xpos = R;
        for (int i = 0; i < 4; ++i) {
            panel_text_at(items[i], i == 3 ? L : xpos, top[3], i == 3 ? 2 : 0);
            xpos += panel_width(items[i]) + (gap > 4 ? gap : 4);
        }
    }
    g_panel.have = 1;
}

/* coverage of the box with rounded corners (PANEL_RADIUS), and of the same shape 1 px inside it: the difference is
 * the 1 px outline (user 2026-11-23); 4 x 4 samples per pixel */
static uint8_t g_corner[PANEL_W * PANEL_H_MAX], g_inner[PANEL_W * PANEL_H_MAX];
static int shape_samples(int col, int row, double inset)
{
    int in = 0;
    const double r = PANEL_RADIUS - inset;
    for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) {
            const double x = col + (sx + 0.5) / 4, y = row + (sy + 0.5) / 4;
            const int W = g_panel.w, H = g_panel.h;
            if (x < inset || y < inset || x > W - inset || y > H - inset)
                continue;
            const double cx = x < PANEL_RADIUS ? PANEL_RADIUS : x > W - PANEL_RADIUS ? W - PANEL_RADIUS : x;
            const double cy = y < PANEL_RADIUS ? PANEL_RADIUS : y > H - PANEL_RADIUS ? H - PANEL_RADIUS : y;
            in += (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
        }
    return in;
}
static void corner_mask(void)
{
    for (int row = 0; row < g_panel.h; ++row)
        for (int col = 0; col < g_panel.w; ++col) {
            g_corner[row * g_panel.w + col] = (uint8_t)(shape_samples(col, row, 0) * 255 / 16);
            g_inner[row * g_panel.w + col] = (uint8_t)(shape_samples(col, row, 1) * 255 / 16);
        }
}

/* the box as libnmsdk's subpicture, composited by our own stream's video mixer (see the comment above) */
typedef int (*nv_sub_configure_fn)(void*, void*, const uint32_t*);   /* 21 SubPictureConfigure(h, {width, height}) */
typedef int (*nv_sub_palette_fn)(void*, void*, const uint8_t*);      /* 22 SubPicturePalette(h, 16 x {Y, Cr, Cb}) */
typedef int (*nv_sub_frame_fn)(void*, void*, int, const uint8_t*, uint64_t, uint64_t, void*);   /* 23 SubPictureFrame */
typedef int (*nv_sub_abort_fn)(void*, void*);                        /* 24 SubPictureAbort(h) */
enum { NV_SUB_CONFIGURE = 21, NV_SUB_PALETTE = 22, NV_SUB_FRAME = 23, NV_SUB_ABORT = 24 };
/* libnmsdk (2014) stream object: the subpicture's NVVA output surface (OnSubPictureConfigure) and the flag that puts
 * it into VideoRenderFrame's mixer (set by OnSubPictureFrame, cleared by OnSubPictureAbort); read for the log only */
enum { SUB_SURFACE_AT = 0x114, SUB_FLAG_AT = 0x1a8 };

/* background colour and opacity: decoder.panelbg, hex RRGGBBAA (user 2026-11-23: to match Android Auto's dark grey
 * buttons; default 000000B4, translucent black; the car: 323232F0). Read when a stream is set up. Text colour and
 * outline follow from it (sub_box). Grey backgrounds are exact; for coloured ones red and blue may come out swapped
 * (the order of the library's colour table in the surface is not traced). */
static unsigned g_bg_r = 0, g_bg_g = 0, g_bg_b = 0, g_bg_a = 0xB4;

/* The subpicture's palette (16 entries, one for everything in it): 0 = transparent (its colour unused), 1..9 = the
 * trip box, from its background to its text colour (sub_box), 10..15 = the volume popup's own colours (vol_draw). */
enum { SUB_BOX_FIRST = 1, SUB_BOX_STEPS = 9 };
enum { VOL_DARK = 10, VOL_LIGHT, VOL_EDGE, VOL_TRACK, VOL_CREAM, VOL_PALE };
static const unsigned char g_vol_rgb[6][3] = {
    {26, 29, 31},     /* VOL_DARK: the panel */
    {60, 66, 68},     /* VOL_LIGHT: the panel's row below its top edge */
    {180, 190, 192},  /* VOL_EDGE: the bar's outline, the panel's top edge */
    {4, 8, 12},       /* VOL_TRACK: the bar's empty part */
    {250, 246, 196},  /* VOL_CREAM: the bar's fill */
    {252, 253, 246},  /* VOL_PALE: the fill's edges and bright end, the speaker */
};

static struct {
    void* stream;   /* the stream the subpicture belongs to */
    int fw, fh;     /* its size: the stream's buffers */
    int x, y, flip;
    uint8_t* frame;   /* fw x fh, one byte per pixel (alpha << 4 | index), 0 = transparent */
    uint8_t box[PANEL_W * PANEL_H_MAX], last[PANEL_W * PANEL_H_MAX];
    int dirty;    /* the box composed since it was last put into the frame */
    int box_in;   /* the box is in the frame */
    int vol_in;   /* the volume popup is in the frame */
    int live;     /* the frame is in the mixer (uploaded, not aborted) */
    int upload;   /* the frame changed */
} g_sub;

/* the volume popup's state (vol_policy) */
static struct {
    long seq;          /* the last line's number; -1 = none read yet (the first is only the starting point) */
    int level, max, muted, type;
    long long until;   /* shown until (monotonic ms) */
    long long read_at; /* the file is read at most every VOL_POLL_MS */
    int off;           /* decoder.novolume */
    unsigned shown;    /* how many times, for the log */
} g_vol = {-1, 0, 0, 0, 0, 0, 0, 0, 0};

/* a palette entry as OnSubPicturePalette reads it: Y, Cr, Cb (BT.601 video range), each colour channel truncated and
 * stored as a byte without clamping (outside 0-255 it wraps): the nearest entry that stays inside */
static void sub_ycrcb(double r, double g, double b, uint8_t out[3])
{
    const double k = 1 + 0.391 / 2.018 + 0.813 / 1.596;
    const double l = (g + 0.391 * b / 2.018 + 0.813 * r / 1.596) / k;
    const int y0 = (int)(l / 1.164 + 16.5), v0 = (int)((r - l) / 1.596 + 128.5), u0 = (int)((b - l) / 2.018 + 128.5);
    double best = 1e30;
    out[0] = 16;
    out[1] = out[2] = 128;
    for (int y = y0 - 2; y <= y0 + 2; ++y)
        for (int v = v0 - 2; v <= v0 + 2; ++v)
            for (int u = u0 - 2; u <= u0 + 2; ++u) {
                if (y < 0 || y > 255 || v < 0 || v > 255 || u < 0 || u > 255)
                    continue;
                const double ly = 1.164 * (y - 16), R = 1.596 * (v - 128) + ly,
                             G = -0.391 * (u - 128) + ly + -0.813 * (v - 128), B = 2.018 * (u - 128) + ly;
                if (R < 0 || R >= 256 || G < 0 || G >= 256 || B < 0 || B >= 256)
                    continue;
                const double e = ((int)R - r) * ((int)R - r) + ((int)G - g) * ((int)G - g) + ((int)B - b) * ((int)B - b);
                if (e < best) {
                    best = e;
                    out[0] = (uint8_t)y;
                    out[1] = (uint8_t)v;
                    out[2] = (uint8_t)u;
                }
            }
}

/* the drawn box (coverage over PANEL_BG) as palette index and alpha (user 2026-11-23): text and icons white on a dark
 * background, black on a light one (decoder.panelbg's luminance, BT.601); a 1 px outline derived from the background:
 * a quarter of the way towards the text colour, at least as opaque as the background, half way to opaque. Layers,
 * each "over" the one below (straight alpha): background, outline, text; then the rounded corners. Every colour lies
 * between background and text colour: s = 0..1 along that line, palette entries 1..9 = 9 steps of it (panel_open).
 * Byte per pixel: NVVAOutputSurfacePutBitsIndexed with format 1 (as OnSubPictureFrame calls it) builds its table as
 * alpha << 4 | index, alpha 0-15 (libnmsdk 2014). decoder.panelflip: bit 0 rows reversed, bit 1 columns (default 0). */
static void sub_box(void)
{
    const double bga = g_bg_a / 255.0, ola = (1 + bga) / 2;
    const int W = g_panel.w, H = g_panel.h;
    for (int i = 0; i < W * H; ++i) {
        const int row = i / W, col = i % W;
        const int src = ((g_sub.flip & 1) ? H - 1 - row : row) * W + ((g_sub.flip & 2) ? W - 1 - col : col);
        const int d = g_panel.draw[src] - PANEL_BG;
        const double t = d <= 0 ? 0 : d >= PANEL_FG - PANEL_BG ? 1 : (double)d / (PANEL_FG - PANEL_BG);
        const double outer = g_corner[src] / 255.0, inner = g_inner[src] / 255.0;
        const double w = outer > 0 && outer > inner ? (outer - inner) / outer : 0;   /* the outline's share */
        double s = 0, a = bga;
        if (w > 0) {   /* outline over the background */
            const double oa = ola * w, na = oa + a * (1 - oa);
            s = na > 0 ? (0.25 * oa + s * a * (1 - oa)) / na : 0;
            a = na;
        }
        if (t > 0) {   /* text over both */
            const double na = t + a * (1 - t);
            s = na > 0 ? (t + s * a * (1 - t)) / na : 0;
            a = na;
        }
        const int alpha = (int)(a * outer * 15 + 0.5), index = SUB_BOX_FIRST + (int)(s * (SUB_BOX_STEPS - 1) + 0.5);
        g_sub.box[i] = alpha ? (uint8_t)(alpha << 4 | index) : 0;
    }
}

/* The stream's video mixer, as libnmsdk's OnVideoConfigure creates it: NVVAVideoMixerCreate(device, 0, width,
 * height, 0, 0), the last argument the most layers over the video (min(n, 4), mixer byte 0x6c), and
 * NVVAVideoMixerRender fails when given more layers than that (car 2026-11-26: every picture failed with the
 * subpicture on, "ERROR: NVVAVideoMixerRender failed"). So right after VideoConfigure (nothing else is running in the
 * library then: the worker thread is idle, no buffers, no stream, no picture yet) the same mixer with 1 layer replaces
 * it: created with the same arguments, put where the stream keeps it (CVideo + 0x60, which VideoRenderFrame and
 * CVideo::Destroy use), the old one destroyed. Its per-layer arrays are sized at creation ((2 x layers + 3) entries),
 * so the old mixer's limit cannot simply be raised in place. Only when the box can be shown at all. */
enum { NV_DEVICE_AT = 0x1e0, CVIDEO_MIXER_AT = 0x60 };
typedef void* (*nvva_mixer_create_fn)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void (*nvva_destroy_fn)(void*);
static int g_sub_mixer;   /* the stream has our mixer with a layer */

static int panel_possible(int w, int out_h)
{
    return g_opt.width >= PANEL_X + PANEL_W && w == g_opt.width && out_h == g_opt.height;
}

static void sub_mixer(int w, int h, int out_h)
{
    g_sub_mixer = 0;
    if (!panel_possible(w, out_h))
        return;
    nvva_mixer_create_fn create = (nvva_mixer_create_fn)dlsym(g_car.lib, "NVVAVideoMixerCreate");
    nvva_destroy_fn destroy = (nvva_destroy_fn)dlsym(g_car.lib, "NVVADeviceObjectDestroy");
    void* const device = *(void* const*)((const uint8_t*)g_car.nv + NV_DEVICE_AT);
    void** const slot = (void**)((uint8_t*)g_car.stream + CVIDEO_MIXER_AT);
    void* const old = *slot;
    if (!create || !destroy || !device || !old) {
        say("panel: no mixer with a layer (%s), no box", !create || !destroy ? "symbols" : "no device or mixer");
        return;
    }
    void* const mixer = create(device, 0, (uint32_t)w, (uint32_t)h, 0, 1);
    if (!mixer) {
        say("panel: NVVAVideoMixerCreate with 1 layer failed, no box");
        return;
    }
    *slot = mixer;
    destroy(old);
    g_sub_mixer = 1;
    say("panel: the stream's mixer %p replaced by %p (1 layer, %dx%d)", old, mixer, w, h);
}

/* after a stream is set up: the subpicture as large as its buffers, the palette, the box's settings */
static void panel_open(void)
{
    if (g_sub.stream == g_car.stream)
        return;
    g_sub.stream = g_car.stream;
    g_panel.ok = 0;
    g_panel.shown = 0;
    g_sub_live = g_sub_failed = 0;
    g_sub.box_in = g_sub.vol_in = g_sub.live = g_sub.upload = 0;
    g_vol.seq = -1;
    if (g_opt.width < PANEL_X + PANEL_W) {
        say("panel: window only %d wide (split screen), no box", g_opt.width);
        return;
    }
    if (!panel_possible(g_car.w, g_car.out_h)) {   /* the box would be scaled with the picture */
        say("panel: stream buffers %dx%d, window %dx%d: no box", g_car.w, g_car.out_h, g_opt.width, g_opt.height);
        return;
    }
    if (!g_sub_mixer)
        return;
    {
        char size[16] = "";
        panel_read(RT_DIR "/decoder.panelsize", size, sizeof(size));
        const int trip = !strncmp(size, "trip", 4), car = !strncmp(size, "car", 3);
        const int small = !trip && !car && strncmp(size, "full", 4) != 0;   /* the default */
        g_panel.paged = small || trip || car;
        g_panel.pages = small ? 2 : 1;
        g_panel.page = car ? 2 : small && access(PANEL_PAGE2_FILE, F_OK) == 0 ? 2 : 1;
    }
    g_panel.w = g_panel.paged ? PANEL_PAGED_W : PANEL_W;
    g_panel.h = g_panel.paged ? PANEL_PAGED_H : PANEL_H;
    g_sub.x = (int)read_number(RT_DIR "/decoder.panelx", PANEL_X + PANEL_W - g_panel.w);
    g_sub.y = (int)read_number(RT_DIR "/decoder.panely", PANEL_Y);
    g_sub.flip = (int)read_number(RT_DIR "/decoder.panelflip", 0);
    {
        FILE* f = fopen(RT_DIR "/decoder.panelbg", "r");
        unsigned v;
        if (f) {
            if (fscanf(f, "%x", &v) == 1) {
                g_bg_r = v >> 24 & 255;
                g_bg_g = v >> 16 & 255;
                g_bg_b = v >> 8 & 255;
                g_bg_a = v & 255;
            }
            fclose(f);
        }
    }
    corner_mask();
    free(g_sub.frame);
    g_sub.fw = g_car.w;
    g_sub.fh = g_car.out_h;
    g_sub.frame = (uint8_t*)calloc((size_t)g_sub.fw * g_sub.fh, 1);
    if (!g_sub.frame) {
        say("panel: out of memory");
        return;
    }
    /* palette: see g_vol_rgb */
    const unsigned luma = (299 * g_bg_r + 587 * g_bg_g + 114 * g_bg_b) / 1000;
    const double fg = luma > 128 ? 0 : 255, n = SUB_BOX_STEPS - 1;
    uint8_t palette[48];
    sub_ycrcb(0, 0, 0, palette);
    for (int k = 0; k < SUB_BOX_STEPS; ++k)
        sub_ycrcb(g_bg_r + (fg - g_bg_r) * k / n, g_bg_g + (fg - g_bg_g) * k / n, g_bg_b + (fg - g_bg_b) * k / n,
                  palette + 3 * (SUB_BOX_FIRST + k));
    for (int k = 0; k < 6; ++k)
        sub_ycrcb(g_vol_rgb[k][0], g_vol_rgb[k][1], g_vol_rgb[k][2], palette + 3 * (VOL_DARK + k));
    void* nv = g_car.nv;
    const uint32_t conf[2] = {(uint32_t)g_sub.fw, (uint32_t)g_sub.fh};
    const int r1 = ((nv_sub_configure_fn)nv_method(nv, NV_SUB_CONFIGURE))(nv, g_car.stream, conf);
    const int r2 = ((nv_sub_palette_fn)nv_method(nv, NV_SUB_PALETTE))(nv, g_car.stream, palette);
    void* const surface = *(void* const*)((const uint8_t*)g_car.stream + SUB_SURFACE_AT);
    const char* const kind = !g_panel.paged      ? ""
                             : g_panel.pages == 2 ? " (small)"
                             : g_panel.page == 2  ? " (car only)"
                                                  : " (trip only)";
    say("panel: subpicture %dx%d (configure 0x%08x, palette 0x%08x, surface %p), box %dx%d%s at %d,%d, background "
        "%02x%02x%02x%02x", g_sub.fw, g_sub.fh, r1, r2, surface, g_panel.w, g_panel.h, kind, g_sub.x, g_sub.y, g_bg_r,
        g_bg_g, g_bg_b, g_bg_a);
    if (r1 < 0 || r2 < 0 || !surface)
        return;
    g_panel.ok = 1;
    g_panel.text_at = 0;
    g_vol.off = access(RT_DIR "/decoder.novolume", F_OK) == 0;
}

/* a w x h block of the frame at x, y: from src (rows of w), or cleared (src 0); clipped to the frame */
static void sub_put(int x, int y, int w, int h, const uint8_t* src)
{
    for (int row = 0; row < h; ++row) {
        const int fy = y + row;
        if (fy < 0 || fy >= g_sub.fh)
            continue;
        for (int col = 0; col < w; ++col) {
            const int fx = x + col;
            if (fx >= 0 && fx < g_sub.fw)
                g_sub.frame[(size_t)fy * g_sub.fw + fx] = src ? src[row * w + col] : 0;
        }
    }
    g_sub.upload = 1;
}

/* the frame into the mixer: uploaded while anything is in it (the first upload after an abort also puts it back into
 * the mixer), taken out of the mixer (SubPictureAbort) when nothing is */
static void sub_commit(void)
{
    if (!g_sub.upload)
        return;
    g_sub.upload = 0;
    if (!g_sub.box_in && !g_sub.vol_in) {
        if (g_sub.live) {
            g_sub.live = g_sub_live = 0;
            const int r = ((nv_sub_abort_fn)nv_method(g_car.nv, NV_SUB_ABORT))(g_car.nv, g_car.stream);
            say("panel: nothing to show, out of the picture (SubPictureAbort 0x%08x)", r);
        }
        return;
    }
    const long long t0 = now_ms();
    /* shown from time 0 to the end of time: VideoRenderFrame blends it while start <= now <= end */
    const int r = ((nv_sub_frame_fn)nv_method(g_car.nv, NV_SUB_FRAME))(g_car.nv, g_car.stream, 0, g_sub.frame, 0,
                                                                       UINT64_MAX, 0);
    const long long dt = now_ms() - t0;
    if (r < 0 || dt > 200)
        say("panel: SubPictureFrame 0x%08x after %lld ms", r, dt);
    if (r < 0)
        return;
    if (!g_sub.live)
        say("panel: in the picture (subpicture flag %d)", *((const uint8_t*)g_car.stream + SUB_FLAG_AT));
    g_sub.live = g_sub_live = 1;
}

/* the box into the frame if its picture changed */
static void box_put(void)
{
    sub_box();
    const size_t n = (size_t)g_panel.w * g_panel.h;
    if (g_sub.box_in && !memcmp(g_sub.box, g_sub.last, n))
        return;
    memcpy(g_sub.last, g_sub.box, n);
    sub_put(g_sub.x, g_sub.y, g_panel.w, g_panel.h, g_sub.box);
    if (!g_sub.box_in)
        say("panel: box shown");
    g_sub.box_in = 1;
    g_panel.shown = 1;
}

static void box_take(void)
{
    if (!g_sub.box_in)
        return;
    sub_put(g_sub.x, g_sub.y, g_panel.w, g_panel.h, 0);
    g_sub.box_in = 0;
    g_panel.shown = 0;
    say("panel: box hidden");
}

/* ---- the volume popup (user 2026-11-27) ----
 * iDrive's own volume popup is behind Android Auto's / CarPlay's picture, so ours is drawn into it: the stock look
 * (photo of the car's screen, the user's, 2026-11-27: a dark translucent panel with a light top edge, a speaker, a
 * dark bar with a light outline and a cream fill with a bright end, below it the audio's name: "Entertainment"
 * (user 2026-11-27) where the photo had "Navigation"). 604 x 84, centred, its middle at 2/3 of the height. Sizes
 * measured on that photo (a camera picture of the screen, so to a pixel or two); the name in our font (DejaVu Sans
 * 22 px, made bold by drawing it twice, 1 px apart), at the bar's left edge as on the photo.
 * The level comes from /fs/ram3/nbt-aa-volume, one line "<number> <level> <maximum> <muted 0/1> <type>", written by
 * nbt_knob from the audio master's volume status (MOST 20/00 C08) on every step (the number counts up, so the same
 * level again, at the maximum, shows the popup too). Type 1 (the only one seen) = "Entertainment", others "Volume".
 * By hand: echo "1 12 40 0 1" > /fs/ram3/nbt-aa-volume
 * The first line read is only the starting point. Shown for VOL_SHOW_MS after the last change (the stock timing is
 * not measured), only while Android Auto's window is shown (otherwise iDrive's own popup is on screen).
 * decoder.novolume (file in the package): never shown. */
#define VOL_FILE "/fs/ram3/nbt-aa-volume"
enum { VOL_W = 604, VOL_H = 84, VOL_SHOW_MS = 3000, VOL_POLL_MS = 50 };
enum { VOL_ICON_X = 48, VOL_ICON_W = 22, VOL_ICON_H = 26 };
enum { VOL_BAR_X = 108, VOL_BAR_Y = 11, VOL_BAR_W = VOL_W - VOL_BAR_X - 50, VOL_BAR_H = 22, VOL_GLOW = 12 };
enum { VOL_TEXT_TOP = 40 };   /* the name's glyph box (its capitals from about 5 px lower) */
enum { VOL_RADIUS = 3, VOL_ALPHA = 12 };   /* the panel's opacity (of 15): the map shows through, as on the photo */
static uint8_t g_vol_px[VOL_W * VOL_H];
static uint8_t g_vol_text[VOL_W * VOL_H];   /* the name's coverage */

/* the name into g_vol_text: bold = each glyph twice, the second 1 px to the right */
static void vol_text(const char* t, int x, int top)
{
    memset(g_vol_text, 0, sizeof(g_vol_text));
    for (; *t; ++t) {
        const int g = panel_glyph((unsigned char)*t);
        const unsigned char* bits = panel_bits + panel_glyphs[g].offset;
        const int gw = panel_glyphs[g].width;
        for (int pass = 0; pass < 2; ++pass) {
            const int gx = x + panel_glyphs[g].left + pass;
            for (int row = 0; row < PANEL_FONT_HEIGHT; ++row)
                for (int col = 0; col < gw; ++col) {
                    const int px = gx + col, py = top + row;
                    if (px < 0 || px >= VOL_W || py < 0 || py >= VOL_H)
                        continue;
                    uint8_t* d = &g_vol_text[py * VOL_W + px];
                    if (bits[row * gw + col] > *d)
                        *d = bits[row * gw + col];
                }
        }
        x += panel_glyphs[g].advance;
    }
}

static int vol_x(void)
{
    return (g_sub.fw - VOL_W) / 2;
}

static int vol_y(void)
{
    return g_sub.fh * 2 / 3 - VOL_H / 2;
}

/* squared distance of the point from the segment a-b */
static double seg_dist2(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
    double t = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const double ex = ax + t * dx - px, ey = ay + t * dy - py;
    return ex * ex + ey * ey;
}

/* the speaker: a magnet (a small box) and a cone, 2 px outlines, as the stock icon; muted: a slash across */
static int vol_speaker(double x, double y, int muted)
{
    static const double pts[][2] = {{1.5, 8.5}, {7.5, 8.5}, {19.5, 1.5}, {19.5, 24.5}, {7.5, 17.5}, {1.5, 17.5}};
    const int n = (int)(sizeof(pts) / sizeof(pts[0]));
    for (int i = 0; i < n; ++i) {
        const double* a = pts[i];
        const double* b = pts[(i + 1) % n];
        if (seg_dist2(x, y, a[0], a[1], b[0], b[1]) <= 1.0)
            return 1;
    }
    if (seg_dist2(x, y, 7.5, 8.5, 7.5, 17.5) <= 1.0)   /* where the magnet meets the cone */
        return 1;
    return muted && seg_dist2(x, y, -1, 26, 22, 0) <= 1.21;
}

/* rounded corners: coverage of the pixel, 4 x 4 samples */
static int vol_cover(int col, int row)
{
    int in = 0;
    for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) {
            const double x = col + (sx + 0.5) / 4, y = row + (sy + 0.5) / 4;
            const double cx = x < VOL_RADIUS ? VOL_RADIUS : x > VOL_W - VOL_RADIUS ? VOL_W - VOL_RADIUS : x;
            const double cy = y < VOL_RADIUS ? VOL_RADIUS : y > VOL_H - VOL_RADIUS ? VOL_H - VOL_RADIUS : y;
            in += (x - cx) * (x - cx) + (y - cy) * (y - cy) <= VOL_RADIUS * VOL_RADIUS;
        }
    return in;
}

/* the popup for level / max into g_vol_px (alpha << 4 | index) */
static void vol_draw(int level, int max, int muted, int type)
{
    static const unsigned char bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const int by = VOL_BAR_Y, iy = VOL_BAR_Y + (VOL_BAR_H - VOL_ICON_H) / 2;
    vol_text(type == 1 ? "Entertainment" : "Volume", VOL_BAR_X, VOL_TEXT_TOP);
    const int inner = VOL_BAR_W - 4;   /* outline and a 1 px gap on each side */
    int fill = max > 0 ? (int)((double)inner * (level < 0 ? 0 : level > max ? max : level) / max + 0.5) : 0;
    for (int row = 0; row < VOL_H; ++row)
        for (int col = 0; col < VOL_W; ++col) {
            const int cover = vol_cover(col, row);
            int index, alpha = VOL_ALPHA;
            /* the panel: a light top edge, a lighter row below it (a bevel), then the panel's colour */
            index = row == 1 ? VOL_LIGHT : VOL_DARK;
            if (row == 0) {
                index = VOL_EDGE;
                alpha = 9;
            }
            const int bx = col - VOL_BAR_X, bry = row - by;
            if (bx >= 0 && bx < VOL_BAR_W && bry >= 0 && bry < VOL_BAR_H) {   /* the bar */
                alpha = 15;
                if (bx == 0 || bx == VOL_BAR_W - 1 || bry == 0 || bry == VOL_BAR_H - 1)
                    index = VOL_EDGE;
                else if (bx >= 2 && bx < 2 + fill && bry >= 2 && bry < VOL_BAR_H - 2) {
                    /* cream, its top and bottom rows and its end paler; the end fades in (dithered) */
                    const int end = 2 + fill - bx;   /* 1 = the last column */
                    const int glow = end <= VOL_GLOW && bayer[row & 3][col & 3] * VOL_GLOW >= (end - 1) * 16;
                    index = bry == 2 || bry == VOL_BAR_H - 3 || end <= 2 || glow ? VOL_PALE : VOL_CREAM;
                }
                else
                    index = VOL_TRACK;
            }
            const int ix = col - VOL_ICON_X, iry = row - iy;
            if (ix >= -2 && ix < VOL_ICON_W + 2 && iry >= -2 && iry < VOL_ICON_H + 2 && vol_speaker(ix + 0.5, iry + 0.5, muted)) {
                index = VOL_PALE;
                alpha = 15;
            }
            /* the name: its coverage in the popup's own colours, darkest to lightest (no colour in between) */
            const unsigned tc = g_vol_text[row * VOL_W + col];
            if (tc >= 40) {
                index = tc < 110 ? VOL_LIGHT : tc < 190 ? VOL_EDGE : VOL_PALE;
                alpha = 15;
            }
            alpha = alpha * cover / 16;
            g_vol_px[row * VOL_W + col] = alpha ? (uint8_t)(alpha << 4 | index) : 0;
        }
}

/* every window policy round: a new line in VOL_FILE shows the popup, VOL_SHOW_MS after the last one it goes */
static void vol_policy(long long now)
{
    if (g_vol.off)
        return;
    if (now >= g_vol.read_at) {
        g_vol.read_at = now + VOL_POLL_MS;
        char line[64] = "";
        long seq;
        int level, max, muted = 0, type = 1;
        FILE* f = fopen(VOL_FILE, "r");
        if (f) {
            if (!fgets(line, sizeof(line), f))
                line[0] = 0;
            fclose(f);
        }
        if (sscanf(line, "%ld %d %d %d %d", &seq, &level, &max, &muted, &type) >= 3 && seq != g_vol.seq) {
            const int first = g_vol.seq == -1;
            g_vol.seq = seq;
            if (!first && g_win.shown == 1) {
                if (g_vol.shown++ < 20)
                    say("volume: %d of %d%s", level, max, muted ? ", muted" : "");
                if (!g_sub.vol_in || level != g_vol.level || max != g_vol.max || muted != g_vol.muted ||
                    type != g_vol.type) {
                    vol_draw(level, max, muted, type);
                    sub_put(vol_x(), vol_y(), VOL_W, VOL_H, g_vol_px);
                }
                g_vol.level = level;
                g_vol.max = max;
                g_vol.muted = muted;
                g_vol.type = type;
                g_vol.until = now + VOL_SHOW_MS;
                g_sub.vol_in = 1;
            }
        }
    }
    if (g_sub.vol_in && now >= g_vol.until) {
        sub_put(vol_x(), vol_y(), VOL_W, VOL_H, 0);
        g_sub.vol_in = 0;
    }
}

/* every window policy round (main thread). The box: drawn every second (the short-term average speed integrates
 * there); put into the picture only while Android Auto's window is shown, it hides and shows with that window by
 * itself (it is part of the window's picture); long NAV (/fs/ram3/nbt-aa-panel-off) hides it. The volume popup:
 * vol_policy. One upload for both. */
static void panel_policy(void)
{
    if (!g_panel.ok)
        return;
    if (g_sub_failed) {   /* the picture first: nothing more in it for the rest of this program */
        say("panel: VideoRenderFrame failed with the subpicture in the picture, box and volume off");
        g_sub.box_in = g_sub.vol_in = 0;
        g_sub.upload = 1;
        sub_commit();
        g_panel.ok = 0;
        return;
    }
    const long long now = now_ms();
    const int want = access(PANEL_OFF_FILE, F_OK) != 0;
    if (g_panel.pages == 2) {   /* another page (long NAV): drawn and uploaded at once */
        const int page = access(PANEL_PAGE2_FILE, F_OK) == 0 ? 2 : 1;
        if (page != g_panel.page) {
            g_panel.page = page;
            g_panel.text_at = 0;
            say("panel: page %d", page);
        }
    }
    if (now >= g_panel.text_at) {
        g_panel.text_at = now + 1000;
        panel_compose();
        g_sub.dirty = 1;
    }
    if (!want)
        box_take();
    else if (g_win.shown == 1 && g_panel.have && g_sub.dirty) {
        g_sub.dirty = 0;
        box_put();
    }
    vol_policy(now);
    sub_commit();
}

/* ---- display thread ---- */

/* the newest picture; with nothing new for repeat_ms while the window is shown, the last one again: the car fades the
 * window out when no picture comes for a short while (car 2026-11-19: YouTube Music, ~6 pictures/s, the picture and
 * iDrive's page alternating about twice a second; Waze at ~30/s was steady) */
static void* display_thread(void* arg)
{
    (void)arg;
    int have = 0;
    for (;;) {
        pthread_mutex_lock(&g_box.lock);
        int repeat = 0;
        while (!g_box.fresh && !g_quit && !repeat) {
            if (!have || g_win.shown != 1 || g_opt.repeat_ms <= 0) {
                pthread_cond_wait(&g_box.posted, &g_box.lock);
                continue;
            }
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_nsec += g_opt.repeat_ms % 1000 * 1000000;
            until.tv_sec += g_opt.repeat_ms / 1000 + until.tv_nsec / 1000000000;
            until.tv_nsec %= 1000000000;
            if (pthread_cond_timedwait(&g_box.posted, &g_box.lock, &until) == ETIMEDOUT && !g_box.fresh)
                repeat = 1;
        }
        if (g_quit) {
            pthread_mutex_unlock(&g_box.lock);
            return 0;
        }
        if (!repeat) {
            const int take = g_box.newest;
            g_box.newest = g_box.showing;
            g_box.showing = take;
            g_box.fresh = 0;
            have = 1;
            if (g_win.shown != 1)
                ++g_stat.hidden;
        }
        const int show = g_box.showing;
        pthread_mutex_unlock(&g_box.lock);
        if (g_win.shown == 1)
            car_present(&g_box.pic[show], repeat);
    }
}

static void post_picture(void)
{
    static int started;
    pthread_mutex_lock(&g_box.lock);
    const int done = g_box.writing;
    g_box.writing = g_box.newest;
    g_box.newest = done;
    if (g_box.fresh)
        ++g_stat.replaced;
    g_box.fresh = 1;
    pthread_cond_signal(&g_box.posted);
    pthread_mutex_unlock(&g_box.lock);
    if (!started) {
        started = 1;
        if (pthread_create(&g_display, 0, display_thread, 0) != 0) {
            say("display thread could not start");
            g_quit = 1;
        } else
            g_display_started = 1;
    }
}

/* ---- window policy ---- */

static int session_running(long long now)
{
    if (now - g_win.state_read >= 500) {
        g_win.state_read = now;
        int running = 0;
        struct stat st;
        if (stat(STATE_FILE, &st) == 0 && time(0) - st.st_mtime <= 5) {
            char text[200] = "";
            const int fd = open(STATE_FILE, O_RDONLY);
            if (fd >= 0) {
                const ssize_t n = read(fd, text, sizeof(text) - 1);
                text[n > 0 ? n : 0] = 0;
                close(fd);
            }
            running = strstr(text, "state=running") != 0;
        }
        if (running) {
            g_win.stopped_since = 0;
            if (!g_win.running)
                say("session: running");
            g_win.running = 1;
        } else {
            if (!g_win.stopped_since)
                g_win.stopped_since = now;
            if (g_win.running && now - g_win.stopped_since >= 1500) {
                say("session: not running, window hidden");
                g_win.running = 0;
            }
        }
    }
    return g_win.running;
}

static void window_policy(void)
{
    const long long now = now_ms();
    /* a picture of this session first: the stream still holds the previous session's last one */
    int show = g_win.last_picture && access(HIDDEN_FILE, F_OK) != 0 && session_running(now);
    if (show && g_opt.stale_ms && now - g_win.last_picture >= g_opt.stale_ms) {
        if (!g_win.stale_said)
            say("window: no picture for %ld ms, hidden", g_opt.stale_ms);
        g_win.stale_said = 1;
        show = 0;
    }
    if (show)
        g_win.hide_from = 0;
    else {
        if (!g_win.hide_from)
            g_win.hide_from = now + 400;
        if (g_win.shown == 1 && now < g_win.hide_from)
            show = 1;   /* not yet */
    }
    if (!g_car.ready || (show == g_win.shown && now < g_win.refresh_at)) {
        panel_policy();
        return;
    }
    if (show != g_win.shown)
        say("window: %s", show ? "shown" : "hidden");
    window_set(show);
    g_win.shown = show;
    g_win.refresh_at = now + 2000;
    panel_policy();
}

/* ---- decoding ---- */

static void stats_maybe(void)
{
    const long long now = now_ms();
    if (!g_stat.since)
        g_stat.since = now;
    if (now - g_stat.since < 5000)
        return;
    pthread_mutex_lock(&g_box.lock);
    const double secs = (now - g_stat.since) / 1000.0;
    const unsigned long presents = g_stat.shown + g_stat.repeated + g_stat.present_errors;
    if (g_stat.received || g_stat.errors)
        say("stats %.1f s: received %lu (%.1f/s), decoded %lu (%.1f/s), shown %lu (%.1f/s) + %lu repeats, %lu replaced, "
            "%lu while hidden, %lu decode / %lu show errors; decode %.1f ms per call (max %.1f), show %.1f ms (max %.1f)",
            secs, g_stat.received, g_stat.received / secs, g_stat.decoded, g_stat.decoded / secs, g_stat.shown,
            g_stat.shown / secs, g_stat.repeated, g_stat.replaced, g_stat.hidden, g_stat.errors, g_stat.present_errors,
            g_stat.received ? g_stat.decode_us / 1000.0 / g_stat.received : 0.0, g_stat.decode_max / 1000.0,
            presents ? g_stat.upload_us / 1000.0 / presents : 0.0, g_stat.upload_max / 1000.0);
    if (g_hw.active)
        say("hw: %lu pictures decoded by the car, %lu errors so far", g_hw.pictures, g_hw.errors);
    const unsigned long total = g_stat.total;
    memset(&g_stat, 0, sizeof(g_stat));
    g_stat.total = total;
    g_stat.since = now;
    pthread_mutex_unlock(&g_box.lock);
}

/* ---- the car's hardware decoder (--hw) ---- */

/* decoding by the car does not work out after all: note it for native_service (no --hw for the next sessions) and
 * end, the session ends with us (switching to the CPU in the middle of a stream would need a new car stream) */
static void hw_give_up(const char* why)
{
    say("hw: %s, giving up: decoder.hwfail written, the next session decodes on the CPU", why);
    const int fd = open("decoder.hwfail", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (write(fd, why, strlen(why)) < 0) {
        }
        close(fd);
    }
    car_close();
    _exit(3);
}

/* a reference picture's row in the reference table: its handle, once the car has a surface for it */
static int hw_row(VdpVideoSurface handle)
{
    return handle >= 1 && handle <= HW_HANDLES && g_hw.surf[handle] ? (int)handle : -1;
}

/* a picture the car did not decode; gives up when it never worked or keeps failing */
static VdpStatus hw_failed(VdpStatus status)
{
    ++g_hw.errors;
    ++g_hw.in_a_row;
    if (g_hw.in_a_row >= HW_GIVE_UP && !g_hw.pictures)
        hw_give_up("VideoDecode never worked");
    if (g_hw.in_a_row >= 10 * HW_GIVE_UP)
        hw_give_up("VideoDecode keeps failing");
    return status;
}

/* FFmpeg's VDPAU render call: one picture (its slices, each after 00 00 01) into the surface of handle target */
static VdpStatus hw_render(VdpDecoder decoder, VdpVideoSurface target, VdpPictureInfo const* info, uint32_t count,
                           VdpBitstreamBuffer const* buffers)
{
    (void)decoder;
    const VdpPictureInfoH264* pic = (const VdpPictureInfoH264*)info;
    static struct { const void* data; uint32_t size; } list[512];
    static int said_scaling;
    if (target < 1 || target > HW_HANDLES || count > 512)
        return VDP_STATUS_INVALID_HANDLE;
    if (!hw_flat_scaling(pic)) {
        if (!said_scaling)
            say("hw: the stream uses its own scaling matrices, which this hand-over does not carry");
        said_scaling = 1;
        hw_give_up("own scaling matrices");
    }
    if (hw_bsp_fill(g_hw.bsp, pic, hw_row) < 0) {
        if (g_hw.in_a_row < 3 || g_hw.in_a_row % 50 == 0)
            say("hw: a reference picture without a surface (picture %lu)", g_hw.pictures);
        return hw_failed(VDP_STATUS_INVALID_HANDLE);
    }
    for (uint32_t i = 0; i < count; ++i) {
        list[i].data = buffers[i].bitstream;
        list[i].size = buffers[i].bitstream_bytes;
    }
    for (int i = 0; i <= HW_HANDLES; ++i) {
        g_hw.table[2 * i] = g_hw.surf[i];
        g_hw.table[2 * i + 1] = 0;
    }
    void* surface = g_hw.surf[target] ? g_hw.surf[target] : (void*)-1;
    g_hw.pts += 1000000 / g_opt.fps;
    const long long t0 = now_us();
    const int r = ((nv_decode_fn)nv_method(g_car.nv, NV_DECODE))(g_car.nv, g_car.stream, list, count, g_hw.pts, 0,
                                                                g_hw.bsp, g_hw.table, HW_HANDLES + 1, &surface);
    const long long dt = now_us() - t0;
    if (r < 0 || !surface || surface == (void*)-1) {
        if (g_hw.in_a_row < 3 || g_hw.in_a_row % 50 == 0)
            say("hw: VideoDecode failed 0x%08x after %lld ms (picture %lu, handle %u, %u buffers, %d slices)", r,
                dt / 1000, g_hw.pictures, (unsigned)target, (unsigned)count, (int)pic->slice_count);
        return hw_failed(VDP_STATUS_ERROR);
    }
    if (!g_hw.pictures)
        say("hw: first picture decoded by the car in %lld ms (surface %p, %d slices, frame_num %u, %s)", dt / 1000,
            surface, (int)pic->slice_count, (unsigned)pic->frame_num, pic->is_reference ? "reference" : "not a reference");
    if (!g_hw.surf[target])
        say("hw: handle %u = surface %p", (unsigned)target, surface);
    g_hw.surf[target] = surface;
    g_hw.in_a_row = 0;
    ++g_hw.pictures;
    return VDP_STATUS_OK;
}

static void hw_release(void* opaque, uint8_t* data)
{
    (void)data;
    g_hw.in_use[(intptr_t)opaque] = 0;
}

static int hw_get_buffer(AVCodecContext* avctx, AVFrame* frame, int flags)
{
    if (frame->format != AV_PIX_FMT_VDPAU)
        return avcodec_default_get_buffer2(avctx, frame, flags);
    for (int h = 1; h <= HW_HANDLES; ++h) {
        if (g_hw.in_use[h])
            continue;
        frame->buf[0] = av_buffer_create(&g_hw.token[h], 1, hw_release, (void*)(intptr_t)h, 0);
        if (!frame->buf[0])
            return AVERROR(ENOMEM);
        g_hw.in_use[h] = 1;
        frame->data[3] = (uint8_t*)(intptr_t)h;
        return 0;
    }
    say("hw: all %d surfaces in use", HW_HANDLES);
    return AVERROR(ENOMEM);
}

/* FFmpeg asks for the output format once it knows the stream (SPS): the car decodes it if it takes the decoder */
static enum AVPixelFormat hw_get_format(AVCodecContext* avctx, const enum AVPixelFormat* formats)
{
    const enum AVPixelFormat* f = formats;
    while (*f != AV_PIX_FMT_NONE && *f != AV_PIX_FMT_VDPAU)
        ++f;
    if (*f == AV_PIX_FMT_VDPAU && !g_hw.failed && !g_opt.bench) {
        const int w = avctx->coded_width, h = avctx->coded_height;
        if (g_car.ready && g_hw.active && (w != g_car.w || h != g_car.h)) {
            say("hw: the stream changed to %dx%d (the car decodes %dx%d)", w, h, g_car.w, g_car.h);
            hw_give_up("picture size changed");
        }
        if (!g_car.ready) {
            const int rows = shown_rows(avctx->height);
            const struct rect source = {0, 0, (uint32_t)avctx->width, (uint32_t)rows};
            /* only some rows shown (--rows): buffers of just those rows, so they are copied 1:1 and the window shows
             * them 1:1; with buffers of the whole coded height they are scaled up into them and down again by the
             * window (--hw-scale, and the fallback if the car refuses the smaller buffers) */
            const int direct = rows < h && !g_opt.hwscale;
            const struct rect destination = {0, 0, (uint32_t)w, (uint32_t)(direct ? rows : h)};
            const struct rect whole = {0, 0, (uint32_t)w, (uint32_t)h};
            say("hw: stream %dx%d (picture %dx%d, %d rows shown), profile %d level %d: asking the car to decode it", w, h,
                avctx->width, avctx->height, rows, avctx->profile, avctx->level);
            int r = car_stream_with(w, h, DECODER_H264, HW_HANDLES, &source, &destination, direct ? rows : h);
            if (r != 0 && direct) {
                say("hw: the car refused %dx%d buffers, trying %dx%d (the rows scaled twice)", w, rows, w, h);
                r = car_stream_with(w, h, DECODER_H264, HW_HANDLES, &source, &whole, h);
            }
            if (r != 0) {
                say("hw: the car did not take the decoder, decoding on the CPU");
                g_hw.failed = 1;
                g_car.ready = 0;
            } else
                g_hw.active = 1;
        }
        if (g_hw.active)
            return AV_PIX_FMT_VDPAU;
    }
    while (*formats != AV_PIX_FMT_NONE && (av_pix_fmt_desc_get(*formats)->flags & AV_PIX_FMT_FLAG_HWACCEL))
        ++formats;
    return *formats;
}

static int decoder_open(void)
{
    avcodec_register_all();
    AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    g_dec = codec ? avcodec_alloc_context3(codec) : 0;
    g_frame = av_frame_alloc();
    if (!g_dec || !g_frame)
        return -1;
    g_dec->thread_count = 1;
    g_dec->flags |= AV_CODEC_FLAG_LOW_DELAY;   /* a picture out for every picture in */
    if (g_opt.hw) {
        g_hw.ctx = av_vdpau_alloc_context();
        if (!g_hw.ctx)
            return -1;
        g_hw.ctx->decoder = 1;   /* "created by the application": FFmpeg only calls render */
        g_hw.ctx->render = hw_render;
        g_dec->hwaccel_context = g_hw.ctx;
        g_dec->get_format = hw_get_format;
        g_dec->get_buffer2 = hw_get_buffer;
        g_dec->refcounted_frames = 1;   /* the frames we hold keep their surface (decode() unrefs g_frame) */
        for (int i = 0; i < 3; ++i)
            if (!(g_box.pic[i].hw = av_frame_alloc()))
                return -1;
    }
    if (avcodec_open2(g_dec, codec, 0) < 0)
        return -1;
    const int cpu = av_get_cpu_flags();
    say("decoder: FFmpeg %s H.264, SIMD%s%s%s", av_version_info(), cpu & AV_CPU_FLAG_SSE2 ? " SSE2" : "",
        cpu & AV_CPU_FLAG_SSSE3 ? " SSSE3" : "", cpu & AV_CPU_FLAG_SSE4 ? " SSE4.1" : "");
    return 0;
}

/* I420 -> NV12 into the picture being written */
static void to_nv12(const AVFrame* f, struct picture* p)
{
    const int h = shown_rows(f->height);
    for (int row = 0; row < h; ++row)
        memcpy(p->y + (size_t)row * g_car.w, f->data[0] + (size_t)row * f->linesize[0], (size_t)f->width);
    for (int row = 0; row < h / 2; ++row) {
        const uint8_t* u = f->data[1] + (size_t)row * f->linesize[1];
        const uint8_t* v = f->data[2] + (size_t)row * f->linesize[2];
        uint8_t* out = p->uv + (size_t)row * g_car.w;
        for (int x = 0; x < f->width / 2; ++x) {
            out[0] = u[x];
            out[1] = v[x];
            out += 2;
        }
    }
}

static void picture_out(const AVFrame* f);

static void decode(uint8_t* data, size_t size)
{
    if (!g_dec && decoder_open() < 0) {
        say("decoder: FFmpeg H.264 not available");
        g_quit = 1;
        return;
    }
    memset(data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = data;
    pkt.size = (int)size;
    int got = 0;
    ++g_stat.received;
    const long long t0 = now_us();
    const int r = avcodec_decode_video2(g_dec, g_frame, &got, &pkt);
    const long long dt = now_us() - t0;
    g_stat.decode_us += dt;
    if (dt > g_stat.decode_max)
        g_stat.decode_max = dt;
    if (r < 0) {
        ++g_stat.errors;
        return;
    }
    if (!got)
        return;
    ++g_stat.decoded;
    ++g_stat.total;
    picture_out(g_frame);
    if (g_dec->refcounted_frames)
        av_frame_unref(g_frame);
}

/* a decoded picture to the display thread */
static void picture_out(const AVFrame* f)
{
    if (g_opt.bench)
        return;
    if (f->format == AV_PIX_FMT_VDPAU) {
        const int handle = (int)(intptr_t)f->data[3];
        if (handle < 1 || handle > HW_HANDLES || !g_hw.surf[handle]) {
            ++g_stat.errors;
            return;
        }
        const long long now = now_ms();
        if (g_win.last_picture && now - g_win.last_picture >= 1000)
            say("decoder: %lld ms without a picture%s", now - g_win.last_picture, g_win.stale_said ? ", shown again" : "");
        g_win.last_picture = now;
        g_win.stale_said = 0;
        struct picture* p = &g_box.pic[g_box.writing];
        av_frame_unref(p->hw);   /* its surface may be decoded into again */
        if (av_frame_ref(p->hw, f) < 0) {
            ++g_stat.errors;
            return;
        }
        p->surface = g_hw.surf[handle];
        p->number = g_stat.total;
        post_picture();
        return;
    }
    if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) {
        ++g_stat.errors;
        return;
    }
    if (!g_car.ready) {
        say("decoder: first picture %dx%d%s", f->width, f->height, shown_rows(f->height) < f->height ? ", top rows only" : "");
        if (car_stream(f->width & ~1, shown_rows(f->height) & ~1) != 0) {
            g_quit = 1;
            return;
        }
        for (int i = 0; i < 3; ++i) {
            g_box.pic[i].y = (uint8_t*)malloc((size_t)g_car.w * g_car.h);
            g_box.pic[i].uv = (uint8_t*)malloc((size_t)g_car.w * g_car.h / 2);
            if (!g_box.pic[i].y || !g_box.pic[i].uv) {
                say("out of memory");
                g_quit = 1;
                return;
            }
        }
    }
    if (f->width > g_car.w || shown_rows(f->height) > g_car.h) {
        ++g_stat.errors;   /* larger than the stream set up for the first picture */
        return;
    }
    const long long now = now_ms();
    if (g_win.last_picture && now - g_win.last_picture >= 1000)
        say("decoder: %lld ms without a picture%s", now - g_win.last_picture, g_win.stale_said ? ", shown again" : "");
    g_win.last_picture = now;
    g_win.stale_said = 0;
    struct picture* p = &g_box.pic[g_box.writing];
    to_nv12(f, p);
    p->number = g_stat.total;
    post_picture();
}

/* ---- Annex B -> access units ---- */

enum { INPUT_SIZE = 1 << 20, AU_SIZE = 1 << 20 };
static uint8_t g_input[INPUT_SIZE];
static uint8_t g_au[AU_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
static size_t g_au_len;
static int g_au_slices;

/* a NAL unit with its start code; a new access unit starts with AUD, SPS, PPS, SEI or types 14-18 after a slice, or
 * with a slice whose first_mb_in_slice is 0 (its first bit is 1) */
static void nal_unit(const uint8_t* nal, size_t len)
{
    const size_t head = nal[2] == 1 ? 3 : 4;
    if (len <= head)
        return;
    const int type = nal[head] & 0x1f;
    if (type == 12)
        return;   /* filler */
    const int slice = type == 1 || type == 5;
    if (g_au_slices && ((type >= 6 && type <= 9) || (type >= 14 && type <= 18) ||
                        (slice && len > head + 1 && (nal[head + 1] & 0x80)))) {
        decode(g_au, g_au_len);
        g_au_len = 0;
        g_au_slices = 0;
    }
    if (g_au_len + len > AU_SIZE) {
        say("input: access unit larger than %d bytes, dropped", AU_SIZE);
        g_au_len = 0;
        g_au_slices = 0;
        return;
    }
    memcpy(g_au + g_au_len, nal, len);
    g_au_len += len;
    g_au_slices += slice;
}

/* one writer, until it closes the FIFO (0) or the program is told to stop (-1) */
static int read_session(int fd)
{
    size_t fill = 0, scan = 0;
    long start = -1;   /* the NAL unit being read */
    g_au_len = 0;
    g_au_slices = 0;
    while (!g_quit) {
        if (!g_opt.bench) {
            window_policy();
            fd_set in;
            FD_ZERO(&in);
            FD_SET(fd, &in);
            struct timeval wait = {0, 100000};
            if (select(fd + 1, &in, 0, 0, &wait) <= 0)
                continue;
        }
        stats_maybe();
        if (fill == INPUT_SIZE) {
            say("input: no start code in %d bytes, starting over", INPUT_SIZE);
            fill = scan = 0;
            start = -1;
        }
        const ssize_t n = read(fd, g_input + fill, INPUT_SIZE - fill);
        if (n == 0) {
            if (start >= 0)
                nal_unit(g_input + start, fill - (size_t)start);
            if (g_au_slices)
                decode(g_au, g_au_len);
            return 0;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            say("input: %s", strerror(errno));
            return 0;
        }
        fill += (size_t)n;
        for (size_t i = scan; i + 3 <= fill; ++i) {
            if (g_input[i + 2] > 1) {
                i += 2;   /* no start code can end before i + 3 */
                continue;
            }
            if (g_input[i] || g_input[i + 1] || g_input[i + 2] != 1)
                continue;
            const size_t at = i && !g_input[i - 1] ? i - 1 : i;
            if (start >= 0 && at > (size_t)start)
                nal_unit(g_input + start, at - (size_t)start);
            start = (long)at;
            /* an access unit delimiter ends the picture before it: decode now, not when the next frame comes (the
             * writer puts one after each frame) */
            const size_t h = g_input[at + 2] == 1 ? at + 3 : at + 4;
            if (h < fill && (g_input[h] & 0x1f) == 9 && g_au_slices) {
                decode(g_au, g_au_len);
                g_au_len = 0;
                g_au_slices = 0;
            }
            i += 2;
        }
        if (start > 0) {
            memmove(g_input, g_input + start, fill - (size_t)start);
            fill -= (size_t)start;
            start = 0;
        }
        scan = fill > 3 ? fill - 3 : 1;
    }
    return -1;
}

static int run(void)
{
    if (!g_opt.bench && car_open() != 0)
        return 2;

    for (;;) {
        if (g_opt.ready) {
            const int r = open(g_opt.ready, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (r >= 0)
                close(r);
        }
        say("input: waiting for a writer on %s", g_opt.input);
        int fd;
        do
            fd = open(g_opt.input, O_RDONLY);
        while (fd < 0 && errno == EINTR && !g_quit);
        if (fd < 0) {
            if (!g_quit)
                say("input: %s: %s", g_opt.input, strerror(errno));
            break;
        }
        /* the trip computer box at the start of a session (user 2026-11-23): shown only with "Trip computer: on" in
         * iDrive's Android Auto page (panel-at-start in the package, default off); long NAV still toggles it */
        if (!g_opt.bench) {
            unlink(PANEL_PAGE2_FILE);   /* the paged box starts on its first page */
            if (access(RT_DIR "/panel-at-start", F_OK) == 0)
                unlink(PANEL_OFF_FILE);
            else {
                const int off = open(PANEL_OFF_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (off >= 0)
                    close(off);
            }
        }
        const long long t0 = now_ms();
        const int end = read_session(fd);
        close(fd);
        if (g_opt.bench) {
            const double secs = (now_ms() - t0) / 1000.0;
            say("bench: %lu pictures in %.2f s, %.1f pictures/s, %lu errors", g_stat.total, secs,
                secs > 0 ? g_stat.total / secs : 0.0, g_stat.errors);
            return 0;
        }
        if (end < 0)
            break;
        say("input: writer closed, %lu pictures so far; window hidden until the next session", g_stat.total);
        if (g_car.ready)
            window_set(0);
        g_win.shown = 0;
        g_win.last_picture = 0;
        g_win.stale_said = 0;
        if (g_dec)
            avcodec_flush_buffers(g_dec);
    }
    say("stopping: window hidden, stream closed");
    car_close();
    usleep(200000);
    return 0;
}

static int kd_main(int argc, const char* const* argv)
{
    (void)argc;
    (void)argv;
    _exit(run());
}

static int usage(void)
{
    fprintf(stderr, "usage: nbt_decoder --input FIFO [--ready FILE] [--size WxH] [--fps 30|60] [--rows N] [--hw] [--hw-scale]\n"
                    "       nbt_decoder --bench FILE\n");
    return 2;
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : 0;
        if (!strcmp(a, "--input") && value)
            g_opt.input = argv[++i];
        else if (!strcmp(a, "--ready") && value)
            g_opt.ready = argv[++i];
        else if (!strcmp(a, "--bench") && value) {
            g_opt.input = argv[++i];
            g_opt.bench = 1;
        } else if (!strcmp(a, "--size") && value) {
            if (sscanf(argv[++i], "%dx%d", &g_opt.width, &g_opt.height) != 2 || g_opt.width <= 0 || g_opt.height <= 0)
                return usage();
        } else if (!strcmp(a, "--hw")) {
            g_opt.hw = 1;
        } else if (!strcmp(a, "--hw-scale")) {
            g_opt.hwscale = 1;
        } else if (!strcmp(a, "--rows") && value) {
            g_opt.rows = atoi(argv[++i]);
            if (g_opt.rows < 2)
                return usage();
        } else if (!strcmp(a, "--fps") && value) {
            g_opt.fps = atoi(argv[++i]);
            if (g_opt.fps != 30 && g_opt.fps != 60)
                return usage();
        } else
            return usage();
    }
    if (!g_opt.input)
        return usage();
    g_opt.stale_ms = read_number("decoder.stale", 5000);
    g_opt.repeat_ms = read_number(RT_DIR "/decoder.repeat", 50);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    char timeout[16];
    snprintf(timeout, sizeof(timeout), "%ld", read_number(RT_DIR "/decoder.timeout", 3000));
    setenv("NV_NMSDK_TASK_TIMEOUT_MS", timeout, 0);
    say("nbt_decoder: %s %s, window %dx%d, %d fps, stale %ld ms, repeat after %ld ms%s%s", g_opt.bench ? "bench" : "input",
        g_opt.input, g_opt.width, g_opt.height, g_opt.fps, g_opt.stale_ms, g_opt.repeat_ms,
        g_opt.rows ? ", top rows only" : "", g_opt.hw ? ", hardware decoding (test)" : "");
    if (g_opt.bench)
        _exit(run());
    /* the video calls need libKD's thread setup: run inside NvKdLaunch */
    void* kd = dlopen("libKD.so", RTLD_NOW | RTLD_GLOBAL);
    int (*launch)(int (*)(int, const char* const*), int, char**) =
        kd ? (int (*)(int (*)(int, const char* const*), int, char**))dlsym(kd, "NvKdLaunch") : 0;
    if (!launch) {
        say("libKD.so: NvKdLaunch not found");
        _exit(2);
    }
    launch(kd_main, argc, argv);
    _exit(0);
}
