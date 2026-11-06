"""Exercise production pending-counter lifetime and stale-read prevention without a GPU."""
import argparse
import os
from pathlib import Path
import subprocess

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/clang.exe')
    args = parser.parse_args()
    source = (ROOT / 'runtime/vk_exec_xfer.c').read_text()
    begin = source.index('// Reports queued by replay must remain known')
    pending = source[begin:source.index('// ---- occlusion counts', begin)]
    read = function(source, 'static int counter_word(')
    prelude = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
static uint64_t completed;
static unsigned guest_reads, checks;
static uint64_t guest[4096][2];
static uint64_t vk_sync_fence_done(void) { return completed; }
static void rt_fatal(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); abort();
}
typedef struct { int cls; } VkMemSpan;
static int nvn_gpu_only(uint64_t addr) { return addr == 1; }
static int vk_mem_resolve(uint64_t addr, uint64_t size, VkMemSpan *m) {
    uint64_t lo = (uintptr_t)guest, hi = lo + sizeof guest;
    m->cls = 0;
    return addr >= lo && addr <= hi && size <= hi - addr;
}
#define VK_MUSE_CPU_READ 1
static void vk_mem_count(int cls, int use, uint64_t size) {
    (void)cls; (void)use; (void)size; guest_reads++;
}
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
'''
    driver = r'''
int main(void) {
    uint64_t value = 123;
    completed = 9;
    for (unsigned i = 0; i < 2048; i++) {
        guest[i][0] = i % 3 ? i + 100 : 0;
        pending_note((uintptr_t)&guest[i], 10, 1, completed);
    }
    // The old 256-report ring forgot the oldest 1792 reports and read these stale words.
    for (unsigned i = 0; i < 2048; i++) {
        CHECK(counter_word((uintptr_t)&guest[i], &value) == 2);
        CHECK(value == 0);
    }
    CHECK(guest_reads == 0);
    completed = 10;
    for (unsigned i = 0; i < 2048; i++) {
        CHECK(counter_word((uintptr_t)&guest[i], &value) == 1);
        CHECK(value == guest[i][0]);
    }
    CHECK(guest_reads == 2048);
    // Latest report wins, including changes from sample queries to known timestamp zero.
    guest[0][0] = 99;
    pending_note((uintptr_t)&guest[0], 20, 1, completed);
    pending_note((uintptr_t)&guest[0], 20, 0, completed);
    CHECK(counter_word((uintptr_t)&guest[0], &value) == 1 && value == 0);
    CHECK(guest_reads == 2048);
    pending_note((uintptr_t)&guest[0], 21, 1, completed);
    CHECK(counter_word((uintptr_t)&guest[0], &value) == 2 && value == 0);
    CHECK(counter_word(1, &value) == 0);
    CHECK(counter_word(2, &value) == 0);

    // Check growth, pruning and repeated writes against an independent newest-report model.
    uint64_t fences[4096] = {0};
    unsigned unknown[4096] = {0};
    free(pending); pending = NULL; pending_cap = pending_used = 0;
    uint32_t rng = 0x714a3;
    for (unsigned step = 0; step < 120000; step++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        unsigned index = rng & 4095;
        uint64_t fence = 100 + step / 800;
        completed = fence - 3;
        if ((rng >> 12) % 3) {
            fences[index] = fence;
            unknown[index] = (rng >> 17) & 1;
            pending_note((uintptr_t)&guest[index], fence, unknown[index], completed);
        }
        unsigned reads_before = guest_reads;
        int result = counter_word((uintptr_t)&guest[index], &value);
        if (fences[index] > completed) {
            CHECK(result == (unknown[index] ? 2 : 1) && value == 0);
            CHECK(guest_reads == reads_before);
        } else {
            CHECK(result == 1 && value == guest[index][0]);
            CHECK(guest_reads == reads_before + 1);
        }
    }
    free(pending);
    printf("pending counters: %u checks passed\n", checks);
    return 0;
}
'''
    out = ROOT / 'build/tests/pending-counter'
    out.mkdir(parents=True, exist_ok=True)
    path = out / 'pending_counter.c'
    path.write_text(prelude + pending + read + driver)
    for opt in ('-O0', '-O2'):
        exe = out / ('pending_counter_' + opt[1:] + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([args.cc, '-std=c11', opt, '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
