#pragma once

#include "Core.h"
#include "VkUtils.h"
#include <filesystem>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct Vertex
{
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

struct GeoSurface
{
	uint32_t startIndex;
	uint32_t count;
	// fastgltf::PrimitiveType type;
};

struct MeshData
{
	VkUtils::Buffer vertexBuffer;
	VkUtils::Buffer indexBuffer;
	VkDeviceAddress vertexBufferAddress = 0;
};

struct MeshResource
{
	MeshData data;
	std::vector<GeoSurface> surfaces;
	std::string name;
};

class Resource
{
	Resource() = delete;

public:
	static void LoadMesh(const std::filesystem::path& path, std::vector<MeshResource>& outMeshes);

};