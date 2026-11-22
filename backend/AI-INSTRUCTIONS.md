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

The backend accepts a source image (or uses the source image already
defined by the project), converts it into an 800 × 480 display image
using black, white, and red, and serves it over HTTP.

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

Use an appropriate PHP library, preferably GD or Imagick, depending on
the available runtime environment.

Requirements:

1.  Load source images safely and reject invalid files.
2.  Resize to 800 × 480 and crop appropriately (cover behavior), unless
    the project already specifies another image-fitting rule.
3.  Preserve aspect ratio; do not introduce unintended distortion.
4.  Composite transparency onto a documented background (white by
    default).
5.  Quantize colors deterministically to black, white, or red.
6.  Make dithering optional only if it is configurable and documented.
7.  Ensure no pixel is set in both planes at the same time.
8.  Validate the output is exactly 96,000 bytes and that both planes are
    valid before publication.

Keep image processing maintainable and separate from HTTP controllers
and filesystem access.

## HTTP endpoints

Implement the endpoints defined in the shared specification:

### `GET /api/display/current`

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

Return only the 96,000 binary bytes as `application/octet-stream`. No
JSON and no additional file header.

Support `ETag` and `If-None-Match` if practical, according to the
specification. If the ETag matches, the endpoint may return
`304 Not Modified`.

## Atomic publication and consistency

Never publish a hash that does not correspond to the fully written image
file.

Recommended process:

1.  Write the new image data completely to a temporary file on the same
    filesystem.
2.  Validate its length and content.
3.  Calculate SHA-256 over the temporary file.
4.  Use a versioned image file or another consistent publication
    mechanism.
5.  Atomically update the active manifest with hash, size, format, and
    image path.
6.  Remove temporary files on failure.

Account for concurrent requests. A metadata request followed shortly by
an image request must resolve to a consistent version. Use versioned
image files, immutable filenames, or appropriate locking if needed.
Simply replacing two independent files without a consistency strategy is
insufficient.

## Security and robustness

-   Validate input files; file extensions alone are not sufficient.
-   Limit upload size and allowed MIME/image formats.
-   Never use unchecked filenames as filesystem paths.
-   Do not write credentials or secret configuration values into
    responses or logs.
-   Return appropriate HTTP status codes for errors.
-   Do not emit PHP warnings/notices into binary responses.
-   The binary endpoint must be free of debug output, BOM, and extra
    whitespace.
-   Manage paths and permissions through configuration; do not hardcode
    secrets.

## Tests

Write automated tests where the existing project structure allows. At
minimum, verify:

-   output dimensions and plane lengths;
-   total length is exactly 96,000 bytes;
-   SHA-256 matches the actual binary response body;
-   all-white, all-black, and all-red test images produce the expected
    planes;
-   no pixel is encoded in both planes;
-   ETag / `If-None-Match` behavior;
-   metadata and binary image remain consistent during updates;
-   invalid source files are rejected;
-   a failure during image generation does not overwrite the last valid
    version.

Create fixtures for all-white, all-black, all-red, color quadrants,
corner pixels, and stripes.

## Documentation and deliverables

Create or update:

-   installation and runtime requirements;
-   configuration and required PHP extensions;
-   API documentation with example responses;
-   image processing and quantization method;
-   test and deployment instructions;
-   description of the atomic publication mechanism.

## Working rules

-   Inspect the existing codebase before changing anything; do not
    overwrite working parts without reason.
-   Follow the existing style and architecture where appropriate.
-   Separate image conversion, storage, and HTTP output.
-   Do not perform unrequested framework migrations.
-   Do not modify the ESP32 firmware.
-   Do not change the shared EPD2 specification without proposing the
    change first.
-   Run tests and report honestly which tests were actually executed.
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
5.  tests executed and their results;
6.  outstanding items and required deployment steps.
