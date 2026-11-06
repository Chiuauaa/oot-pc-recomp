"""Exercise production task/mesh client request framing with CPU-only translator stubs."""
from pathlib import Path
import subprocess
from nvn2_compute_c0_test import function
ROOT = Path(__file__).resolve().parents[1]
PRELUDE = r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#define PATH_CAP 1024
#define LINE_MAX_LEN 8192
#define COUNT(a) ((int)(sizeof(a)/sizeof(*(a))))
typedef int PlatMutex;
typedef struct { void *child; size_t in_len; } Svc;
static PlatMutex mesh_lock;
static Svc mesh_svc;
static char mesh_exe[PATH_CAP]="mesh.exe", cache_dir[PATH_CAP]="cache";
static uint8_t mesh_id[32];
static int mesh_ready, native_kind=6, mode, disk_calls, writes, exchanges, failed;
static volatile int64_t n_failures,n_xlated;
struct GfxShader { int stage; } shader;
static char request[20000];
static void plat_mutex_lock(PlatMutex *m) {(void)m;}
static void plat_mutex_unlock(PlatMutex *m) {(void)m;}
static int mesh_init(void) {return 1;}
int gfx_shader_is_nvn2(const uint8_t *c) {(void)c;return 1;}
static int nvn2_sizes(const uint8_t *c,uint32_t *a,uint32_t *b) {(void)c;*a=8;*b=8;return 1;}
uint32_t nvn2_native_kind(const uint8_t *c,const uint8_t *b) {(void)c;(void)b;return native_kind;}
void rt_sha256(const void *p,size_t n,uint8_t out[32]) {(void)p;(void)n;memset(out,0xab,32);}
static const GfxShader *mem_find(const uint8_t *k) {(void)k;return NULL;}
static int known_failed(const uint8_t *k) {(void)k;return 0;}
static GfxShader *disk_load_from(const uint8_t *k,const uint8_t *id,int p,int flags) {
 (void)k;(void)id;(void)p;(void)flags;disk_calls++;
 if(mode==4 || (mode==0 && disk_calls%2==0))return &shader;return NULL;
}
int gfx_shader_stage(const GfxShader *s) {return s->stage;}
static const GfxShader *mem_insert(GfxShader *s) {return s;}
static void shader_free(GfxShader *s) {(void)s;}
static void key_hex(const uint8_t k[32],char out[65]) {for(int i=0;i<32;i++)sprintf(out+i*2,"%02x",k[i]);}
static void mkdir_p(const char *p) {(void)p;}
static int file_exists(const char *p) {(void)p;return 0;}
static int plat_file_write_atomic(const char *p,const void *v,size_t n) {(void)p;(void)v;(void)n;writes++;return 1;}
#define rt_log(...) ((void)0)
static void *plat_child_start(const char *p,const char *a,uint32_t *e) {(void)p;(void)a;(void)e;return (void*)1;}
static uint64_t plat_now_ms(void) {return 1;}
static int child_exchange(Svc *s,const char *line,char *reply,size_t cap) {
 (void)s;exchanges++;assert(strlen(line)<sizeof request);strcpy(request,line);
 if(mode==3)return 0;
 char key[65];memset(key,'a',64);key[64]=0;
 if(mode!=1)for(int i=0;i<32;i++)memcpy(key+2*i,"ab",2);
 snprintf(reply,cap,"ok %s",key);return 1;
}
static void child_stop(Svc *s) {s->child=NULL;}
static void note_failed(const uint8_t *k) {(void)k;failed++;}
static void plat_inc64(volatile int64_t *p) {++*p;}
uint64_t gfx_shader_nvn2_hash(const uint8_t *c,const uint8_t *b) {(void)c;(void)b;return 1;}
static const char *tex_names[]={"Unknown","Texture2DArrayShadow"};
static const char *fmt_names[]={"Unknown","R32G32B32A32Float"};
'''
TEST = r'''
int main(void) {
 unsigned char code[8]={0},ctl[8]={0};GfxShaderAnswers a={0};
 memset(a.tex_type,1,32);memset(a.img_type,1,32);memset(a.img_format,1,32);
 shader.stage=GFX_STAGE_TS;
 assert(!gfx_shader_get_mesh(GFX_STAGE_TS,ctl,code,NULL));
 assert(!gfx_shader_get_mesh(GFX_STAGE_MS,ctl,code,&a));assert(!exchanges);
 assert(gfx_shader_get_mesh(GFX_STAGE_TS,ctl,code,&a)==&shader);
 assert(exchanges==1 && writes==2 && strlen(request)>2048);
 assert(strstr(request,"\"31\":{\"type\":\"Texture2DArrayShadow\""));
 char escaped[32];assert(json_escape("a\\b\"c",escaped,sizeof escaped));
 assert(!json_escape("bad\npath",escaped,sizeof escaped));assert(!json_escape("long",escaped,2));
 mode=1;disk_calls=0;assert(!gfx_shader_get_mesh(GFX_STAGE_TS,ctl,code,&a));assert(failed==1);
 mode=3;disk_calls=0;assert(!gfx_shader_get_mesh(GFX_STAGE_TS,ctl,code,&a));assert(!mesh_svc.child);
 mode=0;disk_calls=0;native_kind=7;shader.stage=GFX_STAGE_MS;
 assert(gfx_shader_get_mesh(GFX_STAGE_MS,ctl,code,&a)==&shader);
 mode=4;shader.stage=GFX_STAGE_TS;assert(!gfx_shader_get_mesh(GFX_STAGE_MS,ctl,code,&a));
 printf("Mesh client: full descriptors, escaping, reply identity, disconnect and stage checks passed\n");
 return 0;
}
'''
def main():
 out=ROOT/'build/tests/mesh-client';out.mkdir(parents=True,exist_ok=True)
 source=(ROOT/'runtime/gfx_shader.c').read_text()
 body=PRELUDE+function(source,'static int json_escape(')+function(source,'const GfxShader *gfx_shader_get_mesh(')+TEST
 f=out/'client.c';f.write_text(body)
 for opt in ['O0','O2']:
  exe=out/(opt+'.exe');subprocess.run(['C:/msys64/ucrt64/bin/clang.exe','-std=gnu11','-'+opt,'-I',str(ROOT/'runtime'),str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
