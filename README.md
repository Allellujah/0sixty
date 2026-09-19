# 0sixty

*by Allelujah*

GPS performance ("0-60"/quarter-mile) and long-drive trip tracker firmware
for an M5Stack Cardputer ADV + LoRa1262 Cap (used only for its onboard GNSS
chip, ATGM336H-6N@AT6668 -- the SX1262 LoRa radio is unused).

Two modes (never simultaneous), switched with a key or a dashboard button:
1. **Performance ("0sixty") mode** -- auto-armed 0-60/0-100 runs, live graph +
   splits on your phone, a light distance/0-60/graph view on the Cardputer.
2. **Trip mode** -- long-drive logging, exported as a speed-banded KML that
   opens directly in Google My Maps, or a raw CSV.

Design notes and the reasoning behind each change live in `CHANGELOG.md`;
`MANUAL.md` covers day-to-day use.

## Status

**v1.4.1 (2026-09-19)** -- code-review fixes to the v1.4.0 export: stops no
longer emit a pin *and* a redundant near-zero sliver, a trip's final leg
can't silently vanish, and both timestamp paths are guarded against
unsigned underflow.

**v1.4.0 (2026-09-19)** -- reworked the KML export after seeing it in Google
My Maps: speed-band changes now have to persist before they split a leg
(hundreds of one-point slivers become a handful of real legs), each leg is
labelled with its actual time range, distance and average/max speed instead
of just a band name, stops become their own pins, and legs carry timestamps
so Google Earth can animate a drive.

**v1.3.1 (2026-09-18)** -- author signature ("Allelujah") on the boot
splash, serial banner, dashboard footer, printable report, and stamped into
every exported recording's metadata.

**v1.3.0 (2026-09-18)** -- real dates/times on recordings (from GPS, latched
into the system clock so they survive losing the fix; Europe/Brussels DST
handled by the C library's timezone rules), on-device naming via the `N`
key, names on Performance runs (which previously had none at all), names and
timestamps in saved filenames, per-recording delete buttons, and the two
history lists split so each mode shows only its own. Metadata parsing moved
from positional to key-based so older recordings keep reading correctly.

**v1.2.0 (2026-09-17)** -- fixed a real bug where a completed Performance
run's results got wiped within ~500ms of stopping (found via real-hardware
testing); every run now saves to SD like trips do, browsable from a new
Performance History section; added a printable/"export as PDF" report page
for any run. See `CHANGELOG.md` for the full list.

**v1.1.1 (2026-09-17)** -- fixed a broken "Open Google My Maps" link (the
captive-portal WebView it lived in gets closed by the OS on disconnect, so
it could never work); added the firmware-versioning system itself.

**v1.1 (2026-09-17)** -- QR WiFi provisioning + captive portal, auto trip
start/stop with a summary screen, SD-backed trip history, trip naming,
hard-event flagging, quarter-mile/60-130 performance splits, low-battery
power save, and optional home-WiFi auto-upload. The AP is open (no
password) -- see "Interacting with the device" below.

Everything from v1.1 onward was built without physical hardware access
except where a CHANGELOG entry says otherwise -- check `CHANGELOG.md`'s
"Known limitations" for each version before trusting something new.

### Phases 1-4 (2026-09-15/16), confirmed on real hardware

- **Phase 1 (GPS + IMU acquisition, status display)** -- working.
- **Phase 2 (WiFi AP + live web dashboard)** -- working.
- **Phase 3 (Performance mode)** -- built, compiles clean, boots and holds a
  GPS fix on real hardware. The arm/trigger/run state machine itself
  (auto-arming at a stop, triggering on launch, splits) has **not** been
  validated with an actual drive or push/shake test -- that needs a human
  holding the device, which no amount of USB/serial access can substitute
  for. Do that before trusting a real 0-60 time from it.
- **Phase 4 (Trip mode + KML/CSV export)** -- built and field-tested: a real
  drive produced a real KML/CSV export. That test caught a genuine bug --
  Trip mode was recording the IMU-fused speed, which zeroes out after ~400ms
  of steady (non-accelerating) motion, so most samples read 0.00 km/h during
  normal cruising even though the GPS track itself was fine. Fixed
  2026-09-16: Trip mode now records the GPS's own `fix.speedKmh` and no
  longer waits on IMU calibration before it starts recording.
- **UI/UX pass (2026-09-16)** -- see below.
- **SD logging now works (2026-09-16)** -- root-caused and fixed: the
  LoRa1262 Cap's SX1262 radio shares the SD card's exact SPI bus with its
  own chip-select, which this firmware never parked (deselected), so the
  radio was very likely interfering with every SD transaction. Fixed in
  `storage.cpp` by driving that pin high before any SD activity; confirmed
  on real hardware with the cap attached. Raw samples land in
  `/logs/raw_<ms>.csv` on the card, and now record the GPS's own
  `fix.speedKmh` (same fix, same reason, as the Trip mode bug above) rather
  than the IMU-fused speed. This is a separate 10Hz raw logger
  (`storage.cpp`'s `logSample()`) from Trip mode's own KML/CSV export --
  both modes' data was already, and still is, also downloadable straight
  from the web dashboard regardless of SD.

### The GPS pin gotcha (read this before touching `config.h`)

The LoRa1262 Cap's own datasheet says "GPS_RX→G8, GPS_TX→G9" -- **this is
wrong**, or at least misleading: those are connector-position labels, not
literal ESP32 GPIO numbers. G8/G9 are actually the Cardputer ADV's own
keyboard I2C bus (SDA/SCL to its TCA8418 controller) -- using them for GPS
UART silently kills the physical keyboard and never receives any real GPS
data. The real, hardware-confirmed pins are **RX=G15, TX=G13** (source:
Bruce firmware's actual working config for this exact cap,
`boards/m5stack-cardputer/interface.cpp`), already set correctly in
`include/config.h`. Don't "fix" these back to G8/G9 from the datasheet.

## Interacting with the device

The Cardputer's own 240x135 screen shows a themed status view with a header
(mode, GPS fix dot + satellite count, battery) and a footer listing the keys
that work on the current screen -- you don't need this README open to use it
day to day, but here's the full picture:

| Key | Action |
|---|---|
| `M` | Switch Performance <-> Trip mode (same as the dashboard's "Switch Mode" button) |
| `C` | Cycle the on-device color theme: green -> red -> blue -> white |
| `U` | Toggle km/h <-> mph, and km <-> mi for Trip's on-device distance -- on-device readouts only; the dashboard and KML/CSV exports are always in km/h + meters |
| `X` | Trip mode: dismiss the trip-complete summary screen, or force-end and save the current trip early |
| `Q` | Show/hide the WiFi-join QR code (v1.1) -- see MANUAL.md |
| `N` | Name the next trip/run on the device (v1.3.0) -- Enter saves, Del backspaces, `` ` `` cancels |

**v1.1: Trip mode now starts/stops recording automatically** (sustained
movement to start, sustained parking to stop) rather than running
continuously whenever selected -- see MANUAL.md for the exact thresholds
and `config.h`'s `AUTO_TRIP_START_*`/`AUTO_TRIP_STOP_*` to tune them. Every
trip is saved to the SD card as its own file under `/trips/` as it
records, browsable from the dashboard's Trip History section regardless of
whether it's still the live trip.

Theme, active mode, and speed unit all persist across power cycles (stored in
the ESP32's NVS flash via `src/settings.cpp`), so you don't need to re-pick
them every boot.

### Switching between "GPS tracker" (Trip mode) and "0sixty" (Performance mode)

They're the same firmware, same device -- there's no separate flash/reflash
involved. Press `M` on the Cardputer's own keyboard, or open the web
dashboard (see below) and tap **Switch Mode**. Both act on the same
underlying setting and take effect within one display refresh (~250ms).
Performance mode is the "0sixty" 0-60 timer; Trip mode is the
GPS-tracker-style continuous route logger.

### Getting your data off the device

Both modes' data lives in RAM and is served straight from the Cardputer's
own web server -- no SD card needed for this. (The SD card works too now,
see "Status" above, but it's an independent raw log, not where this data
comes from.)

1. On your phone (or laptop), join the WiFi network named in `AP_SSID`
   (`include/config.h`, currently `0sixty`) -- **v1.1: open, no
   password**. Press `Q` on the device for a scannable join QR code, or just
   join it manually from your phone's WiFi list.
2. Open `http://192.168.4.1` in a browser. No internet is available while
   joined to this AP (by design -- the Cardputer's WiFi radio only runs the
   AP, not a client+AP combo), so the dashboard is fully self-contained.
3. Scroll to the **Downloads** row at the bottom:
   - **Performance mode:** "Download last run (CSV)" -- the most recent
     completed run's speed/altitude/accel history plus the splits table.
     Available even if you're currently in Trip mode (it's the *last*
     Performance run, not the live one). Next to it, "View / print report"
     opens `/report/run.html` -- a printable report (headline stats, splits
     table, speed graph) with a "Print / Save as PDF" button, no app needed.
     Every completed run is also saved to SD as its own file (v1.2.0); the
     **Performance History** section further down the dashboard lists every
     saved run with its own report/CSV link, same pattern as Trip History
     below.
   - **Trip mode:** "Download trip (KML)" -- opens directly in
     [Google My Maps](https://mymaps.google.com) (Import -> pick the file),
     drawn as labelled driving legs colored by speed band (v1.4.0 -- a band
     change has to persist before it splits a leg, so each leg is a real
     stretch of driving, not a one-point sliver, and its name shows the real
     time range, distance and average speed rather than just a band name; a
     stop of 2+ minutes drops its own labelled pin; see MANUAL.md's "The
     dashboard" section for what the exported map looks like, and
     `TRIP_SPEED_BANDS` in `config.h` for the band colors; the on-device
     screen no longer shows a matching legend, see "On-device UI" below).
     Download it, then
     **disconnect from the tracker's WiFi and open mymaps.google.com
     yourself** in your phone's normal browser to import it -- v1.1 used to
     show an in-page "Open Google My Maps" shortcut link here, but real-
     hardware testing found it can never work: on Android, the captive
     portal (see MANUAL.md's "Getting your phone connected") opens this
     dashboard inside a temporary WiFi sign-in view, not a real browser tab,
     and that view is torn down by the OS the instant you disconnect -- so
     there's no page left to click a link from by the time you'd have
     internet to use it.
     The link was removed; this note replaces it. "Download trip (CSV)"
     gives the raw lat/lng/speed/altitude points if you want to process them
     yourself.
4. Trip data survives switching to Performance mode and back, and survives
   the buffer filling up -- recording pauses at `TRIP_MAX_POINTS` (no more
   new track points, so the KML/CSV export's resolution stops improving),
   but the live distance/duration/max-speed stats keep updating for the rest
   of the drive. It's only cleared by pressing `X` in Trip mode or
   power-cycling the device.

## Build

```
pio run -e cardputer                              # compile
pio run -e cardputer -t upload --upload-port /dev/ttyACM0   # flash
pio device monitor -b 115200 -p /dev/ttyACM0       # serial console (see note below)
```

The device enumerates as `/dev/ttyACM0` (ESP32-S3's native USB-JTAG-serial),
not the `/dev/ttyUSB0` you'd expect from an external USB-serial chip --
`--upload-port`/`-p` may be needed explicitly if other serial devices are
also attached. `platformio.ini` sets `-DARDUINO_USB_MODE=1
-DARDUINO_USB_CDC_ON_BOOT=1`; without these, `Serial` doesn't route to
`/dev/ttyACM0` at all and nothing prints, even though the build succeeds.

**If the project directory is on a network mount (CIFS/NAS, as it is on the
NAS-synced dev machines):** PlatformIO's SCons build doesn't cache
incrementally there -- every `pio run` recompiles the entire framework from
scratch (~15-25 minutes), even for a one-line source change. Point the build
output at local disk instead to get real incremental builds:
```
export PLATFORMIO_BUILD_DIR=/tmp/some-local-dir   # anywhere NOT on the network mount
pio run -e cardputer -t upload --upload-port /dev/ttyACM0
```
The first build after doing this is still a full cold build; every one after
that only recompiles what actually changed.

### Versioning (every build gets a version)

`include/config.h`'s `FIRMWARE_VERSION` is the single source of truth --
bump it before building any `.bin` meant to leave this machine (project
rule, 2026-09-17): a small/isolated fix bumps **patch** (`1.1.0` ->
`1.1.1`), a batch of new features bumps **minor** or **major** depending on
scope. Shown on the device's boot screen and the dashboard footer
(`/api/version`), and read automatically by both scripts below to name their
output -- don't hardcode a version anywhere else.

### Flashing without PlatformIO installed

Two scripts, both reading the version from `config.h` automatically, run
from the repo root after a normal `pio run -e cardputer` build:

- **`tools/make_app_bin.sh [build_dir]`** -- the plain app-only image, for
  Launcher's SD-card or WebUI sideload method. Writes
  `0sixty-v<version>-app.bin`.
- **`tools/merge_bin.sh [build_dir]`** -- packs the bootloader, partition
  table, OTA data, and app into one image at their real flash offsets, for a
  direct `esptool` flash with no PlatformIO install (the same single-file
  pattern this device was originally flashed with -- Bruce's `flasherProxy`
  image). Writes `0sixty-v<version>-merged.bin`.

```
./tools/make_app_bin.sh                       # or ./tools/make_app_bin.sh <build_dir> if you used PLATFORMIO_BUILD_DIR
./tools/merge_bin.sh                          # or ./tools/merge_bin.sh <build_dir> if you used PLATFORMIO_BUILD_DIR
```
Both outputs are gitignored (build artifacts) except when deliberately
tracked for sideloading -- regenerate after any firmware change rather than
trusting an old copy. Flash the merged image from any machine with just
`esptool.py` installed, no PlatformIO:
```
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 0sixty-v<version>-merged.bin
```
Verified on real hardware (2026-09-16): flashing purely from this merged
image boots identically to a normal `pio run -t upload` (same free-heap
reading, same GPS fix).

**If flashing via a web tool (e.g. [esptool-js](https://espressif.github.io/esptool-js/))
instead of the CLI**: its Flash Address field defaults to `0x1000`, which is
correct for classic ESP32 but wrong for this ESP32-S3 board -- `merge_bin.sh`
builds the image starting at `0x0`. Writing it at `0x1000` shifts the whole
image 4KB into flash, so the ROM bootloader can't find a valid header at
`0x0` and the device boots to a black screen (confirmed on real hardware,
2026-09-16 -- recovered by reconnecting, the ROM bootloader is unaffected,
and reflashing the same file at `0x0`). **Always change that field to `0x0`
before clicking Program.** Leave Flash Mode/Frequency/Size on `keep` -- the
image's header already has the right `dio`/`80MHz`/`8MB` values baked in by
`merge_bin.sh`. Don't use Erase Flash first either; it would also wipe the
NVS region holding your saved theme/mode/unit preferences, which a normal
write doesn't touch.

## Using the dashboard

1. Flash and power the device -- it briefly shows the WiFi AP name on its
   own screen at boot.
2. On your phone, join the WiFi network named in `AP_SSID` (`include/config.h`,
   currently `0sixty`) -- open, no password (v1.1). Press `Q` on the
   device for a join QR code, and a captive-portal prompt should open the
   dashboard automatically once connected.
3. If it doesn't auto-open, browse to `http://192.168.4.1` (ESP32's default
   SoftAP address) manually.
4. Live speed, GPS fix/sat status, battery, and the active mode's stats
   (Performance: graph + splits table; Trip: distance/duration/max speed,
   plus a live-drawn route line built client-side from accumulated GPS
   points -- no basemap or map tiles, just the raw track) update over a
   WebSocket. No app install, no internet needed (and none available -- your
   phone loses its own internet connection while joined to this AP).
5. Tap **Switch Mode** to flip between Performance and Trip; the dashboard's
   own layout switches to match within one broadcast tick (~100ms).

## On-device UI

Rewritten this session (2026-09-16) to fix a visible flash on every refresh
and to actually answer "how do I interact with this":

- **Flicker fix:** the old code cleared the whole 240x135 panel to black and
  redrew directly on top of it 4x/second, which is what was flashing --
  every refresh briefly showed a blank black frame before the redraw caught
  up. `display.cpp` now draws into an offscreen `M5Canvas` sprite and pushes
  it to the panel in one go, so the panel itself never shows a partial frame.
- **Battery:** a small icon + percentage in the header, read from
  `M5Cardputer.Power.getBatteryLevel()`. Shows `?` if the hardware can't
  report a level.
- **Color themes:** four presets (green/red/blue/white, Pip-Boy-style bright
  color on near-black) cycled with `C`, persisted across reboots. See
  `src/theme.cpp` if you want to tune the actual RGB values -- they're plain
  0-255 triples, not hand-computed hex.
- **Footer key hints:** always visible, so the controls table above is a
  reference, not a requirement.
- **Trip mode's screen (redesigned 2026-09-16):** state + duration,
  distance, live speed, and max speed, each on its own line, plus a
  smaller-text points-counter line (`Pts:x/y`) below them. The old
  speed-band color-swatch legend was dropped to make room for the live speed
  reading -- the KML export's speed bands are unaffected, see
  `TRIP_SPEED_BANDS` in `config.h`.

This was reviewed against the real M5GFX/M5Cardputer library sources rather
than assumed, but I (Claude) cannot see the physical screen -- please check
that the flashing is actually gone and the themes/battery icon look right
the first time you power it on after this update.

## Restoring Bruce

This firmware replaces the Bruce multi-tool firmware that was previously
flashed to this device. To go back, reflash Bruce's release binary the same
way it was originally flashed (see `project_cardputer_lora.md` in memory for
the esptool command used).

## Known issues

- **GNSS output rate** is capped at whatever the ATGM336H-6N@AT6668 defaults
  to (likely 1Hz) until its CASIC/`$PCAS` configuration command set is known
  -- see the `configureRate()` stub in `src/gps.cpp`. Waiting on the module
  datasheet.
- **Performance mode's arm/trigger state machine is still functionally
  untested** -- see "Status" above. It needs a human with the physical
  device (a drive, or at minimum a push/shake test) to confirm before
  trusting the numbers it produces. Trip mode, by contrast, has now been
  field-tested with a real drive (see "Status" above) -- that test is what
  caught and fixed the 0.00 km/h recording bug.
- **Trip buffer size (`TRIP_MAX_POINTS`/`TRIP_SAMPLE_MS` in `config.h`) is a
  RAM/duration tradeoff, tuned once on real hardware.** The first value
  tried (3600 points @ 2s = 2 hours) left only 65KB free heap with WiFi +
  the web server already up -- too tight a margin. The current value (1800
  points @ 4s, same 2-hour cap, coarser track resolution) leaves ~123KB
  free, measured on real hardware. If you want finer-grained trip tracking
  over a shorter max duration, or a longer max duration at the current
  resolution, this is the tradeoff to re-tune, and re-measure free heap
  after doing so (`Serial.printf` line in `main.cpp`'s `setup()`). Filling
  the buffer only stops the recorded track (and so the KML/CSV export's
  resolution) from growing further -- live distance/duration/max-speed
  stats keep updating for the rest of the drive either way.
