#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <orbis/libkernel.h>

std::mutex print_mutex;
std::atomic<int> shared_counter(0);

void thread_worker(int id, int iterations) {
    {
        std::lock_guard<std::mutex> lock(print_mutex);
        printf("Thread %d started!\n", id);
    }

    for (int i = 0; i < iterations; i++) {
        shared_counter.fetch_add(1, std::memory_order_relaxed);
        sceKernelUsleep(10000); // 10ms
    }

    {
        std::lock_guard<std::mutex> lock(print_mutex);
        printf("Thread %d finished its %d iterations.\n", id, iterations);
    }
}

int main() {
    // Disable stdout buffering for immediate output
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("main: Starting multi-threading test...\n");

    const int iterations_per_thread = 50;
    std::thread t1(thread_worker, 1, iterations_per_thread);
    std::thread t2(thread_worker, 2, iterations_per_thread);

    printf("main: Spawned worker threads, waiting for join...\n");
    t1.join();
    t2.join();

    int final_count = shared_counter.load();
    printf("main: Both threads joined. Final counter = %d (expected %d)\n",
           final_count, iterations_per_thread * 2);

    if (final_count == iterations_per_thread * 2) {
        printf("main: Multi-threading test PASS\n");
    } else {
        printf("main: Multi-threading test FAIL (count mismatch)\n");
    }

    printf("main: Done. Infinitely looping...\n");
    for (;;) {
        sceKernelUsleep(1000000);
    }

    return 0;
}
