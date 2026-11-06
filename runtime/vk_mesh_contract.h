// Task/mesh pipeline admission checks for metadata already matched to SPIR-V.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "gfx.h"

static int vk_mesh_local_fits(const GfxShaderMesh *m, const uint32_t axes[3], uint32_t max_invocations) {
    uint64_t count = 1;
    for (unsigned k = 0; k < 3; ++k) {
        if (!m->local[k] || m->local[k] > axes[k] || count > max_invocations / m->local[k]) return 0;
        count *= m->local[k];
    }
    return count <= max_invocations;
}

// Output memory/interface sizing is checked separately by vk_mesh_output_limits. Workgroup memory is derived from
// emitted variables, including compiler-generated scratch, rather than the native requested allocation alone.
static const char *vk_mesh_execution_limits(const GfxShaderMesh *task, const GfxShaderMesh *mesh,
                                           const VkPhysicalDeviceMeshShaderPropertiesEXT *p) {
    if (!mesh || !mesh->max_vertices || !mesh->max_primitives || mesh->topology != 3)
        return "missing triangle mesh metadata";
    if (!vk_mesh_local_fits(mesh, p->maxMeshWorkGroupSize, p->maxMeshWorkGroupInvocations))
        return "mesh workgroup exceeds device limits";
    if (mesh->max_vertices > p->maxMeshOutputVertices || mesh->max_primitives > p->maxMeshOutputPrimitives)
        return "mesh output counts exceed device limits";
    if (mesh->shared_bytes > p->maxMeshSharedMemorySize) return "mesh shared memory exceeds device limit";
    if ((uint64_t)mesh->payload_words * 4 + mesh->shared_bytes > p->maxMeshPayloadAndSharedMemorySize)
        return "mesh payload exceeds device limit";
    if (task) {
        if (!vk_mesh_local_fits(task, p->maxTaskWorkGroupSize, p->maxTaskWorkGroupInvocations))
            return "task workgroup exceeds device limits";
        if (task->payload_words != mesh->payload_words) return "task and mesh payload sizes differ";
        if (task->shared_bytes > p->maxTaskSharedMemorySize) return "task shared memory exceeds device limit";
        if (task->payload_words > p->maxTaskPayloadSize / 4 ||
            (uint64_t)task->payload_words * 4 + task->shared_bytes > p->maxTaskPayloadAndSharedMemorySize)
            return "task payload exceeds device limits";
    }
    return NULL;
}

// Vulkan Mesh Shader Output counts effective 32-bit scalars, excluding primitive indices.
// This admission path accepts flat per-vertex/per-primitive arrays emitted by our compiler;
// blocks, component packing and per-view outputs need explicit interface support first.
static const char *vk_mesh_output_limits(const GfxShader *mesh, const GfxShader *fs,
                                        const VkPhysicalDeviceMeshShaderPropertiesEXT *p) {
    uint64_t words = 0;
    const uint32_t *w = gfx_shader_spirv(mesh, &words);
    const GfxShaderMesh *m = gfx_shader_mesh(mesh);
    if (!w || words < 5 || !m || !w[3] || w[3] > (1u << 20)) return "missing mesh output module";
    typedef struct { uint32_t kind, base, count, bits, value, builtin, location, primitive, component; } Type;
    Type *t = calloc(w[3], sizeof *t);
    if (!t) return "mesh output metadata allocation failed";
    const char *why = "unsupported mesh output interface";
    GfxShaderIo out[32] = {{0}};
    uint32_t mask = 0, vertex = 0, primitive = 0;
    for (uint64_t i = 5; i < words;) {
        uint32_t n = w[i] >> 16, op = w[i] & 65535;
        if (!n || n > words - i) goto done;
        const uint32_t *a = w + i + 1;
        if (op == 71 && n >= 3 && a[0] < w[3]) {
            Type *v = &t[a[0]];
            if (a[1] == 11 && n == 4) v->builtin = a[2] + 1;
            if (a[1] == 30 && n == 4) v->location = a[2] + 1;
            if (a[1] == 31 && n == 4) v->component = a[2];
            if (a[1] == 5271 && n == 3) v->primitive = 1; // PerPrimitiveEXT
            if (a[1] == 5272) goto done; // PerViewNV
        } else if ((op == 20 && n == 2) || (op == 21 && n == 4) || (op == 22 && n == 3)) {
            if (a[0] >= w[3]) goto done;
            t[a[0]].kind = op;
            t[a[0]].bits = op == 20 ? 32 : a[1];
            t[a[0]].value = op == 21 ? a[2] : 0;
        } else if ((op == 23 || op == 28 || op == 32) && n == 4) {
            if (a[0] >= w[3] || (op != 32 && a[1] >= w[3]) || (op != 23 && a[2] >= w[3])) goto done;
            t[a[0]].kind = op;
            t[a[0]].base = op == 32 ? a[2] : a[1];
            t[a[0]].count = a[2];
        } else if (op == 43 && n == 4) {
            if (a[0] >= w[3] || a[1] >= w[3]) goto done;
            if (t[a[0]].kind == 21 && t[a[0]].bits == 32) t[a[1]].value = a[2];
        } else if (op == 59 && n >= 4 && a[2] == 3) {
            if (a[0] >= w[3] || a[1] >= w[3]) goto done;
            Type v = t[a[1]], ptr = t[a[0]];
            if (ptr.kind != 32 || t[ptr.base].kind != 28) goto done;
            Type arr = t[ptr.base];
            uint32_t count = t[arr.count].value;
            // Primitive index vectors are a separate output budget, not attribute storage.
            if (v.builtin >= 5294 + 1 && v.builtin <= 5296 + 1) {
                if (count != m->max_primitives) goto done;
                i += n;
                continue;
            }
            if (count != (v.primitive ? m->max_primitives : m->max_vertices) || v.component) goto done;
            uint32_t id = arr.base;
            uint64_t comps = 1;
            // A second array is a built-in attribute such as ClipDistance, not another vertex dimension.
            uint32_t depth = 0;
            while (t[id].kind == 28) {
                if (++depth > 32) goto done;
                uint32_t len = t[t[id].count].value;
                if (!len || comps > UINT32_MAX / len) goto done;
                comps *= len;
                id = t[id].base;
            }
            if (t[id].kind == 23) {
                if (t[id].count < 2 || t[id].count > 4 || comps > UINT32_MAX / t[id].count) goto done;
                comps *= t[id].count;
                id = t[id].base;
            }
            Type scalar = t[id];
            if ((scalar.kind != 20 && scalar.kind != 21 && scalar.kind != 22) ||
                (scalar.bits != 16 && scalar.bits != 32 && scalar.bits != 64)) goto done;
            uint64_t effective = comps * (scalar.bits == 64 ? 2 : 1);
            uint32_t *sum = v.primitive ? &primitive : &vertex;
            if (effective > UINT32_MAX - *sum) goto done;
            *sum += (uint32_t)effective;
            if (v.location) {
                uint32_t loc = v.location - 1;
                if (v.primitive || loc >= 32 || (mask >> loc & 1) || comps > 4 || scalar.bits != 32) goto done;
                mask |= 1u << loc;
                out[loc] = (GfxShaderIo){scalar.kind == 22 ? GFX_TYPE_FLOAT : scalar.value ? GFX_TYPE_SINT : GFX_TYPE_UINT,
                                        (uint8_t)comps, 0, (uint8_t)scalar.bits};
            } else if (!v.builtin) goto done;
        }
        i += n;
    }
    why = "mesh output components exceed device limit";
    if ((uint64_t)vertex + primitive > p->maxMeshOutputComponents) goto done;
    why = "missing mesh output allocation granularity";
    if (!p->meshOutputPerVertexGranularity || !p->meshOutputPerPrimitiveGranularity) goto done;
    uint64_t gv = p->meshOutputPerVertexGranularity, gp = p->meshOutputPerPrimitiveGranularity;
    uint64_t nv = ((uint64_t)m->max_vertices + gv - 1) / gv * gv;
    uint64_t np = ((uint64_t)m->max_primitives + gp - 1) / gp * gp;
    // Counts were bounded to uint32 above; use division to reject overflow before multiplication.
    why = "mesh output memory exceeds device limit";
    if (vertex && nv > p->maxMeshOutputMemorySize / 4u / vertex) goto done;
    if (primitive && np > p->maxMeshOutputMemorySize / 4u / primitive) goto done;
    uint64_t bytes = 4 * (nv * vertex + np * primitive);
    if (bytes > p->maxMeshOutputMemorySize || bytes + (uint64_t)m->payload_words * 4 > p->maxMeshPayloadAndOutputMemorySize) goto done;
    if (fs) {
        GfxShaderIo in[32];
        uint32_t need = gfx_shader_inputs(fs, in);
        why = "mesh and fragment varying interfaces differ";
        if (need & ~mask) goto done;
        for (unsigned i = 0; i < 32; i++) if (need >> i & 1) {
            if (in[i].packed || in[i].scalar_bits != 32 || in[i].type != out[i].type || in[i].comps > out[i].comps) goto done;
        }
    }
    why = NULL;
done:
    free(t);
    return why;
}
