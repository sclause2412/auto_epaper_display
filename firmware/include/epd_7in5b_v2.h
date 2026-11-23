#pragma once

#include "hardware_contract.h"

class Epd7in5bV2 final : public EpdHardware
{
public:
  bool begin() override;
  bool writeBlackChunk(const uint8_t *data, size_t length, size_t blackOffset) override;
  bool writeRedChunk(const uint8_t *data, size_t length, size_t redOffset) override;
  bool refreshAndWait() override;
  bool sleep() override;
  void showStartupSettings(const char *ssid, const char *apiUrl,
                           const char *apiKey) override;
  void showSetupMessage(const char *ssid, const char *password) override;
  void showTimeoutMessage() override;
  void showApiKeyErrorMessage() override;
  void showServerUnavailableMessage(const char *retryMessage) override;

private:
  static constexpr uint32_t BUSY_TIMEOUT_SECONDS = 60;

  size_t nextBlackOffset_ = 0;
  size_t nextRedOffset_ = 0;
  bool initialized_ = false;

  void reset();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendByte(uint8_t data);
  bool waitUntilIdle();
  bool initializeController();
  void drawTextImage(const char *const *lines, size_t lineCount,
                     size_t lineSpacing = 42, int redLineIndex = -1);
  static void drawTextRow(uint8_t *row, size_t y, const char *text, size_t x, size_t yStart);
  static void drawCenteredTextRow(uint8_t *row, size_t y, const char *text, size_t yStart);
};
