# Changelog

## v1.4.1 (2026-09-19)

Independent code review of the v1.4.0 KML rework, before any of it had been
exercised by a real drive. Five fixes, two of which undermined the point of
the rework itself.

### Fixed

- **Every stop produced a pin AND a meaningless sliver.** `KML_STOP_MIN_MS`
  (2 min) is six times `KML_MIN_SEGMENT_MS` (20s), so a real stop would
  first trip the ordinary band-change debounce -- spawning a near-zero
  "0-30 km/h" leg -- and only ~100s later be recognised as a stop. The
  result was a "Stopped" pin and a co-located, near-zero-distance coloured
  line at every single stop: exactly the clutter v1.4.0 existed to remove.
  Band splitting is now suppressed while a possible stop is in progress.
- **A trip's final leg could vanish from the map.** Legs with fewer than two
  points were discarded outright, and the likeliest place to produce one is
  arrival: a fresh leg opens as you slow to a stop, then recording ends
  before a second point lands. Single-point legs are now emitted as a point
  rather than dropped.
- **Unsigned-subtraction underflow in both timestamp paths.**
  `tMs - firstMs` had no guard. On the live path a `millis()` rollover
  (~49 days uptime) could trigger it; on the historical path `tMs` comes
  straight from CSV column 0 with no sanity check, so a torn row from a
  power loss mid-write could too. Either would have baked an absurd date
  into a leg's name and `<TimeSpan>`. Both now fall back to "no time"
  rather than inventing one.
- **The timestamp anchor is snapshotted once per chunk** instead of re-read
  per point. The main loop can stop one trip and start another on the other
  core mid-export, which would have stamped the tail of one trip's points
  with the next trip's anchor.
- **The buffer-cap split no longer discards an in-flight band-change
  debounce**, which delayed recognising a genuine transition that was
  already partly confirmed.

### Known and accepted

- A leg boundary credits the joining step's distance (up to one sample
  interval, ~130 m at 120 km/h) to the new leg. That falls out of keeping
  the odometer continuous across boundaries; documented in
  `kmlAddPoint()` rather than "fixed" into something worse.

## v1.4.0 (2026-09-19)

Reworks the KML export after seeing what it actually produced in Google My
Maps: a sidebar of hundreds of entries, every one labelled only with a
speed *band* ("0-30 km/h"), none showing an actual speed and none showing a
time.

### Fixed

- **The export no longer shatters a drive into slivers.** It used to open a
  new Placemark on *every* speed-band crossing, so speed hovering near a
  boundary in traffic (29 -> 31 -> 28 km/h) produced a new segment each
  time. A band change now has to persist -- `KML_MIN_SEGMENT_MS` (20s) or
  `KML_MIN_SEGMENT_M` (250m) -- before it splits a leg, which collapses
  hundreds of fragments into a handful without losing the real transitions.
- **Segments carry real numbers instead of a band label.** Each leg is now
  named with its time range, distance and average speed, e.g.
  `08:12-08:47  32.4 km  87 km/h avg`, with avg *and* max speed, distance
  and time range in the description. "How fast was I along here" is finally
  answerable from the map.

### Added

- **Stop detection**, mirroring how Google Timeline separates driving legs
  from visits: two minutes below 3 km/h ends the current leg and drops a
  labelled pin (`Stopped 09:02-09:18 (16 min)`).
- **Timestamps in the KML.** Each leg gets a `<TimeSpan>`, so Google Earth
  can animate a drive along its timeline. This is only possible now that
  v1.3.0 gave the device a clock -- a point's `millis()` stamp is anchored
  to the recording's `startedEpoch`.
- Historical trips get times too: the exporter reads `startedEpoch` out of
  the saved file's metadata header. Recordings made before v1.3.0 have no
  such field, so they export exactly as before but **without** times rather
  than with invented ones.

### Notes

- Colours are unchanged -- the route is still painted by speed band, there
  are simply far fewer and far more meaningful pieces of it.
- KML requires `<name>` before `<coordinates>`, but a leg's statistics
  aren't final until it ends, so a leg's coordinate text is buffered until
  close. That buffer is capped (`KML_MAX_SEGMENT_BUFFER`, 8 KB): a leg that
  outgrows it is split into a same-coloured continuation, which keeps peak
  heap bounded on a long motorway run that never changes band.

## v1.3.1 (2026-09-18)

### Added

- **Author signature ("Allelujah") throughout.** Defined once as
  `AUTHOR_NAME` in `include/config.h` alongside `FIRMWARE_VERSION`, so it
  can't drift out of sync, and surfaced everywhere it's useful:
  - **Boot splash** on the device -- `0sixty v1.3.1` / `by Allelujah`.
  - **Serial banner** printed at startup.
  - **Dashboard footer**, via the `/api/version` endpoint (which now
    returns an `author` field alongside `version`).
  - **Printable report / PDF** -- a byline under the title, so a shared or
    printed run report carries the attribution.
  - **Exported data**: an `author=` field in every trip and run metadata
    header, and a `<description>` element in the KML (which shows up in
    Google My Maps when a trip is imported). The live trip CSV export gets
    a `# recorded with 0sixty ... by ...` comment line.

  Because the author is stamped into each recording *at capture time*, an
  old report keeps whatever signature it was recorded under rather than
  being retroactively relabelled; the report page falls back to the running
  firmware's author only for files that predate the field.

  Deliberately **not** added to the live driving screens (status,
  performance, trip) -- at 240x135 those are already dense, and a byline
  competing with speed and split times while driving is the wrong trade.

## v1.3.0 (2026-09-18)

Five user-requested changes, plus the plumbing they turned out to need.

### Added

- **Real dates and times on every recording.** The device has no RTC, and
  earlier versions documented "no calendar date" as a hard limitation --
  that was too pessimistic. The GNSS stream carries UTC date/time all
  along; it simply was never read. It is now, and the first fix of each
  session latches it into the ESP32's own system clock, so timestamps keep
  working afterwards **even if the fix is lost** (tunnel, underground car
  park). Local time uses the C library's POSIX timezone rules for
  Europe/Brussels, so CET/CEST switches on the correct Sundays with nothing
  to maintain seasonally.
- **On-device naming (`N` key).** Type a name for the next trip or run on
  the Cardputer itself, instead of only from the dashboard. Enter saves,
  Del backspaces, backtick cancels. While the entry screen is up the normal
  single-key shortcuts are suspended -- otherwise typing a name containing
  "m" would switch modes mid-word.
- **Performance runs have names too.** They previously had no name concept
  at all -- only trips did. Names now flow through the run's metadata, the
  history list, the report page, and the filename.
- **Names and timestamps in filenames**, e.g.
  `trip00012_20260918-1432_Belgium.csv`. The id stays first so files still
  sort correctly and the id remains recoverable. Either part is omitted
  when unavailable, so an unnamed recording made before the first GPS fix
  is simply `trip00013.csv` -- the exact v1.2 shape.
- **Delete buttons** next to each recording's KML/CSV links, with a
  confirmation prompt. Filenames are validated server-side (plain basename,
  correct prefix and extension, no path separators) so a crafted request
  can't reach anything else on the card, and the trip currently being
  recorded refuses to be deleted out from under itself.

### Changed

- **Performance and Trip history are now separate**, each shown only in its
  own mode, rather than both lists stacked on every screen.
- **Metadata parsing is now key-based instead of positional.** This is
  load-bearing for backward compatibility: the old parser consumed fields
  strictly in order, so adding `startedEpoch` and run names would have
  silently shifted every later field when reading a file written by older
  firmware -- producing plausible-but-wrong numbers rather than an obvious
  failure. Fields are now looked up by name, so existing recordings from
  earlier field tests still read correctly, and future additions won't
  break them either.
- **Names are sanitized** to letters, digits, `-` and `_` (anything else
  becomes `-`), since a name now has to survive being embedded in a
  filename as well as in a comma-delimited metadata line. Trip naming from
  the dashboard previously allowed spaces; it no longer does.

### Fixed before release (independent code review)

- **Two unsynchronized cross-task `String` accesses**, the same defect class
  v1.1 already fixed once for `TripMode`/`Settings`. The new
  `PerformanceMode::_currentName` was written by the main loop when a run
  starts and read from the web server task via `/download/run.csv` with no
  lock; and `deleteTrip()`'s "don't delete the trip being recorded" guard
  compared `_currentTripFilename` *outside* the mutex that every writer of
  that field holds. Both are use-after-free risks rather than just stale
  reads, since `String` reallocates on assignment -- and in the delete case
  the guard could also have read false spuriously and removed a file still
  open for writing. Both now properly locked.
- **The `N` key could fire another key's action on the same frame it opened
  the naming screen.** The modal check ran once at the top of the keyboard
  handler, so a key pressed in the same scan as `N` was still dispatched
  normally -- `N`+`X` in Trip mode would open the name entry *and* end the
  recording in progress.

### Known limitations

- **Not yet tested on hardware**, same caveat as every release since v1.1.
- `idFromFilename()` returns id 0 for a malformed hand-placed filename
  (e.g. a file literally named `trip.csv`) rather than flagging it as
  unparseable. This firmware never generates such a name; left as-is.
- **A recording started before the session's first GPS fix has no date.**
  It reports "no date" and omits the timestamp from its filename rather
  than inventing a wrong one. Once any fix lands, the clock stays set for
  the rest of the session.
- The UTC-to-epoch conversion is hand-rolled (`daysFromCivil()` in
  `timeutil.cpp`) because this toolchain's newlib provides no `timegm()`,
  and `mktime()` would have applied the local offset a second time.

## v1.2.1 (2026-09-17)

**Independent code review, requested via "wrap it up" before this session
ended** (v1.2.0 itself had shipped without one). Found one real bug and one
pre-existing, out-of-scope issue worth documenting:

### Fixed

- **Slope% wasn't actually included in v1.2.0's "results stay frozen" fix.**
  `PerformanceMode::slopePercent()` computed live from `_lastAltitudeM`/
  `_startAltitudeM`, both of which keep tracking the current GPS altitude
  even after a run finishes (`_startAltitudeM` re-tracks continuously while
  `ARMED`, in preparation for the *next* launch; `_lastAltitudeM` updates
  unconditionally every sample regardless of state). So while distance, max
  speed, splits, quarter mile, and the rolling split all correctly froze at
  v1.2.0, the slope% stat kept silently drifting away from the real value
  for the entire waiting period after a run -- previously unnoticeable
  (the old bug wiped everything in ~500ms anyway), now clearly wrong once
  results actually stick around. Fixed by freezing slope% the same way as
  everything else, at the exact instant a run finishes.

### Known, deliberately not fixed this pass

- **`/download/run.csv` reads live `PerformanceMode` fields from the web
  server's own task with no synchronization against the main loop** that
  writes them. Confirmed pre-existing (the same unsynchronized reads
  existed in the old `buildRunCsv()` before v1.2.0's refactor) and bounded
  -- not a crash risk, just possibly-corrupted numbers in a CSV, and only
  if you happen to download mid-run or in the exact instant a new run
  starts. A proper fix would need the same kind of mutex `storage.cpp`
  already uses, but wrapping every `PerformanceMode` field it touches is a
  bigger change than this pass's scope. Left as a known limitation.

## v1.2.0 (2026-09-17)

**User-discovered bug, second round of real field feedback:** after doing a
run, the user checked the device and saw no results at all. Root cause was
a real bug, not a missing feature -- `PerformanceMode`'s `FINISHED` state
called `resetRun()` (wiping distance/splits/quarter-mile/everything) the
moment the car sat still for just 500ms, which happens almost immediately
after any run ends. The results were being deleted before there was any
real chance to look at them.

### Fixed

- **Results now stay until the next run actually starts.** `resetRun()`
  moved from the "car stopped, re-arming" transition to the "a new launch
  was just detected" transition (`src/modes/performance.cpp`). Re-arming in
  the background is unaffected; what's displayed (on-device and the
  dashboard) now correctly stays frozen on the last completed run through
  the whole waiting period, only clearing when a genuinely new run begins.

### Added

- **Every completed Performance run now saves to SD**, one file per run
  under `/runs/runNNNNN.csv` (mirrors Trip mode's per-trip files) --
  written in one shot right when the run finishes, since a run's whole
  history already fits in RAM (no need for Trip mode's incremental
  streaming-while-recording approach). No more "only the last run
  survives a reboot or the next run."
- **Performance History** section on the dashboard -- every saved run,
  listed with its 0-60/quarter-mile/max speed, each with its own CSV
  download and report link. Same pattern as Trip History.
- **Printable report / "export as PDF"** -- a new `/report/run.html` page
  (linked from both the live run and every history entry) renders a clean
  report -- headline stats, full splits table, speed graph -- with a
  "Print / Save as PDF" button. No PDF library on the device: it just
  parses the same CSV the download links already produce and hands off to
  the phone's own browser print dialog, where "Save as PDF" is already a
  standard option. Works for the live/last run and any historical one.
- On-device SD-save failures are now surfaced instead of silently lost --
  the dashboard shows "DONE (not saved!)" if a run's SD write failed
  (`PerformanceMode::lastRunPersisted()`), same pattern as Trip mode's
  summary screen fix in v1.1.

### Known limitations

- **No real-world testing yet** -- this was built and compiled without
  physical access to the device, same caveat as every prior release. The
  auto-reset fix and SD persistence follow the same patterns already
  validated for Trip mode, but haven't themselves been confirmed on a real
  run.
- **No calendar date/time on runs** -- the device has no real-time clock
  source (GPS time isn't currently captured), so runs are identified only
  by an incrementing ID, not a date. The report page deliberately doesn't
  claim a date it doesn't have.

## v1.1.1 (2026-09-17)

**User-discovered bug, first real field feedback on v1.1:** the dashboard's
"Open Google My Maps" link never actually worked, for a reason nobody
(human or Claude) had reasoned through beforehand -- the QR-code captive
portal (see below) opens the dashboard inside Android's special "sign in to
network" WebView, not a normal browser tab. That view has no internet while
connected (expected, by design), but it *also gets torn down by the OS the
instant you disconnect from the network* -- so the documented "download the
KML, then disconnect and tap the My Maps link" flow was broken twice over:
the link could never work while connected, and after disconnecting there
was no page left to click it from at all.

**Fixed:** removed the dead link entirely and rewrote the dashboard's hint
text to say what's actually true -- download the KML, disconnect from
0sixty's WiFi, then open `mymaps.google.com` yourself in your phone's normal
browser and import the file from Downloads. The download itself was never
affected by this; only the in-page shortcut link was structurally incapable
of surviving the disconnect. `src/webserver.cpp`'s `myMapsLink` element and
its JS references removed; `myMapsHint` text rewritten.

### Added

- **Firmware versioning (new project rule).** Every compiled `.bin` now
  carries a version: small/isolated fixes bump **patch**, feature batches
  bump **minor** or **major**. `FIRMWARE_VERSION` in `config.h` is the
  single source of truth -- shown on the boot screen and the dashboard
  footer (new `/api/version` endpoint), and read automatically by
  `tools/merge_bin.sh` and the new `tools/make_app_bin.sh` to name their
  output (`0sixty-v<version>-app.bin` / `0sixty-v<version>-merged.bin`).
  This release is the first to carry a version, hence v1.1.1.

## v1.1 (2026-09-17)

Built overnight in one session, without physical access to the device --
everything below compiles clean (RAM 35.2%, Flash 34.2%) but **nothing has
been validated on real hardware yet**. See "Needs your hands on the device"
at the bottom before trusting any of it on a real drive.

### Added

- **WiFi-join QR code** (`Q` key, any mode) -- replaces the old "join this AP
  and type this password" flow. The AP is now **open, no password**
  (`config.h`/`webserver.cpp`); scanning the code (`WIFI:S:<ssid>;T:nopass;;`)
  joins it directly. Drawn black-on-white full-screen regardless of your
  color theme, for scanner contrast.
- **Captive portal** -- joining the AP (via the QR code or manually) now
  auto-opens the dashboard on your phone, the same way public WiFi captive
  portals work, instead of needing you to type `192.168.4.1` yourself.
- **Auto trip start/stop** -- Trip mode no longer records continuously
  whenever selected. It now auto-starts after sustained movement
  (`AUTO_TRIP_START_SPEED_KMH`/`_HOLD_MS`, config.h) and auto-stops after
  being parked for a while (`AUTO_TRIP_STOP_*`). No more remembering to
  press a key when you drive off.
- **Trip summary screen** -- when a trip auto-stops, the device shows a
  frozen "trip complete" card (name, distance, duration, max speed) instead
  of just idling. `X` dismisses it and arms for the next trip.
- **Trips now save to the SD card**, one file per trip
  (`/trips/tripNNNNN.csv`), as they're recorded -- not just held in RAM until
  you download them over WiFi before power-off. Each file is plain CSV with
  `#`-prefixed metadata/event comment lines (same convention `run.csv`
  already used for its splits section).
- **Trip naming / vehicle tagging** -- set a name for the current or next
  trip from the dashboard; it's saved into the trip's SD file and shown on
  the on-device summary screen.
- **Trip history browser** on the dashboard -- lists every trip saved to the
  SD card (not just the live one), each with its own KML/CSV download link.
- **Hard-event flagging** -- a generic brake/swerve/bump heuristic (total
  accelerometer magnitude deviating from 1g) logs a marker (time + position)
  into the active trip, visible in the dashboard's event counter and the
  trip's SD file (`# event,...` lines). Needs real-world threshold tuning --
  see "Needs your hands on the device."
- **Performance mode: quarter mile + 60-130 km/h rolling split** -- alongside
  the existing 0-X splits table. Required raising the run's auto-end
  threshold and timeout (`PERF_MAX_TARGET_KMH` 105->140,
  `PERF_MAX_RUN_MS` 30s->60s) so a run doesn't get cut off before reaching
  130 or the quarter-mile mark.
- **Low-battery power save** -- screen brightness drops under 15% battery
  and recovers at 20% (hysteresis to avoid flickering right at the
  threshold).
- **Home WiFi auto-upload (experimental, off by default)** -- optionally
  configure a home SSID/password + an upload URL from the dashboard; while
  parked (never mid-trip), the device briefly joins that network
  (concurrent with its own AP -- ESP32-S3 supports AP+STA) and POSTs any
  not-yet-uploaded trip file there. **Untested** -- there was no home WiFi
  available during development to confirm this end-to-end. Leaving the SSID
  field blank keeps it fully inactive.

### Changed

- SD access is now guarded by a mutex (`storage.cpp`'s `SdLock`/`_sdMutex`).
  Before v1.1, every SD read/write happened from the main loop only; the new
  trip-history downloads and the auto-upload feature read SD from the web
  server's own task, so this is now a genuine cross-task resource and needed
  real locking, not just the single-task discipline the original design
  relied on.
- `AP_PASSWORD` removed from `config.h` -- the AP is open now (see QR code
  above).
- On-device footer key hints on every screen now include `Q:qr`.

### Fixed (pre-release code review, before this ever reached hardware)

An independent review pass caught several real bugs in the first draft of
the features above, all fixed before this release:

- **Trip summary could claim "Saved to SD" when it wasn't.** If the SD card
  is missing/full/errored right when a trip auto-starts, the trip still ran
  in RAM only, but the summary screen said "Saved to SD" regardless. It now
  tracks whether the SD write actually succeeded and shows "NOT saved
  (check SD!)" when it didn't.
- **Cross-task race on shared trip-name/sync-config strings.** The dashboard
  (running on a separate task from the main GPS/IMU loop) can rename a trip
  or save sync settings at any moment; those values are plain `String`s,
  whose internal buffer can reallocate on write. A concurrent read during
  that reallocation is a real corruption/crash risk, not just a style
  concern. Now guarded by mutexes (`TripMode`'s `_nameMutex`, `Settings`'
  `_mutex`).
- **Home WiFi sync could freeze the whole device for up to 8+ seconds.** The
  original implementation blocked the main loop with `delay(100)` while
  waiting to join your home WiFi -- during that freeze, GPS/IMU updates,
  keyboard input, and display refresh all stopped, and an in-progress
  Performance run's split times would have been silently thrown off. Sync is
  now a non-blocking state machine (`sync.cpp`) that only ever does one
  bounded step per loop() call, and its "stay out of the way" check now
  covers an armed/running Performance run too, not just an active Trip
  recording.
- **Trip mode could auto-start or auto-stop instantly and wrongly right
  after switching back into it.** The auto-start/stop timers kept their
  timestamps from before you switched to Performance mode and back; if
  enough real time had passed, the very first check after returning could
  fire immediately off a stale timestamp, with no actual continuous
  movement or parking behind it. Both the on-device `M` key and the
  dashboard's Switch Mode button now clear these timers on the way out of
  Trip mode.
- **`Storage::listTrips()` allocated from the heap on every call**
  (`new`/`delete`), against this project's own "no dynamic allocation"
  convention and a real crash risk on this device's tight free heap over a
  long session. Now a fixed-size array, safe to make `static` because the
  whole function already runs under the SD mutex.

### Known limitations / what still needs your hands on the device

Nothing below can be confirmed without physically holding the device:

- **QR scan itself** -- the code is generated and rendered on-screen, but
  whether a real phone camera actually reads it cleanly off the small
  240x135 panel (module size, quiet zone, screen glare/angle) is unverified.
- **Captive portal auto-launch** -- the DNS + redirect logic follows the
  standard pattern, but whether iOS/Android actually pop the "Sign in to
  network" prompt for this specific setup hasn't been seen firsthand.
- **Auto trip start/stop thresholds** (5 km/h for 5s to start, <=3 km/h for
  3 minutes to stop) are first guesses, not tuned against a real drive --
  they might start too eagerly at a red light's crawl, or take a while to
  end at a longer stop.
- **Hard-event threshold** (`HARD_EVENT_THRESHOLD_G = 0.4`) is untuned --
  might fire on normal potholes/speed bumps, or miss a real hard brake.
  Adjust in `config.h` after a few real drives.
- **Home WiFi auto-upload** -- entirely untested, see above. Try it
  deliberately once parked near your home WiFi, and check the trip actually
  shows up at the configured URL before relying on it.
- **SD mutex under real load** -- the locking logic is correct by
  inspection, but its actual behavior under concurrent load (someone
  browsing trip history on their phone while a long drive is actively
  logging) hasn't been observed on hardware.
