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

void HelloVulkan::createComputePipeline_Spatial()
{
  // Creating the Pipeline Layout
  VkPushConstantRange pushConstantRanges = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(PushConstantComputeSpatial)};

  std::array<VkDescriptorSetLayout, 2> descSetLayouts;
  descSetLayouts[0] = m_ReStirDescSetLayout;
  descSetLayouts[1] = m_GbufferDescSetLayout;
  //   descSetLayouts[2] = m_rtDescSetLayout;

  VkPipelineLayoutCreateInfo createInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  createInfo.setLayoutCount         = descSetLayouts.size();
  createInfo.pSetLayouts            = descSetLayouts.data();
  createInfo.pushConstantRangeCount = 1;
  createInfo.pPushConstantRanges    = &pushConstantRanges;
  vkCreatePipelineLayout(m_device, &createInfo, nullptr, &m_SpatialComputePipeLayout);

  // Shader loading and pipeline creation
  VkShaderModule computeModule =
      nvvk::createShaderModule(m_device, nvh::loadFile("spv/cs_SpatialReuse.comp.spv", true, defaultSearchPaths, true));

  // Describes the entrypoint and the stage to use for this shader module in the pipeline
  VkPipelineShaderStageCreateInfo shaderStageCreateInfo{.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                        .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
                                                        .module = computeModule,
                                                        .pName  = "main"};

  // Create the compute pipeline
  VkComputePipelineCreateInfo pipelineCreateInfo{.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                 .stage  = shaderStageCreateInfo,
                                                 .layout = m_SpatialComputePipeLayout};

  NVVK_CHECK(vkCreateComputePipelines(m_device,                     // Device
                                      VK_NULL_HANDLE,               // Pipeline cache (uses default)
                                      1, &pipelineCreateInfo,       // Compute pipeline create info
                                      nullptr,                      // Allocator (uses default)
                                      &m_SpatialComputePipeLine));  // Output

  vkDestroyShaderModule(m_device, computeModule, nullptr);
}

void HelloVulkan::computeSpatial(const VkCommandBuffer& cmdBuf)
{
  // Bind the compute shader pipeline
  vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_SpatialComputePipeLine);

  // Bind the descriptor set
  std::array<VkDescriptorSet, 2> descSets;
  descSets[0] = m_ReStirDescSet;
  descSets[1] = m_GbufferDescSet;
  //   descSets[2] = m_rtDescSet;

  vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_SpatialComputePipeLayout, 0, descSets.size(),
                          descSets.data(), 0, nullptr);

  vkCmdPushConstants(cmdBuf, m_SpatialComputePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(PushConstantComputeSpatial), &m_pcSpatial);

  // Run the compute shader with enough workgroups to cover the entire buffer
  vkCmdDispatch(cmdBuf, (m_size.width + CS_WORK_GROUP_SIZE_X - 1) / CS_WORK_GROUP_SIZE_X,
                (m_size.height + CS_WORK_GROUP_SIZE_Y - 1) / CS_WORK_GROUP_SIZE_Y, 1);
}