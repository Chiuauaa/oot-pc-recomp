// Task/mesh pipeline admission checks for metadata already matched to SPIR-V.
#pragma once
#include <stdint.h>
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

// Output memory/interface sizing still needs pipeline validation. Workgroup memory is derived from
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
