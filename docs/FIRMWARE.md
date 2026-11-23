# ESP32 firmware

The firmware project is in [`../firmware/`](../firmware/) and targets the
Waveshare e-Paper ESP32 Driver Board with the 7.5-inch e-Paper (B) V2 display.
The firmware project-specific instructions are in
[`../firmware/AI-INSTRUCTIONS.md`](../firmware/AI-INSTRUCTIONS.md).

## Build and upload

Open the `firmware/` directory in VS Code with the pioarduino extension. The
PlatformIO CLI can also be used from that directory:

```sh
pio run
pio run -t upload
pio device monitor
```

The serial monitor uses 115200 baud. There are no automated firmware tests.
Verify display behavior on the physical hardware using the commissioning
items in [HARDWARE.md](./HARDWARE.md).

## Provisioning

With no saved Wi-Fi settings, the device starts a WPA2 access point with a
generated SSID and password. With saved settings, it attempts to connect for
up to five minutes before entering setup mode. Connect to the access point
and open `http://192.168.4.1`. The portal scans for nearby Wi-Fi networks and
accepts the selected network credentials, image-server URL, API key, and
content ID. The configuration page also controls whether GPIO2 is used as a
status LED; this is enabled by default. It tests Wi-Fi connectivity and
authenticated EPD2 metadata before allowing the settings to be saved.

The form submits credentials in the POST body, not URL parameters. Password
fields are blank on page load. Existing non-secret settings are prefilled;
leaving the Wi-Fi password or API key blank retains its saved value. The API
key is never displayed or returned by the settings endpoint. Saving tested
settings restarts the device; it reconnects, checks image metadata, fetches and
validates a changed image, then refreshes the display. The portal's
"Erase saved settings and reset" action clears the Wi-Fi and image-server
configuration and restarts into initial setup.

HTTP is accepted but sends the API key without encryption; use HTTPS with
certificate validation for deployment. Wi-Fi credentials and API settings
are stored in ESP32 NVS only after successful testing and explicit save.
NVS is not automatically encrypted.

## Startup and reset

On a non-deep-sleep startup, the display shows the saved SSID, image-server
URL, and a notice that the API key is not shown for one minute before the
first image request. To edit settings without erasing them, hold BOOT for five
seconds while this startup screen is visible. This opens setup mode with the
current non-secret settings prefilled. BOOT is GPIO0. Do not hold BOOT while
pressing EN or powering on, as this can select the ESP32 ROM download mode.

GPIO2 retains its existing rapid blink behavior while BOOT is held during the
startup-status window; BOOT presses outside that window are ignored. After the
window, the LED follows the configured status-LED setting: on while the
controller is active, including in setup mode, and off before deep sleep. The
setup-entry blink remains available even when status indication is disabled.

## Image updates and power

Any non-negative 64-bit integer content ID can be configured; the example IDs
0 and 1 are not hardcoded as the only choices. Display width, height, color
count, and ordered color string are fixed in the firmware profile:
800 x 480 and `black|red`. They are not user settings because this panel has
only this supported configuration. The device sends the content ID and these
fixed display parameters, together with the API key, in a POST to
`/api/display/current`. The server returns both `hash` and `response_id`.

The device compares the image hash with the last successfully displayed hash
and downloads only changed images. The image POST includes `response_id`;
missing or unknown response IDs are server errors. Image data is received in
fixed 512-byte chunks. The five-byte header is checked against the fixed panel
geometry and two-color count, and the body length is independently derived
before the image planes are streamed to controller RAM. The ESP32 does not
allocate a complete framebuffer or complete color planes. The last displayed
hash is kept in RTC RAM, not NVS. It survives scheduled deep-sleep wakeups so
an unchanged image skips the download and refresh at the next update interval.
It is explicitly cleared before the startup settings (boot) screen is shown,
so the current image is fetched and displayed again after a normal startup,
including after power loss.

An unchanged image skips the image transfer and display refresh. After an
update check, the device enters one-hour deep sleep. Unreachable-server,
404, and server errors are retried every minute for up to five attempts; after
that the device shows an error and resumes the normal hourly cycle. A 403
displays an API-key error immediately.

The display's visible image may persist without power, but controller RAM
retention is not assumed. See [HARDWARE.md](./HARDWARE.md) for the driver
record and outstanding physical-device verification.
