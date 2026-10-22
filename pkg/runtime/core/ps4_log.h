#ifndef PS4_LOG_H
#define PS4_LOG_H

// Asynchronous stdout/stderr (see ps4_log.c). Restores direct writes and waits until everything
// written so far has reached the original files. Async-signal-safe; safe to call repeatedly and
// from several threads.
void ps4_log_drain_sync(double timeout_s);

#endif
