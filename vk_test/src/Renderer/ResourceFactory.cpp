#include "ResourceFactory.h"
#include "VkUtils.h"

constexpr u64 STAGING_BUFFER_SIZE = 256 * 1024 * 1024; // 256MB total staging mapped memory

ResourceFactory::ResourceFactory()
	: m_Context(nullptr)
	, m_Device(VK_NULL_HANDLE)
    , m_MappedStagingBuffer(nullptr)
    , m_StagingQueue(VK_NULL_HANDLE)
    , m_StagingCmdPool(VK_NULL_HANDLE)
    , m_StagingCmd(VK_NULL_HANDLE)
    , m_StagingFence(VK_NULL_HANDLE)
    , m_StopLoaderThread(false)
{
}

ResourceFactory::~ResourceFactory()
{
}

void ResourceFactory::Init(RendererContext* context)
{
	check(context);

	m_Context = context;
	m_Device = context->GetDevice();

    m_StagingBuffer = VkUtils::CreateBuffer(m_Device, STAGING_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    vkCheck(vkMapMemory(m_Device, m_StagingBuffer.memory, 0, STAGING_BUFFER_SIZE, 0, &m_MappedStagingBuffer));
    
    check(context->GetRendererDevice().GetTransferQueues().size());

    // find transfer queue on different family (not the same family as the graphics queue)
    u32 gfxFamily = context->GetRendererDevice().GetGraphicsQueueFamilyIndex();
    for (Queue queue : context->GetRendererDevice().GetTransferQueues())
    {
        if (queue.familyIndex != gfxFamily)
        {
            m_StagingQueue = queue;
            break;
        }
    }
    check(m_StagingQueue.queue);

    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = m_StagingQueue.familyIndex;
    vkCheck(vkCreateCommandPool(m_Device, &commandPoolInfo, nullptr, &m_StagingCmdPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.pNext = nullptr;
    cmdAllocInfo.commandPool = m_StagingCmdPool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkCheck(vkAllocateCommandBuffers(m_Device, &cmdAllocInfo, &m_StagingCmd));

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCheck(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_StagingFence));

    m_GPULoaderThread = std::thread([this]() {
        while (true)
        {
            std::unique_lock<std::mutex> lock(m_PendingLoadingLock);
            m_GPULoaderThread_CondVar.wait(lock, [this]() { return m_PendingLoading.size() > 0 || m_StopLoaderThread; });

            if (m_StopLoaderThread)
            {
                lock.unlock();
                break;
            }

            u64 staginMemoryLeft = STAGING_BUFFER_SIZE;
            std::vector<PendingLoadingRes> loadBatch; // todo: linear allocator
            
            for (const PendingLoadingRes& res : m_PendingLoading)
            {
                if (res.size < staginMemoryLeft)
                {
                    staginMemoryLeft -= res.size;
                    loadBatch.push_back(res);
                }
            }

            LOG_INFO("GPU Loader: %d in the queue, starting a batch of %d", m_PendingLoading.size(), loadBatch.size());
            m_PendingLoading.erase(m_PendingLoading.begin(), m_PendingLoading.begin() + loadBatch.size());
            lock.unlock();

            LoadPendingResources_LoaderThread(loadBatch);
        }
    });
}

void ResourceFactory::Shutdown()
{
    // stop loader thread
    {
        std::lock_guard<std::mutex> lock(m_PendingLoadingLock);
        m_StopLoaderThread = true;
    }
    m_GPULoaderThread_CondVar.notify_all();

    if(m_GPULoaderThread.joinable())
        m_GPULoaderThread.join();

    vkUnmapMemory(m_Device, m_StagingBuffer.memory);
    VkUtils::DestroyBuffer(m_Device, m_StagingBuffer);

    vkDestroyFence(m_Device, m_StagingFence, nullptr);
    vkDestroyCommandPool(m_Device, m_StagingCmdPool, nullptr);
}

static VkFormat GetVkFormat(EImageFormat format)
{
    switch (format)
    {
        case EImageFormat::Undefined: return VK_FORMAT_UNDEFINED;
        case EImageFormat::RGBA8:     return VK_FORMAT_R8G8B8A8_UNORM;
    }

    check(0);
    return VK_FORMAT_UNDEFINED;
}

static u32 GetPixelSize(EImageFormat format)
{
    switch (format)
    {
        case EImageFormat::Undefined: return 0;
        case EImageFormat::RGBA8:     return 4;
    }

    check(0);
    return 0;
}


void ResourceFactory::CreateTexture(Texture* texture)
{
    VkUtils::ImageDesc desc;
    desc.width = texture->m_Desc.width;
    desc.height = texture->m_Desc.height;
    desc.format = GetVkFormat(texture->m_Desc.format);
    desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    texture->m_Image = VkUtils::CreateImage(m_Device, desc);
}

void ResourceFactory::CreateMesh(Mesh* mesh)
{
    // crea index buffer & vertex buffer
    mesh->m_VertexBuffer = VkUtils::CreateBuffer(m_Device, mesh->GetVertexBufferSize(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    mesh->m_IndexBuffer = VkUtils::CreateBuffer(m_Device, mesh->GetIndexBufferSize(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // prendi il puntatore al vertex buffer
    VkBufferDeviceAddressInfo deviceAdressInfo = {};
    deviceAdressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAdressInfo.buffer = mesh->m_VertexBuffer.buffer;
    mesh->m_VertexBufferAddress = vkGetBufferDeviceAddress(m_Device, &deviceAdressInfo);
}

void ResourceFactory::PushLoading(const PendingLoadingRes& res)
{
    std::lock_guard<std::mutex> lock(m_PendingLoadingLock);
    m_PendingLoading.push_back(res);

    m_GPULoaderThread_CondVar.notify_all(); // is all needed?
}

u32 ResourceFactory::PullLoaded(std::vector<PendingLoadingRes>& outLoadedRes)
{
    u32 loaded = 0;
#if 1
    if (m_LoadedLock.try_lock())
    {
        for (PendingLoadingRes& res : m_Loaded)
            outLoadedRes.push_back(res);

        loaded = m_Loaded.size();
        m_Loaded.clear();

        m_LoadedLock.unlock();
    }
#else

    {
        std::lock_guard<std::mutex> lock(m_LoadedLock);

        for (PendingLoadingRes& res : m_Loaded)
            outLoadedRes.push_back(res);

        loaded = m_Loaded.size();
        m_Loaded.clear();
    }

#endif

    return loaded;
}

void ResourceFactory::LoadPendingResources_LoaderThread(const std::vector<PendingLoadingRes>& loadBatch)
{
    // record commands
    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_StagingCmd, &cmdBeginInfo);

    u64 stagingMemoryOffset = 0;
    for (const PendingLoadingRes& res : loadBatch)
    {
        if (res.type == EResourceType::Texture)
        {
            // Texture upload WORK IN PROGRESS

            Texture* texture = res.texture;
            memcpy((void*)((u64)(m_MappedStagingBuffer)+stagingMemoryOffset), texture->m_Data.data(), res.size);

            // change layout: undefined -> transfer
            {
                VkImageMemoryBarrier barrier = {};
                barrier.image = texture->m_Image.image;
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = m_StagingQueue.familyIndex;
                barrier.dstQueueFamilyIndex = m_StagingQueue.familyIndex;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                vkCmdPipelineBarrier(m_StagingCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr, 0, nullptr, 1, &barrier);
                
                VkBufferImageCopy imgRegion = {};
                imgRegion.bufferOffset = stagingMemoryOffset;
                imgRegion.imageExtent = { texture->m_Desc.width, texture->m_Desc.height, 1 };
                imgRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imgRegion.imageSubresource.layerCount = 1;
                vkCmdCopyBufferToImage(m_StagingCmd, m_StagingBuffer.buffer, texture->m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgRegion);
            }

            // change layout: transfer -> read optimal + change queue family for exclusive sharing: transfer -> graphics
            {
                VkImageMemoryBarrier barrier = {};
                barrier.image = texture->m_Image.image;
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex = m_StagingQueue.familyIndex;
                barrier.dstQueueFamilyIndex = m_Context->GetRendererDevice().GetGraphicsQueueFamilyIndex();
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                vkCmdPipelineBarrier(m_StagingCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                    0, nullptr, 0, nullptr, 1, &barrier);
            }
        }
        else if (res.type == EResourceType::MeshBuffer)
        {
            Mesh* mesh = res.mesh;
            memcpy((void*)((u64)(m_MappedStagingBuffer) + stagingMemoryOffset), mesh->m_Vertices.data(), mesh->GetVertexBufferSize());
            memcpy((void*)((u64)(m_MappedStagingBuffer) + stagingMemoryOffset + mesh->GetVertexBufferSize()), mesh->m_Indices.data(), mesh->GetIndexBufferSize());

            VkBufferCopy vertexBufferCopy;
            vertexBufferCopy.srcOffset = stagingMemoryOffset;
            vertexBufferCopy.dstOffset = 0;
            vertexBufferCopy.size = mesh->GetVertexBufferSize();
            vkCmdCopyBuffer(m_StagingCmd, m_StagingBuffer.buffer, mesh->m_VertexBuffer.buffer, 1, &vertexBufferCopy);

            VkBufferCopy indexBufferCopy;
            indexBufferCopy.srcOffset = stagingMemoryOffset + mesh->GetVertexBufferSize();
            indexBufferCopy.dstOffset = 0;
            indexBufferCopy.size = mesh->GetIndexBufferSize();
            vkCmdCopyBuffer(m_StagingCmd, m_StagingBuffer.buffer, mesh->m_IndexBuffer.buffer, 1, &indexBufferCopy);
        }

        stagingMemoryOffset += res.size;
    }

    vkEndCommandBuffer(m_StagingCmd);

    // flush staging memory
    VkMappedMemoryRange stagingMappedMemory = {};
    stagingMappedMemory.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    stagingMappedMemory.memory = m_StagingBuffer.memory;
    stagingMappedMemory.offset = 0;
    stagingMappedMemory.size = STAGING_BUFFER_SIZE;
    vkFlushMappedMemoryRanges(m_Device, 1, &stagingMappedMemory);

    // execute and wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_StagingCmd;
    vkQueueSubmit(m_StagingQueue.queue, 1, &submitInfo, m_StagingFence);

    // wait...
    vkWaitForFences(m_Device, 1, &m_StagingFence, VK_TRUE, 0xffffffffffffffff);
    vkResetFences(m_Device, 1, &m_StagingFence);

    // add resources to completed list
    {
        std::lock_guard<std::mutex> lock(m_LoadedLock);
        for (const PendingLoadingRes& res : loadBatch)
            m_Loaded.push_back(res);
    }
}