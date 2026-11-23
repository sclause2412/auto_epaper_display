# EPD2 -- Shared Protocol Specification

**Status:** Proposed specification for dynamic displays; implementation may
follow only after backend and firmware are changed together.
**Purpose:** Binding interface definition shared by the PHP backend and ESP32
firmware.

## 1. Target and display geometry

- Display: compatible with the ESP32 firmware and color-plane e-paper protocol.
- Resolution: configurable per request.
- Color mode: one or more logical color planes.
- The width and height are declared by the requesting display, and the color
  configuration is transmitted as an ordered, pipe-separated string, for
  example `black|red`.
- A display is responsible for converting logical polarity and controller
  representation as required by its own hardware.

## 2. Binary data format

The transport body is named **EPD2**. It begins with a five-byte header:

| Offset | Length | Content |
|---:|---:|---|
| 0 | 2 bytes | Width as unsigned big-endian integer |
| 2 | 2 bytes | Height as unsigned big-endian integer |
| 4 | 1 byte | Number of color planes |

The header has no magic bytes and no color names. The color names are known
from the display request and must not affect image suitability if they differ;
only width, height, and color count are validated by the firmware.

After the header, the file contains one 1-bit plane for each declared color:

```text
planeSize = ceil(width * height / 8)
totalSize = 5 + (planeSize * colorCount)
```

Each plane is row-major. Within each byte, the backend uses **LSB-first**
packing. White is represented by an unset bit in every plane. Each supported
color is represented by its matching plane; a pixel must not be set in more
than one plane.

Examples:

- 800 x 480, two colors: `5 + (48,000 * 2) = 96,005` bytes.
- 800 x 480, one color: `5 + 48,000 = 48,005` bytes.
- 10 x 10, one color: `5 + ceil(100 / 8) = 18` bytes.

The example 10 x 10 value documents byte rounding: 100 pixels require 13 bytes
as one 1-bit plane, plus the five-byte header. One-byte-per-pixel encoding is
not used.

The existing 800 x 480 display remains compatible with the 1-bit-per-pixel,
LSB-first convention, but now receives a header before its two planes.

## 3. HTTP interface

### 3.1 Authentication

Both display endpoints require a shared API key. It may be supplied through
the `X-API-Key` header or an `API_KEY`/`api_key` POST field. The header is
preferred. API keys must not be placed in URL query parameters. The backend
must compare the supplied value with its configured key using a
timing-safe comparison.

Missing or invalid keys return `403 Forbidden`. Authentication is required
before metadata or ETag processing.

Clients must not put the key in normal URLs or logs. Public deployments should
use HTTPS and keep production keys out of source control.

### 3.2 Display request parameters

`POST /api/display/current` is the preferred request method. `GET` is also
accepted for compatibility.

Parameters:

| Name | Required | Description |
|---|---:|---|
| `API_KEY` or `api_key` | yes | Shared API key |
| `CONTENT_ID` or `content_id` | yes | Integer content selector |
| `WIDTH` or `width`, `display_width` | yes | Display width in pixels |
| `HEIGHT` or `height`, `display_height` | yes | Display height in pixels |
| `COLORS` or `colors`, `color_config`, `display_colors` | yes | Ordered `|`-separated color names |

`CONTENT_ID=0` and `CONTENT_ID=1` select the two example image variants:

- `CONTENT_ID=0` -- legacy example:
  - A white background.
  - The English weekday name from the server date, for example `Tuesday`,
    rendered as a large, horizontally and vertically centered text block.
  - The date in `DD.MM.YYYY` format, for example `24.11.2026`, rendered
    below the weekday.
  - For the reference 800 x 480 layout, the weekday block is centered around
    y=190 and the date block around y=300.
- `CONTENT_ID=1` -- German weekday example:
  - The same white background and layout as content ID 0.
  - The German weekday name, for example `Dienstag`, instead of the English
    weekday name.
  - The same date in `DD.MM.YYYY` format below it.

The server uses the current server date. The weekday/date cache therefore
belongs to both a content ID and the date. Unknown content IDs must be rejected
with `404 Not Found`.

The display size is determined by width and height. The server receives
`COLORS`, accepts only supported display sizes and color configurations for
the example implementation, and generates or retrieves the matching EPD2 file.

For both content IDs:

- Exactly one supported logical color (`black` or `red`): render both text
  blocks in that single color.
- Two or more supported logical colors: render the weekday in red and the date
  in black. Additional colors in the third and later positions are ignored.
- For example, `black|red|yellow` uses black and red planes and ignores
  `yellow`; `red|black` also renders red text and black text, while the plane
  order remains `red`, then `black`.
- Color names in `COLORS` determine plane order. Positive or negative polarity
  is handled by the display firmware and is not encoded in this string.

### 3.3 Metadata response

Success: `200 OK`, `Content-Type: application/json`

```json
{
  "hash": "HEX_SHA256",
  "response_id": "HEX_SHA256",
  "width": 800,
  "height": 480,
  "format": "EPD2",
  "size": 96005,
  "url": "/api/display/image"
}
```

Fields:

- `hash`: SHA-256 of the complete binary transport body, including the header.
- `response_id`: 64-character SHA-256-style identifier derived from the
  configuration (`content_id`, width, height, and ordered colors).
- `width`, `height`: requested display dimensions.
- `format`: always `EPD2`.
- `size`: exact EPD2 body length.
- `url`: binary image endpoint.

The `response_id` is a cache and lookup key for the configuration. It is not
the image hash. A weekday/date image may change during the day; such content
may require a new image hash while keeping the same response ID.

### 3.4 Binary image

`POST /api/display/image` is the preferred request. `GET` is accepted for
compatibility.

The request requires authentication and a `RESPONSE_ID` or `response_id`
parameter. Missing or unknown response IDs return `404 Not Found`.

Success:

- Status: `200 OK`
- Content-Type: `application/octet-stream`
- Body: exactly the EPD2 body described by the matching metadata
- No JSON wrapper or extra file header

For ETag support, the server may return `304 Not Modified` when `If-None-Match`
matches the image `hash`. Authentication and a valid response ID are still
required.

The response ID lookup must use an in-memory or server-side cache entry that
contains width, height, ordered colors, content ID, and current EPD2 file
metadata. It must never permit an arbitrary response ID to map to another
device's configuration.

## 4. Cache and consistency

The server must generate or cache an EPD2 file for each unique combination of:

- content ID
- width
- height
- ordered color configuration
- date/content version, where required

The cache key may be derived from the response ID and content date. Generation
must finish and validate the complete file before it becomes available.

The server must ensure metadata and image delivery use the same version:

1. Generate the complete PNG and EPD2 body.
2. Validate header and exact body length.
3. Calculate SHA-256 over the complete body, including the header.
4. Publish or atomically replace the cache entry.
5. Return the response ID and hash only after publication succeeds.

Invalid or unsupported requests must not publish false metadata or binary data.

## 5. Hash and response ID rules

- Algorithm: SHA-256.
- Image hash input: exact EPD2 body, starting with the header.
- Image hash output: 64 lowercase or hexadecimal ASCII characters.
- Response ID input: canonical configuration string
  `content_id=<id>|width=<width>|height=<height>|colors=<pipe-separated-colors>`.
- Response ID output: 64-character SHA-256 hexadecimal value.
- Neither HTTP metadata nor filenames are included in the image hash.
- The response ID must not include the API key or any secret.

## 6. Firmware streaming requirements

The firmware must:

- send all display parameters and the API key in a POST request;
- include content ID, width, height, and ordered colors;
- validate the metadata header values and declared color count;
- require and preserve the `response_id`;
- send the response ID with the binary image POST;
- read the five-byte header separately from the image planes;
- validate `width`, `height`, and `colorCount` against the configured display;
- validate the exact body size:
  `5 + ceil(width * height / 8) * colorCount`;
- stream each plane with a fixed, bounded buffer;
- never allocate a full image buffer proportional to width, height, or number
  of colors;
- compute the image SHA-256 over the complete body, including the header;
- cache only the image hash after a complete transfer and successful refresh.

Plane and polarity conversion remains hardware-specific. The firmware must
transform LSB-first logical bytes as required by the actual controller.

## 7. Error cases

- Missing or invalid API key: `403 Forbidden`.
- Unsupported content ID or invalid configuration: `400` or `404` as
  appropriate.
- Missing or unknown response ID: `404 Not Found`.
- Unexpected width, height, or color count: reject without refresh.
- Incorrect body length or interrupted stream: reject and do not update the
  volatile image hash.
- Controller refresh failure: do not cache the image hash.
- Server unreachable: retry according to firmware policy.

## 8. Change management

This file is the shared interface agreement. Any change to header layout,
width/height encoding, color count, plane order, byte encoding, file length,
hash calculation, response ID, or API parameters must first be documented here
and then adopted by both backend and firmware.

## 9. Acceptance tests

Test at least:

1. 800 x 480, one color.
2. 800 x 480, two colors.
3. 800 x 480, three or more configured colors.
4. 10 x 10, one color.
5. Content ID 0 and content ID 1.
6. Missing, invalid, and unknown response IDs.
7. Image and response IDs changing or remaining stable as specified.
8. All-white, all-black, all-red, quadrants, corners, stripes, and checkerboard.
