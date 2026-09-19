# Third-party components

0sixty itself is MIT licensed (see `LICENSE`). It builds against the
libraries below, which carry their own licences. Versions are whatever
`platformio.ini` resolves; the licences here were read from the copies
PlatformIO actually installed, not from memory.

| Library | Used for | Licence |
|---|---|---|
| [M5Cardputer](https://github.com/m5stack/M5Cardputer) | Board support: display, keyboard, power | MIT (no LICENSE file shipped in the package; MIT per the project) |
| [M5Unified](https://github.com/m5stack/M5Unified) | M5Stack hardware abstraction, IMU | MIT |
| [M5GFX](https://github.com/m5stack/M5GFX) | Display driver and canvas | MIT |
| [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus) | NMEA parsing | **See note below** |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Dashboard JSON payloads | MIT |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | Web dashboard, WebSocket, chunked exports | **LGPL-3.0** |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | TCP layer beneath the web server | **LGPL-3.0** |
| [QRCode](https://github.com/ricmoo/QRCode) | WiFi-join QR rendering | MIT |

Plus the Espressif Arduino core / ESP-IDF, under their respective licences.

## Notes on the copyleft dependencies

**TinyGPSPlus** ships no LICENSE file in the PlatformIO package. It is
commonly described as LGPL-2.1. That has not been verified against the
upstream repository, so treat it as unconfirmed rather than settled.

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
