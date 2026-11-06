"""Exercise production stage/table mappings and driver-bank packing for task/mesh bindings."""
from pathlib import Path
import re,subprocess
from nvn2_compute_c0_test import function
ROOT=Path(__file__).resolve().parents[1]
PRELUDE=r'''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "gfx.h"
#include "vk_exec.h"
#define VK_C0_BYTES NVN2_C0_BYTES
static const Nvn2C0Layout *c0_lay;
static uint32_t c0_push[8][64];
void rt_fatal(const char *s,...) { (void)s;abort(); }
'''
TEST=r'''
static uint32_t word(const void *p) {uint32_t x;memcpy(&x,p,4);return x;}
int main(void) {
 unsigned checks=0;unsigned char task[VK_C0_BYTES],mesh[VK_C0_BYTES];
 p_bind_buf sb[16]={{0}},ub[16]={{0}};const uint32_t dims[3]={3,5,7};
 assert(nvn_stage_of(GFX_STAGE_TS)==VK_EXEC_TASK);checks++;
 assert(nvn_stage_of(GFX_STAGE_MS)==VK_EXEC_MESH);checks++;
 int stages[]={NVN_STAGE_VS,NVN_STAGE_FS,NVN_STAGE_CS,NVN_STAGE_GS,VK_EXEC_TASK,VK_EXEC_MESH};
 for(int i=0;i<6;i++){assert(c0_cache_index(stages[i])==i);checks++;}
 assert(c0_cache_index(-1)==-1&&c0_cache_index(99)==-1);checks++;
 for(unsigned i=0;i<16;i++){sb[i].addr=0x123456780000ULL+0x1000*i;sb[i].size=0x100+i;}
 vk_c0_fill(task,VK_EXEC_TASK,sb,ub,dims,19);
 for(unsigned i=0;i<16;i++){sb[i].addr=0x876543210000ULL+0x1000*i;sb[i].size=0x200+i;}
 vk_c0_fill(mesh,VK_EXEC_MESH,sb,ub,dims,23);
 for(unsigned i=0;i<16;i++){
  uint64_t ta,ma;memcpy(&ta,task+0x110+16*i,8);memcpy(&ma,mesh+0x310+16*i,8);
  assert(ta==0x123456780000ULL+0x1000*i&&ma==sb[i].addr);checks++;
  assert(word(task+0x118+16*i)==0x100+i&&word(mesh+0x318+16*i)==0x200+i);checks++;
  int is_ubo=99;GfxShaderRes res={.cbuf_slot=0,.handle=(0x110+16*i)/4};
  assert(ssbo_slot(&res,VK_EXEC_TASK,&is_ubo)==(int)i&&!is_ubo);checks++;
  res.handle=(0x310+16*i)/4;assert(ssbo_slot(&res,VK_EXEC_MESH,&is_ubo)==(int)i&&!is_ubo);checks++;
 }
 assert(word(task+0x644)==19&&word(mesh+0x644)==23);checks++;
 // Mesh table does not also activate the compute-only UBO alias table.
 GfxShaderRes bad={.cbuf_slot=0,.handle=0x210/4};int ubo;
 assert(ssbo_slot(&bad,VK_EXEC_MESH,&ubo)==-1&&!ubo);checks++;
 bad.handle=0x110/4+1;assert(ssbo_slot(&bad,VK_EXEC_TASK,&ubo)==-1);checks++;
 bad.cbuf_slot=3;bad.handle=0x110/4;assert(ssbo_slot(&bad,VK_EXEC_TASK,&ubo)==-1);checks++;
 assert(!word(task+0x210)&&!word(mesh+4));checks++;
 puts("Task/mesh bindings: isolated driver banks and all 16 descriptor slots passed");
 printf("%u production binding checks passed\n",checks);return 0;
}
'''
def main():
 source=(ROOT/'runtime/vk_exec_draw.c').read_text();macros='\n'.join(re.findall(r'^#define NVN_STAGE_.*$',source,re.M))
 pieces='\n'.join(function(source,n) for n in ['static int nvn_stage_of(', 'static int c0_cache_index(', 'static void c0_table(', 'void vk_c0_fill(', 'static int ssbo_slot('])
 out=ROOT/'build/tests/mesh-bindings';out.mkdir(parents=True,exist_ok=True);f=out/'bindings.c';f.write_text(PRELUDE+macros+'\n'+pieces+TEST)
 for opt in ['O0','O2']:
  exe=out/(opt+'.exe');subprocess.run(['C:/msys64/ucrt64/bin/clang.exe','-std=gnu11','-'+opt,'-I',str(ROOT/'runtime'),'-I','C:/msys64/ucrt64/include/vma',str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
