#include "hello_vulkan.h"
#include "nvh/alignment.hpp"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/images_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"
#include "nvvk/shaders_vk.hpp"
#include "nvvk/buffers_vk.hpp"

extern std::vector<std::string> defaultSearchPaths;

void HelloVulkan::createComputePipeline_RIS()
{
  // Creating the Pipeline Layout
  VkPushConstantRange pushConstantRanges = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(PushConstantComputeRIS)};

  std::array<VkDescriptorSetLayout, 1> descSets;
  descSets[0] = m_ReStirDescSetLayout;

  VkPipelineLayoutCreateInfo createInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  createInfo.setLayoutCount         = descSets.size();
  createInfo.pSetLayouts            = descSets.data();
  createInfo.pushConstantRangeCount = 1;
  createInfo.pPushConstantRanges    = &pushConstantRanges;
  vkCreatePipelineLayout(m_device, &createInfo, nullptr, &m_RIScomputePipeLayout);

  // Shader loading and pipeline creation
  VkShaderModule computeModule =
      nvvk::createShaderModule(m_device, nvh::loadFile("spv/cs_ResampledIS.comp.spv", true, defaultSearchPaths, true));

  // Describes the entrypoint and the stage to use for this shader module in the pipeline
  VkPipelineShaderStageCreateInfo shaderStageCreateInfo{.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                        .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
                                                        .module = computeModule,
                                                        .pName  = "main"};

  // Create the compute pipeline
  VkComputePipelineCreateInfo pipelineCreateInfo{.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                 .stage  = shaderStageCreateInfo,
                                                 .layout = m_RIScomputePipeLayout};

  NVVK_CHECK(vkCreateComputePipelines(m_device,                 // Device
                                      VK_NULL_HANDLE,           // Pipeline cache (uses default)
                                      1, &pipelineCreateInfo,   // Compute pipeline create info
                                      nullptr,                  // Allocator (uses default)
                                      &m_RIScomputePipeLine));  // Output

  vkDestroyShaderModule(m_device, computeModule, nullptr);
}


/**
 * @brief Create 2 storage buffers containing the description of the ReSTIR structure
 * 
 */
void HelloVulkan::createReStir_StorageBuffer()
{
  size_t       reservoirStride = sizeof(Reservoir);
  size_t       reservoirCount  = m_size.width * m_size.height;  // one per pixel
  VkDeviceSize bufSize         = reservoirStride * reservoirCount;

  VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  // CS / RT read-write
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |    // 清空 / ping-pong copy
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;  // 若未来 inline RT 用地址
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size  = bufSize;
  bci.usage = usage;

  m_ReStirBufferCur  = m_alloc.createBuffer(bci,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);  // 驻 GPU，本地访问最快
  m_ReStirBufferPrev = m_alloc.createBuffer(bci, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

/**
 * @brief create and write desc set for restir SSBO
 * 
 */
void HelloVulkan::createReStir_DescriptorSet()
{
  // add binding
  m_ReStirDescSetLayoutBind.addBinding(ReSTIR::eReservoirCur, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                       VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
  m_ReStirDescSetLayoutBind.addBinding(ReSTIR::eReservoirPrev, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                       VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

  m_ReStirDescPool      = m_ReStirDescSetLayoutBind.createPool(m_device);
  m_ReStirDescSetLayout = m_ReStirDescSetLayoutBind.createLayout(m_device);

  // create desc set
  VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocateInfo.descriptorPool     = m_ReStirDescPool;
  allocateInfo.descriptorSetCount = 1;
  allocateInfo.pSetLayouts        = &m_ReStirDescSetLayout;
  vkAllocateDescriptorSets(m_device, &allocateInfo, &m_ReStirDescSet);

  // write data to desc set

  VkDescriptorBufferInfo curInfo{m_ReStirBufferCur.buffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo prevInfo{m_ReStirBufferPrev.buffer, 0, VK_WHOLE_SIZE};

  std::array<VkWriteDescriptorSet, 2> writes;
  writes[0] = m_ReStirDescSetLayoutBind.makeWrite(m_ReStirDescSet, ReSTIR::eReservoirCur, &curInfo);
  writes[1] = m_ReStirDescSetLayoutBind.makeWrite(m_ReStirDescSet, ReSTIR::eReservoirPrev, &prevInfo);
  vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void HelloVulkan::updateReStir_DescriptorSet()
{
  vkDeviceWaitIdle(m_device);  // 防止gpu正在使用旧资源
  // ─── 重新生成 ReSTIR Reservoir Buffer ───
  {
    uint32_t     pixCount  = m_size.width * m_size.height;
    uint32_t     strideGPU = sizeof(Reservoir);  // scalar 布局下 = 48
    VkDeviceSize bufSz     = strideGPU * pixCount;

    m_alloc.destroy(m_ReStirBufferPrev);
    m_alloc.destroy(m_ReStirBufferCur);

    // Device-local / Storage
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                               | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    auto createResBuf = [&](nvvk::Buffer& dst) {
      dst = m_alloc.createBuffer(bufSz, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    };
    createResBuf(m_ReStirBufferPrev);
    createResBuf(m_ReStirBufferCur);
  }

  // write data to desc set

  VkDescriptorBufferInfo curInfo{m_ReStirBufferCur.buffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo prevInfo{m_ReStirBufferPrev.buffer, 0, VK_WHOLE_SIZE};

  std::array<VkWriteDescriptorSet, 2> writes;
  writes[0] = m_ReStirDescSetLayoutBind.makeWrite(m_ReStirDescSet, ReSTIR::eReservoirCur, &curInfo);
  writes[1] = m_ReStirDescSetLayoutBind.makeWrite(m_ReStirDescSet, ReSTIR::eReservoirPrev, &prevInfo);
  vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
