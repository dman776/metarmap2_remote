// Why the last run ended, logged at startup (into the web page's /log): the chip's reset reason and, after a
// crash, the core dump's summary from flash (the task, where it was, a backtrace for addr2line), which is then
// erased so it's reported once.
#pragma once

// Call in setup(), right after log_begin().
void log_last_run();
