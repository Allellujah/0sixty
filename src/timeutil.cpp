#include "timeutil.h"
#include <time.h>
#include <sys/time.h>

namespace TimeUtil {

static bool _clockSet = false;

void begin() {
	setenv("TZ", TZ_BRUSSELS, 1);
	tzset();
}

// Days since 1970-01-01 for a civil (proleptic Gregorian) date. Howard
// Hinnant's days_from_civil -- exact integer arithmetic, valid well beyond
// any date this device will see.
//
// Used instead of timegm(), which this toolchain's newlib doesn't provide,
// and instead of mktime(), which would interpret the struct as *local* time
// and so double-count the timezone offset that localtime() applies again
// when formatting.
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
	y -= m <= 2;
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);                      // [0, 399]
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
	return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool latch(const GpsFix &fix) {
	if (_clockSet) return false;
	if (!fix.dateTimeValid) return false;

	// GPS reports UTC, so this converts straight to a Unix epoch with no
	// timezone applied; the TZ rule installed by begin() is applied later,
	// only at formatting time via localtime_r().
	int64_t days = daysFromCivil(fix.year, fix.month, fix.day);
	time_t epoch = (time_t)(days * 86400LL + fix.hour * 3600L + fix.minute * 60L + fix.second);
	if (epoch <= 0) return false;

	struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
	settimeofday(&tv, nullptr);
	_clockSet = true;
	return true;
}

bool isSet() {
	return _clockSet;
}

uint32_t epochNow() {
	if (!_clockSet) return 0;
	return (uint32_t)time(nullptr);
}

static String format(const char *fmt) {
	if (!_clockSet) return String("");
	time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local); // applies the TZ rule installed by begin()
	char buf[32];
	strftime(buf, sizeof(buf), fmt, &local);
	return String(buf);
}

String formatEpoch(uint32_t epoch, const char *fmt) {
	if (epoch == 0) return String("");
	time_t t = (time_t)epoch;
	struct tm local;
	localtime_r(&t, &local); // applies the TZ rule installed by begin()
	char buf[48];
	strftime(buf, sizeof(buf), fmt, &local);
	return String(buf);
}

String hhmm(uint32_t epoch) {
	return formatEpoch(epoch, "%H:%M");
}

String iso8601Utc(uint32_t epoch) {
	if (epoch == 0) return String("");
	time_t t = (time_t)epoch;
	struct tm utc;
	gmtime_r(&t, &utc); // KML timestamps are unambiguous in UTC
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
	return String(buf);
}

String stampForFilename() {
	return format("%Y%m%d-%H%M");
}

String stampForDisplay() {
	return format("%Y-%m-%d %H:%M");
}

} // namespace TimeUtil
