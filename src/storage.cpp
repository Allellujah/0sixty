#include "storage.h"
#include "../include/config.h"
#include <SPI.h>

// RAII-style lock over _sdMutex -- every method below wraps its actual SD
// work in one of these so no two tasks (main loop vs. the async web server,
// see storage.h) ever touch the SD/SPI bus at the same time.
class SdLock {
public:
	explicit SdLock(SemaphoreHandle_t m) : _m(m) {
		if (_m) xSemaphoreTake(_m, portMAX_DELAY);
	}
	~SdLock() {
		if (_m) xSemaphoreGive(_m);
	}
private:
	SemaphoreHandle_t _m;
};

bool Storage::begin() {
	_sdMutex = xSemaphoreCreateMutex();

	// The Cardputer's onboard SD slot is NOT on the default ESP32 SPI pins --
	// it's wired to SCK=G40, MISO=G39, MOSI=G14, CS=G12 per M5Stack's own
	// sdcard.ino example. SD.begin() with no args silently fails against
	// the wrong (default) pins.
	//
	// Root cause of SD never mounting, found 2026-09-16: the LoRa1262 Cap's
	// SX1262 radio shares this exact SPI bus (its MOSI/MISO/SCK are the same
	// physical pins as SD_MOSI_PIN/SD_MISO_PIN/SD_SCK_PIN), with its own CS
	// on LORA_CS_PIN (G5) -- a supported shared-bus design, but only if
	// whichever device is idle keeps its CS deselected. This project never
	// touches the LoRa radio, so that pin was left floating and the SX1262
	// was very likely interfering with every SD transaction. Must be parked
	// HIGH (deselected) before any SPI activity on this bus, every boot.
	pinMode(LORA_CS_PIN, OUTPUT);
	digitalWrite(LORA_CS_PIN, HIGH);

	SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

	bool mounted = false;
	const uint32_t attemptFreqs[] = {25000000, 10000000, 4000000};
	for (uint32_t freq : attemptFreqs) {
		if (SD.begin(SD_CS_PIN, SPI, freq)) {
			mounted = true;
			break;
		}
		SD.end();
		delay(100);
	}
	if (!mounted) {
		_ready = false;
		return false;
	}

	if (!SD.exists(LOG_DIR)) {
		SD.mkdir(LOG_DIR);
	}
	if (!SD.exists(TRIPS_DIR)) {
		SD.mkdir(TRIPS_DIR);
	}
	if (!SD.exists(RUNS_DIR)) {
		SD.mkdir(RUNS_DIR);
	}

	char path[48];
	snprintf(path, sizeof(path), "%s/raw_%lu.csv", LOG_DIR, (unsigned long)millis());
	_currentLogPath = path;

	// Held open for the whole session (see logSample()) rather than
	// reopened per sample -- avoids repeated open/close overhead at 10Hz,
	// which was stalling loop() long enough to risk desyncing GPS UART
	// reads and skewing IMU dt, and is a plausible stressor for the SD
	// flakiness itself.
	_logFile = SD.open(_currentLogPath, FILE_WRITE);
	if (!_logFile) {
		_ready = false;
		return false;
	}
	_logFile.println("millis,lat,lng,speedKmh,altitudeM,satellites");
	_logFile.flush();

	_ready = true;
	return true;
}

void Storage::logSample(uint32_t timestampMs, double lat, double lng, float speedKmh, float altitudeM, uint32_t satellites) {
	if (!_ready) return;
	SdLock lock(_sdMutex);

	_logFile.printf("%lu,%.6f,%.6f,%.2f,%.1f,%lu\n",
	                 (unsigned long)timestampMs, lat, lng, speedKmh, altitudeM, (unsigned long)satellites);
	_logFile.flush();
}

String sanitizeName(const String &raw) {
	String out;
	for (size_t i = 0; i < raw.length() && (int)out.length() < TRIP_NAME_MAX_LEN; i++) {
		char c = raw[i];
		bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		            (c >= '0' && c <= '9') || c == '-' || c == '_';
		out += safe ? c : '-';
	}
	// Trailing separators look like truncation damage in a filename; strip them.
	while (out.length() > 0 && (out[out.length() - 1] == '-' || out[out.length() - 1] == '_')) {
		out.remove(out.length() - 1);
	}
	return out;
}

// v1.3.0 filename shape: "<prefix><id>[_<date>][_<name>].csv", e.g.
// "trip00012_20260918-1432_Belgium.csv". The zero-padded id stays first so
// plain lexicographic order still matches numeric order, and so the id can
// still be recovered from the name (see idFromFilename()). The date and
// name parts are each omitted when unavailable -- an unnamed trip recorded
// before the GPS clock was set is simply "trip00013.csv", exactly the v1.2
// shape, which is also why old files on an existing card keep working.
static String buildRecordingFilename(const char *prefix, uint32_t id, const String &dateStamp, const String &name) {
	char head[24];
	snprintf(head, sizeof(head), "%s%05lu", prefix, (unsigned long)id);
	String out(head);
	if (dateStamp.length() > 0) out += "_" + dateStamp;
	String clean = sanitizeName(name);
	if (clean.length() > 0) out += "_" + clean;
	out += ".csv";
	return out;
}

bool Storage::beginTrip(uint32_t id, const String &name, const String &dateStamp, uint32_t startedEpoch) {
	if (!_ready) return false;
	SdLock lock(_sdMutex);

	_currentTripFilename = buildRecordingFilename("trip", id, dateStamp, name);
	String path = String(TRIPS_DIR) + "/" + _currentTripFilename;
	_tripFile = SD.open(path, FILE_WRITE);
	if (!_tripFile) {
		_tripOpen = false;
		return false;
	}
	_tripFile.printf("# id=%lu,author=%s,name=%s,started=%lu,startedEpoch=%lu\n",
	                 (unsigned long)id, AUTHOR_NAME, sanitizeName(name).c_str(),
	                 (unsigned long)millis(), (unsigned long)startedEpoch);
	_tripFile.println("millis,lat,lng,speedKmh,altitudeM");
	_tripFile.flush();
	_tripOpen = true;
	return true;
}

void Storage::logTripPoint(uint32_t tMs, double lat, double lng, float speedKmh, float altitudeM) {
	if (!_tripOpen) return;
	SdLock lock(_sdMutex);

	_tripFile.printf("%lu,%.6f,%.6f,%.2f,%.1f\n", (unsigned long)tMs, lat, lng, speedKmh, altitudeM);
	_tripFile.flush();
}

void Storage::logTripEvent(uint32_t tMs, double lat, double lng, float deviationG) {
	if (!_tripOpen) return;
	SdLock lock(_sdMutex);

	_tripFile.printf("# event,%lu,%.6f,%.6f,%.2f\n", (unsigned long)tMs, lat, lng, deviationG);
	_tripFile.flush();
}

void Storage::renameTrip(const String &name) {
	if (!_tripOpen) return;
	SdLock lock(_sdMutex);

	_tripFile.printf("# name=%s\n", name.c_str());
	_tripFile.flush();
}

void Storage::endTrip(float distanceM, uint32_t durationMs, float maxSpeedKmh) {
	if (!_tripOpen) return;
	SdLock lock(_sdMutex);

	_tripFile.printf("# ended=%lu,distanceM=%.1f,durationMs=%lu,maxSpeedKmh=%.1f\n",
	                  (unsigned long)millis(), distanceM, (unsigned long)durationMs, maxSpeedKmh);
	_tripFile.flush();
	_tripFile.close();
	_tripOpen = false;
	_currentTripFilename = "";
}

// v1.3.0: key-based "key=value,key=value,..." lookup, replacing the earlier
// fixed-order positional parsing. Positional parsing meant that adding any
// new metadata field (as this version does, with startedEpoch and run
// names) silently shifted every field after it when reading a file written
// by an older firmware -- producing plausible-looking but wrong numbers
// rather than an obvious failure. Looking fields up by name instead makes
// old files, new files, and any future added field all read correctly, and
// a missing field simply returns the supplied default.
//
// Values never contain ',' or '=' (names are run through sanitizeName() at
// write time, numeric fields obviously can't), so a plain scan is enough.
static String findField(const String &line, const char *key, const String &fallback = String("")) {
	String needle = String(key) + "=";
	int at = line.indexOf(needle);
	// Must sit at the start or immediately after a comma, or "ended=" would
	// also match inside e.g. "appended=".
	while (at > 0 && line[at - 1] != ',' && line[at - 1] != ' ') {
		at = line.indexOf(needle, at + 1);
	}
	if (at < 0) return fallback;
	int valueStart = at + needle.length();
	int comma = line.indexOf(',', valueStart);
	String value = (comma < 0) ? line.substring(valueStart) : line.substring(valueStart, comma);
	value.trim();
	return value.length() > 0 ? value : fallback;
}

static uint32_t findUInt(const String &line, const char *key) {
	return (uint32_t)findField(line, key, "0").toInt();
}
static float findFloat(const String &line, const char *key) {
	return findField(line, key, "0").toFloat();
}

// Recovers the numeric id from a filename of either the v1.2 shape
// ("trip00012.csv") or the v1.3 shape ("trip00012_20260918-1432_Name.csv"):
// everything between the prefix and the first '_' or '.'.
static uint32_t idFromFilename(const String &filename, const char *prefix) {
	int start = strlen(prefix);
	if ((int)filename.length() <= start) return 0;
	int end = filename.length();
	for (int i = start; i < (int)filename.length(); i++) {
		if (filename[i] == '_' || filename[i] == '.') {
			end = i;
			break;
		}
	}
	return (uint32_t)filename.substring(start, end).toInt();
}

int Storage::listTrips(TripSummary *out, int maxCount) {
	if (!_ready || maxCount <= 0) return 0;
	SdLock lock(_sdMutex);

	File dir = SD.open(TRIPS_DIR);
	if (!dir) return 0;

	// Scan cap independent of maxCount (the dashboard's display cap) so a
	// device with more trips than TRIP_LIST_MAX still finds the *most
	// recent* ones rather than whatever the filesystem happens to return
	// first. Static, not heap-allocated -- matches this project's fixed-
	// buffer convention (see config.h's TripPoint/HistoryPoint arrays), and
	// avoids a repeated new[]/delete[] pair on every /api/trips request and
	// every Sync::loop() check for the life of the session, which risked
	// heap fragmentation/allocation failure over a long-running session on
	// this device's tight free heap. Safe as `static` here specifically
	// because SdLock above holds _sdMutex for this function's entire scope,
	// so no two callers (main loop vs. the async web task) are ever inside
	// listTrips() at the same time to race on these.
	constexpr int SCAN_CAP = 200;
	static uint32_t ids[SCAN_CAP];
	static String names[SCAN_CAP];
	int scanCount = 0;

	File entry = dir.openNextFile();
	while (entry && scanCount < SCAN_CAP) {
		if (!entry.isDirectory()) {
			String name = String(entry.name());
			int slash = name.lastIndexOf('/');
			if (slash >= 0) name = name.substring(slash + 1);
			if (name != _currentTripFilename && name.startsWith("trip") && name.endsWith(".csv")) {
				ids[scanCount] = idFromFilename(name, "trip");
				names[scanCount] = name;
				scanCount++;
			}
		}
		entry.close();
		entry = dir.openNextFile();
	}
	dir.close();

	// Simple insertion sort descending by id (scanCount is small -- a
	// personal device's trip count, not a bulk dataset).
	for (int i = 1; i < scanCount; i++) {
		uint32_t idKey = ids[i];
		String nameKey = names[i];
		int j = i - 1;
		while (j >= 0 && ids[j] < idKey) {
			ids[j + 1] = ids[j];
			names[j + 1] = names[j];
			j--;
		}
		ids[j + 1] = idKey;
		names[j + 1] = nameKey;
	}

	int n = min(scanCount, maxCount);
	for (int i = 0; i < n; i++) {
		TripSummary &s = out[i];
		s.filename = names[i];
		String path = String(TRIPS_DIR) + "/" + names[i];
		File f = SD.open(path, FILE_READ);
		if (!f) continue;

		String header = f.readStringUntil('\n');
		if (header.startsWith("# id=")) {
			s.name = findField(header, "name");
			s.startedMs = findUInt(header, "started");
			s.startedEpoch = findUInt(header, "startedEpoch"); // absent in pre-v1.3 files -> 0
		}

		// A trip file can also carry later "# name=..." rename lines (see
		// renameTrip()); the last one wins. Cheap to catch while scanning the
		// tail for the summary footer below.
		size_t fsize = f.size();
		size_t tailLen = min((size_t)400, fsize);
		f.seek(fsize - tailLen);
		String tail;
		tail.reserve(tailLen + 1);
		while (f.available()) tail += (char)f.read();

		int renameIdx = tail.lastIndexOf("# name=");
		if (renameIdx >= 0) {
			String renameLine = tail.substring(renameIdx + 2); // past "# "
			int nl = renameLine.indexOf('\n');
			if (nl >= 0) renameLine = renameLine.substring(0, nl);
			String renamed = findField(renameLine, "name");
			if (renamed.length() > 0) s.name = renamed;
		}

		int endedIdx = tail.lastIndexOf("# ended=");
		if (endedIdx >= 0) {
			String footer = tail.substring(endedIdx);
			int nl = footer.indexOf('\n');
			if (nl >= 0) footer = footer.substring(0, nl);
			s.distanceM = findFloat(footer, "distanceM");
			s.durationMs = findUInt(footer, "durationMs");
			s.maxSpeedKmh = findFloat(footer, "maxSpeedKmh");
			s.ended = true;
		} else {
			s.ended = false;
		}
		f.close();
	}

	return n;
}

File Storage::openTripForRead(const String &filename) {
	if (!_ready || filename == _currentTripFilename) return File();
	SdLock lock(_sdMutex);
	return SD.open(String(TRIPS_DIR) + "/" + filename, FILE_READ);
}

size_t Storage::readTripChunk(File &f, uint8_t *buf, size_t len) {
	if (!f) return 0;
	SdLock lock(_sdMutex);
	return f.read(buf, len);
}

void Storage::closeTripRead(File &f) {
	if (!f) return;
	SdLock lock(_sdMutex);
	f.close();
}

static constexpr const char *UPLOADED_INDEX_PATH = "/trips/.uploaded";

bool Storage::isTripUploaded(const String &filename) {
	if (!_ready) return false;
	SdLock lock(_sdMutex);

	File f = SD.open(UPLOADED_INDEX_PATH, FILE_READ);
	if (!f) return false;
	bool found = false;
	while (f.available()) {
		String line = f.readStringUntil('\n');
		line.trim();
		if (line == filename) {
			found = true;
			break;
		}
	}
	f.close();
	return found;
}

void Storage::markTripUploaded(const String &filename) {
	if (!_ready) return;
	SdLock lock(_sdMutex);

	File f = SD.open(UPLOADED_INDEX_PATH, FILE_APPEND);
	if (!f) return;
	f.println(filename);
	f.flush();
	f.close();
}

bool Storage::saveRun(uint32_t id, const String &name, const String &dateStamp, const String &csv) {
	if (!_ready) return false;
	SdLock lock(_sdMutex);

	String path = String(RUNS_DIR) + "/" + buildRecordingFilename("run", id, dateStamp, name);
	File f = SD.open(path, FILE_WRITE);
	if (!f) return false;
	f.print(csv);
	f.flush();
	f.close();
	return true;
}

int Storage::listRuns(RunSummary *out, int maxCount) {
	if (!_ready || maxCount <= 0) return 0;
	SdLock lock(_sdMutex);

	File dir = SD.open(RUNS_DIR);
	if (!dir) return 0;

	constexpr int SCAN_CAP = 200; // see listTrips()'s comment -- same reasoning
	static uint32_t ids[SCAN_CAP];
	static String names[SCAN_CAP];
	int scanCount = 0;

	File entry = dir.openNextFile();
	while (entry && scanCount < SCAN_CAP) {
		if (!entry.isDirectory()) {
			String name = String(entry.name());
			int slash = name.lastIndexOf('/');
			if (slash >= 0) name = name.substring(slash + 1);
			if (name.startsWith("run") && name.endsWith(".csv")) {
				ids[scanCount] = idFromFilename(name, "run");
				names[scanCount] = name;
				scanCount++;
			}
		}
		entry.close();
		entry = dir.openNextFile();
	}
	dir.close();

	for (int i = 1; i < scanCount; i++) {
		uint32_t idKey = ids[i];
		String nameKey = names[i];
		int j = i - 1;
		while (j >= 0 && ids[j] < idKey) {
			ids[j + 1] = ids[j];
			names[j + 1] = names[j];
			j--;
		}
		ids[j + 1] = idKey;
		names[j + 1] = nameKey;
	}

	int n = min(scanCount, maxCount);
	for (int i = 0; i < n; i++) {
		RunSummary &s = out[i];
		s.filename = names[i];
		String path = String(RUNS_DIR) + "/" + names[i];
		File f = SD.open(path, FILE_READ);
		if (!f) continue;

		String header = f.readStringUntil('\n');
		if (header.startsWith("# id=")) {
			s.name = findField(header, "name");                 // absent in pre-v1.3 run files
			s.startedEpoch = findUInt(header, "startedEpoch");   // ditto
			s.startedMs = findUInt(header, "started");
			s.elapsedMs = findUInt(header, "elapsedMs");
			s.zeroSixtyMs = findUInt(header, "zeroSixtyMs");
			s.quarterMileMs = findUInt(header, "quarterMileMs");
			s.quarterMileTrapKmh = findFloat(header, "quarterMileTrapKmh");
			s.rollingSplitMs = findUInt(header, "rollingSplitMs");
			s.maxSpeedKmh = findFloat(header, "maxSpeedKmh");
			s.distanceM = findFloat(header, "distanceM");
		}
		f.close();
	}

	return n;
}

File Storage::openRunForRead(const String &filename) {
	if (!_ready) return File();
	SdLock lock(_sdMutex);
	return SD.open(String(RUNS_DIR) + "/" + filename, FILE_READ);
}

// The filename here arrives straight from an HTTP query parameter, so it
// gets checked before being concatenated into a path: plain basename only
// (no '/' or '\', no ".."), correct prefix, correct extension. Without this
// a crafted request could delete arbitrary files on the card.
static bool isSafeRecordingName(const String &filename, const char *prefix) {
	if (filename.length() == 0 || filename.length() > 64) return false;
	if (filename.indexOf('/') >= 0 || filename.indexOf('\\') >= 0) return false;
	if (filename.indexOf("..") >= 0) return false;
	if (!filename.startsWith(prefix) || !filename.endsWith(".csv")) return false;
	return true;
}

bool Storage::deleteTrip(const String &filename) {
	if (!_ready) return false;
	if (!isSafeRecordingName(filename, "trip")) return false;

	SdLock lock(_sdMutex);
	// The _currentTripFilename comparison MUST be inside the lock: every
	// writer of that field (beginTrip/endTrip/renameTrip) mutates it while
	// holding this same mutex, and it's an Arduino String, so comparing it
	// from the web task while the main loop reassigns it is both a
	// use-after-free risk and a way for the guard to spuriously read false
	// -- which would then delete the file TripMode still has open for
	// writing, mid-recording.
	if (filename == _currentTripFilename) return false;
	return SD.remove(String(TRIPS_DIR) + "/" + filename);
}

bool Storage::deleteRun(const String &filename) {
	if (!_ready) return false;
	if (!isSafeRecordingName(filename, "run")) return false;

	SdLock lock(_sdMutex);
	return SD.remove(String(RUNS_DIR) + "/" + filename);
}
