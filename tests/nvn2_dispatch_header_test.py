"""Run the production driver-bank fill with NVN 1/2 layouts and real dispatch dimensions."""
import argparse
from pathlib import Path
import subprocess
from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/clang.exe')
    args = parser.parse_args()
    production = (ROOT / 'runtime/vk_exec_draw.c').read_text()
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "nvn2_c0.h"
#define VK_C0_BYTES NVN2_C0_BYTES
enum {NVN_STAGE_VS=0, NVN_STAGE_FS=1, NVN_STAGE_GS=4, NVN_STAGE_CS=5};
static const Nvn2C0Layout *c0_lay;
static uint32_t c0_push[8][64];
static void rt_fatal(const char *message) { (void)message; abort(); }
''' + function(production, 'static void c0_table(') + '\n' + function(production, 'void vk_c0_fill(') + r'''
static unsigned word(const unsigned char *p) { unsigned value; memcpy(&value,p,4); return value; }
int main(void) {
    uint8_t bank[NVN2_C0_BYTES];
    p_bind_buf ssbo[16]={{0}},ubo[16]={{0}};
    Nvn2C0Layout layout={.ubos=1,.ssbos=1,.roots={1}};
    const uint32_t groups[3]={1,3,5};
    c0_lay=&layout; c0_push[0][0]=0x12345678;
    ubo[0].addr=0x12345000; ubo[0].size=64;
    ssbo[0].addr=0x56789000; ssbo[0].size=192;
    vk_c0_fill(bank,NVN_STAGE_CS,ssbo,ubo,groups,0);
    assert(word(bank)==0 && word(bank+4)==1 && word(bank+8)==3 && word(bank+12)==5);
    assert(word(bank+16)==0x12345678 && word(bank+32)==0x12345000 && word(bank+48)==0x56789000);
    assert(((word(bank+4)<<5)*(word(bank+8)<<5))==3072);
    c0_lay=NULL;
    vk_c0_fill(bank,NVN_STAGE_CS,ssbo,ubo,groups,0);
    assert(!memcmp(bank+0x200,groups,sizeof groups) && word(bank+4)==0);
    assert(word(bank+0x210)==0x12345000 && word(bank+0x310)==0x56789000);
    puts("Production dispatch header: NVN 1 and NVN 2 passed");
    return 0;
}
'''
    out = ROOT / 'build/test/nvn2-dispatch-header'
    out.mkdir(parents=True, exist_ok=True)
    src = out / 'header.c'
    src.write_text(source)
    for level in ('0', '2'):
        exe = out / f'header-o{level}.exe'
        subprocess.run([args.cc, f'-O{level}', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'runtime'), str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
