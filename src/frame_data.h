#pragma once

#include <volk.h>
#include "deletion_queue.h"
#include "vk_descriptors.h"

struct FrameData {
	VkCommandPool mCommandPool;
	VkCommandBuffer mMainCommandBuffer;
	VkSemaphore mSwapchainSemaphore, mRenderSemaphore;
	VkFence mRenderFence;
	DeletionQueue mDeletionQueue;
	DescriptorAllocatorGrowable mFrameDescriptors;
};