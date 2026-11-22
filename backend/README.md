# EPD2 PHP backend

This intentionally small example generates an 800 x 480 daily image and exposes it in the EPD2 transport format.

## Requirements

- PHP 8.3 or newer with GD
- Writable `storage/` directory

There are no third-party PHP dependencies.

The Docker image uses the PHP 8.3 Apache base image.

## Generated example

The first request each day generates a centered weekday in large red text and the `DD.MM.YYYY` date in black. It uses GD's built-in bitmap font, enlarged in the PNG, so no system font or font download is needed. The PNG, converted EPD file, and date marker are cached in `storage/`. Later requests reuse the files; the next date causes regeneration.

The image processor maps pixels deterministically to black, white, and red. Each plane uses LSB-first packing within a byte. White sets neither plane, black sets the black plane, and red sets the red plane. This convention is an implementation default, not hardware-verified.

## API

- `GET /api/display/current` returns `{ "hash", "width", "height", "format", "size", "url" }`
- `GET /api/display/image` returns exactly 96,000 bytes with `application/octet-stream`

Both endpoints require the `X-API-Key` HTTP header. The development key is configured in `config.php`; replace it before deployment. If the source repository is public, keep the production configuration private. Requests without a valid key receive `403 Forbidden`. Use HTTPS and do not send the key in a URL or write it to logs.

```sh
curl -H 'X-API-Key: YOUR_CONFIGURED_KEY' https://example.invalid/api/display/current
```

There is no upload or image-generation API.

The image endpoint supports `ETag` and `If-None-Match`. Metadata calculates the SHA-256 directly from the cached EPD file.
