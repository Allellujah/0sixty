#pragma once

#include <Arduino.h>
#include "gps.h"

// v1.3.0: wall-clock time for trip/run timestamps and filenames.
//
// There is no RTC on this hardware -- the GNSS stream is the only clock
// source. Until the first fix carrying a valid UTC date arrives, the device
// genuinely doesn't know what day it is, and everything here reports "not
// set yet" rather than inventing a plausible-looking wrong timestamp (which
// would end up baked into filenames).
//
// Once that first fix lands, latch() pushes it into the ESP32's own system
// clock, after which time keeps advancing from the internal timer even if
// the fix is lost -- so a trip that starts in an underground car park still
// gets a correct timestamp, as long as the device saw satellites at some
// point since boot.
//
// Local time (Belgium) is handled by the C library's POSIX timezone
// support rather than hand-rolled DST arithmetic: the rule string below
// encodes CET/CEST and the EU's last-Sunday-of-March/October switchover, so
// localtime() does the right thing year-round with no seasonal maintenance.
namespace TimeUtil {

// Europe/Brussels: CET (UTC+1) standard, CEST (UTC+2) summer, switching on
// the last Sunday of March and October at 02:00/03:00 local.
constexpr const char *TZ_BRUSSELS = "CET-1CEST,M3.5.0,M10.5.0/3";

void begin(); // installs the timezone rule; call once at boot

// Feed each GPS fix in. The first one carrying a valid date sets the system
// clock; later calls are cheap no-ops. Returns true the moment the clock
// first becomes valid, so callers can react to it once.
bool latch(const GpsFix &fix);

bool isSet(); // has the clock been set from GPS yet this session?

// "20260918-1432" -- for filenames. Empty string if the clock isn't set.
String stampForFilename();

// "2026-09-18 14:32" -- for display. Empty string if the clock isn't set.
String stampForDisplay();

// Seconds since the Unix epoch, or 0 if the clock isn't set. Stored in
// trip/run metadata so the dashboard can format it however it likes.
uint32_t epochNow();

// v1.4.0: format an arbitrary past timestamp (not just "now"), for labelling
// the individual legs of an exported trip. Both return "" for epoch == 0,
// i.e. a recording made before the clock was ever set.
String formatEpoch(uint32_t epoch, const char *fmt); // strftime, local time
String hhmm(uint32_t epoch);                          // "08:47", local time
String iso8601Utc(uint32_t epoch);                    // "2026-09-18T06:47:12Z", for KML <TimeSpan>

} // namespace TimeUtil
