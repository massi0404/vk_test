#include "Engine.h"

#define NOMINMAX

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include "glfw/glfw3.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include "glfw/glfw3native.h"

#include "Renderer/VkUtils.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include "Renderer/PipelineBuilder.h"

#include "Async/TaskPool.h"

#include "Renderer/RendererContext.h"
#include "AssetManager.h"
#include "Renderer/Mesh.h"

#include "Math/Math.h"

struct FrameData
{
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkSemaphore swapchainSemaphore = VK_NULL_HANDLE; // gpu -> gpu
	VkFence fence = VK_NULL_HANDLE; // cpu -> gpu
	DeletionQueue deletionQueue;
	VkDescriptorSet descriptorGBuffer = VK_NULL_HANDLE;
	VkDescriptorSet descriptorCompose = VK_NULL_HANDLE;
	VkDescriptorSet descriptorForward = VK_NULL_HANDLE;
	VkUtils::Buffer uniformBufferLighting;
};

struct SwapchainImage
{
	VkImage image = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE; // finish queue -> present
};

// fifs
constexpr int FRAMES_IN_FLIGHT = 2;
int g_FrameIndex = 0;
FrameData g_FramesData[FRAMES_IN_FLIGHT];

// stuff
constexpr VkFormat DRAW_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT; // migliore della swapchain, per disegnare con precisione...

constexpr VkFormat GBUFFER_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat GBUFFER_ALBEDO_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat GBUFFER_NORMALS_FORMAT = VK_FORMAT_R16G16B16A16_SNORM;
constexpr VkFormat GBUFFER_ENTITY_FORMAT = VK_FORMAT_R16G16B16A16_UINT;
constexpr VkFormat GBUFFER_POSITIONS_FORMAT = VK_FORMAT_R16G16B16A16_SNORM;

// swapchain
VkSwapchainKHR g_Swapchain = VK_NULL_HANDLE;
VkSurfaceFormatKHR g_SwapchainSurfaceFormat = {};
VkExtent2D g_SwapchainExtent = {};
VkPresentModeKHR g_SwapchainPresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
std::vector<VkImage> g_SwapchainImages;
std::vector<VkImageView> g_SwapchainImageViews;
std::vector<VkSemaphore> g_SwapchainSemaphores;

// scratch images
VkUtils::Image g_GBuffer_Depth;
VkUtils::Image g_GBuffer_Albedo;
VkUtils::Image g_GBuffer_Normals;
VkUtils::Image g_GBuffer_Entity;
VkUtils::Image g_GBuffer_Positions;

VkUtils::Image g_CompositeFinal;
VkUtils::Image g_ShadowMap;
u32 shadowMapRes = 1024;

// misc
VkCommandPool g_ImmediateCommandPool = VK_NULL_HANDLE;
VkCommandBuffer g_ImmediateCmdBuffer = VK_NULL_HANDLE;
VkFence g_ImmediateFence = VK_NULL_HANDLE;

VkSampler g_TextureSamplerBasic = VK_NULL_HANDLE;

// descriptors
VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;

VkDescriptorSetLayout g_Gfx_GBuffer_DSLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout g_Gfx_Compose_DSLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout g_Gfx_Forward_DSLayout = VK_NULL_HANDLE;

VkDescriptorSetLayout g_ComputeDSLayout = VK_NULL_HANDLE;
VkDescriptorSet g_ComputeDS = VK_NULL_HANDLE;

VkDescriptorPool g_DescriptorPoolBindless = VK_NULL_HANDLE;
VkDescriptorSetLayout g_BindlessDSLayout = VK_NULL_HANDLE;
VkDescriptorSet g_BindlessDS = VK_NULL_HANDLE;

// compute & graphics pipelines
MyVkPipeline g_ComputePipeline;

MyVkPipeline g_GfxPipelineDeferred_GBuffer;
MyVkPipeline g_GfxPipelineDeferred_Compose;

MyVkPipeline g_GfxPipelineForward;
MyVkPipeline g_GfxPipelineForward_Simple;
MyVkPipeline g_GfxPipelineForward_Shadows;
MyVkPipeline g_GfxPipelineForward_Shadows_Sk;
MyVkPipeline g_GfxPipelineForward_Wireframe;
MyVkPipeline g_GfxPipelineForward_Sk;

// push constants
struct PushConstant0
{
	glm::vec4 colorStart;
	glm::vec4 colorEnd;
	glm::vec4 unused0;
	glm::vec4 unused1;
};

PushConstant0 g_ColorPushConst = {
	.colorStart = { 1.0f, 1.0f, 1.0f, 1.0f },
	.colorEnd = { 1.0f, 0.0f, 1.0f, 1.0f }
};

// geometry data
struct MeshPushConstant // todo: gia oltre i 128 bytes per le push constant lol
{
	glm::mat4 worldMatrix;
	glm::mat4 modelMatrix;
	VkDeviceAddress vertexBuffer;
};

struct SkMeshPushConstant
{
	glm::mat4 worldMatrix;
	glm::mat4 modelMatrix;
	VkDeviceAddress vertexBuffer;
	VkDeviceAddress animBuffer;
};

struct PushConstantShadow
{
	glm::mat4 lightView;
	VkDeviceAddress vertexBuffer;
	VkDeviceAddress animBuffer;
};

struct Transform
{
	glm::vec3 position = { 0.0f, 0.0f, 0.0f };
	glm::vec3 rotation = { 0.0f, 0.0f, 0.0f }; 
	glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
};

Transform g_MeshTransform;

double g_Time = 0.0;
float g_FrameTime = 0.0f; // seconds

struct UniBuffLighting
{
	glm::mat4 lightView;
	glm::vec4 ambient;
	glm::vec4 sunPos; // directional light source
	glm::vec4 sunColor;
	glm::vec4 viewPos;
	u32 textureIndex;
	u32 shadowPassEnabled;
	u32 pad[2];
};

void CreateSwapchain()
{
	VkDevice device = g_RendererContext.GetDevice();
	VkPhysicalDevice gpu = g_RendererContext.GetGPU();
	VkSurfaceKHR surface = g_RendererContext.GetSurface();

	constexpr VkFormat TARGET_FORMAT = VK_FORMAT_B8G8R8A8_UNORM; // _SRGB is washed out?!?
	constexpr VkColorSpaceKHR TARGET_COLOR_SPACE = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	constexpr VkPresentModeKHR TARGET_PRESENT_MODE = VK_PRESENT_MODE_IMMEDIATE_KHR;
	//constexpr VkPresentModeKHR TARGET_PRESENT_MODE = VK_PRESENT_MODE_FIFO_KHR;

	VkSurfaceCapabilitiesKHR surfaceCapabilites = {};
	std::vector<VkSurfaceFormatKHR> availableFormats;
	std::vector<VkPresentModeKHR> availablePresentModes;

	uint32_t formatCount;
	vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr));
	availableFormats.resize(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, availableFormats.data());

	uint32_t presentModeCount;
	vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &presentModeCount, nullptr));
	availablePresentModes.resize(presentModeCount);
	vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &presentModeCount, availablePresentModes.data()));

	vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surfaceCapabilites));

	g_SwapchainSurfaceFormat = availableFormats[0]; // default
	g_SwapchainExtent = surfaceCapabilites.currentExtent; // default
	g_SwapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR; // default, always available

	// find best format
	for (auto fmt : availableFormats)
	{
		if (fmt.format == TARGET_FORMAT && fmt.colorSpace == TARGET_COLOR_SPACE)
		{
			g_SwapchainSurfaceFormat = fmt;
			break;
		}
	}

	// find best present mode
	for (auto mode : availablePresentModes)
	{
		if (mode == TARGET_PRESENT_MODE)
		{
			g_SwapchainPresentMode = mode;
			break;
		}
	}

	// figure out resolution
	if (surfaceCapabilites.currentExtent.width == std::numeric_limits<uint32_t>::max()) // we can choose yeee
	{
		int width, height;
		glfwGetFramebufferSize(g_Window, &width, &height);

		g_SwapchainExtent.width = std::clamp((uint32_t)width, surfaceCapabilites.minImageExtent.width, surfaceCapabilites.maxImageExtent.width);
		g_SwapchainExtent.height = std::clamp((uint32_t)height, surfaceCapabilites.minImageExtent.height, surfaceCapabilites.maxImageExtent.height);
	}

	VkSwapchainCreateInfoKHR swapchainInfo = {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = surface;
	swapchainInfo.minImageCount = surfaceCapabilites.minImageCount + 1;
	swapchainInfo.imageFormat = g_SwapchainSurfaceFormat.format;
	swapchainInfo.imageColorSpace = g_SwapchainSurfaceFormat.colorSpace;
	swapchainInfo.imageExtent = g_SwapchainExtent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // questo vuol dire che tutti i framebuffer hanno lo stesso tipo di attachment????
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // ez, abbiamo la stessa famiglia che fa present e graphics
	swapchainInfo.preTransform = surfaceCapabilites.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // questo solo per le finestra, quindi chissene
	swapchainInfo.presentMode = g_SwapchainPresentMode;
	swapchainInfo.clipped = VK_TRUE; // pixel nascosti da altre fineste, di nuovo chissene
	swapchainInfo.oldSwapchain = VK_NULL_HANDLE; // non stiamo mica fancendo un resize...

	VkResult swapchainCreateRes = vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &g_Swapchain);
	vkCheck(swapchainCreateRes);

	uint32_t swapchainImageCount; // noi abbiamo solo specificato il numero minimo, potrebbero essere di piu, quindi chiediamo
	vkCheck(vkGetSwapchainImagesKHR(device, g_Swapchain, &swapchainImageCount, nullptr));
	g_SwapchainImages.resize(swapchainImageCount);
	vkCheck(vkGetSwapchainImagesKHR(device, g_Swapchain, &swapchainImageCount, g_SwapchainImages.data()));

	VkImageViewCreateInfo imageViewInfo = {};
	imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewInfo.format = g_SwapchainSurfaceFormat.format;
	imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageViewInfo.subresourceRange.baseMipLevel = 0;
	imageViewInfo.subresourceRange.levelCount = 1;
	imageViewInfo.subresourceRange.baseArrayLayer = 0;
	imageViewInfo.subresourceRange.layerCount = 1;

	g_SwapchainImageViews.resize(swapchainImageCount);
	g_SwapchainSemaphores.resize(swapchainImageCount);
	for(uint32_t i = 0; i < swapchainImageCount; i++)
	{
		// image view
		imageViewInfo.image = g_SwapchainImages[i];
		vkCheck(vkCreateImageView(device, &imageViewInfo, nullptr, &g_SwapchainImageViews[i]));
		
		// semaphore
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &g_SwapchainSemaphores[i]));
	}

	g_RendererContext.QueueShutdownFunc([device]() {
		for (int i = 0; i < g_SwapchainImages.size(); i++)
		{
			vkDestroyImageView(device, g_SwapchainImageViews[i], nullptr);
			vkDestroySemaphore(device, g_SwapchainSemaphores[i], nullptr);
		}

		vkDestroySwapchainKHR(device, g_Swapchain, nullptr);
	});
}

// void CreateRenderPass()
// {
	/*
		
		pipeline senza dynamic_rendering:

		A: Render pass
			- B: lista di attachmetns utilizzati nella render pass
			- C: lista di subpass
				- D: lista di attachment utilizzati nella subpass (indici dentro B)

	// render pass 0
	{
		std::array<VkAttachmentDescription, 1> attachments;
		// color
		attachments[0] = {};
		attachments[0].format = g_SwapchainSurfaceFormat.format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // zeremomory prima del render
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // salva dopo il render, cosi possiamo copiare su swapchain target (schermo)
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // non abbiamo ancora depth bfufer, quindi random
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // non abbiamo ancora depth bfufer, quindi random
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		std::array<VkSubpassDescription, 1> subpasses;
		
		std::array<VkAttachmentReference, 1> subpass0ColorAttachments;
		subpass0ColorAttachments[0].attachment = 0; // 0 -> attachments[0] -> color
		subpass0ColorAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		subpasses[0] = {};
		subpasses[0].colorAttachmentCount = (uint32_t)subpass0ColorAttachments.size();
		subpasses[0].pColorAttachments = subpass0ColorAttachments.data();
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = (uint32_t)attachments.size();
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = (uint32_t)subpasses.size();
		renderPassInfo.pSubpasses = subpasses.data();

		VkRenderPass renderPass0;
		VkResult res = vkCreateRenderPass(g_Device, &renderPassInfo, nullptr, &renderPass0);
		vkCheck(res);
	}

	*/
// }

void CreatePipeline()
{
	VkDevice device = g_RendererContext.GetDevice();

	// compute pipeline
	{
		VkPushConstantRange computePushConstant0 = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstant0) };

		ComputePipelineBuilder computeBuilder;
		computeBuilder.m_ComputeShader = "shaders/bin/comp.spv";
		computeBuilder.m_Descriptors = { g_ComputeDSLayout };
		computeBuilder.m_PushConstants = { computePushConstant0 };

		//g_ComputePipeline = computeBuilder.Build(device);
	}

	VkPushConstantRange gfxMeshPushConstant = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant) };
	VkPushConstantRange gfxSkMeshPushConstant = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SkMeshPushConstant) };

	// graphics pipeline gbuffer
	{			
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert_gbuffer.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag_gbuffer.spv";
		graphicsBuilder.m_ColorAttachments = { GBUFFER_ALBEDO_FORMAT, GBUFFER_NORMALS_FORMAT, GBUFFER_ENTITY_FORMAT, GBUFFER_POSITIONS_FORMAT };
		graphicsBuilder.m_PushConstants = { gfxMeshPushConstant };
		graphicsBuilder.m_Descriptors = { g_Gfx_GBuffer_DSLayout };
		//graphicsBuilder.m_BlendMode = EGraphicsBlendMode::GFX_BLEND_ADDITIVE;
		/*
			We give it depth write, and as operator GREATER_OR_EQUAL.
			As mentioned, because 0 is far and 1 is near,
			we will want to only render the pixels if the current depth value is greater than the depth value on the depth image.
		*/
		graphicsBuilder.m_DepthMode = { VK_COMPARE_OP_LESS_OR_EQUAL, GBUFFER_DEPTH_FORMAT, true };

		g_GfxPipelineDeferred_GBuffer = graphicsBuilder.Build(device, g_GfxPipelineDeferred_GBuffer.layout);
	}

	// graphics pipeline compose
	{
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert_composite.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag_composite.spv";
		graphicsBuilder.m_ColorAttachments = { DRAW_FORMAT };
		graphicsBuilder.m_Descriptors = { g_Gfx_Compose_DSLayout };

		g_GfxPipelineDeferred_Compose = graphicsBuilder.Build(device, g_GfxPipelineDeferred_Compose.layout);
	}

	// graphics pipeline forward
	{
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert_forward.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag_forward.spv";
		graphicsBuilder.m_ColorAttachments = { DRAW_FORMAT };
		graphicsBuilder.m_PushConstants = { gfxMeshPushConstant };
		graphicsBuilder.m_Descriptors = { g_Gfx_Forward_DSLayout, g_BindlessDSLayout };
		graphicsBuilder.m_DepthMode = { VK_COMPARE_OP_LESS_OR_EQUAL, GBUFFER_DEPTH_FORMAT, true };

		g_GfxPipelineForward = graphicsBuilder.Build(device, g_GfxPipelineForward.layout);

		// wireframe
		graphicsBuilder.m_PolyMode = VK_POLYGON_MODE_LINE;
		g_GfxPipelineForward_Wireframe = graphicsBuilder.Build(device, g_GfxPipelineForward.layout);
		graphicsBuilder.m_PolyMode = VK_POLYGON_MODE_FILL;
		
		// shadow map
		graphicsBuilder.m_FragmentShader.clear();
		graphicsBuilder.m_ColorAttachments.clear();
		g_GfxPipelineForward_Shadows = graphicsBuilder.Build(device, g_GfxPipelineForward_Shadows.layout);
	}

	// graphics pipeline forward - simple
	{
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert_forward.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag_forward_simple.spv";
		graphicsBuilder.m_ColorAttachments = { DRAW_FORMAT };
		graphicsBuilder.m_PushConstants = { gfxMeshPushConstant };
		graphicsBuilder.m_Descriptors = { g_Gfx_Forward_DSLayout };
		graphicsBuilder.m_DepthMode = { VK_COMPARE_OP_LESS_OR_EQUAL, GBUFFER_DEPTH_FORMAT, true };

		g_GfxPipelineForward_Simple = graphicsBuilder.Build(device, g_GfxPipelineForward_Simple.layout);
	}

	// graphics pipeline forward - skeletal mesh
	{
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert_forward_sk.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag_forward.spv";
		graphicsBuilder.m_ColorAttachments = { DRAW_FORMAT };
		graphicsBuilder.m_PushConstants = { gfxSkMeshPushConstant };
		graphicsBuilder.m_Descriptors = { g_Gfx_Forward_DSLayout, g_BindlessDSLayout };
		graphicsBuilder.m_DepthMode = { VK_COMPARE_OP_LESS_OR_EQUAL, GBUFFER_DEPTH_FORMAT, true };

		g_GfxPipelineForward_Sk = graphicsBuilder.Build(device, g_GfxPipelineForward_Sk.layout);

		// shadow map
		graphicsBuilder.m_FragmentShader.clear();
		graphicsBuilder.m_ColorAttachments.clear();
		g_GfxPipelineForward_Shadows_Sk = graphicsBuilder.Build(device, g_GfxPipelineForward_Shadows_Sk.layout);
	}
}

void CreateCommands()
{
	VkCommandPoolCreateInfo commandPoolInfo = {};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;
	commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolInfo.queueFamilyIndex = g_RendererContext.GetRendererDevice().GetGraphicsQueueFamilyIndex();

	VkDevice device = g_RendererContext.GetDevice();

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
	{
		vkCheck(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &g_FramesData[i].commandPool));

		// command buffer for gfx
		VkCommandBufferAllocateInfo cmdAllocInfo = {};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.pNext = nullptr;
		cmdAllocInfo.commandPool = g_FramesData[i].commandPool;
		cmdAllocInfo.commandBufferCount = 1; // ne abbiamo solo uno: VkCommandBuffer::commandBuffer
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		vkCheck(vkAllocateCommandBuffers(device, &cmdAllocInfo, &g_FramesData[i].commandBuffer));

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // inizia a flaggarla segnalata, altrimneti se facciamo un wait prima di segnalarla ci blocchiamo (primo wait senza averla toccata dopo la creazione)
		vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &g_FramesData[i].fence));

		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &g_FramesData[i].swapchainSemaphore));
	}

	// roba per la roba immediata...
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // inizia a flaggarla segnalata, altrimneti se facciamo un wait prima di segnalarla ci blocchiamo (primo wait senza averla toccata dopo la creazione)
	vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &g_ImmediateFence));

	vkCheck(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &g_ImmediateCommandPool)); // sempre graphics a quanto pare

	VkCommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = g_ImmediateCommandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	vkCheck(vkAllocateCommandBuffers(device, &cmdAllocInfo, &g_ImmediateCmdBuffer));

	g_RendererContext.QueueShutdownFunc([device]() {

		// roba immediata
		vkDestroyCommandPool(device, g_ImmediateCommandPool, nullptr); // distrugge tutti i command buffer allocati con lui
		vkDestroyFence(device, g_ImmediateFence, nullptr);

		// cmd / fence e semafori dei frame
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {

			vkDestroyCommandPool(device, g_FramesData[i].commandPool, nullptr); // distrugge tutti i command buffer allocati con lui

			//destroy sync objects
			vkDestroyFence(device, g_FramesData[i].fence, nullptr);
			vkDestroySemaphore(device, g_FramesData[i].swapchainSemaphore, nullptr);

			g_FramesData[i].deletionQueue.Flush();
		}
	});
}

void ImmediateSubmit(std::function<void(VkCommandBuffer)> func)
{
	VkDevice device = g_RendererContext.GetDevice();

	vkCheck(vkResetFences(device, 1, &g_ImmediateFence));
	vkCheck(vkResetCommandBuffer(g_ImmediateCmdBuffer, 0));

	// begin the command buffer recording. We will use this command buffer exactly
	// once, so we want to let vulkan know that
	VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
	cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufferBeginInfo.pNext = nullptr;
	cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkCheck(vkBeginCommandBuffer(g_ImmediateCmdBuffer, &cmdBufferBeginInfo));

	func(g_ImmediateCmdBuffer); // call user function

	vkCheck(vkEndCommandBuffer(g_ImmediateCmdBuffer));

	VkCommandBufferSubmitInfo cmdinfo = VkUtils::CommandBufferSubmitInfo(g_ImmediateCmdBuffer);
	VkSubmitInfo2 submitInfo = VkUtils::SubmitInfo(&cmdinfo, nullptr, nullptr);

	// submit command buffer to the queue and execute it.
	//  _renderFence will now block until the graphic commands finish execution
	vkCheck(vkQueueSubmit2(g_RendererContext.GetRendererDevice().GetGraphicsQueue(), 1, &submitInfo, g_ImmediateFence));

	vkCheck(vkWaitForFences(device, 1, &g_ImmediateFence, true, 9999999999));
}

void CreateRenderImage()
{
	VkDevice device = g_RendererContext.GetDevice();

	VkUtils::ImageDesc gbufferAttachmentTemplate = {};
	gbufferAttachmentTemplate.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	gbufferAttachmentTemplate.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	gbufferAttachmentTemplate.width = g_SwapchainExtent.width;
	gbufferAttachmentTemplate.height = g_SwapchainExtent.height;
	gbufferAttachmentTemplate.tiling = VK_IMAGE_TILING_OPTIMAL;

	gbufferAttachmentTemplate.format = GBUFFER_ALBEDO_FORMAT;
	g_GBuffer_Albedo = VkUtils::CreateImage(device, gbufferAttachmentTemplate);
	
	gbufferAttachmentTemplate.format = GBUFFER_NORMALS_FORMAT;
	g_GBuffer_Normals = VkUtils::CreateImage(device, gbufferAttachmentTemplate);

	gbufferAttachmentTemplate.format = GBUFFER_ENTITY_FORMAT;
	g_GBuffer_Entity = VkUtils::CreateImage(device, gbufferAttachmentTemplate);
	
	gbufferAttachmentTemplate.format = GBUFFER_POSITIONS_FORMAT;
	g_GBuffer_Positions = VkUtils::CreateImage(device, gbufferAttachmentTemplate);

	gbufferAttachmentTemplate.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	gbufferAttachmentTemplate.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	gbufferAttachmentTemplate.format = GBUFFER_DEPTH_FORMAT;
	g_GBuffer_Depth = VkUtils::CreateImage(device, gbufferAttachmentTemplate);

	VkUtils::ImageDesc compositeFinalAttachment = {};
	compositeFinalAttachment.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	compositeFinalAttachment.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	compositeFinalAttachment.width = g_SwapchainExtent.width;
	compositeFinalAttachment.height = g_SwapchainExtent.height;
	compositeFinalAttachment.tiling = VK_IMAGE_TILING_OPTIMAL;
	compositeFinalAttachment.format = DRAW_FORMAT;
	g_CompositeFinal = VkUtils::CreateImage(device, compositeFinalAttachment);

	VkUtils::ImageDesc shadowMapAttachment = {};
	shadowMapAttachment.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	shadowMapAttachment.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	shadowMapAttachment.width = shadowMapRes;
	shadowMapAttachment.height = shadowMapRes;
	shadowMapAttachment.tiling = VK_IMAGE_TILING_OPTIMAL;
	shadowMapAttachment.format = GBUFFER_DEPTH_FORMAT;
	g_ShadowMap = VkUtils::CreateImage(device, shadowMapAttachment);

	g_RendererContext.QueueShutdownFunc([device]() {
		VkUtils::DestroyImage(device, g_GBuffer_Albedo);
		VkUtils::DestroyImage(device, g_GBuffer_Normals);
		VkUtils::DestroyImage(device, g_GBuffer_Entity);
		VkUtils::DestroyImage(device, g_GBuffer_Positions);
		VkUtils::DestroyImage(device, g_GBuffer_Depth);
		VkUtils::DestroyImage(device, g_CompositeFinal);
		VkUtils::DestroyImage(device, g_ShadowMap);
	});
}

void CreateDescriptors()
{
	VkDevice device = g_RendererContext.GetDevice();

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 12 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = 8;
	poolInfo.poolSizeCount = (u32)std::size(poolSizes);
	poolInfo.pPoolSizes = poolSizes;
	vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &g_DescriptorPool));

	// Compute
	{
		VkUtils::DescSetBinding bindings[] = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
		};

		g_ComputeDSLayout = VkUtils::CreateDescSetLayout(device, bindings);

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = g_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &g_ComputeDSLayout;
		vkCheck(vkAllocateDescriptorSets(device, &allocInfo, &g_ComputeDS));
	}

	// Gfx: Deferred pipeline - GBuffer
	{
		VkUtils::DescSetBinding bindings[] = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } // texture
		};

		g_Gfx_GBuffer_DSLayout = VkUtils::CreateDescSetLayout(device, bindings);
	}

	// Gfx: Deferred pipeline - Final compose
	{
		VkUtils::DescSetBinding bindings[] = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },	// albedo
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },	// normals
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },	// entity id
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },	// positions
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }			// lighting
		};

		g_Gfx_Compose_DSLayout = VkUtils::CreateDescSetLayout(device, bindings);
	}

	// Gfx: Forward pipeline
	{
		VkUtils::DescSetBinding bindings[] = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },	// texture
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },			// lighting
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },   // depth map
		};

		g_Gfx_Forward_DSLayout = VkUtils::CreateDescSetLayout(device, bindings);
	}

	constexpr u32 kDescriptorsCount = 3 * FRAMES_IN_FLIGHT; // gbuffer pipeline, compose pipeline, forward pipeline (compute ignored)

	VkDescriptorSet gfxSets[kDescriptorsCount];

	VkDescriptorSetLayout gfxLayouts[3 * FRAMES_IN_FLIGHT] = { // initialized just for 2 fifs, make a loop
		g_Gfx_GBuffer_DSLayout, g_Gfx_Compose_DSLayout, g_Gfx_Forward_DSLayout,
		g_Gfx_GBuffer_DSLayout, g_Gfx_Compose_DSLayout, g_Gfx_Forward_DSLayout
	};

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = g_DescriptorPool;
	allocInfo.descriptorSetCount = kDescriptorsCount;
	allocInfo.pSetLayouts = gfxLayouts;
	vkAllocateDescriptorSets(device, &allocInfo, gfxSets);

	VkUtils::DescSetUpdate bindingsUpdate[] = {
		{
			.image = { g_TextureSamplerBasic, g_GBuffer_Albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		},
		{
			.image = { g_TextureSamplerBasic, g_GBuffer_Normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		},
		{
			.image = { g_TextureSamplerBasic, g_GBuffer_Entity.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		},
		{
			.image = { g_TextureSamplerBasic, g_GBuffer_Positions.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		}
	};

	VkDescriptorSet* currentFrameSet = gfxSets;
	for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorSet gbufferSet = currentFrameSet[0];
		VkDescriptorSet composeSet = currentFrameSet[1];
		VkDescriptorSet forwardSet = currentFrameSet[2];
		
		// attachments
		VkUtils::UpdateDescBindings(device, composeSet, bindingsUpdate, 0);

		// uniform buffer for lighitng
		VkUtils::Buffer uniBuffLighting = VkUtils::CreateBuffer(device, sizeof(UniBuffLighting),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VkUtils::UpdateDescBinding(device, composeSet, uniBuffLighting.buffer, sizeof(UniBuffLighting), 4);
		VkUtils::UpdateDescBinding(device, forwardSet, uniBuffLighting.buffer, sizeof(UniBuffLighting), 1);
		VkUtils::UpdateDescBinding(device, forwardSet, g_ShadowMap.view, g_TextureSamplerBasic, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);

		g_FramesData[i].descriptorGBuffer = gbufferSet;
		g_FramesData[i].descriptorCompose = composeSet;
		g_FramesData[i].descriptorForward = forwardSet;
		g_FramesData[i].uniformBufferLighting = uniBuffLighting;

		currentFrameSet += 3;
	}

	g_RendererContext.QueueShutdownFunc([device]() {
		vkDestroyDescriptorPool(device, g_DescriptorPool, nullptr);

		vkDestroyDescriptorSetLayout(device, g_ComputeDSLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, g_Gfx_GBuffer_DSLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, g_Gfx_Compose_DSLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, g_Gfx_Forward_DSLayout, nullptr);

		for (FrameData& frameData : g_FramesData)
			VkUtils::DestroyBuffer(device, frameData.uniformBufferLighting);
	});
}

void CreateDescriptorsBindless()
{
	VkDevice device = g_RendererContext.GetDevice();

	constexpr u32 MAX_TEXTURES = 128;

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES }
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT; // concurrently update single items whenevere the hell you want (when not used by shaders)
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = (u32)std::size(poolSizes);
	poolInfo.pPoolSizes = poolSizes;
	vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &g_DescriptorPoolBindless));

	VkDescriptorSetLayoutBinding bindings[1] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_TEXTURES,
			.stageFlags = VK_SHADER_STAGE_ALL
		}
	};

	const VkDescriptorBindingFlagsEXT flags =
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
		VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
	bindingFlags.bindingCount = 1;
	bindingFlags.pBindingFlags = &flags;

	VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
	layoutInfo.pNext = &bindingFlags;
	layoutInfo.bindingCount = (u32)std::size(bindings);
	layoutInfo.pBindings = bindings;
	 
	vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &g_BindlessDSLayout));

	VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocInfo.descriptorPool = g_DescriptorPoolBindless;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &g_BindlessDSLayout;

	vkCheck(vkAllocateDescriptorSets(device, &allocInfo, &g_BindlessDS));

	g_RendererContext.QueueShutdownFunc([device]() {
		vkDestroyDescriptorPool(device, g_DescriptorPoolBindless, nullptr);
		vkDestroyDescriptorSetLayout(device, g_BindlessDSLayout, nullptr);
	});
}

void InitImgui()
{
	VkDevice device = g_RendererContext.GetDevice();

	// 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = { 
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } 
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imgui_pool;
	vkCheck(vkCreateDescriptorPool(device, &pool_info, nullptr, &imgui_pool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	verify(ImGui::CreateContext());

	// this initializes imgui for SDL

	verify(ImGui_ImplGlfw_InitForVulkan(g_Window, true /* chissene per sto test */));

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = g_RendererContext.GetInstance();
	init_info.PhysicalDevice = g_RendererContext.GetGPU();
	init_info.Device = device;
	init_info.Queue = g_RendererContext.GetRendererDevice().GetGraphicsQueue();
	init_info.DescriptorPool = imgui_pool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &g_SwapchainSurfaceFormat.format;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	verify(ImGui_ImplVulkan_Init(&init_info));

	ImGui::StyleColorsDark();
	
	g_RendererContext.QueueShutdownFunc([imgui_pool, device]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(device, imgui_pool, nullptr);
	});
}

void CreateTextureSamplers() 
{
	VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sampl.magFilter = VK_FILTER_NEAREST;
	sampl.minFilter = VK_FILTER_NEAREST;

	vkCheck(vkCreateSampler(g_RendererContext.GetDevice(), &sampl, nullptr, &g_TextureSamplerBasic));

	g_RendererContext.QueueShutdownFunc([]() {
		vkDestroySampler(g_RendererContext.GetDevice(), g_TextureSamplerBasic, nullptr);
	});
}

struct LoadingState
{
	u32 currentlyLoaded;
	u32 loadTarget;
};

LoadingState g_LoadingState;

static std::vector<Mesh*> g_Meshes;
static std::vector<SkeletalMesh*> g_SkeletalMeshes;
static std::vector<Texture*> g_Textures;

struct SceneEntity
{
	std::string debugName;

	Mesh* mesh = nullptr;
	SkeletalMesh* skeletalMesh = nullptr;
	
	Transform transform;
	bool visible;
};


static std::vector<SceneEntity> g_Entities;

static glm::vec3 s_CamPos = { -56.3f, 148.6f, 210.3f };
static glm::vec3 s_CamRot = { 13.7f, 168.8f, 0.0f };
static float s_CamSpeed = 10.0f;
static float s_CamFOV = 60.0f;
static ImVec2 s_MousePos = {};
static float s_MouseSens = 0.1f;

static UniBuffLighting s_UniBuffLighting = {};
static Texture* s_BoundTexture = nullptr;

static glm::vec3 s_LightRotation;
static bool s_LightPosUpdate = false;

static Texture s_DefaultTexture;

static std::pair<VkUtils::Buffer, VkDeviceAddress> s_AnimBuffers[FRAMES_IN_FLIGHT];
static std::pair<VkUtils::Buffer, void*> s_AnimBuffersStaging[FRAMES_IN_FLIGHT];

void LoadGeometry()
{
	// default assets
	u32 pixels[] = { 0xffffffff };
	s_DefaultTexture.DebugName = "White texture";
	s_DefaultTexture.SetData(pixels, sizeof(pixels), { 1, 1, EImageFormat::RGBA8 });
	g_ResourceFactory.CreateTexture(&s_DefaultTexture);
	g_ResourceFactory.PushLoading({
		.texture = &s_DefaultTexture,
		.size = s_DefaultTexture.GetMemoryFootprint(),
		.type = EResourceType::Texture
	});
	g_AssetManager.AssignTextureIndex(&s_DefaultTexture);

	// textures
	g_Textures.push_back(&s_DefaultTexture);
	g_Textures.push_back(g_AssetManager.LoadTexture(std::filesystem::path("assets") / "doom.jpg"));
	g_Textures.push_back(g_AssetManager.LoadTexture(std::filesystem::path("assets") / "textures" / "brickwall.png"));
	g_Textures.push_back(g_AssetManager.LoadTexture(std::filesystem::path("assets") / "textures" / "grass.png"));

	// meshes
	g_Meshes.push_back(g_AssetManager.LoadMesh(std::filesystem::path("assets") / "cube.glb"));
	g_Meshes.push_back(g_AssetManager.LoadMesh(std::filesystem::path("assets") / "diorama.glb"));

	// skeletal meshes
	g_SkeletalMeshes.push_back(g_AssetManager.LoadSkeletalMesh(std::filesystem::path("assets") / "Punching.fbx"));

	g_LoadingState.loadTarget = (u32)(g_Meshes.size() + g_Textures.size() + g_SkeletalMeshes.size());

	// Scene entities
	g_Entities.emplace_back("Floor", g_Meshes[0], nullptr, Transform{ glm::vec3(-40.0f, -19.1f, 14.5f), glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f) }, true);
	g_Entities.emplace_back("Cube", g_Meshes[0], nullptr, Transform{ glm::vec3(-5.4f, -9.2f, 21.7f), glm::vec3(0.0f, 26.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f) }, true);
	g_Entities.emplace_back("Diorama", g_Meshes[1], nullptr, Transform{ glm::vec3(-2.0f, 0.0f, 50.0f), glm::vec3(270.0f, 263.0f, 0.0f), glm::vec3(1.0f) }, true);
	g_Entities.emplace_back("Punching", nullptr, g_SkeletalMeshes[0], Transform{ glm::vec3(0.0f), glm::vec3(0.0f, 180.0f, 0.0f), glm::vec3(1.0f) }, true);

	// scene settings
	s_BoundTexture = g_Textures[0];

	s_UniBuffLighting.ambient = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
	s_UniBuffLighting.sunPos = glm::vec4(0.0f, 103.0f, 285.0f, 1.0f);
	s_UniBuffLighting.sunColor = glm::vec4(1.0f);
	s_UniBuffLighting.viewPos = glm::vec4(1.0f);
	s_UniBuffLighting.shadowPassEnabled = 0;

	// cleanup
	g_RendererContext.QueueShutdownFunc([]() {

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++)
		{
			VkUtils::DestroyBuffer(g_RendererContext.GetDevice(), s_AnimBuffers[i].first);
			vkUnmapMemory(g_RendererContext.GetDevice(), s_AnimBuffersStaging[i].first.memory);
			VkUtils::DestroyBuffer(g_RendererContext.GetDevice(), s_AnimBuffersStaging[i].first);
		}

		for (auto skeletalMesh : g_SkeletalMeshes)
		{
			g_ResourceFactory.DestroySkeletalMesh(skeletalMesh);
			delete skeletalMesh;
		}

		for (auto mesh : g_Meshes)
		{
			g_ResourceFactory.DestroyMesh(mesh);
			delete mesh;
		}
		
		for (auto texture : g_Textures)
		{
			g_ResourceFactory.DestroyTexture(texture);

			if(texture != &s_DefaultTexture)
				delete texture;
		}
	});
}

void InitVulkan()
{
	HWND mainWnd = glfwGetWin32Window(g_Window);
	g_RendererContext.Init((void*)mainWnd);
	
	VkUtils::Init(g_RendererContext.GetGPU());
	
	CreateSwapchain();
	CreateCommands();
	CreateRenderImage();

	CreateTextureSamplers();
	
	CreateDescriptors();
	CreateDescriptorsBindless();

	//CreateRenderPass(); dynamic_rendering yeee
	CreatePipeline();

	VkDevice device = g_RendererContext.GetDevice();
	g_RendererContext.QueueShutdownFunc([device]() {
		
		vkDestroyPipeline(device, g_GfxPipelineForward_Shadows_Sk.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineForward_Shadows_Sk.layout, nullptr);

		vkDestroyPipeline(device, g_GfxPipelineForward_Sk.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineForward_Sk.layout , nullptr);

		vkDestroyPipeline(device, g_GfxPipelineForward_Shadows.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineForward_Shadows.layout, nullptr);

		vkDestroyPipeline(device, g_GfxPipelineForward_Simple.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineForward_Simple.layout, nullptr);
		
		vkDestroyPipeline(device, g_GfxPipelineForward_Wireframe.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineForward.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineForward.layout, nullptr);

		vkDestroyPipeline(device, g_GfxPipelineDeferred_GBuffer.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineDeferred_GBuffer.layout, nullptr);

		vkDestroyPipeline(device, g_GfxPipelineDeferred_Compose.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineDeferred_Compose.layout, nullptr);

		vkDestroyPipeline(device, g_ComputePipeline.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_ComputePipeline.layout, nullptr);
	});
	//CreateFramebuffers(); dynamic_rendering yeee
}

void ShutdownVulkan()
{
	vkCheck(vkDeviceWaitIdle(g_RendererContext.GetDevice()));
	g_RendererContext.Shutdown();
}

static bool s_PipelinesAreDirty = false;
static bool s_Deferred = false;
static bool s_Wireframe = false;

void ImGuii()
{
	ImGui::Begin("Roba");

	ImVec4 lblColor = g_LoadingState.currentlyLoaded == g_LoadingState.loadTarget ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	ImGui::TextColored(lblColor, "Scene loading: %d/%d", g_LoadingState.currentlyLoaded, g_LoadingState.loadTarget);

	ImGui::Separator();

	if (ImGui::BeginCombo("Texture", s_BoundTexture->DebugName.c_str()))
	{
		for (Texture* texture : g_Textures)
		{
			if (ImGui::Selectable(texture->DebugName.c_str(), texture == s_BoundTexture))
				s_BoundTexture = texture;
		}

		ImGui::EndCombo();
	}

	ImGui::Separator();

	static float stableFrametime = g_FrameTime;
	static float lastSample = (float)g_Time;

	if (g_Time - lastSample > 0.05f)
	{
		stableFrametime = g_FrameTime;
		lastSample = (float)g_Time;
	}

	ImGui::Text("Frametime: %.2fms (%.0f FPS)", stableFrametime * 1000.0f, 1.0f / stableFrametime);

	ImGui::Separator();

	ImGui::ColorEdit4("Lighting - Ambient", glm::value_ptr(s_UniBuffLighting.ambient));

	if(s_LightPosUpdate)
		ImGui::BeginDisabled();
	
	ImGui::DragFloat3("Lighting - Sun pos (directional light src)", glm::value_ptr(s_UniBuffLighting.sunPos));
	
	if (s_LightPosUpdate) 
		ImGui::EndDisabled();

	ImGui::DragFloat3("Lighting - Light rotation", glm::value_ptr(s_LightRotation));

	ImGui::ColorEdit3("Lighting - Sun color (directional light color)", glm::value_ptr(s_UniBuffLighting.sunColor));

	ImGui::Checkbox("Lighting - auto move", &s_LightPosUpdate);

	static bool shadowPassEnabled = s_UniBuffLighting.shadowPassEnabled == 1;
	ImGui::Checkbox("Shadows - enable shadow pass", &shadowPassEnabled);
	s_UniBuffLighting.shadowPassEnabled = shadowPassEnabled ? 1 : 0;

	ImGui::Checkbox("Wireframe", &s_Wireframe);

	ImGui::Separator();

	ImGui::DragFloat3("Cam Location", glm::value_ptr(s_CamPos), 0.005f);
	ImGui::DragFloat3("Cam Rotation", glm::value_ptr(s_CamRot), 0.1f);
	ImGui::DragFloat("Cam FOV", &s_CamFOV, 0.001f);
	ImGui::DragFloat("Cam Speed", &s_CamSpeed, 0.1f);

	ImGui::Separator();

	static u32 selectedEntityIndex = 3;
	for (u32 i = 0; i < (u32)g_Entities.size(); i++)
	{
		SceneEntity& ent = g_Entities[i];

		if (ImGui::Selectable(ent.debugName.c_str(), selectedEntityIndex == i))
			selectedEntityIndex = i;
	}

	if (selectedEntityIndex >= 0 && selectedEntityIndex < g_Entities.size())
	{
		SceneEntity& ent = g_Entities[selectedEntityIndex];

		ImGui::DragFloat3("Model Location", glm::value_ptr(ent.transform.position), 0.005f);
		ImGui::DragFloat3("Model Rotation", glm::value_ptr(ent.transform.rotation), 0.1f);
		ImGui::DragFloat3("Model Scale", glm::value_ptr(ent.transform.scale), 0.005f);
		ImGui::Checkbox("Visible", &ent.visible);
	}

	ImGui::Separator();

	if (ImGui::Button("Reload shaders (in realta pipeline)"))
	{
		s_PipelinesAreDirty = true;
	}

	ImGui::Checkbox("Deferred (broken)", &s_Deferred);

	if (shadowPassEnabled)
	{
		static VkDescriptorSet boh = ImGui_ImplVulkan_AddTexture(g_TextureSamplerBasic, g_ShadowMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		ImGui::Image(boh, { 300, 300 });
	}

	ImGui::End();
}

void UpdateAnimations()
{
	for (SkeletalMesh* skMesh : g_SkeletalMeshes)
	{
		if (!skMesh->IsLoaded())
			continue;

		Animation* anim = (Animation*)&skMesh->GetAnimations()[0];

		float animDurationSeconds = anim->frameCount * (1.0f / anim->frameRate);
		float howManyRolls = floor((float)g_Time / animDurationSeconds);
		float rollsTime = howManyRolls * animDurationSeconds;
		float currentAnimTime = (float)g_Time - rollsTime;

		auto& [staging, mapped] = s_AnimBuffersStaging[g_FrameIndex];

		Anim::GenAnimationFrame(anim, currentAnimTime, (glm::mat4*)mapped);
	}
}

void Update(float deltaTime)
{
	// asset straming
	if (g_LoadingState.currentlyLoaded < g_LoadingState.loadTarget)
	{
		std::vector<PendingLoadingRes> loadedAssets;
		u32 loadedAssetsCount = g_AssetManager.CheckLoadedAssets(loadedAssets);

		for (PendingLoadingRes& res : loadedAssets)
		{
			if (res.type == EResourceType::Texture)
			{
				// update bindless descriptor
				u32 index = res.texture->GetImageIndex();

				VkUtils::UpdateDescBinding(g_RendererContext.GetDevice(), g_BindlessDS, res.texture->GetImage().view,
					g_TextureSamplerBasic, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, index);
			}
			else if (res.type == EResourceType::SkeletalMeshBuffers)
			{
				// alloc anim data
				VkDevice gpu = g_RendererContext.GetDevice();
				u32 bufferSize = sizeof(glm::mat4) * res.skeletalMesh->GetBoneCount();

				for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++)
				{
					auto& [finalBuffer, gpuAddress] = s_AnimBuffers[i];

					finalBuffer = VkUtils::CreateBuffer(gpu, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
						VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

					VkBufferDeviceAddressInfo addressInfo = {};
					addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
					addressInfo.buffer = finalBuffer.buffer;
					gpuAddress = vkGetBufferDeviceAddress(gpu, &addressInfo);

					auto& [stagingBuffer, mappedMem] = s_AnimBuffersStaging[i];

					stagingBuffer = VkUtils::CreateBuffer(gpu, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
					vkCheck(vkMapMemory(gpu, stagingBuffer.memory, 0, bufferSize, 0, &mappedMem));
				}
			}
		}

		g_LoadingState.currentlyLoaded += loadedAssetsCount;
	}

	UpdateAnimations();

	// light pos
	if(s_LightPosUpdate)
		s_UniBuffLighting.sunPos.y = (float)sin(g_Time) * 10.0f;

	// camera rotation
	ImVec2 mousePos = ImGui::GetMousePos();

	float lookX = 0.0f;
	float lookY = 0.0f;
	
	if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		lookX = mousePos.x - s_MousePos.x;
		lookY = mousePos.y - s_MousePos.y;
	}

	s_CamRot.y += lookX * s_MouseSens;
	s_CamRot.x += lookY * s_MouseSens;
	
	s_MousePos = mousePos;

	// camera location
	float camSpeed = 5.0f;

	glm::vec3 inputMovement = { 0.0f, 0.0f, 0.0f };

	if (ImGui::IsKeyDown(ImGuiKey_W))
		inputMovement.z += 1.0f;
	if (ImGui::IsKeyDown(ImGuiKey_S))
		inputMovement.z += -1.0f;
	if (ImGui::IsKeyDown(ImGuiKey_A))
		inputMovement.x += -1.0f;
	if (ImGui::IsKeyDown(ImGuiKey_D))
		inputMovement.x += 1.0f;
	if (ImGui::IsKeyDown(ImGuiKey_Q))
		inputMovement.y += -1.0f;
	if (ImGui::IsKeyDown(ImGuiKey_E))
		inputMovement.y += 1.0f;

	if (inputMovement.x || inputMovement.y || inputMovement.z)
	{
		glm::vec3 forward = Math::Forward(glm::radians(s_CamRot));
		glm::vec3 right = Math::Right(glm::radians(s_CamRot));
		glm::vec3 up = glm::cross(forward, right);

		glm::vec3 finalMovement = forward * inputMovement.z + right * inputMovement.x + up * inputMovement.y;
		finalMovement = glm::normalize(finalMovement) * (s_CamSpeed * deltaTime);
		
		s_CamPos = s_CamPos + finalMovement * camSpeed;
	}

	if (ImGui::IsKeyDown(ImGuiKey_R))
		s_CamPos = glm::vec3(0.0f);
}

static glm::mat4 ModelMatrix(const Transform& t)
{
	glm::mat4 rotation = glm::rotate(glm::radians(t.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f))
						* glm::rotate(glm::radians(t.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
						* glm::rotate(glm::radians(t.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));

	glm::mat4 translation = glm::translate(t.position);

	glm::mat4 scale = glm::scale(t.scale);

	return translation * rotation * scale;
}

static glm::mat4 ViewMatrix(glm::vec3 camPosition, glm::vec3 camRotationDegrees)
{
	glm::vec3 camRotRadians = glm::radians(camRotationDegrees);

	glm::vec3 camForward = Math::Forward(camRotRadians);
	glm::vec3 camRight = Math::Right(camRotRadians);
	glm::vec3 camUp = glm::cross(camForward, camRight);
	
	return glm::lookAtLH(camPosition, camForward + camPosition, camUp);
}

static glm::mat4 PerspectiveMatrix(float fovDegrees, float width, float height, float nearZ, float farZ)
{
	return glm::perspectiveFovLH_ZO(glm::radians(fovDegrees), width, height, nearZ, farZ);
}

void NewFrame()
{
	VkDevice device = g_RendererContext.GetDevice();

	//LOG_WARN("New frame!");
	if (s_PipelinesAreDirty)
	{
		system("if 1==1 \"shaders/compile.bat\""); // a quanto pare aspetta finche non ha finito, gg

		vkDeviceWaitIdle(device);
		
		vkDestroyPipeline(device, g_GfxPipelineDeferred_GBuffer.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineDeferred_Compose.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineForward.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineForward_Simple.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineForward_Shadows.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineForward_Wireframe.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineForward_Sk.pipeline, nullptr);
		vkDestroyPipeline(device, g_GfxPipelineForward_Shadows_Sk.pipeline, nullptr);
		
		CreatePipeline();

		s_PipelinesAreDirty = false;
	}

	FrameData& frameData = g_FramesData[g_FrameIndex];

	// wait until the gpu has finished rendering the last frame. Timeout of 1 second
	// se il timeout e' 0 restituisce lo stato corrente della fence
	vkCheck(vkWaitForFences(device, 1, &frameData.fence, true, 1000000000 /* ns */)); // asepettaq che diventi signaled (nel primo frame si bloccerebbe senza il VK_FENCE_CREATE_SIGNALED_BIT nella craezione)
	vkCheck(vkResetFences(device, 1, &frameData.fence)); // settala di nuovo unsignaled

	uint32_t imageIndex;
	vkCheck(vkAcquireNextImageKHR(device, g_Swapchain, 1000000000, frameData.swapchainSemaphore, nullptr, &imageIndex));

	SwapchainImage swapchainImage = {
		.image = g_SwapchainImages[imageIndex],
		.imageView = g_SwapchainImageViews[imageIndex],
		.presentSemaphore = g_SwapchainSemaphores[imageIndex]
	};

	if (s_BoundTexture->IsLoaded())
		VkUtils::UpdateDescBinding(device, frameData.descriptorForward, s_BoundTexture->GetImage().view, g_TextureSamplerBasic, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0);

	VkCommandBuffer cmd = frameData.commandBuffer;
	vkCheck(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
	cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufferBeginInfo.pNext = nullptr;
	cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkCheck(vkBeginCommandBuffer(cmd, &cmdBufferBeginInfo));

	if (s_Deferred)
	{
#if 0
		VkUtils::TransitionImageHandle images[] = {
			g_GBuffer_Albedo.image, g_GBuffer_Normals.image, g_GBuffer_Entity.image, g_GBuffer_Positions.image 
		};

		// Pipeline gbuffers
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_GBuffer.pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_GBuffer.layout, 0, 1, &frameData.descriptorGBuffer, 0, nullptr);

			VkUtils::TransitionImages(cmd, VkUtils::ImageLayout::Undefined, VkUtils::ImageLayout::Clear, images);
			VkUtils::TransitionImages(cmd, VkUtils::ImageLayout::Clear, VkUtils::ImageLayout::RenderTarget, images);

			// render
			VkClearValue clear;
			clear.color.float32[0] = 0.2f;
			clear.color.float32[1] = 0.4f;
			clear.color.float32[2] = 0.6f;
			clear.color.float32[3] = 1.0f;

			VkRenderingAttachmentInfo colorAttachementsInfo[] = {
				VkUtils::AttachmentInfo(g_GBuffer_Albedo.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
				VkUtils::AttachmentInfo(g_GBuffer_Normals.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
				VkUtils::AttachmentInfo(g_GBuffer_Entity.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
				VkUtils::AttachmentInfo(g_GBuffer_Positions.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			};

			VkRenderingAttachmentInfo depthAttachmentInfo = VkUtils::AttachmentInfoDepth(g_GBuffer_Depth.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

			VkRenderingInfo renderInfo = {};
			renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, g_SwapchainExtent };
			renderInfo.layerCount = 1;
			renderInfo.colorAttachmentCount = (u32)std::size(colorAttachementsInfo);
			renderInfo.pColorAttachments = colorAttachementsInfo;
			renderInfo.pDepthAttachment = &depthAttachmentInfo;
			renderInfo.pStencilAttachment = nullptr;
			vkCmdBeginRendering(cmd, &renderInfo);

			if (s_BoundTexture->IsLoaded())
			{
				VkUtils::UpdateDescBinding(device, frameData.descriptorGBuffer, s_BoundTexture->GetImage().view, g_TextureSamplerBasic, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0);

				for (const SceneEntity& entity : g_Entities)
				{
					if (!entity.mesh->IsLoaded() || !entity.visible)
						continue;

					glm::mat4 modelRotation = glm::rotate(glm::radians(entity.transform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f))
						* glm::rotate(glm::radians(entity.transform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
						* glm::rotate(glm::radians(entity.transform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));

					glm::mat4 model = glm::translate(entity.transform.position) * modelRotation * glm::scale(entity.transform.scale);

					MeshPushConstant meshPushConst;
					meshPushConst.worldMatrix = proj * view * model;
					meshPushConst.modelMatrix = model;
					meshPushConst.vertexBuffer = entity.mesh->GetVertexBufferAddress();
					vkCmdPushConstants(cmd, g_GfxPipelineDeferred_GBuffer.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant), &meshPushConst);

					vkCmdBindIndexBuffer(cmd, entity.mesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
					for (const Submesh& submesh : entity.mesh->GetSubmeshes())
						vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
				}
			}

			vkCmdEndRendering(cmd);
		}

		// Pipeline final composite
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_Compose.pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_Compose.layout, 0, 1, &frameData.descriptorCompose, 0, nullptr);

			s_UniBuffLighting.viewPos = glm::vec4(s_CamPos, 1.0f);
			vkCmdUpdateBuffer(cmd, frameData.uniformBufferLighting.buffer, 0, sizeof(UniBuffLighting), &s_UniBuffLighting);

			// Draw
			VkUtils::TransitionImages(cmd, VkUtils::ImageLayout::RenderTarget, VkUtils::ImageLayout::SampleRead, images);

			VkClearValue clear;
			clear.color.float32[0] = 0.2f;
			clear.color.float32[1] = 0.4f;
			clear.color.float32[2] = 0.6f;
			clear.color.float32[3] = 1.0f;

			VkRenderingAttachmentInfo colorAttachementsInfo[] = {
				VkUtils::AttachmentInfo(g_CompositeFinal.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			};

			VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::Undefined, VkUtils::ImageLayout::Clear, g_CompositeFinal.image);
			VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::Clear, VkUtils::ImageLayout::RenderTarget, g_CompositeFinal.image);

			VkRenderingInfo renderInfo = {};
			renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, g_SwapchainExtent };
			renderInfo.layerCount = 1;
			renderInfo.colorAttachmentCount = (u32)std::size(colorAttachementsInfo);
			renderInfo.pColorAttachments = colorAttachementsInfo;
			renderInfo.pDepthAttachment = nullptr;
			renderInfo.pStencilAttachment = nullptr;
			vkCmdBeginRendering(cmd, &renderInfo);

			vkCmdDraw(cmd, 6, 1, 0, 0); // full screen quad :)

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Simple.pipeline);

			Mesh* mesh = g_Meshes[0];
			if (mesh->IsLoaded())
			{
				glm::mat4 model = glm::translate(glm::vec3(s_UniBuffLighting.sunPos)) * glm::scale(glm::vec3(0.2f));

				MeshPushConstant meshPushConst;
				meshPushConst.worldMatrix = proj * view * model;
				meshPushConst.modelMatrix = model;
				meshPushConst.vertexBuffer = mesh->GetVertexBufferAddress();
				vkCmdPushConstants(cmd, g_GfxPipelineForward_Simple.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant), &meshPushConst);

				vkCmdBindIndexBuffer(cmd, mesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
				for (const Submesh& submesh : mesh->GetSubmeshes())
					vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
			}

			vkCmdEndRendering(cmd);
		}
#endif
	}
	else
	{
		glm::mat4 lightView = ViewMatrix(glm::vec3(s_UniBuffLighting.sunPos), s_LightRotation);
		glm::mat4 lightProj = glm::orthoLH_ZO(-30.0f, 30.0f, -30.0f, 30.0f, .1f, 150.0f);

		lightProj = PerspectiveMatrix(60.0f, (float)shadowMapRes, (float)shadowMapRes, .5f, 150.0f);

		// update lighting uniform buffer
		s_UniBuffLighting.lightView = lightProj * lightView;
		s_UniBuffLighting.textureIndex = s_BoundTexture->IsLoaded() ? s_BoundTexture->GetImageIndex() : (u32)-1;
		s_UniBuffLighting.viewPos = glm::vec4(s_CamPos, 1.0f);
		vkCmdUpdateBuffer(cmd, frameData.uniformBufferLighting.buffer, 0, sizeof(UniBuffLighting), &s_UniBuffLighting);

		// update animation data
		auto& [staging, mapped] = s_AnimBuffersStaging[g_FrameIndex];
		VkUtils::Buffer& targetBuffer = s_AnimBuffers[g_FrameIndex].first;

		SkeletalMesh* skm = g_SkeletalMeshes[0];
		if (skm->IsLoaded())
		{
			u32 bufferSize = sizeof(glm::mat4) * skm->GetBoneCount();
#if 1
			VkBufferMemoryBarrier2 barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
			barrier.size = bufferSize;
			barrier.buffer = targetBuffer.buffer;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			
			VkDependencyInfo depInfo = {};
			depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depInfo.pBufferMemoryBarriers = &barrier;
			depInfo.bufferMemoryBarrierCount = 1;
			vkCmdPipelineBarrier2(cmd, &depInfo);

			VkBufferCopy cpy = {};
			cpy.size = bufferSize;
			vkCmdCopyBuffer(cmd, staging.buffer, targetBuffer.buffer, 1, &cpy);
#else
			ImmediateSubmit([&staging, mapped, &targetBuffer, skm, bufferSize](VkCommandBuffer cmd)
			{
				VkBufferCopy cpy = {};
				cpy.size = bufferSize;

				vkCmdCopyBuffer(cmd, staging.buffer, targetBuffer.buffer, 1, &cpy);
			});
#endif
		}


		// build render list
		using Renderable = std::pair<void*, u32>; // asset, entity index

		std::vector<Renderable> staticMeshes; // todo: decent alloc
		std::vector<Renderable> skeletalMeshes; // todo: decent alloc
		
		staticMeshes.reserve(8); // todo: decent alloc
		skeletalMeshes.reserve(8); // todo: decent alloc

		if (s_BoundTexture->IsLoaded())
		{	
			for (u32 i = 0; i < g_Entities.size(); i++)
			{
				SceneEntity& ent = g_Entities[i];
				if (ent.visible)
				{
					if (ent.mesh && ent.mesh->IsLoaded())
						staticMeshes.emplace_back(ent.mesh, i);

					if (ent.skeletalMesh && ent.skeletalMesh->IsLoaded())
						skeletalMeshes.emplace_back(ent.skeletalMesh, i);
				}
			}
		}

		// shadow map
		{
			VkUtils::SetViewportAndScissor(cmd, (float)shadowMapRes, (float)shadowMapRes);

			VkRenderingAttachmentInfo depthAttachmentInfo = VkUtils::AttachmentInfoDepth(g_ShadowMap.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

			VkUtils::TransitionImageHandle imgDepth = { g_ShadowMap.image, true };
			VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::Undefined, VkUtils::ImageLayout::Clear, imgDepth);
			VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::Clear, VkUtils::ImageLayout::DepthTarget, imgDepth);

			VkRenderingInfo renderInfo = {};
			renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, { shadowMapRes, shadowMapRes } };
			renderInfo.layerCount = 1;
			renderInfo.pDepthAttachment = &depthAttachmentInfo;
			renderInfo.pStencilAttachment = nullptr;
			vkCmdBeginRendering(cmd, &renderInfo);

			if (s_UniBuffLighting.shadowPassEnabled)
			{
				// static meshes
				{
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Shadows.pipeline);

					VkDescriptorSet sets[] = { frameData.descriptorForward, g_BindlessDS };
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Shadows.layout, 0, 2, sets, 0, nullptr);

					for (Renderable& renderable : staticMeshes)
					{
						SceneEntity& ent = g_Entities[renderable.second];
						Mesh* mesh = (Mesh*)renderable.first;

						glm::mat4 model = ModelMatrix(ent.transform);

						MeshPushConstant shadowShaderData;
						shadowShaderData.worldMatrix = lightProj * lightView * model;
						shadowShaderData.modelMatrix = glm::mat4(1.0f); // dummy
						shadowShaderData.vertexBuffer = mesh->GetVertexBufferAddress();
						vkCmdPushConstants(cmd, g_GfxPipelineForward_Shadows.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant), &shadowShaderData);

						vkCmdBindIndexBuffer(cmd, mesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
						for (const Submesh& submesh : mesh->GetSubmeshes())
							vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
					}
				}
				// skeletal meshes
				{
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Shadows_Sk.pipeline);

					VkDescriptorSet sets[] = { frameData.descriptorForward, g_BindlessDS };
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Shadows_Sk.layout, 0, 2, sets, 0, nullptr);

					for (Renderable& renderable : skeletalMeshes)
					{
						SceneEntity& ent = g_Entities[renderable.second];
						SkeletalMesh* skMesh = (SkeletalMesh*)renderable.first;

						glm::mat4 model = ModelMatrix(ent.transform);

						SkMeshPushConstant meshPushConst;
						meshPushConst.worldMatrix = lightProj * lightView * model;
						meshPushConst.modelMatrix = glm::mat4(1.0f); // dummy
						meshPushConst.vertexBuffer = skMesh->GetVertexBufferAddress();
						meshPushConst.animBuffer = s_AnimBuffers[g_FrameIndex].second;
						vkCmdPushConstants(cmd, g_GfxPipelineForward_Shadows_Sk.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SkMeshPushConstant), &meshPushConst);

						vkCmdBindIndexBuffer(cmd, skMesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
						for (const Submesh& submesh : skMesh->GetSubmeshes())
							vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
					}
				}
			}

			vkCmdEndRendering(cmd);
		}

		// normal scene
		{
			// setup viewport
			VkUtils::SetViewportAndScissor(cmd, (float)g_SwapchainExtent.width, (float)g_SwapchainExtent.height);

			// image transitions
			VkUtils::TransitionImageHandle images[] = { { g_CompositeFinal.image, false }, { g_GBuffer_Depth.image, true} };
			VkUtils::TransitionImages(cmd, VkUtils::Undefined, VkUtils::Clear, images);

			VkUtils::TransitionImage(cmd, VkUtils::Clear, VkUtils::RenderTarget, images[0]);
			VkUtils::TransitionImage(cmd, VkUtils::Clear, VkUtils::DepthTarget, images[1]);

			VkUtils::TransitionImage(cmd, VkUtils::DepthTarget, VkUtils::SampleRead, { g_ShadowMap.image, true });

			// render
			VkClearValue clear;
			clear.color.float32[0] = 0.2f;
			clear.color.float32[1] = 0.4f;
			clear.color.float32[2] = 0.6f;
			clear.color.float32[3] = 1.0f;

			VkRenderingAttachmentInfo colorAttachementsInfo[] = {
				VkUtils::AttachmentInfo(g_CompositeFinal.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			};

			VkRenderingAttachmentInfo depthAttachmentInfo = VkUtils::AttachmentInfoDepth(g_GBuffer_Depth.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

			VkRenderingInfo renderInfo = {};
			renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, g_SwapchainExtent };
			renderInfo.layerCount = 1;
			renderInfo.colorAttachmentCount = (u32)std::size(colorAttachementsInfo);
			renderInfo.pColorAttachments = colorAttachementsInfo;
			renderInfo.pDepthAttachment = &depthAttachmentInfo;
			renderInfo.pStencilAttachment = nullptr;
			vkCmdBeginRendering(cmd, &renderInfo);

			glm::mat4 camView = ViewMatrix(s_CamPos, s_CamRot);
			glm::mat4 projection = PerspectiveMatrix(s_CamFOV, (float)g_SwapchainExtent.width, (float)g_SwapchainExtent.height, 0.1f, 1000.0f);

			// Static meshes
			{
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_Wireframe ? g_GfxPipelineForward_Wireframe.pipeline : g_GfxPipelineForward.pipeline);

				VkDescriptorSet sets[] = { frameData.descriptorForward, g_BindlessDS };
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward.layout, 0, 2, sets, 0, nullptr);

				for (Renderable& renderable : staticMeshes)
				{
					SceneEntity& ent = g_Entities[renderable.second];
					Mesh* mesh = (Mesh*)renderable.first;

					glm::mat4 model = ModelMatrix(ent.transform);

					MeshPushConstant meshPushConst;
					meshPushConst.worldMatrix = projection * camView * model;
					meshPushConst.modelMatrix = model;
					meshPushConst.vertexBuffer = mesh->GetVertexBufferAddress();
					vkCmdPushConstants(cmd, g_GfxPipelineForward.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant), &meshPushConst);

					vkCmdBindIndexBuffer(cmd, mesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
					for (const Submesh& submesh : mesh->GetSubmeshes())
						vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
				}
			
			}
			// Skeletal meshes
			{
#if 1
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Sk.pipeline);

				VkDescriptorSet sets[] = { frameData.descriptorForward, g_BindlessDS };
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Sk.layout, 0, 2, sets, 0, nullptr);

				for (Renderable& renderable : skeletalMeshes)
				{
					SceneEntity& ent = g_Entities[renderable.second];
					SkeletalMesh* skMesh = (SkeletalMesh*)renderable.first;

					glm::mat4 model = ModelMatrix(ent.transform);

					SkMeshPushConstant meshPushConst;
					meshPushConst.worldMatrix = projection * camView * model;
					meshPushConst.modelMatrix = model;
					meshPushConst.vertexBuffer = skMesh->GetVertexBufferAddress();
					meshPushConst.animBuffer = s_AnimBuffers[g_FrameIndex].second;
					vkCmdPushConstants(cmd, g_GfxPipelineForward_Sk.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SkMeshPushConstant), &meshPushConst);

					vkCmdBindIndexBuffer(cmd, skMesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
					for (const Submesh& submesh : skMesh->GetSubmeshes())
						vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
				}
#endif
			}
			// Debug meshes
			{
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Simple.pipeline);

				VkDescriptorSet sets[] = { frameData.descriptorForward };
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineForward_Simple.layout, 0, 1, sets, 0, nullptr);

				Mesh* debugLightMesh = g_Meshes[0];
				if (debugLightMesh->IsLoaded())
				{
					glm::mat4 model = glm::translate(glm::vec3(s_UniBuffLighting.sunPos)) * glm::scale(glm::vec3(0.2f));

					MeshPushConstant meshPushConst;
					meshPushConst.worldMatrix = projection * camView * model;
					meshPushConst.modelMatrix = model;
					meshPushConst.vertexBuffer = debugLightMesh->GetVertexBufferAddress();
					vkCmdPushConstants(cmd, g_GfxPipelineForward_Simple.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant), &meshPushConst);

					vkCmdBindIndexBuffer(cmd, debugLightMesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
					for (const Submesh& submesh : debugLightMesh->GetSubmeshes())
						vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
				}
			}

			vkCmdEndRendering(cmd);
		}
	}

	// prepare for copying to swapchain
	VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::RenderTarget, VkUtils::ImageLayout::TransferSrc, g_CompositeFinal.image);

	// todo: can vkAcquireNextImageKHR be moved down here???
	VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::Undefined, VkUtils::ImageLayout::TransferDst, swapchainImage.image);
	// copy to swapchain
	VkUtils::CopyImage(cmd, swapchainImage.image, g_CompositeFinal.image, g_SwapchainExtent, g_SwapchainExtent);

	// imgui draw on swapchain
	{
		VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::TransferDst, VkUtils::ImageLayout::RenderTarget, swapchainImage.image);

		// imgui draw... potrebbe essere ovunuque
		ImGuii();
		ImGui::Render();

		VkRenderingAttachmentInfo swapchainColorAttachment = VkUtils::AttachmentInfo(swapchainImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		VkRenderingInfo swapchainRenderInfo = VkUtils::RenderingInfo(g_SwapchainExtent, &swapchainColorAttachment, nullptr);

		vkCmdBeginRendering(cmd, &swapchainRenderInfo);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
		vkCmdEndRendering(cmd);
	}

	// preapre for present
	VkUtils::TransitionImage(cmd, VkUtils::ImageLayout::RenderTarget, VkUtils::ImageLayout::Present, swapchainImage.image);

	vkCheck(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = VkUtils::CommandBufferSubmitInfo(cmd);

	VkSemaphoreSubmitInfo waitInfo = VkUtils::SemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, frameData.swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = VkUtils::SemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, swapchainImage.presentSemaphore);

	// launch cmd on graphics queue
	VkQueue gfxQueue = g_RendererContext.GetRendererDevice().GetGraphicsQueue();
	VkSubmitInfo2 submit = VkUtils::SubmitInfo(&cmdInfo, &signalInfo, &waitInfo);
	vkCheck(vkQueueSubmit2(gfxQueue, 1, &submit, frameData.fence));

	// present
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pSwapchains = &g_Swapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &swapchainImage.presentSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &imageIndex;
	vkCheck(vkQueuePresentKHR(gfxQueue, &presentInfo)); 

	g_FrameIndex = (g_FrameIndex + 1) % FRAMES_IN_FLIGHT;
}

int main()
{
	LOG_INFO("Starting!");

	CORE_ASSERT(glfwInit() == GLFW_TRUE, "Unable to init glfw!");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	g_Window = glfwCreateWindow(1800, 1080, "Vulkan!!!", nullptr, nullptr);
	CORE_ASSERT(g_Window, "Unable to spawn window!");
	glfwSetWindowAttrib(g_Window, GLFW_RESIZABLE, 0);

	InitVulkan();
	InitImgui();
	
	g_ResourceFactory.Init(&g_RendererContext);
	g_AssetManager.Init(2);
	LoadGeometry();
		
	while (!glfwWindowShouldClose(g_Window))
	{
		glfwPollEvents();

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		double now = glfwGetTime();
		g_FrameTime = (float)(now - g_Time);
		Update(g_FrameTime);
		g_Time = now;

		NewFrame();
	}

	g_AssetManager.Shutdown();
	g_ResourceFactory.Shutdown();
	ShutdownVulkan();

	glfwDestroyWindow(g_Window);
	glfwTerminate();

	LOG_INFO("Finished!");
}