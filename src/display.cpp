#include "display.h"
#include "units.h"
#include "theme.h"
#include "settings.h"
#include <M5Cardputer.h>
#include <qrcode.h>

void Display::begin() {
	_w = M5Cardputer.Display.width();
	_h = M5Cardputer.Display.height();

	_canvas.setColorDepth(16);
	_canvas.setPsram(false); // 240x135x2 bytes is trivial in internal RAM; don't depend on PSRAM being fitted
	_canvas.createSprite(_w, _h);
	_canvas.setTextWrap(false);
}

// Drawn inside the header bar (filled with `barBg`), so the outline/nub/text
// use `ink` (the same dark-on-light color the header's mode label uses) --
// the bright fg color is reserved for the proportional fill so it still pops.
static void drawBatteryIcon(M5Canvas &c, int x, int y, const ThemePalette &pal, uint16_t ink, uint16_t barBg) {
	int32_t pct = M5Cardputer.Power.getBatteryLevel(); // 0-100, or -1 if unknown
	bool charging = M5Cardputer.Power.isCharging() == M5Cardputer.Power.is_charging;

	// Small icon: 16x8 body + 2x4 nub.
	c.drawRect(x, y, 16, 8, ink);
	c.fillRect(x + 16, y + 2, 2, 4, ink);
	if (pct >= 0) {
		int fillW = (int)((pct / 100.0f) * 14.0f);
		if (fillW > 0) c.fillRect(x + 1, y + 1, fillW, 6, pal.fg);
	}

	c.setTextSize(1);
	c.setTextColor(ink, barBg);
	c.setTextDatum(top_right);
	char buf[8];
	if (pct >= 0) snprintf(buf, sizeof(buf), "%s%d%%", charging ? "+" : "", (int)pct);
	else snprintf(buf, sizeof(buf), "?");
	c.drawString(buf, x - 3, y - 1);
}

void Display::drawChrome(const char *modeLabel, bool gpsHasFix, uint32_t satellites, const char *hints, int &contentTop, int &contentBottom) {
	ThemePalette pal = themeFor(Settings::theme());
	_canvas.fillSprite(pal.bg);
	_canvas.drawRect(0, 0, _w, _h, pal.dim);

	constexpr int HEADER_H = 14;
	constexpr int FOOTER_H = 11;

	// Header bar: mode name, GPS fix dot + sat count, battery.
	_canvas.fillRect(1, 1, _w - 2, HEADER_H - 2, pal.dim);
	_canvas.setTextSize(1);
	_canvas.setTextColor(pal.bg, pal.dim);
	_canvas.setTextDatum(top_left);
	_canvas.drawString(modeLabel, 4, 3);

	int fixDotX = 60;
	if (gpsHasFix) {
		_canvas.fillCircle(fixDotX, HEADER_H / 2, 3, pal.bg);
	} else {
		_canvas.drawCircle(fixDotX, HEADER_H / 2, 3, pal.bg);
	}
	char satBuf[10];
	snprintf(satBuf, sizeof(satBuf), "%lu", (unsigned long)satellites);
	_canvas.drawString(satBuf, fixDotX + 7, 3);

	drawBatteryIcon(_canvas, _w - 44, 4, pal, pal.bg, pal.dim);

	_canvas.drawFastHLine(0, HEADER_H, _w, pal.dim);

	// Footer bar: key hints, always visible so the "how do I interact with
	// this" question has an on-screen answer instead of needing the README.
	int footerY = _h - FOOTER_H;
	_canvas.drawFastHLine(0, footerY, _w, pal.dim);
	_canvas.setTextColor(pal.dim, pal.bg);
	_canvas.setTextDatum(bottom_left);
	_canvas.drawString(hints, 4, _h - 1);

	contentTop = HEADER_H + 3;
	contentBottom = footerY - 2;
}

// M5GFX/LGFX's '\n' (inside printf/print/println) resets the cursor's X to
// 0, not back to whatever setCursor() last set it to -- confirmed against
// LGFXBase::write()'s actual newline handling. Left uncorrected, only the
// very first line of a multi-line block (which got an explicit setCursor)
// sits at the intended left margin; every line after it snaps to the true
// screen edge instead. Call this after any printf/println ending in '\n'
// (or before the next one) to keep multi-line text actually left-aligned.
static void nextLine(M5Canvas &c, int leftX) {
	c.setCursor(leftX, c.getCursorY());
}

void Display::showStatus(bool gpsHasFix, uint32_t satellites, float fusedSpeedKmh, bool imuCalibrated, const char *lastKey) {
	int top, bottom;
	drawChrome(imuCalibrated ? "STATUS" : "CALIBRATING", gpsHasFix, satellites, "M:mode C:color U:unit Q:qr N:name", top, bottom);

	ThemePalette pal = themeFor(Settings::theme());
	_canvas.setTextColor(pal.fg, pal.bg);
	_canvas.setTextDatum(top_left);
	_canvas.setTextSize(2);
	_canvas.setCursor(4, top);
	_canvas.printf("IMU: %s\n", imuCalibrated ? "OK" : "cal...");
	nextLine(_canvas, 4);
	_canvas.printf("Speed: %.1f %s\n", Units::fromKmh(fusedSpeedKmh), Units::label());
	_canvas.setTextSize(1);
	nextLine(_canvas, 4);
	_canvas.setCursor(4, _canvas.getCursorY() + 8); // blank spacer line before Key
	_canvas.printf("Key: %s\n", (lastKey && lastKey[0]) ? lastKey : "(none yet)");

	_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

void Display::showPerformance(const PerformanceMode &perf, bool gpsHasFix, uint32_t satellites) {
	int top, bottom;
	drawChrome("PERF", gpsHasFix, satellites, "M:mode C:color U:unit Q:qr N:name", top, bottom);

	ThemePalette pal = themeFor(Settings::theme());
	_canvas.setTextColor(pal.fg, pal.bg);
	_canvas.setTextDatum(top_left);

	const char *stateStr = "WAIT";
	switch (perf.state()) {
		case RunState::IDLE_WAIT: stateStr = "WAIT"; break;
		case RunState::ARMED: stateStr = "ARMED"; break;
		case RunState::RUNNING: stateStr = "RUN"; break;
		case RunState::FINISHED: stateStr = "DONE"; break;
	}

	_canvas.setTextSize(2);
	_canvas.setCursor(4, top);
	_canvas.printf("%s  %lums\n", stateStr, (unsigned long)perf.elapsedMs());
	nextLine(_canvas, 4);
	_canvas.printf("Dist: %.1fm\n", perf.distanceM());
	nextLine(_canvas, 4);

	const SplitResult *s60 = perf.findSplit(60);
	if (s60 && s60->reached) {
		_canvas.printf("0-60: %.2fs\n", s60->timeMs / 1000.0f);
	} else {
		_canvas.printf("0-60: --\n");
	}
	nextLine(_canvas, 4);
	_canvas.printf("Max: %.1f %s\n", Units::fromKmh(perf.maxSpeedKmh()), Units::label());

	// Sparkline of the run's speed history, filling the rest of the content area.
	int historyCount = perf.historyCount();
	if (historyCount >= 2) {
		const HistoryPoint *hist = perf.history();
		float maxSpeed = 10.0f;
		for (int i = 0; i < historyCount; i++) {
			if (hist[i].speedKmh > maxSpeed) maxSpeed = hist[i].speedKmh;
		}
		int graphTop = bottom - 30;
		int graphHeight = 30;
		int graphLeft = 4;
		int graphWidth = _w - 8;
		for (int i = 1; i < historyCount; i++) {
			int x0 = graphLeft + (int)((float)(i - 1) / (historyCount - 1) * graphWidth);
			int x1 = graphLeft + (int)((float)i / (historyCount - 1) * graphWidth);
			int y0 = graphTop + graphHeight - (int)(hist[i - 1].speedKmh / maxSpeed * graphHeight);
			int y1 = graphTop + graphHeight - (int)(hist[i].speedKmh / maxSpeed * graphHeight);
			_canvas.drawLine(x0, y0, x1, y1, pal.fg);
		}
	}

	_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

static void formatDuration(uint32_t ms, char *buf, size_t bufLen) {
	uint32_t totalS = ms / 1000;
	uint32_t h = totalS / 3600;
	uint32_t m = (totalS % 3600) / 60;
	uint32_t s = totalS % 60;
	if (h > 0) snprintf(buf, bufLen, "%luh%02lum", (unsigned long)h, (unsigned long)m);
	else snprintf(buf, bufLen, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
}

// Shared by the live-speed and max-speed lines: clamps so a spurious multi-
// thousand-km/h GPS glitch (a "valid" fix reporting an absurd speed) can't
// push either line's text off the 240px screen at textSize(3) -- 4+ digit
// speeds would exceed the margin that normal drive values comfortably fit.
static float clampDisplaySpeed(float speedKmh) {
	return (speedKmh > 999.9f) ? 999.9f : speedKmh;
}

void Display::showTrip(const TripMode &trip, bool gpsHasFix, uint32_t satellites, float liveSpeedKmh) {
	int top, bottom;
	drawChrome("TRIP", gpsHasFix, satellites, "M:mode U:unit X:reset Q:qr N:name", top, bottom);

	ThemePalette pal = themeFor(Settings::theme());
	_canvas.setTextColor(pal.fg, pal.bg);
	_canvas.setTextDatum(top_left);

	// v1.1: state now reflects TripMode's own auto-start/stop machine, not
	// just "does GPS have a fix" -- WAIT means idle, waiting for movement to
	// auto-start a trip (see AUTO_TRIP_START_* in config.h).
	const char *stateStr;
	if (!gpsHasFix) stateStr = "NO FIX";
	else if (trip.isFull()) stateStr = "FULL";
	else if (trip.tripState() == TripState::RECORDING) stateStr = "REC";
	else stateStr = "WAIT";
	char durBuf[16];
	formatDuration(trip.durationMs(), durBuf, sizeof(durBuf));

	// Trip mode has no sparkline (nothing to graph -- it's one long steady
	// recording, not a single run), so the stats get the space Performance
	// mode spends on the graph.
	_canvas.setTextSize(3);
	_canvas.setCursor(4, top);
	_canvas.printf("%s %s\n", stateStr, durBuf);
	nextLine(_canvas, 4);
	_canvas.printf("D:%.1f%s\n", Units::fromKm(trip.distanceM() / 1000.0f), Units::distLabel());
	nextLine(_canvas, 4);
	_canvas.printf("Sp:%.1f%s\n", Units::fromKmh(clampDisplaySpeed(liveSpeedKmh)), Units::label());
	nextLine(_canvas, 4);
	_canvas.printf("Mx:%.1f%s\n", Units::fromKmh(clampDisplaySpeed(trip.maxSpeedKmh())), Units::label());
	// Back down to size 1 (from the stats block's size 3) -- four size-3
	// lines plus this one leaves no room at size 2 without clipping the
	// footer bar; this is the smallest legible size, freeing just enough.
	_canvas.setTextSize(1);
	nextLine(_canvas, 4);
	_canvas.printf("Pts:%d/%d\n", trip.pointCount(), TRIP_MAX_POINTS);

	_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

void Display::showTripSummary(const TripMode &trip, bool gpsHasFix, uint32_t satellites) {
	int top, bottom;
	drawChrome("TRIP DONE", gpsHasFix, satellites, "M:mode X:new Q:qr", top, bottom);

	ThemePalette pal = themeFor(Settings::theme());
	_canvas.setTextColor(pal.fg, pal.bg);
	_canvas.setTextDatum(top_left);

	char durBuf[16];
	formatDuration(trip.durationMs(), durBuf, sizeof(durBuf));

	_canvas.setTextSize(2);
	_canvas.setCursor(4, top);
	_canvas.printf("%s\n", trip.currentName().c_str());
	nextLine(_canvas, 4);
	_canvas.printf("Dist: %.1f%s\n", Units::fromKm(trip.distanceM() / 1000.0f), Units::distLabel());
	nextLine(_canvas, 4);
	_canvas.printf("Time: %s\n", durBuf);
	nextLine(_canvas, 4);
	_canvas.printf("Max: %.1f%s\n", Units::fromKmh(trip.maxSpeedKmh()), Units::label());
	_canvas.setTextSize(1);
	nextLine(_canvas, 4);
	if (trip.persistedToSd()) {
		_canvas.println("Saved to SD -- X for a new trip");
	} else {
		_canvas.println("NOT saved (check SD!) -- X for new");
	}

	_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

// Byte-mode capacity of QR_VERSION at ECC_LOW (ricmoo/QRCode's own table) --
// a hard runtime guard so a future longer AP_SSID fails loudly (a message
// screen) instead of qrcode_initText() overrunning qrcodeData's buffer.
static constexpr size_t QR_TEXT_MAX_BYTES = 53;

void Display::showQr(const char *ssid) {
	char text[80];
	int len = snprintf(text, sizeof(text), "WIFI:S:%s;T:nopass;;", ssid);

	_canvas.fillSprite(TFT_WHITE);

	if (len < 0 || (size_t)len > QR_TEXT_MAX_BYTES) {
		_canvas.setTextColor(TFT_BLACK, TFT_WHITE);
		_canvas.setTextDatum(top_left);
		_canvas.setTextSize(2);
		_canvas.setCursor(4, 4);
		_canvas.println("SSID too long");
		nextLine(_canvas, 4);
		_canvas.println("for QR (v1.1)");
		_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
		return;
	}

	QRCode qrcode;
	uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
	qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_LOW, text);

	int totalModules = qrcode.size + QR_QUIET_ZONE_MODULES * 2;
	int hintH = 12;
	int avail = min(_w, _h - hintH);
	int modulePx = avail / totalModules;
	if (modulePx < 1) modulePx = 1;
	int qrPx = totalModules * modulePx;
	int originX = (_w - qrPx) / 2;
	int originY = (_h - hintH - qrPx) / 2;
	if (originY < 0) originY = 0;

	for (int y = 0; y < qrcode.size; y++) {
		for (int x = 0; x < qrcode.size; x++) {
			if (qrcode_getModule(&qrcode, x, y)) {
				int px = originX + (x + QR_QUIET_ZONE_MODULES) * modulePx;
				int py = originY + (y + QR_QUIET_ZONE_MODULES) * modulePx;
				_canvas.fillRect(px, py, modulePx, modulePx, TFT_BLACK);
			}
		}
	}

	_canvas.setTextColor(TFT_BLACK, TFT_WHITE);
	_canvas.setTextDatum(bottom_center);
	_canvas.setTextSize(1);
	char line[64];
	snprintf(line, sizeof(line), "%s (open) - Q to return", ssid);
	_canvas.drawString(line, _w / 2, _h - 2);

	_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

void Display::showNameEntry(const char *title, const String &buffer) {
	int top, bottom;
	drawChrome(title, false, 0, "ENTER:save  DEL:back  `:cancel", top, bottom);

	ThemePalette pal = themeFor(Settings::theme());
	_canvas.setTextColor(pal.fg, pal.bg);
	_canvas.setTextDatum(top_left);

	_canvas.setTextSize(1);
	_canvas.setCursor(4, top);
	_canvas.printf("Name for next recording:\n");

	// Entry field: a framed box with the text and a blinking caret, so it's
	// obvious the device is waiting for typing rather than frozen.
	int boxY = top + 14;
	int boxH = 22;
	_canvas.drawRect(4, boxY, _w - 8, boxH, pal.dim);

	_canvas.setTextSize(2);
	_canvas.setCursor(8, boxY + 4);
	// textSize 2 is 12px/char; the box holds ~19 chars, so show the tail of
	// a longer name rather than letting it run off the edge.
	String shown = buffer;
	const int maxChars = (_w - 24) / 12;
	if ((int)shown.length() > maxChars) shown = shown.substring(shown.length() - maxChars);
	_canvas.print(shown);
	if ((millis() / 500) % 2 == 0) _canvas.print("_");

	_canvas.setTextSize(1);
	_canvas.setCursor(4, boxY + boxH + 6);
	_canvas.printf("%d/%d chars\n", (int)buffer.length(), TRIP_NAME_MAX_LEN);
	nextLine(_canvas, 4);
	_canvas.printf("Letters/digits/-/_ only\n");

	_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

void Display::showMessage(const char *line1, const char *line2) {
	ThemePalette pal = themeFor(Settings::theme());
	_canvas.fillSprite(pal.bg);
	_canvas.setTextColor(pal.fg, pal.bg);
	_canvas.setTextDatum(top_left);
	_canvas.setTextSize(2);
	_canvas.setCursor(4, 4);
	_canvas.println(line1);
	if (line2) {
		nextLine(_canvas, 4);
		_canvas.println(line2);
	}
	_canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}
