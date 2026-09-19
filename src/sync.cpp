#include "sync.h"
#include "storage.h"
#include "settings.h"
#include "../include/config.h"
#include <WiFi.h>
#include <HTTPClient.h>

// Wraps Storage's mutex-protected trip-file reads as a Stream, so
// HTTPClient's streamed POST never touches SD directly (bypassing the lock
// that protects it from the async web server's own SD reads -- see
// storage.h). Read-only; write() is unused.
class LockedTripFileStream : public Stream {
public:
	LockedTripFileStream(Storage *storage, File file, size_t size)
		: _storage(storage), _file(file), _size(size) {}

	int available() override { return (int)(_size - _pos); }
	int read() override {
		uint8_t b;
		size_t n = _storage->readTripChunk(_file, &b, 1);
		if (n != 1) return -1;
		_pos++;
		return b;
	}
	int peek() override { return -1; } // not used by HTTPClient's send path
	size_t readBytes(char *buffer, size_t length) override {
		size_t n = _storage->readTripChunk(_file, (uint8_t *)buffer, length);
		_pos += n;
		return n;
	}
	size_t write(uint8_t) override { return 0; }

private:
	Storage *_storage;
	File _file;
	size_t _size;
	size_t _pos = 0;
};

void Sync::begin(Storage *storage) {
	_storage = storage;
}

void Sync::abortToIdle() {
	WiFi.disconnect();
	WiFi.mode(WIFI_AP);
	_state = SyncState::IDLE;
}

bool Sync::uploadOne(const String &filename) {
	File f = _storage->openTripForRead(filename);
	if (!f) return false;
	size_t size = f.size();

	LockedTripFileStream stream(_storage, f, size);

	WiFiClient client;
	HTTPClient http;
	String url = Settings::uploadUrl();
	if (!http.begin(client, url)) {
		_storage->closeTripRead(f);
		return false;
	}
	http.addHeader("Content-Type", "text/csv");
	http.addHeader("X-Trip-Filename", filename);

	int code = http.sendRequest("POST", &stream, size);
	http.end();
	_storage->closeTripRead(f);

	bool ok = (code >= 200 && code < 300);
	if (ok) _storage->markTripUploaded(filename);
	return ok;
}

void Sync::loop(bool busy, uint32_t nowMs) {
	switch (_state) {
		case SyncState::IDLE: {
			if (busy) return;
			if (Settings::homeSsid().length() == 0 || Settings::uploadUrl().length() == 0) return; // not configured
			if (_lastCheckMs != 0 && (nowMs - _lastCheckMs) < AUTO_UPLOAD_CHECK_INTERVAL_MS) return;
			_lastCheckMs = nowMs;

			_pendingCount = _storage->listTrips(_pending, TRIP_LIST_MAX);
			_pendingIdx = 0;
			while (_pendingIdx < _pendingCount && _storage->isTripUploaded(_pending[_pendingIdx].filename)) {
				_pendingIdx++;
			}
			if (_pendingIdx >= _pendingCount) return; // nothing to upload

			// Join the home network as a station. ESP32-S3 supports
			// concurrent AP+STA, so the device's own SoftAP dashboard keeps
			// running throughout (both radios share one channel while STA
			// is connected). WiFi.begin() itself is non-blocking -- the
			// actual wait for a result happens across later loop() calls
			// below, not here.
			WiFi.mode(WIFI_AP_STA);
			WiFi.begin(Settings::homeSsid().c_str(), Settings::homePassword().c_str());
			_connectStartMs = nowMs;
			_state = SyncState::CONNECTING;
			return;
		}

		case SyncState::CONNECTING: {
			if (busy) {
				// Caller needs full attention now (e.g. a Performance run
				// just armed) -- bail out cleanly rather than press on.
				abortToIdle();
				return;
			}
			if (WiFi.status() == WL_CONNECTED) {
				_state = SyncState::UPLOADING;
				return;
			}
			if (nowMs - _connectStartMs >= WIFI_STA_CONNECT_TIMEOUT_MS) {
				abortToIdle();
			}
			return; // still waiting -- checked again next loop() call, no blocking here
		}

		case SyncState::UPLOADING: {
			if (busy) {
				abortToIdle();
				return;
			}
			while (_pendingIdx < _pendingCount && _storage->isTripUploaded(_pending[_pendingIdx].filename)) {
				_pendingIdx++;
			}
			if (_pendingIdx >= _pendingCount) {
				abortToIdle();
				return;
			}
			// One file's POST per call -- still a blocking HTTPClient call
			// for its duration, but bounded to a single (typically small,
			// see CHANGELOG.md) trip file rather than every pending file
			// back-to-back, and only while the caller has already said
			// nothing else needs the CPU/radio right now.
			uploadOne(_pending[_pendingIdx].filename);
			_pendingIdx++;
			return;
		}
	}
}
