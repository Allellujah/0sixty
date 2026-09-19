#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../../include/config.h"

class Storage; // fwd decl, see storage.h -- PerformanceMode saves each completed run to SD

// 0sixty-style auto-armed performance run detection. No manual start/stop:
// arms when stationary, starts timing when the driver actually launches,
// stops on reaching the target speed, dropping back off, or a timeout.
// See config.h for the tunable thresholds and the project plan's "Mode 1
// auto-arm/trigger" section for the reasoning.
//
// v1.2.0: a completed run's results now stay frozen and visible through the
// whole ARMED-waiting-for-next-launch period -- resetRun() used to fire the
// moment the car sat still again after FINISHED (same short hold as the
// original arm-from-idle timer), wiping the just-finished run's stats
// before there was any real chance to look at them. It now only fires when
// a *new* run actually starts (ARMED -> RUNNING), so "results stay until
// the next run" is just "don't clear until you're overwriting them for a
// reason." Every FINISHED run is also saved to SD as its own file (see
// Storage::saveRun()), mirroring TripMode's per-trip persistence.

enum class RunState { IDLE_WAIT, ARMED, RUNNING, FINISHED };

struct HistoryPoint {
	uint32_t tMs;
	float speedKmh;
	float altitudeM;
	float accelG;
};

struct SplitResult {
	int thresholdKmh;
	uint32_t timeMs; // 0 = not yet reached
	bool reached;
};

class PerformanceMode {
public:
	// storage may be null (e.g. SD failed to mount) -- runs still work in
	// RAM only in that case, just aren't persisted.
	void begin(Storage *storage);

	// Call every loop with the latest fused speed/altitude/accel. Internally
	// drives the arm/trigger/run state machine and records history/splits
	// while RUNNING.
	void update(float speedKmh, float altitudeM, float accelG, uint32_t nowMs);

	RunState state() const { return _state; }
	float distanceM() const { return _distanceM; }
	float maxSpeedKmh() const { return _maxSpeedKmh; }
	uint32_t elapsedMs() const { return _elapsedMs; }
	float slopePercent() const;
	const SplitResult *splits() const { return _splits; }
	int splitCount() const { return PERF_SPLIT_COUNT; }
	const HistoryPoint *history() const { return _history; }
	int historyCount() const { return _historyCount; }

	// v1.1: quarter mile trap (time + speed at QUARTER_MILE_M from launch,
	// same launch reference as the 0-X splits) and the 60-130 rolling split
	// (time between first crossing 60 and first crossing 130 within the same
	// run -- independent of when launch happened, unlike the 0-X splits).
	uint32_t quarterMileTimeMs() const { return _quarterMileTimeMs; }
	float quarterMileTrapKmh() const { return _quarterMileTrapKmh; }
	bool quarterMileReached() const { return _quarterMileTimeMs > 0; }
	uint32_t rollingSplitMs() const { return (_rollingLowMs > 0 && _rollingHighMs > 0) ? (_rollingHighMs - _rollingLowMs) : 0; }
	bool rollingSplitReached() const { return _rollingLowMs > 0 && _rollingHighMs > 0; }

	// Looks up a specific split by its km/h threshold (e.g. 60 for "0-60") --
	// used for the on-device "light view" headline stat. Returns nullptr if
	// that threshold isn't one of the tracked splits.
	const SplitResult *findSplit(int thresholdKmh) const;

	// v1.2.0: whether the currently-shown (last completed) run actually made
	// it to SD -- mirrors TripMode::persistedToSd(), same reasoning (SD can
	// fail silently; don't claim it saved when it didn't).
	bool lastRunPersisted() const { return _persistedToSd; }

	// v1.3.0: runs carry a user-supplied label now, same as trips. Taken
	// from Settings' pending-run-name at the moment a run starts, so a name
	// typed on the device beforehand lands on the run it was meant for.
	//
	// Mutex-guarded for the same reason TripMode::currentName() is: this is
	// written by the main loop (when a run starts) and read from the async
	// web server task (toCsv(), via /download/run.csv). Arduino String
	// reallocates its buffer on assignment, so an unsynchronized concurrent
	// read is a use-after-free, not merely a stale value.
	String currentName() const;

	// Builds this run's full CSV export (metadata header + speed/altitude/
	// accel history + splits + quarter mile + rolling split) -- shared by
	// the live "download last run" route and the one-shot SD save at the
	// moment a run finishes, so the format only lives in one place.
	String toCsv() const;

private:
	RunState _state = RunState::IDLE_WAIT;
	uint32_t _stateEnteredMs = 0;
	uint32_t _belowArmSpeedMs = 0;
	uint32_t _aboveTriggerSpeedMs = 0;

	uint32_t _runStartMs = 0;
	uint32_t _elapsedMs = 0;
	float _distanceM = 0.0f;
	float _maxSpeedKmh = 0.0f;
	float _startAltitudeM = 0.0f;
	float _lastAltitudeM = 0.0f;
	// v1.2.1: slopePercent() used to always compute live from
	// _lastAltitudeM/_startAltitudeM, but those two keep tracking the
	// current GPS altitude even after a run FINISHES (ARMED re-tracks
	// _startAltitudeM continuously in preparation for the *next* launch,
	// and _lastAltitudeM updates unconditionally every sample) -- so the
	// v1.2.0 "results stay frozen" fix missed this one stat, and it would
	// silently drift away from the completed run's real value for the
	// entire waiting period instead of staying put like everything else.
	// Frozen once, at the RUNNING->FINISHED transition, same moment
	// everything else is finalized.
	float _frozenSlopePercent = 0.0f;
	float _lastSpeedKmh = 0.0f;
	uint32_t _lastSampleMs = 0;
	uint32_t _lastHistoryMs = 0;

	SplitResult _splits[PERF_SPLIT_COUNT];
	HistoryPoint _history[PERF_HISTORY_MAX_POINTS];
	int _historyCount = 0;

	uint32_t _quarterMileTimeMs = 0; // 0 = not yet reached
	float _quarterMileTrapKmh = 0.0f;
	uint32_t _rollingLowMs = 0;  // timestamp first crossing PERF_ROLLING_LOW_KMH, 0 = not yet
	uint32_t _rollingHighMs = 0; // timestamp first crossing PERF_ROLLING_HIGH_KMH, 0 = not yet

	Storage *_storage = nullptr;
	uint32_t _currentRunId = 0;
	String _currentName;
	SemaphoreHandle_t _nameMutex = nullptr; // guards _currentName, see currentName()
	uint32_t _startedEpoch = 0;
	bool _persistedToSd = false;

	void setCurrentNameLocked(const String &name);

	void resetRun();
	void recordHistory(float speedKmh, float altitudeM, float accelG, uint32_t nowMs);
};
