#pragma once

#include <stdint.h>

typedef struct
{
  const uint8_t *table;
  uint16_t Width;
  uint16_t Height;
} sFONT;

extern sFONT Font16;
