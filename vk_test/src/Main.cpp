#include "Core.h"
#include "Utils.h"

#define NOMINMAX

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include "glfw/glfw3.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include "glfw/glfw3native.h"

#include "VkUtils.h"

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"

#include "PipelineBuilder.h"
#include "Resource.h"

class DeletionQueue
{
public:
	DeletionQueue() = default;

	~DeletionQueue()
	{
		Flush();
	}

	void PushBack(std::function<void()>&& function)
	{
		m_DeletionQueue.push_back(function);
	}

	void Flush()
	{
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = m_DeletionQueue.rbegin(); it != m_DeletionQueue.rend(); it++)
			(*it)(); //call functors

		m_DeletionQueue.clear();
	}

private:
	std::deque<std::function<void()>> m_DeletionQueue;
};

struct FrameData
{
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkSemaphore swapchainSemaphore = VK_NULL_HANDLE; // gpu -> gpu
	VkFence fence = VK_NULL_HANDLE; // cpu -> gpu
	DeletionQueue deletionQueue;
};

struct SwapchainImage
{
	VkImage image = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;
	VkSemaphore presentSemaphore = VK_NULL_HANDLE; // finish queue -> present
};

// window
GLFWwindow* g_Window = nullptr;

// vulkan cleanup queue
DeletionQueue g_MainDeletionQueue;

// vukan instance & device
VkInstance g_vkInstance = VK_NULL_HANDLE;
VkSurfaceKHR g_vkSurface = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT g_vkDebugMessenger = VK_NULL_HANDLE;
VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
VkDevice g_Device = VK_NULL_HANDLE;

// queues 
uint32_t g_GraphicsQueueFamilyIndex = 0;
std::vector<VkQueue> g_Queues;
uint32_t g_GraphicsQueueIndex = 0;

// fifs
constexpr int FRAMES_IN_FLIGHT = 2;
int g_FrameIndex = 0;
FrameData g_FramesData[FRAMES_IN_FLIGHT];

// stuff
constexpr VkFormat DRAW_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT; // migliore della swapchain, per disegnare con precisione...
constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

// swapchain
VkSwapchainKHR g_Swapchain = VK_NULL_HANDLE;
VkSurfaceFormatKHR g_SwapchainSurfaceFormat = {};
VkExtent2D g_SwapchainExtent = {};
VkPresentModeKHR g_SwapchainPresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
std::vector<VkImage> g_SwapchainImages;
std::vector<VkImageView> g_SwapchainImageViews;
std::vector<VkSemaphore> g_SwapchainSemaphores;

// scratch image
VkUtils::Image g_DrawImage = {};
VkUtils::Image g_DrawImageDepth = {};

// misc
VkCommandPool g_ImmediateCommandPool = VK_NULL_HANDLE;
VkCommandBuffer g_ImmediateCmdBuffer = VK_NULL_HANDLE;
VkFence g_ImmediateFence = VK_NULL_HANDLE;

// descriptors
VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;

VkDescriptorSetLayout g_DescriptorSetLayout0 = VK_NULL_HANDLE;
VkDescriptorSet g_DescriptorSet0 = VK_NULL_HANDLE;

// compute & graphics pipelines
MyVkPipeline g_ComputePipeline;
MyVkPipeline g_GraphicsPipeline0;
MyVkPipeline g_GraphicsPipeline1;

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

std::vector<MeshResource> g_Meshes;

struct Transform
{
	glm::vec3 position = { 0.0f, 0.0f, 0.0f };
	glm::vec3 rotation = { 0.0f, 0.0f, 0.0f };
	glm::vec3 scale = { 1.0f, 1.0f, 1.0f };
};

Transform g_MeshTransform;

static VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugErrorCallback(
VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, 
	void* pUserData) 
{
#if _WIN32
	static HANDLE stdOutputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	static constexpr int DEFAULT_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	
	int textColor = DEFAULT_TEXT_COLOR;

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		textColor = FOREGROUND_RED;
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		textColor = FOREGROUND_RED | FOREGROUND_GREEN;

	SetConsoleTextAttribute(stdOutputHandle, textColor);
	std::cout << "validation layer error: " << pCallbackData->pMessage << "\n";
	SetConsoleTextAttribute(stdOutputHandle, DEFAULT_TEXT_COLOR);
#else
	std::cout << "validation layer error: " << pCallbackData->pMessage << "\n";
#endif
	
	return VK_FALSE;
}

void CreateInstance()
{
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "massi_vk_test";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "massi";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = VkDebugErrorCallback;
	debugCreateInfo.pUserData = nullptr; // Optional

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.pNext = &debugCreateInfo;

	// extensions...
	uint32_t glfwExtensionsCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

	std::vector<const char*> extensions = {
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
	};

	extensions.reserve(extensions.size() + (size_t)glfwExtensionsCount);
	for (uint32_t i = 0; i < glfwExtensionsCount; i++)
		extensions.push_back(glfwExtensions[i]);

	createInfo.enabledExtensionCount = (uint32_t)extensions.size();
	createInfo.ppEnabledExtensionNames = extensions.data();

	// layers...
	uint32_t availableLayersCount;
	vkCheck(vkEnumerateInstanceLayerProperties(&availableLayersCount, nullptr));

	std::vector<VkLayerProperties> availableLayers(availableLayersCount);
	vkCheck(vkEnumerateInstanceLayerProperties(&availableLayersCount, availableLayers.data()));

	std::vector<const char*> desiredLayers = {
		"VK_LAYER_KHRONOS_validation" // default vulkan sdk validation layer
	};

	for (const char* myLayer : desiredLayers)
	{
		bool found = false;

		for (const VkLayerProperties& availLayer : availableLayers)
			if (strcmp(myLayer, availLayer.layerName) == 0)
				found = true;

		check(found);
	}

	createInfo.enabledLayerCount = (uint32_t)desiredLayers.size();
	createInfo.ppEnabledLayerNames = desiredLayers.data();

	// create instance
	VkResult createInstanceRes = vkCreateInstance(&createInfo, nullptr, &g_vkInstance);
	vkCheck(createInstanceRes);

	auto debugLogSetterProc = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_vkInstance, "vkCreateDebugUtilsMessengerEXT");
	check(debugLogSetterProc);

	vkCheck(debugLogSetterProc(g_vkInstance, &debugCreateInfo, nullptr, &g_vkDebugMessenger));

	g_MainDeletionQueue.PushBack([]() {
		auto destroyMessengerProc = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_vkInstance, "vkDestroyDebugUtilsMessengerEXT");
		destroyMessengerProc(g_vkInstance, g_vkDebugMessenger, nullptr);

		vkDestroySurfaceKHR(g_vkInstance, g_vkSurface, nullptr);

		vkDestroyInstance(g_vkInstance, nullptr);
	});
}

void CreateSurfaceWin32()
{
	VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
	surfaceInfo.hinstance = GetModuleHandleA(nullptr);
	surfaceInfo.hwnd = glfwGetWin32Window(g_Window);
	surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;

	VkResult createRes = vkCreateWin32SurfaceKHR(g_vkInstance, &surfaceInfo, nullptr, &g_vkSurface);
	vkCheck(createRes);
}

bool DeviceSupportsExtensions(VkPhysicalDevice dev, const std::vector<const char*>& required_exts)
{
	uint32_t gpuExtensionsCount;
	vkCheck(vkEnumerateDeviceExtensionProperties(dev, nullptr, &gpuExtensionsCount, nullptr));

	std::vector<VkExtensionProperties> gpuExtensions(gpuExtensionsCount);
	vkCheck(vkEnumerateDeviceExtensionProperties(dev, nullptr, &gpuExtensionsCount, gpuExtensions.data()));

	std::set<std::string> requiredExtensionsSet(required_exts.begin(), required_exts.end());
	
	for (VkExtensionProperties& gpuExt : gpuExtensions)
		requiredExtensionsSet.erase(gpuExt.extensionName);

	return requiredExtensionsSet.empty();
}

void CreateDeviceAndQueues()
{
	// "choose" device
	uint32_t deviceCount = 0;
	vkCheck(vkEnumeratePhysicalDevices(g_vkInstance, &deviceCount, nullptr));
	check(deviceCount > 0);

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkCheck(vkEnumeratePhysicalDevices(g_vkInstance, &deviceCount, devices.data()));

	std::vector<const char*> requiredGpuExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME
	};

	for (VkPhysicalDevice dev : devices)
	{
		VkPhysicalDeviceProperties gpuProperties;
		vkGetPhysicalDeviceProperties(dev, &gpuProperties);

		uint32_t formatsCount, presentModesCount;
		vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(dev, g_vkSurface, &formatsCount, nullptr));
		vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(dev, g_vkSurface, &presentModesCount, nullptr));

		bool dedicatedGpu = gpuProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
		bool gpuSupportsAllExtensions = DeviceSupportsExtensions(dev, requiredGpuExtensions);
		bool gpuSupportsSwapchain = formatsCount && presentModesCount;

		if (dedicatedGpu && gpuSupportsAllExtensions && gpuSupportsSwapchain)
		{
			g_PhysicalDevice = dev;
			break;
		}
	}
	check(g_PhysicalDevice);

	VkPhysicalDeviceFeatures gpuFeatures; // unused
	vkGetPhysicalDeviceFeatures(g_PhysicalDevice, &gpuFeatures);

	// setup queues
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

	std::optional<uint32_t> graphicsQueueFamilyIndex;
	std::optional<uint32_t> presentQueueFamilyIndex;

	for (uint32_t i = 0; i < (size_t)queueFamilies.size(); i++)
	{
		VkQueueFamilyProperties& family = queueFamilies[i];

		int queueCount = family.queueCount; // unused

		bool hasGraphics = family.queueFlags & VK_QUEUE_GRAPHICS_BIT;
		bool hasCompute = family.queueFlags & VK_QUEUE_COMPUTE_BIT; // unused
		bool hasTransfer = family.queueFlags & VK_QUEUE_TRANSFER_BIT; // unused

		VkBool32 canPresent = VK_FALSE;
		vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, i, g_vkSurface, &canPresent));

		if (!graphicsQueueFamilyIndex && hasGraphics)
			graphicsQueueFamilyIndex = i; // found family that supports graphics

		if (!presentQueueFamilyIndex && canPresent)
			presentQueueFamilyIndex = i; // found family that supports present

		if (graphicsQueueFamilyIndex && presentQueueFamilyIndex)
			break;
	}

	check(graphicsQueueFamilyIndex && presentQueueFamilyIndex);
	g_GraphicsQueueFamilyIndex = graphicsQueueFamilyIndex.value();

	// a single family will most likely support both... and we'll end up having only 1 queue index (same for both -> not added in the set)
	// guarda VulkanCapsViewer per capire come sono strutturate ste famiglie...
	std::set<uint32_t> uniqueFamilies = {
		graphicsQueueFamilyIndex.value(),
		presentQueueFamilyIndex.value()
	};

	std::vector<VkDeviceQueueCreateInfo> queueInfos;
	queueInfos.reserve(uniqueFamilies.size());

	float queuePriority = 1.0f;
	for (uint32_t family : uniqueFamilies)
	{
		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = family;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceVulkan13Features deviceFeatures_13 = {};
	deviceFeatures_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	deviceFeatures_13.dynamicRendering = true;
	deviceFeatures_13.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features deviceFeatures_12 = {};
	deviceFeatures_12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	deviceFeatures_12.pNext = &deviceFeatures_13;
	deviceFeatures_12.bufferDeviceAddress = true;
	deviceFeatures_12.descriptorIndexing = true;

	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = &deviceFeatures_12; // because of pNext
	deviceCreateInfo.pQueueCreateInfos = queueInfos.data();
	deviceCreateInfo.queueCreateInfoCount = (uint32_t)queueInfos.size();
	deviceCreateInfo.pEnabledFeatures = nullptr; // because of pNext
	deviceCreateInfo.enabledExtensionCount = (uint32_t)requiredGpuExtensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = requiredGpuExtensions.data();

	VkResult deviceCreationRes = vkCreateDevice(g_PhysicalDevice, &deviceCreateInfo, nullptr, &g_Device);
	vkCheck(deviceCreationRes);

	g_Queues.reserve(queueInfos.size());
	for (uint32_t family : uniqueFamilies)
	{
		VkQueue& queue = g_Queues.emplace_back();
		vkGetDeviceQueue(g_Device, family, 0, &queue);

		if (family == g_GraphicsQueueFamilyIndex)
			g_GraphicsQueueIndex = family;
	}

	g_MainDeletionQueue.PushBack([]() {
		vkDestroyDevice(g_Device, nullptr);
	});
}

void CreateSwapchain()
{
	constexpr VkFormat TARGET_FORMAT = VK_FORMAT_B8G8R8A8_UNORM; // _SRGB is washed out?!?
	constexpr VkColorSpaceKHR TARGET_COLOR_SPACE = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	constexpr VkPresentModeKHR TARGET_PRESENT_MODE = VK_PRESENT_MODE_IMMEDIATE_KHR;
	//constexpr VkPresentModeKHR TARGET_PRESENT_MODE = VK_PRESENT_MODE_FIFO_RELAXED_KHR;

	VkSurfaceCapabilitiesKHR surfaceCapabilites = {};
	std::vector<VkSurfaceFormatKHR> availableFormats;
	std::vector<VkPresentModeKHR> availablePresentModes;

	uint32_t formatCount;
	vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysicalDevice, g_vkSurface, &formatCount, nullptr));
	availableFormats.resize(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysicalDevice, g_vkSurface, &formatCount, availableFormats.data());

	uint32_t presentModeCount;
	vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(g_PhysicalDevice, g_vkSurface, &presentModeCount, nullptr));
	availablePresentModes.resize(presentModeCount);
	vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(g_PhysicalDevice, g_vkSurface, &presentModeCount, availablePresentModes.data()));

	vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_PhysicalDevice, g_vkSurface, &surfaceCapabilites));

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
	swapchainInfo.surface = g_vkSurface;
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

	VkResult swapchainCreateRes = vkCreateSwapchainKHR(g_Device, &swapchainInfo, nullptr, &g_Swapchain);
	vkCheck(swapchainCreateRes);

	uint32_t swapchainImageCount; // noi abbiamo solo specificato il numero minimo, potrebbero essere di piu, quindi chiediamo
	vkCheck(vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &swapchainImageCount, nullptr));
	g_SwapchainImages.resize(swapchainImageCount);
	vkCheck(vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &swapchainImageCount, g_SwapchainImages.data()));

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
		vkCheck(vkCreateImageView(g_Device, &imageViewInfo, nullptr, &g_SwapchainImageViews[i]));
		
		// semaphore
		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCheck(vkCreateSemaphore(g_Device, &semaphoreInfo, nullptr, &g_SwapchainSemaphores[i]));
	}

	g_MainDeletionQueue.PushBack([]() {
		for (int i = 0; i < g_SwapchainImages.size(); i++)
		{
			vkDestroyImageView(g_Device, g_SwapchainImageViews[i], nullptr);
			vkDestroySemaphore(g_Device, g_SwapchainSemaphores[i], nullptr);
		}

		vkDestroySwapchainKHR(g_Device, g_Swapchain, nullptr);
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
	// compute pipeline
	{
		VkPushConstantRange computePushConstant0;
		computePushConstant0.size = sizeof(PushConstant0);
		computePushConstant0.offset = 0;
		computePushConstant0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		ComputePipelineBuilder computeBuilder;
		computeBuilder.m_ComputeShader = "shaders/bin/comp.spv";
		computeBuilder.m_Descriptors = { g_DescriptorSetLayout0 };
		computeBuilder.m_PushConstants = { computePushConstant0 };

		g_ComputePipeline = computeBuilder.Build(g_Device);
	}

	// graphics pipeline 0
	{
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/vert.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag.spv";
		graphicsBuilder.m_ColorAttachments = { DRAW_FORMAT };
		graphicsBuilder.m_ViewportSize = g_SwapchainExtent;

		g_GraphicsPipeline0 = graphicsBuilder.Build(g_Device);
	}

	// graphics pipeline 1
	{
		VkPushConstantRange meshPushConst;
		meshPushConst.size = sizeof(MeshPushConstant);
		meshPushConst.offset = 0;
		meshPushConst.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
			
		GraphicsPipelineBuilder graphicsBuilder;
		graphicsBuilder.m_VertexShader = "shaders/bin/mesh_vert.spv";
		graphicsBuilder.m_FragmentShader = "shaders/bin/frag.spv";
		graphicsBuilder.m_ColorAttachments = { DRAW_FORMAT };
		graphicsBuilder.m_ViewportSize = g_SwapchainExtent;
		graphicsBuilder.m_PushConstants = { meshPushConst };
		//graphicsBuilder.m_BlendMode = EGraphicsBlendMode::GFX_BLEND_ADDITIVE;
		/*
			We give it depth write, and as operator GREATER_OR_EQUAL.
			As mentioned, because 0 is far and 1 is near,
			we will want to only render the pixels if the current depth value is greater than the depth value on the depth image.
		*/
		graphicsBuilder.m_DepthMode = { VK_COMPARE_OP_GREATER_OR_EQUAL, DEPTH_FORMAT, true };

		g_GraphicsPipeline1 = graphicsBuilder.Build(g_Device);
	}

	// cleanup
	g_MainDeletionQueue.PushBack([]() {
		vkDestroyPipeline(g_Device, g_GraphicsPipeline1.pipeline, nullptr);
		vkDestroyPipelineLayout(g_Device, g_GraphicsPipeline1.layout, nullptr);

		vkDestroyPipeline(g_Device, g_GraphicsPipeline0.pipeline, nullptr);
		vkDestroyPipelineLayout(g_Device, g_GraphicsPipeline0.layout, nullptr);

		vkDestroyPipeline(g_Device, g_ComputePipeline.pipeline, nullptr);
		vkDestroyPipelineLayout(g_Device, g_ComputePipeline.layout, nullptr);
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
	commandPoolInfo.queueFamilyIndex = g_GraphicsQueueFamilyIndex;

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
	{
		vkCheck(vkCreateCommandPool(g_Device, &commandPoolInfo, nullptr, &g_FramesData[i].commandPool));

		// command buffer for gfx
		VkCommandBufferAllocateInfo cmdAllocInfo = {};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.pNext = nullptr;
		cmdAllocInfo.commandPool = g_FramesData[i].commandPool;
		cmdAllocInfo.commandBufferCount = 1; // ne abbiamo solo uno: VkCommandBuffer::commandBuffer
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		vkCheck(vkAllocateCommandBuffers(g_Device, &cmdAllocInfo, &g_FramesData[i].commandBuffer));

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // inizia a flaggarla segnalata, altrimneti se facciamo un wait prima di segnalarla ci blocchiamo (primo wait senza averla toccata dopo la creazione)
		vkCheck(vkCreateFence(g_Device, &fenceInfo, nullptr, &g_FramesData[i].fence));

		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCheck(vkCreateSemaphore(g_Device, &semaphoreInfo, nullptr, &g_FramesData[i].swapchainSemaphore));
	}

	// roba per la roba immediata...
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // inizia a flaggarla segnalata, altrimneti se facciamo un wait prima di segnalarla ci blocchiamo (primo wait senza averla toccata dopo la creazione)
	vkCheck(vkCreateFence(g_Device, &fenceInfo, nullptr, &g_ImmediateFence));

	vkCheck(vkCreateCommandPool(g_Device, &commandPoolInfo, nullptr, &g_ImmediateCommandPool)); // sempre graphics a quanto pare

	VkCommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = g_ImmediateCommandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	vkCheck(vkAllocateCommandBuffers(g_Device, &cmdAllocInfo, &g_ImmediateCmdBuffer));

	g_MainDeletionQueue.PushBack([]() {

		// roba immediata
		vkDestroyCommandPool(g_Device, g_ImmediateCommandPool, nullptr); // distrugge tutti i command buffer allocati con lui
		vkDestroyFence(g_Device, g_ImmediateFence, nullptr);

		// cmd / fence e semafori dei frame
		for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {

			vkDestroyCommandPool(g_Device, g_FramesData[i].commandPool, nullptr); // distrugge tutti i command buffer allocati con lui

			//destroy sync objects
			vkDestroyFence(g_Device, g_FramesData[i].fence, nullptr);
			vkDestroySemaphore(g_Device, g_FramesData[i].swapchainSemaphore, nullptr);

			g_FramesData[i].deletionQueue.Flush();
		}
	});
}

void CreateRenderImage()
{
	// draw image
	{
		VkUtils::ImageDesc imageDesc = {
			.width = g_SwapchainExtent.width,
			.height = g_SwapchainExtent.height,
			.format = DRAW_FORMAT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.aspect = VK_IMAGE_ASPECT_COLOR_BIT
		};

		g_DrawImage = VkUtils::CreateImage(g_Device, imageDesc);
	}

	// draw image depth
	{
		VkUtils::ImageDesc imageDesc = {
			.width = g_SwapchainExtent.width,
			.height = g_SwapchainExtent.height,
			.format = DEPTH_FORMAT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			.aspect = VK_IMAGE_ASPECT_DEPTH_BIT
		};

		g_DrawImageDepth = VkUtils::CreateImage(g_Device, imageDesc);
	}

	g_MainDeletionQueue.PushBack([]() {
		VkUtils::DestroyImage(g_Device, g_DrawImageDepth);
		VkUtils::DestroyImage(g_Device, g_DrawImage); 
	});
}

void CreateDescriptors()
{
	// questo logicamente e' piu un std::set che un std::vector
	// infatti VkDescriptorPoolCreateInfo fa la somma di tutti PoolSize con lo stesso type...
	std::array<VkDescriptorPoolSize, 1> poolSizes; 
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	poolSizes[0].descriptorCount = 1; // per ora una singola immagine di un singolo bartolo per tutto il pool

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = 1; // per ora usiamo 1 solo set
	poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();

	vkCheck(vkCreateDescriptorPool(g_Device, &poolInfo, nullptr, &g_DescriptorPool));

	// crea i bindings (gli struct / immagini nelle shader)
	std::array<VkDescriptorSetLayoutBinding, 1> bindings;
	// bindings[0] = image
	bindings[0].binding = 0; // binding 0 nello shader
	bindings[0].descriptorCount = 1; // array size in shader
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	// crea il set (raccolta di tutti i bindings, disponibili in tutta la pipeline, ma filtrati da stageFlags del sinogolo binding)
	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pBindings = bindings.data();
	layoutInfo.bindingCount = (uint32_t)bindings.size();

	vkCheck(vkCreateDescriptorSetLayout(g_Device, &layoutInfo, nullptr, &g_DescriptorSetLayout0));

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = g_DescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &g_DescriptorSetLayout0;

	vkCheck(vkAllocateDescriptorSets(g_Device, &allocInfo, &g_DescriptorSet0));

	// binding... questo lo facciamo una sola volta, ma viene fatto piu spesso
	VkDescriptorImageInfo imgInfo = {};
	imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imgInfo.imageView = g_DrawImage.view;

	VkWriteDescriptorSet drawImageWrite = {};
	drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

	drawImageWrite.dstBinding = 0; // layoutInfo.pBindings[x]
	drawImageWrite.dstSet = g_DescriptorSet0;
	drawImageWrite.descriptorCount = 1; // bindings[0].descriptorCount
	drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	drawImageWrite.pImageInfo = &imgInfo;

	vkUpdateDescriptorSets(g_Device, 1, &drawImageWrite, 0, nullptr);

	g_MainDeletionQueue.PushBack([]() {
		vkDestroyDescriptorPool(g_Device, g_DescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(g_Device, g_DescriptorSetLayout0, nullptr);
	});
}

void InitImgui()
{
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
	vkCheck(vkCreateDescriptorPool(g_Device, &pool_info, nullptr, &imgui_pool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	check(ImGui::CreateContext());

	// this initializes imgui for SDL

	check(ImGui_ImplGlfw_InitForVulkan(g_Window, true /* chissene per sto test */));

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = g_vkInstance;
	init_info.PhysicalDevice = g_PhysicalDevice;
	init_info.Device = g_Device;
	init_info.Queue = g_Queues[g_GraphicsQueueIndex];
	init_info.DescriptorPool = imgui_pool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &g_SwapchainSurfaceFormat.format;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	check(ImGui_ImplVulkan_Init(&init_info));

	ImGui::StyleColorsDark();
	
	g_MainDeletionQueue.PushBack([imgui_pool]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(g_Device, imgui_pool, nullptr);
	});
}

void ImmediateSubmit(std::function<void(VkCommandBuffer)> func)
{
	vkCheck(vkResetFences(g_Device, 1, &g_ImmediateFence));
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
	VkQueue graphicsQueue = g_Queues[g_GraphicsQueueIndex];
	vkCheck(vkQueueSubmit2(graphicsQueue, 1, &submitInfo, g_ImmediateFence));

	vkCheck(vkWaitForFences(g_Device, 1, &g_ImmediateFence, true, 9999999999));
}

MeshData LoadMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
	MeshData outMeshData = {};

	size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	// crea index buffer & vertex buffer
	outMeshData.vertexBuffer = VkUtils::CreateBuffer(g_Device, vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
		| VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	outMeshData.indexBuffer = VkUtils::CreateBuffer(g_Device, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// prendi il puntatore al vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo = {};
	deviceAdressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	deviceAdressInfo.buffer = outMeshData.vertexBuffer.buffer;
	outMeshData.vertexBufferAddress = vkGetBufferDeviceAddress(g_Device, &deviceAdressInfo);

	VkUtils::Buffer tmpStagingBuffer = VkUtils::CreateBuffer(g_Device, vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// copia i dati da cpu a gpu
	void* data = 0;
	vkCheck(vkMapMemory(g_Device, tmpStagingBuffer.memory, 0, vertexBufferSize + indexBufferSize, 0, &data));
	memcpy(data, vertices.data(), vertexBufferSize); // copy vertex data to staging buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize); // copy index data to staging buffer
	vkUnmapMemory(g_Device, tmpStagingBuffer.memory);

	ImmediateSubmit([&](VkCommandBuffer cmd) { // mega stallo evvai
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;
		vkCmdCopyBuffer(cmd, tmpStagingBuffer.buffer, outMeshData.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, tmpStagingBuffer.buffer, outMeshData.indexBuffer.buffer, 1, &indexCopy);
		});

	// adesso lo distruggiamo... ma sarebbe utile avere uno staging buffer "statico" per questo genere di cose... non ha senso
	// crearne uno ogni volta che dobbiamo spostare roba...
	VkUtils::DestroyBuffer(g_Device, tmpStagingBuffer);

	return outMeshData;
}

void LoadGeometry()
{
	Resource::LoadMesh(std::filesystem::path("assets") / "basicmesh.glb", g_Meshes);

	g_MainDeletionQueue.PushBack([]() {

		for (auto& mesh : g_Meshes)
		{
			VkUtils::DestroyBuffer(g_Device, mesh.data.vertexBuffer);
			VkUtils::DestroyBuffer(g_Device, mesh.data.indexBuffer);
		}
	});
}

void InitVulkan()
{
	CreateInstance();
	CreateSurfaceWin32(); // glfwCreateWindowSurface()
	CreateDeviceAndQueues();

	VkUtils::Init(g_PhysicalDevice);
	
	CreateSwapchain();
	CreateCommands();
	CreateRenderImage();

	CreateDescriptors();

	//CreateRenderPass(); dynamic_rendering yeee
	CreatePipeline();
	//CreateFramebuffers(); dynamic_rendering yeee

	LoadGeometry();
}

void ShutdownVulkan()
{
	//make sure the gpu has stopped doing its things				
	vkDeviceWaitIdle(g_Device);
	//flush the global deletion queue
	g_MainDeletionQueue.Flush();
}

void ImGuii()
{
	ImGui::Begin("Color (push const)");	

	ImGui::ColorEdit4("Color start", glm::value_ptr(g_ColorPushConst.colorStart));
	ImGui::ColorEdit4("Color end", glm::value_ptr(g_ColorPushConst.colorEnd));
	
	ImGui::Separator();

	ImGui::DragFloat3("Location", glm::value_ptr(g_MeshTransform.position), 0.005f);
	ImGui::DragFloat3("Rotation", glm::value_ptr(g_MeshTransform.rotation), 0.1f);
	ImGui::DragFloat3("Scale", glm::value_ptr(g_MeshTransform.scale), 0.005f);

	ImGui::End();
}

void NewFrame()
{
	FrameData& frameData = g_FramesData[g_FrameIndex];

	// wait until the gpu has finished rendering the last frame. Timeout of 1 second
	// se il timeout e' 0 restituisce lo stato corrente della fence
	vkCheck(vkWaitForFences(g_Device, 1, &frameData.fence, true, 1000000000 /* ns */)); // asepettaq che diventi signaled (nel primo frame si bloccerebbe senza il VK_FENCE_CREATE_SIGNALED_BIT nella craezione)
	vkCheck(vkResetFences(g_Device, 1, &frameData.fence)); // settala di nuovo unsignaled

	uint32_t imageIndex;
	vkCheck(vkAcquireNextImageKHR(g_Device, g_Swapchain, 1000000000, frameData.swapchainSemaphore, nullptr, &imageIndex));

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

	//std::cout << std::format("frame index: {}, image index: {}\n", g_FrameIndex, imageIndex);

	// convert image for writing, VK_IMAGE_LAYOUT_GENERAL va bene per scrivere da compute o fare il clear, non e' il meglio...
	VkUtils::TransitionImage(cmd, g_DrawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	auto imageRange = VkUtils::ImageRange(VK_IMAGE_ASPECT_COLOR_BIT);
	
	// clear
	VkClearColorValue clearColor;
	clearColor.float32[0] = 0.2f;
	clearColor.float32[1] = 0.4f;
	clearColor.float32[2] = 0.6f;
	clearColor.float32[3] = 1.0f;
	vkCmdClearColorImage(cmd, g_DrawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &imageRange);

#if 0 
	// PIPELINE COMPUTE!!!
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ComputePipeline.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_ComputePipeline.layout, 0, 1, &g_DescriptorSet0, 0, nullptr);
	vkCmdPushConstants(cmd, g_ComputePipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstant0), &g_ColorPushConst);
	
	uint32_t groupCountX = (uint32_t)std::ceil((float)g_SwapchainExtent.width / 16.0f);
	uint32_t groupCountY = (uint32_t)std::ceil((float)g_SwapchainExtent.height / 16.0f);
	uint32_t groupCountZ = 1;
	vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ); // dispatchaaaaaaa
#endif

	// draw with graphics
	VkUtils::TransitionImage(cmd, g_DrawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	
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

	VkRenderingAttachmentInfo drawImageColorAttachment = VkUtils::AttachmentInfo(g_DrawImage.view, nullptr, VK_IMAGE_LAYOUT_GENERAL /* perche? */);
	VkRenderingAttachmentInfo drawImageDepth = VkUtils::AttachmentInfoDepth(g_DrawImageDepth.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

#if 0
	// PIPELINE GRAPHICS 0!!!
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GraphicsPipeline0.pipeline);

	// vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GraphicsPipeline0.layout, 0, 1, ???, 0, nullptr);
	// vkCmdPushConstants(cmd, g_GraphicsPipeline.layout, ???, 0, ???, ???);

	// la nostra drawimage diventa il nostro colorattachment[0]... infatti hanno lo stesso formato
	VkRenderingInfo renderInfo0 = VkUtils::RenderingInfo(g_SwapchainExtent, &drawImageColorAttachment, nullptr);
	vkCmdBeginRendering(cmd, &renderInfo0);
	vkCmdDraw(cmd, 3, 1, 0, 0); // draw test triangle !!!
	vkCmdEndRendering(cmd); // schifo, perche dobbiamo aggiungere il depth...
#endif

	// PIPELINE GRAPHICS 1!!!
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_GraphicsPipeline1.pipeline);
	
	VkRenderingInfo renderInfo1 = VkUtils::RenderingInfo(g_SwapchainExtent, &drawImageColorAttachment, &drawImageDepth);
	vkCmdBeginRendering(cmd, &renderInfo1);

	std::vector<MeshResource> toDraw = { g_Meshes[2] };

	for (auto& mesh : toDraw)
	{
		glm::mat4 rotation = glm::rotate(glm::radians(g_MeshTransform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f))
			* glm::rotate(glm::radians(g_MeshTransform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f))
			* glm::rotate(glm::radians(g_MeshTransform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));

		glm::mat4 model = glm::translate(g_MeshTransform.position) * rotation * glm::scale(g_MeshTransform.scale);
		
		glm::vec3 eyeFocus = glm::vec3(0.0f, 0.0f, 1.0f);
		glm::vec3 eyePos = glm::vec3(0.0f, 0.0f, -3.0f);
		glm::mat4 view = glm::lookAtLH(eyePos, eyeFocus + eyePos, glm::vec3(0.0f, 1.0f, 0.0f));

		glm::mat4 proj = glm::perspectiveLH(glm::radians(70.0f), (float)g_SwapchainExtent.width / (float)g_SwapchainExtent.height, 10000.f, 0.1f);

		MeshPushConstant meshPushConst;
		meshPushConst.worldMatrix = proj * view * model;
		meshPushConst.vertexBuffer = mesh.data.vertexBufferAddress;

		vkCmdPushConstants(cmd, g_GraphicsPipeline1.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstant), &meshPushConst);
		vkCmdBindIndexBuffer(cmd, mesh.data.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		for(auto& surface : mesh.surfaces)
			vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
	}

	vkCmdEndRendering(cmd);

	// prepare for copying to swapchain
	VkUtils::TransitionImage(cmd, g_DrawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	VkUtils::TransitionImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// copy to swapchain
	VkUtils::CopyImage(cmd, swapchainImage.image, g_DrawImage.image, g_SwapchainExtent, g_SwapchainExtent);

	// prepare for imgui
	VkUtils::TransitionImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	
	// imgui draw... potrebbe essere ovunuque
	ImGuii();
	ImGui::Render();

	VkRenderingAttachmentInfo swapchainColorAttachment = VkUtils::AttachmentInfo(swapchainImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo swapchainRenderInfo = VkUtils::RenderingInfo(g_SwapchainExtent, &swapchainColorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &swapchainRenderInfo);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	vkCmdEndRendering(cmd);

	// preapre for present
	VkUtils::TransitionImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	vkCheck(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = VkUtils::CommandBufferSubmitInfo(cmd);

	VkSemaphoreSubmitInfo waitInfo = VkUtils::SemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, frameData.swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = VkUtils::SemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, swapchainImage.presentSemaphore);

	VkSubmitInfo2 submit = VkUtils::SubmitInfo(&cmdInfo, &signalInfo, &waitInfo);

	// launch cmd on graphics queue
	VkQueue graphicsQueue = g_Queues[g_GraphicsQueueIndex];

	vkCheck(vkQueueSubmit2(graphicsQueue, 1, &submit, frameData.fence));

	// present
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pSwapchains = &g_Swapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &swapchainImage.presentSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &imageIndex;
	vkCheck(vkQueuePresentKHR(graphicsQueue, &presentInfo));

	g_FrameIndex = (g_FrameIndex + 1) % FRAMES_IN_FLIGHT;
}

int main()
{
	check(glfwInit() == GLFW_TRUE);

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	g_Window = glfwCreateWindow(1100, 720, "Vulkan!!!", nullptr, nullptr);

	InitVulkan();
	InitImgui();

	while (!glfwWindowShouldClose(g_Window))
	{
		glfwPollEvents();

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		NewFrame();
	}

	ShutdownVulkan();

	glfwDestroyWindow(g_Window);
	glfwTerminate();
}