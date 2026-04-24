#pragma once

#include "Core/Core.h"
#include "Async/TaskPool.h"
#include "Renderer/Mesh.h"
#include "Renderer/SkeletalMesh.h"
#include "Renderer/Texture.h"
#include <unordered_map>
#include "Renderer/ResourceFactory.h"

enum class EAssetType
{
	Mesh,
	Texture
};

using AssetUUID = u64;

struct Asset
{
	AssetUUID uuid;
	void* assetRes;
	EAssetType type;
};

class AssetManager
{
public:
	void Init(u32 asyncLoaderThreads);
	void Shutdown();

	Mesh* LoadMesh(const std::filesystem::path& path);
	SkeletalMesh* LoadSkeletalMesh(const std::filesystem::path& path);
	Texture* LoadTexture(const std::filesystem::path& path);

	u32 CheckLoadedAssets(std::vector<PendingLoadingRes>& outLoadedAssets);
	u32 CheckLoadedAssets();

	void AssignTextureIndex(Texture* texture);

private:
	inline u32 GenNewTextureIndex() { return m_CurrentTextureIndex++; }
	AssetUUID RegisterAsset(void* assetRes, EAssetType type);

private:
	TaskPool m_AsyncLoader;

	// todo: allocate with decent allocator
	std::unordered_map<AssetUUID, Asset> m_AssetsDB;
	u32 m_CurrentTextureIndex = 0;
};