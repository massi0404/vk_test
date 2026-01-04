#pragma once

#include "Core/Core.h"
#include <vulkan/vulkan.h>
#include "Misc/DeletionQueue.h"

struct Queue
{
	VkQueue queue = VK_NULL_HANDLE;
	u32 familyIndex = 0;
};

class RendererDevice
{
public:
	RendererDevice();
	~RendererDevice();

	void Init(VkInstance instance, VkSurfaceKHR surface);
	void Shutdown();

	inline VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
	inline VkDevice GetDevice() const { return m_Device; }
	inline VkQueue GetGraphicsQueue() const { return m_GraphicsQueue.queue; }
	inline u32 GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueue.familyIndex; }
	inline const std::vector<Queue>& GetTransferQueues() const { return m_TransferQueues; }

private:
	VkPhysicalDevice m_PhysicalDevice;
	VkDevice m_Device;
	Queue m_GraphicsQueue;
	std::vector<Queue> m_TransferQueues;

	bool m_Initialized;
};

class RendererContext
{
public:
	RendererContext();
	~RendererContext();

	void Init(void* wndHandle);
	void Shutdown();

	inline const RendererDevice& GetRendererDevice() const { return m_Device; }
	inline VkDevice GetDevice() const { return m_Device.GetDevice(); }
	inline VkPhysicalDevice GetGPU() const { return m_Device.GetPhysicalDevice(); }

	inline VkInstance GetInstance() const { return m_Instance; }
	inline VkSurfaceKHR GetSurface() const { return m_Surface; }

	inline void QueueShutdownFunc(std::function<void()>&& func) { m_DeletionQueue.PushBack(std::move(func)); }

private:
	void CreateInstance();
	void CreateSurface(void* wndHandle);

private:
	RendererDevice m_Device;
	DeletionQueue m_DeletionQueue;

	VkInstance m_Instance;
	VkDebugUtilsMessengerEXT m_DebugMessenger;
	VkSurfaceKHR m_Surface;

	bool m_Initialized;
};