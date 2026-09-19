#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <sys/mman.h>
#include <orbis/libkernel.h>

// 1. Sieve of Eratosthenes up to 1,000,000 (mathematically 78498 primes)
static int test_sieve() {
    const int MAX_PRIME = 1000000;
    std::vector<uint8_t> is_prime(MAX_PRIME + 1, 1);
    is_prime[0] = 0;
    is_prime[1] = 0;

    for (int p = 2; p * p <= MAX_PRIME; p++) {
        if (is_prime[p]) {
            for (int i = p * p; i <= MAX_PRIME; i += p) {
                is_prime[i] = 0;
            }
        }
    }

    int prime_count = 0;
    for (int i = 2; i <= MAX_PRIME; i++) {
        if (is_prime[i]) {
            prime_count++;
        }
    }
    return prime_count;
}

// 2. Matrix Multiplication (128x128 floating point)
static double test_matrix_mult() {
    const int N = 128;
    std::vector<double> A(N * N, 1.0);
    std::vector<double> B(N * N, 2.0);
    std::vector<double> C(N * N, 0.0);

    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            double aik = A[i * N + k];
            for (int j = 0; j < N; j++) {
                C[i * N + j] += aik * B[k * N + j];
            }
        }
    }

    double total = 0.0;
    for (int i = 0; i < N * N; i++) {
        total += C[i];
    }
    return total;
}

// 3. In-place QuickSort on 50,000 integers
static void quicksort(uint64_t *arr, int low, int high) {
    if (low < high) {
        uint64_t pivot = arr[high];
        int i = low - 1;
        for (int j = low; j < high; j++) {
            if (arr[j] <= pivot) {
                i++;
                uint64_t temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
        uint64_t temp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = temp;

        int pi = i + 1;
        quicksort(arr, low, pi - 1);
        quicksort(arr, pi + 1, high);
    }
}

static bool test_quicksort() {
    const int N = 50000;
    std::vector<uint64_t> data(N);
    uint64_t lcg = 0x123456789ABCDEF0ULL;
    for (int i = 0; i < N; i++) {
        lcg = lcg * 6364136223846793005ULL + 1442695040888963407ULL;
        data[i] = lcg;
    }

    quicksort(data.data(), 0, N - 1);

    for (int i = 1; i < N; i++) {
        if (data[i - 1] > data[i]) {
            return false;
        }
    }
    return true;
}

// 4. Dynamic container churn (5,000 iterations)
static bool test_container_churn() {
    for (int i = 0; i < 5000; i++) {
        std::vector<int> v;
        for (int j = 0; j < 64; j++) {
            v.push_back(i * 64 + j);
        }
        std::string s = "iteration_";
        s += std::to_string(i);
        if (v.size() != 64 || s.empty()) {
            return false;
        }
    }
    return true;
}

// 5. Large page-level mmap/munmap extent churn (100 cycles x 4MB = 400MB total churn)
static bool test_mmap_churn() {
    const size_t CHUNK_SZ = 4 * 1024 * 1024; // 4MB
    const int CYCLES = 100;
    void *first_addr = NULL;
    int reuse_count = 0;

    for (int c = 0; c < CYCLES; c++) {
        uint8_t *buf = (uint8_t *)mmap(NULL, CHUNK_SZ, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (buf == MAP_FAILED || buf == NULL) {
            printf("test_mmap_churn: mmap failed at cycle %d\n", c);
            return false;
        }

        if (c == 0) {
            first_addr = buf;
        } else if (buf == first_addr) {
            reuse_count++;
        }

        // Write pattern across all pages
        for (size_t i = 0; i < CHUNK_SZ; i += 4096) {
            buf[i] = (uint8_t)(c ^ (i & 0xFF));
        }

        // Verify pattern
        for (size_t i = 0; i < CHUNK_SZ; i += 4096) {
            if (buf[i] != (uint8_t)(c ^ (i & 0xFF))) {
                printf("test_mmap_churn: memory corruption at cycle %d, offset %zu\n", c, i);
                munmap(buf, CHUNK_SZ);
                return false;
            }
        }

        if (munmap(buf, CHUNK_SZ) != 0) {
            printf("test_mmap_churn: munmap failed at cycle %d\n", c);
            return false;
        }
    }

    printf("test_mmap_churn: Completed %d cycles of 4MB (%d MB churn). Address reused %d times!\n",
           CYCLES, (int)((CYCLES * CHUNK_SZ) / (1024 * 1024)), reuse_count);
    return (reuse_count > 0);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("main: Starting Aggressive Memory & Loop Stress Test...\n");

    // 1. Sieve
    printf("main: Running Sieve of Eratosthenes to 1,000,000...\n");
    int primes = test_sieve();
    printf("main: Primes found = %d (expected 78498) -> %s\n", primes, primes == 78498 ? "OK" : "FAIL");
    if (primes != 78498) return 1;

    // 2. Matrix Multiplication
    printf("main: Running 128x128 Matrix Multiplication (2M operations)...\n");
    double mat_sum = test_matrix_mult();
    printf("main: Matrix total sum = %llu (%.1f, expected 4194304) -> %s\n",
           (unsigned long long)mat_sum, mat_sum, (mat_sum == 4194304.0) ? "OK" : "FAIL");
    if (mat_sum != 4194304.0) return 2;

    // 3. QuickSort
    printf("main: Running QuickSort on 50,000 64-bit integers...\n");
    bool sort_ok = test_quicksort();
    printf("main: QuickSort verification -> %s\n", sort_ok ? "OK" : "FAIL");
    if (!sort_ok) return 3;

    // 4. Container Churn
    printf("main: Running 5,000 dynamic container allocation cycles...\n");
    bool churn_ok = test_container_churn();
    printf("main: Container churn verification -> %s\n", churn_ok ? "OK" : "FAIL");
    if (!churn_ok) return 4;

    // 5. Virtual Memory Extent Allocation & Munmap Reuse Churn
    printf("main: Running 400MB mmap/munmap virtual memory extent churn...\n");
    bool mmap_ok = test_mmap_churn();
    printf("main: VM extent reuse verification -> %s\n", mmap_ok ? "OK" : "FAIL");
    if (!mmap_ok) return 5;

    printf("\nmain: All memory and loop stress tests PASSED!\n");
    printf("main: Done. Infinitely looping...\n");
    for (;;) {
        sceKernelUsleep(1000000);
    }
    return 0;
}
