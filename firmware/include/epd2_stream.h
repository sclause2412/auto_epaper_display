#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t EPD2_WIDTH = 800;
static constexpr size_t EPD2_HEIGHT = 480;
static constexpr uint8_t EPD2_COLOR_COUNT = 2;
static constexpr char EPD2_COLORS[] = "black|red";
static constexpr size_t EPD2_PLANE_BYTES = EPD2_WIDTH * EPD2_HEIGHT / 8;
static constexpr size_t EPD2_TRANSFER_BUFFER_SIZE = 512;

struct Epd2Sizes
{
  size_t planeBytes;
  size_t bodyBytes;
};

struct PlaneChunkSplit
{
  bool valid;
  size_t firstOffset;
  size_t firstLength;
  size_t secondOffset;
  size_t secondLength;
};

bool calculateEpd2Sizes(uint16_t width, uint16_t height, uint8_t colorCount,
                        Epd2Sizes &sizes);
PlaneChunkSplit splitPlaneChunk(size_t totalOffset, size_t chunkLength,
                                size_t planeBytes, uint8_t colorCount);
uint8_t reverseByteBits(uint8_t value);
