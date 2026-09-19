#pragma once

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../../include/config.h"

class Storage; // fwd decl, see storage.h -- TripMode owns the SD-backed trip file lifecycle

// Phase 4 + v1.1: long-drive logging, exported as a speed-banded KML (see
// webserver.cpp's chunked /download/trip.kml route) or a raw CSV. Recording
// is no longer purely manual: TripState below adds auto-start-on-movement /
// auto-stop-when-parked, with a SUMMARY state in between so a completed
// trip's stats stay on screen instead of just resetting.
struct TripPoint {
	uint32_t tMs; // millis() at the time of this sample (absolute, not trip-relative)
	double lat;
	double lng;
	float speedKmh;
	float altitudeM;
};

// v1.1: a hard-brake/swerve/bump marker (see ImuFusion::checkHardEvent),
// logged at whatever point in the trip it occurred. Small fixed cap
// (TRIP_MAX_EVENTS) -- this is a marker list, not a data stream.
struct TripEvent {
	uint32_t tMs;
	double lat;
	double lng;
	float deviationG;
};

enum class TripState { IDLE, RECORDING, SUMMARY };

class TripMode {
public:
	// storage may be null (e.g. if the SD card failed to mount) -- trips
	// still work in-RAM-only in that case, just aren't persisted to SD.
	void begin(Storage *storage);

	// Call every loop while Trip mode is active. Internally runs the
	// auto-start/auto-stop state machine and throttles point recording to
	// TRIP_SAMPLE_MS; no-ops on the point buffer once full (see below).
	void update(bool hasFix, double lat, double lng, float speedKmh, float altitudeM, uint32_t nowMs);

	// v1.1: log a hard-event marker at the current position. No-ops outside
	// TripState::RECORDING.
	void logEvent(uint32_t nowMs, double lat, double lng, float deviationG);

	// Manual override: clears the buffer and returns to IDLE from any state
	// (also serves as "dismiss summary, ready for next trip"). Bound to a
	// keypress in main.cpp.
	void reset();

	// v1.1: sets the name/vehicle tag for the trip -- applied immediately if
	// already RECORDING (written as a new metadata comment line), or queued
	// via Settings::setPendingTripName() for the next auto-started trip if
	// IDLE. Called from the dashboard's /api/trip/name handler.
	void setName(const String &name);

	// v1.1: clears the auto-start/auto-stop hold timers. Call when Trip mode
	// stops being the active mode (the M key, in main.cpp) -- otherwise a
	// timer latched right before switching away stays latched for however
	// long the user is gone, and the very next update() call after switching
	// back can see its hold duration already satisfied and immediately
	// auto-start/stop a trip with no real continuous movement/parking behind
	// it. Not needed on entry, only exit: begin()/reset() already zero these.
	void onModeExit();

	TripState tripState() const { return _state; }

	// v1.1: whether the trip currently shown in SUMMARY actually made it to
	// the SD card. beginTrip() can fail (card full, pulled mid-session, no
	// card at all) without that being visible anywhere else -- the RAM-only
	// point buffer and live dashboard exports still work either way, but the
	// on-device summary screen must not claim "saved to SD" when it wasn't.
	bool persistedToSd() const { return _persistedToSd; }

	// _count is read from the WiFi/AsyncTCP task (webserver.cpp's KML/CSV
	// export generators) while update() writes it from the main loop task on
	// the other core -- atomic with release/acquire ordering guarantees a
	// reader that observes an incremented count also sees the fully-written
	// TripPoint at that index, not a torn/stale one.
	int pointCount() const { return _count.load(std::memory_order_acquire); }
	bool isFull() const { return pointCount() >= TRIP_MAX_POINTS; }
	float distanceM() const { return _distanceM; }
	float maxSpeedKmh() const { return _maxSpeedKmh; }
	// Wall-clock span of the trip so far. Tracked independently of the point
	// buffer (not derived from _points[0]/_points[n-1]) so it -- like
	// distanceM()/maxSpeedKmh() -- keeps advancing for the rest of a long
	// drive even after the fixed-size buffer fills and stops accepting new
	// points. See update()'s comment for why.
	uint32_t durationMs() const {
		return (_firstSampleMs != 0) ? (_lastSampleMs - _firstSampleMs) : 0;
	}
	const TripPoint *points() const { return _points; }
	const TripEvent *events() const { return _events; }
	int eventCount() const { return _eventCount; }

	// v1.1: _currentName is written from two tasks -- setName() (the async
	// web server task, via /api/trip/name) and startRecording()/reset() (the
	// main loop task) -- and read from the main loop (display/broadcast).
	// Arduino String's internal buffer can reallocate on assignment, so an
	// unsynchronized read during a concurrent write is a real use-after-free/
	// torn-read risk, not just a style concern. _nameMutex (created in
	// begin()) serializes every access.
	String currentName() const;

	// Index into config.h's TRIP_SPEED_BANDS for a given speed -- shared by
	// the KML generator (webserver.cpp) and the on-device trip screen so the
	// color legend always matches.
	static int bandFor(float speedKmh);

	// Great-circle distance in metres. Exposed (v1.4.0) because the KML
	// exporter needs per-leg distances and should use the same maths the
	// trip's own odometer does, rather than a second copy that could drift.
	static double haversineM(double lat1, double lng1, double lat2, double lng2);

	// v1.4.0: wall-clock anchor for the recording, so the exporter can turn
	// each point's millis() stamp into a real timestamp:
	//   pointEpoch = startedEpoch() + (p.tMs - firstSampleMs()) / 1000
	// startedEpoch() is 0 when the trip began before the GPS clock was set,
	// in which case the export omits times rather than inventing them.
	uint32_t startedEpoch() const { return _startedEpoch; }
	uint32_t firstSampleMs() const { return _firstSampleMs; }

private:
	Storage *_storage = nullptr;
	TripState _state = TripState::IDLE;
	TripPoint _points[TRIP_MAX_POINTS];
	std::atomic<int> _count{0};
	TripEvent _events[TRIP_MAX_EVENTS];
	int _eventCount = 0;
	float _distanceM = 0.0f;
	float _maxSpeedKmh = 0.0f;
	bool _hasLast = false;
	double _lastLat = 0.0;
	double _lastLng = 0.0;
	uint32_t _firstSampleMs = 0;
	uint32_t _lastSampleMs = 0;
	uint32_t _startedEpoch = 0; // v1.4.0, see startedEpoch()
	String _currentName;
	SemaphoreHandle_t _nameMutex = nullptr; // guards _currentName, see currentName()'s comment
	bool _persistedToSd = false;

	void setCurrentNameLocked(const String &name); // internal helper, takes _nameMutex itself

	// Auto start/stop: each is 0 while its condition is false, or the
	// timestamp the condition first became true (so "how long has it been
	// true" is just nowMs - this, no per-call dt accumulation needed). See
	// AUTO_TRIP_START_*/AUTO_TRIP_STOP_* in config.h.
	uint32_t _speedAboveStartSinceMs = 0;
	uint32_t _speedBelowStopSinceMs = 0;

	void startRecording(uint32_t nowMs);
	void stopRecording();
};
