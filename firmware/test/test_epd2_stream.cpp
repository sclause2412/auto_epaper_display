#include <unity.h>
#include "epd2_stream.h"
#include "../src/epd2_stream.cpp"

void test_plane_sizes() {
  TEST_ASSERT_EQUAL_UINT32(48000, EPD2_PLANE_BYTES);
  TEST_ASSERT_EQUAL_UINT32(96000, EPD2_IMAGE_BYTES);
  TEST_ASSERT_EQUAL_UINT32(512, EPD2_TRANSFER_BUFFER_SIZE);
}

void test_chunk_before_boundary() {
  const uint8_t data[16]{};
  auto split = splitPlaneChunk(47992, data, 16);
  TEST_ASSERT_EQUAL_UINT32(47992, split.blackOffset);
  TEST_ASSERT_EQUAL_UINT32(8, split.blackLength);
  TEST_ASSERT_EQUAL_UINT32(0, split.redOffset);
  TEST_ASSERT_EQUAL_UINT32(8, split.redLength);
}

void test_exact_black_boundary() {
  const uint8_t data[512]{};
  auto split = splitPlaneChunk(47500, data, 512);
  TEST_ASSERT_EQUAL_UINT32(47500, split.blackOffset);
  TEST_ASSERT_EQUAL_UINT32(500, split.blackLength);
  TEST_ASSERT_EQUAL_UINT32(0, split.redOffset);
  TEST_ASSERT_EQUAL_UINT32(12, split.redLength);
}

void test_chunk_in_red_plane() {
  const uint8_t data[32]{};
  auto split = splitPlaneChunk(48000, data, 32);
  TEST_ASSERT_EQUAL_UINT32(0, split.blackLength);
  TEST_ASSERT_EQUAL_UINT32(0, split.redOffset);
  TEST_ASSERT_EQUAL_UINT32(32, split.redLength);
}

void test_too_many_bytes_are_rejected() {
  const uint8_t data[512]{};
  auto split = splitPlaneChunk(95999, data, 512);
  TEST_ASSERT_EQUAL_UINT32(0, split.blackLength);
  TEST_ASSERT_EQUAL_UINT32(0, split.redLength);
}

void test_reverse_byte_bits() {
  TEST_ASSERT_EQUAL_HEX8(0x80, reverseByteBits(0x01));
  TEST_ASSERT_EQUAL_HEX8(0x96, reverseByteBits(0x69));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_plane_sizes);
  RUN_TEST(test_chunk_before_boundary);
  RUN_TEST(test_exact_black_boundary);
  RUN_TEST(test_chunk_in_red_plane);
  RUN_TEST(test_too_many_bytes_are_rejected);
  RUN_TEST(test_reverse_byte_bits);
  return UNITY_END();
}
