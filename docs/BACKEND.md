# PHP backend

The backend generates an 800 × 480 black/white/red image and exposes its
metadata and binary image through the EPD2 API. The shared transport format is
defined in [EPD2-SPECIFICATION.md](./EPD2-SPECIFICATION.md).

## Requirements

- PHP 8.3 or newer
- PHP GD extension
- Writable `backend/storage/` directory

There are no third-party PHP dependencies.

The example image is generated once per day: a centered weekday in large red
text and the date in black. The PNG, converted EPD image, and date marker are
cached in `storage/`; subsequent requests reuse them until the date changes.
The image processor maps pixels deterministically to black, white, and red.
Each plane uses LSB-first bit packing; white sets neither plane, while black
and red set the matching plane. This is the backend's logical EPD2 encoding;
the firmware handles any conversion required by the display controller.

## API

- `GET /api/display/current` returns JSON metadata containing `hash`, `width`,
  `height`, `format`, `size`, and `url`.
- `GET /api/display/image` returns the 96,000-byte image as
  `application/octet-stream`.

Both endpoints require the `X-API-Key` HTTP header. Invalid or missing keys
receive `403 Forbidden`. The image endpoint supports `ETag` and
`If-None-Match`; metadata includes the SHA-256 hash of the cached EPD image.

Configure the backend key in the backend configuration. Replace any
development key before deployment and keep production configuration private.
Do not send keys in URL parameters or write them to logs. Use HTTPS in
production.

Example request (replace the placeholder with the configured key):

```sh
curl -H 'X-API-Key: YOUR_CONFIGURED_KEY' \
  https://example.invalid/api/display/current
```

The backend does not provide an upload or image-generation API.
