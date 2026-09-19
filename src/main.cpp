#include <M5Cardputer.h>
#include <cctype>
#include "../include/config.h"
#include "gps.h"
#include "imu.h"
#include "display.h"
#include "storage.h"
#include "webserver.h"
#include "settings.h"
#include "theme.h"
#include "sync.h"
#include "timeutil.h"
#include "modes/performance.h"
#include "modes/trip.h"

// Phases 1-4 + v1.1: GPS + IMU acquisition, SD logging, a WiFi AP + live web
// dashboard, Performance ("0sixty") mode, and Trip mode -- selected via
// Settings::mode(), never simultaneous. v1.1 adds a WiFi-join QR screen, SD-
// backed trip records with auto start/stop, home-WiFi auto-upload, hard-
// event flagging, and quarter-mile/60-130 splits. See CHANGELOG.md for the
// per-version history and README.md for the overall design.
//
// Keyboard controls (also shown in the on-device footer):
//   M -- switch Performance <-> Trip mode (same as the dashboard's button)
//   C -- cycle the on-device color theme (GN/RD/BU/WH)
//   U -- toggle km/h <-> mph
//   X -- reset the current trip log (Trip mode only)
//   Q -- show/hide the WiFi-join QR code (any mode)
//   N -- name the next trip/run, typed on the device (v1.3.0)
// All preferences persist across reboots (Settings, NVS-backed).

static GpsReader gps;
static ImuFusion imu;
static Display display;
static Storage storage;
static WebServer webServer;
static Sync tripSync; // not named `sync` -- collides with libc's global sync()
static PerformanceMode perf;
static TripMode trip;

static uint32_t lastDisplayMs = 0;
static uint32_t lastLogMs = 0;
static uint32_t lastBroadcastMs = 0;
static constexpr uint32_t DISPLAY_INTERVAL_MS = 250;
static constexpr uint32_t LOG_INTERVAL_MS = 100; // ~10Hz cap, matches GNSS's max rate once configured

static bool qrVisible = false;
static bool lowBatteryDimmed = false;

// v1.3.0 on-device name entry. While active, keystrokes go into the buffer
// instead of firing single-key actions -- see handleKeyboard().
static bool nameEntryActive = false;
static String nameEntryBuffer;

void setup() {
	Serial.begin(115200); // raw GPS NMEA passthrough for live debugging, see gps.cpp

	auto cfg = M5.config();
	M5Cardputer.begin(cfg, true); // true = enable keyboard (safe: GPS is on 15/13, not the keyboard's I2C pins)

	Settings::begin(); // load theme/unit/mode before the first draw so boot messages use them
	TimeUtil::begin(); // install the timezone rule before any timestamp is formatted
	display.begin();
	char versionLine[32];
	char authorLine[32];
	snprintf(versionLine, sizeof(versionLine), "0sixty v%s", FIRMWARE_VERSION);
	snprintf(authorLine, sizeof(authorLine), "by %s", AUTHOR_NAME);
	display.showMessage(versionLine, authorLine);
	Serial.printf("\n0sixty v%s by %s\n", FIRMWARE_VERSION, AUTHOR_NAME);
	delay(1500); // long enough to actually read the splash before it's overwritten below
	display.showMessage("Booting...", "GPS may take ~23s (cold start)");

	gps.begin();
	imu.begin();
	perf.begin(&storage); // ok even if storage.begin() (below) fails -- runs just won't persist to SD

	if (!storage.begin()) {
		display.showMessage("SD card error!", "Check card is inserted");
		delay(3000);
	}
	trip.begin(&storage); // ok even if storage.begin() failed above -- trip files just won't persist to SD
	tripSync.begin(&storage);

	webServer.setDataSources(&perf, &trip, &storage);
	webServer.begin();
	display.showMessage("WiFi AP (open):", AP_SSID);
	Serial.printf("Free heap after WiFi+webserver init: %u bytes\n", (unsigned)ESP.getFreeHeap());
	delay(2000);

	display.showMessage("Hold still --", "auto-calibrating IMU...");
}

static char lastKeyStr[32] = "";

static void applyKeyAction(char c) {
	switch (tolower(c)) {
		case 'm': {
			AppMode current = Settings::mode();
			// Clear Trip mode's auto-start/auto-stop hold timers on the way
			// out -- otherwise a timer latched right before switching away
			// stays latched, and the first update() call after switching
			// back can see its hold duration already "satisfied" from a
			// stale timestamp and immediately auto-start/stop with no real
			// continuous movement/parking behind it.
			if (current == AppMode::TRIP) trip.onModeExit();
			Settings::setMode(current == AppMode::PERFORMANCE ? AppMode::TRIP : AppMode::PERFORMANCE);
			break;
		}
		case 'c':
			Settings::setTheme(nextTheme(Settings::theme()));
			break;
		case 'u':
			Settings::toggleUnit();
			break;
		case 'x':
			if (Settings::mode() == AppMode::TRIP) trip.reset();
			break;
		case 'q':
			qrVisible = !qrVisible;
			break;
		case 'n':
			// Seed with whatever name is already queued, so N is also "edit
			// the pending name" rather than only "type a fresh one".
			nameEntryBuffer = (Settings::mode() == AppMode::TRIP)
				? Settings::peekPendingTripName()
				: Settings::peekPendingRunName();
			nameEntryActive = true;
			break;
	}
}

// Commits (or discards) whatever is in the name buffer and closes the modal.
static void finishNameEntry(bool save) {
	if (save) {
		if (Settings::mode() == AppMode::TRIP) {
			// Routed through TripMode rather than Settings directly: it
			// renames a trip that's already recording, and only falls back
			// to queueing for the next one when idle.
			trip.setName(nameEntryBuffer);
		} else {
			Settings::setPendingRunName(nameEntryBuffer);
		}
	}
	nameEntryActive = false;
	nameEntryBuffer = "";
}

// `Keyboard_Class::isChange()` only compares the held-key *count* to its
// previous value, and `keysState().word` is every currently-held key, not
// just newly-pressed ones -- gating on isChange()+isPressed() and acting on
// the whole word (as an earlier version did) both double-fires an action
// while a key is held down through another key's rollover, and can drop a
// keystroke outright if a release and a press happen to net the same count
// between two polls. Comparing this poll's word against the previous one
// (independent of isChange()) sidesteps both: an action only fires once,
// exactly when its character transitions from absent to present.
static String prevWord;
// enter/del are separate flags rather than characters in `word`, so they
// need their own edge tracking or holding one would repeat every poll.
static bool prevEnter = false;
static bool prevDel = false;

static void handleKeyboard() {
	auto status = M5Cardputer.Keyboard.keysState();
	String word;
	for (auto c : status.word) word += c;

	if (M5Cardputer.Keyboard.isPressed()) {
		String keys = word;
		if (status.enter) keys += "[enter]";
		if (status.del) keys += "[del]";
		if (status.fn) keys += "[fn]";
		snprintf(lastKeyStr, sizeof(lastKeyStr), "%s", keys.c_str());
	}

	// v1.3.0: while the name-entry modal is up, every keystroke is text --
	// the normal single-key actions (M/C/U/X/Q/N) are suspended so a name
	// containing any of those letters doesn't switch modes or wipe a trip
	// mid-word.
	if (nameEntryActive) {
		if (status.enter && !prevEnter) {
			finishNameEntry(true);
		} else if (status.del && !prevDel) {
			if (nameEntryBuffer.length() > 0) nameEntryBuffer.remove(nameEntryBuffer.length() - 1);
		} else {
			for (char c : word) {
				if (prevWord.indexOf(c) >= 0) continue; // already held, not a fresh press
				if (c == '`') {                         // cancel without saving
					finishNameEntry(false);
					break;
				}
				if ((int)nameEntryBuffer.length() < TRIP_NAME_MAX_LEN && c >= ' ' && c <= '~') {
					nameEntryBuffer += c;
				}
			}
		}
		prevWord = word;
		prevEnter = status.enter;
		prevDel = status.del;
		return;
	}
	prevEnter = status.enter;
	prevDel = status.del;

	for (char c : word) {
		if (prevWord.indexOf(c) < 0) applyKeyAction(c);
		// applyKeyAction('n') opens the name-entry modal mid-loop. The
		// modal check at the top of this function already ran for this
		// frame, so without bailing out here any *other* key pressed in the
		// same scan would still be dispatched as a normal action -- e.g.
		// N+X in Trip mode would open the naming screen and simultaneously
		// end the recording in progress. Remaining keys this frame are
		// swallowed deliberately; prevWord below still records them, so
		// they don't re-fire once released.
		if (nameEntryActive) break;
	}
	prevWord = word;
}

// v1.1: dims the panel under LOW_BATTERY_PCT to stretch runtime, with
// hysteresis (LOW_BATTERY_RECOVER_PCT) so it doesn't flicker back and forth
// right at the threshold. Cheap enough to just call every display tick.
static void applyBatteryPowerSave(int batteryPct) {
	if (batteryPct < 0) return; // unknown -- leave brightness alone
	if (!lowBatteryDimmed && batteryPct <= LOW_BATTERY_PCT) {
		lowBatteryDimmed = true;
		M5Cardputer.Display.setBrightness(LOW_BATTERY_BRIGHTNESS);
	} else if (lowBatteryDimmed && batteryPct >= LOW_BATTERY_RECOVER_PCT) {
		lowBatteryDimmed = false;
		M5Cardputer.Display.setBrightness(NORMAL_BRIGHTNESS);
	}
}

void loop() {
	M5Cardputer.update();
	handleKeyboard();
	webServer.loop(); // services the captive-portal DNS server

	bool newFix = gps.update();
	if (newFix) {
		const GpsFix &fix = gps.lastFix();
		imu.onGpsFix(fix.speedKmh, fix.timestampMs);
		// First fix carrying a valid UTC date sets the system clock; after
		// that this is a cheap no-op and time runs from the internal timer
		// even if the fix drops (tunnel, garage). See timeutil.h.
		TimeUtil::latch(fix);
	}

	imu.update();

	uint32_t now = millis();
	const GpsFix &fix = gps.lastFix();
	AppMode mode = Settings::mode();

	if (mode == AppMode::PERFORMANCE) {
		// Needs the IMU's higher-resolution, GPS-corrected speed for split
		// timing between GPS fixes -- and its zero-velocity-update (ZUPT) is
		// exactly the "parked between runs" detector this mode wants.
		if (imu.isCalibrated()) {
			perf.update(imu.fusedSpeedKmh(), fix.altitudeM, imu.accelG(), now);
		}
	} else {
		// Trip mode uses the GPS's own raw ground speed, not the IMU-fused
		// value: that value's ZUPT snaps to 0 after ~400ms of no acceleration
		// (config.h's ZUPT_ACCEL_THRESHOLD_MS2/ZUPT_HOLD_MS), which is right
		// for "car parked between runs" but wrong for a real drive's long
		// steady-speed stretches, where acceleration is ~0 while speed very
		// much isn't. Also, TripMode's own auto-start/stop machine (v1.1)
		// needs this same raw speed to decide when a drive has actually
		// begun/ended -- see trip.cpp.
		trip.update(gps.hasFix(), fix.lat, fix.lng, fix.speedKmh, fix.altitudeM, now);

		if (trip.tripState() == TripState::RECORDING) {
			float deviationG;
			if (imu.checkHardEvent(now, deviationG)) {
				trip.logEvent(now, fix.lat, fix.lng, deviationG);
			}
		}
	}

	// Sync must stay out of the way whenever anything time-sensitive is
	// happening: an active Trip recording (GPS/SD logging bandwidth), or a
	// Performance run that's armed or in progress (a multi-second stall here
	// would silently inflate its split/quarter-mile/rolling-split times).
	bool syncBusy = (mode == AppMode::TRIP && trip.tripState() == TripState::RECORDING) ||
	                (mode == AppMode::PERFORMANCE && (perf.state() == RunState::ARMED || perf.state() == RunState::RUNNING));
	tripSync.loop(syncBusy, now);

	if (now - lastLogMs >= LOG_INTERVAL_MS) {
		lastLogMs = now;
		// Raw GPS speed, not the IMU-fused value -- this logger runs
		// unconditionally in every mode, and the same ZUPT-during-steady-
		// cruise bug fixed for Trip mode's speed source (see the mode-update
		// block above) would otherwise show up here too, now that SD logging
		// actually works and this data will get trusted for real.
		storage.logSample(now, fix.lat, fix.lng, fix.speedKmh, fix.altitudeM, fix.satellites);
	}

	if (now - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
		lastDisplayMs = now;
		applyBatteryPowerSave(M5Cardputer.Power.getBatteryLevel());

		if (nameEntryActive) {
			display.showNameEntry(mode == AppMode::TRIP ? "NAME TRIP" : "NAME RUN", nameEntryBuffer);
		} else if (qrVisible) {
			display.showQr(AP_SSID);
		} else if (mode == AppMode::TRIP) {
			if (trip.tripState() == TripState::SUMMARY) {
				display.showTripSummary(trip, gps.hasFix(), gps.satellites());
			} else {
				display.showTrip(trip, gps.hasFix(), gps.satellites(), fix.speedKmh);
			}
		} else if (!imu.isCalibrated()) {
			display.showStatus(gps.hasFix(), gps.satellites(), imu.fusedSpeedKmh(), imu.isCalibrated(), lastKeyStr);
		} else {
			display.showPerformance(perf, gps.hasFix(), gps.satellites());
		}
	}

	if (now - lastBroadcastMs >= WS_BROADCAST_INTERVAL_MS) {
		lastBroadcastMs = now;
		int batteryPct = M5Cardputer.Power.getBatteryLevel();
		// Same reasoning as the trip.update() call above: the live dashboard
		// speed readout must match whichever speed source each mode actually
		// records, or Trip mode would show a phone screen stuck near 0 while
		// genuinely cruising.
		float liveSpeedKmh = (mode == AppMode::PERFORMANCE) ? imu.fusedSpeedKmh() : fix.speedKmh;
		LiveSample sample{gps.hasFix(), fix.satellites, liveSpeedKmh, fix.lat, fix.lng, fix.altitudeM, batteryPct};
		webServer.broadcast(sample, mode, perf, trip);
	}
}
