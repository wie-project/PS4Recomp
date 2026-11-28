// SPDX-License-Identifier: GPL-2.0-or-later

package hosttest

import "testing"

// TestKernelTime runs testdata/kernel_time/main.c: libkernel's time functions against shadPS4's
// kernel/time.cpp (clock ids and errors, clock_getres, interruptible and resumed sleeps, time
// zone conversions, process counters).
func TestKernelTime(t *testing.T) {
	bin := buildHostCFlags(t, "kernel_time", []string{kernelInclude},
		"../runtime/modules/libkernel/ps4_kernel_time.c",
	)
	runHostCases(t, bin, []string{"clocks", "sleep", "interrupt", "timezone", "counters"})
}

// TestKernelSync runs testdata/kernel_sync/main.c: POSIX and Orbis semaphores and event flags
// against shadPS4's kernel/threads/semaphore.cpp and event_flag.cpp, with real waiting threads.
func TestKernelSync(t *testing.T) {
	bin := buildHostCFlags(t, "kernel_sync", []string{threadingInclude},
		"../runtime/kernel/sync/ps4_handle_table.c",
		"../runtime/kernel/sync/ps4_semaphore.c",
		"../runtime/kernel/sync/ps4_event_flag.c",
	)
	runHostCases(t, bin, []string{"posix-sem", "posix-sem-threads", "kernel-sema", "kernel-sema-order",
		"event-flag", "event-flag-threads"})
}

// TestUsbStorage runs testdata/usb_storage/main.c: libSceUsbStorage against shadPS4's
// usbd/usbstorage.cpp with the /mnt/usb0 mount of the real VFS.
func TestUsbStorage(t *testing.T) {
	bin := buildHostC(t, "usb_storage",
		"../runtime/modules/libSceUsbStorage/ps4_usb_storage.c",
		"../runtime/kernel/vfs/ps4_vfs.c",
		"../runtime/kernel/vfs/ps4_pak.c",
		"../runtime/kernel/vfs/ps4_datx.c",
	)
	runHostCases(t, bin, []string{"device", "map", "getdents"})
}

// TestKernelThreads runs testdata/kernel_threads/main.c: guest pthreads and their mutexes,
// condition variables, rwlocks, barriers and pthread_once against shadPS4's kernel/threads
// (pthread.cpp, pthread_attr.cpp, pthread_spec.cpp, pthread_clean.cpp, mutex.cpp, condvar.cpp,
// rwlock.cpp, barrier.cpp), on real host threads running ps4_threading.c's thread runner.
func TestKernelThreads(t *testing.T) {
	bin := buildHostCFlags(t, "kernel_threads", []string{kernelInclude},
		"../runtime/kernel/threading/ps4_threading.c",
		"../runtime/kernel/threading/ps4_pthread_attr.c",
		"../runtime/kernel/threading/ps4_stall_profile.c",
		"../runtime/kernel/sync/ps4_sync.c",
		"../runtime/kernel/sync/ps4_sync_objects.c",
		"../runtime/kernel/sync/ps4_barrier.c",
		"../runtime/kernel/sync/ps4_handle_table.c",
		"../runtime/kernel/sync/ps4_semaphore.c",
		"../runtime/modules/libkernel/ps4_kernel_time.c",
	)
	runHostCases(t, bin, []string{"attr", "create", "join", "keys", "cancel", "mutex", "cond", "rwlock",
		"barrier", "once", "suspend"})
}
