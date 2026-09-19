#pragma once

#include <cstdint>

// ---- Firmware version ----
// Bump this every time a new .bin is built (project rule, 2026-09-17): a
// small/isolated fix bumps PATCH (1.1.0 -> 1.1.1), a batch of new features
// bumps MINOR or MAJOR depending on scope. Shown on the boot screen
// (main.cpp) and the dashboard footer (webserver.cpp's /api/version) so
// it's visible without opening this file, and read by tools/merge_bin.sh
// to name build artifacts -- keep this the single source of truth, don't
// hardcode the version anywhere else.
constexpr const char *FIRMWARE_VERSION = "1.4.1";

// Author/signature, shown on the boot splash, the serial banner, the
// dashboard footer, the printable report, and stamped into every exported
// recording's metadata. Single source of truth, same as FIRMWARE_VERSION --
// don't hardcode it anywhere else.
constexpr const char *AUTHOR_NAME = "Allelujah";

// Plain RGB565 packer, shared by theme.cpp and the trip speed-band swatches
// below so both can be specified as ordinary 0-255 RGB triples instead of
// hand-computed 16-bit hex.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
	return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3);
}

// ---- GNSS (ATGM336H-6N@AT6668, on the LoRa1262 Cap) ----
// The cap datasheet's "GPS_RX->G8, GPS_TX->G9" turned out to be connector-
// position labels, NOT literal ESP32 GPIO numbers -- G8/G9 are actually the
// Cardputer ADV's own keyboard I2C bus (SDA/SCL to its TCA8418 controller).
// Confirmed against Bruce firmware's actual working source
// (boards/m5stack-cardputer/interface.cpp), which is proven correct on this
// exact hardware (both GPS and keyboard work simultaneously there):
//   bruceConfigPins.gps_bus.rx = 15; bruceConfigPins.gps_bus.tx = 13;
//   bruceConfigPins.gpsBaudrate = 115200;
// rxPin/txPin below are from the ESP32's own perspective, matching
// HardwareSerial::begin(baud, config, rxPin, txPin)'s parameter order.
constexpr int GPS_RX_PIN = 15;
constexpr int GPS_TX_PIN = 13;
constexpr uint32_t GPS_BAUD = 115200;

// SX1262 LoRa pins on the same cap -- intentionally unused by this project,
// EXCEPT for its chip-select (see LORA_CS_PIN below), which this firmware
// must actively deselect. (An earlier version of this comment had the roles
// scrambled -- NSS=5, not 40 -- re-verified against M5Stack's own Cap
// LoRa-1262 product docs rather than trusted as-is, same lesson as the GPS
// pin mislabeling above.)
// NSS/CS=5 MOSI=14 MISO=39 SCK=40 RST=3 DIO1/IRQ=4 BUSY=6
//
// Root cause of the SD card never mounting on real hardware (found
// 2026-09-16): the SX1262 shares the SD card's exact SPI bus (MOSI/MISO/SCK
// above are identical to SD_MOSI_PIN/SD_MISO_PIN/SD_SCK_PIN below) with its
// own separate chip-select -- a supported M5Stack design where radio and SD
// coexist on one bus, but only if whichever device isn't in use keeps its CS
// deselected. This project never touched the LoRa chip's CS at all, leaving
// it floating; the SX1262 likely read itself as selected and corrupted every
// SD transaction. Confirmed on real hardware: SD.begin() succeeds with the
// cap physically removed, AND Bruce/Launcher firmware (which does properly
// park this pin) succeeds with the cap attached -- isolating this exactly.
constexpr int LORA_CS_PIN = 5;

// The keyboard's I2C bus (SDA=G8, SCL=G9) no longer conflicts with anything
// in this firmware now that GPS is correctly on 15/13 -- the keyboard is
// safe to use again (M5Cardputer.Keyboard.*), unlike in earlier revisions.

// ---- SD card (Cardputer's onboard slot, per M5Stack's own sdcard.ino example) ----
constexpr int SD_CS_PIN = 12;
constexpr int SD_MOSI_PIN = 14;
constexpr int SD_MISO_PIN = 39;
constexpr int SD_SCK_PIN = 40;

// ---- IMU fusion tuning (Phase 1 starting points, tune once on real hardware) ----
constexpr float IMU_SAMPLE_HZ = 150.0f;
constexpr float STATIONARY_SPEED_THRESHOLD_KMH = 1.5f;

// Auto-calibration: an axis deviating from its slow-tracked baseline by more
// than this (in g) is treated as a real acceleration event and locks the
// forward axis. Kept even though the keyboard works now (see above) -- it's
// still the right behavior for an unattended "0sixty-style" launch detector.
constexpr float AUTO_CALIBRATION_THRESHOLD_G = 0.3f;
// A single noisy sample above threshold used to lock calibration instantly
// (e.g. the device being picked up, not launched) -- now requires the same
// axis/direction to stay above threshold for this long before committing.
constexpr uint32_t AUTO_CALIBRATION_DEBOUNCE_MS = 80;
constexpr float BASELINE_LOWPASS_ALPHA = 0.01f; // slow tracking of at-rest per-axis reading

// Zero-velocity update: without periodic GPS corrections, dead-reckoned
// speed drifts unboundedly from accelerometer noise/bias alone. If forward
// accel stays below this for this long, treat the device as stationary and
// snap the fused speed back to zero.
constexpr float ZUPT_ACCEL_THRESHOLD_MS2 = 0.5f;
constexpr uint32_t ZUPT_HOLD_MS = 400;

// ---- Storage ----
constexpr const char *LOG_DIR = "/logs";

// ---- Performance ("0sixty") mode (Phase 3) ----
// Splits table matches the 0sixty reference: 0-10 through 0-100 km/h. Fixed
// in km/h regardless of the active display unit (Units class) -- keeps the
// crossing logic simple; the splits table is always labeled km/h even if
// the live speed readout is showing mph.
constexpr int PERF_SPLIT_THRESHOLDS_KMH[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
constexpr int PERF_SPLIT_COUNT = 10;

constexpr float PERF_ARM_SPEED_KMH = 2.0f;    // below this = "stopped" for arming
constexpr uint32_t PERF_ARM_HOLD_MS = 500;    // how long it must stay stopped to (re-)arm
constexpr float PERF_TRIGGER_SPEED_KMH = 3.0f; // sustained past this = a run has started
constexpr uint32_t PERF_TRIGGER_HOLD_MS = 100; // debounce so GPS/IMU noise can't false-trigger
// v1.1: raised from 105/30000 so a run doesn't auto-end before reaching the
// new 60-130 rolling split or a quarter-mile trap speed, both of which can
// need more time/speed than the original 0-100 splits table alone.
constexpr float PERF_MAX_TARGET_KMH = 140.0f;  // run auto-ends once past this + margin
constexpr uint32_t PERF_MAX_RUN_MS = 60000;    // safety timeout, guards a stuck "running" state
constexpr int PERF_HISTORY_MAX_POINTS = 200;   // graph buffer, matches the dashboard's own cap
constexpr uint32_t PERF_HISTORY_SAMPLE_MS = 100; // ~10Hz history recording during a run

// ---- Performance mode extras: quarter mile + 60-130 rolling split (v1.1) ----
// Quarter mile is a distance-based trap (time + speed at 402.336m from
// launch, same launch reference as the 0-X splits). The 60-130 split is a
// *rolling* window -- time between first crossing 60 and first crossing 130
// within the same run, independent of launch time -- so it's tracked
// separately from the from-a-stop splits array.
constexpr float QUARTER_MILE_M = 402.336f;
constexpr float PERF_ROLLING_LOW_KMH = 60.0f;
constexpr float PERF_ROLLING_HIGH_KMH = 130.0f;

// ---- WiFi AP + web dashboard (Phase 2) ----
constexpr const char *AP_SSID = "0sixty";
// v1.1: AP is now open (no password) -- the QR code screen (Q key) replaces
// typing a password, and WIFI: URI encoding is simpler/shorter without one
// (T:nopass instead of T:WPA;P:...). See qrscreen.cpp.
constexpr uint32_t WS_BROADCAST_INTERVAL_MS = 100; // ~10Hz to the browser, independent of internal sample rate

// ---- QR WiFi provisioning (v1.1) ----
// Fixed QR version rather than auto-sizing: the encoded text is always
// "WIFI:S:<ssid>;T:nopass;;", short and fixed in shape. Version 3 (29x29
// modules) comfortably fits it in Byte mode at ECC_LOW (53 byte capacity)
// with margin for a longer SSID than the default. If AP_SSID is ever made
// much longer, bump this and re-check against ricmoo/QRCode's capacity table.
constexpr int QR_VERSION = 3;
constexpr int QR_QUIET_ZONE_MODULES = 2; // tight vs. the usual 4 -- screen is only 135px tall

// ---- Auto trip start/stop + summary (v1.1) ----
// Trip mode no longer just records continuously whenever selected -- see
// TripMode's TripState. These thresholds are a first guess, not tuned on
// real hardware.
constexpr float AUTO_TRIP_START_SPEED_KMH = 5.0f;
constexpr uint32_t AUTO_TRIP_START_HOLD_MS = 5000;
constexpr float AUTO_TRIP_STOP_SPEED_KMH = 3.0f;
constexpr uint32_t AUTO_TRIP_STOP_HOLD_MS = 180000; // 3 minutes stopped ends the trip

// ---- Battery power save (v1.1) ----
constexpr int LOW_BATTERY_PCT = 15;
constexpr int LOW_BATTERY_RECOVER_PCT = 20; // hysteresis so brightness doesn't flicker right at the threshold
constexpr uint8_t NORMAL_BRIGHTNESS = 128;
constexpr uint8_t LOW_BATTERY_BRIGHTNESS = 40;

// ---- Hard event detection (v1.1) ----
// Heuristic, not axis-specific: total accel magnitude deviating from
// baseline (gravity) by more than this flags a hard brake, swerve, or bump.
// A single generic threshold on the combined vector, not a separate
// forward/lateral read, since this project's forward-axis calibration
// doesn't establish a reliable lateral axis. Needs real-world tuning -- no
// hardware access to validate.
constexpr float HARD_EVENT_THRESHOLD_G = 0.4f;
constexpr uint32_t HARD_EVENT_DEBOUNCE_MS = 1000; // don't re-log the same ongoing event every sample
constexpr int TRIP_MAX_EVENTS = 100;

// ---- SD-backed trip records (v1.1) ----
// Each trip is now its own file under TRIPS_DIR (was: only ever downloaded
// from RAM over HTTP before being lost on reboot). Plain CSV with '#'
// comment lines for metadata/events, so the existing KML/CSV export logic's
// "skip lines starting with #" parsing rule covers both.
constexpr const char *TRIPS_DIR = "/trips";
constexpr int TRIP_NAME_MAX_LEN = 24;
constexpr int TRIP_LIST_MAX = 40; // cap on how many historical trips one /api/trips response lists

// ---- SD-backed Performance run records (v1.2.0) ----
// Unlike trips (long, so streamed incrementally to SD as they record),
// a run's whole history fits in RAM already (PERF_HISTORY_MAX_POINTS
// caps it) -- each completed run is written to SD in one shot, right when
// it finishes. Every run is kept (bounded by SD capacity, not an arbitrary
// count) and browsable from the dashboard, same pattern as trips.
constexpr const char *RUNS_DIR = "/runs";
constexpr int RUN_LIST_MAX = 40;

// ---- Home WiFi auto-upload (v1.1) ----
// Fully optional and dormant unless a home SSID is configured via the
// dashboard's Sync Settings form. ESP32-S3 supports concurrent AP+STA
// (WIFI_AP_STA) -- both radios share one channel while STA is connected,
// which briefly happens only when idle (not while a trip is RECORDING).
constexpr uint32_t AUTO_UPLOAD_CHECK_INTERVAL_MS = 30000;
constexpr uint32_t WIFI_STA_CONNECT_TIMEOUT_MS = 8000;
constexpr int SETTINGS_STRING_MAX_LEN = 64;

// ---- Trip mode (Phase 4) ----
// Fixed-size point buffer (project convention -- no dynamic allocation, see
// PerformanceMode's history array). TripPoint is 32 bytes (uint32_t + 2x
// double + 2x float, 8-byte aligned), so this static array is a meaningful
// chunk of the ESP32-S3's 320KB SRAM -- measured on real hardware with WiFi
// AP + AsyncWebServer already up: free heap was only 65KB with the first
// value tried here (3600 pts, 2s sample -> 115KB of static RAM), too tight
// a margin. 1800 pts @ 4s covers the same 2-hour drive at 57.6KB instead,
// leaving ~123KB free heap. Recording simply stops (not wraps) once full so
// an in-progress KML/CSV export is never mid-corrupted by an overwrite.
constexpr int TRIP_MAX_POINTS = 1800;
constexpr uint32_t TRIP_SAMPLE_MS = 4000;

// Speed bands for the KML export's per-segment line color (opens directly in
// Google My Maps, matching the fleet-tracker reference from the project plan).
// Colors are ordered slowest->fastest. KML colors are aabbggrr (alpha, blue,
// green, red -- the reverse byte order of a normal #RRGGBB), fully opaque
// (aa=ff). Source colors: Tailwind green-500/yellow-500/orange-500/red-500/
// purple-500 (#22c55e/#eab308/#f97316/#ef4444/#a855f7).
// rgb565 duplicates the KML color as a packed panel color so the on-device
// Trip screen's band legend always matches the exported map's line colors.
struct SpeedBand { float minKmh; const char *kmlColor; uint16_t rgb565; const char *label; };
constexpr SpeedBand TRIP_SPEED_BANDS[] = {
	{0.0f,   "ff5ec522", rgb565(0x22, 0xc5, 0x5e), "0-30"},
	{30.0f,  "ff08b3ea", rgb565(0xea, 0xb3, 0x08), "30-60"},
	{60.0f,  "ff1673f9", rgb565(0xf9, 0x73, 0x16), "60-90"},
	{90.0f,  "ff4444ef", rgb565(0xef, 0x44, 0x44), "90-120"},
	{120.0f, "fff755a8", rgb565(0xa8, 0x55, 0xf7), "120+"},
};
constexpr int TRIP_SPEED_BAND_COUNT = 5;

// ---- KML leg segmentation (v1.4.0) ----
// Before v1.4.0 the exporter started a new coloured segment on *every* band
// change, so speed hovering near a boundary (29-31 km/h in traffic) split a
// single drive into hundreds of one-point slivers, each labelled only with
// its band ("0-30 km/h") and carrying no time, distance or actual speed.
// A band change must now persist for this long / this far before it's
// treated as a real transition rather than noise.
constexpr uint32_t KML_MIN_SEGMENT_MS = 20000;   // ~5 points at TRIP_SAMPLE_MS
constexpr float KML_MIN_SEGMENT_M = 250.0f;

// A run of points at or below this speed, lasting at least this long, is
// treated as a stop: it ends the current leg and drops a labelled pin, the
// way Google Timeline separates driving legs with visits.
constexpr float KML_STOP_SPEED_KMH = 3.0f;
constexpr uint32_t KML_STOP_MIN_MS = 120000; // 2 minutes parked

// A leg's coordinate text is buffered until the leg closes, because KML
// requires <name> (which needs the leg's finished stats) to precede its
// <coordinates>. This caps that buffer: a leg longer than this is split
// into a continuation leg of the same colour, which keeps peak heap bounded
// on a long motorway run that never changes band.
constexpr size_t KML_MAX_SEGMENT_BUFFER = 8192;

// ---- On-device UI theme (Phase 4 UX pass) ----
enum class ThemeColor { GREEN, RED, BLUE, WHITE };

// ---- Settings persistence (NVS, survives reboot/power-loss) ----
constexpr const char *SETTINGS_NAMESPACE = "cardputer";
