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

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include "Renderer/PipelineBuilder.h"

#include "Async/TaskPool.h"

#include "Renderer/RendererContext.h"
#include "AssetManager.h"
#include "Renderer/Mesh.h"

#include "Math/Math.h"

struct UniformBuffer
{
	VkUtils::Buffer buffer;
	void* mappedMemory = nullptr;
};

struct FrameData
{
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkSemaphore swapchainSemaphore = VK_NULL_HANDLE; // gpu -> gpu
	VkFence fence = VK_NULL_HANDLE; // cpu -> gpu
	DeletionQueue deletionQueue;
	VkDescriptorSet descriptorGBuffer = VK_NULL_HANDLE;
	VkDescriptorSet descriptorCompose = VK_NULL_HANDLE;
	UniformBuffer uniformBuffer0;
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

VkUtils::Image g_CompositeFinal;

// misc
VkCommandPool g_ImmediateCommandPool = VK_NULL_HANDLE;
VkCommandBuffer g_ImmediateCmdBuffer = VK_NULL_HANDLE;
VkFence g_ImmediateFence = VK_NULL_HANDLE;

VkSampler g_TextureSamplerBasic = VK_NULL_HANDLE;

// descriptors
VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;

VkDescriptorSetLayout g_Gfx_GBuffer_DSLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout g_Gfx_Compose_DSLayout = VK_NULL_HANDLE;

VkDescriptorSetLayout g_ComputeDSLayout = VK_NULL_HANDLE;
VkDescriptorSet g_ComputeDS = VK_NULL_HANDLE;

// compute & graphics pipelines
MyVkPipeline g_ComputePipeline;

MyVkPipeline g_GfxPipelineDeferred_GBuffer;
MyVkPipeline g_GfxPipelineDeferred_Compose;

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
struct MeshPushConstant
{
	glm::mat4 worldMatrix;
	VkDeviceAddress vertexBuffer;
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

struct UniBuffBoh
{
	glm::vec4 color0;
	glm::vec4 color1;
	glm::vec4 color2;
	glm::vec4 color3;
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
		VkPushConstantRange computePushConstant0;
		computePushConstant0.size = sizeof(PushConstant0);
		computePushConstant0.offset = 0;
		computePushConstant0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ComputePipelineBuilder computeBuilder;
		computeBuilder.m_ComputeShader = "shaders/bin/comp.spv";
		computeBuilder.m_Descriptors = { g_ComputeDSLayout };
		computeBuilder.m_PushConstants = { computePushConstant0 };

		//g_ComputePipeline = computeBuilder.Build(device);
	}

	// graphics pipeline gbuffer
	{
		VkPushConstantRange meshPushConst;
		meshPushConst.size = sizeof(MeshPushConstant);
		meshPushConst.offset = 0;
		meshPushConst.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert_gbuffer.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag_gbuffer.spv";
		graphicsBuilder.m_ColorAttachments = { GBUFFER_ALBEDO_FORMAT, GBUFFER_NORMALS_FORMAT, GBUFFER_ENTITY_FORMAT };
		graphicsBuilder.m_ViewportSize = g_SwapchainExtent;
		graphicsBuilder.m_PushConstants = { meshPushConst };
		graphicsBuilder.m_Descriptors = { g_Gfx_GBuffer_DSLayout };
		//graphicsBuilder.m_BlendMode = EGraphicsBlendMode::GFX_BLEND_ADDITIVE;
		/*
			We give it depth write, and as operator GREATER_OR_EQUAL.
			As mentioned, because 0 is far and 1 is near,
			we will want to only render the pixels if the current depth value is greater than the depth value on the depth image.
		*/
		graphicsBuilder.m_DepthMode = { VK_COMPARE_OP_GREATER_OR_EQUAL, GBUFFER_DEPTH_FORMAT, true };

		g_GfxPipelineDeferred_GBuffer = graphicsBuilder.Build(device);
	}

	// graphics pipeline compose
	{
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert_composite.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag_composite.spv";
		graphicsBuilder.m_ColorAttachments = { DRAW_FORMAT };
		graphicsBuilder.m_ViewportSize = g_SwapchainExtent;
		graphicsBuilder.m_Descriptors = { g_Gfx_Compose_DSLayout };

		g_GfxPipelineDeferred_Compose = graphicsBuilder.Build(device);
	}

	// cleanup
	g_RendererContext.QueueShutdownFunc([device]() {
		vkDestroyPipeline(device, g_GfxPipelineDeferred_GBuffer.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineDeferred_GBuffer.layout, nullptr);

		vkDestroyPipeline(device, g_GfxPipelineDeferred_Compose.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_GfxPipelineDeferred_Compose.layout, nullptr);

		vkDestroyPipeline(device, g_ComputePipeline.pipeline, nullptr);
		vkDestroyPipelineLayout(device, g_ComputePipeline.layout, nullptr);
	});
}

//void CreateFramebuffers()
//{
//	// boh... voodoo
//
//	for (int i = 0; i < g_SwapchainImageViews.size(); i++)
//	{
//		VkImageView attachments[1] = {
//			g_SwapchainImageViews[i]
//		};
//
//		VkFramebufferCreateInfo framebufferInfo = {};
//		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
//		framebufferInfo.renderPass = g_RenderPass;
//		framebufferInfo.attachmentCount = 1;
//		framebufferInfo.pAttachments = attachments;
//		framebufferInfo.width = g_SwapchainExtent.width;
//		framebufferInfo.height = g_SwapchainExtent.height;
//		framebufferInfo.layers = 1;
//
//		VkFramebuffer& newFramebufferHandle = g_Framebuffers.emplace_back();
//		VkResult res = vkCreateFramebuffer(g_Device, &framebufferInfo, nullptr, &newFramebufferHandle);
//		vkCheck(res);
//	}
//}

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

	g_RendererContext.QueueShutdownFunc([device]() {
		VkUtils::DestroyImage(device, g_GBuffer_Albedo);
		VkUtils::DestroyImage(device, g_GBuffer_Normals);
		VkUtils::DestroyImage(device, g_GBuffer_Entity);
		VkUtils::DestroyImage(device, g_GBuffer_Depth);
		VkUtils::DestroyImage(device, g_CompositeFinal);
	});
}

void CreateDescriptors()
{
	VkDevice device = g_RendererContext.GetDevice();

	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 12 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = 8;
	poolInfo.poolSizeCount = (u32)std::size(poolSizes);
	poolInfo.pPoolSizes = poolSizes;
	vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &g_DescriptorPool));

	// compute
	{
		VkDescriptorSetLayoutBinding bindings[1];
		bindings[0].binding = 0;
		bindings[0].descriptorCount = 1; // array count
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[0].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.pBindings = bindings;
		layoutInfo.bindingCount = (u32)std::size(bindings);
		vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &g_ComputeDSLayout));

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = g_DescriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &g_ComputeDSLayout;
		vkCheck(vkAllocateDescriptorSets(device, &allocInfo, &g_ComputeDS));
	}

	// gfx
	{
		// GBuffer pipeline
		{
			VkDescriptorSetLayoutBinding bindings[2];

			// texture
			bindings[0].binding = 0;
			bindings[0].descriptorCount = 1; // array count
			bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[0].pImmutableSamplers = nullptr;

			// uniformbuffer0
			bindings[1].binding = 1;
			bindings[1].descriptorCount = 1; // array count
			bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[1].pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.pBindings = bindings;
			layoutInfo.bindingCount = (u32)std::size(bindings);
			vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &g_Gfx_GBuffer_DSLayout));
		}

		// Compose pipeline
		{
			VkDescriptorSetLayoutBinding bindings[3];

			// albedo
			bindings[0].binding = 0;
			bindings[0].descriptorCount = 1; // array count
			bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[0].pImmutableSamplers = nullptr;

			// normals
			bindings[1].binding = 1;
			bindings[1].descriptorCount = 1; // array count
			bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[1].pImmutableSamplers = nullptr;

			// entity id
			bindings[2].binding = 2;
			bindings[2].descriptorCount = 1; // array count
			bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			bindings[2].pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.pBindings = bindings;
			layoutInfo.bindingCount = (u32)std::size(bindings);
			vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &g_Gfx_Compose_DSLayout));
		}

		constexpr u32 kDescriptorsCount = FRAMES_IN_FLIGHT + FRAMES_IN_FLIGHT; // gbuffer pipeline, compose pipeline

		VkDescriptorSet gfxSets[kDescriptorsCount];
		VkDescriptorSetLayout gfxLayouts[kDescriptorsCount] = { g_Gfx_GBuffer_DSLayout, g_Gfx_GBuffer_DSLayout, g_Gfx_Compose_DSLayout, g_Gfx_Compose_DSLayout };

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = g_DescriptorPool;
		allocInfo.descriptorSetCount = kDescriptorsCount;
		allocInfo.pSetLayouts = gfxLayouts;
		vkAllocateDescriptorSets(device, &allocInfo, gfxSets);

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++)
		{
			// uniform buffer
			UniformBuffer& uniform0 = g_FramesData[i].uniformBuffer0;
			
			uniform0.buffer = VkUtils::CreateBuffer(device, sizeof(UniBuffBoh), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT /* :( pero se la cambiamo ogni frame... */ | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkMapMemory(device, uniform0.buffer.memory, 0, sizeof(UniBuffBoh), 0, &uniform0.mappedMemory);

			// descriptors
			g_FramesData[i].descriptorGBuffer = gfxSets[i];
			g_FramesData[i].descriptorCompose = gfxSets[i + 2];
		}
	}

	g_RendererContext.QueueShutdownFunc([device]() {
		vkDestroyDescriptorPool(device, g_DescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(device, g_ComputeDSLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, g_Gfx_GBuffer_DSLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, g_Gfx_Compose_DSLayout, nullptr);

		for (FrameData& frameData : g_FramesData)
			VkUtils::DestroyBuffer(device, frameData.uniformBuffer0.buffer);
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
static std::vector<Texture*> g_Textures;

static glm::vec3 s_CamPos = { 3.3f, 1.3f, -13.0f };
static glm::vec3 s_CamRot = { 0.0f, 0.0f, 0.0f };
static float s_CamSpeed = 10.0f;
static float s_CamFOV = 60.0f;
static ImVec2 s_MousePos = {};
static float s_MouseSens = 0.1f;

static UniBuffBoh s_ShaderUniBuff = {};
static Texture* s_BoundTexture = nullptr;

void LoadGeometry()
{
	g_MeshTransform.rotation = { -90.0f, 200.0f, 0.0f };
	g_MeshTransform.position = { 0.0f, -3.0f, 8.0f };

	std::filesystem::path meshesToLoad[] = {
		//std::filesystem::path("assets") / "car.glb"
		std::filesystem::path("assets") / "diorama.glb"
		//std::filesystem::path("assets") / "chisa_wuthering_waves.glb"
	};

	std::filesystem::path texturesToLoad[] = {
		std::filesystem::path("assets") / "doom.jpg",
		std::filesystem::path("assets") / "textures" / "brickwall.png",
		std::filesystem::path("assets") / "textures" / "grass.png",
	};

	for (const auto& meshPath : meshesToLoad)
	{
		Mesh* mesh = g_AssetManager.LoadMesh(meshPath);
		g_Meshes.push_back(mesh);
	}

	for (const auto& texturePath : texturesToLoad)
	{
		Texture* texture = g_AssetManager.LoadTexture(texturePath);
		g_Textures.push_back(texture);
	}

	g_LoadingState.loadTarget = g_Meshes.size() + g_Textures.size();

	s_BoundTexture = g_Textures[0]; // doom
	s_ShaderUniBuff.color0 = glm::vec4(1.0f);

	g_RendererContext.QueueShutdownFunc([]() {
		for (auto mesh : g_Meshes)
		{
			VkUtils::DestroyBuffer(g_RendererContext.GetDevice(), mesh->GetVertexBuffer());
			VkUtils::DestroyBuffer(g_RendererContext.GetDevice(), mesh->GetIndexBuffer());
			delete mesh;
		}
		for (auto texture : g_Textures)
		{
			VkUtils::DestroyImage(g_RendererContext.GetDevice(), texture->GetImage());
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

	CreateDescriptors();

	//CreateRenderPass(); dynamic_rendering yeee
	CreatePipeline();
	//CreateFramebuffers(); dynamic_rendering yeee
	CreateTextureSamplers();
}

void ShutdownVulkan()
{
	vkCheck(vkDeviceWaitIdle(g_RendererContext.GetDevice()));
	g_RendererContext.Shutdown();
}

void ImGuii()
{
	ImGui::Begin("Roba");

	ImVec4 lblColor = g_LoadingState.currentlyLoaded == g_LoadingState.loadTarget ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	ImGui::TextColored(lblColor, "Scene loading: %d/%d", g_LoadingState.currentlyLoaded, g_LoadingState.loadTarget);
	
	if (ImGui::Button("Load scene async"))
	{
		std::filesystem::path meshPath = std::filesystem::path("assets") / "car.glb";
		for (u32 i = 0; i < 16; i++)
		{
			Mesh* meshRes = g_AssetManager.LoadMesh(meshPath);
			g_Meshes.push_back(meshRes);
		}

		g_LoadingState.loadTarget += 16;
	}

	ImGui::Separator();

	if (ImGui::BeginCombo("Texture", s_BoundTexture ? s_BoundTexture->DebugName.c_str() : "Nulla :("))
	{
		if (ImGui::Selectable("Nulla :(", s_BoundTexture == nullptr))
			s_BoundTexture = nullptr;

		for (Texture* texture : g_Textures)
		{
			if (ImGui::Selectable(texture->DebugName.c_str(), texture == s_BoundTexture))
				s_BoundTexture = texture;
		}

		ImGui::EndCombo();
	}

	ImGui::Separator();

	static float stableFrametime = g_FrameTime;
	static float lastSample = g_Time;

	if (g_Time - lastSample > 0.05f)
	{
		stableFrametime = g_FrameTime;
		lastSample = g_Time;
	}

	ImGui::Text("Frametime: %.2fms (%.0f FPS)", stableFrametime * 1000.0f, 1.0f / stableFrametime);

	ImGui::Separator();

	ImGui::ColorEdit4("Uniform buffer 'color0'", glm::value_ptr(s_ShaderUniBuff.color0));

	ImGui::Separator();

	ImGui::DragFloat3("Cam Location", glm::value_ptr(s_CamPos), 0.005f);
	ImGui::DragFloat3("Cam Rotation", glm::value_ptr(s_CamRot), 0.1f);
	ImGui::DragFloat("Cam FOV", &s_CamFOV, 0.001f);
	ImGui::DragFloat("Cam Speed", &s_CamSpeed, 0.1f);

	ImGui::Separator();

	ImGui::DragFloat3("Model Location", glm::value_ptr(g_MeshTransform.position), 0.005f);
	ImGui::DragFloat3("Model Rotation", glm::value_ptr(g_MeshTransform.rotation), 0.1f);
	ImGui::DragFloat3("Model Scale", glm::value_ptr(g_MeshTransform.scale), 0.005f);

	ImGui::End();
}

void Update(float deltaTime)
{
	// asset straming
	if (g_LoadingState.currentlyLoaded < g_LoadingState.loadTarget)
	{
		u32 loadedAssets = g_AssetManager.CheckLoadedAssets();
		g_LoadingState.currentlyLoaded += loadedAssets;
	}

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
		
		s_CamPos = s_CamPos + finalMovement;
	}

	if (ImGui::IsKeyDown(ImGuiKey_R))
		s_CamPos = glm::vec3(0.0f);
}

void NewFrame()
{
	//LOG_WARN("New frame!");
	VkDevice device = g_RendererContext.GetDevice();

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

	VkCommandBuffer cmd = frameData.commandBuffer;
	vkCheck(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
	cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufferBeginInfo.pNext = nullptr;
	cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkCheck(vkBeginCommandBuffer(cmd, &cmdBufferBeginInfo));

	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = (float)g_SwapchainExtent.height;
	viewport.width = (float)g_SwapchainExtent.width;
	viewport.height = -(float)g_SwapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = g_SwapchainExtent.width;
	scissor.extent.height = g_SwapchainExtent.height;
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Pipeline gbuffers
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_GBuffer.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_GBuffer.layout, 0, 1, &frameData.descriptorGBuffer, 0, nullptr);

		// Barriers (clear & attach)
		{
			VkImageSubresourceRange subimageRange = VkUtils::ImageRange(VK_IMAGE_ASPECT_COLOR_BIT);

			VkImageMemoryBarrier2 barriers[3];

			VkImageMemoryBarrier2 clearBarrier = {};
			clearBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			clearBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			clearBarrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			clearBarrier.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
			clearBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
			clearBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			clearBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			clearBarrier.subresourceRange = subimageRange;

			VkImageMemoryBarrier2 renderBarrier = {};
			renderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			renderBarrier.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
			renderBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
			renderBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			renderBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			renderBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			renderBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			renderBarrier.subresourceRange = subimageRange;

			VkDependencyInfo depInfo = {};
			depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depInfo.imageMemoryBarrierCount = 3;
			depInfo.pImageMemoryBarriers = barriers;

			barriers[0] = clearBarrier;
			barriers[0].image = g_GBuffer_Albedo.image;
			barriers[1] = clearBarrier;
			barriers[1].image = g_GBuffer_Normals.image;
			barriers[2] = clearBarrier;
			barriers[2].image = g_GBuffer_Entity.image;
			vkCmdPipelineBarrier2(cmd, &depInfo);

			barriers[0] = renderBarrier;
			barriers[0].image = g_GBuffer_Albedo.image;
			barriers[1] = renderBarrier;
			barriers[1].image = g_GBuffer_Normals.image;
			barriers[2] = renderBarrier;
			barriers[2].image = g_GBuffer_Entity.image;
			vkCmdPipelineBarrier2(cmd, &depInfo);
		}

		// render
		{
			glm::vec3 camRotRadians = glm::radians(s_CamRot);

			glm::vec3 camForward = Math::Forward(camRotRadians);
			glm::vec3 camRight = Math::Right(camRotRadians);
			glm::vec3 camUp = glm::cross(camForward, camRight);

			glm::mat4 view = glm::lookAtLH(s_CamPos, camForward + s_CamPos, camUp);
			view = view * glm::rotate(camRotRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));

			glm::mat4 proj = glm::perspectiveFovLH_ZO(glm::radians(s_CamFOV), (float)g_SwapchainExtent.width, (float)g_SwapchainExtent.height, 10000.f, 0.1f);

			VkClearValue clear;
			clear.color.float32[0] = 0.2f;
			clear.color.float32[1] = 0.4f;
			clear.color.float32[2] = 0.6f;
			clear.color.float32[3] = 1.0f;

			VkRenderingAttachmentInfo colorAttachementsInfo[] = {
				VkUtils::AttachmentInfo(g_GBuffer_Albedo.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
				VkUtils::AttachmentInfo(g_GBuffer_Normals.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
				VkUtils::AttachmentInfo(g_GBuffer_Entity.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
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

			Mesh* mesh = g_Meshes[0];
			if (mesh->IsLoaded() && s_BoundTexture && s_BoundTexture->IsLoaded())
			{
				// push constants
				glm::mat4 modelRotation = glm::rotate(glm::radians(g_MeshTransform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f))
					* glm::rotate(glm::radians(g_MeshTransform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
					* glm::rotate(glm::radians(g_MeshTransform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));

				glm::mat4 model = glm::translate(g_MeshTransform.position) * modelRotation * glm::scale(g_MeshTransform.scale);

				MeshPushConstant meshPushConst;
				meshPushConst.worldMatrix = proj * view * model;
				meshPushConst.vertexBuffer = mesh->GetVertexBufferAddress();
				vkCmdPushConstants(cmd, g_GfxPipelineDeferred_GBuffer.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant), &meshPushConst);

				// descriptors :(
				VkDescriptorImageInfo descriptorTexture;
				descriptorTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				descriptorTexture.sampler = g_TextureSamplerBasic;
				descriptorTexture.imageView = s_BoundTexture->GetImage().view;

				VkWriteDescriptorSet descriptorWrite = {};
				descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrite.dstSet = frameData.descriptorGBuffer;
				descriptorWrite.dstArrayElement = 0;
				descriptorWrite.dstBinding = 0;
				descriptorWrite.pImageInfo = &descriptorTexture;
				descriptorWrite.descriptorCount = 1;
				descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

				// draw calls
				vkCmdBindIndexBuffer(cmd, mesh->GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
				for (const Submesh& submesh : mesh->GetSubmeshes())
					vkCmdDrawIndexed(cmd, submesh.indexCount, 1, submesh.indexOffset, 0, 0);
			}

			vkCmdEndRendering(cmd);
		}
	}

	// Pipeline final composite
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_Compose.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GfxPipelineDeferred_Compose.layout, 0, 1, &frameData.descriptorCompose, 0, nullptr);

		// Barriers
		{
			VkImageSubresourceRange subimageRange = VkUtils::ImageRange(VK_IMAGE_ASPECT_COLOR_BIT);
			
			// attachments
			{
				VkImageMemoryBarrier2 barriers[3];

				VkImageMemoryBarrier2 renderBarrier = {};
				renderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				renderBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				renderBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
				renderBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
				renderBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
				renderBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				renderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				renderBarrier.subresourceRange = subimageRange;

				barriers[0] = renderBarrier;
				barriers[0].image = g_GBuffer_Albedo.image;
				barriers[1] = renderBarrier;
				barriers[1].image = g_GBuffer_Normals.image;
				barriers[2] = renderBarrier;
				barriers[2].image = g_GBuffer_Entity.image;

				VkDependencyInfo depInfoRender = {};
				depInfoRender.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				depInfoRender.imageMemoryBarrierCount = 3;
				depInfoRender.pImageMemoryBarriers = barriers;
				vkCmdPipelineBarrier2(cmd, &depInfoRender);
			}

			// final image
			{
				VkImageMemoryBarrier2 barriers[2] = {};

				// clear barrier
				barriers[0] = {};
				barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
				barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
				barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
				barriers[0].dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
				barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
				barriers[0].subresourceRange = subimageRange;
				barriers[0].image = g_CompositeFinal.image;

				// render barrier
				barriers[1] = {};
				barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
				barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				barriers[1].srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
				barriers[1].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
				barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
				barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				barriers[1].subresourceRange = subimageRange;
				barriers[1].image = g_CompositeFinal.image;

				VkDependencyInfo depInfo = {};
				depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				depInfo.imageMemoryBarrierCount = 2;
				depInfo.pImageMemoryBarriers = barriers;
				vkCmdPipelineBarrier2(cmd, &depInfo);
			}
		}

		// Draw
		{
			VkDescriptorImageInfo descriptorTextures[3];

			descriptorTextures[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			descriptorTextures[0].sampler = g_TextureSamplerBasic;
			descriptorTextures[0].imageView = g_GBuffer_Albedo.view;

			descriptorTextures[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			descriptorTextures[1].sampler = g_TextureSamplerBasic;
			descriptorTextures[1].imageView = g_GBuffer_Normals.view;

			descriptorTextures[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			descriptorTextures[2].sampler = g_TextureSamplerBasic;
			descriptorTextures[2].imageView = g_GBuffer_Entity.view;

			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = frameData.descriptorCompose;
			descriptorWrite.dstArrayElement = 0;
			descriptorWrite.dstBinding = 0;
			descriptorWrite.pImageInfo = descriptorTextures;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr); // todo: update normals & entity

			VkClearValue clear;
			clear.color.float32[0] = 0.2f;
			clear.color.float32[1] = 0.4f;
			clear.color.float32[2] = 0.6f;
			clear.color.float32[3] = 1.0f;

			VkRenderingAttachmentInfo colorAttachementsInfo[] = {
				VkUtils::AttachmentInfo(g_CompositeFinal.view, &clear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			};

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

			vkCmdEndRendering(cmd);
		}
	}

	// todo: fix barriers, VkUtils::TransitionImage sucks
	// prepare for copying to swapchain
	VkUtils::TransitionImage(cmd, g_CompositeFinal.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// todo: can vkAcquireNextImageKHR be moved down here???
	VkUtils::TransitionImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	// copy to swapchain
	VkUtils::CopyImage(cmd, swapchainImage.image, g_CompositeFinal.image, g_SwapchainExtent, g_SwapchainExtent);

	// imgui draw on swapchain
	{
		VkUtils::TransitionImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

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
	VkUtils::TransitionImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

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
	g_Window = glfwCreateWindow(1400, 900, "Vulkan!!!", nullptr, nullptr);
	CORE_ASSERT(g_Window, "Unable to spawn window!");

	InitVulkan();
	InitImgui();
	
	g_ResourceFactory.Init(&g_RendererContext);
	g_AssetManager.Init(4);
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