// SPDX-License-Identifier: GPL-2.0-or-later
#include "ps4_gnm_pending_writes.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

using ps4gnm::GnmPendingWrites;
static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void Snapshots() {
    GnmPendingWrites writes;
    writes.Note(0x1000, 32, 1);
    writes.Commit(1);
    GnmPendingWrites::Range hit;
    uint64_t seq;
    Check(writes.OverlapsAfter(0x1008, 4, 0, &hit, &seq) && seq == 1, "completed since snapshot still overlaps");
    Check(!writes.OverlapsAfter(0x1008, 4, 1, &hit, &seq), "write completed before snapshot is excluded");
    writes.Note(0x1000, 32, 2);
    Check(writes.OverlapsAfter(0x1008, 4, 1, &hit, &seq) && seq == GnmPendingWrites::kOpen,
          "a repeated range in an open command buffer still overlaps");
    writes.Commit(2);
    writes.Prune(1);
    Check(writes.OverlapsAfter(0x1008, 4, 1, &hit, &seq) && seq == 2 && hit.epoch == 2,
          "pruning old writes preserves the newer write");
    writes.Prune(2);
    Check(!writes.OverlapsAfter(0x1008, 4, 2), "oldest snapshot permits pruning the write");
    writes.Note(0x1000, 32, 3);
    Check(writes.Overlaps(0x1008, 4), "pending-write query still sees a reused entry");
    Check(!writes.OverlapsAfter(0x1020, 1, 0), "adjacent ranges do not overlap");
}

static void Queues() {
    GnmPendingWrites writes[2];
    writes[0].Note(0x1000, 32, 1);
    writes[1].Note(0x1000, 32, 2);
    writes[1].Commit(1);
    uint64_t seq;
    Check(writes[0].OverlapsAfter(0x1000, 32, 1, nullptr, &seq) && seq == GnmPendingWrites::kOpen,
          "another queue's commit does not hide an open write");
    Check(!writes[1].OverlapsAfter(0x1000, 32, 1), "completed queue excluded by snapshot");
    writes[0].Commit(2);
    for (auto& w : writes) w.Prune(1);
    Check(writes[0].OverlapsAfter(0x1000, 32, 1, nullptr, &seq) && seq == 2,
          "queues committed in reverse recording order preserve newest sequence");
}

static void Randomized() {
    struct Write { uint64_t addr, size, epoch, seq; unsigned queue; };
    GnmPendingWrites writes[2] = {GnmPendingWrites(8), GnmPendingWrites(8)};
    std::vector<Write> scan;
    std::mt19937_64 rng(0x73004);
    uint64_t epoch = 0, committed = 0, floor = 0;
    unsigned queries = 0;
    for (unsigned step = 0; step < 30000; ++step) {
        const unsigned q = rng() % 2, op = rng() % 8;
        if (op < 4) {
            const uint64_t addr = (rng() % 256) * (1ull << 20) + (rng() % 4) * 16;
            const uint64_t size = step % 97 == 0 ? 80ull << 20 : 16 + rng() % 65536;
            writes[q].Note(addr, size, ++epoch);
            scan.push_back({addr, size, epoch, GnmPendingWrites::kOpen, q});
        } else if (op == 4) {
            writes[q].Commit(++committed);
            for (auto& w : scan) if (w.queue == q && w.seq == GnmPendingWrites::kOpen) w.seq = committed;
        } else if (op == 5) {
            floor += rng() % (committed - floor + 1);
            for (auto& w : writes) w.Prune(floor);
            scan.erase(std::remove_if(scan.begin(), scan.end(), [&](const Write& w) {
                return w.seq != GnmPendingWrites::kOpen && w.seq <= floor;
            }), scan.end());
        } else {
            const uint64_t addr = (rng() % 256) * (1ull << 20) + rng() % 128;
            const uint64_t size = step % 13 == 0 ? 96ull << 20 : 1 + rng() % 65536;
            const uint64_t snap = floor + rng() % (committed - floor + 1);
            bool expected = false;
            uint64_t newest = 0;
            for (const auto& w : scan) {
                if (w.seq != GnmPendingWrites::kOpen && w.seq <= snap) continue;
                if (w.addr < addr + size && addr < w.addr + w.size) {
                    expected = true;
                    newest = std::max(newest, w.seq);
                }
            }
            bool actual = false;
            uint64_t indexed_newest = 0;
            for (auto& index : writes) {
                GnmPendingWrites::Range hit;
                uint64_t seq;
                if (!index.OverlapsAfter(addr, size, snap, &hit, &seq)) continue;
                actual = true;
                indexed_newest = std::max(indexed_newest, seq);
                const bool valid_hit = std::any_of(scan.begin(), scan.end(), [&](const Write& w) {
                    return w.addr == hit.addr && w.size == hit.size && w.epoch == hit.epoch &&
                        (w.seq == GnmPendingWrites::kOpen || w.seq > snap) &&
                        w.addr < addr + size && addr < w.addr + w.size;
                });
                Check(valid_hit, "indexed hit belongs to the retained write history");
            }
            Check(actual == expected && indexed_newest == newest, "index agrees with the full write-history scan");
            ++queries;
        }
    }
    Check(queries > 5000 && writes[0].Compactions() + writes[1].Compactions() > 0,
          "randomized test exercises queries and compaction");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!std::strcmp(argv[1], "snapshots")) Snapshots();
    else if (!std::strcmp(argv[1], "queues")) Queues();
    else if (!std::strcmp(argv[1], "randomized")) Randomized();
    else return 2;
    std::printf("PASS %s\n", argv[1]);
}
