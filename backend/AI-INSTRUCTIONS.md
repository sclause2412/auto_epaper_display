# Instructions for the Backend AI (PHP)

You are developing only the PHP backend for the battery-powered e-paper
picture frame.

## Read these files first

Before making any changes, read in full:

1.  `../docs/EPD2-SPECIFICATION.md`
2.  this file
3.  the existing backend code and configuration

The EPD2 specification is the binding interface to the firmware. Do not
change it independently. If a change appears technically necessary,
first explain why and propose a coordinated specification change.

## Goal

The current example generates configurable weekday/date or legacy example
images, converts them into the EPD2 format, and serves them over HTTP. It has
no upload or user-provided image endpoint.

The firmware should download the image only when its SHA-256 hash has
changed.

## Required image output

Generate exactly the format defined in `../docs/EPD2-SPECIFICATION.md`:

-   A five-byte header containing width, height, and color-plane count.
-   1 bit per pixel per logical color plane.
-   Plane size = `ceil(width * height / 8)` bytes.
-   Total size = `5 + planeSize * colorCount`.
-   Plane order follows the display's ordered `COLORS` string.
-   White: unset in every plane.
-   Each named color: matching plane bit set.
-   Controller polarity and conversion remain firmware concerns.

The exact bit order within each byte is explicitly marked in the
specification as requiring verification against the firmware hardware record.
Implement the logical encoding cleanly and encapsulate the byte-packing
decision so it can be adjusted once the firmware/driver inspection
confirms it. Document the convention actually used. Do not claim it is
hardware-verified until tests prove that.

## Image processing

Use GD to create the daily weekday/date PNG with its built-in font and
convert it into EPD2. There is no user-provided image or upload endpoint.

Requirements:

1.  Generate the example PNG at the requested width and height.
2.  Quantize colors deterministically according to the supported color list.
3.  Make dithering optional only if it is configurable and documented.
4.  Ensure no pixel is set in more than one plane.
5.  Validate the five-byte header and exact variable body length before
    publication.

Keep image processing maintainable and separate from HTTP controllers
and filesystem access.

For content generation, implement these exact example variants:

- Content ID 0: white background; English weekday name, for example `Tuesday`,
  in the upper centered text area; date in `DD.MM.YYYY`, for example
  `24.11.2026`, in the lower centered text area.
- Content ID 1: identical layout and date format with the German weekday name,
  for example `Dienstag`.
- At 800 x 480, reference the existing layout: weekday around y=190 and date
  around y=300.
- One supported logical color: render both text blocks in that color.
- Two or more supported logical colors: weekday red and date black; ignore
  additional colors from the third position onward.
- Cache ID 0 and ID 1 independently and include the current date in the cache
  key for each.

## HTTP endpoints

Implement the endpoints defined in the shared specification:

### `POST /api/display/current`

Accept `POST` and `GET`. Require the configured API key in `X-API-Key`,
or as `API_KEY`/`api_key` in the POST body. Never accept or log the key in a
URL query parameter. Accept content ID, width, height, and ordered
colors through `CONTENT_ID`/`content_id`, `WIDTH`/`width`/
`display_width`, `HEIGHT`/`height`/`display_height`, and
`COLORS`/`colors`/`color_config`/`display_colors`.

Missing or invalid keys return `403 Forbidden`. Unknown content IDs return
`404 Not Found`. Invalid geometry or colors return `400 Bad Request`.

Return JSON metadata containing:

- `hash`
- `response_id`
- `width`
- `height`
- `format`
- `size`
- `url`

The hash is SHA-256 over the complete EPD2 body including the header. The
response ID is SHA-256 over the canonical configuration:
`content_id=<id>|width=<width>|height=<height>|colors=<ordered-colors>`.

### `POST /api/display/image`

Accept `POST` and `GET`. Require authentication and a valid
`RESPONSE_ID`/`response_id`. Missing or unknown response IDs return
`404 Not Found`.

Return only the matching variable-size EPD2 body as
`application/octet-stream`.

Support `ETag` and `If-None-Match` if practical, according to the
specification. If the ETag matches, the endpoint may return
`304 Not Modified`.

## Example image cache

The example stores generated PNG and EPD files under `storage/`, keyed by
content ID, dimensions, ordered colors, and date when required. Reuse a cache
entry only after the header and exact variable body length have been validated.
Generate temporary files in the same directory and replace cache files only
after successful generation. The metadata endpoint calculates SHA-256 from the
cached complete EPD body. Maintain a response-ID-to-configuration mapping so
the image endpoint knows exactly which file is requested.

## Security and robustness

-   Never use unchecked filenames as filesystem paths.
-   Do not write credentials or secret configuration values into
    responses or logs.
-   Require the configured API key in `X-API-Key` for both display API
    endpoints. Compare keys with `hash_equals`; return `403` for missing
    or invalid keys. Never accept the key in a URL.
-   Use HTTPS in public deployments. Replace the example key and keep
    the production configuration out of public source control.
-   Return appropriate HTTP status codes for errors.
-   Do not emit PHP warnings/notices into binary responses.
-   The binary endpoint must be free of debug output, BOM, and extra
    whitespace.
-   Manage paths and permissions through configuration. The example key
    may be in `config.php`; use private deployment configuration for
    production.

## Verification

This example intentionally has no automated test suite. For changes to
the protocol or image conversion, verify endpoint authentication,
metadata, exact image length, SHA-256, ETag behavior, and color/bit
encoding manually. Hardware-dependent encoding still requires tests
against the firmware hardware record and physical display.

## Documentation and deliverables

Create or update:

-   installation and runtime requirements;
-   configuration and required PHP extensions;
-   API documentation including `X-API-Key` authentication;
-   image processing and quantization method;
-   deployment instructions, including HTTPS and secret-key handling.

## Working rules

-   Inspect the existing codebase before changing anything; do not
    overwrite working parts without reason.
-   Follow the existing style and architecture where appropriate.
-   Separate image conversion, storage, and HTTP output.
-   Do not perform unrequested framework migrations.
-   Do not modify the ESP32 firmware.
-   Do not change the shared EPD2 specification without proposing the
    change first.
-   Perform manual verification and report honestly what was checked.
-   If a requirement cannot be implemented because a PHP extension or
    infrastructure component is missing, identify the exact blocker and
    suggest an appropriate alternative.

## Final report

After implementation, list:

1.  files created and modified;
2.  implemented endpoints;
3.  image processing method used;
4.  how bit packing and polarity are handled, and what still requires
    hardware verification;
5.  verification performed and its results;
6.  outstanding items and required deployment steps.
