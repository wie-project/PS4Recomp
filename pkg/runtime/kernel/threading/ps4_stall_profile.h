// Aggregation of thread samples taken during a frame stall (PS4_STALL_PROFILE): samples are
// stored raw while the stall lasts; the report symbolizes them, counts the leaf host function of
// every sample per thread and prints the most frequent ones.
#pragma once
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STALL_PROFILE_MAX_THREADS 64
#define STALL_PROFILE_DEPTH 8
// Samples kept per stall (all threads together); later samples count in totals only.
#define STALL_PROFILE_POOL 32768

// Resolves pc to the start address and name of its function; returns 0 when unknown.
typedef int (*stall_symbolize_fn)(uint64_t pc, uint64_t *func_start, const char **name);

typedef struct StallProfile StallProfile;

StallProfile *stall_profile_create(stall_symbolize_fn sym);
void stall_profile_destroy(StallProfile *p);
void stall_profile_reset(StallProfile *p);
// One sample of thread `slot`: pcs[0] is the PC, pcs[1..n-1] the return addresses (innermost
// first). Only copies the pcs into the pool allocated at create: never symbolizes, never
// allocates, never takes a lock.
void stall_profile_add(StallProfile *p, int slot, const char *thread_name, const uint64_t *pcs, int n);
// Records the cumulative CPU time (user + system, microseconds) of thread `slot`, read at the
// moment of a sample. The first and the last reading of a stall give the CPU the thread really
// got while it was sampled; "busy" alone cannot tell a running thread from a runnable one that
// is starved. Call it together with stall_profile_add.
void stall_profile_thread_cpu(StallProfile *p, int slot, uint64_t cpu_us);
// Cumulative CPU time (user + system) of a pthread in microseconds; 0 when it cannot be read.
uint64_t stall_profile_thread_cpu_us(pthread_t th);
// Brackets a stall: begin records the CPU of this process and of every other process, end stores
// what each of them used since, for the report header. Not thread safe; one thread calls both.
void stall_profile_cpu_begin(StallProfile *p);
void stall_profile_cpu_end(StallProfile *p);
// Prints "[stall-profile]" lines: the stall, then every thread that was busy for at least 5 %
// of its samples (and "Frame Submission" always) with its `top` most frequent functions.
// A thread with at least two CPU readings gets ", cpu <ms> ms" after its busy share; it covers
// the sampled window of that thread (first to last sample), not the whole stall. The process
// figure covers the time between stall_profile_cpu_begin and _end. "other processes" is a
// lower bound: proc_pidinfo only answers for processes of the same user, others count as 0.
// The header gets ", process cpu <ms> ms, other processes <ms> ms (top <name> <ms> ms)" when it was set.
// Symbolizes the stored samples on the calling thread.
void stall_profile_report(const StallProfile *p, FILE *out, double stall_ms, int top);
// Same, with " (watchdog max gap G ms)" appended to the header when max_gap_ms >= 0.
void stall_profile_report_gap(const StallProfile *p, FILE *out, double stall_ms, int top, double max_gap_ms);

#ifdef __cplusplus
}
#endif
