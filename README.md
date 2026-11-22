# Smart E-Paper Picture Frame

A battery-powered digital picture frame built around a Waveshare 7.5-inch
e-Paper (B) V2 display and the Waveshare ESP32 Driver Board. A PHP backend
prepares display-ready images; the ESP32 retrieves updates over Wi-Fi and
keeps power use low between checks.

## What it does

- Provisions Wi-Fi and server settings through an on-device setup portal.
- Fetches authenticated image metadata and transfers an image only when its
  content hash changes.
- Streams image data to the display controller without allocating a complete
  image framebuffer in ESP32 memory.
- Sleeps between scheduled image checks.

## Project layout

- [`backend/`](./backend/) — PHP image-generation and EPD2 API.
- [`firmware/`](./firmware/) — pioarduino/PlatformIO ESP32 firmware project.
- [`docs/EPD2-SPECIFICATION.md`](./docs/EPD2-SPECIFICATION.md) — shared
  backend/firmware image protocol.
- [`docs/HARDWARE.md`](./docs/HARDWARE.md) — display, board, and driver
  verification record.
- [`docs/BACKEND.md`](./docs/BACKEND.md) — backend requirements and API details.
- [`docs/FIRMWARE.md`](./docs/FIRMWARE.md) — firmware build, provisioning, and
  operating details.

## Getting started

1. Configure and deploy the PHP backend. See
   [`docs/BACKEND.md`](./docs/BACKEND.md).
2. Open `firmware/` in VS Code with the pioarduino extension and build the
   firmware. See [`docs/FIRMWARE.md`](./docs/FIRMWARE.md).
3. Configure the device through its Wi-Fi setup portal and provide the backend
   URL and API key there; these values are not compiled into the firmware.
4. Check the hardware setup and remaining commissioning items in
   [`docs/HARDWARE.md`](./docs/HARDWARE.md).

## Security

The API key is provisioned on the device rather than embedded in firmware.
Use HTTPS for deployed backends; HTTP sends the API key without encryption.
Do not commit production API keys or expose them in URLs or logs.

## Hardware status

The display version and driver board are identified in the hardware record.
Some display/controller behaviors still require verification on the physical
device; see its outstanding checks before deployment.
