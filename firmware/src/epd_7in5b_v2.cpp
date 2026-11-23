#include "epd_7in5b_v2.h"

#include <stdio.h>
#include <string.h>

#include "epd2_stream.h"
#include "font16.h"
#include "serial_log.h"

namespace
{
  constexpr uint8_t PIN_SCK = 13;
  constexpr uint8_t PIN_MOSI = 14;
  constexpr uint8_t PIN_CS = 15;
  constexpr uint8_t PIN_RST = 26;
  constexpr uint8_t PIN_DC = 27;
  constexpr uint8_t PIN_BUSY = 25;

  constexpr uint8_t COLOR_BLACK = 0x00;
  constexpr size_t ROW_BYTES = EPD2_WIDTH / 8;
}

bool Epd7in5bV2::begin()
{
  pinMode(PIN_BUSY, INPUT);
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_CS, OUTPUT);

  digitalWrite(PIN_CS, HIGH);
  digitalWrite(PIN_SCK, LOW);
  initialized_ = initializeController();
  nextBlackOffset_ = 0;
  nextRedOffset_ = 0;
  return initialized_;
}

void Epd7in5bV2::reset()
{
  digitalWrite(PIN_RST, HIGH);
  delay(200);
  digitalWrite(PIN_RST, LOW);
  delay(2);
  digitalWrite(PIN_RST, HIGH);
  delay(200);
}

void Epd7in5bV2::sendByte(uint8_t data)
{
  for (uint8_t bit = 0; bit < 8; ++bit)
  {
    digitalWrite(PIN_MOSI, (data & 0x80) ? HIGH : LOW);
    data <<= 1;
    digitalWrite(PIN_SCK, HIGH);
    digitalWrite(PIN_SCK, LOW);
  }
}

void Epd7in5bV2::sendCommand(uint8_t command)
{
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  sendByte(command);
  digitalWrite(PIN_CS, HIGH);
}

void Epd7in5bV2::sendData(uint8_t data)
{
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  sendByte(data);
  digitalWrite(PIN_CS, HIGH);
}

bool Epd7in5bV2::waitUntilIdle()
{
  const uint32_t started = millis();
  while (millis() - started < BUSY_TIMEOUT_MS)
  {
    sendCommand(0x71);
    if (digitalRead(PIN_BUSY) == HIGH)
    {
      delay(200);
      return true;
    }
    delay(10);
  }
  return false;
}

bool Epd7in5bV2::initializeController()
{
  reset();

  sendCommand(0x01);
  sendData(0x07);
  sendData(0x07);
  sendData(0x3F);
  sendData(0x3F);

  sendCommand(0x04);
  delay(100);
  if (!waitUntilIdle())
    return false;

  sendCommand(0x00);
  sendData(0x0F);

  sendCommand(0x61);
  sendData(0x03);
  sendData(0x20);
  sendData(0x01);
  sendData(0xE0);

  sendCommand(0x15);
  sendData(0x00);

  sendCommand(0x50);
  sendData(0x11);
  sendData(0x07);

  sendCommand(0x60);
  sendData(0x22);

  sendCommand(0x65);
  sendData(0x00);
  sendData(0x00);
  sendData(0x00);
  sendData(0x00);
  return true;
}

bool Epd7in5bV2::writeBlackChunk(const uint8_t *data, size_t length, size_t blackOffset)
{
  if (!initialized_ || data == nullptr || blackOffset != nextBlackOffset_ ||
      length > EPD2_PLANE_BYTES - nextBlackOffset_)
  {
    return false;
  }
  if (blackOffset == 0)
    sendCommand(0x10);
  for (size_t i = 0; i < length; ++i)
    sendData(static_cast<uint8_t>(~data[i]));
  nextBlackOffset_ += length;
  return true;
}

bool Epd7in5bV2::writeRedChunk(const uint8_t *data, size_t length, size_t redOffset)
{
  if (!initialized_ || data == nullptr || nextBlackOffset_ != EPD2_PLANE_BYTES ||
      redOffset != nextRedOffset_ || length > EPD2_PLANE_BYTES - nextRedOffset_)
  {
    return false;
  }
  if (redOffset == 0)
  {
    sendCommand(0x92);
    sendCommand(0x13);
  }
  for (size_t i = 0; i < length; ++i)
    sendData(data[i]);
  nextRedOffset_ += length;
  return true;
}

bool Epd7in5bV2::refreshAndWait()
{
  if (!initialized_ || nextBlackOffset_ != EPD2_PLANE_BYTES ||
      nextRedOffset_ != EPD2_PLANE_BYTES)
  {
    return false;
  }
  sendCommand(0x12);
  delay(100);
  const bool completed = waitUntilIdle();
  if (completed)
  {
    nextBlackOffset_ = 0;
    nextRedOffset_ = 0;
  }
  return completed;
}

bool Epd7in5bV2::sleep()
{
  if (!initialized_)
    return false;
  sendCommand(0x02);
  if (!waitUntilIdle())
    return false;
  sendCommand(0x07);
  sendData(0xA5);
  initialized_ = false;
  return true;
}

void Epd7in5bV2::drawTextRow(uint8_t *row, size_t y, const char *text,
                             size_t x, size_t yStart)
{
  const size_t glyphBytesPerRow = (Font16.Width + 7) / 8;
  const size_t glyphBytes = Font16.Height * glyphBytesPerRow;
  if (y < yStart || y >= yStart + Font16.Height)
    return;

  for (size_t character = 0; text[character] != '\0'; ++character)
  {
    const unsigned char value = static_cast<unsigned char>(text[character]);
    if (value < ' ' || value > '~')
      continue;
    const size_t glyphOffset = (value - ' ') * glyphBytes;
    const size_t glyphRowOffset = glyphOffset + (y - yStart) * glyphBytesPerRow;
    for (size_t column = 0; column < Font16.Width; ++column)
    {
      const size_t pixelX = x + character * Font16.Width + column;
      if (pixelX >= EPD2_WIDTH)
        break;
      if ((Font16.table[glyphRowOffset + column / 8] & (0x80 >> (column % 8))) != 0)
      {
        row[pixelX / 8] |= static_cast<uint8_t>(0x80 >> (pixelX % 8));
      }
    }
  }
}

void Epd7in5bV2::drawCenteredTextRow(uint8_t *row, size_t y, const char *text,
                                     size_t yStart)
{
  const size_t textWidth = strlen(text) * Font16.Width;
  const size_t x = textWidth < EPD2_WIDTH ? (EPD2_WIDTH - textWidth) / 2 : 0;
  drawTextRow(row, y, text, x, yStart);
}

void Epd7in5bV2::drawTextImage(const char *const *lines, size_t lineCount,
                               size_t lineSpacing, int redLineIndex)
{
  uint8_t blackRow[ROW_BYTES];
  uint8_t redRow[ROW_BYTES];
  if (lineCount == 0)
    return;
  const size_t textHeight = (lineCount - 1) * lineSpacing + Font16.Height;
  const size_t firstLineY = (EPD2_HEIGHT - textHeight) / 2;

  sendCommand(0x10);
  for (size_t y = 0; y < EPD2_HEIGHT; ++y)
  {
    memset(blackRow, COLOR_BLACK, sizeof(blackRow));
    memset(redRow, COLOR_BLACK, sizeof(redRow));
    for (size_t line = 0; line < lineCount; ++line)
    {
      uint8_t *row = static_cast<int>(line) == redLineIndex ? redRow : blackRow;
      drawCenteredTextRow(row, y, lines[line], firstLineY + line * lineSpacing);
    }
    for (size_t x = 0; x < ROW_BYTES; ++x)
    {
      sendData(static_cast<uint8_t>(~blackRow[x]));
    }
  }

  sendCommand(0x92);
  sendCommand(0x13);
  for (size_t y = 0; y < EPD2_HEIGHT; ++y)
  {
    memset(redRow, COLOR_BLACK, sizeof(redRow));
    for (size_t line = 0; line < lineCount; ++line)
    {
      if (static_cast<int>(line) == redLineIndex)
      {
        drawCenteredTextRow(redRow, y, lines[line], firstLineY + line * lineSpacing);
      }
    }
    for (size_t x = 0; x < ROW_BYTES; ++x)
      sendData(redRow[x]);
  }

  sendCommand(0x12);
  delay(100);
  if (!waitUntilIdle())
  {
    SerialLog::error("E-paper refresh timed out.");
  }
}

void Epd7in5bV2::showStartupSettings(const char *ssid, const char *apiUrl,
                                     const char *apiKey)
{
  constexpr size_t MAX_LINE_CHARS = EPD2_WIDTH / 11;
  constexpr size_t MAX_LINES = 14;
  char lineStorage[MAX_LINES][MAX_LINE_CHARS + 1]{};
  const char *lines[MAX_LINES]{};
  size_t lineCount = 0;

  auto appendLine = [&](const char *text)
  {
    if (lineCount >= MAX_LINES)
      return;
    snprintf(lineStorage[lineCount], sizeof(lineStorage[lineCount]), "%s", text);
    lines[lineCount] = lineStorage[lineCount];
    ++lineCount;
  };
  auto appendWrapped = [&](const char *text)
  {
    const size_t length = strlen(text);
    for (size_t offset = 0; offset < length && lineCount < MAX_LINES;)
    {
      const size_t chunkLength =
          length - offset < MAX_LINE_CHARS ? length - offset : MAX_LINE_CHARS;
      memcpy(lineStorage[lineCount], text + offset, chunkLength);
      lineStorage[lineCount][chunkLength] = '\0';
      lines[lineCount] = lineStorage[lineCount];
      ++lineCount;
      offset += chunkLength;
    }
    if (length == 0)
      appendLine("");
  };

  appendLine("Current settings");
  char label[MAX_LINE_CHARS + 1];
  snprintf(label, sizeof(label), "SSID: %s", ssid);
  appendLine(label);
  appendLine("API URL:");
  appendWrapped(apiUrl);
  char apiKeyLabel[MAX_LINE_CHARS + 1];
  const size_t apiKeyLength = apiKey ? strlen(apiKey) : 0;
  const size_t previewLength = apiKeyLength < 4 ? apiKeyLength : 4;
  const size_t maskedLength =
      apiKeyLength > previewLength ? apiKeyLength - previewLength : 0;
  const size_t prefixLength = snprintf(apiKeyLabel, sizeof(apiKeyLabel),
                                       "API key: %.*s",
                                       static_cast<int>(previewLength),
                                       apiKey ? apiKey : "");
  size_t labelLength = prefixLength;
  for (size_t i = 0; i < maskedLength && labelLength < sizeof(apiKeyLabel) - 1;
       ++i)
  {
    apiKeyLabel[labelLength++] = '*';
  }
  apiKeyLabel[labelLength] = '\0';
  appendLine(apiKeyLabel);
  appendLine("Setup: Hold BOOT for 5 sec");
  appendLine("Image fetch begins in 1 minute");
  drawTextImage(lines, lineCount, 32, static_cast<int>(lineCount) - 2);
}

void Epd7in5bV2::showSetupMessage(const char *ssid, const char *password)
{
  const char *lines[] = {
      "Wi-Fi setup",
      "Connect to this hotspot:",
      ssid,
      "Password (WPA2):",
      password,
      "Open this link:",
      "http://192.168.4.1",
      "Hotspot closes after 10 minutes",
  };
  drawTextImage(lines, sizeof(lines) / sizeof(lines[0]));
}

void Epd7in5bV2::showTimeoutMessage()
{
  const char *lines[] = {
      "Please restart the display to enter Setup mode...",
      "Press EN to reboot.",
  };
  drawTextImage(lines, sizeof(lines) / sizeof(lines[0]), 42, 1);
}

void Epd7in5bV2::showApiKeyErrorMessage()
{
  const char *lines[] = {
      "API key rejected (HTTP 403).",
      "Press EN to reboot.",
  };
  drawTextImage(lines, sizeof(lines) / sizeof(lines[0]), 42, 1);
}

void Epd7in5bV2::showServerUnavailableMessage()
{
  const char *lines[] = {
      "Image server unavailable or 404.",
      "Ten attempts failed.",
      "Next retry in one hour.",
      "Press EN to reboot.",
  };
  drawTextImage(lines, sizeof(lines) / sizeof(lines[0]), 42, 3);
}
