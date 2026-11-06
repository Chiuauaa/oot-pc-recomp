// Headless creation of original task/mesh pipelines through the production Vulkan builder.
// Included after actual GfxShader structure, reflector and accessor definitions.
#include <stdarg.h>

static unsigned checks, failures;
static VkCtx fixture_context;
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
static void load(GfxShader *s,const char *dir,const char *name,int stage) {
 char path[4096];snprintf(path,sizeof path,"%s/%s.spv",dir,name);
 FILE *f=fopen(path,"rb");CHECK(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 memset(s,0,sizeof *s);s->stage=stage;s->spirv=malloc(size);s->words=size/4;
 CHECK(fread(s->spirv,size,1,f)==1);fclose(f);reflect_spirv(s);
 static GfxShaderRes res={.binding=1,.count=1,.slot=3};
 s->res[GFX_RES_UBO]=&res;s->nres[GFX_RES_UBO]=1;
 if(stage==GFX_STAGE_MS)s->mesh=(GfxShaderMesh){.local={32,1,1},.max_vertices=3,.max_primitives=1,.topology=3};
}

static unsigned validation_errors;
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
 VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
 (void)type;(void)user;
 if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)validation_errors++;
 fprintf(stderr,"Vulkan: %s\n",data->pMessage);return VK_FALSE;
}
#define VK(c) do {VkResult r_=(c);if(r_!=VK_SUCCESS){fprintf(stderr,"%s: %d\n",#c,r_);exit(2);}}while(0)
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 const char *layers[]={"VK_LAYER_KHRONOS_validation"};
 VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_3};
 const char *instance_ext[]={VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
 VkDebugUtilsMessengerCreateInfoEXT debug={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
 .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
 .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
 .pfnUserCallback=validation};
 VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pNext=&debug,.enabledExtensionCount=1,.ppEnabledExtensionNames=instance_ext,.pApplicationInfo=&ai,.enabledLayerCount=1,.ppEnabledLayerNames=layers};
 VkInstance instance;VK(vkCreateInstance(&ic,NULL,&instance));
 VkDebugUtilsMessengerEXT messenger;
 PFN_vkCreateDebugUtilsMessengerEXT create_debug=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
 VK(create_debug(instance,&debug,NULL,&messenger));
 uint32_t n=0;VK(vkEnumeratePhysicalDevices(instance,&n,NULL));VkPhysicalDevice *devices=calloc(n,sizeof*devices);VK(vkEnumeratePhysicalDevices(instance,&n,devices));
 VkPhysicalDeviceMeshShaderFeaturesEXT meshf={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
 VkPhysicalDeviceVulkan13Features f13={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext=&meshf};
 VkPhysicalDeviceFeatures2 f={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&f13};
 for(uint32_t i=0;i<n;i++){vkGetPhysicalDeviceFeatures2(devices[i],&f);if(meshf.meshShader&&meshf.taskShader&&f13.dynamicRendering){fixture_context.phys=devices[i];break;}}
 free(devices);if(!fixture_context.phys){fprintf(stderr,"No task/mesh capable GPU\n");return 77;}
 uint32_t nq=0;vkGetPhysicalDeviceQueueFamilyProperties(fixture_context.phys,&nq,NULL);VkQueueFamilyProperties *q=calloc(nq,sizeof*q);vkGetPhysicalDeviceQueueFamilyProperties(fixture_context.phys,&nq,q);
 uint32_t family=0;while(family<nq&&!(q[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;free(q);CHECK(family<nq);
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
 const char *ext[]={VK_EXT_MESH_SHADER_EXTENSION_NAME,VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME};
 VkPhysicalDeviceDepthClipControlFeaturesEXT clip={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT,.depthClipControl=VK_TRUE};
 meshf=(VkPhysicalDeviceMeshShaderFeaturesEXT){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,.pNext=&clip,.meshShader=VK_TRUE,.taskShader=VK_TRUE};
 f13=(VkPhysicalDeviceVulkan13Features){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,.pNext=&meshf,.dynamicRendering=VK_TRUE};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&f13,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.enabledExtensionCount=2,.ppEnabledExtensionNames=ext};
 VK(vkCreateDevice(fixture_context.phys,&di,NULL,&P.device));fixture_context.device=P.device;
 VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&fixture_context.mesh_props};fixture_context.mesh_props.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;vkGetPhysicalDeviceProperties2(fixture_context.phys,&props);
 VkPipelineCacheCreateInfo pci={.sType=VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};VK(vkCreatePipelineCache(P.device,&pci,NULL,&P.cache));
 GfxShader mesh,task,fs;load(&mesh,argv[1],"mesh",GFX_STAGE_MS);load(&task,argv[1],"task",GFX_STAGE_TS);load(&fs,argv[1],"fragment",GFX_STAGE_FS);
 CHECK(!vk_mesh_output_limits(&mesh,&fs,&fixture_context.mesh_props));
 VkPipeState state={.mesh=&mesh,.fs=&fs};state.chmask.mask=15;P.dyn_vi=1;
 P.dyn[0]=VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT;P.dyn[1]=VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT;P.n_dyn=2;
 for(int use_task=0;use_task<2;use_task++){
  state.task=use_task?&task:NULL;Bindings b={0};CHECK(add_stage(&b,&mesh,0));uint32_t off=binding_span(&mesh)-1;if(use_task){CHECK(add_stage(&b,&task,off));off+=binding_span(&task)-1;}CHECK(add_stage(&b,&fs,off));
  VkDescriptorSetLayoutCreateInfo sci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=b.n,.pBindings=b.b};VkDescriptorSetLayout set;VK(vkCreateDescriptorSetLayout(P.device,&sci,NULL,&set));
  VkPipelineLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set};Layout layout={0};VK(vkCreatePipelineLayout(P.device,&lci,NULL,&layout.layout));
  GKey key;gkey_fill(&state,&key);VkPipeline pipe=create_graphics(&state,&key,&layout,off);CHECK(pipe!=VK_NULL_HANDLE);if(pipe)vkDestroyPipeline(P.device,pipe,NULL);vkDestroyPipelineLayout(P.device,layout.layout,NULL);vkDestroyDescriptorSetLayout(P.device,set,NULL);
 }
 printf("GPU %s: mesh and task/mesh pipeline creation: %u checks, %u failures\n",props.properties.deviceName,checks,failures);
 free(mesh.spirv);free(task.spirv);free(fs.spirv);vkDestroyPipelineCache(P.device,P.cache,NULL);vkDestroyDevice(P.device,NULL);((PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,NULL);
 vkDestroyInstance(instance,NULL);CHECK(validation_errors==0);printf("Validation errors: %u\n",validation_errors);return failures?1:0;
}
