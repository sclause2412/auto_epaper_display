# Instructions for the Firmware AI (ESP32 / C++)

You are developing only the ESP32 firmware for the battery-powered
e-paper picture frame.

## Read these files first

Before making any changes, read in full:

1.  `../docs/EPD2-SPECIFICATION.md`
2.  this file
3.  the unchanged original Waveshare code in `waveshare-original/`
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
the available hardware documentation and original Waveshare code. Do not
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

Determine and document from `waveshare-original/`:

1.  exact controller and display revision;
2.  initialization sequence;
3.  controller RAM addresses and write commands;
4.  black/red plane order;
5.  bit order and bit polarity;
6.  address/window configuration and row/scan direction;
7.  refresh sequence and BUSY-pin behavior;
8.  sleep/power-off behavior;
9.  whether controller RAM survives ESP32 deep sleep or loss of power.

Do not use guessed commands. Values such as `0x10`, `0x13`, or `0x12`
mentioned in earlier drafts are unconfirmed and must not be used unless
the original driver confirms them for this exact hardware.

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

### Setup access point

-   SSID: `DISPLAY_` followed by four random digits.
-   Password: exactly ten random digits.
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
-   provide a password field;
-   receive credentials through HTTP POST;
-   never transmit passwords in URL query parameters;
-   never write passwords to logs;
-   render password fields empty on every page load;
-   validate input server-side;
-   save the credentials and test the connection using them.

Store Wi-Fi data persistently, for example using Preferences/NVS. Do not
expose passwords through output or debug logging. Be aware of the
capabilities and limitations of ESP32 NVS storage.

### Setup timeout

-   Setup mode runs for exactly 10 minutes from the time it starts.
-   Show the remaining time on the e-paper, for example as a
    minutes/seconds countdown.
-   Refresh the display no more than once per minute to avoid
    unnecessary refreshes.
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

Implement a way to reset saved Wi-Fi credentials.

Preferred method: hold the BOOT button (often GPIO0) for at least 5
seconds while the device is running. First verify the actual board
revision and GPIO using the schematic/original code.

Important: GPIO0 is an ESP32 boot-strapping pin. Holding BOOT while
powering on can trigger the ROM download mode. Design and document reset
detection so an intentional long press during normal operation is
recognized without accidentally changing the boot mode. If the board
hardware does not allow this reliably, document a safe alternative reset
method.

When a Wi-Fi reset is confirmed: - erase saved Wi-Fi credentials; - do
not display or log credentials; - enter setup mode.

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
2.  Validate the JSON: `width=800`, `height=480`, `format=EPD2`,
    `size=96000`.
3.  Compare the server hash with the locally persisted hash of the last
    successfully displayed image.
4.  If the hash is unchanged: do not download the image and do not
    perform an unnecessary display refresh.
5.  If the hash is new:
    -   request the binary image endpoint;
    -   optionally use `If-None-Match`/ETag;
    -   validate the response status;
    -   stream exactly 96,000 bytes;
    -   write the black and red planes directly to controller RAM in the
        specified order;
    -   trigger the controller refresh according to the original driver;
    -   wait for BUSY completion with a timeout;
    -   persist the new hash only after the complete transfer and
        successful refresh.
6.  Disable Wi-Fi and any peripherals that are no longer needed.
7.  Put the ESP32 into deep sleep for one hour.

If the hash is unchanged, the device can go directly to deep sleep after
the hash check. No full image transfer is needed.

On network error, invalid metadata, incorrect length, interrupted
stream, or refresh failure: - do not update the hash; - do not report
success; - shut down Wi-Fi/peripherals cleanly; - follow the intended
low-power schedule rather than retrying indefinitely.

## Deep sleep and retaining the visible image

-   The visible e-paper image should remain during the sleep period.
-   Put the display controller into the appropriate low-power state
    before deep sleep, as specified by the original driver.
-   Do not assume controller RAM remains valid after deep sleep.
-   On the next wake, reinitialize the controller if required by the
    driver.
-   If the hash is unchanged, no refresh should occur. If controller RAM
    is lost after wake, investigate whether a refresh is actually needed
    to preserve the physically retained image. Base the solution on
    observed behavior of the exact hardware.

## Network and HTTP robustness

-   Set connection and read timeouts.
-   Validate HTTP status codes and Content-Length where available.
-   Correctly handle chunked transfer encoding if the server uses it;
    preferably the server should provide a known Content-Length of
    96,000 bytes.
-   Abort if fewer or more than 96,000 image bytes are received.
-   Never mark partially received data as a valid image.
-   Do not log passwords, Wi-Fi PSKs, or other secrets.
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
7.  Hash is stored only after a successful refresh.
8.  Unchanged hash causes no transfer and no refresh.
9.  Missing saved Wi-Fi credentials lead directly to setup mode.
10. Saved Wi-Fi credentials get at most five minutes to connect.
11. AP SSID follows the `DISPLAY_####` format.
12. AP password contains exactly ten digits.
13. Setup portal is reachable at `192.168.4.1`.
14. Setup ends after ten minutes; the exact message is displayed and
    deep sleep is entered.
15. No automatic restart occurs after setup timeout.
16. Wi-Fi reset works safely without accidentally triggering ESP32 ROM
    download mode.

Test display orientation, bit order, and color mapping with the test
images listed in the EPD2 specification on real hardware.

## Working rules

-   Inspect the existing firmware and original Waveshare driver first.
-   Preserve working display initialization unless it is incorrect for
    the hardware.
-   Do not make assumptions about controller, pins, commands, or
    polarity.
-   Do not modify the PHP backend.
-   Do not change the EPD2 specification independently.
-   Never allocate full image buffers.
-   Never hardcode Wi-Fi credentials.
-   Run available builds and tests and report honestly when hardware
    tests could not be performed.
-   Document every hardware-dependent assumption and every remaining
    verification item.

## Final report

After implementation, list:

1.  files created and modified;
2.  confirmed display revision and controller (or clearly state if still
    unknown);
3.  how the original driver was adapted for streaming;
4.  evidence that no full-frame buffer is used;
5.  Wi-Fi setup and reset behavior;
6.  deep-sleep and refresh behavior;
7.  builds/tests executed and their results;
8.  outstanding hardware tests and commissioning steps.
