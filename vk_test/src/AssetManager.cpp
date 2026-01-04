#include "AssetManager.h"
#include "Renderer/ResourceFactory.h"
#include "Engine.h"
#include "Misc/Utils.h"

static AssetUUID s_AssetUUIDCounter = 0; // puzza, ma per il momento fa bene

void AssetManager::Init(u32 asyncLoaderThreads)
{
	m_AsyncLoader.Start(asyncLoaderThreads);
}

void AssetManager::Shutdown()
{
	m_AsyncLoader.Stop();
}

Mesh* AssetManager::LoadMesh(const std::filesystem::path& path)
{
	Mesh* mesh = new Mesh(); // todo: decent allocator

	m_AsyncLoader.AddTask([mesh, path]() {
		// disk -> ram
		mesh->Load(path);
		LOG_INFO("Asset manager: Mesh %s loaded on ram! (%.2f MB)", path.string().c_str(), Utils::BytesToMegabytes(mesh->GetMemoryFootprint()));
		// ram -> vram
		PendingLoadingRes res;
		res.mesh = mesh;
		res.size = mesh->GetMemoryFootprint();
		res.type = EResourceType::MeshBuffer;
		g_ResourceFactory.PushLoading(res);
	});

	RegisterAsset(mesh, EAssetType::Mesh);
	return mesh;
}

Texture* AssetManager::LoadTexture(const std::filesystem::path& path)
{
	Texture* texture = new Texture(); // todo: decent allocator

	m_AsyncLoader.AddTask([texture, path]() {
		// disk -> ram
		texture->Load(path);
		LOG_INFO("Asset manager: Texture %s loaded on ram! (%.2f MB)", path.string().c_str(), Utils::BytesToMegabytes(texture->GetMemoryFootprint()));
		// ram -> vram
		PendingLoadingRes res;
		res.texture = texture;
		res.size = texture->GetMemoryFootprint();
		res.type = EResourceType::Texture;
		g_ResourceFactory.PushLoading(res);
	});

	RegisterAsset(texture, EAssetType::Texture);
	return texture;
}

u32 AssetManager::CheckLoadedAssets()
{
	std::vector<PendingLoadingRes> loaded;
	u32 freshlyLoadedCount = g_ResourceFactory.PullLoaded(loaded);
	
	for (PendingLoadingRes& res : loaded)
	{
		switch (res.type)
		{
			case EResourceType::Texture:
				res.texture->m_IsLoaded = true;
				if (!res.texture->KeepCPUData)
					res.texture->ClearData();
				break;

			case EResourceType::MeshBuffer:
				res.mesh->m_IsLoaded = true;
				if (!res.mesh->KeepCPUData)
					res.mesh->ClearData();
				break;
		}
	}

	return freshlyLoadedCount;
}

AssetUUID AssetManager::RegisterAsset(void* assetRes, EAssetType type)
{
	Asset newAsset;
	newAsset.uuid = ++s_AssetUUIDCounter;
	newAsset.assetRes = assetRes;
	newAsset.type = type;

	m_AssetsDB[newAsset.uuid] = newAsset;

	return newAsset.uuid;
}
