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

The serial monitor uses 115200 baud. There are currently no automated tests.
Verify display behavior on the physical hardware using the commissioning
items in [HARDWARE.md](./HARDWARE.md).

## Provisioning

With no saved Wi-Fi settings, the device starts a WPA2 access point with a
generated SSID and password. With saved settings, it attempts to connect for
up to five minutes before entering setup mode. Connect to the access point
and open `http://192.168.4.1`. The portal scans for nearby Wi-Fi networks and
accepts the selected network credentials, image-server URL, and API key. It
tests Wi-Fi connectivity and authenticated EPD2 metadata before allowing the
settings to be saved.

The form submits credentials in the POST body, not URL parameters. Password
fields are blank on page load, and the API key is not prefilled. Saving tested
settings restarts the device; it reconnects, checks image metadata, fetches and
validates a changed image, then refreshes the display.

HTTP is accepted but sends the API key without encryption; use HTTPS with
certificate validation for deployment. Wi-Fi credentials and API settings
are stored in ESP32 NVS only after successful testing and explicit save.
NVS is not automatically encrypted.

## Startup and reset

On a non-deep-sleep startup, the display shows the saved SSID, image-server
URL, and API key with only its first four characters visible for one minute
before the first image request. To erase saved Wi-Fi and API settings, press
and release EN, wait for the startup screen, then hold BOOT for five seconds.
BOOT is GPIO0. Do not hold BOOT while pressing EN or powering on, as this can
select the ESP32 ROM download mode.

GPIO2 blinks rapidly while BOOT is held during the startup-status window. It
then remains off; BOOT presses outside that window are ignored.

## Image updates and power

The device requests authenticated metadata and downloads the image only when
its SHA-256 differs from the last successfully displayed image. Image data is
received in fixed 512-byte chunks and streamed to controller RAM. Setup text
is rendered one row at a time with a 100-byte row buffer. The ESP32 does not
allocate a 96,000-byte framebuffer or two complete 48,000-byte planes. The
last displayed hash is kept in volatile RTC RAM, not NVS.

An unchanged image skips the image transfer and display refresh. After an
update check, the device enters one-hour deep sleep. Unreachable-server,
404, and server errors are retried every minute for up to ten attempts; after
that the device shows an error and resumes the normal hourly cycle. A 403
displays an API-key error immediately.

The display's visible image may persist without power, but controller RAM
retention is not assumed. See [HARDWARE.md](./HARDWARE.md) for the driver
record and outstanding physical-device verification.
