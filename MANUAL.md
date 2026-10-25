# 0sixty -- User Manual (v1.5.0)

*by Allelujah*

A GPS performance timer ("0sixty"-style 0-60 runs) and long-drive trip
logger on an M5Stack Cardputer ADV + LoRa1262 Cap (used only for its GNSS
chip). This manual covers day-to-day use; see `README.md` for build/flash
instructions and technical background.

## Getting your phone connected

1. Power on the device -- it briefly shows the WiFi network name on its own
   screen.
2. Press **`Q`** on the device's keyboard to show the WiFi-join QR code.
3. Scan it with your phone's camera app (not a separate QR scanner --
   standard camera apps handle WiFi QR codes directly). It should join the
   network and pop up a "Sign in to network" prompt automatically.
   - If the prompt doesn't appear, open a browser and go to
     `http://192.168.4.1` manually.
   - The network is **open** (no password) -- the QR code is just a
     convenience for typing nothing.
4. Press **`Q`** again on the device to return to the normal screen.

Your phone has **no internet** while joined to this network (by design --
the device's WiFi radio only serves its own AP). Downloads/exports work
fine; anything else (maps, browsing) won't until you reconnect to normal
WiFi or data.

## The two modes

Press **`M`** on the device (or tap **Switch Mode** on the dashboard) to
flip between them. Same firmware, no reflashing.

### Performance mode ("0sixty")

Two ways to arm, switchable any time from the Settings menu (**`S`** key, or
the dashboard's Settings section -- see below):

- **Auto** (default): come to a stop, wait a moment, and the device arms
  itself.
- **Manual**: nothing arms on its own -- press **`A`** (or the dashboard's
  **Arm** button) when you're ready.

Either way, once armed, the run itself still starts automatically: launch and
it starts timing the instant GPS speed **or the accelerometer** crosses its
threshold, whichever happens first -- no button to press mid-run. Manual
mode exists because real-world testing showed Auto can mis-fire: with the
device just sitting stationary, ordinary GPS speed jitter (worse with a
marginal antenna connection) can repeatedly cross the auto-arm/trigger
thresholds and log a flood of meaningless sub-second "runs". If you see a lot
of near-zero-distance runs in your history, that's what happened -- switch to
Manual and only arm right before you actually mean to launch.

- **0-10 through 0-100 km/h splits**, a **quarter-mile time + trap speed**,
  a **60-130 km/h rolling split** (time between crossing 60 and crossing
  130, wherever that happens in the run), and **peak accelerating/braking
  G-force** (from the onboard accelerometer) -- all shown on the phone
  dashboard.
- The device's own small screen shows state, elapsed time, distance, the
  0-60 split, and a live speed sparkline.
- A run ends when it reaches target speed, drops back off, or after 60
  seconds (safety timeout). **Results stay on screen and on the dashboard
  until your next launch** -- they don't disappear just because you've
  come to a stop (fixed in v1.2.0; earlier versions cleared them almost
  immediately, a real bug, not intentional).
- Download the last completed run as CSV from the dashboard (available even
  if you've since switched to Trip mode), or open its **printable report**
  (headline stats, splits table, speed graph) and use your phone's own
  Print dialog to save it as a PDF -- no app needed.
- **Every run is saved to the SD card**, not just the last one. The
  dashboard's **Performance History** section lists every past run with a
  report/CSV link for each, same as Trip History.

### Trip mode (GPS tracker)

Same Auto/Manual choice as Performance mode, in the same Settings menu:

- **Auto** (default): starts recording once you've been moving faster than
  ~5 km/h for 5 seconds straight, and stops and saves once you've been at or
  below ~3 km/h for 3 minutes straight (i.e. actually parked, not just
  stopped at a light).
- **Manual**: press **`A`** (or the dashboard's **Start/Stop** button) to
  start recording right now, and again to stop and save. Works as a quick
  override in Auto mode too, if you don't want to wait out the hold timers.
- When it stops, the device shows a **trip summary screen** (name, distance,
  duration, max speed, "saved to SD"). Press **`X`** to dismiss it and arm
  for the next trip.
- You can still press **`X`** mid-trip to force-end and save it early if you
  want to split a drive into two trips.

Every trip is saved directly to the SD card as it's recorded (not just held
in memory), so a trip survives a crash or dead battery -- you don't need to
download it before powering off.

**Naming a trip or run:** press **`N`** on the Cardputer and type it there,
or use the "Trip / vehicle name" field on the dashboard. Naming a trip while
one is already recording renames it immediately; otherwise the name is held
for the next recording that starts. Names are limited to letters, digits,
`-` and `_` (anything else becomes `-`), because the name goes into the
saved file's name as well as its contents.

**Dates and times** come from the GPS, which is the device's only clock --
there's no battery-backed clock on board. The first fix of each session sets
the clock, and it keeps running afterwards even if you lose the fix in a
tunnel or a car park. A recording started *before* the first fix of a
session honestly shows "no date" rather than a made-up one.

**Hard-event markers:** a hard brake, swerve, or bump gets flagged
automatically (shown as an event count on the dashboard) and saved into the
trip file with its time and location. This is a generic accelerometer-based
heuristic, not vehicle-specific -- expect it to need some real-world tuning
(see `config.h`'s `HARD_EVENT_THRESHOLD_G` if it's too trigger-happy or too
quiet).

## The dashboard (`http://192.168.4.1`)

- **Live view**: speed, GPS fix/satellites, battery, and the active mode's
  stats over a live WebSocket connection.
- **Trip History**: every trip ever saved to the SD card, with its own KML
  and CSV download links -- not just the one that's currently active or
  just recorded. **To view a KML in Google My Maps:** download it, then
  disconnect your phone from 0sixty's WiFi and open `mymaps.google.com`
  yourself in your normal browser (Import -> pick the file from Downloads).
  The imported map is split into driving legs rather than one line: each is
  coloured by speed band and labelled with its real time range, distance and
  average speed (`08:12-08:47  32.4 km  87 km/h avg`), with avg/max speed in
  the description when you click it. Stops of more than a couple of minutes
  appear as their own pins (`Stopped 09:02-09:18 (16 min)`). The layer panel
  shows the trip's **total distance and duration** in the layer name (e.g.
  "0sixty Trip — 123.4 km, 2h 34m") -- visible at a glance without clicking
  anything. A **yellow-star "Trip Summary" pin** at the last GPS point gives
  full totals (distance, duration, avg speed, max speed) in a tap-to-open
  popup. Opening the same file in Google Earth instead will animate the drive
  along its timeline, since each leg carries a timestamp range.
  Don't expect a link on this page to still work after disconnecting -- the
  QR-code join opens the dashboard inside a temporary "sign in to network"
  view that Android closes the moment you disconnect, confirmed on real
  hardware.
- **Settings**: the same Auto/Manual start-mode toggles as the on-device
  Settings menu (`S` key) -- either surface works, they stay in sync. An
  **Arm** / **Start/Stop** button next to the mode switcher does the same
  thing as the on-device `A` key, useful for arming from your phone without
  reaching for the keyboard.
- **Home WiFi Auto-Upload** (optional, off by default): enter your home
  WiFi's SSID/password and an upload URL, and the device will try to push
  any not-yet-uploaded trip file there whenever it's parked near that
  network. Leave the SSID blank to keep this off. **This is new and
  untested** -- try it once deliberately and confirm a trip actually arrives
  at your configured URL before relying on it.

## On-device keys

| Key | Action |
|---|---|
| `M` | Switch Performance <-> Trip mode |
| `C` | Cycle the color theme: green -> red -> blue -> white |
| `U` | Toggle km/h <-> mph, and km <-> mi for Trip's distance (on-device readouts only) |
| `X` | Trip mode: dismiss the summary screen / force-end and save the current trip |
| `Q` | Show/hide the WiFi-join QR code |
| `N` | Name the next trip or run, typed on the device (Enter saves, Del backspaces, `` ` `` cancels) |
| `A` | Manual arm (Performance) / manual start-stop (Trip) -- only needed day-to-day in Manual start mode, but works as a quick override in Auto too |
| `S` | Open/close the Settings menu: Performance/Trip start mode (Auto/Manual), toggled with `1`/`2`, `` ` `` to close |

Theme, mode, unit, and the two start-mode settings all persist across power
cycles. The footer at the bottom of every screen lists most of the keys that
work on that screen (`C`/`U` aren't listed on the Performance/Trip screens
themselves to make room for `A`/`S`, but still work).

## Battery

- A small icon + percentage always shows in the header.
- Below 15% battery, the screen dims automatically to stretch runtime; it
  brightens again once back above 20%.

## Getting your data

- **Live/recent data**: the dashboard's download links (Performance: last
  run CSV, or its printable report; Trip: live KML/CSV for the trip in
  progress or just completed).
- **Any past run**: the Performance History list on the dashboard, any
  time, as long as the SD card is in the device -- each entry has its own
  report and CSV link.
- **Any past trip**: the Trip History list on the dashboard, any time, as
  long as the SD card is in the device.
- **Off the SD card directly**: files live under `/runs/` (one per
  Performance run) and `/trips/` (one per trip) on the card, plain CSV,
  openable in any spreadsheet or text editor (lines starting with `#` are
  metadata/event comments, not data rows).

## Known rough edges (v1.5.0)

See `CHANGELOG.md`'s "Known limitations" section for each version --
notably: auto trip start/stop timing, the hard-event threshold, and home
WiFi auto-upload are all first-pass defaults that haven't been tuned
against a real drive yet, and the v1.2.0 results-persist fix and run
history/SD-save haven't themselves been confirmed on a real run. If
something feels off (trip starts too early/late, too many/few hard-event
flags), the tunable values are in `include/config.h` with comments
explaining each one.

Recordings started before the session's first GPS fix carry no date (see
"Dates and times" above). Everything saved before v1.3.0 also predates
dates and names entirely -- those files still list and download correctly,
they just show "no date" and "(unnamed)".

Manual start mode and the G-sensor launch trigger (v1.5.0) are confirmed
compiling and boot-tested, but not yet validated against a real drive --
`PERF_TRIGGER_ACCEL_G` (config.h) is a first guess at how hard a "launch"
push needs to be, same caveat as the other first-pass thresholds above.
Peak accel/brake G is recorded from the same single-axis forward
calibration the rest of Performance mode uses, so it only reads correctly
once the device has calibrated (a real push/launch locks the forward axis --
see README.md's IMU section).
