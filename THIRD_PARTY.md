# Third-party components

0sixty itself is MIT licensed (see `LICENSE`). It builds against the
libraries below, which carry their own licences. Versions are whatever
`platformio.ini` resolves; the licences here were read from the copies
PlatformIO actually installed, not from memory.

| Library | Used for | Licence |
|---|---|---|
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) | Board support: display, keyboard, power | **Unconfirmed** (no LICENSE file shipped, GitHub reports no detected licence, and the project's own README states MIT only for its M5GFX/M5Unified dependencies -- not for itself) |
| [M5Unified](https://github.com/m5stack/M5Unified) | M5Stack hardware abstraction, IMU | MIT |
| [M5GFX](https://github.com/m5stack/M5GFX) | Display driver and canvas | MIT |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | NMEA parsing | **See note below** |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Dashboard JSON payloads | MIT |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | Web dashboard, WebSocket, chunked exports | **LGPL-3.0** |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | TCP layer beneath the web server | **LGPL-3.0** |
| [QRCode](https://github.com/ricmoo/QRCode) | WiFi-join QR rendering | MIT |

Plus the Espressif Arduino core / ESP-IDF, under their respective licences.

**IRremote** also appears under PlatformIO's installed libraries because
M5Cardputer's own `library.json` lists it as a dependency, but nothing in
this project's source, and nothing in M5Cardputer's own core source (only
one of its example sketches), includes it -- the build's dependency graph
never pulls it in, and no `IRremote` object files are produced by the
actual firmware build. It is a leftover/transitive install, not something
this firmware ships, so it is intentionally left out of the table above.

## Notes on uncertain and copyleft licences

**TinyGPSPlus** ships no LICENSE file in the PlatformIO package, and GitHub's
own licence detector reports none for the upstream repository either. The
library's source files themselves (`TinyGPS++.h`, `TinyGPS++.cpp`,
`TinyGPSPlus.h`) each carry a header stating it is free software under "the
GNU Lesser General Public License ... version 2.1 of the License, or (at your
option) any later version" -- so LGPL-2.1-or-later is what the code itself
claims, even though there is no standalone LICENSE file to point to.

**M5Cardputer** is in the same position but with less to go on: no LICENSE
file in the package, no licence field on the GitHub repository, and no
licence statement for itself anywhere in its own README (which lists MIT
only for its M5GFX and M5Unified dependencies). Treat its licence as
unconfirmed rather than assuming MIT because its M5Stack siblings are MIT.

**ESPAsyncWebServer and AsyncTCP are LGPL-3.0.** Statically linking LGPL
code into a firmware image — which is what an ESP32 build does — normally
carries an obligation to let recipients relink the work against a modified
version of the library.

For this project that obligation is met in the ordinary way open hardware
projects meet it: the complete source is published here, the exact
dependency versions are pinned in `platformio.ini`, and `README.md`
documents the full build so anyone can rebuild the firmware with their own
version of any library.

If you intend to **redistribute the compiled binary commercially**, or to
build on this under a more restrictive licence, review these obligations
properly rather than relying on this summary — it is a description of the
situation, not legal advice.
