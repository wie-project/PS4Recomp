#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <orbis/libkernel.h>

int main(void) {
    printf("[PS4 Timers & CPU Sys Test] Starting...\n");

    // 1. Process Time Counter Frequency
    uint64_t freq = sceKernelGetProcessTimeCounterFrequency();
    printf("[+] sceKernelGetProcessTimeCounterFrequency = %llu Hz\n", (unsigned long long)freq);
    if (freq != 1000000000ULL) {
        printf("ERROR: Unexpected timer frequency: %llu\n", (unsigned long long)freq);
        return 1;
    }

    // 2. High-Resolution Timer Measurement
    uint64_t t1 = sceKernelGetProcessTimeCounter();
    sceKernelUsleep(30000); // 30 ms
    uint64_t t2 = sceKernelGetProcessTimeCounter();
    uint64_t diff = t2 - t1;
    printf("[+] t1 = %llu ns, t2 = %llu ns, elapsed = %llu ns (%.2f ms)\n",
           (unsigned long long)t1, (unsigned long long)t2, (unsigned long long)diff, (double)diff / 1000000.0);
    if (t2 <= t1 || diff < 20000000ULL || diff > 100000000ULL) {
        printf("ERROR: Process timer measurement out of expected range: %llu ns\n", (unsigned long long)diff);
        return 1;
    }

    // 3. TSC Frequency & Read TSC
    uint64_t tscFreq = sceKernelGetTscFrequency();
    printf("[+] sceKernelGetTscFrequency = %llu Hz\n", (unsigned long long)tscFreq);
    if (tscFreq != 2130000000ULL) {
        printf("ERROR: Unexpected TSC frequency: %llu\n", (unsigned long long)tscFreq);
        return 1;
    }
    uint64_t tsc1 = sceKernelReadTsc();
    sceKernelUsleep(10000); // 10 ms
    uint64_t tsc2 = sceKernelReadTsc();
    if (tsc2 <= tsc1) {
        printf("ERROR: TSC counter did not advance: tsc1=%llu, tsc2=%llu\n",
               (unsigned long long)tsc1, (unsigned long long)tsc2);
        return 1;
    }
    printf("[+] TSC advanced: %llu -> %llu (delta = %llu)\n",
           (unsigned long long)tsc1, (unsigned long long)tsc2, (unsigned long long)(tsc2 - tsc1));

    // 4. Time of Day
    OrbisKernelTimeval tv = {0};
    int rc = sceKernelGettimeofday(&tv);
    if (rc != 0 || tv.tv_sec < 1600000000LL) {
        printf("ERROR: sceKernelGettimeofday failed or invalid: rc=%d, tv_sec=%lld\n", rc, (long long)tv.tv_sec);
        return 1;
    }
    printf("[+] sceKernelGettimeofday: sec = %lld, usec = %lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);

    // 5. CPU Mode & Neo Mode
    int isNeo = sceKernelIsNeoMode();
    int cpuMode = sceKernelGetCpumode();
    int currentCpu = sceKernelGetCurrentCpu();
    printf("[+] sceKernelIsNeoMode = %d, sceKernelGetCpumode = %d, currentCpu = %d\n", isNeo, cpuMode, currentCpu);
    if (isNeo != 1 || cpuMode != 1) {
        printf("ERROR: Neo or CPU mode mismatch: isNeo=%d, cpuMode=%d\n", isNeo, cpuMode);
        return 1;
    }

    // 6. System Software Version
    OrbisKernelSwVersion swVer = {0};
    rc = sceKernelGetSystemSwVersion(&swVer);
    if (rc != 0 || swVer.Version != 0x09000001) {
        printf("ERROR: sceKernelGetSystemSwVersion failed or mismatch: rc=%d, ver=0x%x\n", rc, swVer.Version);
        return 1;
    }
    printf("[+] System Software Version: 0x%08x (\"%s\")\n", swVer.Version, swVer.VersionString);

    // 7. Configured Flexible Memory Size
    uint64_t flexMemSize = 0;
    extern int sceKernelConfiguredFlexibleMemorySize(uint64_t *sizeOut);
    rc = sceKernelConfiguredFlexibleMemorySize(&flexMemSize);
    if (rc != 0 || flexMemSize != (512ULL * 1024 * 1024)) {
        printf("ERROR: sceKernelConfiguredFlexibleMemorySize failed: rc=%d, size=%llu\n", rc, (unsigned long long)flexMemSize);
        return 1;
    }
    printf("[+] sceKernelConfiguredFlexibleMemorySize = %llu MB\n", (unsigned long long)(flexMemSize / (1024 * 1024)));

    // 8. System Service Parameters
    extern int sceSystemServiceParamGetInt(int paramId, int *value);
    int lang = 0;
    rc = sceSystemServiceParamGetInt(1 /* Lang */, &lang);
    if (rc != 0 || lang != 1) {
        printf("ERROR: sceSystemServiceParamGetInt(Lang) failed: rc=%d, lang=%d\n", rc, lang);
        return 1;
    }
    int enterBtn = 0;
    rc = sceSystemServiceParamGetInt(1000 /* EnterButtonAssign */, &enterBtn);
    if (rc != 0 || enterBtn != 1) {
        printf("ERROR: sceSystemServiceParamGetInt(EnterButton) failed: rc=%d, btn=%d\n", rc, enterBtn);
        return 1;
    }
    printf("[+] sceSystemServiceParamGetInt: Lang=%d, EnterButton=%d\n", lang, enterBtn);

    printf("[SUCCESS] All PS4 Process Timers, System and Memory tests PASSED!\n");
    return 0;
}
