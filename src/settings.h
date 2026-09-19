#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../include/config.h"
#include "units.h"

enum class AppMode { PERFORMANCE, TRIP };

// Persists the user-facing preferences (theme color, speed unit, active
// mode) across reboots/power loss via the ESP32's NVS flash (Preferences
// library). Loaded once at boot; each setter writes through immediately --
// these change rarely (a keypress), so there's no need to batch writes.
//
// v1.1 adds: a monotonic trip-ID counter (for trip filenames), a pending
// trip name/vehicle tag (set from the dashboard, applied at the next trip
// start), and home-WiFi + upload-URL strings for the optional auto-upload
// sync feature (see sync.cpp) -- all NVS-backed the same way.
class Settings {
public:
	static void begin();

	static ThemeColor theme() { return _theme; }
	static AppMode mode() { return _mode; }

	static void setTheme(ThemeColor t);
	static void setMode(AppMode m);
	static void toggleUnit(); // wraps Units::toggle() and persists the result

	// Returns the next trip ID and persists the incremented counter --
	// each call consumes one ID, so only call this when a trip actually starts.
	static uint32_t nextTripId();

	// v1.2.0: same idea as nextTripId(), for Performance runs -- separate
	// counter/namespace, so trips and runs number independently.
	static uint32_t nextRunId();

	// v1.1: _pendingTripName/_homeSsid/_homePassword/_uploadUrl are all
	// written from the async web server task (setPendingTripName() via
	// TripMode::setName(), setSyncConfig() via /api/sync/config) while
	// read/written from the main loop task too (takePendingTripName() from
	// TripMode::startRecording(), the getters from Sync::loop()). Arduino
	// String's internal buffer can reallocate on assignment, so an
	// unsynchronized cross-task read during a concurrent write is a real
	// use-after-free risk -- every method below takes _mutex (created in
	// begin()) around its actual String access.
	static void setPendingTripName(const String &name);
	static String takePendingTripName(); // returns and clears the pending name

	// v1.3.0: same for Performance runs, which gained names of their own.
	static void setPendingRunName(const String &name);
	static String takePendingRunName();
	static String peekPendingRunName();  // read without consuming, for the on-device display
	static String peekPendingTripName();

	static void setSyncConfig(const String &ssid, const String &password, const String &uploadUrl);
	static String homeSsid();
	static String homePassword();
	static String uploadUrl();

private:
	static ThemeColor _theme;
	static AppMode _mode;
	static String _pendingTripName;
	static String _pendingRunName;
	static String _homeSsid;
	static String _homePassword;
	static String _uploadUrl;
	static SemaphoreHandle_t _mutex;
};
