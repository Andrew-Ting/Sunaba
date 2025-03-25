#pragma once

#include <volk.h>
#include <vector>
#include <glm/glm.hpp>
#include <vk_mem_alloc.h>

#include "deletion_queue.h"
#include "frame_data.h"
#include "vk_types.h"

class VulkanEngine {

public:
	inline static const int FRAMES_IN_FLIGHT = 2;
	inline static const char* ENGINE_NAME = "Sunaba";
	inline static const double RENDER_SCALE = 1.0f;

	struct EngineStats {
		float frametime;
	};
	EngineStats engineStatistics;

	void init();
	void run();
	void cleanup();

private:
	VkExtent2D mWindowExtent{ 1700 , 900 }; // window size
	struct SDL_Window* mWindow{ nullptr };

	VkInstance mVkInstance;// Vulkan library handle
	VkDebugUtilsMessengerEXT mDebugMessenger;// Vulkan debug output handle
	VkPhysicalDevice mPhysicalDevice;// GPU chosen as the default device
	VkDevice mLogicalDevice; // Vulkan device for commands

	VkSurfaceKHR mSwapchainSurface;// Vulkan window surface
	VkSwapchainKHR mSwapchain;
	VkFormat mSwapchainImageFormat;
	VkExtent2D mSwapchainExtent; // size of swapchain image
	std::vector<VkImage> mSwapchainImages;
	std::vector<VkImageView> mSwapchainImageViews;

	VkQueue mGraphicsQueue;
	uint32_t mGraphicsQueueFamily;

	// resources for immediate rendering
	VkCommandPool mImmediateCommandPool;
	VkCommandBuffer mImmediateCommandBuffer;
	VkFence mImmediateFence;

	int mCurrentFrameNumber {0};
	FrameData mFrames[FRAMES_IN_FLIGHT];

	bool mStopRendering = false;
	bool mSwapchainResizeRequested = false;
	VmaAllocator mVmaAllocator;
	DeletionQueue mEngineDeletionQueue;

	// resources for initial drawing of frame (i.e. before up/downscaling)
	AllocatedImage mDrawImage;
	VkExtent2D mDrawExtent; // actual resolution with which we render frames

	void init_sdl();
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void init_descriptors();
	void init_pipelines();
	void init_background_pipelines();
	void init_mesh_pipeline();
	void init_imgui();

	FrameData& get_current_frame() { return mFrames[mCurrentFrameNumber]; };

	void create_swapchain(uint32_t width, uint32_t height);
	void resize_swapchain();
	void destroy_swapchain();

	void draw();
	void clear_scene(VkCommandBuffer cmd, VkImage targetImage);
	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
};