"""Exercise production mesh pipeline construction with original GLSL and Vulkan spies."""
from pathlib import Path
import subprocess
import re
import sys
import os
import geometry_pipeline_test as pipeline
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/tests/mesh-pipeline'
SOURCES={
'mesh': """#version 460
#extension GL_EXT_mesh_shader : require
layout(local_size_x=32) in;
layout(triangles,max_vertices=3,max_primitives=1) out;
struct Payload { uint value; };
taskPayloadSharedEXT Payload payload;
layout(location=0) out vec4 colour[];
layout(binding=1) uniform Params { vec4 value; } params;
void main(){SetMeshOutputsEXT(3,1);if(gl_LocalInvocationIndex==0){
gl_MeshVerticesEXT[0].gl_Position=vec4(-1,-1,0,1);
gl_MeshVerticesEXT[1].gl_Position=vec4(1,-1,0,1);
gl_MeshVerticesEXT[2].gl_Position=vec4(0,1,0,1);
colour[0]=colour[1]=colour[2]=params.value;
gl_PrimitiveTriangleIndicesEXT[0]=uvec3(0,1,2);}}""",
'fragment': """#version 460
layout(location=0) in vec4 colour;
layout(location=0) out vec4 result;
layout(binding=1) uniform Params { vec4 value; } params;
void main(){result=colour*params.value;}""",
 'task': """#version 460
#extension GL_EXT_mesh_shader : require
layout(local_size_x=32) in;
struct Payload { uint value; };
taskPayloadSharedEXT Payload payload;
layout(binding=1) uniform Params { uvec4 value; } params;
void main(){EmitMeshTasksEXT(params.value.x,1,1);}"""}
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 for name,stage in [('mesh','mesh'),('task','task'),('fragment','frag')]:
  f=OUT/(name+'.glsl');f.write_text(SOURCES[name])
  subprocess.run(['C:/msys64/ucrt64/bin/glslangValidator.exe','-V','--target-env','vulkan1.2','-S',stage,str(f),'-o',str(OUT/(name+'.spv'))],check=True,capture_output=True)
  if name=='mesh':
   # Our service emits flat builtin arrays. Flatten this original GLSL fixture's
   # Position-only writes and retain a block variant as a rejected-layout control.
   asm=subprocess.check_output(['C:/msys64/ucrt64/bin/spirv-dis.exe',str(OUT/'mesh.spv')],text=True)
   (OUT/'mesh-block.spv').write_bytes((OUT/'mesh.spv').read_bytes())
   asm=re.sub(r'^.*OpMemberDecorate %gl_MeshPerVertexEXT.*\n','',asm,flags=re.M)
   asm=re.sub(r'^.*OpMemberName %gl_MeshPerVertexEXT.*\n','',asm,flags=re.M)
   asm=re.sub(r'^.*OpName %gl_MeshPerVertexEXT .*\n','',asm,flags=re.M)
   asm=asm.replace('OpDecorate %gl_MeshPerVertexEXT Block','OpDecorate %gl_MeshVerticesEXT BuiltIn Position')
   asm=asm.replace('%gl_MeshPerVertexEXT = OpTypeStruct %v4float %float %_arr_float_uint_1 %_arr_float_uint_1','%gl_MeshPerVertexEXT = OpTypeVector %float 4')
   # Reuse v4float instead of defining an equivalent type (SPIR-V types must be unique).
   asm=re.sub(r'^.*%gl_MeshPerVertexEXT = OpTypeVector.*\n','',asm,flags=re.M)
   asm=asm.replace('OpTypeArray %gl_MeshPerVertexEXT ', 'OpTypeArray %v4float ')
   asm=re.sub(r'(OpAccessChain %_ptr_Output_v4float %gl_MeshVerticesEXT %int_[012]) %int_0',r'\1',asm)
   (OUT/'mesh-flat.spvasm').write_text(asm)
   subprocess.run(['C:/msys64/ucrt64/bin/spirv-as.exe','--target-env','vulkan1.3',str(OUT/'mesh-flat.spvasm'),'-o',str(OUT/'mesh.spv')],check=True)
  subprocess.run(['C:/msys64/ucrt64/bin/spirv-val.exe','--target-env','vulkan1.3',str(OUT/(name+'.spv'))],check=True)
 source=pipeline.pipeline_source()+'\n'+pipeline.shader_adapter()+'\n'+(ROOT/'tests/mesh_pipeline.c').read_text()
 f=OUT/'pipeline.c';f.write_text(source)
 for opt in ['0','2']:
  exe=OUT/('pipeline-O'+opt+'.exe')
  subprocess.run(['C:/msys64/ucrt64/bin/clang.exe','-std=gnu11','-O'+opt,'-ffunction-sections','-fdata-sections','-I',str(ROOT/'runtime'),'-I','C:/msys64/ucrt64/include','-I','C:/msys64/ucrt64/include/vma',str(f),'-Wl,--gc-sections,--exclude-all-symbols','-o',str(exe)],check=True)
  subprocess.run([str(exe),str(OUT)],check=True)
 if '--gpu' in sys.argv:
  generated=OUT/'pipeline-gpu.c';generated.write_text(pipeline.pipeline_source()+'\n'+pipeline.shader_adapter()+'\n'+(ROOT/'tests/mesh_pipeline_gpu.c').read_text())
  exe=OUT/'pipeline-gpu.exe'
  subprocess.run(['C:/msys64/ucrt64/bin/clang.exe','-std=gnu11','-O2','-ffunction-sections','-fdata-sections','-I',str(ROOT/'runtime'),'-I','C:/msys64/ucrt64/include','-I','C:/msys64/ucrt64/include/vma',str(generated),'-Wl,--gc-sections,--exclude-all-symbols','-LC:/msys64/ucrt64/lib','-lvulkan-1','-o',str(exe)],check=True)
  env=dict(os.environ);env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');env['VK_LAYER_PATH']='C:/msys64/ucrt64/bin'
  subprocess.run([str(exe),str(OUT)],check=True,env=env)
if __name__=='__main__':main()
