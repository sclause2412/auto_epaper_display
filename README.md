# Smart E-Paper Picture Frame

Project foundation for a battery-powered picture frame using a Waveshare
7.5-inch E-Paper display (B, presumably V2), a Waveshare e-Paper ESP32
Driver Board, ESP32 firmware, and a PHP backend.

## Project goal

The server prepares an image for the display. The ESP32 wakes
periodically, checks an image version/hash, and downloads the image only
when it has changed. Image data is transferred in small chunks directly
into the display controller's RAM. The firmware must **never hold a
complete image buffer** in ESP32 RAM.

## Directory structure

``` text
E-Paper-Frame/
├── README.md
├── docs/
│   └── EPD2-SPECIFICATION.md
├── backend/
│   └── AI-INSTRUCTIONS.md
└── firmware/
    └── AI-INSTRUCTIONS.md
```

Also place an unchanged copy of the original Waveshare example code in
`firmware/waveshare-original/`. This is the authoritative reference for
initialization, controller commands, RAM write sequence, bit order,
polarity, and refresh procedure.

## Working with two separate AI development projects

There are two separate work areas:

1.  **Backend AI:** implements PHP image processing and HTTP endpoints.
2.  **Firmware AI:** implements ESP32 firmware, Wi-Fi provisioning,
    streaming, and power management.

Both AIs must read and follow `docs/EPD2-SPECIFICATION.md` as the shared
interface definition. At the start of every AI session, instruct it to
read its local `AI-INSTRUCTIONS.md` and the shared specification.

**Important:** Neither AI may independently change the EPD2 format or
HTTP interface. Proposed changes must first be discussed and
incorporated into the shared specification.

Both display API endpoints require the configured shared key in the
`X-API-Key` HTTP header. Use HTTPS for deployment, keep production keys
out of public source control, and never put keys in URLs or logs.

## Hardware and driver notes

-   Display: Waveshare 7.5inch E-Paper (B), presumably V2, 800 × 480
    pixels.
-   Controller board: Waveshare e-Paper ESP32 Driver Board.
-   Development environment: Arduino IDE or PlatformIO, C++.
-   Backend: PHP.

The exact display revision and controller must be confirmed using the
physical hardware markings, board/display revision, and original driver.
**Do not guess controller commands, RAM addresses, bit order, data
polarity, or refresh sequence.** The firmware AI must trace these
details through the original Waveshare code and document its findings.

## High-level operating sequence

-   If Wi-Fi credentials are saved: attempt to connect for up to 5
    minutes.
-   If no credentials are saved: enter setup mode immediately.
-   If saved credentials cannot connect within 5 minutes: enter setup
    mode; retain the previous credentials initially.
-   Setup mode: dedicated WPA2 access point and captive portal, active
    for 10 minutes.
-   After new Wi-Fi credentials are submitted and a connection succeeds:
    save the credentials and continue normal operation.
-   If the 10-minute setup period expires without successful
    configuration: display the specified message and enter deep sleep.
    Do not automatically restart.
-   During normal operation: check the hash, stream the image if it
    changed, refresh the display, and then sleep for one hour.
-   If the hash is unchanged: do not transfer the image and do not
    perform an unnecessary refresh.

Full requirements are in `firmware/AI-INSTRUCTIONS.md`.

## Non-negotiable memory requirement

The firmware must not allocate a complete 96,000-byte image buffer. Use
a small, fixed transfer buffer (for example, 512 bytes) and write
received data directly into the display controller's RAM. The display
controller has its own image memory; the ESP32 acts only as a streaming
source.

Note: the physical e-paper panel typically retains its visible image
without power. This does **not** mean that the display controller's RAM
survives an ESP32 deep-sleep cycle. After waking, verify the controller
state and reinitialize it if necessary.

## Definition of done

-   Backend and firmware use exactly the same EPD2 specification.
-   The server requires API-key authentication and returns metadata
    whose SHA-256 corresponds to the EPD2 image body.
-   Firmware streams image data without a full framebuffer.
-   An unchanged hash causes no image transfer and no unnecessary
    refresh.
-   Wi-Fi provisioning and timeouts match the firmware requirements.
-   Errors never cause an incomplete image to be recorded as
    successfully displayed.
-   Build, test, and commissioning steps are documented.
