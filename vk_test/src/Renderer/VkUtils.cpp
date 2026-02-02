#include "VkUtils.h"

static VkPhysicalDeviceMemoryProperties s_GPUProps = {};

namespace VkUtils {

    void Init(VkPhysicalDevice gpu)
    {
        vkGetPhysicalDeviceMemoryProperties(gpu, &s_GPUProps);
    }

    const VkPhysicalDeviceMemoryProperties& GetDeviceMemProps()
    {
        return s_GPUProps;
    }

    uint32_t GetMemoryTypeIndex(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        for (uint32_t i = 0; i < s_GPUProps.memoryTypeCount; i++)
        {
            bool sameType = typeFilter & (1 << i);
            bool sameExactProps = (s_GPUProps.memoryTypes[i].propertyFlags & properties) == properties;

            if (sameType && sameExactProps)
                return i;
        }

        check(0);
        return 0xffffffff;
    }

    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        VkResult res = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
        vkCheck(res);

        return shaderModule;
    }

    VkImageSubresourceRange ImageRange(VkImageAspectFlags aspect)
    {
        VkImageSubresourceRange subImageRange = {};
        subImageRange.baseMipLevel = 0;
        subImageRange.levelCount = VK_REMAINING_MIP_LEVELS;
        subImageRange.baseArrayLayer = 0;
        subImageRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        subImageRange.aspectMask = aspect;

        return subImageRange;
    }

    Buffer CreateBuffer(VkDevice device, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags)
    {
        return CreateBuffer(device, size, usage, memoryFlags, 0, 0);
    }

    Buffer CreateBuffer(VkDevice device, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryFlags, u32 uploadQueue, u32 renderQueue)
    {
        VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = size;
        bufferInfo.usage = usage;

        u32 queues[2] = { uploadQueue, renderQueue };

        if (uploadQueue != renderQueue)
        {
            bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            bufferInfo.queueFamilyIndexCount = 2;
            bufferInfo.pQueueFamilyIndices = queues;
        }
        else
        {
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        VkBuffer buffer;
        vkCheck(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateFlagsInfo allocFlags = {};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocFlags.deviceMask = 0; // ? boh

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlags;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = GetMemoryTypeIndex(memRequirements.memoryTypeBits, memoryFlags);

        VkDeviceMemory memory;
        vkCheck(vkAllocateMemory(device, &allocInfo, nullptr, &memory));

        vkCheck(vkBindBufferMemory(device, buffer, memory, 0));

        return { buffer, memory };
    }

    void DestroyBuffer(VkDevice device, Buffer buffer)
    {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        vkFreeMemory(device, buffer.memory, nullptr);
    }

    Image CreateImage(VkDevice device, const ImageDesc& imageDesc)
    {
        return CreateImage(device, imageDesc, 0, 0);
    }

    Image CreateImage(VkDevice device, const ImageDesc& imageDesc, u32 uploadQueue, u32 renderQueue)
    {
        u32 concurrentQueues[2] = { uploadQueue, renderQueue };

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = imageDesc.width;
        imageInfo.extent.height = imageDesc.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = imageDesc.format;
        imageInfo.tiling = imageDesc.tiling;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = imageDesc.usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (uploadQueue != renderQueue)
        {
            imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            imageInfo.queueFamilyIndexCount = 2;
            imageInfo.pQueueFamilyIndices = concurrentQueues;
        }

        VkImage image;
        vkCheck(vkCreateImage(device, &imageInfo, nullptr, &image));

        VkMemoryRequirements memRequirements = {};
        vkGetImageMemoryRequirements(device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = GetMemoryTypeIndex(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory memory;
        vkCheck(vkAllocateMemory(device, &allocInfo, nullptr, &memory));

        vkCheck(vkBindImageMemory(device, image, memory, 0));

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageDesc.format;
        viewInfo.subresourceRange.aspectMask = imageDesc.aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView view;
        vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &view));

        return { image, view, memory };
    }


    void DestroyImage(VkDevice device, const Image& image)
    {
        vkDestroyImageView(device, image.view, nullptr);
        vkDestroyImage(device, image.image, nullptr);
        vkFreeMemory(device, image.memory, nullptr);
    }

    void CopyImage(VkCommandBuffer cmd, VkImage dst, VkImage src, VkExtent2D dstSize, VkExtent2D srcSize)
    {
        VkImageBlit2 blitRegion = { .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

        blitRegion.srcOffsets[1].x = srcSize.width;
        blitRegion.srcOffsets[1].y = srcSize.height;
        blitRegion.srcOffsets[1].z = 1;

        blitRegion.dstOffsets[1].x = dstSize.width;
        blitRegion.dstOffsets[1].y = dstSize.height;
        blitRegion.dstOffsets[1].z = 1;

        blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.srcSubresource.baseArrayLayer = 0;
        blitRegion.srcSubresource.layerCount = 1;
        blitRegion.srcSubresource.mipLevel = 0;

        blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blitRegion.dstSubresource.baseArrayLayer = 0;
        blitRegion.dstSubresource.layerCount = 1;
        blitRegion.dstSubresource.mipLevel = 0;

        VkBlitImageInfo2 blitInfo = { .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
        blitInfo.dstImage = dst;
        blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitInfo.srcImage = src;
        blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blitInfo.filter = VK_FILTER_LINEAR;
        blitInfo.regionCount = 1;
        blitInfo.pRegions = &blitRegion;

        vkCmdBlitImage2(cmd, &blitInfo);
    }

    void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
    {
        VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageSubresourceRange subimageRange = ImageRange(aspectMask);

        VkImageMemoryBarrier2 imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // fa un po schifo, ma noi lo facciamo 1 volta per frame, quindi per ora lo ignoriamo
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT; // blooca la scrittura
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT; // blocca scrittura e lettura
        imageBarrier.oldLayout = currentLayout;
        imageBarrier.newLayout = newLayout;
        imageBarrier.subresourceRange = subimageRange;
        imageBarrier.image = image;

        VkDependencyInfo depInfo = {};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    VkSemaphoreSubmitInfo SemaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore)
    {
        VkSemaphoreSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        submitInfo.pNext = nullptr;
        submitInfo.semaphore = semaphore;
        submitInfo.stageMask = stageMask;
        submitInfo.deviceIndex = 0;
        submitInfo.value = 1;

        return submitInfo;
    }

    VkCommandBufferSubmitInfo CommandBufferSubmitInfo(VkCommandBuffer cmd)
    {
        VkCommandBufferSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        info.pNext = nullptr;
        info.commandBuffer = cmd;
        info.deviceMask = 0;

        return info;
    }

    VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo)
    {
        VkSubmitInfo2 info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        info.pNext = nullptr;

        info.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
        info.pWaitSemaphoreInfos = waitSemaphoreInfo;

        info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
        info.pSignalSemaphoreInfos = signalSemaphoreInfo;

        info.commandBufferInfoCount = 1;
        info.pCommandBufferInfos = cmd;

        return info;
    }

    VkRenderingAttachmentInfo AttachmentInfo(VkImageView view, VkClearValue* clear, VkImageLayout layout)
    {
        VkRenderingAttachmentInfo colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView = view;
        colorAttachment.imageLayout = layout;

        if (clear) 
        {
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.clearValue = *clear;
        }
        else
        {
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        return colorAttachment;
    }

    VkRenderingAttachmentInfo AttachmentInfoDepth(VkImageView view, VkImageLayout layout)
    {
        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = view;
        depthAttachment.imageLayout = layout;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil.depth = 0.0f; // noi usiamo 0 far e 1 near (glm::perspective con near e far swappate)
        
        return depthAttachment;
    }

    VkRenderingInfo RenderingInfo(VkExtent2D renderExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthAttachment)
    {
        VkRenderingInfo renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, renderExtent };
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = colorAttachment;
        renderInfo.pDepthAttachment = depthAttachment;
        renderInfo.pStencilAttachment = nullptr;

        return renderInfo;
    }

}