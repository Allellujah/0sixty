#include "trip.h"
#include "../storage.h"
#include "../settings.h"
#include "../timeutil.h"
#include <math.h>

void TripMode::begin(Storage *storage) {
	_storage = storage;
	_nameMutex = xSemaphoreCreateMutex();
	reset();
}

void TripMode::setCurrentNameLocked(const String &name) {
	xSemaphoreTake(_nameMutex, portMAX_DELAY);
	_currentName = name;
	xSemaphoreGive(_nameMutex);
}

String TripMode::currentName() const {
	xSemaphoreTake(_nameMutex, portMAX_DELAY);
	String n = _currentName;
	xSemaphoreGive(_nameMutex);
	return n;
}

void TripMode::onModeExit() {
	_speedAboveStartSinceMs = 0;
	_speedBelowStopSinceMs = 0;
}

int TripMode::bandFor(float speedKmh) {
	int band = 0;
	for (int i = 0; i < TRIP_SPEED_BAND_COUNT; i++) {
		if (speedKmh >= TRIP_SPEED_BANDS[i].minKmh) band = i;
	}
	return band;
}

// Haversine distance in meters between two lat/lng pairs (degrees).
double TripMode::haversineM(double lat1, double lng1, double lat2, double lng2) {
	constexpr double R = 6371000.0;
	double dLat = (lat2 - lat1) * DEG_TO_RAD;
	double dLng = (lng2 - lng1) * DEG_TO_RAD;
	double a = sin(dLat / 2) * sin(dLat / 2) +
	           cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * sin(dLng / 2) * sin(dLng / 2);
	double c = 2 * atan2(sqrt(a), sqrt(1 - a));
	return R * c;
}

void TripMode::startRecording(uint32_t nowMs) {
	(void)nowMs;
	_count.store(0, std::memory_order_release);
	_eventCount = 0;
	_hasLast = false;
	_distanceM = 0.0f;
	_maxSpeedKmh = 0.0f;
	_firstSampleMs = 0;
	_lastSampleMs = 0;

	String name = Settings::takePendingTripName();
	if (name.length() == 0) name = "trip";
	setCurrentNameLocked(name);

	uint32_t id = Settings::nextTripId();
	_startedEpoch = TimeUtil::epochNow(); // 0 if the GPS clock isn't set yet
	_persistedToSd = _storage
		? _storage->beginTrip(id, name, TimeUtil::stampForFilename(), _startedEpoch)
		: false;

	_state = TripState::RECORDING;
}

void TripMode::stopRecording() {
	if (_persistedToSd && _storage) _storage->endTrip(_distanceM, durationMs(), _maxSpeedKmh);
	_state = TripState::SUMMARY;
}

void TripMode::reset() {
	// If a trip is still actively RECORDING when this is called (the
	// existing manual X-key behavior, now also reachable mid-drive), close
	// it out properly so the SD file gets a valid summary footer instead of
	// being left open with no "# ended=" line.
	if (_state == TripState::RECORDING && _persistedToSd && _storage) {
		_storage->endTrip(_distanceM, durationMs(), _maxSpeedKmh);
	}
	_count.store(0, std::memory_order_release);
	_eventCount = 0;
	_hasLast = false;
	_distanceM = 0.0f;
	_maxSpeedKmh = 0.0f;
	_firstSampleMs = 0;
	_lastSampleMs = 0;
	_startedEpoch = 0;
	setCurrentNameLocked("");
	_persistedToSd = false;
	_speedAboveStartSinceMs = 0;
	_speedBelowStopSinceMs = 0;
	_state = TripState::IDLE;
}

void TripMode::setName(const String &name) {
	// v1.3.0: shared with Storage, because the name now has to survive being
	// embedded in a filename as well as in a comma-delimited metadata line.
	// (Was a local replace() chain here that allowed spaces -- fine for
	// metadata, not for filenames.)
	String clean = sanitizeName(name);

	if (_state == TripState::RECORDING) {
		setCurrentNameLocked(clean);
		if (_persistedToSd && _storage) _storage->renameTrip(clean);
	} else {
		Settings::setPendingTripName(clean);
	}
}

void TripMode::logEvent(uint32_t nowMs, double lat, double lng, float deviationG) {
	if (_state != TripState::RECORDING) return;
	if (_eventCount < TRIP_MAX_EVENTS) {
		_events[_eventCount++] = {nowMs, lat, lng, deviationG};
	}
	if (_storage) _storage->logTripEvent(nowMs, lat, lng, deviationG);
}

void TripMode::update(bool hasFix, double lat, double lng, float speedKmh, float altitudeM, uint32_t nowMs) {
	if (!hasFix) return;

	// --- auto start/stop, runs regardless of current state ---
	bool aboveStart = speedKmh >= AUTO_TRIP_START_SPEED_KMH;
	bool belowStop = speedKmh <= AUTO_TRIP_STOP_SPEED_KMH;
	if (aboveStart) {
		if (_speedAboveStartSinceMs == 0) _speedAboveStartSinceMs = nowMs;
	} else {
		_speedAboveStartSinceMs = 0;
	}
	if (belowStop) {
		if (_speedBelowStopSinceMs == 0) _speedBelowStopSinceMs = nowMs;
	} else {
		_speedBelowStopSinceMs = 0;
	}

	if (_state == TripState::IDLE) {
		if (_speedAboveStartSinceMs != 0 && (nowMs - _speedAboveStartSinceMs) >= AUTO_TRIP_START_HOLD_MS) {
			startRecording(nowMs);
		}
	} else if (_state == TripState::RECORDING) {
		if (_speedBelowStopSinceMs != 0 && (nowMs - _speedBelowStopSinceMs) >= AUTO_TRIP_STOP_HOLD_MS) {
			stopRecording();
			return; // just stopped this call -- don't also record a point below
		}
	}

	if (_state != TripState::RECORDING) return;

	if (_lastSampleMs != 0 && (nowMs - _lastSampleMs) < TRIP_SAMPLE_MS) return;
	if (_firstSampleMs == 0) _firstSampleMs = nowMs;
	_lastSampleMs = nowMs;

	if (_hasLast) {
		_distanceM += (float)haversineM(_lastLat, _lastLng, lat, lng);
	}
	_lastLat = lat;
	_lastLng = lng;
	_hasLast = true;

	if (speedKmh > _maxSpeedKmh) _maxSpeedKmh = speedKmh;

	if (_storage) _storage->logTripPoint(nowMs, lat, lng, speedKmh, altitudeM);

	// The above (distance/duration/max speed, and the SD-backed log) are all
	// O(1)/append-only, not bounded by the fixed-size RAM point buffer below
	// -- keep updating them for the rest of a long drive even once that
	// buffer is full. Only the RAM-backed track (and so the live KML/CSV
	// export's resolution) stops growing; the SD file keeps every point
	// regardless.
	if (isFull()) return;

	// Write the full point BEFORE publishing the new count (release store) --
	// a cross-core reader (webserver.cpp's export generators) that observes
	// the incremented count via an acquire load is then guaranteed to see
	// this complete write, not a torn one. See trip.h's _count comment.
	int i = _count.load(std::memory_order_relaxed);
	_points[i] = {nowMs, lat, lng, speedKmh, altitudeM};
	_count.store(i + 1, std::memory_order_release);
}
