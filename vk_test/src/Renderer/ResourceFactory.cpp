#include "ResourceFactory.h"
#include "VkUtils.h"

constexpr u64 STAGING_BUFFER_SIZE = 256 * 1000 * 1000; // 256 total staging mapped memory

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

    m_StagingBuffer = VkUtils::CreateBuffer(m_Device, STAGING_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkCheck(vkMapMemory(m_Device, m_StagingBuffer.memory, 0, STAGING_BUFFER_SIZE, 0, &m_MappedStagingBuffer));
    
    check(context->GetRendererDevice().GetTransferQueues().size());

    Queue stagingQueue = context->GetRendererDevice().GetTransferQueues()[0];
    m_StagingQueue = stagingQueue.queue;

    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = stagingQueue.familyIndex;
    vkCheck(vkCreateCommandPool(m_Device, &commandPoolInfo, nullptr, &m_StagingCmdPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.pNext = nullptr;
    cmdAllocInfo.commandPool = m_StagingCmdPool;
    cmdAllocInfo.commandBufferCount = 1; // ne abbiamo solo uno: VkCommandBuffer::commandBuffer
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkCheck(vkAllocateCommandBuffers(m_Device, &cmdAllocInfo, &m_StagingCmd));

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    //fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
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
    desc.aspect = 0;
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
    {
        std::lock_guard<std::mutex> lock(m_LoadedLock);
    
        for (PendingLoadingRes& res : m_Loaded)
            outLoadedRes.push_back(res);

        loaded = m_Loaded.size();
        m_Loaded.clear();
    }
    return loaded;
}

void ResourceFactory::LoadPendingResources_LoaderThread(const std::vector<PendingLoadingRes>& loadBatch)
{
    LOG_INFO("ResourceFactory::LoadPendingResources_LoaderThread: Load batch started: %d resources", (u32)loadBatch.size());

    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_StagingCmd, &cmdBeginInfo);

    u64 stagingMemoryOffset = 0;
    for (const PendingLoadingRes& res : loadBatch)
    {
        if (res.type == EResourceType::Texture)
        {
            Texture* texture = res.texture;
            memcpy((void*)((u64)(m_MappedStagingBuffer)+stagingMemoryOffset), texture->m_Data.data(), res.size);

            VkBufferImageCopy imgRegion = {};
            imgRegion.bufferOffset = stagingMemoryOffset;
            imgRegion.imageExtent = { texture->m_Desc.width, texture->m_Desc.height, 1 };
            imgRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgRegion.imageSubresource.layerCount = 1;
            vkCmdCopyBufferToImage(m_StagingCmd, m_StagingBuffer.buffer, texture->m_Image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgRegion);
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

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_StagingCmd;

    vkEndCommandBuffer(m_StagingCmd);
    vkQueueSubmit(m_StagingQueue, 1, &submitInfo, m_StagingFence);
    vkWaitForFences(m_Device, 1, &m_StagingFence, VK_TRUE, 0xffffffffffffffff); // gpu lock, wait for previous staging copy to finish :(
    vkResetFences(m_Device, 1, &m_StagingFence);

    LOG_INFO("ResourceFactory::LoadPendingResources_LoaderThread: Load batch completed!");

    // add resources to completed list
    {
        std::lock_guard<std::mutex> lock(m_LoadedLock);
        for (const PendingLoadingRes& res : loadBatch)
            m_Loaded.push_back(res);
    }
}