// Original CPU contract for real vk_pipeline.c functions; no loader/device is linked.
// Included after actual GfxShader structure, reflector and accessor definitions.
#include <stdarg.h>

static unsigned checks, failures, made, destroyed, pipeline_calls, fail_module;
static VkShaderStageFlags captured_stages;
static VkPrimitiveTopology captured_topology;
static uint32_t captured_count, captured_binding[8][128], captured_nbinding[8];
static uint32_t captured_outputs[8];
static unsigned captured_clip_declarations[8], captured_cull_declarations[8];
static VkCtx fixture_context;
static const char *output_directory;
static int capture_plain_module;
static const char *capture_prefix;
VkCaps vk_caps;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"line %d: %s\n",__LINE__,#c); } } while (0)

VkCtx *vk_ctx(void) { return &fixture_context; }
uint64_t perf_begin(void) { return 0; }
void perf_end(int kind, uint64_t start) { (void)kind; (void)start; }
void perf_count(int kind) { (void)kind; }
void rt_log(const char *format, ...) { (void)format; }
const char *cfg_get(const char *name) { (void)name; abort(); }
int cfg_int(const char *name) { (void)name; abort(); }
int tex_vk_format(int format, TexVkFormat *out) { (void)format; (void)out; abort(); }
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice device,
    VkFormat format, VkFormatProperties *out) { (void)device; (void)format; (void)out; abort(); }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device,
    const VkShaderModuleCreateInfo *info, const VkAllocationCallbacks *alloc, VkShaderModule *out) {
    (void)device; (void)alloc;
    unsigned index = ++made;
    if (fail_module && index == fail_module) return VK_ERROR_OUT_OF_HOST_MEMORY;
    CHECK(index < 8);
    captured_clip_declarations[index]=captured_cull_declarations[index]=0;
    if(!pipeline_calls) {
        char path[4096];
        if(capture_plain_module)snprintf(path,sizeof path,"%s/plain-vs.spv",output_directory);
        else if(capture_prefix)snprintf(path,sizeof path,"%s/%s-module%u.spv",output_directory,capture_prefix,index);
        else snprintf(path,sizeof path,"%s/module%u.spv",output_directory,index);
        FILE *f=fopen(path,"wb"); if(!f)abort();
        if(fwrite(info->pCode,1,info->codeSize,f)!=info->codeSize || fclose(f))abort();
    }
    const uint32_t *w = info->pCode;
    GfxShader reflected={.spirv=(uint32_t*)w,.words=info->codeSize/4,.stage=GFX_STAGE_VS};
    for (size_t i=5; i<info->codeSize/4;) {
        uint32_t len=w[i]>>16, op=w[i]&65535;
        CHECK(len && len<=info->codeSize/4-i);
        if (!len || len>info->codeSize/4-i) break;
        if(op==15 && len>=3) reflected.stage=w[i+1]==3?GFX_STAGE_GS:w[i+1]==4?GFX_STAGE_FS:GFX_STAGE_VS;
        if(op==72 && len==5 && w[i+3]==11) {
            captured_clip_declarations[index]+=w[i+4]==3;
            captured_cull_declarations[index]+=w[i+4]==4;
        }
        if (op==71 && len==4 && w[i+2]==33) {
            CHECK(captured_nbinding[index]<128);
            captured_binding[index][captured_nbinding[index]++]=w[i+3];
        }
        i+=len;
    }
    reflect_spirv(&reflected); captured_outputs[index]=reflected.out_mask;
    *out=(VkShaderModule)(uintptr_t)index;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule module,
    const VkAllocationCallbacks *alloc) { (void)device; (void)module; (void)alloc; destroyed++; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache cache,
    uint32_t count, const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *alloc, VkPipeline *out) {
    (void)device; (void)cache; (void)alloc;
    CHECK(count==1); pipeline_calls++;
    captured_count=info->stageCount; captured_stages=0;
    for (uint32_t i=0;i<info->stageCount;i++) {
        CHECK(!(captured_stages & info->pStages[i].stage));
        captured_stages|=info->pStages[i].stage;
        CHECK(!strcmp(info->pStages[i].pName,"main"));
    }
    CHECK(!info->pVertexInputState && !info->pInputAssemblyState);
    CHECK(!info->pRasterizationState->pNext);
    for(uint32_t j=0;j<info->pDynamicState->dynamicStateCount;j++) {
        VkDynamicState d=info->pDynamicState->pDynamicStates[j];
        CHECK(d!=VK_DYNAMIC_STATE_VERTEX_INPUT_EXT && d!=VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE &&
              d!=VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY && d!=VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE);
    }
    CHECK(!info->flags || !(info->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR));
    *out=(VkPipeline)(uintptr_t)99; return VK_SUCCESS;
}


static void load(GfxShader *s,const char *dir,const char *name,int stage) {
 char path[4096];snprintf(path,sizeof path,"%s/%s.spv",dir,name);
 FILE *f=fopen(path,"rb");CHECK(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 memset(s,0,sizeof *s);s->stage=stage;s->spirv=malloc(size);s->words=size/4;
 CHECK(fread(s->spirv,size,1,f)==1);fclose(f);reflect_spirv(s);
 static GfxShaderRes res={.binding=1,.count=1,.slot=3};
 s->res[GFX_RES_UBO]=&res;s->nres[GFX_RES_UBO]=1;
 if(stage==GFX_STAGE_MS)s->mesh=(GfxShaderMesh){.local={32,1,1},.max_vertices=3,.max_primitives=1,.topology=3};
}
int main(int argc,char **argv) {
 if(argc!=2)return 2;output_directory=argv[1];pipeline_calls=1;
 GfxShader mesh,task,fs;load(&mesh,argv[1],"mesh",GFX_STAGE_MS);
 load(&task,argv[1],"task",GFX_STAGE_TS);load(&fs,argv[1],"fragment",GFX_STAGE_FS);
 GfxShader block;load(&block,argv[1],"mesh-block",GFX_STAGE_MS);
 VkPhysicalDeviceMeshShaderPropertiesEXT limits={.maxMeshOutputComponents=128,.maxMeshOutputMemorySize=65536,
 .maxMeshPayloadAndOutputMemorySize=65536,.meshOutputPerVertexGranularity=32,.meshOutputPerPrimitiveGranularity=32};
 const char *why_output=vk_mesh_output_limits(&mesh,&fs,&limits);
 if(why_output)fprintf(stderr,"output rejected: %s\n",why_output);
 CHECK(!why_output);CHECK(vk_mesh_output_limits(&block,&fs,&limits));free(block.spirv);
 limits.maxMeshOutputMemorySize=100;CHECK(vk_mesh_output_limits(&mesh,&fs,&limits));limits.maxMeshOutputMemorySize=65536;
 limits.maxMeshOutputComponents=7;CHECK(vk_mesh_output_limits(&mesh,&fs,&limits));limits.maxMeshOutputComponents=128;
 limits.maxMeshPayloadAndOutputMemorySize=100;CHECK(vk_mesh_output_limits(&mesh,&fs,&limits));limits.maxMeshPayloadAndOutputMemorySize=65536;
 limits.meshOutputPerVertexGranularity=0;CHECK(vk_mesh_output_limits(&mesh,&fs,&limits));limits.meshOutputPerVertexGranularity=32;
 fs.in[0].type=GFX_TYPE_UINT;CHECK(vk_mesh_output_limits(&mesh,&fs,&limits));reflect_spirv(&fs);
 fs.in_mask|=2;CHECK(vk_mesh_output_limits(&mesh,&fs,&limits));reflect_spirv(&fs);
 VkPipeState s={.mesh=&mesh,.task=&task,.fs=&fs};s.chmask.mask=15;
 P.dyn_vi=1;P.indep=1;P.pv_last=1;
 P.dyn[0]=VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;P.dyn[1]=VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY;
 P.dyn[2]=VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE;P.dyn[3]=VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;P.n_dyn=4;
 for(unsigned with_task=0;with_task<2;with_task++) {
  s.task=with_task?&task:NULL;Bindings b={0};
  CHECK(add_stage(&b,&mesh,0));uint32_t off=binding_span(&mesh)-1;
  if(s.task){CHECK(add_stage(&b,&task,off));off+=binding_span(&task)-1;}
  CHECK(add_stage(&b,&fs,off));
  Layout *l=calloc(1,sizeof *l+b.n*sizeof *l->b);l->n=b.n;memcpy(l->b,b.b,b.n*sizeof *b.b);l->layout=(VkPipelineLayout)(uintptr_t)88;
  char why[128];CHECK(layout_covers(&s,l,off,why,sizeof why));
  GKey key;gkey_fill(&s,&key);CHECK(!key.indep && key.mesh==&mesh && key.task==s.task && !key.vs);
  unsigned before=destroyed;made=0;memset(captured_nbinding,0,sizeof captured_nbinding);CHECK(create_graphics(&s,&key,l,off));
  CHECK(captured_stages==(VK_SHADER_STAGE_MESH_BIT_EXT|VK_SHADER_STAGE_FRAGMENT_BIT|(with_task?VK_SHADER_STAGE_TASK_BIT_EXT:0)));
  CHECK(captured_count==2+with_task && destroyed-before==2+with_task);
  // Modules are created as fragment, optional task, then mesh.
  CHECK(captured_binding[1][0]==off+1);
  CHECK(captured_binding[with_task?3:2][0]==1);
  if(with_task)CHECK(captured_binding[2][0]==2);
  free(l);
 }
 free(mesh.spirv);free(task.spirv);free(fs.spirv);
 printf("Mesh pipeline: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
