#pragma once

// Shared km/h <-> mph handling so the display, web dashboard, and KML export
// all agree on the active unit and can be switched at runtime.

enum class SpeedUnit { KMH, MPH };

class Units {
public:
	static SpeedUnit active;

	static void toggle() {
		active = (active == SpeedUnit::KMH) ? SpeedUnit::MPH : SpeedUnit::KMH;
	}

	// Internal speed values are always stored/computed in km/h; convert only at
	// the point of display/export.
	static float fromKmh(float kmh) {
		return (active == SpeedUnit::KMH) ? kmh : kmh * 0.621371f;
	}

	static const char *label() {
		return (active == SpeedUnit::KMH) ? "km/h" : "mph";
	}

	// Same conversion factor as fromKmh (mi = km * 0.621371); kept separate so
	// callers displaying a distance don't have to relabel a speed conversion.
	static float fromKm(float km) {
		return (active == SpeedUnit::KMH) ? km : km * 0.621371f;
	}

	static const char *distLabel() {
		return (active == SpeedUnit::KMH) ? "km" : "mi";
	}
};
