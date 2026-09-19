#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include "modes/performance.h"
#include "modes/trip.h"

// UX pass (2026-09-16): everything draws into an offscreen M5Canvas sprite
// and is pushed to the panel in one go -- the previous per-frame
// fillScreen(BLACK)+redraw direct to the panel was the cause of the visible
// flash on every refresh (DISPLAY_INTERVAL_MS in main.cpp), since the panel
// briefly showed the cleared black frame before the redraw caught up.
//
// Theme (color + a Pip-Boy-style header/footer chrome) and battery level are
// read directly from Settings/M5.Power inside each show*() call, so main.cpp
// doesn't need to thread them through every call site.
class Display {
public:
	void begin();
	void showStatus(bool gpsHasFix, uint32_t satellites, float fusedSpeedKmh, bool imuCalibrated, const char *lastKey = nullptr);
	void showMessage(const char *line1, const char *line2 = nullptr);

	// Compact on-device view for Performance mode: state, distance, the
	// 0-60km/h split (the spec's explicit must-have), and a small sparkline
	// of the run's speed history. Deliberately light -- the full 0sixty-style
	// view (multi-series graph, stat tiles, full splits table) lives on the
	// phone dashboard, see webserver.cpp.
	void showPerformance(const PerformanceMode &perf, bool gpsHasFix, uint32_t satellites);

	// Trip mode's on-device view: recording state, distance/duration, and
	// live + max speed. liveSpeedKmh is the raw GPS speed (same source Trip
	// mode itself records, see main.cpp), not the IMU-fused value.
	void showTrip(const TripMode &trip, bool gpsHasFix, uint32_t satellites, float liveSpeedKmh);

	// v1.1: shown once a trip auto-stops (TripState::SUMMARY) -- the just-
	// completed trip's frozen stats, until X starts a fresh one.
	void showTripSummary(const TripMode &trip, bool gpsHasFix, uint32_t satellites);

	// v1.1: full-screen WiFi-join QR code (WIFI:S:<ssid>;T:nopass;;),
	// replacing the old "join this AP, type this password" flow now that the
	// AP is open. Deliberately drawn black-on-white regardless of the active
	// color theme -- QR scanners expect that contrast, and this is a
	// standalone utility screen, not part of the themed UI. Toggled by the Q
	// key; not persisted (always starts hidden on boot).
	void showQr(const char *ssid);

	// v1.3.0: modal text-entry screen for naming the next trip/run from the
	// device itself (the N key), rather than only from the dashboard. While
	// this is up, main.cpp routes keystrokes into the buffer instead of the
	// normal single-key actions -- otherwise typing a name containing "m"
	// would switch modes mid-word.
	void showNameEntry(const char *title, const String &buffer);

private:
	M5Canvas _canvas;
	int _w = 0, _h = 0;

	// Shared chrome: theme background, header bar (mode name, fix/sat, battery),
	// footer key-hint bar, and the divider lines between them. Returns the
	// content area's top/bottom y so callers know where they can draw.
	void drawChrome(const char *modeLabel, bool gpsHasFix, uint32_t satellites, const char *hints, int &contentTop, int &contentBottom);
};
