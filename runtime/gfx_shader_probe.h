// Declaration guard for local SPIR-V numerical probes. Preserve the original interface verbatim and allow only
// private tracing state before the function bodies; resource reflection can then be shared with the original.
#ifndef GFX_SHADER_PROBE_H
#define GFX_SHADER_PROBE_H
#include <stdint.h>
#include <string.h>

static int gfx_probe_framed(const uint32_t *w, uint64_t n) {
    if (n < 5 || w[0] != 0x07230203) return 0;
    for (uint64_t i = 5; i < n;) {
        unsigned count = w[i] >> 16;
        if (!count || count > n - i) return 0;
        i += count;
    }
    return 1;
}

static int gfx_probe_declarations(const uint32_t *base, uint64_t nb, const uint32_t *probe, uint64_t np) {
    if (!gfx_probe_framed(base, nb) || !gfx_probe_framed(probe, np)) return 0;
    uint64_t prefix = 5;
    while (prefix < nb && (base[prefix] & 0xffff) != 54) prefix += base[prefix] >> 16;
    if (prefix == nb || np <= prefix || memcmp(probe + 5, base + 5, (size_t)(prefix - 5) * 4)) return 0;
    for (uint64_t i = prefix; i < np; i += probe[i] >> 16) {
        unsigned op = probe[i] & 0xffff, count = probe[i] >> 16;
        if (op == 54) return 1;
        if (!((op == 20 && count == 2) || (op == 21 && count == 4) || (op == 23 && count == 4) ||
              (op == 32 && count == 4 && probe[i + 2] == 6) || (op == 43 && count >= 4) ||
              (op == 59 && count >= 4 && probe[i + 3] == 6))) return 0;
    }
    return 0;
}
#endif
