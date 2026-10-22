#include "ps4_direct_mem.h"
#include "ps4_hitch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

// External NEO mode query
extern int32_t sceKernelIsNeoMode(void);

// ---------------------------------------------------------------------------------
// Data Structures for shadPS4-style Physical & Virtual Memory Management
// ---------------------------------------------------------------------------------

typedef struct PhysicalMemoryArea {
    uint64_t base;
    uint64_t size;
    int32_t memory_type;
    PhysicalMemoryType dma_type;
} PhysicalMemoryArea;

typedef struct PhysAreaNode {
    PhysicalMemoryArea area;
    struct PhysAreaNode *prev;
    struct PhysAreaNode *next;
} PhysAreaNode;

typedef struct PhysMap {
    PhysAreaNode *head;
    PhysAreaNode *tail;
} PhysMap;

typedef struct VMANode {
    uint64_t base;
    uint64_t size;
    VMAType type;
    MemoryProt prot;
    char name[32];
    int32_t fd;
    bool disallow_merge;
    PhysMap phys_areas;
    struct VMANode *prev;
    struct VMANode *next;
} VMANode;

typedef struct VMAMap {
    VMANode *head;
    VMANode *tail;
} VMAMap;

// Global Memory Manager State
static pthread_mutex_t g_mem_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_mem_inited = 0;

static uint64_t g_total_direct_size = 0;
static uint64_t g_total_flexible_size = 0;
static uint64_t g_flexible_usage = 0;
static uint64_t g_pool_budget = 0;

static PhysMap g_dmem_map = {0};
static PhysMap g_fmem_map = {0};
static VMAMap  g_vma_map = {0};

static uint8_t *g_phys_base = NULL;
static size_t   g_phys_size = 0;

__attribute__((weak)) void ps4_gnm_guest_remapped(uint64_t addr, uint64_t size) {
    (void)addr; (void)size;
}

static inline uint64_t align_up(uint64_t val, uint64_t align) {
    if (align == 0) return val;
    return (val + align - 1) & ~(align - 1);
}

static inline uint64_t align_down(uint64_t val, uint64_t align) {
    if (align == 0) return val;
    return val & ~(align - 1);
}

// ---------------------------------------------------------------------------------
// Physical Backing (mach_vm_remap on Apple Silicon)
// ---------------------------------------------------------------------------------
static void dm_phys_init(void) {
    if (g_phys_base || getenv("PS4_NO_PHYS_ALIAS")) return;
    size_t sz = 128ULL << 30; // 128 GB virtual reservation (demand-paged)
    void *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "[ps4-mem] WARNING: cannot reserve physical backing (%d); aliasing disabled\n", errno);
        return;
    }
    g_phys_base = (uint8_t *)p;
    g_phys_size = sz;
}

// Map guest [vaddr, vaddr+len) onto physical [phys, phys+len)
static int dm_alias(GuestContext *ctx, uint64_t vaddr, uint64_t phys, size_t len) {
#if defined(__APPLE__)
    if (!ctx) return -1;
    GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
    if (!g_phys_base || ((vaddr | phys | len) & 0x3fffULL) || phys + len > g_phys_size ||
        vaddr + len > proc->mem_size)
        return -1;
    mach_vm_address_t target = (mach_vm_address_t)(uintptr_t)(proc->mem_base + vaddr);
    vm_prot_t cur = 0, max = 0;
    kern_return_t kr;
    PS4_HITCH_TIMED(PS4_HITCH_VM, kr = mach_vm_remap(mach_task_self(), &target, len, 0,
                                                     VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                                                     (mach_vm_address_t)(uintptr_t)(g_phys_base + phys), FALSE,
                                                     &cur, &max, VM_INHERIT_SHARE));
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "[ps4-mem] mach_vm_remap(va=0x%llx phys=0x%llx len=0x%zx) failed: %d\n",
                (unsigned long long)vaddr, (unsigned long long)phys, len, kr);
        return -1;
    }
    if (ps4_gnm_guest_remapped) ps4_gnm_guest_remapped(vaddr, len);
    return 0;
#else
    (void)ctx; (void)vaddr; (void)phys; (void)len;
    return -1;
#endif
}

// ---------------------------------------------------------------------------------
// PhysMap Interval List Operations
// ---------------------------------------------------------------------------------

static bool can_merge_phys(const PhysicalMemoryArea *a, const PhysicalMemoryArea *b) {
    if (a->base + a->size != b->base) return false;
    if (a->memory_type != b->memory_type) return false;
    if (a->dma_type != b->dma_type) return false;
    return true;
}

static PhysAreaNode *find_phys_area(PhysMap *map, uint64_t addr) {
    PhysAreaNode *curr = map->head;
    PhysAreaNode *found = NULL;
    while (curr) {
        if (curr->area.base <= addr) {
            found = curr;
        } else {
            break;
        }
        curr = curr->next;
    }
    return found;
}

static PhysAreaNode *split_phys_area(PhysMap *map, PhysAreaNode *node, uint64_t offset_in_area) {
    if (!node || offset_in_area == 0 || offset_in_area >= node->area.size) return node;

    PhysAreaNode *new_node = (PhysAreaNode *)calloc(1, sizeof(PhysAreaNode));
    if (!new_node) return node;

    new_node->area = node->area;
    node->area.size = offset_in_area;
    new_node->area.base += offset_in_area;
    new_node->area.size -= offset_in_area;

    new_node->prev = node;
    new_node->next = node->next;
    if (node->next) node->next->prev = new_node;
    else map->tail = new_node;
    node->next = new_node;

    return new_node;
}

static PhysAreaNode *merge_phys_adjacent(PhysMap *map, PhysAreaNode *node) {
    if (!node) return NULL;
    // Try merge with next
    if (node->next && can_merge_phys(&node->area, &node->next->area)) {
        PhysAreaNode *next = node->next;
        node->area.size += next->area.size;
        node->next = next->next;
        if (next->next) next->next->prev = node;
        else map->tail = node;
        free(next);
    }
    // Try merge with prev
    if (node->prev && can_merge_phys(&node->prev->area, &node->area)) {
        PhysAreaNode *prev = node->prev;
        prev->area.size += node->area.size;
        prev->next = node->next;
        if (node->next) node->next->prev = prev;
        else map->tail = prev;
        free(node);
        node = prev;
    }
    return node;
}

static PhysAreaNode *carve_phys_area(PhysMap *map, uint64_t addr, uint64_t size) {
    PhysAreaNode *node = find_phys_area(map, addr);
    if (!node) return NULL;

    uint64_t start_in_area = addr - node->area.base;
    uint64_t end_in_area = start_in_area + size;

    if (end_in_area < node->area.size) {
        split_phys_area(map, node, end_in_area);
    }
    if (start_in_area > 0) {
        node = split_phys_area(map, node, start_in_area);
    }
    return node;
}

static void append_phys_area(PhysMap *map, uint64_t base, uint64_t size, int32_t mtype, PhysicalMemoryType dtype) {
    PhysAreaNode *node = (PhysAreaNode *)calloc(1, sizeof(PhysAreaNode));
    if (!node) return;
    node->area.base = base;
    node->area.size = size;
    node->area.memory_type = mtype;
    node->area.dma_type = dtype;
    if (map->tail) {
        map->tail->next = node;
        node->prev = map->tail;
        map->tail = node;
    } else {
        map->head = node;
        map->tail = node;
    }
}

static void clear_phys_map(PhysMap *map) {
    PhysAreaNode *curr = map->head;
    while (curr) {
        PhysAreaNode *next = curr->next;
        free(curr);
        curr = next;
    }
    map->head = NULL;
    map->tail = NULL;
}

// ---------------------------------------------------------------------------------
// VMAMap Interval List Operations
// ---------------------------------------------------------------------------------

static bool can_merge_vma(const VMANode *a, const VMANode *b) {
    if (a->disallow_merge || b->disallow_merge) return false;
    if (a->base + a->size != b->base) return false;
    if (a->prot != b->prot || a->type != b->type) return false;
    if (strncmp(a->name, b->name, sizeof(a->name)) != 0) return false;
    if (a->type == VMA_TYPE_DIRECT && b->type == VMA_TYPE_DIRECT) {
        if (!a->phys_areas.tail || !b->phys_areas.head) return false;
        const PhysicalMemoryArea *last_phys = &a->phys_areas.tail->area;
        const PhysicalMemoryArea *first_next = &b->phys_areas.head->area;
        if (last_phys->base + last_phys->size != first_next->base ||
            last_phys->memory_type != first_next->memory_type) {
            return false;
        }
    }
    return true;
}

static VMANode *find_vma(VMAMap *map, uint64_t addr) {
    VMANode *curr = map->head;
    VMANode *found = NULL;
    while (curr) {
        if (curr->base <= addr) {
            found = curr;
        } else {
            break;
        }
        curr = curr->next;
    }
    return found;
}

static VMANode *split_vma(VMAMap *map, VMANode *node, uint64_t offset_in_vma) {
    if (!node || offset_in_vma == 0 || offset_in_vma >= node->size) return node;

    VMANode *new_node = (VMANode *)calloc(1, sizeof(VMANode));
    if (!new_node) return node;

    *new_node = *node;
    node->size = offset_in_vma;
    new_node->base = node->base + offset_in_vma;
    new_node->size -= offset_in_vma;

    memset(&new_node->phys_areas, 0, sizeof(PhysMap));
    PhysAreaNode *curr = node->phys_areas.head;
    while (curr) {
        PhysAreaNode *next = curr->next;
        uint64_t off = curr->area.base; // offset in VMA
        if (off + curr->area.size <= offset_in_vma) {
            // stays in old VMA
        } else if (off < offset_in_vma && off + curr->area.size > offset_in_vma) {
            uint64_t size_in_old = offset_in_vma - off;
            uint64_t size_in_new = curr->area.size - size_in_old;
            append_phys_area(&new_node->phys_areas, 0, size_in_new, curr->area.memory_type, curr->area.dma_type);
            curr->area.size = size_in_old;
        } else {
            // moves to new VMA
            if (curr->prev) curr->prev->next = curr->next;
            else node->phys_areas.head = curr->next;
            if (curr->next) curr->next->prev = curr->prev;
            else node->phys_areas.tail = curr->prev;

            curr->prev = NULL;
            curr->next = NULL;
            curr->area.base = off - offset_in_vma;
            if (new_node->phys_areas.tail) {
                new_node->phys_areas.tail->next = curr;
                curr->prev = new_node->phys_areas.tail;
                new_node->phys_areas.tail = curr;
            } else {
                new_node->phys_areas.head = curr;
                new_node->phys_areas.tail = curr;
            }
        }
        curr = next;
    }

    new_node->prev = node;
    new_node->next = node->next;
    if (node->next) node->next->prev = new_node;
    else map->tail = new_node;
    node->next = new_node;

    return new_node;
}

static VMANode *merge_vma_adjacent(VMAMap *map, VMANode *node) {
    if (!node) return NULL;
    // Try merge with next
    if (node->next && can_merge_vma(node, node->next)) {
        VMANode *next = node->next;
        node->size += next->size;
        // Merge physical areas
        PhysAreaNode *p = next->phys_areas.head;
        while (p) {
            PhysAreaNode *p_next = p->next;
            append_phys_area(&node->phys_areas, p->area.base, p->area.size, p->area.memory_type, p->area.dma_type);
            free(p);
            p = p_next;
        }
        node->next = next->next;
        if (next->next) next->next->prev = node;
        else map->tail = node;
        free(next);
    }
    // Try merge with prev
    if (node->prev && can_merge_vma(node->prev, node)) {
        VMANode *prev = node->prev;
        prev->size += node->size;
        PhysAreaNode *p = node->phys_areas.head;
        while (p) {
            PhysAreaNode *p_next = p->next;
            append_phys_area(&prev->phys_areas, p->area.base, p->area.size, p->area.memory_type, p->area.dma_type);
            free(p);
            p = p_next;
        }
        prev->next = node->next;
        if (node->next) node->next->prev = prev;
        else map->tail = prev;
        free(node);
        node = prev;
    }
    return node;
}

static VMANode *carve_vma(VMAMap *map, uint64_t addr, uint64_t size) {
    VMANode *node = find_vma(map, addr);
    if (!node) return NULL;

    uint64_t start_in_vma = addr - node->base;
    uint64_t end_in_vma = start_in_vma + size;

    if (end_in_vma < node->size) {
        split_vma(map, node, end_in_vma);
    }
    if (start_in_vma > 0) {
        node = split_vma(map, node, start_in_vma);
    }
    return node;
}

static uint64_t search_free_vma(VMAMap *map, uint64_t virtual_addr, uint64_t size, uint64_t alignment) {
    if (alignment == 0) alignment = GUEST_PAGE_SIZE;
    uint64_t min_search = DEFAULT_MAPPING_BASE;
    uint64_t max_search = 0x8000000000ULL; // 512 GB limit

    if (virtual_addr < min_search) virtual_addr = min_search;
    virtual_addr = align_up(virtual_addr, alignment);

    VMANode *node = find_vma(map, virtual_addr);
    while (node) {
        if (node->type == VMA_TYPE_FREE) {
            uint64_t cand = align_up(node->base > virtual_addr ? node->base : virtual_addr, alignment);
            if (cand < max_search && cand + size <= node->base + node->size) {
                return cand;
            }
        }
        node = node->next;
    }
    return (uint64_t)-1;
}

// ---------------------------------------------------------------------------------
// SetupMemoryRegions (Mirroring shadPS4 linker.cpp & memory.cpp)
// ---------------------------------------------------------------------------------
static void setup_memory_regions_locked(void) {
    const bool is_neo = (sceKernelIsNeoMode() != 0);
    // DevKit Pro memory limit: 7936 MB. Base PS4: 5248 MB.
    uint64_t total_size = is_neo ? ORBIS_KERNEL_TOTAL_MEM_DEV_PRO : ORBIS_KERNEL_TOTAL_MEM_DEV;

    const char *env_direct = getenv("PS4_DIRECT_MEM_SIZE");
    if (env_direct && *env_direct) {
        char *end = NULL;
        unsigned long long val = strtoull(env_direct, &end, 0);
        if (end && *end) {
            if (*end == 'g' || *end == 'G') val *= 1024ULL * 1024 * 1024;
            else if (*end == 'm' || *end == 'M') val *= 1024ULL * 1024;
            else if (*end == 'k' || *end == 'K') val *= 1024ULL;
        }
        if (val > 0) total_size = val + ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE;
    }

    uint64_t fmem_size = ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE; // 512 MB default
    const char *env_fmem = getenv("PS4_FLEXIBLE_MEM_SIZE");
    if (env_fmem && *env_fmem) {
        char *end = NULL;
        unsigned long long val = strtoull(env_fmem, &end, 0);
        if (end && *end) {
            if (*end == 'g' || *end == 'G') val *= 1024ULL * 1024 * 1024;
            else if (*end == 'm' || *end == 'M') val *= 1024ULL * 1024;
            else if (*end == 'k' || *end == 'K') val *= 1024ULL;
        }
        if (val > 0) fmem_size = val;
    }

    g_total_flexible_size = fmem_size - ORBIS_KERNEL_FLEXIBLE_MEMORY_BASE;
    g_total_direct_size = total_size - fmem_size;
    g_flexible_usage = 0;
    g_pool_budget = 0;

    clear_phys_map(&g_dmem_map);
    append_phys_area(&g_dmem_map, 0, g_total_direct_size, 0, PHYS_MEM_TYPE_FREE);

    clear_phys_map(&g_fmem_map);
    append_phys_area(&g_fmem_map, g_total_direct_size, g_total_flexible_size, 0, PHYS_MEM_TYPE_FREE);

    // Initialize virtual memory area covering 256MB up to 512GB
    if (!g_vma_map.head) {
        VMANode *root = (VMANode *)calloc(1, sizeof(VMANode));
        if (root) {
            root->base = 0x10000000ULL;
            root->size = 0x8000000000ULL - root->base;
            root->type = VMA_TYPE_FREE;
            root->prot = MEM_PROT_NO_ACCESS;
            g_vma_map.head = root;
            g_vma_map.tail = root;
        }
    }

    printf("[ps4-mem] SetupMemoryRegions: Neo=%d Total=%.2f MB Direct=%.2f MB Flexible=%.2f MB\n",
           is_neo, (double)total_size / (1024.0 * 1024.0),
           (double)g_total_direct_size / (1024.0 * 1024.0),
           (double)g_total_flexible_size / (1024.0 * 1024.0));
    fflush(stdout);
}

int ps4_direct_mem_init(void) {
    pthread_mutex_lock(&g_mem_mutex);
    if (!g_mem_inited) {
        dm_phys_init();
        setup_memory_regions_locked();
        g_mem_inited = 1;
    }
    pthread_mutex_unlock(&g_mem_mutex);
    return 0;
}

void ps4_direct_mem_destroy(void) {
    pthread_mutex_lock(&g_mem_mutex);
    clear_phys_map(&g_dmem_map);
    clear_phys_map(&g_fmem_map);
    VMANode *curr = g_vma_map.head;
    while (curr) {
        VMANode *next = curr->next;
        clear_phys_map(&curr->phys_areas);
        free(curr);
        curr = next;
    }
    g_vma_map.head = NULL;
    g_vma_map.tail = NULL;
    g_mem_inited = 0;
    pthread_mutex_unlock(&g_mem_mutex);
}

void ps4_direct_mem_on_unmap(GuestContext *ctx, uint64_t addr, size_t size) {
    (void)ctx;
    if (size == 0) return;
    sceKernelReleaseDirectMemory((off_t)addr, size);
}

// ---------------------------------------------------------------------------------
// Direct Memory Query & Allocation
// ---------------------------------------------------------------------------------

size_t sceKernelGetDirectMemorySize(void) {
    ps4_direct_mem_init();
    return g_total_direct_size;
}

// Mirroring shadPS4 MemoryManager::DirectQueryAvailable (memory.cpp lines 1262-1307)
int sceKernelAvailableDirectMemorySizeExact(off_t searchStart, off_t searchEnd, size_t alignment, off_t *physAddrOut, size_t *sizeOut) {
    ps4_direct_mem_init();
    if (!physAddrOut || !sizeOut) return 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL

    pthread_mutex_lock(&g_mem_mutex);

    PhysAreaNode *node = find_phys_area(&g_dmem_map, (uint64_t)searchStart);
    uint64_t best_paddr = 0;
    uint64_t max_size = 0;

    while (node) {
        if (node->area.dma_type == PHYS_MEM_TYPE_FREE) {
            uint64_t aligned_base = alignment > 0 ? align_up(node->area.base, alignment) : node->area.base;
            uint64_t align_pad = aligned_base - node->area.base;
            uint64_t rem = node->area.size >= align_pad ? node->area.size - align_pad : 0;

            if (node->area.base < (uint64_t)searchStart) {
                uint64_t cut = (uint64_t)searchStart - node->area.base;
                rem = rem > cut ? rem - cut : 0;
                aligned_base = alignment > 0 ? align_up((uint64_t)searchStart, alignment) : (uint64_t)searchStart;
            }

            uint64_t area_end = node->area.base + node->area.size;
            if (searchEnd > 0 && area_end > (uint64_t)searchEnd) {
                uint64_t cut = area_end - (uint64_t)searchEnd;
                rem = rem > cut ? rem - cut : 0;
            }

            if (rem > max_size) {
                best_paddr = aligned_base;
                max_size = rem;
            }
        }
        node = node->next;
    }

    pthread_mutex_unlock(&g_mem_mutex);

    if (max_size == 0) {
        return 0x8002000c; // ORBIS_KERNEL_ERROR_ENOMEM
    }

    *physAddrOut = (off_t)best_paddr;
    *sizeOut = (size_t)max_size;
    return 0; // ORBIS_OK
}

size_t sceKernelAvailableDirectMemorySize(void) {
    off_t paddr = 0;
    size_t sz = 0;
    if (sceKernelAvailableDirectMemorySizeExact(0, (off_t)g_total_direct_size, 0, &paddr, &sz) == 0) {
        return sz;
    }
    return 0;
}

// Mirroring shadPS4 MemoryManager::Allocate (memory.cpp lines 245-282)
int sceKernelAllocateDirectMemory(off_t searchStart, off_t searchEnd, size_t length, size_t alignment, int type, off_t *physAddrOut) {
    ps4_direct_mem_init();
    if (!physAddrOut || length == 0 || (length & 0x3fffULL)) return 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    if (searchStart < 0 || searchEnd < 0) return 0x80020016;
    if (alignment == 0) alignment = GUEST_PAGE_SIZE;

    pthread_mutex_lock(&g_mem_mutex);

    PhysAreaNode *node = find_phys_area(&g_dmem_map, (uint64_t)searchStart);
    uint64_t mapping_start = align_up(node ? (node->area.base > (uint64_t)searchStart ? node->area.base : (uint64_t)searchStart) : (uint64_t)searchStart, alignment);
    uint64_t mapping_end = mapping_start + length;

    while (node && (node->area.dma_type != PHYS_MEM_TYPE_FREE || (node->area.base + node->area.size) < mapping_end)) {
        node = node->next;
        if (!node) break;
        mapping_start = align_up(node->area.base, alignment);
        mapping_end = mapping_start + length;
    }

    if (!node || (searchEnd > 0 && mapping_end > (uint64_t)searchEnd)) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x80020023; // ORBIS_KERNEL_ERROR_EAGAIN
    }

    PhysAreaNode *carved = carve_phys_area(&g_dmem_map, mapping_start, length);
    if (!carved) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x80020023;
    }

    carved->area.memory_type = type;
    carved->area.dma_type = PHYS_MEM_TYPE_ALLOCATED;
    merge_phys_adjacent(&g_dmem_map, carved);

    *physAddrOut = (off_t)mapping_start;
    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelAllocateMainDirectMemory(size_t length, size_t alignment, int type, off_t *physAddrOut) {
    const off_t searchEnd = (off_t)sceKernelGetDirectMemorySize();
    return sceKernelAllocateDirectMemory(0, searchEnd, length, alignment, type, physAddrOut);
}

// Mirroring shadPS4 MemoryManager::Free (memory.cpp lines 284-376)
int sceKernelReleaseDirectMemory(off_t physAddr, size_t length) {
    ps4_direct_mem_init();
    if ((physAddr & 0x3fffULL) || (length & 0x3fffULL)) return 0x80020016;
    if (length == 0) return 0;

    pthread_mutex_lock(&g_mem_mutex);
    if ((uint64_t)physAddr > g_total_direct_size) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0;
    }

    uint64_t remaining = length;
    PhysAreaNode *curr = find_phys_area(&g_dmem_map, (uint64_t)physAddr);
    while (curr && remaining > 0) {
        if (curr->area.dma_type != PHYS_MEM_TYPE_FREE) {
            uint64_t cur_phys = curr->area.base > (uint64_t)physAddr ? curr->area.base : (uint64_t)physAddr;
            uint64_t offset = cur_phys - curr->area.base;
            uint64_t sz = remaining < (curr->area.size - offset) ? remaining : (curr->area.size - offset);

            PhysAreaNode *carved = carve_phys_area(&g_dmem_map, cur_phys, sz);
            if (carved) {
                carved->area.dma_type = PHYS_MEM_TYPE_FREE;
                carved->area.memory_type = 0;
                curr = merge_phys_adjacent(&g_dmem_map, carved);
            }
            remaining -= sz;
        }
        if (curr) curr = curr->next;
    }

    pthread_mutex_unlock(&g_mem_mutex);
    return 0;
}

int sceKernelCheckedReleaseDirectMemory(off_t physAddr, size_t length) {
    return sceKernelReleaseDirectMemory(physAddr, length);
}

int sceKernelDirectMemoryQuery(off_t offset, int flags, OrbisDirectMemoryQueryInfo *info, size_t infoSize) {
    (void)flags;
    if (!info || infoSize < sizeof(OrbisDirectMemoryQueryInfo)) return 0x80020016;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);
    PhysAreaNode *node = find_phys_area(&g_dmem_map, (uint64_t)offset);
    if (!node || node->area.dma_type == PHYS_MEM_TYPE_FREE) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000d; // ORBIS_KERNEL_ERROR_EACCES
    }

    info->start = node->area.base;
    info->end = node->area.base + node->area.size;
    info->memoryType = node->area.memory_type;
    pthread_mutex_unlock(&g_mem_mutex);
    return 0;
}

// ---------------------------------------------------------------------------------
// Virtual Memory Allocation & Mapping (Mirroring shadPS4 MapMemory)
// ---------------------------------------------------------------------------------

int sceKernelMapDirectMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags, off_t physAddr, size_t alignment) {
    if (!ctx || !addrInOut || length == 0 || (length & 0x3fffULL) || (physAddr & 0x3fffULL)) {
        return 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL
    }
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);

    uint64_t virtual_addr = (uint64_t)*addrInOut;
    if (flags & MEM_MAP_FIXED) {
        if (virtual_addr == 0) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x80020016;
        }
    } else {
        virtual_addr = search_free_vma(&g_vma_map, virtual_addr, length, alignment);
        if (virtual_addr == (uint64_t)-1) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x8002000c; // ORBIS_KERNEL_ERROR_ENOMEM
        }
    }

    VMANode *vma = carve_vma(&g_vma_map, virtual_addr, length);
    if (!vma) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c;
    }

    vma->type = VMA_TYPE_DIRECT;
    vma->prot = (MemoryProt)prot;
    strncpy(vma->name, "direct_mem", sizeof(vma->name) - 1);
    clear_phys_map(&vma->phys_areas);

    // Carve physical areas in dmem_map and attach to VMA
    uint64_t current_phys = (uint64_t)physAddr;
    uint64_t rem = length;
    while (rem > 0) {
        PhysAreaNode *dma = find_phys_area(&g_dmem_map, current_phys);
        if (!dma) break;
        uint64_t offset = current_phys - dma->area.base;
        uint64_t sz = rem < (dma->area.size - offset) ? rem : (dma->area.size - offset);

        PhysAreaNode *carved = carve_phys_area(&g_dmem_map, current_phys, sz);
        if (carved) {
            carved->area.dma_type = PHYS_MEM_TYPE_MAPPED;
            append_phys_area(&vma->phys_areas, current_phys - (uint64_t)physAddr, sz, carved->area.memory_type, PHYS_MEM_TYPE_MAPPED);
            merge_phys_adjacent(&g_dmem_map, carved);
        }
        current_phys += sz;
        rem -= sz;
    }

    merge_vma_adjacent(&g_vma_map, vma);

    // Remap guest virtual address onto physical backing
    dm_alias(ctx, virtual_addr, (uint64_t)physAddr, length);

    *addrInOut = (void *)virtual_addr;
    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelMapNamedFlexibleMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags, const char *name) {
    if (!ctx || !addrInOut || length == 0) return 0x80020016;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);
    if (g_flexible_usage + length > g_total_flexible_size) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x80020016; // ORBIS_KERNEL_ERROR_EINVAL (Out of flexible memory)
    }

    uint64_t virtual_addr = (uint64_t)*addrInOut;
    size_t alignment = 65536; // 64 KB alignment for flexible memory
    size_t aligned_len = align_up(length, alignment);

    if (flags & MEM_MAP_FIXED) {
        if (virtual_addr == 0) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x80020016;
        }
    } else {
        virtual_addr = search_free_vma(&g_vma_map, virtual_addr, aligned_len, alignment);
        if (virtual_addr == (uint64_t)-1) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x8002000c;
        }
    }

    VMANode *vma = carve_vma(&g_vma_map, virtual_addr, aligned_len);
    if (!vma) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c;
    }

    vma->type = VMA_TYPE_FLEXIBLE;
    vma->prot = (MemoryProt)prot;
    strncpy(vma->name, name ? name : "flexible_mem", sizeof(vma->name) - 1);
    clear_phys_map(&vma->phys_areas);

    // Carve physical blocks from fmem_map
    uint64_t rem = aligned_len;
    uint64_t cur_va = virtual_addr;
    PhysAreaNode *fnode = g_fmem_map.head;
    while (fnode && rem > 0) {
        if (fnode->area.dma_type == PHYS_MEM_TYPE_FREE) {
            uint64_t sz = rem < fnode->area.size ? rem : fnode->area.size;
            PhysAreaNode *carved = carve_phys_area(&g_fmem_map, fnode->area.base, sz);
            if (carved) {
                carved->area.dma_type = PHYS_MEM_TYPE_FLEXIBLE;
                append_phys_area(&vma->phys_areas, cur_va - virtual_addr, sz, 0, PHYS_MEM_TYPE_FLEXIBLE);
                dm_alias(ctx, cur_va, carved->area.base, sz);
                merge_phys_adjacent(&g_fmem_map, carved);
            }
            cur_va += sz;
            rem -= sz;
            g_flexible_usage += sz;
        }
        fnode = fnode->next;
    }

    merge_vma_adjacent(&g_vma_map, vma);

    *addrInOut = (void *)virtual_addr;
    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelMapFlexibleMemory(GuestContext *ctx, void **addrInOut, size_t length, int prot, int flags) {
    return sceKernelMapNamedFlexibleMemory(ctx, addrInOut, length, prot, flags, "flexible_mem");
}

int sceKernelReserveVirtualRange(GuestContext *ctx, void **addrInOut, size_t length, int flags, size_t alignment) {
    if (!ctx || !addrInOut || length == 0) return 0x80020016;
    ps4_direct_mem_init();

    if (alignment == 0) alignment = 65536;
    size_t aligned_len = align_up(length, alignment);
    uint64_t virtual_addr = (uint64_t)*addrInOut;

    pthread_mutex_lock(&g_mem_mutex);
    if (flags & MEM_MAP_FIXED) {
        if (virtual_addr == 0) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x80020016;
        }
    } else {
        virtual_addr = search_free_vma(&g_vma_map, virtual_addr, aligned_len, alignment);
        if (virtual_addr == (uint64_t)-1) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x8002000c;
        }
    }

    VMANode *vma = carve_vma(&g_vma_map, virtual_addr, aligned_len);
    if (!vma) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c;
    }

    vma->type = VMA_TYPE_RESERVED;
    vma->prot = MEM_PROT_NO_ACCESS;
    strncpy(vma->name, "reserved", sizeof(vma->name) - 1);
    clear_phys_map(&vma->phys_areas);
    merge_vma_adjacent(&g_vma_map, vma);

    *addrInOut = (void *)virtual_addr;
    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelSetVirtualRangeName(GuestContext *ctx, const void *addr, size_t len, const char *name) {
    (void)ctx;
    if (!name) return -0x80020014;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);
    VMANode *node = find_vma(&g_vma_map, (uint64_t)addr);
    if (node && node->type != VMA_TYPE_FREE) {
        strncpy(node->name, name, sizeof(node->name) - 1);
    }
    pthread_mutex_unlock(&g_mem_mutex);
    return 0;
}

int sceKernelConfiguredFlexibleMemorySize(GuestContext *ctx, uint64_t *sizeOut) {
    (void)ctx;
    if (!sizeOut) return 0x80020016;
    ps4_direct_mem_init();
    *sizeOut = g_total_flexible_size;
    return 0;
}

size_t sceKernelAvailableFlexibleMemorySize(GuestContext *ctx) {
    (void)ctx;
    ps4_direct_mem_init();
    pthread_mutex_lock(&g_mem_mutex);
    size_t avail = g_total_flexible_size > g_flexible_usage ? (size_t)(g_total_flexible_size - g_flexible_usage) : 0;
    pthread_mutex_unlock(&g_mem_mutex);
    return avail;
}

int sceKernelMprotect(GuestContext *ctx, const void *addr, size_t size, int prot) {
    (void)ctx;
    if (size == 0) return 0;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);
    VMANode *node = find_vma(&g_vma_map, (uint64_t)addr);
    if (node && node->type != VMA_TYPE_FREE) {
        node->prot = (MemoryProt)prot;
    }
    pthread_mutex_unlock(&g_mem_mutex);
    return 0;
}

int sceKernelMtypeprotect(GuestContext *ctx, const void *addr, size_t size, int mtype, int prot) {
    (void)mtype;
    return sceKernelMprotect(ctx, addr, size, prot);
}

int sceKernelVirtualQuery(GuestContext *ctx, const void *addr, int flags, OrbisKernelVirtualQueryInfo *info, size_t infoSize) {
    (void)ctx;
    (void)flags;
    if (!info || infoSize < sizeof(OrbisKernelVirtualQueryInfo)) return 0x80020016;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);
    VMANode *node = find_vma(&g_vma_map, (uint64_t)addr);
    if (!node || node->type == VMA_TYPE_FREE) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000d; // ORBIS_KERNEL_ERROR_EACCES
    }

    memset(info, 0, infoSize);
    info->start = (void *)node->base;
    info->end = (void *)(node->base + node->size);
    info->prot = (int32_t)node->prot;
    info->isFlexibleMemory = (node->type == VMA_TYPE_FLEXIBLE) ? 1 : 0;
    info->isDirectMemory = (node->type == VMA_TYPE_DIRECT) ? 1 : 0;
    info->isStack = (node->type == VMA_TYPE_STACK) ? 1 : 0;
    info->isPooledMemory = (node->type == VMA_TYPE_POOLED || node->type == VMA_TYPE_POOL_RESERVED) ? 1 : 0;
    info->isCommitted = (node->type != VMA_TYPE_FREE && node->type != VMA_TYPE_RESERVED && node->type != VMA_TYPE_POOL_RESERVED) ? 1 : 0;
    strncpy(info->name, node->name, sizeof(info->name) - 1);

    if (node->type == VMA_TYPE_DIRECT && node->phys_areas.head) {
        info->offset = (off_t)node->phys_areas.head->area.base;
        info->memory_type = node->phys_areas.head->area.memory_type;
    }
    if (node->type == VMA_TYPE_RESERVED || node->type == VMA_TYPE_POOL_RESERVED) {
        info->prot = 0;
    }

    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelQueryMemoryProtection(GuestContext *ctx, const void *addr, void **start, void **end, int *prot) {
    (void)ctx;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);
    VMANode *node = find_vma(&g_vma_map, (uint64_t)addr);
    if (!node || node->type == VMA_TYPE_FREE) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000d;
    }
    if (start) *start = (void *)node->base;
    if (end) *end = (void *)(node->base + node->size);
    if (prot) *prot = (int)node->prot;
    pthread_mutex_unlock(&g_mem_mutex);
    return 0;
}

int sceKernelBatchMap(GuestContext *ctx, OrbisKernelBatchMapEntry *entries, int numEntries, int *numEntriesOut) {
    if (!entries || numEntries <= 0) return 0x80020016;
    int processed = 0;
    int rc = 0;
    for (int i = 0; i < numEntries; i++) {
        OrbisKernelBatchMapEntry *e = &entries[i];
        void *addr = (void *)e->start;
        switch (e->operation) {
            case ORBIS_KERNEL_BATCH_MAP_DIRECT:
                rc = sceKernelMapDirectMemory(ctx, &addr, e->length, e->protection, 0, (off_t)e->offset, 0);
                e->start = (uint64_t)addr;
                break;
            case ORBIS_KERNEL_BATCH_MAP_UNMAP:
                rc = sceKernelReleaseDirectMemory((off_t)e->start, e->length);
                break;
            case ORBIS_KERNEL_BATCH_MAP_PROTECT:
                rc = sceKernelMprotect(ctx, addr, e->length, e->protection);
                break;
            case ORBIS_KERNEL_BATCH_MAP_FLEXIBLE:
                rc = sceKernelMapFlexibleMemory(ctx, &addr, e->length, e->protection, 0);
                e->start = (uint64_t)addr;
                break;
            case ORBIS_KERNEL_BATCH_MAP_TYPE_PROTECT:
                rc = sceKernelMtypeprotect(ctx, addr, e->length, e->type, e->protection);
                break;
            default:
                rc = 0x80020016;
                break;
        }
        if (rc != 0) break;
        processed++;
    }
    if (numEntriesOut) *numEntriesOut = processed;
    return rc;
}

// ---------------------------------------------------------------------------------
// Memory Pools (Mirroring shadPS4 PoolExpand, PoolReserve, PoolCommit, PoolDecommit)
// ---------------------------------------------------------------------------------

int sceKernelMemoryPoolExpand(GuestContext *ctx, off_t searchStart, off_t searchEnd, size_t len, size_t alignment, off_t *physAddrOut) {
    (void)ctx;
    ps4_direct_mem_init();
    if (!physAddrOut || len == 0 || (len & 0xffffULL)) return 0x80020016; // Length must be 64KB aligned
    if (searchStart < 0 || (searchEnd > 0 && searchEnd <= searchStart)) return 0x80020016;
    if (alignment == 0) alignment = 65536; // 64 KB alignment

    pthread_mutex_lock(&g_mem_mutex);

    PhysAreaNode *node = find_phys_area(&g_dmem_map, (uint64_t)searchStart);
    uint64_t mapping_start = align_up(node ? (node->area.base > (uint64_t)searchStart ? node->area.base : (uint64_t)searchStart) : (uint64_t)searchStart, alignment);
    uint64_t mapping_end = mapping_start + len;

    while (node && (node->area.dma_type != PHYS_MEM_TYPE_FREE || (node->area.base + node->area.size) < mapping_end)) {
        node = node->next;
        if (!node) break;
        mapping_start = align_up(node->area.base, alignment);
        mapping_end = mapping_start + len;
    }

    if (!node || (searchEnd > 0 && mapping_end > (uint64_t)searchEnd)) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c; // ORBIS_KERNEL_ERROR_ENOMEM
    }

    PhysAreaNode *carved = carve_phys_area(&g_dmem_map, mapping_start, len);
    if (!carved) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c;
    }

    carved->area.dma_type = PHYS_MEM_TYPE_POOLED;
    carved->area.memory_type = 3;
    g_pool_budget += len;

    *physAddrOut = (off_t)mapping_start;

    printf("[ps4-mem] sceKernelMemoryPoolExpand SUCCESS: start=0x%llx len=%zu (%.2f MB) pool_budget=%.2f MB\n",
           (unsigned long long)mapping_start, len, (double)len / (1024.0 * 1024.0),
           (double)g_pool_budget / (1024.0 * 1024.0));
    fflush(stdout);

    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelMemoryPoolReserve(GuestContext *ctx, void *addrIn, size_t len, size_t alignment, int flags, void **addrOut) {
    (void)ctx;
    if (!addrOut || len == 0 || (len & 0x1fffffULL)) return 0x80020016; // Must be 2MB aligned
    ps4_direct_mem_init();

    if (alignment == 0) alignment = 2ULL * 1024 * 1024; // 2 MB alignment
    size_t aligned_size = align_up(len, alignment);
    uint64_t virtual_addr = (uint64_t)addrIn;

    pthread_mutex_lock(&g_mem_mutex);
    if (flags & MEM_MAP_FIXED) {
        if (virtual_addr == 0) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x80020016;
        }
    } else {
        virtual_addr = search_free_vma(&g_vma_map, virtual_addr, aligned_size, alignment);
        if (virtual_addr == (uint64_t)-1) {
            pthread_mutex_unlock(&g_mem_mutex);
            return 0x8002000c;
        }
    }

    VMANode *vma = carve_vma(&g_vma_map, virtual_addr, aligned_size);
    if (!vma) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c;
    }

    vma->type = VMA_TYPE_POOL_RESERVED;
    vma->prot = MEM_PROT_NO_ACCESS;
    strncpy(vma->name, "pool_reserved", sizeof(vma->name) - 1);
    clear_phys_map(&vma->phys_areas);
    merge_vma_adjacent(&g_vma_map, vma);

    *addrOut = (void *)virtual_addr;

    printf("[ps4-mem] sceKernelMemoryPoolReserve SUCCESS: out=%p len=%zu (%.2f MB)\n",
           *addrOut, aligned_size, (double)aligned_size / (1024.0 * 1024.0));
    fflush(stdout);

    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelMemoryPoolCommit(GuestContext *ctx, void *addr, size_t len, int type, int prot, int flags) {
    (void)flags;
    if (!addr) return 0x80020016;
    if (len == 0) return 0;
    if (len & 0xffffULL) return 0x80020016; // 64KB aligned
    ps4_direct_mem_init();

    uint64_t virtual_addr = align_up((uint64_t)addr, 65536);

    pthread_mutex_lock(&g_mem_mutex);
    VMANode *vma = find_vma(&g_vma_map, virtual_addr);
    if (!vma || vma->type != VMA_TYPE_POOL_RESERVED) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x80020016;
    }

    if (g_pool_budget < len) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c; // ORBIS_KERNEL_ERROR_ENOMEM
    }
    g_pool_budget -= len;

    VMANode *carved_vma = carve_vma(&g_vma_map, virtual_addr, len);
    if (!carved_vma) {
        g_pool_budget += len;
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x8002000c;
    }

    carved_vma->type = VMA_TYPE_POOLED;
    carved_vma->prot = (MemoryProt)prot;
    strncpy(carved_vma->name, "pool_committed", sizeof(carved_vma->name) - 1);
    clear_phys_map(&carved_vma->phys_areas);

    // Carve physical areas from dmem_map that are marked PHYS_MEM_TYPE_POOLED
    uint64_t rem = len;
    uint64_t cur_va = virtual_addr;
    PhysAreaNode *pnode = g_dmem_map.head;
    while (pnode && rem > 0) {
        if (pnode->area.dma_type == PHYS_MEM_TYPE_POOLED) {
            uint64_t sz = rem < pnode->area.size ? rem : pnode->area.size;
            PhysAreaNode *carved = carve_phys_area(&g_dmem_map, pnode->area.base, sz);
            if (carved) {
                carved->area.dma_type = PHYS_MEM_TYPE_COMMITTED;
                carved->area.memory_type = type;
                append_phys_area(&carved_vma->phys_areas, cur_va - virtual_addr, sz, type, PHYS_MEM_TYPE_COMMITTED);
                dm_alias(ctx, cur_va, carved->area.base, sz);
                merge_phys_adjacent(&g_dmem_map, carved);
            }
            cur_va += sz;
            rem -= sz;
        }
        pnode = pnode->next;
    }

    merge_vma_adjacent(&g_vma_map, carved_vma);
    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

int sceKernelMemoryPoolDecommit(GuestContext *ctx, void *addr, size_t len, int flags) {
    (void)flags;
    if (!addr || len == 0) return 0;
    ps4_direct_mem_init();

    pthread_mutex_lock(&g_mem_mutex);
    uint64_t virtual_addr = (uint64_t)addr;
    VMANode *vma = find_vma(&g_vma_map, virtual_addr);
    if (!vma) {
        pthread_mutex_unlock(&g_mem_mutex);
        return 0x80020016;
    }

    uint64_t rem = len;
    uint64_t cur_va = virtual_addr;
    while (vma && rem > 0) {
        uint64_t offset = cur_va - vma->base;
        uint64_t sz = rem < (vma->size - offset) ? rem : (vma->size - offset);

        if (vma->type == VMA_TYPE_POOLED) {
            g_pool_budget += sz;
            // Restore physical blocks to PHYS_MEM_TYPE_POOLED
            PhysAreaNode *pa = vma->phys_areas.head;
            while (pa) {
                PhysAreaNode *dma = carve_phys_area(&g_dmem_map, pa->area.base, pa->area.size);
                if (dma) {
                    dma->area.dma_type = PHYS_MEM_TYPE_POOLED;
                    merge_phys_adjacent(&g_dmem_map, dma);
                }
                pa = pa->next;
            }
        }

        VMANode *carved = carve_vma(&g_vma_map, cur_va, sz);
        if (carved) {
            carved->type = VMA_TYPE_POOL_RESERVED;
            carved->prot = MEM_PROT_NO_ACCESS;
            clear_phys_map(&carved->phys_areas);
            merge_vma_adjacent(&g_vma_map, carved);
        }

        if (ctx && ctx->mem_base) {
            GuestContext *proc = ctx->process_ctx ? ctx->process_ctx : ctx;
            if (cur_va + sz <= proc->mem_size) {
#if defined(__APPLE__) || defined(__linux__)
                madvise(proc->mem_base + cur_va, sz, MADV_DONTNEED);
#endif
            }
        }

        cur_va += sz;
        rem -= sz;
        vma = find_vma(&g_vma_map, cur_va);
    }

    pthread_mutex_unlock(&g_mem_mutex);
    return 0; // ORBIS_OK
}

// ---------------------------------------------------------------------------------
// Guest Execution Shims (SysV AMD64 ABI: RDI, RSI, RDX, RCX, R8, R9)
// ---------------------------------------------------------------------------------

void shim_sceKernelAllocateDirectMemory(GuestContext *ctx) {
    off_t searchStart = (off_t)ctx->rdi;
    off_t searchEnd = (off_t)ctx->rsi;
    size_t length = (size_t)ctx->rdx;
    size_t alignment = (size_t)ctx->rcx;
    int type = (int)ctx->r8;
    uint64_t physAddrOutGuest = ctx->r9;

    off_t physAddr = 0;
    int rc = sceKernelAllocateDirectMemory(searchStart, searchEnd, length, alignment, type, &physAddr);
    if (rc == 0 && physAddrOutGuest != 0 && ctx->mem_base && physAddrOutGuest + sizeof(off_t) <= ctx->mem_size) {
        *(off_t *)(ctx->mem_base + physAddrOutGuest) = physAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelAllocateMainDirectMemory(GuestContext *ctx) {
    size_t length = (size_t)ctx->rdi;
    size_t alignment = (size_t)ctx->rsi;
    int type = (int)ctx->rdx;
    uint64_t physAddrOutGuest = ctx->rcx;

    off_t physAddr = 0;
    int rc = sceKernelAllocateMainDirectMemory(length, alignment, type, &physAddr);
    if (rc == 0 && physAddrOutGuest != 0 && ctx->mem_base && physAddrOutGuest + sizeof(off_t) <= ctx->mem_size) {
        *(off_t *)(ctx->mem_base + physAddrOutGuest) = physAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelGetDirectMemorySize(GuestContext *ctx) {
    ctx->rax = (uint64_t)sceKernelGetDirectMemorySize();
    SHIM_RETURN();
}

void shim_sceKernelAvailableDirectMemorySize(GuestContext *ctx) {
    // PS4 SysV ABI: sceKernelAvailableDirectMemorySize(searchStart, searchEnd, alignment, physAddrOut, sizeOut)
    // RDI: searchStart, RSI: searchEnd, RDX: alignment, RCX: physAddrOut, R8: sizeOut
    off_t searchStart = (off_t)ctx->rdi;
    off_t searchEnd = (off_t)ctx->rsi;
    size_t alignment = (size_t)ctx->rdx;
    uint64_t physAddrOutGuest = ctx->rcx;
    uint64_t sizeOutGuest = ctx->r8;

    if (physAddrOutGuest != 0 || sizeOutGuest != 0) {
        off_t physAddr = 0;
        size_t size = 0;
        int rc = sceKernelAvailableDirectMemorySizeExact(searchStart, searchEnd, alignment, &physAddr, &size);
        if (rc == 0) {
            if (physAddrOutGuest != 0 && ctx->mem_base && physAddrOutGuest + sizeof(off_t) <= ctx->mem_size) {
                *(off_t *)(ctx->mem_base + physAddrOutGuest) = physAddr;
            }
            if (sizeOutGuest != 0 && ctx->mem_base && sizeOutGuest + sizeof(size_t) <= ctx->mem_size) {
                *(size_t *)(ctx->mem_base + sizeOutGuest) = size;
            }
        }
        ctx->rax = (uint64_t)(int64_t)rc;
    } else {
        // Fallback: 0-arg call returning total free size
        ctx->rax = (uint64_t)sceKernelAvailableDirectMemorySize();
    }
    SHIM_RETURN();
}

void shim_sceKernelReleaseDirectMemory(GuestContext *ctx) {
    off_t physAddr = (off_t)ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelReleaseDirectMemory(physAddr, length);
    SHIM_RETURN();
}

void shim_sceKernelCheckedReleaseDirectMemory(GuestContext *ctx) {
    off_t physAddr = (off_t)ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    ctx->rax = (uint64_t)(int64_t)sceKernelCheckedReleaseDirectMemory(physAddr, length);
    SHIM_RETURN();
}

void shim_sceKernelDirectMemoryQuery(GuestContext *ctx) {
    off_t offset = (off_t)ctx->rdi;
    int flags = (int)ctx->rsi;
    uint64_t infoGuest = ctx->rdx;
    size_t infoSize = (size_t)ctx->rcx;

    OrbisDirectMemoryQueryInfo info = {0};
    int rc = sceKernelDirectMemoryQuery(offset, flags, &info, sizeof(info));
    if (rc == 0 && infoGuest != 0 && ctx->mem_base && infoGuest + sizeof(info) <= ctx->mem_size) {
        size_t copy_sz = infoSize < sizeof(info) ? infoSize : sizeof(info);
        memcpy(ctx->mem_base + infoGuest, &info, copy_sz);
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMapDirectMemory(GuestContext *ctx) {
    uint64_t addrInOutGuest = ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    int prot = (int)ctx->rdx;
    int flags = (int)ctx->rcx;
    off_t physAddr = (off_t)ctx->r8;
    size_t alignment = (size_t)ctx->r9;

    void *mappedAddr = NULL;
    if (addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        mappedAddr = (void *)*(uint64_t *)(ctx->mem_base + addrInOutGuest);
    }
    int rc = sceKernelMapDirectMemory(ctx, &mappedAddr, length, prot, flags, physAddr, alignment);
    if (rc == 0 && addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + addrInOutGuest) = (uint64_t)mappedAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMapFlexibleMemory(GuestContext *ctx) {
    uint64_t addrInOutGuest = ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    int prot = (int)ctx->rdx;
    int flags = (int)ctx->rcx;

    void *mappedAddr = NULL;
    if (addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        mappedAddr = (void *)*(uint64_t *)(ctx->mem_base + addrInOutGuest);
    }
    int rc = sceKernelMapFlexibleMemory(ctx, &mappedAddr, length, prot, flags);
    if (rc == 0 && addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + addrInOutGuest) = (uint64_t)mappedAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMapNamedFlexibleMemory(GuestContext *ctx) {
    uint64_t addrInOutGuest = ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    int prot = (int)ctx->rdx;
    int flags = (int)ctx->rcx;
    const char *name = (ctx->r8 && ctx->mem_base && ctx->r8 < ctx->mem_size) ? (const char *)(ctx->mem_base + ctx->r8) : "flexible_mem";

    void *mappedAddr = NULL;
    if (addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        mappedAddr = (void *)*(uint64_t *)(ctx->mem_base + addrInOutGuest);
    }
    int rc = sceKernelMapNamedFlexibleMemory(ctx, &mappedAddr, length, prot, flags, name);
    if (rc == 0 && addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + addrInOutGuest) = (uint64_t)mappedAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelReserveVirtualRange(GuestContext *ctx) {
    uint64_t addrInOutGuest = ctx->rdi;
    size_t length = (size_t)ctx->rsi;
    int flags = (int)ctx->rdx;
    size_t alignment = (size_t)ctx->rcx;

    void *mappedAddr = NULL;
    if (addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        mappedAddr = (void *)*(uint64_t *)(ctx->mem_base + addrInOutGuest);
    }
    int rc = sceKernelReserveVirtualRange(ctx, &mappedAddr, length, flags, alignment);
    if (rc == 0 && addrInOutGuest != 0 && ctx->mem_base && addrInOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + addrInOutGuest) = (uint64_t)mappedAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelSetVirtualRangeName(GuestContext *ctx) {
    const void *addr = (const void *)ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    const char *name = (ctx->rdx && ctx->mem_base && ctx->rdx < ctx->mem_size) ? (const char *)(ctx->mem_base + ctx->rdx) : NULL;
    ctx->rax = (uint64_t)(int64_t)sceKernelSetVirtualRangeName(ctx, addr, len, name);
    SHIM_RETURN();
}

void shim_sceKernelMprotect(GuestContext *ctx) {
    const void *addr = (const void *)ctx->rdi;
    size_t size = (size_t)ctx->rsi;
    int prot = (int)ctx->rdx;
    ctx->rax = (uint64_t)(int64_t)sceKernelMprotect(ctx, addr, size, prot);
    SHIM_RETURN();
}

void shim_sceKernelMtypeprotect(GuestContext *ctx) {
    const void *addr = (const void *)ctx->rdi;
    size_t size = (size_t)ctx->rsi;
    int mtype = (int)ctx->rdx;
    int prot = (int)ctx->rcx;
    ctx->rax = (uint64_t)(int64_t)sceKernelMtypeprotect(ctx, addr, size, mtype, prot);
    SHIM_RETURN();
}

void shim_sceKernelBatchMap(GuestContext *ctx) {
    uint64_t entriesGuest = ctx->rdi;
    int numEntries = (int)ctx->rsi;
    uint64_t numEntriesOutGuest = ctx->rdx;

    if (!entriesGuest || !ctx->mem_base || entriesGuest + (uint64_t)numEntries * sizeof(OrbisKernelBatchMapEntry) > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)0x80020016;
        SHIM_RETURN();
    }

    OrbisKernelBatchMapEntry *entries = (OrbisKernelBatchMapEntry *)(ctx->mem_base + entriesGuest);
    int numOut = 0;
    int rc = sceKernelBatchMap(ctx, entries, numEntries, &numOut);
    if (numEntriesOutGuest != 0 && ctx->mem_base && numEntriesOutGuest + sizeof(int) <= ctx->mem_size) {
        *(int *)(ctx->mem_base + numEntriesOutGuest) = numOut;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelConfiguredFlexibleMemorySize(GuestContext *ctx) {
    uint64_t sizeOutGuest = ctx->rdi;
    uint64_t cfg_size = 0;
    int rc = sceKernelConfiguredFlexibleMemorySize(ctx, &cfg_size);
    if (sizeOutGuest != 0 && ctx->mem_base && sizeOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + sizeOutGuest) = cfg_size;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelAvailableFlexibleMemorySize(GuestContext *ctx) {
    size_t avail = sceKernelAvailableFlexibleMemorySize(ctx);
    uint64_t sizeOutGuest = ctx->rdi;
    if (sizeOutGuest != 0 && ctx->mem_base && sizeOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + sizeOutGuest) = (uint64_t)avail;
        ctx->rax = 0;
    } else {
        ctx->rax = (uint64_t)avail;
    }
    SHIM_RETURN();
}

void shim_sceKernelVirtualQuery(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    int flags = (int)ctx->rsi;
    uint64_t infoGuest = ctx->rdx;
    size_t infoSize = (size_t)ctx->rcx;

    if (!infoGuest || !ctx->mem_base || infoGuest + infoSize > ctx->mem_size) {
        ctx->rax = (uint64_t)(int64_t)0x80020016;
        SHIM_RETURN();
    }

    OrbisKernelVirtualQueryInfo info = {0};
    int rc = sceKernelVirtualQuery(ctx, (const void *)addr, flags, &info, sizeof(info));
    if (rc == 0) {
        size_t copy_sz = infoSize < sizeof(info) ? infoSize : sizeof(info);
        memcpy(ctx->mem_base + infoGuest, &info, copy_sz);
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelQueryMemoryProtection(GuestContext *ctx) {
    uint64_t addr = ctx->rdi;
    uint64_t startGuest = ctx->rsi;
    uint64_t endGuest = ctx->rdx;
    uint64_t protGuest = ctx->rcx;

    void *start = NULL;
    void *end = NULL;
    int prot = 0;
    int rc = sceKernelQueryMemoryProtection(ctx, (const void *)addr, &start, &end, &prot);
    if (rc == 0) {
        if (startGuest && ctx->mem_base && startGuest + sizeof(uint64_t) <= ctx->mem_size)
            *(uint64_t *)(ctx->mem_base + startGuest) = (uint64_t)start;
        if (endGuest && ctx->mem_base && endGuest + sizeof(uint64_t) <= ctx->mem_size)
            *(uint64_t *)(ctx->mem_base + endGuest) = (uint64_t)end;
        if (protGuest && ctx->mem_base && protGuest + sizeof(int) <= ctx->mem_size)
            *(int *)(ctx->mem_base + protGuest) = prot;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolReserve(GuestContext *ctx) {
    // PS4 SysV ABI: sceKernelMemoryPoolReserve(addrIn, len, alignment, flags, addrOut)
    // RDI: addrIn, RSI: len, RDX: alignment, RCX: flags, R8: addrOut
    void *addrIn = (void *)ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    size_t alignment = (size_t)ctx->rdx;
    int flags = (int)ctx->rcx;
    uint64_t addrOutGuest = ctx->r8;

    void *addrOut = NULL;
    int rc = sceKernelMemoryPoolReserve(ctx, addrIn, len, alignment, flags, &addrOut);
    if (rc == 0 && addrOutGuest != 0 && ctx->mem_base && addrOutGuest + sizeof(uint64_t) <= ctx->mem_size) {
        *(uint64_t *)(ctx->mem_base + addrOutGuest) = (uint64_t)addrOut;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolExpand(GuestContext *ctx) {
    // PS4 SysV ABI: sceKernelMemoryPoolExpand(searchStart, searchEnd, len, alignment, physAddrOut)
    // RDI: searchStart, RSI: searchEnd, RDX: len, RCX: alignment, R8: physAddrOut
    off_t searchStart = (off_t)ctx->rdi;
    off_t searchEnd = (off_t)ctx->rsi;
    size_t len = (size_t)ctx->rdx;
    size_t alignment = (size_t)ctx->rcx;
    uint64_t physAddrOutGuest = ctx->r8;

    off_t physAddr = 0;
    int rc = sceKernelMemoryPoolExpand(ctx, searchStart, searchEnd, len, alignment, &physAddr);
    if (rc == 0 && physAddrOutGuest != 0 && ctx->mem_base && physAddrOutGuest + sizeof(off_t) <= ctx->mem_size) {
        *(off_t *)(ctx->mem_base + physAddrOutGuest) = physAddr;
    }
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolCommit(GuestContext *ctx) {
    // PS4 SysV ABI: sceKernelMemoryPoolCommit(addr, len, type, prot, flags)
    // RDI: addr, RSI: len, RDX: type, RCX: prot, R8: flags
    void *addr = (void *)ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    int type = (int)ctx->rdx;
    int prot = (int)ctx->rcx;
    int flags = (int)ctx->r8;

    int rc = sceKernelMemoryPoolCommit(ctx, addr, len, type, prot, flags);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}

void shim_sceKernelMemoryPoolDecommit(GuestContext *ctx) {
    // PS4 SysV ABI: sceKernelMemoryPoolDecommit(addr, len, flags)
    // RDI: addr, RSI: len, RDX: flags
    void *addr = (void *)ctx->rdi;
    size_t len = (size_t)ctx->rsi;
    int flags = (int)ctx->rdx;

    int rc = sceKernelMemoryPoolDecommit(ctx, addr, len, flags);
    ctx->rax = (uint64_t)(int64_t)rc;
    SHIM_RETURN();
}
