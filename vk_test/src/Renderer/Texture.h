#pragma once

#include "Core/Core.h"
#include "VkUtils.h"

enum EImageFormat
{
	Undefined,
	RGBA8
};

struct TextureDesc
{
	u32 width = 0;
	u32 height = 0;
	EImageFormat format = EImageFormat::Undefined;
};

u32 GetPixelSize(EImageFormat format);

class Texture
{
public:
	Texture() = default;
	~Texture() = default;

	void Load(const std::filesystem::path& path);
	void SetData(const void* data, u64 size, const TextureDesc& desc);
	void ClearData();

	void CreateOnGPU();

	inline const std::vector<u8>& GetData() const { return m_Data; }
	inline const TextureDesc& GetDesc() const { return m_Desc; }

	// bytes
	inline u64 GetMemoryFootprint() const
	{
		return m_Data.size();
	}

	inline const VkUtils::Image& GetImage() const { return m_Image; }
	inline bool IsLoaded() const { return m_IsLoaded; }

public:
	std::string DebugName;

private:
	friend class ResourceFactory;
	friend class AssetManager;

	std::vector<u8> m_Data;
	VkUtils::Image m_Image;
	TextureDesc m_Desc;

	bool m_IsLoaded = false;

public:
	bool KeepCPUData = false;
};