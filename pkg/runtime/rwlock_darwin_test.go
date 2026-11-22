//go:build darwin

// SPDX-License-Identifier: GPL-2.0-or-later

package recompruntime

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"
)

func TestRecursiveReadWithQueuedWriter(t *testing.T) {
	clang, err := exec.LookPath("clang")
	if err != nil {
		t.Skip("clang is required for the native rwlock regression")
	}

	dir := t.TempDir()
	source := filepath.Join(dir, "rwlock_test.c")
	const harness = `
#include "ps4_sync.c"
#include <assert.h>
#include <sched.h>

static RwlockNode lock = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .available = PTHREAD_COND_INITIALIZER,
};
static int writer_released_lock;

static void *write_lock(void *arg) {
    (void)arg;
    assert(guest_rwlock_write(&lock, 0) == 0);
    assert(lock.readers == 0);
    assert(guest_rwlock_unlock(&lock) == 0);
    writer_released_lock = 1;
    pthread_testcancel();
    return NULL;
}

static void *try_first_read(void *arg) {
    (void)arg;
    assert(guest_rwlock_read(&lock, 1) == EBUSY);
    return NULL;
}

int main(void) {
    assert(guest_rwlock_read(&lock, 0) == 0);
    pthread_t writer;
    assert(pthread_create(&writer, NULL, write_lock, NULL) == 0);
    for (;;) {
        pthread_mutex_lock(&lock.mutex);
        unsigned waiting = lock.waiting_writers;
        pthread_mutex_unlock(&lock.mutex);
        if (waiting)
            break;
        sched_yield();
    }

    pthread_t other_reader;
    assert(pthread_create(&other_reader, NULL, try_first_read, NULL) == 0);
    assert(pthread_join(other_reader, NULL) == 0);
    assert(pthread_cancel(writer) == 0);
    assert(guest_rwlock_read(&lock, 0) == 0);
    assert(guest_rwlock_read(&lock, 1) == 0);
    assert(guest_rwlock_unlock(&lock) == 0);
    assert(guest_rwlock_unlock(&lock) == 0);
    assert(guest_rwlock_write(&lock, 1) == EBUSY);
    assert(guest_rwlock_unlock(&lock) == 0);
    void *result;
    assert(pthread_join(writer, &result) == 0);
    assert(writer_released_lock && result == PTHREAD_CANCELED);
    return 0;
}
`
	if err := os.WriteFile(source, []byte(harness), 0600); err != nil {
		t.Fatal(err)
	}
	bin := filepath.Join(dir, "rwlock_test")
	cmd := exec.Command(clang, "-std=gnu11", "-O2", "-Wl,-dead_strip",
		"-Icore", "-Ikernel/sync", "-Ikernel/threading", source, "-o", bin)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("compile native rwlock regression: %v\n%s", err, out)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if out, err := exec.CommandContext(ctx, bin).CombinedOutput(); err != nil {
		t.Fatalf("native rwlock regression: %v\n%s", err, out)
	}
}
