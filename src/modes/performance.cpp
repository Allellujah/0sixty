#include "performance.h"
#include "../storage.h"
#include "../settings.h"
#include "../timeutil.h"

void PerformanceMode::begin(Storage *storage) {
	_storage = storage;
	_nameMutex = xSemaphoreCreateMutex();
	resetRun();
	_state = RunState::IDLE_WAIT;
}

void PerformanceMode::setCurrentNameLocked(const String &name) {
	xSemaphoreTake(_nameMutex, portMAX_DELAY);
	_currentName = name;
	xSemaphoreGive(_nameMutex);
}

String PerformanceMode::currentName() const {
	xSemaphoreTake(_nameMutex, portMAX_DELAY);
	String n = _currentName;
	xSemaphoreGive(_nameMutex);
	return n;
}

void PerformanceMode::resetRun() {
	_distanceM = 0.0f;
	_maxSpeedKmh = 0.0f;
	_elapsedMs = 0;
	_historyCount = 0;
	_quarterMileTimeMs = 0;
	_quarterMileTrapKmh = 0.0f;
	_rollingLowMs = 0;
	_rollingHighMs = 0;
	_frozenSlopePercent = 0.0f;
	_persistedToSd = false;
	// _currentName/_startedEpoch are deliberately NOT cleared here: they're
	// set together with the new run's id immediately after this call, and
	// clearing them would blank the label on the results still being shown
	// from the previous run in the instant before the new one overwrites it.
	for (int i = 0; i < PERF_SPLIT_COUNT; i++) {
		_splits[i] = {PERF_SPLIT_THRESHOLDS_KMH[i], 0, false};
	}
}

void PerformanceMode::recordHistory(float speedKmh, float altitudeM, float accelG, uint32_t nowMs) {
	if (nowMs - _lastHistoryMs < PERF_HISTORY_SAMPLE_MS) return;
	_lastHistoryMs = nowMs;
	if (_historyCount < PERF_HISTORY_MAX_POINTS) {
		_history[_historyCount++] = {nowMs - _runStartMs, speedKmh, altitudeM, accelG};
	}
}

const SplitResult *PerformanceMode::findSplit(int thresholdKmh) const {
	for (int i = 0; i < PERF_SPLIT_COUNT; i++) {
		if (_splits[i].thresholdKmh == thresholdKmh) return &_splits[i];
	}
	return nullptr;
}

float PerformanceMode::slopePercent() const {
	// Live while RUNNING (matches the run actually happening); frozen at
	// whatever it was the instant the run finished otherwise -- see
	// _frozenSlopePercent's comment in performance.h for why this can't
	// just keep computing from _lastAltitudeM/_startAltitudeM like before.
	if (_state != RunState::RUNNING) return _frozenSlopePercent;
	if (_distanceM < 1.0f) return 0.0f;
	return ((_lastAltitudeM - _startAltitudeM) / _distanceM) * 100.0f;
}

String PerformanceMode::toCsv() const {
	String out;
	const SplitResult *s60 = findSplit(60);
	out += "# id=" + String(_currentRunId) +
	       ",author=" + String(AUTHOR_NAME) +
	       ",name=" + sanitizeName(currentName()) + // accessor, not the raw field: toCsv() also runs on the web task
	       ",started=" + String(_runStartMs) +
	       ",startedEpoch=" + String(_startedEpoch) +
	       ",elapsedMs=" + String(_elapsedMs) +
	       ",zeroSixtyMs=" + String((s60 && s60->reached) ? s60->timeMs : 0) +
	       ",quarterMileMs=" + String(_quarterMileTimeMs) +
	       ",quarterMileTrapKmh=" + String(_quarterMileTrapKmh, 1) +
	       ",rollingSplitMs=" + String(rollingSplitMs()) +
	       ",maxSpeedKmh=" + String(_maxSpeedKmh, 1) +
	       ",distanceM=" + String(_distanceM, 1) + "\n";

	out += "elapsedMs,speedKmh,altitudeM,accelG\n";
	for (int i = 0; i < _historyCount; i++) {
		out += String(_history[i].tMs) + "," + String(_history[i].speedKmh, 2) + "," +
		       String(_history[i].altitudeM, 1) + "," + String(_history[i].accelG, 3) + "\n";
	}

	out += "\n# splits (km/h,ms,reached)\n";
	for (int i = 0; i < PERF_SPLIT_COUNT; i++) {
		out += String(_splits[i].thresholdKmh) + "," + String(_splits[i].timeMs) + "," + (_splits[i].reached ? "1" : "0") + "\n";
	}

	out += "\n# quarterMile (reached,timeMs,trapKmh)\n";
	out += String(quarterMileReached() ? "1" : "0") + "," + String(_quarterMileTimeMs) + "," + String(_quarterMileTrapKmh, 1) + "\n";

	out += "\n# rollingSplit60to130 (reached,ms)\n";
	out += String(rollingSplitReached() ? "1" : "0") + "," + String(rollingSplitMs()) + "\n";

	return out;
}

void PerformanceMode::update(float speedKmh, float altitudeM, float accelG, uint32_t nowMs) {
	float dt = (_lastSampleMs == 0) ? 0.0f : (nowMs - _lastSampleMs) / 1000.0f;
	_lastSampleMs = nowMs;
	_lastAltitudeM = altitudeM;
	_lastSpeedKmh = speedKmh;

	bool belowArm = speedKmh < PERF_ARM_SPEED_KMH;
	bool aboveTrigger = speedKmh > PERF_TRIGGER_SPEED_KMH;
	uint32_t dtMs = (dt > 0.0f && dt < 2.0f) ? (uint32_t)(dt * 1000.0f) : 0;

	_belowArmSpeedMs = belowArm ? (_belowArmSpeedMs + dtMs) : 0;
	_aboveTriggerSpeedMs = aboveTrigger ? (_aboveTriggerSpeedMs + dtMs) : 0;

	switch (_state) {
	case RunState::IDLE_WAIT:
		// Only reached once, at boot, before any run has ever happened --
		// nothing to preserve yet, so resetRun() here is harmless (all
		// zeros already) rather than load-bearing.
		if (_belowArmSpeedMs >= PERF_ARM_HOLD_MS) {
			_startAltitudeM = altitudeM;
			_state = RunState::ARMED;
		}
		break;

	case RunState::ARMED:
		_startAltitudeM = altitudeM; // keep tracking "at rest" altitude right up until launch
		if (_aboveTriggerSpeedMs >= PERF_TRIGGER_HOLD_MS) {
			// v1.2.0: resetRun() moved here (from the FINISHED->ARMED
			// transition below) -- clear the *previous* run's results right
			// as the *new* one actually starts, not the moment the car sits
			// still after finishing. That's what used to wipe a just-
			// completed run's stats within ~500ms, before there was any
			// real chance to read them.
			resetRun();
			_currentRunId = _storage ? Settings::nextRunId() : 0;
			setCurrentNameLocked(Settings::takePendingRunName()); // whatever was typed on-device beforehand
			_startedEpoch = TimeUtil::epochNow();
			_runStartMs = nowMs - _aboveTriggerSpeedMs; // back-date to when it actually crossed
			_lastHistoryMs = 0;
			_state = RunState::RUNNING;
		}
		break;

	case RunState::RUNNING: {
		if (dtMs > 0) {
			_distanceM += (speedKmh / 3.6f) * dt;
		}
		if (speedKmh > _maxSpeedKmh) _maxSpeedKmh = speedKmh;
		_elapsedMs = nowMs - _runStartMs;

		for (int i = 0; i < PERF_SPLIT_COUNT; i++) {
			if (!_splits[i].reached && speedKmh >= _splits[i].thresholdKmh) {
				_splits[i].reached = true;
				_splits[i].timeMs = _elapsedMs;
			}
		}

		if (_quarterMileTimeMs == 0 && _distanceM >= QUARTER_MILE_M) {
			_quarterMileTimeMs = _elapsedMs;
			_quarterMileTrapKmh = speedKmh;
		}
		if (_rollingLowMs == 0 && speedKmh >= PERF_ROLLING_LOW_KMH) {
			_rollingLowMs = nowMs;
		}
		if (_rollingLowMs != 0 && _rollingHighMs == 0 && speedKmh >= PERF_ROLLING_HIGH_KMH) {
			_rollingHighMs = nowMs;
		}

		recordHistory(speedKmh, altitudeM, accelG, nowMs);

		bool reachedTarget = speedKmh >= PERF_MAX_TARGET_KMH;
		bool droppedOff = _belowArmSpeedMs >= PERF_ARM_HOLD_MS;
		bool timedOut = _elapsedMs >= PERF_MAX_RUN_MS;
		if (reachedTarget || droppedOff || timedOut) {
			_frozenSlopePercent = slopePercent(); // still RUNNING here -- captures the live value one last time
			_state = RunState::FINISHED;
			// One-shot save, right as the run ends -- unlike TripMode's
			// incremental per-point SD writes (a drive can run for hours),
			// a run's entire history fits comfortably in RAM already
			// (PERF_HISTORY_MAX_POINTS caps it), so there's no need to
			// stream to SD while RUNNING -- just write the whole CSV once.
			_persistedToSd = _storage
				? _storage->saveRun(_currentRunId, currentName(), TimeUtil::stampForFilename(), toCsv())
				: false;
		}
		break;
	}

	case RunState::FINISHED:
		// v1.2.0: no longer calls resetRun() here -- see the ARMED case
		// above for why. Re-arming quietly in the background is fine; the
		// displayed results (distance/splits/quarter mile/etc.) stay frozen
		// and correct through this whole waiting period.
		if (_belowArmSpeedMs >= PERF_ARM_HOLD_MS) {
			_startAltitudeM = altitudeM;
			_state = RunState::ARMED;
		}
		break;
	}
}
