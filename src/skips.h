// The visualizer picker's skip marks: which tiles show SKIP while a hold's answer is on the way, and what to say when
// the Chart answers. Plain C++, so it's unit tested on the host (test/test_skips); ui.cpp draws what it says.
//
// The marks are the Chart's list (base: from /api/live, /api/status or a skip's answer) with each hold still waiting
// for its answer applied on top, in order. A hold is a skip or an unskip of one id, which the Chart does whether or not
// it was done already, so applying one to a list that has it already (a poll after the Chart did it) changes nothing.
// Each hold has the number MapClient::requestSkip() gave its command, and the answer carries it back, so an answer
// ends its own hold (and any older ones whose answers were overwritten before the screen saw them), never another's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "map_state.h"

namespace skips {

// A skip command still in MapClient's queue after this long is dropped, not sent (it waited out a Wi-Fi outage, or a
// queue of slow requests): MapClient answers it as failed instead.
constexpr uint32_t kQueuedMs = 20000;
// A hold's marks wait this long at most for the Chart's answer: a dropped command's answer comes by kQueuedMs, a sent
// one's by kQueuedMs and one request (3 s to connect, 5 s to answer) later. Past it the mark is undone.
constexpr uint32_t kWaitMs = 30000;
// After an answer, a poll's list isn't taken for this long (the status's list can be older than the answer)
constexpr uint32_t kSettleMs = 3000;

// What a hold on a tile does
enum class Hold {
  Ask,        // send it: requestSkip(id, !isSkipped(id)), then asked()
  OldChart,   // "Update the Chart to skip visualizers": the Chart doesn't say what it skips
  Exclusive,  // "<name>: always left out" (Rainbow, Chase)
};

// What to say about an answer, or a hold whose answer didn't come
enum class Said {
  Nothing,         // an answer to a hold given up on already (or a late failure): no message
  Skipped,         // "<name>: skipped"
  BackInRotation,  // "<name>: back in rotation"
  Last,            // "Can't skip the last one in rotation"
  Exclusive,       // "<name>: always left out"
  Failed,          // "Couldn't change <name>"
};

class Marks {
 public:
  // The Chart's list from a poll: known is false for a Chart that doesn't say. Not taken for kSettleMs after an answer.
  void polled(bool known, const std::vector<std::string>& ids, uint32_t now);

  // What a hold on a tile does (exclusive: Rainbow, Chase)
  Hold hold(bool exclusive) const;

  // The hold's command was queued as number `seq` (MapClient::requestSkip()'s): it's marked at once. 0 (the queue
  // was full) is nothing: no answer will come.
  void asked(uint32_t seq, const std::string& id, bool skip, uint32_t now);

  // The Chart's answer to command a.seq (a dropped one's too): it ends that hold and any older ones
  Said answered(const SkipAnswer& a, uint32_t now);

  // Holds whose answer didn't come within kWaitMs are given up, their marks undone; the ids, oldest first
  std::vector<std::string> expire(uint32_t now);

  bool known() const { return known_; }
  bool isSkipped(const std::string& id) const;
  const std::vector<std::string>& shown() const { return shown_; }
  bool waiting() const { return !pending_.empty(); }
  // Changes whenever known() or shown() do: redraw the tiles when it does
  uint32_t revision() const { return revision_; }

 private:
  struct Pending {
    uint32_t seq;
    std::string id;
    bool skip;
    uint32_t at;
  };
  void recompute();

  bool known_ = false;
  std::vector<std::string> base_;   // the Chart's list, as last heard
  std::vector<Pending> pending_;    // holds waiting for their answers, oldest first
  std::vector<std::string> shown_;  // base_ with pending_ applied
  bool settling_ = false;           // an answer came less than kSettleMs ago
  uint32_t settledAt_ = 0;          // when
  uint32_t revision_ = 0;
};

}  // namespace skips
