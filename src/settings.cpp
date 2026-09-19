#include "settings.h"
#include <Preferences.h>

static Preferences _prefs;

ThemeColor Settings::_theme = ThemeColor::GREEN;
AppMode Settings::_mode = AppMode::PERFORMANCE;
String Settings::_pendingTripName = "";
String Settings::_pendingRunName = "";
String Settings::_homeSsid = "";
String Settings::_homePassword = "";
String Settings::_uploadUrl = "";
SemaphoreHandle_t Settings::_mutex = nullptr;

void Settings::begin() {
	_mutex = xSemaphoreCreateMutex();
	_prefs.begin(SETTINGS_NAMESPACE, /*readOnly=*/false);
	_theme = (ThemeColor)_prefs.getUChar("theme", (uint8_t)ThemeColor::GREEN);
	_mode = (AppMode)_prefs.getUChar("mode", (uint8_t)AppMode::PERFORMANCE);
	Units::active = (SpeedUnit)_prefs.getUChar("unit", (uint8_t)SpeedUnit::KMH);
	_homeSsid = _prefs.getString("homeSsid", "");
	_homePassword = _prefs.getString("homePass", "");
	_uploadUrl = _prefs.getString("uploadUrl", "");
}

void Settings::setTheme(ThemeColor t) {
	_theme = t;
	_prefs.putUChar("theme", (uint8_t)t);
}

void Settings::setMode(AppMode m) {
	_mode = m;
	_prefs.putUChar("mode", (uint8_t)m);
}

void Settings::toggleUnit() {
	Units::toggle();
	_prefs.putUChar("unit", (uint8_t)Units::active);
}

uint32_t Settings::nextTripId() {
	uint32_t id = _prefs.getUInt("tripId", 0) + 1;
	_prefs.putUInt("tripId", id);
	return id;
}

uint32_t Settings::nextRunId() {
	uint32_t id = _prefs.getUInt("runId", 0) + 1;
	_prefs.putUInt("runId", id);
	return id;
}

void Settings::setPendingTripName(const String &name) {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	_pendingTripName = name.substring(0, TRIP_NAME_MAX_LEN);
	xSemaphoreGive(_mutex);
}

String Settings::takePendingTripName() {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	String n = _pendingTripName;
	_pendingTripName = "";
	xSemaphoreGive(_mutex);
	return n;
}

String Settings::peekPendingTripName() {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	String n = _pendingTripName;
	xSemaphoreGive(_mutex);
	return n;
}

void Settings::setPendingRunName(const String &name) {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	_pendingRunName = name.substring(0, TRIP_NAME_MAX_LEN);
	xSemaphoreGive(_mutex);
}

String Settings::takePendingRunName() {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	String n = _pendingRunName;
	_pendingRunName = "";
	xSemaphoreGive(_mutex);
	return n;
}

String Settings::peekPendingRunName() {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	String n = _pendingRunName;
	xSemaphoreGive(_mutex);
	return n;
}

void Settings::setSyncConfig(const String &ssid, const String &password, const String &uploadUrl) {
	String s = ssid.substring(0, SETTINGS_STRING_MAX_LEN);
	String p = password.substring(0, SETTINGS_STRING_MAX_LEN);
	String u = uploadUrl.substring(0, SETTINGS_STRING_MAX_LEN);

	xSemaphoreTake(_mutex, portMAX_DELAY);
	_homeSsid = s;
	_homePassword = p;
	_uploadUrl = u;
	xSemaphoreGive(_mutex);

	_prefs.putString("homeSsid", s);
	_prefs.putString("homePass", p);
	_prefs.putString("uploadUrl", u);
}

String Settings::homeSsid() {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	String v = _homeSsid;
	xSemaphoreGive(_mutex);
	return v;
}

String Settings::homePassword() {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	String v = _homePassword;
	xSemaphoreGive(_mutex);
	return v;
}

String Settings::uploadUrl() {
	xSemaphoreTake(_mutex, portMAX_DELAY);
	String v = _uploadUrl;
	xSemaphoreGive(_mutex);
	return v;
}
