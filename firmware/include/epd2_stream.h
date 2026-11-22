#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t EPD2_WIDTH = 800;
static constexpr size_t EPD2_HEIGHT = 480;
static constexpr size_t EPD2_PLANE_BYTES = EPD2_WIDTH * EPD2_HEIGHT / 8;
static constexpr size_t EPD2_IMAGE_BYTES = EPD2_PLANE_BYTES * 2;
static constexpr size_t EPD2_TRANSFER_BUFFER_SIZE = 512;

enum class PlaneChunkResult : uint8_t
{
  Ok,
  TooManyBytes,
};

struct PlaneChunkSplit
{
  size_t blackOffset;
  size_t blackLength;
  size_t redOffset;
  size_t redLength;
};

PlaneChunkSplit splitPlaneChunk(size_t totalOffset, const uint8_t *chunk, size_t chunkLength);
uint8_t reverseByteBits(uint8_t value);
