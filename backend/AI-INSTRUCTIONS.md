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

The current example generates a daily weekday/date image, converts it
into the EPD2 format, and serves it over HTTP. It has no upload or
generation endpoint.

The firmware should download the image only when its SHA-256 hash has
changed.

## Required image output

Generate exactly the format defined in `../docs/EPD2-SPECIFICATION.md`:

-   800 × 480 pixels.
-   1 bit per pixel per plane.
-   48,000 bytes for the black plane.
-   48,000 bytes for the red plane.
-   Order: black plane, then red plane.
-   Total length: exactly 96,000 bytes.
-   No header in the binary body.
-   White: both logical bits 0.
-   Black: black bit 1, red bit 0.
-   Red: black bit 0, red bit 1.

The exact bit order within each byte is explicitly marked in the
specification as requiring verification against the original driver.
Implement the logical encoding cleanly and encapsulate the byte-packing
decision so it can be adjusted once the firmware/driver inspection
confirms it. Document the convention actually used. Do not claim it is
hardware-verified until tests prove that.

## Image processing

Use GD to create the daily weekday/date PNG with its built-in font and
convert it into EPD2. There is no user-provided image or upload endpoint.

Requirements:

1.  Generate the example PNG at exactly 800 × 480 pixels.
2.  Quantize colors deterministically to black, white, or red.
3.  Make dithering optional only if it is configurable and documented.
4.  Ensure no pixel is set in both planes at the same time.
5.  Validate the output is exactly 96,000 bytes and that both planes are
    valid before publication.

Keep image processing maintainable and separate from HTTP controllers
and filesystem access.

## HTTP endpoints

Implement the endpoints defined in the shared specification:

### `GET /api/display/current`

Require the configured API key in the `X-API-Key` request header.
Requests with a missing or invalid key return `403 Forbidden`.

Return JSON metadata containing:

-   `hash`
-   `width`
-   `height`
-   `format`
-   `size`
-   `url`

Expected values: 800, 480, `EPD2`, 96000. The hash is SHA-256 over the
exact binary body returned by the image endpoint.

### `GET /api/display/image`

Require the configured API key in the `X-API-Key` request header.
Requests with a missing or invalid key return `403 Forbidden`.

Return only the 96,000 binary bytes as `application/octet-stream`. No
JSON and no additional file header.

Support `ETag` and `If-None-Match` if practical, according to the
specification. If the ETag matches, the endpoint may return
`304 Not Modified`.

## Example image cache

The example stores the generated PNG, EPD file, and a date marker under
`storage/`. Reuse them while the date marker matches the current date
and the EPD file is exactly 96,000 bytes. Generate temporary files in
the same directory and replace the cache files only after successful
generation. The metadata endpoint calculates SHA-256 from the cached
EPD file. Do not add a manifest or lock unless the project requirements
grow beyond this example.

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
against the original driver and physical display.

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
