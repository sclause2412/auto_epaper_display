# Hardware verification record

## Identified display, board, and driver

The hardware is the Waveshare 7.5-inch e-Paper (B) V2 raw display (without
HAT) and Waveshare E-Paper ESP32 Driver Board. The panel version was confirmed
by the user. The [display manual](https://www.waveshare.com/wiki/7.5inch_e-Paper_HAT_(B)_Manual)
lists 800 x 480 for V2. The Waveshare code does not state the controller IC
part number.

The supplied demo selects `EPD_7IN5B_V2` from `EPD.h` and calls
`EPD_7IN5B_V2_*`. Its header identifies the Waveshare driver as version V2.1
(dated 2020-12-01) and defines 800 x 480 pixels.

## Wiring and serial signaling in the demo

`DEV_Config.h` assigns the same fixed connections listed in the [board
manual](https://www.waveshare.com/wiki/E-Paper_ESP32_Driver_Board):

| Signal | ESP32 GPIO |
| --- | ---: |
| SCK | 13 |
| MOSI | 14 |
| CS | 15 |
| RST | 26 |
| DC | 27 |
| BUSY | 25 |

`DEV_Config.cpp` configures BUSY as input and the other listed signals as
outputs. The demo bit-bangs the serial interface (the hardware SPI calls are
commented out): SCK is initialized low, each byte is shifted most-significant
bit first, and MOSI is set before SCK is driven high then low. The display
manual specifies SPI mode 0. The driver asserts DC low for a command and high
for data.

The board manual says the e-Paper configuration switch No. 1 selects the
resistor setting; its table lists **0.47R (B)** for the 7.5-inch e-Paper (B).
Confirm the physical switch position before first refresh. The same manual
describes the board's display connections as fixed.

The physical board's BOOT button is confirmed to use GPIO0. For factory reset,
press and release EN, then hold BOOT only after firmware startup. Holding BOOT
while resetting or applying power can select the ESP32 ROM download mode.

## Initialization and image transfer

`EPD_7IN5B_V2_Init()` performs a reset (RST high 200 ms, low 2 ms, then high
200 ms), sends power settings with command `0x01`, turns power on with `0x04`,
and waits for BUSY. It then configures panel settings (`0x00`), resolution
(`0x61`, bytes `03 20 01 E0`, i.e. 800 x 480), and other panel/timing
registers (`0x15`, `0x50`, `0x60`, `0x65`). This sequence is implemented in
`src/epd_7in5b_v2.cpp`; do not substitute a different driver's sequence.

The full-frame `EPD_7IN5B_V2_Display()` sends:

1. command `0x10`, then 48,000 bytes from `blackimage`;
2. command `0x92`;
3. command `0x13`, then 48,000 bytes, each sent as `~ryimage[i]`;
4. display refresh via command `0x12`.

The full-frame function streams from supplied pointers and does not itself
allocate a framebuffer, but the demo allocates two 48,000-byte image arrays.
The controller write interface can therefore be adapted to streaming by
retaining the command sequence and replacing its pointer loops with fixed
chunk writes. A partial-window path exists separately in
`EPD_7IN5B_V2_Display_Partial()` and should not be confused with full-frame
transfer.

The firmware adapter in `src/epd_7in5b_v2.cpp` now uses that command
sequence for fixed-size streaming writes. Its setup screen is generated one
row at a time with a 100-byte row buffer; it does not allocate a framebuffer.

## EPD2 conversion, bit order, and plane layout

`GUI_Paint.h` defines `WHITE=0xFF`, `BLACK=0x00`, and `RED=BLACK`. In
`GUI_Paint.cpp`, the 1-bit pixel writer addresses the most-significant bit
first (`0x80 >> (X % 8)`), stores a black pixel as 0 and a non-black pixel
as 1, and advances through rows in the image buffer. The Waveshare red-plane
path complements its input before transmission. The display manual confirms
the same convention for the image representation: bit 1 is white, bit 0 is
black/red, with pixels packed MSB-first; it also describes separate black/white
and red/white planes.

Relative to the logical EPD2 encoding (set bit = that color), the indicated
backend output uses LSB-first pixels within each byte, while the Waveshare
display representation is MSB-first. The firmware reverses the bit order in
each received byte before applying this conversion:

- black plane: bitwise complement each EPD2 byte before writing command
  `0x10`;
- red plane: bit-reverse each EPD2 byte, then write it to command `0x13`.

This follows the manual's plane convention, the demo's `Paint` data
representation, and its `~ryimage` operation. It still needs the EPD2
acceptance images on the actual panel to verify orientation and color mapping
in situ. Full-frame byte order in the driver is
row-major: 100 bytes per row, rows iterated from 0 through 479. The source
does not explicitly configure a full-frame RAM window or provide an IC
datasheet, so physical scan orientation must not be inferred beyond this
buffer order.

## Refresh, BUSY, and low power

Refresh sends `0x12`, waits 100 ms, then calls
`EPD_7IN5B_V2_WaitUntilIdle()`. The implementation reads BUSY and continues
waiting while it is LOW; it proceeds when BUSY is HIGH, then delays another
200 ms. This matches the code's `busy = !(busy & 0x01)` condition, although
the adjacent source comment says “goes LOW.” The firmware adapter follows
the actual condition and adds a bounded timeout instead of copying the
unbounded original wait.

`EPD_7IN5B_V2_Sleep()` sends `0x02` (power off), waits for BUSY, then sends
`0x07` with data `0xA5` (deep sleep). The display manual says image data sent
while the display is asleep is ignored and that it must be initialized again
before refreshing. Neither the source nor manual guarantees controller RAM
survival across ESP32 deep sleep or loss of board power; do not rely on it.
The physical panel's image retention and the controller RAM's retention are
separate questions.

The panel manual specifies a 26-second refresh time and recommends at least
180 seconds between refreshes. The one-hour update cadence exceeds this
recommended interval. It also recommends putting an idle display into sleep
or powering it off.

During Wi-Fi setup, firmware displays the generated AP SSID, eight-digit
WPA2-PSK password, setup URL, and a static notice that the hotspot closes
after 10 minutes. The screen is refreshed only at setup entry. At timeout it
displays that setup was not completed and the required restart message,
sleeps the controller, and enters ESP32 deep sleep.

## Still to verify

- Exact controller IC.
- Confirm board switch No. 1 is set to the 0.47R (B) position for this panel.
- Orientation, logical-plane conversion, and red/black appearance using the
  EPD2 test images.
- BUSY timing under real refresh and appropriate timeout duration.
- Visible image retention across the proposed sleep/power sequence and
  controller-RAM behavior after deep sleep.
- BOOT/reset button pin and safe long-press behavior. No BOOT pin is defined
  by the supplied display demo; GPIO0 remains unconfirmed.
