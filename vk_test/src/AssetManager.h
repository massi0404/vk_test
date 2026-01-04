#pragma once

#include "Core/Core.h"
#include "Async/TaskPool.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include <unordered_map>

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
	Texture* LoadTexture(const std::filesystem::path& path);

	u32 CheckLoadedAssets();

private:
	AssetUUID RegisterAsset(void* assetRes, EAssetType type);

private:
	TaskPool m_AsyncLoader;

	// todo: allocate with decent allocator
	std::unordered_map<AssetUUID, Asset> m_AssetsDB;
};