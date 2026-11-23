# PHP backend

The backend generates EPD2 images for display configurations submitted by
multiple devices and exposes metadata and binary image data through the EPD2
API. The shared transport format is defined in
[EPD2-SPECIFICATION.md](./EPD2-SPECIFICATION.md).

## Requirements

- PHP 8.3 or newer
- PHP GD extension
- Writable `backend/storage/` directory

There are no third-party PHP dependencies.

The example image cache is keyed by content ID, width, height, ordered colors,
and date where applicable. A 10 x 10, one-color example is useful for protocol
tests. The image processor uses 1-bit-per-pixel color planes and LSB-first
packing. It prepends the 5-byte width/height/color-count header.

Content ID 0 produces the legacy example: a white background, the English
weekday name (for example `Tuesday`) in the upper text area, and the date in
`DD.MM.YYYY` format in the lower text area. At the reference 800 x 480 size,
the weekday is centered around y=190 and the date around y=300.

Content ID 1 produces the same layout, but with the German weekday name (for
example `Dienstag`) and the same date in `DD.MM.YYYY` format. Both variants
change their cache entry when the server date changes.

With exactly one supported logical color, both text blocks use that color.
With two or more supported colors, the weekday is red and the date is black;
additional colors from the third position onward are ignored. The display
supplies the plane order through the pipe-separated color string.

## API

- `POST /api/display/current` (also `GET`) returns `hash`, `response_id`,
  `width`, `height`, `format`, `size`, and `url`.
- `POST /api/display/image` (also `GET`) requires `response_id` and returns
  the matching EPD2 body as `application/octet-stream`.

Metadata requests accept `CONTENT_ID`/`content_id`, `WIDTH`/`width`,
`HEIGHT`/`height`, and `COLORS`/`colors`. The image size is computed from
these values and is no longer fixed.

Both endpoints require the `X-API-Key` HTTP header or the `API_KEY`/`api_key`
field. Invalid or missing keys receive `403 Forbidden`. The image endpoint
requires a valid `response_id`; missing or unknown IDs receive `404`.
ETag/If-None-Match support remains optional.

Configure the backend key in the backend configuration. Replace any
development key before deployment and keep production configuration private.
Do not send keys in URL parameters or write them to logs. Use HTTPS in
production.

Example request (replace the placeholder with the configured key):

```sh
curl -X POST \
  -H 'X-API-Key: YOUR_CONFIGURED_KEY' \
  -d 'CONTENT_ID=1&WIDTH=800&HEIGHT=480&COLORS=black|red' \
  https://example.invalid/api/display/current

curl -X POST \
  -H 'X-API-Key: YOUR_CONFIGURED_KEY' \
  -d 'RESPONSE_ID=RESPONSE_ID_FROM_METADATA' \
  https://example.invalid/api/display/image
```

The backend does not provide an upload or image-generation API.
