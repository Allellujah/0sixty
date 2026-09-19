#pragma once

#include <Arduino.h>
#include "modes/performance.h"
#include "modes/trip.h"
#include "settings.h"

class Storage;

// Phases 2-4 + v1.1: SoftAP + local web dashboard. The Cardputer hosts its
// own WiFi network; a phone joins it and opens a browser -- no app install.
// Also serves the data-export routes: a completed Performance run as CSV,
// the live Trip log as KML/CSV, and (v1.1) historical SD-backed trips.
//
// v1.1: the AP is now open (no password, see config.h) and runs a DNS-based
// captive portal (WebServer::loop()) so joining via the QR code screen (Q
// key on-device) also auto-launches the dashboard, matching how public WiFi
// captive portals behave -- no separate "now open your browser" step.
struct LiveSample {
	bool gpsHasFix;
	uint32_t satellites;
	float speedKmh;
	double lat;
	double lng;
	float altitudeM;
	int batteryPct; // -1 if unknown, matches Power_Class::getBatteryLevel()
};

class WebServer {
public:
	void begin();

	// Services the captive-portal DNS server -- call every main loop
	// iteration (cheap no-op when there's nothing pending).
	void loop();

	// Call periodically (main.cpp throttles to WS_BROADCAST_INTERVAL_MS);
	// pushes the current sample + whichever mode is active to any connected
	// dashboard(s) in one message. Only one of perf/trip is meaningful,
	// selected by `mode`; the other is ignored (pass either, unused fields
	// aren't read).
	void broadcast(const LiveSample &sample, AppMode mode, const PerformanceMode &perf, const TripMode &trip);

	// Set once at startup so the download/history routes and dashboard's
	// mode toggle can read/act on live + historical data without WebServer
	// needing its own copies of everything main.cpp already owns.
	void setDataSources(PerformanceMode *perf, TripMode *trip, Storage *storage);
};
