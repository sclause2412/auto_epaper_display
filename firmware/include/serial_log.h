#pragma once

#include <Arduino.h>

namespace SerialLog
{
  inline void begin(uint32_t baudRate)
  {
    Serial.begin(baudRate);
  }

  inline void info(const char *message)
  {
    Serial.printf("[INFO] %s\n", message);
  }

  inline void warn(const char *message)
  {
    Serial.printf("[WARN] %s\n", message);
  }

  inline void error(const char *message)
  {
    Serial.printf("[ERROR] %s\n", message);
  }

  template <typename... Args>
  inline void infof(const char *format, Args... args)
  {
    Serial.printf("[INFO] ");
    Serial.printf(format, args...);
    Serial.println();
  }

  template <typename... Args>
  inline void warnf(const char *format, Args... args)
  {
    Serial.printf("[WARN] ");
    Serial.printf(format, args...);
    Serial.println();
  }

  template <typename... Args>
  inline void errorf(const char *format, Args... args)
  {
    Serial.printf("[ERROR] ");
    Serial.printf(format, args...);
    Serial.println();
  }
}
