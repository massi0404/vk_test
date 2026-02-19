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
	VkExtent2D m_ViewportSize = {};
	EGraphicsBlendMode m_BlendMode = GFX_BLEND_NONE;
	std::optional<GraphicsDepthMode> m_DepthMode;
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