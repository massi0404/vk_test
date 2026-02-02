#include "PipelineBuilder.h"
#include "Misc/Utils.h"
#include <array>

void GraphicsPipelineBuilder::Clear()
{
	m_VertexShader = "";
	m_FragmentShader = "";
	m_ColorAttachments.clear();
	m_Descriptors.clear();
	m_PushConstants.clear();
	m_ViewportSize = {};
}

MyVkPipeline GraphicsPipelineBuilder::Build(VkDevice device)
{
	MyVkPipeline result;

	std::vector<char> vertSpv = Utils::ReadFileBinary(m_VertexShader);
	std::vector<char> fragSpv = Utils::ReadFileBinary(m_FragmentShader);
	check(vertSpv.size() && fragSpv.size());

	VkShaderModule vertShaderModule = VkUtils::CreateShaderModule(device, vertSpv);
	VkShaderModule fragShaderModule = VkUtils::CreateShaderModule(device, fragSpv);

	VkPipelineShaderStageCreateInfo vertStageInfo = {};
	vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStageInfo.module = vertShaderModule;
	vertStageInfo.pName = "main";
	vertStageInfo.pSpecializationInfo = nullptr; // constants

	VkPipelineShaderStageCreateInfo fragStageInfo = {};
	fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragStageInfo.module = fragShaderModule;
	fragStageInfo.pName = "main";
	fragStageInfo.pSpecializationInfo = nullptr; // constants

	std::array<VkPipelineShaderStageCreateInfo, 2> pipelineStages = {
		vertStageInfo, // vertex shader 
		fragStageInfo  // fragment shader
	};

	/*	fixed states (fissi per tutta la pipeline):
		- input assembly
		- rasterizer
		- multisampling
		- depth stencil
		- renderi info (dynamic_state) 
	*/

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {}; // tutto vuoto...utilizziamo il vertex pulling
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE; // se VK_TRUE, quando usi lo _STRIP e nell'index buffer trova 0xFFFFFFFF, stoppa il triangolo / linea

	VkPipelineRasterizationStateCreateInfo rasterizerState = {};
	rasterizerState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizerState.depthClampEnable = VK_FALSE; // se VK_TRUE invece che ignorare i vertici oltre far & near li clampa, richiede gpu freature
	rasterizerState.rasterizerDiscardEnable = VK_FALSE; // se VK_TRUE la geometry non va oltre -> non c'e' output sul framebuffer...
	rasterizerState.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizerState.lineWidth = 1.0f; // per le linee... spessore in fragaments... > 1 richiede GPU feature (wideLines)
	rasterizerState.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizerState.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizerState.depthBiasEnable = VK_FALSE; // offsetta il valore depth in base ai seguenti parametri...
	rasterizerState.depthBiasConstantFactor = 0.0f; // Optional
	rasterizerState.depthBiasClamp = 0.0f; // Optional
	rasterizerState.depthBiasSlopeFactor = 0.0f; // Optional

	VkPipelineMultisampleStateCreateInfo multisamplingState = {};
	multisamplingState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingState.sampleShadingEnable = VK_FALSE; // disablitato per ora...
	multisamplingState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisamplingState.minSampleShading = 1.0f; // Optional
	multisamplingState.pSampleMask = nullptr; // Optional
	multisamplingState.alphaToCoverageEnable = VK_FALSE; // Optional
	multisamplingState.alphaToOneEnable = VK_FALSE; // Optional

	VkPipelineDepthStencilStateCreateInfo depthStencilState = {}; // vuoto per ora
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	if (m_DepthMode)
	{
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = m_DepthMode->writeEnable;
		depthStencilState.depthCompareOp = m_DepthMode->compareOp;
		depthStencilState.depthBoundsTestEnable = VK_FALSE;
		depthStencilState.stencilTestEnable = VK_FALSE;
		depthStencilState.front = {};
		depthStencilState.back = {};
		depthStencilState.minDepthBounds = 0.0f;
		depthStencilState.maxDepthBounds = 1.0f;
	}

	std::vector<VkPipelineColorBlendAttachmentState> blendStates(m_ColorAttachments.size());

	// todo: setup blend states!!!!

	for (auto& blendState : blendStates)
	{
		blendState = {};
		blendState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	}

	/*blendStates[0] = {};
	blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;*/

	//blendStates[1] = {};
	//blendStates[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	//if (m_BlendMode != GFX_BLEND_NONE) // solo il primo, schifo, tmp...
	//{
	//	/*
	//		outColor = srcColor * srcColorBlendFactor <op> dstColor * dstColorBlendFactor

	//		VK_BLEND_FACTOR_ONE:		outColor = srcColor.rgb * srcColor.a + dstColor.rgb * 1.0
	//		VK_BLEND_FACTOR_SRC_ALPHA:	outColor = srcColor.rgb * srcColor.a + dstColor.rgb * (1.0 - srcColor.a)
	//	*/

	//	blendStates[0].blendEnable = VK_TRUE;
	//	blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	//	blendStates[0].dstColorBlendFactor = m_BlendMode == GFX_BLEND_ADDITIVE ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	//	blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
	//	blendStates[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	//	blendStates[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	//	blendStates[0].alphaBlendOp = VK_BLEND_OP_ADD;
	//}

	VkPipelineColorBlendStateCreateInfo colorBlendInfoState = {};
	colorBlendInfoState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendInfoState.logicOpEnable = VK_FALSE;
	colorBlendInfoState.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlendInfoState.attachmentCount = (u32)blendStates.size();
	colorBlendInfoState.pAttachments = blendStates.data();
	colorBlendInfoState.blendConstants[0] = 0.0f; // Optional
	colorBlendInfoState.blendConstants[1] = 0.0f; // Optional
	colorBlendInfoState.blendConstants[2] = 0.0f; // Optional
	colorBlendInfoState.blendConstants[3] = 0.0f; // Optional

	/*	dynamic states (non dobbiamo ricreare la pipeline per modificarli):
		- viewport
		- scissor
	*/

	std::array<VkDynamicState, 2> dynamicStates = {
		VK_DYNAMIC_STATE_VIEWPORT,	// per non ricreare la pipeline ad ogni resize
		VK_DYNAMIC_STATE_SCISSOR	// per non ricreare la pipeline ad ogni resize
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.dynamicStateCount = (uint32_t)dynamicStates.size();
	dynamicStateInfo.pDynamicStates = dynamicStates.data();

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = (float)m_ViewportSize.height;
	viewport.width = (float)m_ViewportSize.width;
	viewport.height = -(float)m_ViewportSize.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = m_ViewportSize;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	//viewportState.pViewports = &viewport; // sono nei dynamic state quindi chissene
	//viewportState.pScissors = &scissor;   // sono nei dynamic state quindi chissene

	// dynamic_rendering
	VkPipelineRenderingCreateInfo renderInfo = {};
	renderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderInfo.colorAttachmentCount = m_ColorAttachments.size();
	renderInfo.pColorAttachmentFormats = m_ColorAttachments.data();
	renderInfo.depthAttachmentFormat = m_DepthMode ? m_DepthMode->format : VK_FORMAT_UNDEFINED;

	// layout... uniform, push constants etc...
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = m_Descriptors.size(); // Optional
	pipelineLayoutInfo.pSetLayouts = m_Descriptors.data(); // Optional
	pipelineLayoutInfo.pushConstantRangeCount = m_PushConstants.size(); // Optional
	pipelineLayoutInfo.pPushConstantRanges = m_PushConstants.data(); // Optional

	VkResult layoutCreateRes = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &result.layout);
	vkCheck(layoutCreateRes);

	// pipelineeee
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderInfo; // dynamic_rendering
	pipelineInfo.stageCount = pipelineStages.size();
	pipelineInfo.pStages = pipelineStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizerState;
	pipelineInfo.pMultisampleState = &multisamplingState;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.pColorBlendState = &colorBlendInfoState;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = result.layout;
	//pipelineInfo.renderPass = g_RenderPass; // questa pipeline puo utiulizzare altre renderepass compatibili tra di loro...
	// pipelineInfo.subpass = 0; // mi stai seriamente dicendo la pipeline e' specifica per SUBpass ?!?!?!?!??!!? ...non serve con dynamic_rendering
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional, con VK_PIPELINE_CREATE_DERIVATIVE_BIT puoi derivare i settings da un'altra pipeline...
	pipelineInfo.basePipelineIndex = -1; // Optional

	VkResult pipelineCreateRes = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result.pipeline);
	vkCheck(pipelineCreateRes);

	vkDestroyShaderModule(device, vertShaderModule, nullptr);
	vkDestroyShaderModule(device, fragShaderModule, nullptr);

	return result;
}

void ComputePipelineBuilder::Clear()
{
	m_ComputeShader = "";
	m_Descriptors.clear();
	m_PushConstants.clear();
}

MyVkPipeline ComputePipelineBuilder::Build(VkDevice device)
{
	MyVkPipeline result;

	std::vector<char> computeSpv = Utils::ReadFileBinary(m_ComputeShader);
	check(computeSpv.size());

	VkShaderModule computeShaderModule = VkUtils::CreateShaderModule(device, computeSpv);

	// nel pipeline layout vengono specificati uniform, push constants etc...
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = m_Descriptors.size();
	pipelineLayoutInfo.pSetLayouts = m_Descriptors.data();
	pipelineLayoutInfo.pushConstantRangeCount = m_PushConstants.size();
	pipelineLayoutInfo.pPushConstantRanges = m_PushConstants.data();

	VkResult layoutCreateRes = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &result.layout);
	vkCheck(layoutCreateRes);

	VkPipelineShaderStageCreateInfo stageInfo = {};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = computeShaderModule;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.layout = result.layout;
	pipelineInfo.stage = stageInfo;

	VkResult pipelineCreateRes = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result.pipeline);
	vkCheck(pipelineCreateRes);

	vkDestroyShaderModule(device, computeShaderModule, nullptr);

	return result;
}
