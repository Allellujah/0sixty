#include "gps.h"
#include "../include/config.h"
#include <TinyGPSPlus.h>

static TinyGPSPlus _tgps;
static HardwareSerial _gpsSerial(2); // UART2, free on the Cardputer

void GpsReader::begin() {
	_gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

static constexpr uint32_t FIX_STALE_AFTER_MS = 5000;

bool GpsReader::update() {
	bool newFix = false;

	while (_gpsSerial.available()) {
		char c = _gpsSerial.read();
		if (Serial) Serial.write(c); // raw NMEA passthrough for live debugging, only if a host has the CDC port open
		if (_tgps.encode(c)) {
			// TinyGPSPlus just finished a full, checksum-valid sentence.
			if (_tgps.location.isUpdated() && _tgps.location.isValid()) {
				_fix.valid = true;
				_fix.lat = _tgps.location.lat();
				_fix.lng = _tgps.location.lng();
				_fix.timestampMs = millis();
				newFix = true;
			}
			if (_tgps.speed.isUpdated()) {
				_fix.speedKmh = _tgps.speed.kmph();
			}
			if (_tgps.altitude.isUpdated()) {
				_fix.altitudeM = _tgps.altitude.meters();
			}
			if (_tgps.satellites.isUpdated()) {
				_fix.satellites = _tgps.satellites.value();
			}
			// v1.3.0: UTC date/time. Both halves have to be valid together --
			// TinyGPSPlus can hand back a plausible-looking time with a
			// still-unset date early in a cold start, and a timestamp with
			// the wrong day is worse than no timestamp at all, since it goes
			// straight into filenames.
			if (_tgps.date.isValid() && _tgps.time.isValid() && _tgps.date.year() >= 2024) {
				_fix.dateTimeValid = true;
				_fix.year = _tgps.date.year();
				_fix.month = _tgps.date.month();
				_fix.day = _tgps.date.day();
				_fix.hour = _tgps.time.hour();
				_fix.minute = _tgps.time.minute();
				_fix.second = _tgps.time.second();
			}
		}
	}

	// Without this, one good fix latches _fix.valid=true for the rest of the
	// session even after losing satellite lock (tunnel, cap unplugged) --
	// the display/dashboard would keep showing "FIX" over a stale position.
	if (_fix.valid && _tgps.location.age() > FIX_STALE_AFTER_MS) {
		_fix.valid = false;
	}

	return newFix;
}

void GpsReader::configureRate(int hz) {
	// TODO: blocked on the AT6668/CASIC datasheet -- see gps.h. Once the
	// $PCAS (or equivalent) sentence for rate-setting is known, send it here,
	// e.g.: _gpsSerial.println("$PCAS02,100*1E"); for 10Hz (100ms interval),
	// with the correct checksum for whatever the real command turns out to be.
	(void)hz;
}
