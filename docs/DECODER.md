# Decoder mode

The default picture path: `nbt_decoder` decodes the phone's H.264 on the CPU with FFmpeg and hands every picture to
the car's NVIDIA video library, which shows it in iDrive's EARLYVIDEO window. It replaces `fifo_http` + Cinemo
(`decode_runner`) + `nbt_hwvideo` for the session. Source: `src/android_auto/decoder/nbt_decoder.c` (its header
has the details below in short form).

Why: Cinemo's software decoder needs about 45 % CPU for about 28 pictures/s on this head unit. With the USB sound
running as well it fell behind, its queue grew and the picture lagged several seconds. FFmpeg with its SIMD assembly
decodes an 800x480 picture in about 7 ms on the car.

## Settings

Used whenever `nbt_decoder` is installed, at 60 fps; no file is needed. Optional files in
`/var/opt/hmi/data/rw/NbtAndroidAutoRT`, read at boot:

| File | Effect |
|---|---|
| `decoder.off` | Do not use the decoder mode: the old picture path (Cinemo). Without it the decoder mode is used whenever `nbt_decoder` is installed (the installer ships it when the build has it) |
| `decoder.30fps` | Ask the phone for 30 pictures/s instead of 60. Default: 60 (`NBT_VID_FPS`, core patch `nbt-aad-sdr_builder-video-fps.patch`, and the car's stream set to 60, `--fps 60`) |
| `split800` | The 800x480 picture 1:1 at the left edge (`--size 800x480`), iDrive's split screen stays visible; otherwise stretched to 1280x480 |
| `decoder.stale` | Milliseconds without a new picture before the window is hidden (default 5000, `0` = never) |
| `decoder.800` | Full screen: the old 800x480 picture stretched to 1280x480. Default (without it): sharp, the phone draws 1280x720 with its interface in the top 480 rows, shown 1:1 (`--rows 480`); 2.4x the pixels to decode on the CPU |
| `decoder.sw` | Software decoding (FFmpeg on the CPU) instead of the car's own H.264 decoder (`--hw`, see below; the default since 2026-11-27). "Use software decoder *" in iDrive's Advanced settings; applies after a reboot. (`decoder.hw` is no longer read) |
| `decoder.panelx`, `decoder.panely` | The box's position in Android Auto's window (default: right edge at 1259, y 96) |
| `decoder.fuelscale` | Microlitres of fuel per count of CDS `engine.consumption`, x1000 (default 1000), for the box's instantaneous consumption |
| `decoder.panelbg` | The box's background as hex RRGGBBAA (default 000000B4, translucent black; FF = opaque). Text and icons are white on a dark background and black on a light one; the 1 px outline is derived from it (lighter on dark, darker on light). Grey is exact; with a coloured one red and blue may come out swapped (not traced) |
| `decoder.panelflip` | The box's bitmap: 1 rows reversed, 2 columns reversed, 3 both (default 0) |
| `decoder.novolume` | No volume popup over the picture ("Display volume changes" unticked in Advanced; read every second) |
| `decoder.showfps` | Frame rate overlay in the top left corner: pictures shown in the last second, updated every second, white on opaque black ("Show frame rate" in Advanced; read every second; nothing drawn or uploaded while off; full width only, like the box) |
| `decoder.panelsize` | The box's size, set by "Widget size" in iDrive's Android Auto page: `full` = full width (384 px); none (the default) or `small` = 256 px (same right edge, x 1003) with two pages, long NAV cycles page 1, page 2, off; `trip` / `car` = 256 px with only that page, long NAV on / off. "Use large widget" in iDrive's Settings writes `full` or removes it; read every second, the box changes size at once |
| `decoder.args` | One line of extra arguments for `nbt_decoder` |
| `decoder.bgvideo` | Keep decoding while the Android Auto / CarPlay page is not on screen. Default (since 2026-11-25): nothing is decoded then; Android Auto gets video focus NATIVE and pauses its video, `nbt_carplay` drops the iPhone's frames; back on the page both send a fresh key frame |
| `decoder.delay` | Seconds after iDrive's `MARKER HMIReady` before the first session at boot (default 2) |
| `decoder.showdelay` | Seconds of uptime before Android Auto is brought to the front (start at boot, NAV, AUX; default 40) |
| `decoder.timeout` | `NV_NMSDK_TASK_TIMEOUT_MS` for the video calls (default 3000) |
| `decoder.repeat` | Milliseconds without a new picture before the last one is handed to the car again (default 50, `0` = never) |

Reboot after changing them.

## How it runs

`native_service` starts `nbt_decoder --input annexb.fifo --ready http.ready [--size 800x480] [--fps 60]` once and
keeps it for all following sessions: each session opens the FIFO as writer and closes it at the end. Starting and
killing the program per session froze the whole iDrive display when the phone was plugged in again (car
2026-11-18/19, once "NO SIGNAL" and an iDrive restart).

It also outlives service restarts (2026-11-24): a second `nbt_decoder` in the same boot gets no picture (the window
server keeps the first stream's window: "window 1024 ... already allocated", "buffer increment mismatch", and the car's
decoder runs out of surfaces), and before the stream was closed on exit the head unit went down. `native_service`
leaves it running when it stops and writes `decoder.pid` and `decoder.argv` into the runtime directory; the next run
takes it over when the arguments are the same (or only `--hw` is missing, after `decoder.hwfail`; checked with
`pidin -p <pid> arg`), otherwise it ends it and starts a new one (picture mode or Software/Hardware changed on iDrive's
page). A new one after a car stream already existed in this boot (`decoder.streamed`, written by `nbt_decoder`) gets no
picture: `native_service` writes `reboot.required` (RAM, until the next boot) and `reboot=1` into the state file, and
iDrive's Android Auto page shows "Please restart the system" ("Restart system" is on the same page). `annexb.fifo` stays while it waits on it, and `boot.sh` does not replace its program file: a new `nbt_decoder`
in the package runs after a reboot.

Inside `nbt_decoder`:

- **Main thread** (inside libKD's `NvKdLaunch`, which the video calls need): reads the FIFO, cuts the Annex B stream
  into access units (a new one starts with AUD, SPS, PPS, SEI or types 14-18 after a slice, or with a slice whose
  `first_mb_in_slice` is 0), decodes them with FFmpeg (one thread, low delay), converts I420 to NV12 and puts the
  picture into a three-picture mailbox; a picture not yet taken is replaced by the newer one. It also runs the window
  policy at least every 100 ms.
- **Display thread:** takes the newest picture and, while the window is shown, uploads it (`VideoFrame`) and presents
  it (`VideoRenderFrame`). With nothing new for 50 ms (`decoder.repeat`) it hands the car the last picture again: the car
  fades the window out when no picture comes for a short while. On still screens the phone sends few pictures
  (YouTube Music ~6/s against ~30/s in Waze), and the picture and iDrive's page alternated about twice a second
  (car 2026-11-19).
- **Window policy:** shown while the Android Auto page is in front (no `/fs/ram3/nbt-aa-hidden`), the session runs
  (`/fs/ram3/nbt-aa-state` says `state=running` and is at most 5 s old; a stop counts after 1.5 s), the session has
  sent its first picture and pictures keep coming (`decoder.stale`). Hiding waits 400 ms so short page changes do not
  blink; position, size, z-order and visibility are set again every 2 s.
- `SIGTERM` hides the window, closes the car stream (`VideoStreamDestroy`, `VideoBufferDestroy`, `VideoClose`; also
  when hardware decoding is given up) and ends the program with `_exit()`; returning through `NvKdLaunch` crashed in
  the libraries' cleanup. Without the close the stream stayed in the window server after the process ended, as it
  looks: the next `nbt_decoder` (service restart) showed garbage and the head unit went down (car 2026-11-24).

`nbt_decoder --bench FILE` decodes a recorded stream as fast as possible and prints the rate, without the display.

## The car's video interface

From `/lib/libnmsdk.so`, `/lib/libKD.so` and `/opt/sys/lib/libwal_nvidia.so` (2014). libnmsdk exports
`CNvVideoBase` with full C++ signatures, so the virtual method table and the argument types are readable directly:

| Slot | Method |
|---|---|
| 0 | `Init()` |
| 2 | `VideoOpen(void** handle, ICallback*, INvAvSync*, CConfDecoder*)` |
| 4 | `VideoConfigure(handle, const CConfVideo&)` |
| 5 | `VideoFrame(handle, EVideoFormat, const uint8_t** planes, uint32_t* pitches, int64_t pts, EPictureFormat, void*)` |
| 12, 13 | `SetVideoSource`, `SetVideoDestination(handle, const CRect&)` |
| 14 | `VideoBufferCreate(void** buffer, width, height, float, bool)` |
| 16 | `VideoRenderFrame(buffer, handle, CVideoRenderFrameParams*)` |
| 17 | `VideoStreamCreate(handle, void** buffers, count)` |
| 3, 15, 20 | `VideoClose(handle)`, `VideoBufferDestroy(buffer)`, `VideoStreamDestroy(handle)` |

Every call is queued to the library's worker thread and waits for it, at most `NV_NMSDK_TASK_TIMEOUT_MS`.

- **`CConfVideo`** (13 words, `OnVideoConfigure`): width, height, two floats, the second being the frame rate,
  the decoder type in word 5 (0 = none, pictures come with `VideoFrame`; 1-2 MPEG-2, 3 H.264, 4-6 VC-1, 7 capture), a
  64-bit value in words 9-10, the surface count (1-20) in word 11 and the stream id in word 12, which becomes stream
  property `0x1789` of `kdCreateStreamNV`. The library allocates that many NV24 surfaces.
- **Frame rate:** `OnVideoRenderFrame` holds each picture for two display refreshes when the configured rate is below
  32, for one otherwise. Configured with 30, the stream can show at most 30 pictures/s whatever the phone sends (car
  2026-11-19 with 60 fps from the phone: every upload took ~31 ms and pictures were dropped); with 60 it shows them all.
- **`VideoFrame`:** `EVideoFormat` 1 = NV12 (2/3 = YV12 with U/V in either order, 4/5 = packed 4:2:2). The library
  takes a free surface and converts the picture into it on the CPU (`NVVAVideoSurfacePutBits`, NV12 to NV24): this is
  the ~13-15 ms "upload". It waits for a free surface, and surfaces come back only once the stream is composited on
  screen, so the program does not upload while its window is hidden.
- **`VideoRenderFrame`:** parameter word 0 = picture structure (3 = frame); surface 0 = the one `VideoFrame` just
  filled. Byte 0x14 is set to 1 (its meaning is not traced).
- **Windows** (`wal::LayerId` in libwal_nvidia): 1024 `EARLYVIDEO`, 1052 `VIDEO`. The stream with id -28 lands in
  EARLYVIDEO (1052 - 28), which iDrive leaves alone while Android Auto is in front. libwal_nvidia's
  `WalHmiInterface` (`setWindowPositionAndSize`, `setZOrder`, `setVisible`) places, raises and shows it.

## Hardware decoding (`decoder.hw`, test)

`libnmsdk.so` contains NVIDIA's H.264 decoder for the GPU's video engine (NVVA, microcode for G82, G98 and GT212):
`INvVideo::VideoDecode` (method 6) takes one picture as its slices plus a picture description
(`nvh264bspPictureDecode`) and decodes into one of the stream's surfaces; `VideoRenderFrame` shows a surface given in
its parameters (offset 0x0c) without any copy on the CPU. With `--hw`:

- FFmpeg only parses: its VDPAU interface (hwaccel `h264_vdpau`, no libvdpau) keeps the reference pictures and hands
  each picture over as `VdpPictureInfoH264` and its slices (each after `00 00 01`). `nbt_hwdec.h` rewrites that into
  the library's layout: the offsets are those `h264BSP2NVVA` (libnmsdk 0x34d90) reads, checked by running that
  function in an emulator on what `hw_bsp_fill()` writes (every field and reference came out as VDPAU's).
- The stream is configured for decoder type 3 (H.264, `NVVADecoderCreate` with 16 references) at the coded size with
  20 surfaces. FFmpeg's frames carry handles 1-20; the first picture decoded into a handle takes a surface from the
  library (surface `-1`: taken and kept), later ones reuse it. References are rows of a table (8 bytes per row, the
  surface first) indexed by handle. The three pictures between decoder and display hold their frames, so FFmpeg does
  not decode into a surface that may still be shown.
- `--rows 480` (the sharp full-screen default): the source rectangle is the top 480 rows, copied 1:1 into stream
  buffers of 1280x480, which the window shows 1:1. If the car refuses buffers smaller than the coded 1280x720, the
  destination is the whole 1280x720 buffer instead (the rows scaled up 1.5x, then down again by the window); `--hw-scale`
  (in `decoder.args`) asks for that from the start, to compare. Car 2026-11-23: the 1280x480 buffers are accepted
  (`video: stream 1280x720 (buffers 1280x480)`), 1,471 pictures in 45 s, 0 errors.
- If the car refuses the decoder at the start, the CPU path takes over in the same run. If decoding never works
  (10 pictures) or keeps failing (100 in a row), `nbt_decoder` writes `decoder.hwfail` (in `/fs/ram3/nbt-aa-native`)
  and ends; the session ends with it, and `native_service` starts it again without `--hw`: the CPU decodes until
  the next boot.
- Not covered: streams with their own scaling matrices (High profile; the phone's encoders do not send them so far):
  `nbt_decoder` gives up as above.

Log lines start with `hw:` (stream set-up, first picture, the surface of each handle, failures, a count every 5 s).

Car results: 800x480 at 30 fps, 0 errors, about 0.6 ms per picture on the CPU (parsing and hand-over only); 1280x720
(Baseline, level 3.2) at 60 fps with the top 480 rows shown, 0 errors.

Which decoder a session used: `video: stream` says `decoded by the car` (hardware) or `pictures from the CPU`;
`decoder.hwfail` in `/fs/ram3/nbt-aa-native` means the hardware decoder gave up until the next boot.

```
%CAR% "grep -h -e 'video: stream' -e 'hw:' /fs/ram3/nbt-aa-native/nbt_decoder-*.log | tail -20"
```

## Boot

Shown at boot before about 34-40 s, the picture stalls for good: every `VideoFrame` waits the full timeout
(`0x800f0053`), because the window is not composited yet and no surface comes back. Hence:

- with the phone plugged in at boot, the session waits for iDrive's `MARKER HMIReady` in the system log (+`decoder.delay`);
- "start at boot" brings Android Auto to the front only from 40 s of uptime (`decoder.showdelay`); the session itself
  starts as early as before. Since 2026-11-20 every show request (NAV, AUX, after parking) waits for that too; the HMI
  plugin keeps the request and takes it at 40 s.

Plugged in later, nothing waits.

## FFmpeg build

FFmpeg 2.8.22 with only the H.264 decoder, LGPL 2.1 (no GPL or non-free parts). The QNX VM has no bash, perl or yasm,
so FFmpeg's `configure` and assembler cannot run there (details in `scripts/ffmpeg_qnx.py`):

- `configure` ran once on Linux for a 32-bit x86 QNX target; its `config.h` / `config.asm` were adjusted for QNX 6.5
  and GCC 4.4.2 and are kept in `src/android_auto/decoder/ffmpeg/` with the list of needed files and their SHA-256.
- `build_all.py` clones FFmpeg `n2.8.22` once into `.local/`, checks every file, assembles the 22 assembly files with
  yasm 1.3.0 (on Windows the official `yasm-1.3.0-win64.exe`, downloaded once into `.local/`, SHA-256 checked) and
  sends sources and objects to the VM.
- `scripts/build-ffmpeg-qnx.sh` probes the QNX C library for the values configure would have checked, compiles the
  C files with qcc and puts everything into `libavcodec.a` / `libavutil.a`. The libraries are kept in
  `~/.nbt-build/ffmpeg-<key>` in the VM and reused until an input changes (the first build takes a few minutes).

Without the assembly the car decoded a picture in ~10 ms, with it ~7 ms.

## Car results (2026-11-19, 800x480)

| | 30 fps | 60 fps |
|---|---|---|
| Decode | ~7 ms (~10 ms while driving with the map moving) | ~7 ms |
| Upload (`VideoFrame`) | ~13 ms | ~14-15 ms |
| Replaced before upload | 43 of 133,410 pictures in 75 min | 18 of 1,920 in a minute of scrolling |
| CPU (`aps show -v`, 10 s) | | total ~58 %, partition `interaction` ~39 % (budget 33 %) |

## Log

`/fs/ram3/nbt-aa-native/nbt_decoder-<session>.log` (RAM, the session it was started in). Every 5 s, for that interval:

```
stats 5.0 s: received 150 (30.0/s), decoded 150 (30.0/s), shown 149 (29.8/s) + 0 repeats, 1 replaced, 0 while hidden, 0 decode / 0 show errors; decode 6.8 ms per call (max 14.1), show 12.9 ms (max 21.0)
```

- received: pictures from the phone (access units read from the FIFO)
- decoded: pictures the decoder gave back, counted when the call returned (with `decoder.hw` the car's `VideoDecode`
  included), not when handed in
- shown: new pictures the car took (`VideoFrame` / `VideoRenderFrame` succeeded); repeats: the last one again
- replaced: decoded but never shown (a newer one came first)
- decode: time per decode call; show: per `VideoFrame` (CPU path) or `VideoRenderFrame` (`decoder.hw`)

Shown far below received: the extra pictures only cost. Decoded below received: pictures are lost or held in the
decoder. A decode time that jumps with 60 fps: the decoder is the limit.

```
%CAR% "grep stats /fs/ram3/nbt-aa-native/nbt_decoder-*.log"
```

```
%CAR% "grep -e video: -e window: -e input: -e decoder: /fs/ram3/nbt-aa-native/nbt_decoder-*.log"
```

## Trip computer box

A box at x 875, y 96, 384x167 with a "Trip computer" title line (20 px from the right edge, our own icons before the values) (rounded corners, radius 16, 12 / 8 px inner padding, DejaVu Sans 22 px, 31 px between lines) over the full-width picture, instead of iDrive's split screen:

```
12:04        44 min        30 km      trip start, duration, distance
7.4 L/100km           42.1 km/h      trip averages
833 km        52 L        0.7 L/h       range, tank, instantaneous consumption: L/h stationary, L/100km from 5 km/h (counter unit a guess, `decoder.fuelscale`)
11.5°C    E 80°    O 73°    B 85%   outside, coolant, oil, starter battery charge (CDS sensors.battery)
```

- Data: `nbt_knob` reads iDrive's trip computer from the cluster over MOST (FBlock E0/00 at 0101: 509 averages,
  510 trip start, 511 duration and distance; Get every 30 s, plus the cluster's updates to iDrive) into
  `/fs/ram3/nbt-aa-tripkombi`; the HMI plugin subscribes to CDS (`sensors.fuel`, `sensors.temperatureExterior`,
  `engine.temperature`, `engine.consumption`, averages as fallback) into `/fs/ram3/nbt-aa-trip`. Both RAM only.
- Drawing: libnmsdk's subpicture (since 2026-11-26). Our own stream's video mixer blends it over every picture in
  `VideoRenderFrame`, in our process's GPU channel, with software and hardware decoding alike; the window server only
  gets the finished buffers. Methods 21 `SubPictureConfigure` (an NVVA output surface the size of the stream's
  buffers, 1280x480), 22 `SubPicturePalette` (16 entries Y, Cr, Cb), 23 `SubPictureFrame` (the whole surface, one byte
  per pixel: alpha in the high 4 bits, palette index in the low 4), 24 `SubPictureAbort` (hides it). The box is drawn
  into that bitmap, the rest is transparent; uploaded (600 KB) only when the box's picture changed, at most once a
  second. It shows and hides with Android Auto's window by itself. If the stream's buffers are not the window's size
  (they would be scaled), there is no box.
- The stream's mixer as libnmsdk creates it takes no layer over the video (`NVVAVideoMixerCreate(..., 0)`;
  car 2026-11-26: every picture failed with the subpicture on). Right after `VideoConfigure` the decoder creates the
  same mixer with 1 layer and puts it in the stream's place (`CVideo` + 0x60), the old one destroyed. If a
  `VideoRenderFrame` still fails with the box in the picture, the box is switched off for the rest of the program.
- Before (2026-11-23 to 11-26) the box was a WAL sprite in a process of its own (`nbt_decoder --panel`). It crashed the
  head unit on 2026-11-26: `NVRM: PUSHER RESERVED CMD ERROR`, Fifo Parse Error on WalServer's GPU channel; in its
  command ring (`NVshm_44`) a later command batch had been written into the middle of our sprite's pixel upload.
  A video stream of its own for the box did not work either (car 2026-11-23): in the decoder's process it took over
  Android Auto's window, in a process of its own its window never showed.
- CDS notes (car 2026-11-23): `engine.consumption` is a 16-bit counter (CPxyVehicle InjectionVolume) that wraps at
  65536; the box uses the difference per second, its unit (µl per count) is a guess (`decoder.fuelscale`).
  `sensors.battery` is the IBS state of charge and often reads 255 (shown as `--%`), sometimes a real value.
  The 12 V battery voltage is not available to iDrive: not in CDS, not in the MOST proxies, not in the Jacinto
  system registers (GPIO only).
- In split screen (window 800 wide): no box.
- Long roller push (700 ms, `NBT_PUSH_LONG_MS`, `nbt_knob`): short-term average speed on / off. On, it averages the
  current speed (CDS `driving.speedActual`) from that moment and shows it at the end of the title line; a short push
  is still play / pause (now on the release).
- At the start of each session the box is hidden unless "Open widget automatically" is ticked in iDrive's Settings
  (flag file `panel-at-start` in the package; default off). Long NAV still
  shows / hides it during the session. There is no other switch: in full width the box's process always runs; while
  the box is hidden it only draws (the short-term average speed keeps counting) and sends nothing to the window
  server.
- Long NAV (700 ms, `NBT_NAV_LONG_MS`) in Android Auto toggles `/fs/ram3/nbt-aa-panel-off` (box hidden while it
  exists; on again after a boot). NAV's play / pause now comes with the release of a short press.
- Small box ("Widget size" in iDrive's Android Auto page, full width only, `decoder.panelsize`; user 2026-11-23):
  Full width / Small / Trip only / Car only, each press takes the next; Small is the default. The full box is 384 px wide and its lines need it (up to 365 px
  of text at 22 px), which brings it close to Waze's controls in full width. The paged one is 256 px wide with at most
  two values per line: page "Trip" = start and duration, distance, average consumption, average speed; page "Car" =
  range and tank, instantaneous consumption, outside temperature and battery, coolant and oil. The short-term average
  speed is at the end of the title on both pages while it runs; otherwise two dots there show the page, as Android
  Auto shows pages (same size, the current one white, the other grey). As tall as the full box. Small: long NAV goes
  page 1, page 2 (`/fs/ram3/nbt-aa-panel-page2`), off, page 1 again; each session starts on page 1; a page change is
  drawn and uploaded at once (not waiting for the once-a-second update). Trip only / Car only: one page, no dots, long
  NAV shows / hides it.

## Volume popup

iDrive's own volume popup is behind Android Auto's / CarPlay's picture, so the decoder draws one into the picture, in
the same subpicture as the trip box: the stock look (a photo of the car's screen, 2026-11-27), 604x84, centred, its
middle at 2/3 of the height. A dark translucent panel (corners and outline as the trip box's), the speaker (crossed out when muted), a
dark bar with a light outline and a cream fill with a bright end, and below it "Entertainment" (bold DejaVu Sans) where
the stock popup names the audio.

- Level: `nbt_knob` takes the audio master's volume status from MOST (car trace 2026-11-27: 100F to the HMI, FBlock
  20/00 fkt C08, `01 00 LL 00 64` = type 1, level, maximum 100, one per step, level 0 on mute) and writes
  `/fs/ram3/nbt-aa-volume`: `<number> <level> <maximum> <muted 0/1> <type>` (the number counts up). Muted = level 0
  within 1 s after the mute key (15/0C C04). The decoder reads the file every 50 ms; the first line it reads is only
  the starting point.
- By hand: `echo "1 12 100 0 1" > /fs/ram3/nbt-aa-volume`, then the same with 2, 3, ...
- Type 1 (the only one seen) is shown as "Entertainment", any other as "Volume".
- Shown for 3 s after the last change (the stock popup's time is not measured), then faded out in 6 steps over
  300 ms (6 uploads); only while Android Auto's window is shown (on iDrive's own pages iDrive's popup is visible).
- Corners and outline as the trip box's: radius 16, a 1 px outline a quarter of the way from the panel to white.
- Palette (16 entries for both): 0 transparent, 1..9 the box (its background to its text colour), 10..15 the popup's
  colours. The subpicture is taken out of the picture (`SubPictureAbort`) when neither is shown.
