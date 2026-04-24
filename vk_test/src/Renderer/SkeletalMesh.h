#pragma once

#include "Mesh.h"
#include <utility>
#include "glm/detail/type_quat.hpp"

#include "Animation.h"

constexpr u32 MAX_BONE_WEIGHTS = 4;

struct Node
{
	std::string debugName;
	glm::mat4 localTransform; // relative to parent
	glm::mat4 offsetMatrix;
	std::vector<Node> children;
	u32 nodeID;
	u32 boneDataIndex = -1; // index into various structures
};

struct SkeletalVertex
{
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;

	u32 bones[MAX_BONE_WEIGHTS];
	float bonesWeight[MAX_BONE_WEIGHTS];
};

// Value - Time
using PositionKey = std::pair<glm::vec3, float>;
using RotationKey = std::pair<glm::quat, float>;
using ScaleKey = std::pair<glm::vec3, float>;

struct NodeAnim
{
	std::vector<PositionKey> keyFramesPos;
	std::vector<RotationKey> keyFramesRot;
	std::vector<ScaleKey> keyFramesScale;
};

struct Animation
{
	u32 frameCount;
	u32 frameRate;
	std::string name;
	std::vector<NodeAnim> nodeKeyframes;
	std::vector<u32> keyFrameMap; // Node::finalMatrixIndex -> index inside nodeKeyFrames

	std::vector<Anim::KeyFrameData> bakedAnimData; // count is frameCount + 1
};

class SkeletalMesh
{
public:
	SkeletalMesh() = default;
	~SkeletalMesh() = default;

	void Load(const std::filesystem::path& path);
	void ClearData();

	void CreateOnGPU();

	inline const std::vector<SkeletalVertex>& GetVertices() const { return m_Vertices; }
	inline const std::vector<Index>& GetIndices() const { return m_Indices; }
	inline const std::vector<Submesh>& GetSubmeshes() const { return m_Submeshes; }

	inline u64 GetVertexBufferSize() const { return m_Vertices.size() * sizeof(SkeletalVertex); }
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

	inline const Node& GetRootNode() const { return m_RootNode; }
	inline const std::vector<Animation>& GetAnimations() const { return m_Animations; }
	inline u32 GetNodeCount() const { return m_NodeCount; }
	inline u32 GetBoneCount() const { return m_BoneCount; }

public:
	std::string DebugName;

private:
	friend class ResourceFactory;
	friend class AssetManager;

	std::vector<SkeletalVertex> m_Vertices;
	std::vector<Index> m_Indices;
	std::vector<Submesh> m_Submeshes;
	Node m_RootNode;
	std::vector<Animation> m_Animations;
	u32 m_NodeCount;
	u32 m_BoneCount;
	
	std::vector<Anim::KeyFrameData> m_BakedAnimData;

	VkUtils::Buffer m_VertexBuffer;
	VkUtils::Buffer m_IndexBuffer;
	VkDeviceAddress m_VertexBufferAddress = 0;
	bool m_IsLoaded = false;

public:
	bool KeepCPUData = false;
};