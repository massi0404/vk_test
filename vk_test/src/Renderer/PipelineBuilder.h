#pragma once

#include "VkUtils.h"
#include <string>
#include <vector>
#include <optional>

struct MyVkPipeline
{
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
};

enum EGraphicsBlendMode
{
	GFX_BLEND_NONE,
	GFX_BLEND_ADDITIVE,
	GFX_BLEND_ALPHABLEND
};

enum EGraphicsCullMode
{
	GFX_CULL_NONE,
	GFX_CULL_BACK,
	GFX_CULL_FRONT
};

struct GraphicsDepthMode
{
	VkCompareOp compareOp;
	VkFormat format;
	bool writeEnable;
};

class GraphicsPipelineBuilder
{
public:
	GraphicsPipelineBuilder() = default;
	~GraphicsPipelineBuilder() = default;

	void Clear();
	MyVkPipeline Build(VkDevice device, VkPipelineLayout layout = VK_NULL_HANDLE);

public:
	std::string m_VertexShader;
	std::string m_FragmentShader;
	std::vector<VkFormat> m_ColorAttachments;
	std::vector<VkDescriptorSetLayout> m_Descriptors;
	std::vector<VkPushConstantRange> m_PushConstants;
	EGraphicsBlendMode m_BlendMode = GFX_BLEND_NONE;
	std::optional<GraphicsDepthMode> m_DepthMode;
	VkCullModeFlagBits m_CullMode = VK_CULL_MODE_BACK_BIT;
	VkPolygonMode m_PolyMode = VK_POLYGON_MODE_FILL;
};

class ComputePipelineBuilder
{
public:
	ComputePipelineBuilder() = default;
	~ComputePipelineBuilder() = default;

public:
	void Clear();
	MyVkPipeline Build(VkDevice device);

	std::string m_ComputeShader;
	std::vector<VkDescriptorSetLayout> m_Descriptors;
	std::vector<VkPushConstantRange> m_PushConstants;
};