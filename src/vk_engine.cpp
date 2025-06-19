#include <chrono>
#include <thread>
#include <volk.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "vk_check_macro.h"
#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_utils.h"

void VulkanEngine::init()
{

	init_sdl();

	init_vulkan();

	init_swapchain();

	init_commands();

	init_sync_structures();

	init_descriptors();

	init_pipelines();

	init_imgui();

}

void VulkanEngine::run() {
	SDL_Event sdlEvent;
	bool bQuitEngine = false;

	//main loop
	while (!bQuitEngine)
	{
		// clock at frame beginning
		auto start = std::chrono::system_clock::now();
		//Handle events on queue
		while (SDL_PollEvent(&sdlEvent) != 0)
		{
			//close the window when user alt-f4s or clicks the X button			
			if (sdlEvent.type == SDL_EVENT_QUIT)
				bQuitEngine = true;
			if (sdlEvent.type == SDL_EVENT_WINDOW_MINIMIZED) {
				mStopRendering = true;
			}
			if (sdlEvent.type == SDL_EVENT_WINDOW_RESTORED) {
				mStopRendering = false;
			}

			//send SDL event to imgui for handling
			ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
		}

		// do not draw if the window is minimized
		if (mStopRendering) {
			// throttle the speed to avoid endless spinning
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (mSwapchainResizeRequested) {
			resize_swapchain();
		}

		// imgui new frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();
		// Make this frame a dockspace (i.e. anchorable around the render window)
		ImGui::DockSpaceOverViewport(0, 0, ImGuiDockNodeFlags_PassthruCentralNode);

		if (ImGui::Begin("Statistics")) {
			ImGui::Text("Frame Time: %f ms", engineStatistics.frametime);
		}

		ImGui::End();

		//make imgui calculate internal draw structures
		ImGui::Render();

		draw();

		// clock at frame end
		auto end = std::chrono::system_clock::now();

		// compute frame time in milliseconds
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		engineStatistics.frametime = elapsed.count() / 1000.f;

	}
}

void VulkanEngine::cleanup()
{
	// wait for the GPU to finish all its pending tasks
	vkDeviceWaitIdle(mLogicalDevice);

	//destroy per frame resources
	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		mFrames[i].mDeletionQueue.flush();
	}
	// destroy global engine resources
	mEngineDeletionQueue.flush();

	// destruction of these vulkan objects must come last, and order is important
	destroy_swapchain();
	vkDestroySurfaceKHR(mVkInstance, mSwapchainSurface, nullptr);
	vkDestroyDevice(mLogicalDevice, nullptr);
	vkb::destroy_debug_utils_messenger(mVkInstance, mDebugMessenger);
	vkDestroyInstance(mVkInstance, nullptr);
	SDL_DestroyWindow(mWindow);
}

void VulkanEngine::init_sdl() {
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	mWindow = SDL_CreateWindow(
		ENGINE_NAME,
		mWindowExtent.width,
		mWindowExtent.height,
		window_flags
	);
}

void VulkanEngine::init_vulkan() {

	if (volkInitialize() != VK_SUCCESS) {
		throw std::runtime_error("Failed to initialize Volk!");
	}
	// double check that vulkan-1.dll exists on your system in this directory; if not, point it to the right directory
	// if vulkan-1.dll does not exist on your computer, your GPU may not have support for Vulkan :(
	SDL_Vulkan_LoadLibrary("C:\\Windows\\System32\\vulkan-1.dll");
#if _DEBUG
	constexpr bool bUseValidationLayers = true;
#endif
#if NDEBUG
	constexpr bool bUseValidationLayers = false;
#endif

	vkb::InstanceBuilder builder;
	//make the vulkan instance, with basic debug features
	auto builtVkbInstance = builder.set_app_name("Vulkan Application")
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0)
		.build();

	vkb::Instance vkbInstance = builtVkbInstance.value();

	//grab the vulkan instance and debug messenger
	mVkInstance = vkbInstance.instance;
	mDebugMessenger = vkbInstance.debug_messenger;

	volkLoadInstance(mVkInstance);

	SDL_Vulkan_CreateSurface(mWindow, mVkInstance, nullptr, &mSwapchainSurface);

	//vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features13{ };
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = true;
	features13.synchronization2 = true;

	//vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12{ };
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;


	//use vkbootstrap to select a gpu. 
	//We want a gpu that can write to the SDL surface and supports the correct features of vulkan 1.2/1.3
	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice vkbPhysicalDevice = selector
		.set_minimum_version(1, 3)
		.add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
		.set_required_features_13(features13)
		.set_required_features_12(features12)
		.set_surface(mSwapchainSurface)
		.select()
		.value();

	//create the final vulkan device
	vkb::DeviceBuilder vkbDeviceBuilder{ vkbPhysicalDevice };
	vkb::Device vkbDevice = vkbDeviceBuilder.build().value();

	// Get the VkDevice handle used in the rest of a vulkan application
	mLogicalDevice = vkbDevice.device;
	mPhysicalDevice = vkbPhysicalDevice.physical_device;

	volkLoadDevice(mLogicalDevice);

	// get queues from vkbootstrap
	mGraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	mGraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// pass dynamic vk function pointers to VMA
	VmaVulkanFunctions vma_vulkan_func{};
	vma_vulkan_func.vkAllocateMemory = vkAllocateMemory;
	vma_vulkan_func.vkBindBufferMemory = vkBindBufferMemory;
	vma_vulkan_func.vkBindImageMemory = vkBindImageMemory;
	vma_vulkan_func.vkCreateBuffer = vkCreateBuffer;
	vma_vulkan_func.vkCreateImage = vkCreateImage;
	vma_vulkan_func.vkDestroyBuffer = vkDestroyBuffer;
	vma_vulkan_func.vkDestroyImage = vkDestroyImage;
	vma_vulkan_func.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
	vma_vulkan_func.vkFreeMemory = vkFreeMemory;
	vma_vulkan_func.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
	vma_vulkan_func.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
	vma_vulkan_func.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
	vma_vulkan_func.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
	vma_vulkan_func.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
	vma_vulkan_func.vkMapMemory = vkMapMemory;
	vma_vulkan_func.vkUnmapMemory = vkUnmapMemory;
	vma_vulkan_func.vkCmdCopyBuffer = vkCmdCopyBuffer;

	// initialize the VMA memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = mPhysicalDevice;
	allocatorInfo.device = mLogicalDevice;
	allocatorInfo.instance = mVkInstance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	allocatorInfo.pVulkanFunctions = &vma_vulkan_func;
	vmaCreateAllocator(&allocatorInfo, &mVmaAllocator);

	mEngineDeletionQueue.push_function([&]() {
		vmaDestroyAllocator(mVmaAllocator);
	});
}
void VulkanEngine::init_swapchain() {
	create_swapchain(mWindowExtent.width, mWindowExtent.height);

	// This section creates the drawn image buffer used every frame. The result is then just blitted to the appropriate swapchain image
	// The draw image size will match the window
	VkExtent3D drawImageExtent = {
		mWindowExtent.width,
		mWindowExtent.height,
		1
	};

	//hardcoding the draw format to RGBA 16 bits each, which is good for most purposes
	mDrawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	mDrawImage.imageExtent = drawImageExtent;

	// usage flags are an internal Vulkan image optimization which we don't need to keep track of
	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // allows blitting from the image
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // allows blitting to the image
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT; // allows writing to the image in a compute shader
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // allows writing to the image in a fragment shader

	VkImageCreateInfo drawImageCreateInfo = vkinit::image_create_info(mDrawImage.imageFormat, drawImageUsages, mDrawImage.imageExtent, 1);

	// Allocate the draw image from gpu local memory
	VmaAllocationCreateInfo drawImageAllocationInfo = {};
	drawImageAllocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	drawImageAllocationInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vmaCreateImage(mVmaAllocator, &drawImageCreateInfo, &drawImageAllocationInfo, &mDrawImage.image, &mDrawImage.allocation, nullptr);

	// allocate an image view for the draw image to use for rendering
	VkImageViewCreateInfo drawImageViewCreateInfo = vkinit::imageview_create_info(mDrawImage.imageFormat, mDrawImage.image, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	VK_CHECK(vkCreateImageView(mLogicalDevice, &drawImageViewCreateInfo, nullptr, &mDrawImage.imageView));

	//add draw image and its view to engine deletion queue
	mEngineDeletionQueue.push_function([=]() {
		vkDestroyImageView(mLogicalDevice, mDrawImage.imageView, nullptr);
		vmaDestroyImage(mVmaAllocator, mDrawImage.image, mDrawImage.allocation);
	});
}

void VulkanEngine::init_commands() {
	//create a command pool for commands submitted to the graphics queue.
	//we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(mGraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	
	VK_CHECK(vkCreateCommandPool(mLogicalDevice, &commandPoolInfo, nullptr, &mImmediateCommandPool));

	// allocate the command buffer for immediate submits
	VkCommandBufferAllocateInfo immediateCommandBufferInfo = vkinit::command_buffer_allocate_info(mImmediateCommandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(mLogicalDevice, &immediateCommandBufferInfo, &mImmediateCommandBuffer));

	mEngineDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(mLogicalDevice, mImmediateCommandPool, nullptr);
	});


	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {

		VK_CHECK(vkCreateCommandPool(mLogicalDevice, &commandPoolInfo, nullptr, &mFrames[i].mCommandPool));

		// allocate the default command buffer used for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(mFrames[i].mCommandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(mLogicalDevice, &cmdAllocInfo, &mFrames[i].mMainCommandBuffer));

		// for efficiency, we clean up frame command pools when the engine terminates, not every frame
		mEngineDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(mLogicalDevice, mFrames[i].mCommandPool, nullptr);
		});
	}
}
void VulkanEngine::init_sync_structures() {
	// create synchronization structures
	// one fence to control when the gpu has finished rendering the frame,
	// and 2 semaphores to synchronize rendering with swapchain
	// we want the fence to start signalled so we can wait on it on the first frame
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
		// signals rendering has finished and the CPU can update this frame's command buffers
		VK_CHECK(vkCreateFence(mLogicalDevice, &fenceCreateInfo, nullptr, &mFrames[i].mRenderFence));

		// signals presentation has finished and the next frame can be rendered on
		VK_CHECK(vkCreateSemaphore(mLogicalDevice, &semaphoreCreateInfo, nullptr, &mFrames[i].mSwapchainSemaphore));
		// signals rendering has finished and the frame can be presented
		VK_CHECK(vkCreateSemaphore(mLogicalDevice, &semaphoreCreateInfo, nullptr, &mFrames[i].mRenderSemaphore));

		// for efficiency, we clean up frame sync structures when the engine terminates, not every frame
		mEngineDeletionQueue.push_function([=]() {
			vkDestroyFence(mLogicalDevice, mFrames[i].mRenderFence, nullptr);
			vkDestroySemaphore(mLogicalDevice, mFrames[i].mRenderSemaphore, nullptr);
			vkDestroySemaphore(mLogicalDevice, mFrames[i].mSwapchainSemaphore, nullptr);
		});
	}

	// for the immediate command buffer, ensure the GPU is done using it before writing the next frame's commands on the CPU
	VK_CHECK(vkCreateFence(mLogicalDevice, &fenceCreateInfo, nullptr, &mImmediateFence));

	mEngineDeletionQueue.push_function([=]() { 
		vkDestroyFence(mLogicalDevice, mImmediateFence, nullptr);
	});
}

void VulkanEngine::init_descriptors() {
}

void VulkanEngine::init_pipelines() {
}

void VulkanEngine::init_imgui() {
	// the IMGUI descriptor pool size is overkill, but it's copied from imgui's demos
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

	VkDescriptorPoolCreateInfo imguiPoolInfo = {};
	imguiPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	imguiPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	imguiPoolInfo.maxSets = 1000;
	imguiPoolInfo.poolSizeCount = (uint32_t)std::size(pool_sizes);
	imguiPoolInfo.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(mLogicalDevice, &imguiPoolInfo, nullptr, &imguiPool));

	// initializes the core structures of the imgui library
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // enable docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // enable UI visibility outside the render window

	// initialize imgui for SDL
	ImGui_ImplSDL3_InitForVulkan(mWindow);

	// initialize imgui for Vulkan
	ImGui_ImplVulkan_InitInfo imguiInitInfo = {};
	imguiInitInfo.Instance = mVkInstance;
	imguiInitInfo.PhysicalDevice = mPhysicalDevice;
	imguiInitInfo.Device = mLogicalDevice;
	imguiInitInfo.QueueFamily = mGraphicsQueueFamily;
	imguiInitInfo.Queue = mGraphicsQueue;
	imguiInitInfo.DescriptorPool = imguiPool;
	imguiInitInfo.MinImageCount = 3;
	imguiInitInfo.ImageCount = 3;
	imguiInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	imguiInitInfo.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	imguiInitInfo.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	imguiInitInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	imguiInitInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &mSwapchainImageFormat;


	ImGui_ImplVulkan_Init(&imguiInitInfo);

	ImGui_ImplVulkan_CreateFontsTexture();

	// destroy the imgui resources on engine shutdown
	mEngineDeletionQueue.push_function([=]() {
		ImGui_ImplSDL3_Shutdown();
		ImGui_ImplVulkan_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(mLogicalDevice, imguiPool, nullptr);
	});
}


void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder swapchainBuilder{ mPhysicalDevice, mLogicalDevice, mSwapchainSurface };

	mSwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{ .format = mSwapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	mSwapchainExtent = vkbSwapchain.extent;
	//store swapchain and its related images
	mSwapchain = vkbSwapchain.swapchain;
	mSwapchainImages = vkbSwapchain.get_images().value();
	mSwapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::resize_swapchain() {
	vkDeviceWaitIdle(mLogicalDevice);

	destroy_swapchain();

	int width, height;
	SDL_GetWindowSize(mWindow, &width, &height);
	mWindowExtent.width = width;
	mWindowExtent.height = height;

	create_swapchain(mWindowExtent.width, mWindowExtent.height);

	mSwapchainResizeRequested = false;

}

void VulkanEngine::destroy_swapchain()
{
	vkDestroySwapchainKHR(mLogicalDevice, mSwapchain, nullptr);

	// destroy swapchain resources
	for (int i = 0; i < mSwapchainImageViews.size(); i++) {

		vkDestroyImageView(mLogicalDevice, mSwapchainImageViews[i], nullptr);
	}
}


void VulkanEngine::draw()
{
	// wait until the gpu has finished rendering the previous frame using the same resources
	VK_CHECK(vkWaitForFences(mLogicalDevice, 1, &get_current_frame().mRenderFence, true, 1000000000));
	// re-block the fence so that future frames using these resources need to wait for this frame to finish rendering first
	VK_CHECK(vkResetFences(mLogicalDevice, 1, &get_current_frame().mRenderFence));
	// reset rendering resources 
	get_current_frame().mDeletionQueue.flush();
	get_current_frame().mFrameDescriptors.clear_pools(mLogicalDevice);

	// max resolution of the draw on screen is capped by the swap chain resolution and image buffer resolution
	// however, we could render at an even higher resolution (render scale) then just downsample right before screen display
	mDrawExtent.height = std::min(mSwapchainExtent.height, mDrawImage.imageExtent.height) * RENDER_SCALE;
	mDrawExtent.width = std::min(mSwapchainExtent.width, mDrawImage.imageExtent.width) * RENDER_SCALE;

	// request an image from the swapchain
	uint32_t swapchainImageIndex;
	VkResult acquireImageResult = vkAcquireNextImageKHR(mLogicalDevice, mSwapchain, 1000000000, get_current_frame().mSwapchainSemaphore, nullptr, &swapchainImageIndex);
	if (acquireImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
		// our swapchain image resolution does not match the window resolution
		// Don't waste time rendering until after the swapchain image resolution is fixed
		mSwapchainResizeRequested = true;
		return;
	}

	VkCommandBuffer frameDrawCommandBuffer = get_current_frame().mMainCommandBuffer;
	// reset the command buffer to allow recording again.
	VK_CHECK(vkResetCommandBuffer(frameDrawCommandBuffer, 0));

	// begin the command buffer recording. We will use this command buffer exactly once and let vulkan know that for optimization purposes
	VkCommandBufferBeginInfo frameDrawBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VK_CHECK(vkBeginCommandBuffer(frameDrawCommandBuffer, &frameDrawBeginInfo));

	// set the draw image layout to optimal transfer destination for clearing
	vkutil::transition_image(frameDrawCommandBuffer, mDrawImage.image, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	clear_scene(frameDrawCommandBuffer, mDrawImage.image);

	// transition the draw image layout into an optimal transfer source and the swapchain image into an optimal transfer destination 
	// then blit from the draw image into the swapchain image
	vkutil::transition_image(frameDrawCommandBuffer, mDrawImage.image, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::transition_image(frameDrawCommandBuffer, mSwapchainImages[swapchainImageIndex], 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	vkutil::copy_image_to_image(frameDrawCommandBuffer, mDrawImage.image, mSwapchainImages[swapchainImageIndex], mDrawExtent, mSwapchainExtent);

	// set swapchain image layout to color attachment so IMGUI can write over it
	vkutil::transition_image(frameDrawCommandBuffer, mSwapchainImages[swapchainImageIndex], 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// draw imgui into the swapchain image
	draw_imgui(frameDrawCommandBuffer, mSwapchainImageViews[swapchainImageIndex]);

	// set swapchain image layout to present so the swapchain can present it
	vkutil::transition_image(frameDrawCommandBuffer, mSwapchainImages[swapchainImageIndex], 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(frameDrawCommandBuffer));

	// prepare the rendering command buffer submission to the queue. 
	// rendering waits on the mPresentSemaphore, which signals when the swapchain has finished presenting the previous frame using this resource (and thus we can render on it)
	// rendering signals the mRenderSemaphore, to tell the swapchain that rendering has finished and we can present the new frame on this resource
	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(frameDrawCommandBuffer);
	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame().mSwapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().mRenderSemaphore);

	// submit the rendering command buffer to the queue and execute it.
	// mRenderFence will now block until all the submitted rendering commands finish
	VkSubmitInfo2 submit = vkinit::queue_submit_info(&cmdinfo, &signalInfo, &waitInfo);
	VK_CHECK(vkQueueSubmit2(mGraphicsQueue, 1, &submit, get_current_frame().mRenderFence));

	// prepare image presentation to the window
	// we wait on mRenderSemaphore as rendering commands must have finished before the image can be displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &mSwapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &get_current_frame().mRenderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(mGraphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
		// the swapchain image is already rendered but it doesn't match the window resolution
		// our image is thrown away but that's ok; just fix the swapchain image resolution before rendering the next frame
		mSwapchainResizeRequested = true;
	}

	// move on to next set of frame resources
	mCurrentFrameNumber = (mCurrentFrameNumber + 1) % FRAMES_IN_FLIGHT;
}

void VulkanEngine::clear_scene(VkCommandBuffer cmd, VkImage targetImage) {
	// clear screen to black; you probably don't need this if you're writing to all pixels of the screen every frame
	VkClearColorValue clearColor = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
	vkCmdClearColorImage(cmd, targetImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::rendering_info(mSwapchainExtent, &colorAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);

	// Allow the IMGUI UI to be dragged out of the render window
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}