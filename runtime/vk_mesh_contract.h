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

// Shared/output memory must also be checked once the compiler reports their byte sizes.
// This checks only the execution/payload fields currently represented in GfxShaderMesh.
static const char *vk_mesh_execution_limits(const GfxShaderMesh *task, const GfxShaderMesh *mesh,
                                           const VkPhysicalDeviceMeshShaderPropertiesEXT *p) {
    if (!mesh || !mesh->max_vertices || !mesh->max_primitives || mesh->topology != 3)
        return "missing triangle mesh metadata";
    if (!vk_mesh_local_fits(mesh, p->maxMeshWorkGroupSize, p->maxMeshWorkGroupInvocations))
        return "mesh workgroup exceeds device limits";
    if (mesh->max_vertices > p->maxMeshOutputVertices || mesh->max_primitives > p->maxMeshOutputPrimitives)
        return "mesh output counts exceed device limits";
    if (mesh->payload_words > p->maxMeshPayloadAndSharedMemorySize / 4)
        return "mesh payload exceeds device limit";
    if (task) {
        if (!vk_mesh_local_fits(task, p->maxTaskWorkGroupSize, p->maxTaskWorkGroupInvocations))
            return "task workgroup exceeds device limits";
        if (task->payload_words != mesh->payload_words) return "task and mesh payload sizes differ";
        if (task->payload_words > p->maxTaskPayloadSize / 4 ||
            task->payload_words > p->maxTaskPayloadAndSharedMemorySize / 4)
            return "task payload exceeds device limits";
    }
    return NULL;
}
