#pragma once

#include "vk_check_macro.h"
#include "vk_descriptors.h"

void DescriptorAllocatorGrowable::init(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
    mPoolRatios.clear();

    for (auto ratio : poolRatios) {
        mPoolRatios.push_back(ratio);
    }
    
    // create the first pool we can allocate descriptors from
    VkDescriptorPool newPool = create_pool(device, maxSets, poolRatios);

    mSetsPerPool = maxSets;

    mReadyPools.push_back(newPool);
}

VkDescriptorPool DescriptorAllocatorGrowable::get_descriptor_allocation_pool(VkDevice device)
{
    VkDescriptorPool pool;
    if (mReadyPools.size() != 0) {
        pool = mReadyPools.back();
        mReadyPools.pop_back();
    }
    else {
        // we must allocate a new pool when all pools are full, with a larger number of max sets allocatable by the pool
        mSetsPerPool = mSetsPerPool * GROWTH_FACTOR;
        if (mSetsPerPool > CAP_SETS_IN_POOL) {
            mSetsPerPool = CAP_SETS_IN_POOL;
        }

        pool = create_pool(device, mSetsPerPool, mPoolRatios);

    }

    return pool;
}

VkDescriptorPool DescriptorAllocatorGrowable::create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios)
{
    std::vector<VkDescriptorPoolSize> poolSizes;
    // conservative; we assume on the worst case each allocated descriptor set contains all the specified ratio of descriptors, in which case set count * sum of ratios = # of descriptors
    // if you have a tighter bound on total number of descriptors allocated, you can use it to allocate less descriptors in the pool and be more memory optimized; this is not implemented here
    for (PoolSizeRatio ratio : poolRatios) {
        poolSizes.push_back(VkDescriptorPoolSize{
            .type = ratio.type,
            .descriptorCount = uint32_t(ratio.ratio * setCount)
        });
    }

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = setCount;
    pool_info.poolSizeCount = (uint32_t)poolSizes.size();
    pool_info.pPoolSizes = poolSizes.data();

    VkDescriptorPool newPool;
    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &newPool));
    return newPool;
}


void DescriptorAllocatorGrowable::clear_pools(VkDevice device)
{
    for (auto pool : mReadyPools) {
        vkResetDescriptorPool(device, pool, 0);
    }
    for (auto pool : mFullPools) {
        vkResetDescriptorPool(device, pool, 0);
        mReadyPools.push_back(pool);
    }
    mFullPools.clear();
}

void DescriptorAllocatorGrowable::destroy_pools(VkDevice device)
{
    for (auto pool : mReadyPools) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    mReadyPools.clear();
    for (auto pool : mFullPools) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    mFullPools.clear();
}

// get or create a pool to allocate descriptors from, allocate a descriptor set matching the parameter layout, and return the descriptor set
VkDescriptorSet DescriptorAllocatorGrowable::allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext)
{
    VkDescriptorPool poolToUse = get_descriptor_allocation_pool(device);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.pNext = pNext;
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = poolToUse;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet ds;
    VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

    // if allocation failed, try again
    // it must work this time because a failed allocation forces the creation of a new pool, which better have enough space to allocate the given layout
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {

        mFullPools.push_back(poolToUse);

        poolToUse = get_descriptor_allocation_pool(device);
        allocInfo.descriptorPool = poolToUse;

        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
    }

    mReadyPools.push_back(poolToUse);
    return ds;
}

void DescriptorLayoutBuilder::add_binding(uint32_t binding, VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding newbind{};
    newbind.binding = binding;
    newbind.descriptorCount = 1;
    newbind.descriptorType = type;

    bindings.push_back(newbind);
}

void DescriptorLayoutBuilder::clear()
{
    bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
{
    for (auto& b : bindings) {
        b.stageFlags |= shaderStages;
    }

    VkDescriptorSetLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    info.pNext = pNext;

    info.pBindings = bindings.data();
    info.bindingCount = (uint32_t)bindings.size();
    info.flags = flags;

    VkDescriptorSetLayout set;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

    return set;
}
