#pragma once

#include <Arduino.h>
#include "../include/config.h"

// Four color presets (GN/RD/BU/WH), Pip-Boy-style: a single bright color on
// near-black, no other hues in the UI. Colors are M5GFX 16-bit RGB565 values
// (use M5GFX's color565() at the call site, not raw hex, to stay panel-correct).
struct ThemePalette {
	uint16_t fg;     // primary text/graph color
	uint16_t dim;     // secondary text, frame lines, scanlines
	uint16_t bg;      // background (always near-black, never fully 0 so the
	                  // scanline effect has something to sit on top of)
	const char *label; // short name shown on the settings line ("GN"/"RD"/"BU"/"WH")
};

ThemePalette themeFor(ThemeColor color);
ThemeColor nextTheme(ThemeColor color);
