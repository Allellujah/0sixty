#pragma once

#include <Arduino.h>

// Fuses the BMI270's accelerometer with GPS fixes to estimate speed at a
// higher effective rate than the GPS's own 1-10Hz. GPS remains ground truth;
// the IMU only fills the gaps between fixes and gets corrected back toward
// GPS on every new one. See the "GPS + IMU fusion" section of the project
// plan for the reasoning -- this is a simple complementary filter, not a
// full Kalman filter, which is adequate for split-timing resolution.
//
// Forward-axis calibration is fully automatic (no keypress): the physical
// keyboard is unusable while GPS is running (see config.h's G8/G9 note), so
// this tracks a slow per-axis baseline and locks the forward axis onto
// whichever axis first shows a real acceleration event -- naturally the
// device's first real launch/push, no user action required.
class ImuFusion {
public:
	void begin();

	// Call as often as possible (target ~IMU_SAMPLE_HZ). Integrates
	// acceleration forward in time between GPS fixes, and -- until
	// calibrated -- watches for the first real acceleration event to lock
	// the forward axis.
	void update();

	// Call whenever GpsReader::update() returns a new fix, with that fix's
	// speed and timestamp, to correct drift.
	void onGpsFix(float gpsSpeedKmh, uint32_t timestampMs);

	float fusedSpeedKmh() const { return _fusedSpeedKmh; }
	bool isCalibrated() const { return _calibrated; }
	// Last forward-axis acceleration in g, bias-corrected. For display/logging
	// (g-force readout) -- not used internally beyond the speed integration.
	float accelG() const { return _lastAccelG; }

	// v1.1: generic hard-brake/swerve/bump heuristic. Orientation-independent
	// (unlike accelG(), which is forward-axis only): total accel vector
	// magnitude should sit near 1g whenever the device isn't accelerating,
	// regardless of mounting angle, since that's just gravity. A deviation
	// past HARD_EVENT_THRESHOLD_G (config.h) flags *some* hard maneuver
	// without needing to know which physical axis is "lateral". Edge-
	// triggered with a debounce window so one sustained event (e.g. a long
	// skid) logs once, not every loop iteration while it's happening -- call
	// once per loop while Trip mode is actively recording; returns true at
	// most once per HARD_EVENT_DEBOUNCE_MS.
	bool checkHardEvent(uint32_t nowMs, float &outDeviationG);

private:
	bool _calibrated = false;
	int _forwardAxis = 0; // 0=x, 1=y, 2=z
	float _forwardSign = 1.0f;
	float _baseline[3] = {0.0f, 0.0f, 0.0f}; // slow-tracked at-rest reading per axis
	int _candidateAxis = -1; // debounce state for auto-calibration, see config.h
	float _candidateSign = 1.0f;
	uint32_t _candidateMs = 0;
	float _accelBias = 0.0f; // slowly-adapted zero-offset on the forward axis, post-calibration
	float _fusedSpeedKmh = 0.0f;
	float _lastAccelG = 0.0f;
	uint32_t _lastUpdateMs = 0;
	uint32_t _stationaryMs = 0; // for zero-velocity update, see config.h
	float _lastMagnitudeDeviationG = 0.0f; // |accel vector magnitude - 1g|, updated every update() call
	uint32_t _lastEventLoggedMs = 0;

	void readAccel(float &ax, float &ay, float &az);
	float readForwardAccelMs2();
};
