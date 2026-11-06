// Concurrent CPU writes against write-watch scans with reset (runtime/platform/linux.c): no write may be lost.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include "platform.h"
#define PG 4096ull
#define BASE 0x2000000000ull
#define NPG 256
#define ITER 100000
static atomic_long wseq, done, wbegin;
static atomic_int stop;
static uint64_t pages[NPG];
static void *noise(void *a) { long k = (long)a; while (!atomic_load(&stop)) for (int i = 1; i < NPG; i++) ((volatile uint8_t *)(BASE + i * PG))[k] = 1; return 0; }
static void *writer(void *a) { (void)a;
  for (long i = 1; i <= ITER; i++) { atomic_store(&wbegin, i); ((volatile uint8_t *)BASE)[i & 4095] = (uint8_t)i; atomic_store(&wseq, i); while (atomic_load(&done) < i) ; }
  return 0; }
int main(void) {
  plat_mem_reserve(BASE, NPG * PG, 1); plat_mem_commit(BASE, NPG * PG); plat_mem_written(BASE, NPG*PG, pages, NPG, 1);
  pthread_t w, nz[2]; for (long k = 0; k < 2; k++) pthread_create(&nz[k], 0, noise, (void *)k);
  pthread_create(&w, 0, writer, 0);
  long reported = 0, lost = 0, scans = 0;
  while (reported < ITER) {
    long wi = atomic_load(&wseq);
    uint64_t n = plat_mem_written(BASE, NPG * PG, pages, NPG, 1); scans++;
    int saw = 0; for (uint64_t i = 0; i < n; i++) saw |= pages[i] == BASE;
    long wa = atomic_load(&wbegin);
    if (saw) reported = wa > reported ? wa : reported;
    else if (wi > reported) { lost++; reported = wi; }   // write wi happened before this scan and was never reported
    atomic_store(&done, wi > atomic_load(&done) ? wi : atomic_load(&done));
  }
  atomic_store(&done, ITER); atomic_store(&stop, 1); pthread_join(w, 0); for (int k = 0; k < 2; k++) pthread_join(nz[k], 0);
  printf("15. concurrent write vs scan+reset: %ld writes, %ld scans, %ld lost -> %s\n", (long)ITER, scans, lost, lost ? "FAIL" : "PASS");
  return lost != 0;
}
