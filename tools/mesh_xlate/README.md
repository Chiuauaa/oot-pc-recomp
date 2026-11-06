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
The current runtime service client does not implement mesh pipeline creation or
mesh draw execution. `VK_EXT_mesh_shader` capability detection alone does not
make `OP_DRAW_MESH` render.

CPU client checks:

```powershell
.venv/Scripts/python.exe tests/mesh_shader_client_test.py
```

They exercise production request generation with stubbed child/cache operations,
including all 32 texture and image slots, escaped paths, exact reply identity,
child disconnection and stage mismatches. Actual compiler/captured-game checks
stay private under `build/oot-fix/mesh-compiler/`.
