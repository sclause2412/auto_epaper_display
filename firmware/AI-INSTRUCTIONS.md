# Instructions for the Firmware AI (ESP32 / C++)

You are developing only the ESP32 firmware for the battery-powered
e-paper picture frame.

## Read these files first

Before making any changes, read in full:

1.  `../docs/EPD2-SPECIFICATION.md`
2.  this file
3.  `docs/HARDWARE.md`
4.  the existing firmware and board configuration

The EPD2 specification is the binding interface to the PHP backend. Do
not change it independently. If a change is necessary, propose it first
and explain the impact on both backend and firmware.

## Hardware and development environment

-   Waveshare 7.5inch E-Paper (B), presumably V2, 800 × 480,
    black/white/red.
-   Waveshare e-Paper ESP32 Driver Board.
-   C++ using Arduino IDE or PlatformIO, matching the existing codebase.

Confirm the exact display revision, controller, and board pinout using
the available hardware documentation and the firmware hardware record. Do not
invent pins or controller commands.

## Most important memory requirement: no full framebuffer

The firmware must never allocate a complete 96,000-byte image buffer or
two complete 48,000-byte planes in ESP32 RAM.

-   Use a small, fixed transfer buffer; 512 bytes is recommended.
-   Read HTTP data in chunks.
-   Write each received chunk immediately into the display controller's
    RAM.
-   The first half of the stream contains the 48,000-byte black plane.
-   The second half contains the 48,000-byte red plane.
-   If a received chunk crosses the plane boundary, split it correctly.
-   If the controller API only accepts a complete framebuffer, implement
    a suitable low-level streaming interface based on the original
    driver.
-   Avoid hidden full-image copies, large temporary arrays, or dynamic
    allocations proportional to image size.

The display controller has its own image memory. Use it to transfer
image data directly. The physical e-paper panel typically retains its
visible image without power; however, this does not mean the controller
RAM survives ESP32 deep sleep or loss of power.

## Controller and driver inspection is mandatory

Use `docs/HARDWARE.md` as the hardware verification record and keep it current
with the firmware implementation:

1.  exact controller and display revision;
2.  initialization sequence;
3.  controller RAM addresses and write commands;
4.  black/red plane order;
5.  bit order and bit polarity;
6.  address/window configuration and row/scan direction;
7.  refresh sequence and BUSY-pin behavior;
8.  sleep/power-off behavior;
9.  whether controller RAM survives ESP32 deep sleep or loss of power.

Do not use guessed commands. Values such as `0x10`, `0x13`, or `0x12` must
only be used when they are documented for this exact hardware.

The firmware must convert the logical EPD2 format in
`../docs/EPD2-SPECIFICATION.md` into the representation expected by the
controller. If bit order or polarity differs, transform data
block-by-block using constant memory.

## Wi-Fi credentials and provisioning

Wi-Fi credentials must not be hardcoded in source code.

### Startup behavior

1.  Check whether saved Wi-Fi credentials exist.
2.  If no credentials are saved: enter setup mode immediately. Do not
    wait five minutes.
3.  If credentials exist: attempt to connect for a maximum of 5 minutes.
4.  If a connection cannot be established within 5 minutes: enter setup
    mode.
5.  If a connection attempt fails, retain the previously saved
    credentials initially. Replace them only after new credentials have
    been configured and a connection using them succeeds.
6.  On every startup except a scheduled deep-sleep wake, show the saved
    SSID, image-server URL, and an API key masked after its first four
    characters for one minute before starting the first image request.

### Setup access point

-   SSID: `DISPLAY_` followed by four random digits.
-   Password: exactly eight random digits, the minimum WPA2-PSK passphrase
    length.
-   Generate random values using the ESP32 hardware RNG, `esp_random()`.
-   Use WPA2-PSK.
-   Display the SSID and setup password on the e-paper.
-   Never show saved home Wi-Fi credentials on the display, in an HTTP
    response, or in logs.

### Captive portal

In setup mode, start:

-   the Wi-Fi access point;
-   a DNS server for captive-portal redirection;
-   an HTTP web server at `192.168.4.1`.

The configuration page must:

-   scan for and list available Wi-Fi networks;
-   allow the user to select an SSID;
-   provide Wi-Fi password, HTTP/HTTPS image-server base URL, and API-key fields;
-   receive credentials through HTTP POST;
-   never transmit passwords in URL query parameters;
-   provide a test action that checks Wi-Fi and authenticated EPD2 metadata;
-   expose the save action only after that test succeeds;
-   persist the tested settings only after an explicit save action;
-   never write passwords to logs;
-   render password fields empty on every page load;
-   never prefill or log the API key;
-   validate input server-side;
-   use the ESP-IDF root CA bundle to validate HTTPS server certificates;
-   allow HTTP explicitly and warn that it sends the API key without encryption;
-   do not follow redirects to another endpoint or transport.

Store Wi-Fi data persistently, for example using Preferences/NVS. Do not
expose passwords through output or debug logging. Be aware of the
capabilities and limitations of ESP32 NVS storage.

### Setup timeout

-   Setup mode runs for exactly 10 minutes from the time it starts.
-   Show the SSID and password plus a static notice that the hotspot closes
    after 10 minutes. Do not display a changing countdown or refresh the
    display during setup.
-   If valid credentials are submitted and the connection succeeds: save
    the credentials, stop the AP/DNS/web server, and switch to normal
    operation.
-   If no successful configuration has completed after 10 minutes: stop
    the AP, DNS, and web server; display this exact text:

`Please restart controller to enter Setup mode...`

Then put the display controller into an appropriate low-power state and
put the ESP32 into deep sleep. **Do not automatically restart, retry
setup, or reconnect Wi-Fi.** Only a manual power cycle or reset should
start the process again.

E-paper is not an LCD: the last displayed message remains visible while
the display electronics are subsequently placed in a low-power state.

## Reset Wi-Fi configuration

For a factory reset, press and release EN to restart the controller. After
the startup-settings screen appears, hold BOOT for five seconds. BOOT is
confirmed to be GPIO0 on this board. Never hold BOOT while pressing EN or
powering on, because GPIO0 is a boot-strapping pin and can select ROM
download mode. The GPIO2 LED blinks rapidly while BOOT is held during the
startup-status window, then stays off; ignore BOOT presses outside that
window.

On confirmation, erase the complete Wi-Fi settings namespace, including
Wi-Fi credentials, image-server URL, and API key, then enter setup mode.
Never display or log the erased values.

## Operating states and timeouts

Implement a non-blocking state machine using `millis()` or an
appropriate ESP32 time source. Avoid long blocking loops except where
the display driver must wait for BUSY; such waits must have a timeout.

Suggested states:

-   `WIFI_CONNECTING`
-   `WIFI_SETUP`
-   `NORMAL_OPERATION`
-   `SHUTDOWN`

Constants:

``` cpp
constexpr uint32_t WIFI_TIMEOUT_MS  = 5UL * 60UL * 1000UL;
constexpr uint32_t SETUP_TIMEOUT_MS = 10UL * 60UL * 1000UL;
constexpr uint64_t UPDATE_INTERVAL_US = 60ULL * 60ULL * 1000000ULL;
```

The five-minute timeout applies only when attempting to connect with
credentials that are already saved. If no credentials are saved, enter
setup immediately.

## Normal operation and image updates

After successfully connecting to Wi-Fi:

1.  Request `GET /api/display/current` over HTTP.
    Include the configured API key as an `X-API-Key` header. Provision the
    key through the setup portal and store it in NVS only after the
    connection and image-status tests succeed. Never put it in a URL, log
    it, or commit it to firmware source.
2.  Validate the JSON: `width=800`, `height=480`, `format=EPD2`,
    `size=96000`.
3.  Compare the server hash with the volatile RTC-RAM cache of the last
    successfully displayed image. Never store an image hash in NVS or other
    persistent storage. Clear the cache after resets other than scheduled
    deep-sleep wakeups so a restarted controller fetches the image again.
4.  If the hash is unchanged: do not download the image and do not
    perform an unnecessary display refresh.
5.  If the hash is new:
    -   request the binary image endpoint;
    -   optionally use `If-None-Match`/ETag;
    -   validate the response status;
    -   stream exactly 96,000 bytes;
    -   write the black and red planes directly to controller RAM in the
        specified order;
    -   trigger the controller refresh according to the verified hardware record;
    -   wait for BUSY completion with a timeout;
    -   update the volatile RTC-RAM hash cache only after the complete
        transfer and successful refresh.
6.  Disable Wi-Fi and any peripherals that are no longer needed.
7.  Put the ESP32 into deep sleep for one hour.

If the hash is unchanged, the device can go directly to deep sleep after
the hash check. No full image transfer is needed.

On an unreachable image server, HTTP 404, or server error, retry the
metadata/image request every minute, up to ten attempts. After the tenth
failure, display an error and continue the normal one-hour deep-sleep
cycle. A `403 Forbidden` is not retried minutely; display the API-key
error immediately and continue the hourly cycle. On invalid metadata,
incorrect length, interrupted stream, or refresh failure, do not update
the RTC-RAM hash cache or report success.

## Deep sleep and retaining the visible image

-   The visible e-paper image should remain during the sleep period.
-   Put the display controller into the appropriate low-power state
    before deep sleep, as specified by the verified hardware record.
-   Do not assume controller RAM remains valid after deep sleep.
-   On the next wake, reinitialize the controller if required by the
    driver.
-   If the hash is unchanged, no refresh should occur. If controller RAM
    is lost after wake, investigate whether a refresh is actually needed
    to preserve the physically retained image. Base the solution on
    observed behavior of the exact hardware.

## Network and HTTP robustness

-   Set connection and read timeouts.
-   Send the configured `X-API-Key` header with both display API
    requests. Use HTTPS and validate the server certificate for public
    deployments; do not silently downgrade to HTTP. Treat `403 Forbidden`
    as an authentication/configuration error; do not download, refresh,
    or update the volatile RTC-RAM hash cache.
-   Validate HTTP status codes and Content-Length where available.
-   Correctly handle chunked transfer encoding if the server uses it;
    preferably the server should provide a known Content-Length of
    96,000 bytes.
-   Abort if fewer or more than 96,000 image bytes are received.
-   Never mark partially received data as a valid image.
-   Do not log passwords, Wi-Fi PSKs, or other secrets.
-   Do not log or expose the API key. Never send it in URL query
    parameters. Use HTTPS with server-certificate validation for public
    deployments.
-   Avoid unnecessary heap allocations during image transfer.

## Power-saving requirements

The device is battery-powered:

-   Enable Wi-Fi only when needed.
-   Disable Wi-Fi after the hash check or image update.
-   Avoid unnecessary display refreshes.
-   Avoid continuous polling loops.
-   Use one hour of deep sleep between regular update checks.
-   Verify display and controller-board power behavior on the actual
    hardware.

## Tests and acceptance

Create build and test instructions and verify at least:

1.  Firmware compiles for the actual board/ESP32 target.
2.  No static or dynamic full-frame image buffer is allocated.
3.  Transfer buffer remains small and bounded.
4.  Black/red plane boundary is handled correctly.
5.  Premature HTTP termination is detected.
6.  Incorrect Content-Length is detected.
7.  The volatile RTC-RAM hash cache is updated only after a successful refresh.
8.  Unchanged hash causes no transfer and no refresh.
9.  Missing saved Wi-Fi credentials lead directly to setup mode.
10. Saved Wi-Fi credentials get at most five minutes to connect.
11. AP SSID follows the `DISPLAY_####` format.
12. AP password contains exactly eight digits.
13. Setup portal is reachable at `192.168.4.1`.
14. Setup ends after ten minutes; the exact message is displayed and
    deep sleep is entered.
15. No automatic restart occurs after setup timeout.
16. The EN-then-BOOT sequence clears Wi-Fi and API settings and enters
    setup mode without holding GPIO0 during reset.
17. A `403 Forbidden` from either image endpoint displays a clear API-key
    error on the e-paper and does not update the cached hash.
18. Non-deep-sleep startup displays SSID, URL, and the masked key for
    one minute before the first image request.
19. Unreachable server, 404, and server errors retry every minute ten
    times, then display an error and use the one-hour sleep interval.

Test display orientation, bit order, and color mapping with the test
images listed in the EPD2 specification on real hardware.

## Working rules

-   Inspect the hardware verification record and active firmware driver first.
-   Preserve working display initialization unless it is incorrect for
    the hardware.
-   Do not make assumptions about controller, pins, commands, or
    polarity.
-   Do not modify the PHP backend.
-   Do not change the EPD2 specification independently.
-   Never allocate full image buffers.
-   Never hardcode Wi-Fi credentials.
-   Never commit a production API key in public firmware source.
-   Run available builds and tests and report honestly when hardware
    tests could not be performed.
-   Document every hardware-dependent assumption and every remaining
    verification item.

## Final report

After implementation, list:

1.  files created and modified;
2.  confirmed display revision and controller (or clearly state if still
    unknown);
3.  how the verified display sequence is implemented for streaming;
4.  evidence that no full-frame buffer is used;
5.  Wi-Fi setup and reset behavior;
6.  deep-sleep and refresh behavior;
7.  builds/tests executed and their results;
8.  outstanding hardware tests and commissioning steps.
