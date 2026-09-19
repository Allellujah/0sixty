#pragma once

#include <Arduino.h>
#include "storage.h"
#include "../include/config.h"

// v1.1: optional home-WiFi auto-upload. Fully dormant unless a home SSID is
// configured via the dashboard's Sync Settings form (Settings::homeSsid()).
// When configured, periodically -- and ONLY while the caller says it's safe
// to (see loop()'s `busy` param) -- briefly joins that WiFi as a station
// (ESP32-S3 supports concurrent AP+STA, so the device's own SoftAP dashboard
// stays up throughout) and POSTs any not-yet-uploaded trip file to
// Settings::uploadUrl().
//
// Non-blocking by design: an earlier version connected with a synchronous
// `while (!connected) delay(100)` loop (up to WIFI_STA_CONNECT_TIMEOUT_MS),
// which froze the entire main loop -- GPS/IMU updates, keyboard handling,
// display refresh, and (worse) Performance mode's split/quarter-mile/
// rolling-split timestamps, which would have been silently inflated by
// however long that freeze lasted. loop() below is a small state machine
// instead: each call does at most one bounded step (check connection status,
// or upload exactly one file) and returns, so it only ever adds one main-
// loop iteration's worth of latency, called repeatedly across normal loop()
// iterations until done.
//
// UNTESTED ON REAL HARDWARE: there was no home WiFi available to join during
// development. Treat this as experimental until confirmed working end to
// end -- if it misbehaves, leaving Settings::homeSsid() empty (the default)
// fully disables it with no other effect on the rest of the firmware.
enum class SyncState { IDLE, CONNECTING, UPLOADING };

class Sync {
public:
	void begin(Storage *storage);

	// `busy` must be true whenever it's unsafe to have the radio/CPU
	// occupied by a sync attempt -- Trip mode actively RECORDING, or a
	// Performance run ARMED/RUNNING (main.cpp computes this). If `busy`
	// becomes true mid-connect, the attempt is aborted cleanly back to
	// AP-only mode rather than pressing on.
	void loop(bool busy, uint32_t nowMs);

private:
	Storage *_storage = nullptr;
	uint32_t _lastCheckMs = 0;
	SyncState _state = SyncState::IDLE;
	uint32_t _connectStartMs = 0;

	// Fixed-size, not heap-allocated -- matches this project's convention
	// (see storage.cpp's listTrips() comment) and this array only needs to
	// exist while a sync pass is in progress (IDLE/CONNECTING/UPLOADING).
	TripSummary _pending[TRIP_LIST_MAX];
	int _pendingCount = 0;
	int _pendingIdx = 0;

	void abortToIdle();
	bool uploadOne(const String &filename);
};
