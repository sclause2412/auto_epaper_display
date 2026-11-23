#include "epd2_stream.h"

#include <limits.h>

bool calculateEpd2Sizes(uint16_t width, uint16_t height, uint8_t colorCount,
                        Epd2Sizes &sizes)
{
  if (width == 0 || height == 0 || colorCount == 0 ||
      static_cast<size_t>(width) > SIZE_MAX / static_cast<size_t>(height))
  {
    return false;
  }

  const size_t pixels = static_cast<size_t>(width) * height;
  if (pixels > SIZE_MAX - 7)
  {
    return false;
  }

  const size_t planeBytes = (pixels + 7) / 8;
  if (planeBytes > (SIZE_MAX - 5) / colorCount)
  {
    return false;
  }

  sizes.planeBytes = planeBytes;
  sizes.bodyBytes = 5 + planeBytes * colorCount;
  return true;
}

PlaneChunkSplit splitPlaneChunk(size_t totalOffset, size_t chunkLength,
                                size_t planeBytes, uint8_t colorCount)
{
  PlaneChunkSplit split{};
  if (planeBytes == 0 || colorCount == 0 ||
      planeBytes > SIZE_MAX / colorCount)
  {
    return split;
  }

  const size_t totalBytes = planeBytes * colorCount;
  if (totalOffset >= totalBytes || chunkLength > totalBytes - totalOffset)
    return split;

  split.valid = true;
  if (totalOffset < planeBytes)
  {
    split.firstOffset = totalOffset;
    split.firstLength = chunkLength < planeBytes - totalOffset
                            ? chunkLength
                            : planeBytes - totalOffset;
    split.secondLength = chunkLength - split.firstLength;
    split.secondOffset = 0;
  }
  else
  {
    split.secondOffset = totalOffset - planeBytes;
    split.secondLength = chunkLength;
  }
  return split;
}

uint8_t reverseByteBits(uint8_t value)
{
  value = static_cast<uint8_t>((value >> 4) | (value << 4));
  value = static_cast<uint8_t>(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
  return static_cast<uint8_t>(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
}
