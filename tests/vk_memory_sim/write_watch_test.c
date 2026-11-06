// Write-watch semantics test for runtime/platform/linux.c (userfaultfd WP async + PAGEMAP_SCAN).
// Reference behaviour: Windows GetWriteWatch(WRITE_WATCH_FLAG_RESET), which the Windows build uses.
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include "platform.h"

#define PG 4096ull
#define BASE 0x1000000000ull
#define NPG 64
static uint64_t pages[4096];
static int fails;

static uint64_t scan(int reset) { return plat_mem_written(BASE, NPG * PG, pages, 4096, reset); }
static int has(uint64_t n, uint64_t addr) {
    for (uint64_t i = 0; i < n; i++) if (pages[i] == addr) return 1;
    return 0;
}
#define CHECK(name, cond) do { int ok_ = (cond); printf("%-58s %s\n", name, ok_ ? "PASS" : "FAIL"); fails += !ok_; } while (0)
static volatile uint8_t *P(int i) { return (volatile uint8_t *)(uintptr_t)(BASE + (uint64_t)i * PG); }

static void *writer(void *a) { (void)a; for (int i = 40; i < 48; i++) P(i)[7] = 1; return NULL; }

int main(void) {
    if (!plat_mem_reserve(BASE, NPG * PG, 1) || !plat_mem_commit(BASE, NPG * PG)) { puts("reserve/commit failed"); return 2; }
    uint64_t n = scan(1);
    CHECK("1. fresh committed range: nothing written", n == 0);
    if (n == UINT64_MAX) { puts("write watch unavailable (userfaultfd)"); return 2; }

    P(0)[0] = 1; P(5)[100] = 2;
    n = scan(1);
    CHECK("2. CPU writes on pages 0 and 5 reported", n == 2 && has(n, BASE) && has(n, BASE + 5 * PG));
    n = scan(1);
    CHECK("3. after reset: nothing reported", n == 0);

    volatile uint8_t r = P(9)[0]; (void)r;
    n = scan(1);
    CHECK("4. read only (zero page): not reported", n == 0);

    P(5)[1] = 3;
    n = scan(0);
    CHECK("5. rewrite after reset reported (no reset this time)", n == 1 && has(n, BASE + 5 * PG));
    n = scan(1);
    CHECK("6. still reported without reset in between", n == 1);

    // kernel writes into guest memory: pread (file load path)
    char tmp[] = "/tmp/wwXXXXXX"; int fd = mkstemp(tmp);
    char buf[3 * PG]; memset(buf, 0xab, sizeof buf); if (write(fd, buf, sizeof buf) != (ssize_t)sizeof buf) return 2;
    ssize_t got = pread(fd, (void *)P(12), 3 * PG, 0);
    n = scan(1);
    CHECK("7. pread() by the kernel into pages 12-14 reported", got == 3 * PG && n == 3 && has(n, BASE + 12 * PG) && has(n, BASE + 14 * PG));
    struct iovec iov = {(void *)P(20), PG};
    got = preadv(fd, &iov, 1, 0);
    n = scan(1);
    CHECK("8. preadv() into page 20 reported", got == PG && n == 1 && has(n, BASE + 20 * PG));
    close(fd); unlink(tmp);

    // decommit + recommit + write
    P(30)[0] = 1; scan(1);
    plat_mem_decommit(BASE + 30 * PG, PG); plat_mem_commit(BASE + 30 * PG, PG);
    n = scan(1);
    CHECK("9. decommit+recommit without write: not reported", n == 0);
    P(30)[0] = 2;
    n = scan(1);
    CHECK("10. write after decommit+recommit reported", n == 1 && has(n, BASE + 30 * PG));

    // protect read-only then RW then write
    plat_mem_protect(BASE + 33 * PG, PG, PLAT_PROT_READ); plat_mem_protect(BASE + 33 * PG, PG, PLAT_PROT_RW);
    P(33)[0] = 9;
    n = scan(1);
    CHECK("11. write after protect RO->RW reported", n == 1 && has(n, BASE + 33 * PG));

    pthread_t t; pthread_create(&t, NULL, writer, NULL); pthread_join(t, NULL);
    n = scan(1);
    CHECK("12. writes from another thread (8 pages) reported", n == 8);

    // write to a page, scan with reset, write same page again in the same epoch many times
    int lost = 0;
    for (int k = 0; k < 2000; k++) { P(50)[k & 4095] = (uint8_t)k; n = scan(1); lost += !(n == 1 && has(n, BASE + 50 * PG)); }
    CHECK("13. 2000 write/reset cycles: none lost", lost == 0);

    // memcpy over a page boundary, partial
    memcpy((void *)(P(55) + PG - 8), "0123456789abcdef", 16);
    n = scan(1);
    CHECK("14. 16-byte write straddling pages 55/56: both reported", n == 2);

    printf("\n%d failure(s)\n", fails);
    return fails != 0;
}
