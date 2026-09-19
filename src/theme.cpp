#include "theme.h"

ThemePalette themeFor(ThemeColor color) {
	switch (color) {
		case ThemeColor::GREEN:
			// Classic Pip-Boy phosphor green.
			return {rgb565(80, 255, 90), rgb565(20, 90, 25), rgb565(0, 10, 2), "GN"};
		case ThemeColor::RED:
			return {rgb565(255, 70, 60), rgb565(100, 25, 20), rgb565(10, 1, 0), "RD"};
		case ThemeColor::BLUE:
			return {rgb565(80, 170, 255), rgb565(25, 55, 100), rgb565(0, 2, 10), "BU"};
		case ThemeColor::WHITE:
		default:
			return {rgb565(240, 240, 240), rgb565(110, 110, 110), rgb565(6, 6, 6), "WH"};
	}
}

ThemeColor nextTheme(ThemeColor color) {
	switch (color) {
		case ThemeColor::GREEN: return ThemeColor::RED;
		case ThemeColor::RED: return ThemeColor::BLUE;
		case ThemeColor::BLUE: return ThemeColor::WHITE;
		case ThemeColor::WHITE:
		default: return ThemeColor::GREEN;
	}
}
