#define VK_USE_PLATFORM_WIN32_KHR
#include "RendererContext.h"

#include "VkUtils.h"

#define GLFW_INCLUDE_VULKAN
#include "../vendor/glfw/glfw3.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include "glfw/glfw3native.h"

#define VK_DEBUG 1

static bool DeviceSupportsExtensions(VkPhysicalDevice dev, const std::vector<const char*>& required_exts)
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

RendererDevice::RendererDevice()
	: m_PhysicalDevice(VK_NULL_HANDLE)
	, m_Device(VK_NULL_HANDLE)
	, m_Initialized(false)
{
}

RendererDevice::~RendererDevice()
{
	if (m_Initialized)
		Shutdown();
}

void RendererDevice::Init(VkInstance instance, VkSurfaceKHR surface)
{
	check(!m_Initialized);

	// "choose" device
	uint32_t deviceCount = 0;
	vkCheck(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
	check(deviceCount > 0);

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkCheck(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));

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
		vkCheck(vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatsCount, nullptr));
		vkCheck(vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModesCount, nullptr));

		bool dedicatedGpu = gpuProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
		bool gpuSupportsAllExtensions = DeviceSupportsExtensions(dev, requiredGpuExtensions);
		bool gpuSupportsSwapchain = formatsCount && presentModesCount;

		if (dedicatedGpu && gpuSupportsAllExtensions && gpuSupportsSwapchain)
		{
			m_PhysicalDevice = dev;
			break;
		}
	}
	check(m_PhysicalDevice);

	VkPhysicalDeviceFeatures gpuFeatures; // unused
	vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &gpuFeatures);

	// setup queues
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

	std::optional<uint32_t> graphicsFamilyIndex;
	std::vector<VkDeviceQueueCreateInfo> queueInfos;
	queueInfos.reserve(queueFamilies.size());

	std::vector<float> queuePriorities;

	for (uint32_t i = 0; i < (uint32_t)queueFamilies.size(); i++)
	{
		VkQueueFamilyProperties& family = queueFamilies[i];

		uint32_t queuesToCreate = family.queueCount;
		bool createForTransfer = family.queueFlags & VK_QUEUE_TRANSFER_BIT;
		bool createForGraphics = false;

		if (!graphicsFamilyIndex && family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			VkBool32 canPresent = VK_FALSE;
			vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, surface, &canPresent));

			if (canPresent)
			{
				graphicsFamilyIndex = i;
				createForGraphics = true;

				if (!createForTransfer)
					queuesToCreate = 1; // if a family supporst gfx but not transfer, we only need 1 queue anyway
			}
		}

		if (createForTransfer || createForGraphics)
		{
			queuePriorities.resize(queuesToCreate);
			for (uint32_t i = 0; i < queuesToCreate; i++) // memset maybe??
				queuePriorities[i] = 0.8f;

			if (createForGraphics)
				queuePriorities[0] = 1.0f;

			VkDeviceQueueCreateInfo& queueInfo = queueInfos.emplace_back();
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = i;
			queueInfo.queueCount = queuesToCreate;
			queueInfo.pQueuePriorities = queuePriorities.data();
		}
	}
	check(graphicsFamilyIndex);
	m_GraphicsQueue.familyIndex = graphicsFamilyIndex.value();

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

	VkResult deviceCreationRes = vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_Device);
	vkCheck(deviceCreationRes);

	// graphics queue
	vkGetDeviceQueue(m_Device, m_GraphicsQueue.familyIndex, 0, &m_GraphicsQueue.queue);

	// transfer queues
	for (VkDeviceQueueCreateInfo& queueInfo : queueInfos)
	{
		uint32_t firstQueueIndex = queueInfo.queueFamilyIndex != m_GraphicsQueue.familyIndex ? 0 : 1;

		for (uint32_t i = firstQueueIndex; i < queueInfo.queueCount; i++)
		{
			Queue& queue = m_TransferQueues.emplace_back();
			queue.familyIndex = queueInfo.queueFamilyIndex;
			vkGetDeviceQueue(m_Device, queueInfo.queueFamilyIndex, i, &queue.queue);
		}
	}

	m_Initialized = true;
}

void RendererDevice::Shutdown()
{
	check(m_Initialized);

	vkDestroyDevice(m_Device, nullptr);
	
	m_Initialized = false;
}

RendererContext::RendererContext()
	: m_Instance(VK_NULL_HANDLE)
	, m_Surface(VK_NULL_HANDLE)
	, m_DebugMessenger(VK_NULL_HANDLE)
	, m_Initialized(false)
{
}

RendererContext::~RendererContext()
{
	if (m_Initialized)
		Shutdown();
}

void RendererContext::Init(void* wndHandle)
{
	check(!m_Initialized);

	CreateInstance();
	CreateSurface(wndHandle);

	m_Device.Init(m_Instance, m_Surface);

	m_Initialized = true;
}

void RendererContext::Shutdown()
{
	check(m_Initialized);

	m_DeletionQueue.Flush();

	m_Device.Shutdown();
	vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

#if VK_DEBUG
	auto destroyMessengerProc = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
	destroyMessengerProc(m_Instance, m_DebugMessenger, nullptr);
#endif
	vkDestroyInstance(m_Instance, nullptr);

	m_Initialized = false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugErrorCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	constexpr bool kLogInfoEvents = false;

	ELogSeverity logSeverity = ELogSeverity::Info;

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		logSeverity = ELogSeverity::Error;
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		logSeverity = ELogSeverity::Warning;

	if (logSeverity == ELogSeverity::Info && !kLogInfoEvents)
		return VK_FALSE;

	DEBUG_LOG(logSeverity, "Vulkan validation error: %s", pCallbackData->pMessage);
	return VK_FALSE;
}

void RendererContext::CreateInstance()
{
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "massi_vk_test";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "massi";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	
#if VK_DEBUG
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};
	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = VkDebugErrorCallback;
	debugCreateInfo.pUserData = nullptr; // Optional

	createInfo.pNext = &debugCreateInfo;
#endif

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

	std::vector<const char*> desiredLayers = {};

#if VK_DEBUG
	desiredLayers.push_back("VK_LAYER_KHRONOS_validation"); // default vulkan sdk validation layer
#endif

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
	VkResult createInstanceRes = vkCreateInstance(&createInfo, nullptr, &m_Instance);
	vkCheck(createInstanceRes);

#if VK_DEBUG
	auto debugLogSetterProc = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
	check(debugLogSetterProc);

	vkCheck(debugLogSetterProc(m_Instance, &debugCreateInfo, nullptr, &m_DebugMessenger));
#endif
}

void RendererContext::CreateSurface(void* wndHandle)
{
	check(wndHandle);

	VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
	surfaceInfo.hinstance = GetModuleHandleA(nullptr);
	surfaceInfo.hwnd = (HWND)wndHandle;
	surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;

	VkResult createRes = vkCreateWin32SurfaceKHR(m_Instance, &surfaceInfo, nullptr, &m_Surface);
	vkCheck(createRes);
}
