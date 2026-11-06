// Recording wrappers (vk_rec.h routes vkCmd* to rc_vkCmd*): straight to Vulkan here.
#include <vulkan/vulkan.h>
void rc_vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer s, VkBuffer d, uint32_t n, const VkBufferCopy *r) { vkCmdCopyBuffer(cb, s, d, n, r); }
void rc_vkCmdFillBuffer(VkCommandBuffer cb, VkBuffer b, VkDeviceSize o, VkDeviceSize n, uint32_t v) { vkCmdFillBuffer(cb, b, o, n, v); }
void rc_vkCmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo *d) { vkCmdPipelineBarrier2(cb, d); }
void rc_vkCmdResetQueryPool(VkCommandBuffer cb, VkQueryPool q, uint32_t f, uint32_t n) { vkCmdResetQueryPool(cb, q, f, n); }
void rc_vkCmdWriteTimestamp(VkCommandBuffer cb, VkPipelineStageFlagBits s, VkQueryPool q, uint32_t i) { vkCmdWriteTimestamp(cb, s, q, i); }
