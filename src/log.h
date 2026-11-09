// Logging: to the USB serial port (quiet while a screenshot is being sent), and into the last 32 KB kept in PSRAM
// for the remote's web page (/log).
#pragma once

#include <Arduino.h>

#include <string>

extern volatile bool g_serialBusy;

// Sets up the kept log. Call first thing in setup(); lines logged before it go to serial only.
void log_begin();

// printf to the log
void remote_log(const char* format, ...) __attribute__((format(printf, 1, 2)));

// The kept log from `from` (a position from an earlier call; 0 for all of it), and `from` moved to its end.
std::string log_since(uint64_t& from);

#define LOGF(...) remote_log(__VA_ARGS__)
