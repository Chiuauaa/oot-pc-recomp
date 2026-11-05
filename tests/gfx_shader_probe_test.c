// Exercise the production declaration guard before numerical probes reach Vulkan.
#include "gfx_shader_probe.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    uint32_t base[] = {0x07230203,0x10300,0,20,0,
        3u<<16|22,1,32, 4u<<16|71,8,30,0,
        5u<<16|54,1,10,0,2, 1u<<16|56};
    uint32_t probe[64];
    size_t nb = sizeof base / sizeof *base;
    memcpy(probe, base, sizeof base);
    assert(gfx_probe_declarations(base, nb, probe, nb));
    // A private uint pointer, constant and variable are allowed before the first function.
    uint32_t extra[] = {4u<<16|32,20,6,1, 4u<<16|43,1,21,0, 5u<<16|59,20,22,6,21};
    memcpy(probe + 12, extra, sizeof extra);
    memcpy(probe + 25, base + 12, (nb - 12) * 4);
    size_t np = nb + 13;
    probe[3] = 23;
    assert(gfx_probe_declarations(base, nb, probe, np));
    probe[23] = 2;  // Uniform instead of Private.
    assert(!gfx_probe_declarations(base, nb, probe, np));
    probe[23] = 6;
    probe[11] = 1;  // Changing an output location invalidates shared reflection.
    assert(!gfx_probe_declarations(base, nb, probe, np));
    probe[11] = 0;
    probe[12] = 0;
    assert(!gfx_probe_declarations(base, nb, probe, np));
    probe[12] = 4u<<16|32;
    assert(!gfx_probe_declarations(base, nb, probe, np - 2));
    assert(!gfx_probe_declarations(base, nb, probe, 4));
    assert(!gfx_probe_declarations(base, 12, probe, np));
    puts("Shader probe declaration guard: passed");
    return 0;
}
