#pragma once

#include "RendererContext.h"

#include "VkUtils.h"

#include <mutex>
#include <deque>

#include "Async/TaskPool.h"

#include "Mesh.h"
#include "Texture.h"

enum class EResourceType
{
	Texture,
	MeshBuffer
};

struct PendingLoadingRes
{
	union
	{
		Texture* texture;
		Mesh* mesh;
	};
	u64 size;
	EResourceType type;
};

typedef u32 ResourceID;

#define IMPL_RESOURCE_GET(ResourceT, Storage) template<> const ResourceT& GetResource(ResourceID id) const { return Storage[id]; }

class ResourceFactory
{
public:
	ResourceFactory();
	~ResourceFactory();

	void Init(RendererContext* context);
	void Shutdown();

	void CreateTexture(Texture* texture);
	void CreateMesh(Mesh* mesh);

	void PushLoading(const PendingLoadingRes& res);
	u32 PullLoaded(std::vector<PendingLoadingRes>& outLoadedRes);

private:
	void LoadPendingResources_LoaderThread(const std::vector<PendingLoadingRes>& loadBatch);

private:
	RendererContext* m_Context;
	VkDevice m_Device;

	std::mutex m_ResourceMutex;

	// ram -> vram
	std::mutex m_PendingLoadingLock;
	std::vector<PendingLoadingRes> m_PendingLoading;

	std::condition_variable m_GPULoaderThread_CondVar;
	std::thread m_GPULoaderThread;
	bool m_StopLoaderThread;

	std::mutex m_LoadedLock;
	std::vector<PendingLoadingRes> m_Loaded;

	VkUtils::Buffer m_StagingBuffer;
	void* m_MappedStagingBuffer;
	VkQueue m_StagingQueue;
	VkCommandPool m_StagingCmdPool;
	VkCommandBuffer m_StagingCmd;
	VkFence m_StagingFence;
};