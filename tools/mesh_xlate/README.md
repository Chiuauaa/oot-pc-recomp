# Native task and mesh translator

`mesh_xlate.cpp` is the experimental NVN 2 task/mesh compiler service. It links
Eglantine's shader recompiler at the revision recorded in
`runtime/third_party/eglantine/UPSTREAM_COMMIT`, using the local lowering headers.
The runtime discovers it beside `dkb.exe`, or through `TOTK_MESH_XLATE`.

The source header describes the JSON-line protocol. Game controls, native code,
SPIR-V and reflection must remain under ignored `build/` directories. Stdout is
reserved for replies; diagnostics must go to stderr.

The existing research build is `build/oot-fix/mesh-compiler/obj`; build its
`mesh_xlate` target with MSYS2 UCRT64 Ninja. Its dependency sources, licenses and
pinned versions are described in `THIRD_PARTY_NOTICES.md`. A standalone clean
checkout build/package workflow has not yet been implemented. Do not distribute
this experimental service without the applicable source and license material.

Mesh registers are inferred from NVN control fields and marked
`inferred_registers` in reflection. This remains an unverified ABI hypothesis.
Valid SPIR-V does not demonstrate correct geometry or payload interoperability.
The runtime now constructs mesh/task graphics pipelines, but mesh draw replay is
still unconnected. `VK_EXT_mesh_shader` capability detection alone does not
make `OP_DRAW_MESH` render.

CPU client checks:

```powershell
.venv/Scripts/python.exe tests/mesh_shader_client_test.py
```

They exercise production request generation with stubbed child/cache operations,
including all 32 texture and image slots, escaped paths, exact reply identity,
child disconnection and stage mismatches. Actual compiler/captured-game checks
stay private under `build/oot-fix/mesh-compiler/`.

Reflection now requires `mesh.version = 1`. The runtime compares its literal
execution fields and flat payload array to SPIR-V before admitting the shader
into its cache. Run `tests/mesh_shader_contract_test.py` for synthetic contract
and device-limit checks. These checks do not verify the inferred native ABI.

Pipeline checks using original GLSL (no game data):

```powershell
.venv/Scripts/python.exe tests/mesh_pipeline_test.py
.venv/Scripts/python.exe tests/mesh_pipeline_test.py --gpu
```

The default uses CPU Vulkan spies at O0/O2. The optional headless GPU check
requires a task/mesh-capable Vulkan 1.3 device and the Khronos validation layer;
it creates pipelines but does not draw. Both use MSYS2 UCRT64 glslang, SPIR-V
Tools and Clang. The bounded output contract accepts flat compiler interfaces;
blocks, per-view outputs and packed component layouts remain unsupported.
