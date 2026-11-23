#pragma once

#include <Arduino.h>

class EpdHardware
{
public:
  virtual bool begin() = 0;
  virtual bool writeBlackChunk(const uint8_t *data, size_t length, size_t blackOffset) = 0;
  virtual bool writeRedChunk(const uint8_t *data, size_t length, size_t redOffset) = 0;
  virtual bool refreshAndWait() = 0;
  virtual bool sleep() = 0;
  virtual void showStartupSettings(const char *ssid, const char *apiUrl,
                                   const char *apiKey) = 0;
  virtual void showSetupMessage(const char *ssid, const char *password) = 0;
  virtual void showTimeoutMessage() = 0;
  virtual void showApiKeyErrorMessage() = 0;
  virtual void showServerUnavailableMessage() = 0;
};
