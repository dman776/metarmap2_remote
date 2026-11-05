// Serial logging that keeps quiet while a screenshot is being sent.
#pragma once

#include <Arduino.h>

extern volatile bool g_serialBusy;

#define LOGF(...)                                \
  do {                                           \
    if (!g_serialBusy) Serial.printf(__VA_ARGS__); \
  } while (0)
