#include "imu.h"
#include "../include/config.h"
#include <M5Cardputer.h>

static constexpr float G_TO_MS2 = 9.80665f;
static constexpr float CORRECTION_BLEND = 0.15f; // how hard each GPS fix pulls the fused estimate back

void ImuFusion::begin() {
	_lastUpdateMs = millis();
}

void ImuFusion::readAccel(float &ax, float &ay, float &az) {
	M5.Imu.getAccel(&ax, &ay, &az);
}

float ImuFusion::readForwardAccelMs2() {
	float ax = 0, ay = 0, az = 0;
	readAccel(ax, ay, az);
	float raw = (_forwardAxis == 0) ? ax : (_forwardAxis == 1) ? ay : az;
	return (raw * _forwardSign * G_TO_MS2) - _accelBias;
}

void ImuFusion::update() {
	uint32_t now = millis();
	float dt = (now - _lastUpdateMs) / 1000.0f;
	_lastUpdateMs = now;
	if (dt <= 0.0f || dt > 0.5f) return; // guard against startup/stall glitches

	if (!_calibrated) {
		float ax = 0, ay = 0, az = 0;
		readAccel(ax, ay, az);
		float raw[3] = {ax, ay, az};

		// First-ever reading: seed the baseline instead of comparing against zero.
		static bool baselineSeeded = false;
		if (!baselineSeeded) {
			_baseline[0] = ax;
			_baseline[1] = ay;
			_baseline[2] = az;
			baselineSeeded = true;
			return;
		}

		int bestAxis = -1;
		float bestDeviation = 0.0f;
		for (int i = 0; i < 3; i++) {
			float deviation = raw[i] - _baseline[i];
			if (fabsf(deviation) > fabsf(bestDeviation)) {
				bestDeviation = deviation;
				bestAxis = i;
			}
		}

		bool overThreshold = bestAxis >= 0 && fabsf(bestDeviation) > AUTO_CALIBRATION_THRESHOLD_G;
		float sign = (bestDeviation >= 0) ? 1.0f : -1.0f;

		// Debounced: a single noisy sample (a bump, picking the device up)
		// must not permanently lock the wrong axis -- require the same
		// axis/direction to stay over threshold for AUTO_CALIBRATION_DEBOUNCE_MS.
		if (overThreshold && bestAxis == _candidateAxis && sign == _candidateSign) {
			_candidateMs += (uint32_t)(dt * 1000.0f);
		} else if (overThreshold) {
			_candidateAxis = bestAxis;
			_candidateSign = sign;
			_candidateMs = 0;
		} else {
			_candidateAxis = -1;
			_candidateMs = 0;
		}

		if (overThreshold && _candidateMs >= AUTO_CALIBRATION_DEBOUNCE_MS) {
			_forwardAxis = bestAxis;
			_forwardSign = sign;
			_accelBias = 0.0f;
			_calibrated = true;
			_fusedSpeedKmh = 0.0f;
			return;
		}

		// Still at rest (or noise below threshold): keep tracking baseline so
		// a slow tilt/remount doesn't get mistaken for a launch event later.
		for (int i = 0; i < 3; i++) {
			_baseline[i] += (raw[i] - _baseline[i]) * BASELINE_LOWPASS_ALPHA;
		}
		return;
	}

	float accel = readForwardAccelMs2();
	_lastAccelG = accel / G_TO_MS2;

	// Hard-event heuristic: read the full vector (not just the forward axis)
	// so a lateral swerve is caught too, not just braking/acceleration.
	{
		float ax = 0, ay = 0, az = 0;
		readAccel(ax, ay, az);
		float magnitudeG = sqrtf(ax * ax + ay * ay + az * az);
		_lastMagnitudeDeviationG = fabsf(magnitudeG - 1.0f);
	}

	if (fabsf(accel) < ZUPT_ACCEL_THRESHOLD_MS2) {
		_stationaryMs += (uint32_t)(dt * 1000.0f);
		if (_stationaryMs >= ZUPT_HOLD_MS) {
			_fusedSpeedKmh = 0.0f;
			_accelBias += accel * 0.02f; // slowly null residual bias while confirmed stationary
		}
	} else {
		_stationaryMs = 0;
		float dSpeedMs = accel * dt;
		_fusedSpeedKmh += dSpeedMs * 3.6f;
		if (_fusedSpeedKmh < 0.0f) _fusedSpeedKmh = 0.0f;
	}
}

void ImuFusion::onGpsFix(float gpsSpeedKmh, uint32_t timestampMs) {
	(void)timestampMs;
	if (!_calibrated) return; // nothing to correct yet

	if (gpsSpeedKmh < 2.0f) {
		// GPS itself says we're essentially stationary -- trust that fully
		// rather than a slow blend. Also hard re-baseline the accel bias to
		// whatever the forward axis reads right now: a single-axis
		// accelerometer picks up a gravity component the moment the device
		// isn't held perfectly level, and that tilt can easily exceed the
		// IMU-only ZUPT threshold (config.h), causing runaway drift at rest
		// (observed: 155km/h while sitting still). GPS-confirmed rest is a
		// much stronger, more available signal to re-anchor against than
		// waiting for the IMU's own noise to look "stationary enough."
		_fusedSpeedKmh = 0.0f;
		float ax = 0, ay = 0, az = 0;
		readAccel(ax, ay, az);
		float raw = (_forwardAxis == 0) ? ax : (_forwardAxis == 1) ? ay : az;
		_accelBias = raw * _forwardSign * G_TO_MS2;
		_stationaryMs = ZUPT_HOLD_MS;
		return;
	}

	// Complementary blend toward GPS truth rather than a hard snap, so the
	// live graph doesn't visibly jump on every fix.
	float error = gpsSpeedKmh - _fusedSpeedKmh;
	_fusedSpeedKmh += error * CORRECTION_BLEND;

	// Slowly adapt the accel bias from steady-state error (crude but
	// effective drift compensation) -- tune the divisor once tested live.
	_accelBias += (error / 3.6f) * 0.01f;
}

bool ImuFusion::checkHardEvent(uint32_t nowMs, float &outDeviationG) {
	if (!_calibrated) return false;
	if (_lastMagnitudeDeviationG < HARD_EVENT_THRESHOLD_G) return false;
	if (nowMs - _lastEventLoggedMs < HARD_EVENT_DEBOUNCE_MS) return false;
	_lastEventLoggedMs = nowMs;
	outDeviationG = _lastMagnitudeDeviationG;
	return true;
}
