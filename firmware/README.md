# ESP32 firmware

This is the pioarduino/PlatformIO firmware project for the Smart Display.

## Status

The display adapter in `src/epd_7in5b_v2.cpp` follows the supplied original
Waveshare `EPD_7IN5B_V2` driver. The setup AP's SSID and password are rendered
on the display with a static notice that the hotspot closes after 10 minutes.
The setup screen is not refreshed for a changing countdown.

Set the driver board's display resistor switch to **0.47R (B)** for the
7.5-inch e-Paper (B) before refreshing.

## Build

Open this directory in VS Code with the pioarduino extension and run:

- `pio run`
- `pio run -t upload`
- `pio device monitor`
- `pio test -e native`

The PlatformIO CLI is optional; pioarduino may provide the build commands
from the VS Code extension.

## Provisioning

Enter Wi-Fi credentials, the HTTP/HTTPS image-server base URL, and API key through
the setup portal. The API URL and key are not compiled into the firmware.
Settings are stored in ESP32 NVS only after the Wi-Fi and EPD2 metadata tests
succeed. Both the setup form and its request body keep secret values out of
URLs; password and API-key fields are blank on every page load.
NVS storage is not automatically encrypted; physical-device threat models
may require enabling ESP32 flash/NVS encryption during provisioning.

The portal scans for nearby SSIDs. Its test action connects to the selected
network and requests the authenticated `/api/display/current` endpoint,
checking the EPD2 dimensions, format, size, and SHA-256 field. HTTPS uses
certificate validation. HTTP is allowed but transmits the API key without
encryption; use it only on a trusted network. Saving is enabled only when
these checks pass.

After saving, the device restarts, reconnects to Wi-Fi, fetches the
authenticated EPD2 metadata and image, verifies the image length and SHA-256,
streams it to the display, and refreshes only if the image hash has changed.
It keeps the last successfully displayed hash in volatile RTC RAM, never NVS,
then enters one-hour deep sleep. The cached hash survives scheduled deep-sleep
wakes but is cleared by other resets so a restarted controller always fetches
the current image. Failed updates do not advance the RAM hash.

### Factory reset and API-key errors

At a non-deep-sleep startup, the display shows the current SSID, image-server
URL, and the API key with only its first four characters visible for one
minute before the first image request. To erase the saved Wi-Fi and
image-server settings, press and release EN, wait for that screen, then hold
BOOT for five seconds. The firmware clears the complete settings namespace
and starts the setup portal.

Release BOOT before pressing EN or restoring power. Holding BOOT during reset
can select the ESP32 ROM download mode. BOOT is confirmed to use GPIO0 on
this board. The GPIO2 LED blinks rapidly while BOOT is held during the
startup-status window, then remains off; BOOT presses outside that window are
ignored and do not light the LED.

If either image endpoint returns `403 Forbidden`, the e-paper displays an API
key rejection message in red with an EN reboot hint. The 404/server retry
error and setup-timeout restart message also show their reboot/reset lines in
red. The API-key error remains visible during sleep; use the EN and BOOT
factory-reset sequence to enter setup and save corrected settings.

If the image server is unreachable, returns `404`, or returns a server error,
the controller retries every minute up to ten times. It then displays an
error and resumes the normal one-hour sleep/retry cycle.

## Memory constraint

EPD2 data transfer uses a fixed 512-byte receive buffer. Setup text is
rendered a scanline at a time with a 100-byte row buffer. There is no
96,000-byte complete framebuffer or pair of 48,000-byte plane buffers.

## Outstanding work

- Upload and verify the setup credentials screen on the physical V2 display.
- Verify orientation, red/black polarity, refresh timing, and sleep on
  hardware.
- Confirm the controller IC from physical markings or board documentation.
- Verify normal-operation image download and hash comparison with the deployed
  image server and physical display.
- Verify setup timeout display and deep-sleep behavior on hardware.
