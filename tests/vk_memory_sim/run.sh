#!/bin/sh
# Tests of runtime/vk_memory.c and the Linux write watch, run on a real Vulkan device (any GPU, or Mesa's lavapipe)
# without the game: the real vk_memory.c and platform/linux.c, with the rest of the runtime replaced by stubs
# (shadow_sim.c). They replay what the NVN replay does around a GPU-written storage binding and check guest memory and
# what the GPU reads.
#
#   write_watch_test, write_watch_race_test  userfaultfd write watch: semantics and concurrent writes vs. scans
#   shadow_sim    scenarios A-K: shadow storage vs. import, with OOT_SHADOW_CPU_WINS and OOT_SHADOW_MERGE
#   mirror_sim    scenario H: the VRAM mirror path with the GPU held back by an event (overlapping submissions)
#   vram_sim      device memory taken by whole-pool mirrors, and OOT_MIRROR_MAX_PCT
#
# Linux only (userfaultfd, kernel 6.7+). Needs clang, the Vulkan headers and loader, and vk_mem_alloc.h.
# Usage, from the repository root: sh tests/vk_memory_sim/run.sh [build directory]
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; T="$ROOT/tests/vk_memory_sim"; R="$ROOT/runtime"
O="${1:-$ROOT/build/vk_memory_sim}"; mkdir -p "$O"
F="-std=gnu11 -O1 -g -mavx2 -mfma -mbmi2 -w -I$R -I$R/platform -I$R/third_party -I$T/sdl_stub"
clang $F -c "$R/vk_memory.c" -o "$O/vk_memory.o"
clang $F -c "$R/platform/linux.c" -o "$O/linux.o"
clang $F -c "$R/platform/common.c" -o "$O/common.o"
clang++ -std=c++17 -O1 -w -I"$R" -I"$R/third_party" -c "$R/vk_vma.cpp" -o "$O/vma.o"
clang -O1 -c "$T/rec_stubs.c" -o "$O/rec_stubs.o"
clang $F "$T/write_watch_test.c" "$R/platform/linux.c" -o "$O/write_watch_test" -lpthread -ldl
clang $F -O2 "$T/write_watch_race_test.c" "$R/platform/linux.c" -o "$O/write_watch_race_test" -lpthread -ldl
for s in shadow_sim mirror_sim vram_sim; do
    clang $F -c "$T/$s.c" -o "$O/$s.o"
    clang++ "$O/$s.o" "$O/rec_stubs.o" "$O/vk_memory.o" "$O/linux.o" "$O/common.o" "$O/vma.o" -lvulkan -lpthread -ldl -o "$O/$s"
done
cd "$O"
./write_watch_test; ./write_watch_race_test
# Expected: import 0 failures; shadow 10 (A2 A3 E1 E2 G1 G3 I1 I2 J1 K1); CPU_WINS 3 (I1 J1 K1: it decides per
# page, so GPU bytes in a page the CPU also wrote are lost); MERGE 0.
TOTK_SHADOW=0 ./shadow_sim "import (Windows default)" || true
./shadow_sim "shadow (Linux default)" || true
OOT_SHADOW_CPU_WINS=1 ./shadow_sim "shadow, OOT_SHADOW_CPU_WINS=1" || true
OOT_SHADOW_MERGE=1 ./shadow_sim "shadow, OOT_SHADOW_MERGE=1" || true
# Expected: H1-H3 fail without a fix on both import and shadow; with CPU_WINS or MERGE only H1 (the submission
# already queued when the CPU wrote reads the older page; the next one reads the CPU's).
TOTK_SHADOW=0 TOTK_MEMORY=mirror ./mirror_sim "import + mirror" || true
TOTK_MEMORY=mirror ./mirror_sim "shadow + mirror" || true
TOTK_MEMORY=mirror OOT_SHADOW_CPU_WINS=1 ./mirror_sim "shadow + mirror, OOT_SHADOW_CPU_WINS=1" || true
TOTK_MEMORY=mirror OOT_SHADOW_MERGE=1 ./mirror_sim "shadow + mirror, OOT_SHADOW_MERGE=1" || true
TOTK_SHADOW=0 TOTK_MEMORY=mirror ./vram_sim
TOTK_SHADOW=0 TOTK_MEMORY=mirror OOT_MIRROR_MAX_PCT=3 ./vram_sim
