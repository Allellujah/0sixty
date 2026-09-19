#pragma once

#include <Arduino.h>

struct GpsFix {
	bool valid = false;
	double lat = 0.0;
	double lng = 0.0;
	float speedKmh = 0.0f;
	float altitudeM = 0.0f;
	uint32_t satellites = 0;
	uint32_t timestampMs = 0; // millis() at time of fix, for IMU fusion

	// v1.3.0: UTC wall-clock from the GNSS stream. This is the device's only
	// clock source -- there's no RTC on board -- so until the first fix with
	// a valid date arrives, the system has no idea what day it is. Once one
	// does, timeutil.cpp latches it into the ESP32's own system clock, after
	// which time keeps running even if the fix is lost (tunnel, garage).
	bool dateTimeValid = false;
	uint16_t year = 0;
	uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

class GpsReader {
public:
	void begin();

	// Call every loop iteration; feeds any pending UART bytes into the NMEA
	// parser. Returns true exactly once per new fix (edge-triggered), so
	// callers can react only when there's fresh data.
	bool update();

	const GpsFix &lastFix() const { return _fix; }
	uint32_t satellites() const { return _fix.satellites; }
	bool hasFix() const { return _fix.valid; }

	// NOTE (blocking unknown, see project plan): the ATGM336H-6N@AT6668
	// likely defaults to 1Hz NMEA output. Raising it to the cap's stated
	// 10Hz ceiling needs its CASIC/$PCAS configuration command set, which
	// is pending the module datasheet. Call this once that's known --
	// everything else in this class works at whatever the default rate is.
	void configureRate(int hz);

private:
	GpsFix _fix;
};
