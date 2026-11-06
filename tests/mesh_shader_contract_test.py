"""Validate production task/mesh reflection against original synthetic SPIR-V metadata."""
from pathlib import Path
import subprocess
from nvn2_compute_c0_test import function
ROOT=Path(__file__).resolve().parents[1]
PRELUDE=r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gfx.h"
#include "vk_mesh_contract.h"
struct GfxShader { int stage; uint32_t *spirv; uint64_t words; GfxShaderMesh mesh; };
#define COUNT(a) (sizeof(a)/sizeof(*(a)))
'''
TEST=r'''
static unsigned checks;
#define CHECK(x) do { checks++;if(!(x)){fprintf(stderr,"mesh contract failure line %d\n",__LINE__);exit(1);} }while(0)
#define I(op,n) (((n)<<16)|(op))
static uint32_t module[]={0x07230203,0x00010500,0,20,0,
 I(17,2),5283,
 I(15,5),5365,1,0x6e69616d,0,
 I(16,6),1,17,32,1,1,
 I(16,4),1,26,96,
 I(16,4),1,5270,64,
 I(16,3),1,5298,
 I(21,4),2,32,0,
 I(43,4),2,3,32,
 I(28,4),4,2,3,
 I(32,4),5,5402,4,
 I(59,4),5,6,5402};
static const char *json="{\"mesh\":{\"version\":1,\"local\":[32,1,1],\"payload_words\":32,\"max_vertices\":96,\"max_primitives\":64,\"topology\":3,\"inferred_registers\":true}}";
int main(int argc,char **argv) {
 GfxShader s={.stage=GFX_STAGE_MS,.spirv=module,.words=sizeof(module)/4};
 CHECK(mesh_json(json,json+strlen(json),s.stage,&s.mesh));CHECK(mesh_spirv(&s));
 GfxShaderMesh good=s.mesh;
 VkPhysicalDeviceMeshShaderPropertiesEXT limits={.maxMeshWorkGroupSize={128,128,128},.maxMeshWorkGroupInvocations=128,
 .maxTaskWorkGroupSize={128,128,128},.maxTaskWorkGroupInvocations=128,.maxMeshOutputVertices=256,
 .maxMeshOutputPrimitives=256,.maxTaskPayloadSize=16384,.maxTaskPayloadAndSharedMemorySize=32768,
 .maxMeshPayloadAndSharedMemorySize=32768};
 CHECK(!vk_mesh_execution_limits(NULL,&good,&limits));
 CHECK(!vk_mesh_execution_limits(&good,&good,&limits));
 GfxShaderMesh mismatch=good;mismatch.payload_words++;CHECK(vk_mesh_execution_limits(&mismatch,&good,&limits));
 mismatch=good;mismatch.local[0]=UINT32_MAX;CHECK(vk_mesh_execution_limits(NULL,&mismatch,&limits));
 mismatch=good;mismatch.local[0]=64;mismatch.local[1]=4;CHECK(vk_mesh_execution_limits(NULL,&mismatch,&limits));
 mismatch=good;mismatch.max_vertices=257;CHECK(vk_mesh_execution_limits(NULL,&mismatch,&limits));
 mismatch=good;mismatch.max_primitives=257;CHECK(vk_mesh_execution_limits(NULL,&mismatch,&limits));
 mismatch=good;mismatch.payload_words=UINT32_MAX;CHECK(vk_mesh_execution_limits(NULL,&mismatch,&limits));
 mismatch=good;mismatch.payload_words=5000;CHECK(vk_mesh_execution_limits(&mismatch,&mismatch,&limits));
 s.mesh.local[0]++;CHECK(!mesh_spirv(&s));s.mesh=good;
 s.mesh.payload_words++;CHECK(!mesh_spirv(&s));s.mesh=good;
 s.mesh.max_vertices++;CHECK(!mesh_spirv(&s));s.mesh=good;
 s.mesh.max_primitives++;CHECK(!mesh_spirv(&s));s.mesh=good;
 s.stage=GFX_STAGE_TS;CHECK(!mesh_spirv(&s));s.stage=GFX_STAGE_MS;
 for(size_t i=5;i<s.words;){uint32_t old=module[i],op=old&65535,n=old>>16;
  module[i]=op;CHECK(!mesh_spirv(&s));module[i]=old;
  if(op==16){uint32_t v=module[i+1];module[i+1]=2;CHECK(!mesh_spirv(&s));module[i+1]=v;}
  i+=n;
 }
 const char *bad[]={"{}","{\"mesh\":{}}",
 "{\"mesh\":{\"version\":1.5}}","{\"mesh\":{\"version\":1,\"version\":1}}"};
 for(unsigned i=0;i<sizeof bad/sizeof*bad;i++)CHECK(!mesh_json(bad[i],bad[i]+strlen(bad[i]),GFX_STAGE_MS,&s.mesh));
 char changed[512];
 const char *needles[]={"32,1,1","true","32,1,1","32,1,1","\"payload_words\":32"};
 const char *values[]={"0,1,1","null","32,1,1,1","4294967296,1,1","\"payload_words\":4294967295"};
 for(unsigned i=0;i<5;i++){
  const char *pos=strstr(json,needles[i]);CHECK(pos!=NULL);size_t head=(size_t)(pos-json);
  memcpy(changed,json,head);strcpy(changed+head,values[i]);strcat(changed,pos+strlen(needles[i]));
  CHECK(!mesh_json(changed,changed+strlen(changed),GFX_STAGE_MS,&s.mesh));
 }
 // Optional private corpus mode validates actual compiler outputs without embedding game data.
 if(argc==3){
  FILE *f=fopen(argv[1],"rb");CHECK(f);fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);s.spirv=malloc(len);CHECK(s.spirv);CHECK(fread(s.spirv,len,1,f)==1);fclose(f);s.words=len/4;
  f=fopen(argv[2],"rb");CHECK(f);fseek(f,0,SEEK_END);len=ftell(f);rewind(f);char *j=calloc((size_t)len+1,1);CHECK(j);CHECK(fread(j,len,1,f)==1);fclose(f);
  s.stage=strstr(j,"\"stage\": \"ts\"")?GFX_STAGE_TS:GFX_STAGE_MS;
  CHECK(mesh_json(j,j+len,s.stage,&s.mesh));CHECK(mesh_spirv(&s));free(j);free(s.spirv);
 }
 printf("Mesh metadata contract: %u checks passed\n",checks);return 0;
}
'''
def main():
 source=(ROOT/'runtime/gfx_shader.c').read_text();enum=source[source.index('enum { OP_ENTRY_POINT'):];enum=enum[:enum.index('};')+2]
 names=['static int mesh_spirv(']
 # json_token_end calls itself recursively; function extraction preserves its body.
 scanners=source[source.index('static const char *json_space('):source.index('static const char *const geometry_names')]
 parser=source[source.index('static int mesh_json('):source.index('// The objects of the flat array')]
 body=PRELUDE+enum+'\n'+scanners+parser+'\n'.join(function(source,n) for n in names)+TEST
 out=ROOT/'build/tests/mesh-contract';out.mkdir(parents=True,exist_ok=True);f=out/'contract.c';f.write_text(body)
 for opt in ['O0','O2']:
  exe=out/(opt+'.exe');subprocess.run(['C:/msys64/ucrt64/bin/clang.exe','-std=gnu11','-'+opt,'-I',str(ROOT/'runtime'),str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
