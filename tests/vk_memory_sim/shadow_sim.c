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

// ---------------------------------------------------------------- scenario
#define BASE 0x1000000000ull
#define POOL (16ull << 20)
#define MIB (1ull << 20)
static int fails;
static const char *mode;
#define CHECK(name, cond) do { int ok_ = !!(cond); printf("  %-74s %s\n", name, ok_ ? "ok" : "FAIL"); fails += !ok_; } while (0)
static int all(const uint8_t *p, uint8_t v, uint64_t n) { for (uint64_t i = 0; i < n; i++) if (p[i] != v) return 0; return 1; }
static uint8_t *G(uint64_t off) { return (uint8_t *)(uintptr_t)(BASE + off); }

int main(int argc, char **argv) {
    mode = argc > 1 ? argv[1] : "?";
    quiet = !getenv("SIM_VERBOSE");
    if (!plat_mem_reserve(BASE, POOL, 1) || !plat_mem_commit(BASE, POOL)) rt_fatal("reserve");
    memset(G(0), 0x11, POOL);
    ctx_init();
    static pool_t pool;
    pool.mem = G(0), pool.size = POOL, pool.flags = 0;   // CPU-visible (cached) pool: import or shadow
    if (!vk_pool_init(&pool)) rt_fatal("vk_pool_init");
    RB rb = rb_new(MIB);
    VkMemSpan sp;

    printf("\n== %s\n", mode);
    // --- A: an 8 MiB GPU-written storage binding (unsized SSBO); the shader writes only its first 16 bytes.
    uint64_t R = 1 * MIB, RN = 8 * MIB;
    vk_mem_resolve(BASE + R, RN, &sp);               // bind_range
    vk_mem_gpu_pending(BASE + R, RN);                // note_written
    vk_mem_shadow_sync();                            // vk_sync.c prologue: before the submit
    gpu(sp.buffer, sp.offset, 16, 0xAAAAAAAA, 0, 0, NULL, 0);   // the shader runs (16 bytes)
    // the fence callback has not run yet; the game (CPU) writes new data inside and outside the range
    memset(G(R + 4 * MIB), 0x22, 4096);              // inside the pending range (e.g. next frame's constants)
    memset(G(12 * MIB), 0x33, 4096);                 // outside the range (control)
    // next submit: a draw reads those pages
    vk_mem_resolve(BASE + R + 4 * MIB, 4096, &sp);
    vk_mem_shadow_sync();
    gpu(0, 0, 0, 0, sp.buffer, sp.offset, &rb, 4096);
    int gpu_fresh_in = all(rb.map, 0x22, 4096);
    vk_mem_resolve(BASE + 12 * MIB, 4096, &sp);
    gpu(0, 0, 0, 0, sp.buffer, sp.offset, &rb, 4096);
    int gpu_fresh_out = all(rb.map, 0x33, 4096);
    // the first submit's fence: completion callback
    vk_gpu_written(BASE + R, RN);
    CHECK("A1. GPU reads a CPU write OUTSIDE the pending range (control)", gpu_fresh_out);
    CHECK("A2. GPU reads a CPU write INSIDE the pending range (8 MiB, 16 B written)", gpu_fresh_in);
    CHECK("A3. after the fence, guest memory keeps the CPU write", all(G(R + 4 * MIB), 0x22, 4096));
    CHECK("A4. after the fence, the GPU's 16 bytes are in guest memory", all(G(R), 0xAA, 16));
    CHECK("A5. the rest of the range is unchanged", all(G(R + 16), 0x11, 4096 - 16) && all(G(R + 2 * MIB), 0x11, 4096));

    // --- B: the CPU writes BEFORE the submit to bytes the GPU then writes: the GPU must win.
    uint64_t B = 10 * MIB;
    memset(G(B), 0x44, 64);
    vk_mem_resolve(BASE + B, 4096, &sp);
    vk_mem_gpu_pending(BASE + B, 4096);
    vk_mem_shadow_sync();
    gpu(sp.buffer, sp.offset, 64, 0xBBBBBBBB, 0, 0, NULL, 0);
    vk_gpu_written(BASE + B, 4096);
    CHECK("B1. CPU write before the submit, GPU write to the same bytes: GPU wins", all(G(B), 0xBB, 64));

    // --- C: two write-backs of one range (fence 1 arrives after submit 2): submit 2's value stays.
    uint64_t Cc = 11 * MIB;
    vk_mem_resolve(BASE + Cc, 4096, &sp);
    vk_mem_gpu_pending(BASE + Cc, 4096);
    vk_mem_shadow_sync();
    gpu(sp.buffer, sp.offset, 64, 0xC1C1C1C1, 0, 0, NULL, 0);          // submit 1
    vk_mem_resolve(BASE + Cc, 4096, &sp);
    vk_mem_gpu_pending(BASE + Cc, 4096);
    vk_mem_shadow_sync();
    gpu(sp.buffer, sp.offset, 64, 0xC2C2C2C2, 0, 0, NULL, 0);          // submit 2
    vk_gpu_written(BASE + Cc, 4096);                                    // fence 1
    vk_gpu_written(BASE + Cc, 4096);                                    // fence 2
    CHECK("C1. two write-backs of one range: the last submit's value stays", all(G(Cc), 0xC2, 64));

    // --- D: GPU and CPU write the SAME page after the submit (a real race in the game; informative)
    uint64_t D = 13 * MIB;
    vk_mem_resolve(BASE + D, 4096, &sp);
    vk_mem_gpu_pending(BASE + D, 4096);
    vk_mem_shadow_sync();
    gpu(sp.buffer, sp.offset, 64, 0xDDDDDDDD, 0, 0, NULL, 0);
    memset(G(D + 2048), 0x55, 64);
    vk_gpu_written(BASE + D, 4096);
    printf("  D.  (informative) same page, GPU bytes 0-63 and CPU bytes 2048-2111 after the submit: GPU %s, CPU %s\n",
           all(G(D), 0xDD, 64) ? "kept" : "lost", all(G(D + 2048), 0x55, 64) ? "kept" : "lost");

    // --- E: a read-write buffer. The CPU writes the input BEFORE the submit; the shader reads it
    // (bytes 1024..1087) and writes elsewhere in the same range. The GPU must read the new input.
    uint64_t E = 14 * MIB;
    vk_mem_resolve(BASE + E, 4096, &sp);
    vk_mem_shadow_sync();                       // the range has its shadow, up to date
    memset(G(E + 1024), 0x66, 64);              // the CPU prepares the input
    vk_mem_resolve(BASE + E, 4096, &sp);        // a GPU-written storage binding
    vk_mem_gpu_pending(BASE + E, 4096);
    vk_mem_shadow_sync();                       // submit
    gpu(sp.buffer, sp.offset, 16, 0xEEEEEEEE, sp.buffer, sp.offset + 1024, &rb, 64);
    int e_in = all(rb.map, 0x66, 64);
    vk_gpu_written(BASE + E, 4096);
    CHECK("E1. CPU input written before the submit, read inside a written range", e_in);
    CHECK("E2. after the fence, the input is still in guest memory", all(G(E + 1024), 0x66, 64));

    // --- F: GPU-only state (no CPU writes). Submit N+1 is already submitted when N's fence writes back;
    // N+1 writes after that. Submit N+2 must read what N+1 wrote, not what N wrote.
    uint64_t F = 15 * MIB;
    vk_mem_resolve(BASE + F, 4096, &sp); vk_mem_gpu_pending(BASE + F, 4096); vk_mem_shadow_sync();
    gpu(sp.buffer, sp.offset, 64, 0xA1A1A1A1, 0, 0, NULL, 0);                 // N runs
    vk_mem_resolve(BASE + F, 4096, &sp); vk_mem_gpu_pending(BASE + F, 4096); vk_mem_shadow_sync();   // N+1 submitted
    vk_gpu_written(BASE + F, 4096);                                            // N's fence
    gpu(sp.buffer, sp.offset, 64, 0xA2A2A2A2, 0, 0, NULL, 0);                 // N+1 runs
    vk_mem_resolve(BASE + F, 4096, &sp); vk_mem_shadow_sync();                // N+2
    gpu(0, 0, 0, 0, sp.buffer, sp.offset, &rb, 64);
    int f_ok = all(rb.map, 0xA2, 64);
    vk_gpu_written(BASE + F, 4096);                                            // N+1's fence
    CHECK("F1. GPU-only buffer, overlapping submits: N+2 reads N+1's value", f_ok);
    CHECK("F2. after both fences, guest memory holds N+1's value", all(G(F), 0xA2, 64));

    // --- G: the CPU writes page 1 after N+1 was submitted; the fences of N and N+1 come later.
    // Both submits predate the write, so the CPU write must survive both write-backs.
    uint64_t Gg = 6 * MIB;
    vk_mem_resolve(BASE + Gg, 8192, &sp); vk_mem_gpu_pending(BASE + Gg, 8192); vk_mem_shadow_sync();
    gpu(sp.buffer, sp.offset, 16, 0xB1B1B1B1, 0, 0, NULL, 0);                 // N
    vk_mem_resolve(BASE + Gg, 8192, &sp); vk_mem_gpu_pending(BASE + Gg, 8192); vk_mem_shadow_sync();
    gpu(sp.buffer, sp.offset, 16, 0xB2B2B2B2, 0, 0, NULL, 0);                 // N+1
    memset(G(Gg + 4096), 0x77, 4096);                                          // CPU, page 1
    vk_gpu_written(BASE + Gg, 8192);                                           // fence N
    vk_gpu_written(BASE + Gg, 8192);                                           // fence N+1
    CHECK("G1. a CPU write after two submits survives both fences", all(G(Gg + 4096), 0x77, 4096));
    CHECK("G2. the last submit's GPU bytes are in guest memory", all(G(Gg), 0xB2, 16));
    vk_mem_resolve(BASE + Gg + 4096, 4096, &sp); vk_mem_shadow_sync();
    gpu(0, 0, 0, 0, sp.buffer, sp.offset, &rb, 4096);
    CHECK("G3. the next submit reads the CPU write", all(rb.map, 0x77, 4096));

    printf("  result: %d failure(s)\n", fails);
    return fails != 0;
}
