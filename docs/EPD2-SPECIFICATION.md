# EPD2 -- Shared Protocol Specification

**Status:** Draft / must be verified against the original driver and
physical hardware before implementation\
**Purpose:** Binding interface definition shared by the PHP backend and
ESP32 firmware.

## 1. Target hardware and image geometry

-   Display: Waveshare 7.5inch E-Paper (B), presumably V2.
-   Resolution: 800 × 480 pixels.
-   Color mode: black / white / red (three colors).
-   The exact display revision and controller must be confirmed using
    the actual hardware and original Waveshare driver.

## 2. Binary data format

The transport body is named **EPD2**. This minimal version has **no file
header**. Its layout and length are fixed:

    Offset         Length Content
  -------- -------------- ----------------
         0   48,000 bytes Black plane
    48,000   48,000 bytes Red plane
     Total   96,000 bytes Complete image

Each plane is a 1-bit bitmap with 800 × 480 pixels:

`800 × 480 / 8 = 48,000 bytes`

The black plane comes first, followed by the red plane. White is
represented by the corresponding pixel being unset in both planes.

## 3. Critical driver details -- verification required before implementation

The following properties are **not yet finalized**, because they depend
on the exact display controller and Waveshare driver in use:

1.  Bit order within each byte (MSB-first or LSB-first).
2.  Polarity of a set bit (does `1` mean colored or uncolored?).
3.  Memory layout and scan direction (pixel/row orientation).
4.  Whether the controller RAM accepts black and red plane data directly
    in this order.
5.  Controller commands, RAM addresses, window/address counters, and
    refresh sequence.
6.  Whether controller RAM survives ESP32 deep sleep or loss of power.

The firmware AI must determine these details from the unchanged original
Waveshare code for the exact display revision and record the findings in
the project documentation. The backend must generate the agreed logical
pixel and byte format. If the driver expects a different bit polarity or
byte order, the firmware must transform bytes while streaming with
constant memory usage, or the specification must be changed by mutual
agreement.

**Do not guess controller commands or bit polarity.** Commands such as
`0x10`, `0x13`, or `0x12` mentioned in earlier discussions are not a
confirmed specification.

## 4. Color mapping and quantization

The backend converts an arbitrary source image into an image of exactly
800 × 480 pixels with these three target classes:

-   Black
-   White
-   Red

The backend may scale and crop the image. It should use a documented
color-reduction strategy; dithering may be optional and configurable.
The exact threshold/quantization strategy must be documented and tested
with sample images.

The logical encoding for each pixel is:

  Target color     Black plane   Red plane
  -------------- ------------- -----------
  White                      0           0
  Black                      1           0
  Red                        0           1

This table describes the **logical** EPD2 encoding.
Electrical/controller bit polarity must be verified separately against
the original driver.

A pixel must never be set in both planes at the same time. If a source
color cannot be mapped unambiguously, the backend must apply a
deterministic mapping.

## 5. HTTP interface

### 5.1 Authentication

Both display endpoints require a shared API key in the HTTP request
header:

    X-API-Key: <configured-api-key>

The backend must compare the supplied value with its configured key
using a timing-safe comparison. A missing or invalid key returns
`403 Forbidden` with a JSON error response. Authentication is required
before processing `If-None-Match`; a matching ETag does not bypass the
key check.

Clients must not put the key in a URL or logs. Deployments should use
HTTPS with server-certificate validation and keep production keys out
of public source control. The example uses one shared key, not
per-device credentials or a key-rotation protocol.

### 5.2 Metadata

`GET /api/display/current`

Success response: `200 OK`, `Content-Type: application/json`

Example:

``` json
{
  "hash": "HEX_SHA256",
  "width": 800,
  "height": 480,
  "format": "EPD2",
  "size": 96000,
  "url": "/api/display/image"
}
```

Fields:

-   `hash`: SHA-256 of the complete 96,000-byte binary transport body.
-   `width`: `800`.
-   `height`: `480`.
-   `format`: `EPD2`.
-   `size`: `96000`.
-   `url`: path to the binary endpoint.

The hash must be returned as a 64-character hexadecimal SHA-256 value.

### 5.3 Binary image

`GET /api/display/image`

-   Status: `200 OK`
-   Content-Type: `application/octet-stream`
-   Body: exactly 96,000 bytes in the layout defined above.
-   No additional header, separators, JSON, or multipart wrapper in the
    body.

The endpoint should optionally support `ETag`. The client may send the
previously read hash as `If-None-Match`. If the image has not changed,
the server may return `304 Not Modified` with no body. The firmware must
not rely on ETag alone; the metadata hash remains the primary comparison
mechanism.

### 5.4 Metadata/image consistency

The server must ensure that the metadata hash and the delivered image
file belong to the same version. During publication, there must never be
a visible state where the new hash has been published but the old or
incomplete image file is still being served.

Recommended procedure:

1.  Generate the complete image.
2.  Validate its length and format.
3.  Calculate SHA-256 over exactly the final 96,000 bytes.
4.  Write the binary file to a temporary file.
5.  Atomically rename the temporary file to its active/versioned
    filename.
6.  Atomically update the metadata, or derive metadata from the active
    version.

If multiple files are involved, publication and reads must be designed
so a client can never receive an inconsistent combination of hash and
image. A versioned image file plus an atomically replaced manifest is
one possible approach.

## 6. Hash rules

-   Algorithm: SHA-256.
-   Input: exactly the 96,000 bytes of the EPD2 body, starting with the
    first byte of the black plane.
-   Output: 64 hexadecimal characters.
-   Do not include JSON metadata, HTTP headers, or filenames in the
    hash.
-   Publish a hash as the current version only after the image file has
    been fully generated and validated.

## 7. Client streaming requirements

The firmware must:

-   use a small fixed buffer, recommended size 512 bytes;
-   never allocate a 96,000-byte buffer or complete framebuffer in ESP32
    RAM;
-   receive data sequentially;
-   treat the first half (48,000 bytes) as the black plane and the
    second half (48,000 bytes) as the red plane;
-   pass received chunks directly to the appropriate display-controller
    RAM write function;
-   treat premature connection termination or an incorrect length as a
    failed transfer;
-   update the locally stored hash only after the complete download,
    data transfer, and display refresh have succeeded.

The exact way the display driver accepts data in chunks must be derived
from the original Waveshare code. If the existing API only accepts a
complete framebuffer, implement a streaming interface at the appropriate
low-level driver layer.

## 8. Error cases

-   Metadata endpoint unreachable: do not update the hash; retry on a
    later wake cycle.
-   Missing or invalid API key: server returns `403 Forbidden`; do not
    update the hash or attempt the image transfer.
-   Invalid JSON or unexpected format: abort.
-   Unchanged hash: no image transfer and no refresh.
-   Image length not exactly 96,000 bytes: treat as an error.
-   Network interruption during transfer: do not mark as successful; do
    not update the stored hash.
-   Controller/refresh error: do not store the hash as successfully
    displayed.
-   Missing or invalid server image: backend must return an error status
    and must not publish false metadata.

## 9. Change management

This file is the shared interface agreement. Any change to resolution,
color model, plane order, byte encoding, file length, hash calculation,
or API must first be documented here and then adopted by both backend
and firmware.

## 10. Acceptance tests

Generate and test at least these images on real hardware:

1.  All white.
2.  All black.
3.  All red.
4.  Four clearly separated color quadrants.
5.  Individual pixels at all four corners.
6.  Horizontal and vertical stripes.
7.  Checkerboard pattern.

These tests help verify bit order, polarity, row/column layout, plane
mapping, and image orientation.
