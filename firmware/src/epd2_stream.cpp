#include "epd2_stream.h"

PlaneChunkSplit splitPlaneChunk(size_t totalOffset, const uint8_t *chunk, size_t chunkLength)
{
  (void)chunk;

  PlaneChunkSplit split{};
  if (totalOffset >= EPD2_IMAGE_BYTES || chunkLength > EPD2_IMAGE_BYTES - totalOffset)
  {
    split.blackLength = 0;
    split.redLength = 0;
    return split;
  }

  if (totalOffset < EPD2_PLANE_BYTES)
  {
    split.blackOffset = totalOffset;
    split.blackLength = chunkLength;
    if (totalOffset + chunkLength > EPD2_PLANE_BYTES)
    {
      split.blackLength = EPD2_PLANE_BYTES - totalOffset;
      split.redOffset = 0;
      split.redLength = chunkLength - split.blackLength;
    }
  }
  else
  {
    split.redOffset = totalOffset - EPD2_PLANE_BYTES;
    split.redLength = chunkLength;
  }
  return split;
}

uint8_t reverseByteBits(uint8_t value)
{
  value = static_cast<uint8_t>((value >> 4) | (value << 4));
  value = static_cast<uint8_t>(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
  return static_cast<uint8_t>(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
}
