# 0sixty -- User Manual (v1.4.1)

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

Auto-armed: come to a stop, wait a moment, and the device arms itself. Launch
and it starts timing automatically -- no button to press mid-run.

- **0-10 through 0-100 km/h splits**, a **quarter-mile time + trap speed**,
  and a **60-130 km/h rolling split** (time between crossing 60 and crossing
  130, wherever that happens in the run) -- all shown on the phone dashboard.
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

No button to start or stop a drive -- it's automatic:

- **Starts recording** once you've been moving faster than ~5 km/h for 5
  seconds straight.
- **Stops and saves** once you've been at or below ~3 km/h for 3 minutes
  straight (i.e. actually parked, not just stopped at a light).
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
  appear as their own pins (`Stopped 09:02-09:18 (16 min)`). Opening the
  same file in Google Earth instead will animate the drive along its
  timeline, since each leg carries a timestamp range.
  Don't expect a link on this page to still work after disconnecting -- the
  QR-code join opens the dashboard inside a temporary "sign in to network"
  view that Android closes the moment you disconnect, confirmed on real
  hardware.
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

Theme, mode, and unit persist across power cycles. The footer at the bottom
of every screen always lists the keys that work on that screen.

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

## Known rough edges (v1.4.1)

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
