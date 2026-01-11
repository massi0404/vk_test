#pragma once

#include <vulkan/vulkan.h>

#include "Core/CoreMinimal.h"

#define vkCheck(VkCall) { VkResult vk_res = (VkCall);  check(vk_res == VK_SUCCESS) } 
#define vkCheckSlow(VkRes) { CORE_ASSERT((VkRes) == VK_SUCCESS, "Vulkan error!"); }

#include <vector>
#include <functional>

namespace VkUtils {

	struct ImageDesc
	{
		uint32_t width;
		uint32_t height;
		VkFormat format;
		VkImageTiling tiling;
		VkImageUsageFlags usage;
		VkImageAspectFlags aspect;
	};

	struct Image
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
	};

	struct Buffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
	};

	void Init(VkPhysicalDevice gpu);

	const VkPhysicalDeviceMemoryProperties& GetDeviceMemProps();
	uint32_t GetMemoryTypeIndex(uint32_t type_filter, VkMemoryPropertyFlags properties);

	VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code);

	VkImageSubresourceRange ImageRange(VkImageAspectFlags aspect);

	Buffer CreateBuffer(VkDevice device, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags);
	void DestroyBuffer(VkDevice device, Buffer buffer);

	Image CreateImage(VkDevice device, const ImageDesc& image_desc);
	void DestroyImage(VkDevice device, const Image& image);

	void CopyImage(VkCommandBuffer cmd, VkImage dst, VkImage src, VkExtent2D dst_size, VkExtent2D src_size);
	// !!! pipeline barrier !!!
	void TransitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout current_layout, VkImageLayout new_layout);

	VkSemaphoreSubmitInfo SemaphoreSubmitInfo(VkPipelineStageFlags2 stage_mask, VkSemaphore semaphore);
	VkCommandBufferSubmitInfo CommandBufferSubmitInfo(VkCommandBuffer cmd);
	VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* singal_semaphore_info, VkSemaphoreSubmitInfo* wait_semaphore_info);

	VkRenderingAttachmentInfo AttachmentInfo(VkImageView view, VkClearValue* clear, VkImageLayout layout);
	VkRenderingAttachmentInfo AttachmentInfoDepth(VkImageView view, VkImageLayout layout);
	VkRenderingInfo RenderingInfo(VkExtent2D render_extent, VkRenderingAttachmentInfo* color_attachment, VkRenderingAttachmentInfo* depth_atatchment);

	void ImmediateSubmit(VkDevice device, VkFence fence, VkCommandBuffer cmd, std::function<void(VkCommandBuffer)> func);
}