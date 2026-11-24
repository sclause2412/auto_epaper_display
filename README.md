# Smart E-Paper Picture Frame

A battery-powered digital picture frame built around a Waveshare 7.5-inch
e-Paper (B) V2 display and the Waveshare ESP32 Driver Board. A PHP backend
prepares display-ready images; the ESP32 retrieves updates over Wi-Fi and
keeps power use low between checks.

## AI information

The whole project including the different README.md, all docs, code, etc. was
generated using Vibe Coding with manual verification.
Only this section (AI information) here was written manually.
The idea was to test the capabilities of AI.
Every line was written by GitHub Copilot in VS Code with a free tier.
The whole initial project consumed about 112 out of 200 monthly free credits.
All programming was done within 24 hours.
Inline suggestions were switched off.
Most of the credits were used by bug fixing which were found by manual
inspection because the AI sometimes thinks too complicate.
A very nice example:
All timings are done in seconds now but the first attempt of the AI was a wild
mixture of seconds, minutes and hours.
The code was almost unreadable because of it's complexity.
Same for error messages - AI created about 30 different status messages which
were in the end so similar that I asked it to replace it by a single one.
It would never have done this on it's own.

This was the scope of the initial project:

- Create firmware for my display controller + display
  - Controller: Waveshare ESP32 Driver Board V3
  - Display: Waveshare 7.5 inch E-Paper (B) V2 with black/white/red color
  - Only this controller and display needs to be supported by now, there is no
    one controller - multiple displays option, the hardware spec is fixed
- Create backend for creating the images
  - Simple website written in pure PHP
  - No external libraries
  - Should just create some example images
  - Is just a reference to test the display and use as base for future (more
    fancy) implementations
- Create specification and protocoll
  - Create specification for firmware and backend
  - Make it future proof (different display sizes, different contents)

**Why not using some ready-to-go solutions?**

Because there are none.

There is ESPHome which works but you cannot transfer a complete 800 x 480 two
color image as ESPHome only supports 8bit pictures and tries to completly
transfer it.
Since the Controller has no PSRAM this is not possible.
Creating instead a Dashboard on ESPHome is complicate and not very user friendly.
Additionally, if you want to connect some external data (like Google calendar)
it is almost impossible with ESPHome.

There is the Waveshare example code for WiFi.
It's working but it is the wrong way.
You are pushing data to the controller which requires the controller to be
online all the time.
This requires so much battery that the whole setup will be running out of power
after some days.

There are othere implementations which do not support a simple backend, are
complicate to configure, do not fit on this controller, etc.
The most complicate part is the missing PSRAM.
All solutions I found require a different controller with PSRAM but since I
have this controller already in use and it has a nice connector for the
WaveShare displays I didn't want to switch it.

**Why AI?**

Because I can. It would have taken me very long to write that code because of
the big amount of trial and error, reading hundreds of specs, "translating" the
example code to my needs. But I'm very fast at reading and approving code, so
this was a very good example for AI usage. The only requirement I had: It must
be doable with a free tier.

Another big advandage: If I ever will use a different display, I don't have to
"reinvent" the whole project. I just ask AI to add another display based on the
original firmware code and it will do it within minutes.


## What it does

- Provisions Wi-Fi and server settings through an on-device setup portal.
- Fetches authenticated image metadata and transfers an image only when its
  content hash changes.
- Supports a configurable content ID, display geometry, and ordered color
  configuration for multiple devices.
- Provides two example contents: content ID 0 for the legacy English weekday
  image and content ID 1 for the German weekday plus date image.
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
