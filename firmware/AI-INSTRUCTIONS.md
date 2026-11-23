# Instructions for the Firmware AI (ESP32 / C++)

You are developing only the ESP32 firmware for the battery-powered
e-paper picture frame.

## Read these files first

Before making any changes, read in full:

1.  `../docs/EPD2-SPECIFICATION.md`
2.  this file
3.  `../docs/HARDWARE.md`
4.  the existing firmware and board configuration

The EPD2 specification is the binding interface to the PHP backend. Do
not change it independently. If a change is necessary, propose it first
and explain the impact on both backend and firmware.

## Hardware and development environment

-   Waveshare 7.5inch E-Paper (B) V2, 800 × 480,
    black/white/red.
-   Waveshare e-Paper ESP32 Driver Board.
-   C++ using Arduino IDE or PlatformIO, matching the existing codebase.

Confirm the exact display revision, controller, and board pinout using
the available hardware documentation and the firmware hardware record. Do not
invent pins or controller commands.

## Most important memory requirement: no full framebuffer

The firmware must never allocate a complete variable-size image buffer or
complete color planes in ESP32 RAM.

-   Use a small, fixed transfer buffer; 512 bytes is recommended.
-   Read HTTP data in chunks.
-   Write each received chunk immediately into the display controller's
    RAM.
-   The first half of the stream contains the 48,000-byte black plane.
-   The second half contains the 48,000-byte red plane.
-   If a received chunk crosses the plane boundary, split it correctly.
-   If the controller API only accepts a complete framebuffer, implement
    a suitable low-level streaming interface based on the documented
    hardware write sequence.
-   Avoid hidden full-image copies, large temporary arrays, or dynamic
    allocations proportional to image size.

The display controller has its own image memory. Use it to transfer
image data directly. The physical e-paper panel typically retains its
visible image without power; however, this does not mean the controller
RAM survives ESP32 deep sleep or loss of power.

## Controller and driver inspection is mandatory

Use `../docs/HARDWARE.md` as the hardware verification record and keep it current
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
    wait for the Wi-Fi connection timeout.
3.  If credentials exist: each retrieval attempt first tries to connect for
    at most 30 seconds, then makes one image request if connected.
4.  Any connection or image-retrieval failure counts as one failed attempt.
    Retry after one minute for the first four failures. After the fifth,
    display the common error and retry after one hour, then start a new
    five-attempt cycle. Do not refresh the display while that same error is
    already shown.
5.  If a connection attempt fails, retain the previously saved
    credentials initially. Replace them only after new credentials have
    been configured and a connection using them succeeds.
6.  On every startup except a scheduled deep-sleep wake, show the saved
    SSID and image-server URL for one minute before starting the first image
    request. Never display the API key; show a notice that it is not shown.
    During this window, holding BOOT for five seconds enters setup mode
    without erasing settings.

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

Holding BOOT for five seconds during the startup-settings window enters setup
mode without erasing saved settings. BOOT is confirmed to be GPIO0 on this
board. Never hold BOOT while pressing EN or powering on, because GPIO0 is a
boot-strapping pin and can select ROM download mode. The GPIO2 LED blinks
rapidly while BOOT is held during the startup-status window; ignore BOOT
presses outside that window.

A factory reset is performed only from the configuration portal. Its explicit
reset action erases the complete Wi-Fi settings namespace, including Wi-Fi
credentials, image-server URL, and API key, then restarts into setup mode.
Never display or log the erased values. The portal must not return or prefill
the API key; an empty key field means to retain the saved key, while a
non-empty value replaces it.

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
constexpr uint32_t WIFI_TIMEOUT_SECONDS = 30;
constexpr uint32_t SETUP_TIMEOUT_SECONDS = 10 * 60;
constexpr uint32_t UPDATE_INTERVAL_SECONDS = 60 * 60;
```

The 30-second timeout applies only when attempting to connect with
credentials that are already saved. If no credentials are saved, enter
setup immediately. Each retrieval attempt gets at most 30 seconds to
connect to Wi-Fi and then makes one image request. Any failure counts
equally: retry after one minute for the first four failures; after the
fifth, show the common error, wait one hour, and start a new five-attempt
cycle.

## Normal operation and image updates

After successfully connecting to Wi-Fi:

1.  POST `/api/display/current` over HTTP.
    Include the configured API key as an `X-API-Key` header and send content
    ID, width, height, and the ordered pipe-separated color string in the
    POST body. Accept any non-negative content ID representable as a 64-bit
    integer; do not limit it to the example IDs 0 and 1. Width, height, color count,
    and color order are fixed by the selected hardware profile and must not be
    exposed as configuration settings.
2.  Validate the JSON: `width`, `height`, `format=EPD2`, `response_id`, and
    a size derived from the header and declared color count.
3.  Compare the server hash with the volatile RTC-RAM cache of the last
    successfully displayed image. Never store an image hash in NVS or other
    persistent storage. Clear the cache after resets other than scheduled
    deep-sleep wakeups so a restarted controller fetches the image again.
4.  If the hash is unchanged: do not download the image and do not
    perform an unnecessary display refresh.
5.  If the hash is new:
    -   POST the binary image endpoint with `response_id`;
    -   optionally use `If-None-Match`/ETag;
    -   validate the response status;
    -   validate and consume the five-byte header;
    -   stream exactly the declared variable-size body;
    -   write each logical color plane directly to controller RAM in the
        declared order;
    -   trigger the controller refresh according to the verified hardware record;
    -   wait for BUSY completion with a timeout;
    -   update the volatile RTC-RAM hash cache only after the complete
        transfer and successful refresh.
6.  Disable Wi-Fi and any peripherals that are no longer needed.
7.  Put the ESP32 into deep sleep for one hour.

If the hash is unchanged and no retry error is displayed, the device can
go directly to deep sleep after the hash check. No full image transfer is
needed.

Every retrieval attempt first connects to Wi-Fi for up to 30 seconds and,
if connected, makes one metadata/image request. A connection failure,
HTTP error (including `403` and `404`), invalid metadata or image, transfer
failure, or display refresh failure all count as one failed attempt. Retry
after one minute for the first four failures. After the fifth, display the
common error and sleep for one hour, regardless of the configured update
interval, then start a new five-attempt cycle. Show the next retry time in
UTC when system time is synchronized; otherwise show that time was not
loaded. Do not refresh the display if the common error is already shown.
On invalid metadata, incorrect length, interrupted stream, or refresh
failure, do not update the RTC-RAM hash cache or report success.

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
-   Correctly handle chunked transfer encoding if the server uses it.
-   Prefer a known Content-Length equal to the variable size declared by the
    current metadata.
-   Abort if fewer or more than the calculated EPD2 body size is received.
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
8.  Unchanged hash causes no transfer or refresh unless a retry error
    message replaced the image; in that case, restore the image even when
    its hash is unchanged.
9.  Missing saved Wi-Fi credentials lead directly to setup mode.
10. Each attempt gets at most 30 seconds to connect, then makes one image
    request if connected. All failure types count equally: retry after one
    minute for the first four, then display the common error and retry after
    one hour. Do not refresh an already displayed error.
11. AP SSID follows the `DISPLAY_####` format.
12. AP password contains exactly eight digits.
13. Setup portal is reachable at `192.168.4.1`.
14. Setup ends after ten minutes; the exact message is displayed and
    deep sleep is entered.
15. No automatic restart occurs after setup timeout.
16. Holding BOOT for five seconds during the startup screen enters setup
    without erasing settings; the portal reset action clears them.
17. A `403 Forbidden`, like any other retrieval failure, counts toward the
    same five-attempt limit and does not update the cached hash.
18. Non-deep-sleep startup displays SSID and URL, but never the API key, for
    one minute before the first image request.
19. Wi-Fi failures, HTTP errors, invalid data, transfer failures, and
    display refresh failures all count as retrieval failures: retry after
    one minute for the first four, then display one common error and wait
    one hour, regardless of the configured update interval. Start a new
    five-attempt cycle after that hour.

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
