// Persistent Metal binary archive of the game's pipelines (their GPU code), so a pipeline built
// in an earlier session loads in about a millisecond. Earlier measurements showed the system
// Metal pipeline cache is not reliable across rebuilds of the game; this archive does not
// depend on it.
#pragma once
#import <Metal/Metal.h>
#include <cstdint>
#include <string>

namespace ps4gnm_pso_archive {
// Opens the archive files written earlier (<dir>/pipelines-<n>.metalar, only read) and their key
// index <dir>/pipelines_archived.bin (which records the keys of each file), and starts a new
// archive for additions. Each save writes the additions since the previous save into the next
// pipelines-<n>.metalar file; a save also starts after every 64 additions, the periodic save
// only once there are 16. The index starts with a header (OS build, GPU name and registry ID);
// on a different header or a corrupt index everything is deleted and a new archive started. With
// more than 128 files or more than budget_bytes in them, the oldest files and their keys are
// deleted down to 3/4 of both limits; an unreadable file is deleted with its keys. Once the files
// pass budget_bytes during a session, no more pipelines are added. Leftover temporary files of
// processes that no longer run are removed. Returns false (archive disabled) only when Metal
// cannot create an archive or the index cannot be written, or with PS4_GNM_NO_PSO_ARCHIVE=1
// (only the value "1").
// Only one process changes the directory: Open takes <dir>/pipelines.lock (flock, held until the
// process ends). If another process holds it, the archive opens lookup only: the files its index
// lists are attached and their keys known, but nothing is added, saved, evicted, rewritten or
// cleaned up (returns false only if the index is missing or not for this OS/GPU).
bool Open(id<MTLDevice> dev, const std::string& dir, uint64_t budget_bytes);
bool Enabled();
// True if Open found the directory owned by another process.
bool LookupOnly();
// Makes pipeline creation look in the archive first (descriptor.binaryArchives).
void Attach(MTLRenderPipelineDescriptor* d);
void Attach(MTLComputePipelineDescriptor* d);
// True if the pipeline with this stable key is in the archive or queued for it.
bool Contains(uint64_t stable_key);
// Adds the pipeline's GPU code to the archive on its queue (the descriptor is copied).
void Add(uint64_t stable_key, MTLRenderPipelineDescriptor* d);
void Add(uint64_t stable_key, MTLComputePipelineDescriptor* d);
// Writes the additions since the last save into a new archive file (temporary file + rename)
// after the queued additions; waits at most timeout_s. Returns false on error or timeout.
bool SaveNow(double timeout_s);
// Saves every period_s seconds while there are unsaved additions.
void StartPeriodicSave(double period_s);
} // namespace ps4gnm_pso_archive

// Called by ps4_host_quit (core/recomp_runtime.c, weak reference).
extern "C" void ps4_gnm_pso_archive_save_for_quit(double timeout_s);
