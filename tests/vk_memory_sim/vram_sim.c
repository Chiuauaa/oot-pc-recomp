// NVN memory simulator for runtime/vk_memory.c on a real Vulkan device (any GPU, or lavapipe). See run.sh.
// Replays what the NVN replay does around one GPU-written storage binding (vk_exec_draw.c ssbo_info / note_written,
// vk_sync.c submit, completion callback at the fence) and checks guest memory and what the GPU reads.
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "host.h"
#include "nvn_api.h"
#include "vk.h"
#include "platform/platform.h"

// ---------------------------------------------------------------- runtime stubs
unsigned rt_trace;
const char *cfg_get(const char *n) { return getenv(n); }
const char *cfg_value(const char *n) { const char *e = getenv(n); return e ? e : !strcmp(n, "TOTK_MEMORY") ? "auto" : ""; }
int cfg_bool(const char *n) { const char *e = getenv(n); return !e || !*e || strcmp(e, "0"); }
uint64_t kern_now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
static int quiet = 1;
void rt_log(const char *fmt, ...) {
    char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    if (!quiet || strstr(b, "conflict")) printf("    [log] %s\n", b);
}
void rt_fatal(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); puts(""); abort(); }
int perf_gpu_timing(void) { return 0; }
void nvn_flush_stats(uint64_t out[2][3][2]) { memset(out, 0, sizeof(uint64_t) * 12); }
void vk_crumb(VkCommandBuffer cb, int k, uint64_t n) { (void)cb, (void)k, (void)n; }
static pthread_mutex_t qm = PTHREAD_MUTEX_INITIALIZER;
void vk_queue_lock(void) { pthread_mutex_lock(&qm); }
void vk_queue_unlock(void) { pthread_mutex_unlock(&qm); }
uint64_t vk_sync_issued(void) { return 0; }
void vk_completion_add(uint64_t f, void (*fn)(void *), void *a) { (void)f; fn(a); }
void vk_device_fault_report(VkResult r) { (void)r; }
void vk_texture_census(VkTexCensus *s) { memset(s, 0, sizeof *s); }
void vk_texture_descriptors_retire(uint64_t a, uint64_t s, uint64_t st) { (void)a, (void)s, (void)st; }
uint32_t vk_texture_epoch(void) { return 1; }
void vk_texture_range_gone(uint64_t lo, uint64_t size, VmaAllocation al) { (void)lo, (void)size, (void)al; }
void vk_texture_shutdown(void) {}
// memory.c: write watch straight from the platform layer, no freezing
uint64_t mem_written_pages(uint64_t a, uint64_t n, uint64_t *p, uint64_t m) { return plat_mem_written(a, n, p, m, 1); }
uint64_t mem_written_peek(uint64_t a, uint64_t n, uint64_t *p, uint64_t m) { return plat_mem_written(a, n, p, m, 0); }
int mem_freeze(uint64_t a) { (void)a; return 0; }
int mem_frozen(uint64_t a) { (void)a; return 0; }
void mem_thaw(uint64_t a, uint64_t n) { (void)a, (void)n; }
void mem_freeze_stats(uint64_t *a, uint64_t *b, uint64_t *c, uint64_t *d) { *a = *b = *c = *d = 0; }

// ---------------------------------------------------------------- Vulkan context on lavapipe
static VkCtx C;
VkCtx *vk_ctx(void) { return &C; }
static VkCommandPool cpool;
#define VKOK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) rt_fatal("%s: %d", #x, r_); } while (0)

static void ctx_init(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst;
    VKOK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1;
    vkEnumeratePhysicalDevices(inst, &n, &C.phys);
    vkGetPhysicalDeviceProperties(C.phys, &C.props);
    vkGetPhysicalDeviceMemoryProperties(C.phys, &C.mem_props);
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hp = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &hp};
    vkGetPhysicalDeviceProperties2(C.phys, &p2);
    C.min_imported_host_alignment = (uint32_t)hp.minImportedHostPointerAlignment;
    float pr = 1;
    VkDeviceQueueCreateInfo q = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &pr};
    const char *ext[] = {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .synchronization2 = 1};
    VkPhysicalDeviceVulkan12Features f12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13,
                                            .timelineSemaphore = 1};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f12, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &q, .enabledExtensionCount = 1, .ppEnabledExtensionNames = ext};
    VKOK(vkCreateDevice(C.phys, &dci, NULL, &C.device));
    C.gfx_family = 0;
    vkGetDeviceQueue(C.device, 0, 0, &C.gfx_queue);
    VmaAllocatorCreateInfo ai = {.physicalDevice = C.phys, .device = C.device, .instance = inst,
                                 .vulkanApiVersion = VK_API_VERSION_1_3};
    VKOK(vmaCreateAllocator(&ai, &C.vma));
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    VKOK(vkCreateCommandPool(C.device, &pci, NULL, &cpool));
    VkPhysicalDeviceMemoryProperties mp = C.mem_props;
    vk_mem_class_log(&C.props, &mp);
}

// One GPU submission: fill (GPU write) and/or copy to a readback buffer, waited for.
typedef struct { VkBuffer buf; VmaAllocation al; uint8_t *map; } RB;
static RB rb_new(uint64_t n) {
    RB r;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = n,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo a = {.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                 .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VmaAllocationInfo i;
    VKOK(vmaCreateBuffer(C.vma, &bi, &a, &r.buf, &r.al, &i));
    r.map = i.pMappedData;
    return r;
}
static void gpu(VkBuffer fill_buf, uint64_t fill_off, uint64_t fill_n, uint32_t val, VkBuffer rd_buf, uint64_t rd_off,
                RB *rb, uint64_t rd_n) {
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cpool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb;
    VKOK(vkAllocateCommandBuffers(C.device, &cai, &cb));
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb, &bi);
    if (fill_n) vkCmdFillBuffer(cb, fill_buf, fill_off, fill_n, val);
    if (rd_n) { VkBufferCopy c = {rd_off, 0, rd_n}; vkCmdCopyBuffer(cb, rd_buf, rb->buf, 1, &c); }
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                           .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &di);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    VKOK(vkQueueSubmit(C.gfx_queue, 1, &si, VK_NULL_HANDLE));
    vkQueueWaitIdle(C.gfx_queue);
    vkFreeCommandBuffers(C.device, cpool, 1, &cb);
}


#define BASE 0x1000000000ull
#define MIB (1ull << 20)
int main(void) {
    quiet = !getenv("SIM_VERBOSE");
    const int NP = 12; const uint64_t PS = 64 * MIB;
    if (!plat_mem_reserve(BASE, NP * PS, 1) || !plat_mem_commit(BASE, NP * PS)) rt_fatal("reserve");
    ctx_init();
    static pool_t pool[16];
    uint64_t used0, budget, used; int exact;
    vk_mem_vram(&used0, &budget, &exact);
    int on = 0;
    for (int i = 0; i < NP; i++) {
        pool[i].mem = (uint8_t *)(uintptr_t)(BASE + i * PS), pool[i].size = PS, pool[i].flags = 0x24;  // CPU-cached
        vk_pool_init(&pool[i]);
        // a 4 KiB vertex buffer at the start of the pool (the rest would be textures)
        on += vk_mem_mirror_probe(BASE + i * PS + 0x1000);
    }
    vk_mem_vram(&used, &budget, &exact);
    printf("  budget %.0f MiB | %d pools of 64 MiB, each with one 4 KiB buffer the GPU uses\n", budget / 1048576.0, NP);
    printf("  mirrors made: %d | new device memory: %.0f MiB (%.0f MiB per mirrored pool)\n", on,
           (used - used0) / 1048576.0, on ? (used - used0) / 1048576.0 / on : 0);
    return 0;
}
