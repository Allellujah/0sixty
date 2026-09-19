#pragma once

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../include/config.h"

// v1.1 metadata summary for one historical trip file, as parsed from its
// leading "# id=...,name=...,started=..." comment line and (if present) its
// trailing "# ended=...,distanceM=...,durationMs=...,maxSpeedKmh=..." line.
struct TripSummary {
	String filename;
	String name;
	uint32_t startedEpoch = 0; // v1.3.0: Unix seconds, 0 if the clock wasn't set yet
	uint32_t startedMs = 0;
	bool ended = false;
	float distanceM = 0.0f;
	uint32_t durationMs = 0;
	float maxSpeedKmh = 0.0f;
};

// v1.2.0: metadata summary for one historical Performance run file, parsed
// from its single "# id=...,started=...,elapsedMs=...,..." header line --
// unlike trips, a run file is written whole in one shot (see saveRun()), so
// there's no separate footer to parse.
struct RunSummary {
	String filename;
	String name;               // v1.3.0: user-supplied label, runs have these now too
	uint32_t startedEpoch = 0; // v1.3.0: Unix seconds, 0 if the clock wasn't set yet
	uint32_t startedMs = 0;
	uint32_t elapsedMs = 0;
	uint32_t zeroSixtyMs = 0; // 0 = not reached
	uint32_t quarterMileMs = 0;
	float quarterMileTrapKmh = 0.0f;
	uint32_t rollingSplitMs = 0;
	float maxSpeedKmh = 0.0f;
	float distanceM = 0.0f;
};

// v1.3.0: strips anything FAT filenames (or this project's own '#'/','
// delimited metadata lines) can't safely carry, so a user-typed name can go
// straight into a filename and a metadata field without escaping. Keeps
// letters, digits, '-' and '_'; everything else becomes '-'. Also length-
// capped to TRIP_NAME_MAX_LEN.
String sanitizeName(const String &raw);

// Phase 1: raw sample logging to prove the SD write path. Phases 3/4 extend
// this with structured run/trip records and the KML writer.
//
// v1.1 adds SD-backed trip files under TRIPS_DIR, one per trip, plus a
// historical listing/read API for the dashboard's trip browser. This is the
// first thing in the project that reads from SD from a *different* task
// (ESPAsyncWebServer's task, for historical downloads) than the one that
// writes to it (the main loop, for logSample()/trip recording) -- unlike
// every SD access before it, which only ever happened from the main loop.
// SD/SdFat isn't safe under concurrent access from two tasks even when
// they're touching different files (shared SPI bus/controller state), so
// every method here takes a FreeRTOS mutex around its actual SD work.
class Storage {
public:
	bool begin();

	// Appends one CSV line: millis,lat,lng,speedKmh,altitudeM,satellites
	void logSample(uint32_t timestampMs, double lat, double lng, float speedKmh, float altitudeM, uint32_t satellites);

	// --- Trip file lifecycle (v1.1), called by TripMode ---
	// Opens /trips/trip<id>.csv, writes the metadata + header lines. Returns
	// false (and leaves any prior trip file state untouched) if SD isn't
	// ready or the file can't be opened.
	// v1.3.0: `name` and `dateStamp` (a "20260918-1432" string from
	// TimeUtil::stampForFilename(), or empty if the GPS clock isn't set yet)
	// both go into the filename as well as the metadata header -- see
	// buildRecordingFilename() in storage.cpp for the exact shape.
	bool beginTrip(uint32_t id, const String &name, const String &dateStamp, uint32_t startedEpoch);
	void logTripPoint(uint32_t tMs, double lat, double lng, float speedKmh, float altitudeM);
	void logTripEvent(uint32_t tMs, double lat, double lng, float deviationG);
	void renameTrip(const String &name); // appends a new "# name=..." comment line to the open trip file
	void endTrip(float distanceM, uint32_t durationMs, float maxSpeedKmh); // writes the summary footer line and closes
	bool tripFileOpen() const { return _tripOpen; }

	// --- Historical trip browsing (v1.1) ---
	// Lists up to maxCount trips under TRIPS_DIR, most recent first, into
	// `out`. Skips the currently-open trip file (if any) -- that trip's live
	// data is already served from RAM via the existing /download/trip.*
	// routes, and reading a file another handle still has open for writing
	// isn't safe. Returns the number of entries written.
	int listTrips(TripSummary *out, int maxCount);

	// Opens a historical (closed, not-currently-recording) trip file for
	// reading. Caller must call closeTripRead() when done. Returns an
	// invalid File (operator bool() false) if the filename doesn't exist or
	// is the currently-open trip.
	File openTripForRead(const String &filename);
	// Reads up to len bytes from a File returned by openTripForRead(),
	// mutex-protected the same as every other SD access here.
	size_t readTripChunk(File &f, uint8_t *buf, size_t len);
	void closeTripRead(File &f);

	// --- Home WiFi auto-upload bookkeeping (v1.1, see sync.cpp) ---
	// A flat "one filename per line" index at TRIPS_DIR/.uploaded -- small
	// and simple over a real database, matching this project's existing
	// preference for plain files over dependencies.
	bool isTripUploaded(const String &filename);
	void markTripUploaded(const String &filename);

	// --- Performance run persistence + browsing (v1.2.0) ---
	// One-shot write (not incremental like trips -- a run's whole history
	// already fits in RAM, see config.h's RUNS_DIR comment): writes
	// /runs/run<id>.csv with `csv` as its entire content and closes it
	// immediately. Returns false if SD isn't ready or the file can't be
	// opened -- caller (PerformanceMode) tracks this as "was this run
	// actually saved" rather than assuming success.
	bool saveRun(uint32_t id, const String &name, const String &dateStamp, const String &csv);

	// Lists up to maxCount runs under RUNS_DIR, most recent first. No
	// "currently open" file to skip here (unlike trips) since saveRun()
	// only ever writes a run after it's already finished.
	int listRuns(RunSummary *out, int maxCount);

	// Opens a run file for reading -- reuses readTripChunk()/closeTripRead()
	// below for the actual mutex-protected byte access, since those are
	// generic over any File handle despite the "Trip" name (they don't
	// touch any trip-specific state).
	File openRunForRead(const String &filename);

	// --- Deleting saved recordings (v1.3.0) ---
	// Both refuse a filename that isn't a plain basename under the expected
	// directory (no path separators, correct prefix/extension) so a crafted
	// request can't reach anything else on the card, and deleteTrip()
	// additionally refuses the trip file that's currently open for writing.
	bool deleteTrip(const String &filename);
	bool deleteRun(const String &filename);

private:
	bool _ready = false;
	String _currentLogPath;
	File _logFile; // held open for the session; see storage.cpp for why

	bool _tripOpen = false;
	String _currentTripFilename; // just the filename (e.g. "trip42.csv"), not the full path
	File _tripFile;

	SemaphoreHandle_t _sdMutex = nullptr;
};
