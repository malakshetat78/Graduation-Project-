#pragma once
// One printf-style log for all files, written to the USB Serial Monitor. Safe to call from several tasks.
void log_init();
int log_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Debug progress: where the program is right now.
//   step(3, 9, "Wi-Fi + time")  prints  ">>> STEP 3/9: Wi-Fi + time"  and remembers it.
//   where_am_i()                prints  "[NOW] STEP 3/9: Wi-Fi + time (in this step 12 s, board on 40 s)"
// The .ino calls where_am_i() every few seconds (ICVSP_DEBUG in config.h).
void step(int n, int total, const char* what);
void where_am_i();
