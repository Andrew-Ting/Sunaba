#pragma once

#include <span>
#include <vector>
#include <volk.h>

struct DescriptorAllocatorGrowable {
public:
	// how quickly we try to grow newly allocated pool max set sizes when all other pools are used up
	inline static const int GROWTH_FACTOR = 2;
	// cap of max set size of an allocated pool
	inline static const int CAP_SETS_IN_POOL = 4092;

	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio;
	};

	void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
	void clear_pools(VkDevice device);
	void destroy_pools(VkDevice device);
	VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext = nullptr);

private:
	VkDescriptorPool get_descriptor_allocation_pool(VkDevice device);
	VkDescriptorPool create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

	// all pools allocated by a DescriptorAllocatorGrowable instance obey a ratio of allotted descriptors (set at initialization of DescriptorAllocatorGrowable)
	std::vector<PoolSizeRatio> mPoolRatios;
	std::vector<VkDescriptorPool> mFullPools;
	// there is only ever at most one ready pool
	// We keep it in a vector as checking the vector size is more error-safe than checking if the ready pool is set/valid or not
	std::vector<VkDescriptorPool> mReadyPools;
	// tracks the largest max set size pool we have allocated so far
	// the next allocated pool will have max set size of min(mSetsPerPool * GROWTH_FACTOR, CAP_SETS_IN_POOL)
	uint32_t mSetsPerPool;

};

struct DescriptorLayoutBuilder {

	std::vector<VkDescriptorSetLayoutBinding> bindings;

	void add_binding(uint32_t binding, VkDescriptorType type);
	void clear();
	VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};