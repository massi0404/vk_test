#pragma once

#include "Core/Core.h"
#include <glm/glm.hpp>
#include "VkUtils.h"

struct Vertex
{
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

struct Submesh
{
	u32 indexOffset;
	u32 indexCount;
};

using Index = u32;

class Mesh
{
public:
	Mesh() = default;
	~Mesh() = default;

	void Load(const std::filesystem::path& path);
	void SetData(TBufferView<Vertex> vertices, TBufferView<Index> indices, TBufferView<Submesh> submeshes);
	void ClearData();

	void CreateOnGPU();

	inline const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
	inline const std::vector<Index>& GetIndices() const { return m_Indices; }
	inline const std::vector<Submesh>& GetSubmeshes() const { return m_Submeshes; }

	inline u64 GetVertexBufferSize() const { return m_Vertices.size() * sizeof(Vertex); }
	inline u64 GetIndexBufferSize() const { return m_Indices.size() * sizeof(Index); }

	// bytes
	inline u64 GetMemoryFootprint() const
	{
		return GetVertexBufferSize() + GetIndexBufferSize();
	}

	inline bool IsLoaded() const { return m_IsLoaded; }

	inline const VkUtils::Buffer& GetVertexBuffer() const { return m_VertexBuffer; }
	inline const VkUtils::Buffer& GetIndexBuffer() const { return m_IndexBuffer; }
	inline VkDeviceAddress GetVertexBufferAddress() const { return m_VertexBufferAddress; }

public:
	std::string DebugName;

private:
	friend class ResourceFactory;
	friend class AssetManager;

	std::vector<Vertex> m_Vertices;
	std::vector<Index> m_Indices;
	std::vector<Submesh> m_Submeshes;

	VkUtils::Buffer m_VertexBuffer;
	VkUtils::Buffer m_IndexBuffer;
	VkDeviceAddress m_VertexBufferAddress = 0;
	bool m_IsLoaded = false;

public:
	bool KeepCPUData = false;
};