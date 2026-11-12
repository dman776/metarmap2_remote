// Firmware updates from GitHub, on a background task: checks at startup and
// every 6 hours for the chosen channel, and installs on request (writing the
// spare firmware slot, checking its SHA-256, then restarting into it).
//
// The UI copies the state out with get(), and asks with setChannel(),
// check() and install().
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>

#include "settings.h"
#include "update_check.h"

struct UpdateState {
  enum Phase { Idle, Checking, Installing, Restarting };
  Phase phase = Idle;
  update::Channel channel = update::Channel::Releases;
  std::string branch;                      // for the Branch channel
  std::string release;                     // for the Release channel (Pick release): the tag, "" until one is picked
  bool checked = false;                    // a check has answered since the channel was set
  bool available = false;                  // the candidate is an update (newer): the star
  bool installable = false;                // UPDATE can install it: an update, or on Release an older release too
  update::Age age = update::Age::Unknown;  // Release: the picked release against the installed firmware
  std::string candidate;                   // the channel's newest version, update or not ("" when there's none)
  std::string error;                       // why the last check or install failed
  int progress = 0;                        // installing: 0-100
  std::vector<std::string> branches;       // branches with a build, for the Branch channel
  std::vector<std::string> releases;       // releases with the firmware file, newest first, for Release
};

class Updater {
 public:
  void begin(Settings& settings);

  bool get(UpdateState& out, uint32_t& rev);

  // Saved; checks again. `branch` is for Branch, `release` (a tag) for Release; both are kept whichever is chosen.
  void setChannel(update::Channel channel, const std::string& branch, const std::string& release);
  void check();
  void install();
  // The chosen branch and release (under the lock: the screen may be changing them).
  void choice(std::string& branch, std::string& release);

 private:
  enum Cmd : uint8_t { CHECK, INSTALL };

  static void taskEntry(void* self);
  void run();
  void doCheck();
  void doInstall();
  template <typename F>
  void change(F f);

  Settings* settings_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  UpdateState state_;
  update::Release candidate_;
  uint32_t rev_ = 0;
};
