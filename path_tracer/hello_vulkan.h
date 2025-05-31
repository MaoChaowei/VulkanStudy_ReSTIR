#pragma once

#include "emitter.h"
#include "nvvkhl/appbase_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvvk/memallocator_dma_vk.hpp"
#include "nvvk/resourceallocator_vk.hpp"

#include "shaders/host_device.h"

// #VKRay
#include "nvvk/raytraceKHR_vk.hpp"

struct ObjModel
{
  uint32_t     nbIndices{0};
  uint32_t     nbVertices{0};
  nvvk::Buffer vertexBuffer;    // Device buffer of all 'Vertex'
  nvvk::Buffer indexBuffer;     // Device buffer of the indices forming triangles
  nvvk::Buffer matColorBuffer;  // Device buffer of array of 'Wavefront material'
  nvvk::Buffer matIndexBuffer;  // Device buffer of array of 'Wavefront material'
};

struct ObjInstance
{
  glm::mat4 transform;    // Matrix of the instance
  uint32_t  objIndex{0};  // Model index reference
};

struct EmitterHandles
{
  nvvk::Buffer emittersBuffer;
  nvvk::Buffer emittersPrefixSumBuffer;
};


class HelloVulkan : public nvvkhl::AppBaseVk
{
public:
  void setup(const VkInstance& instance, const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t queueFamily) override;
  void loadModel(const std::string& filename, glm::mat4 transform = glm::mat4(1));
  void createTextureImages(const VkCommandBuffer& cmdBuf, const std::vector<std::string>& textures);
  void clearStatus();
  void resetFrame();
  void updateFrame();
  void onResize(int /*w*/, int /*h*/) override;
  void destroyResources();

  nvvk::ResourceAllocatorDma m_alloc;  // Allocator for buffer, images, acceleration structures
  nvvk::DebugUtil            m_debug;  // Utility to name objects


  /*============================================================================*/
  /*                           Graphic pipeline                                 */
  /*============================================================================*/
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  // void createGraphicsPipeline2();

  void updateGraphicDescriptorSet();
  void createUniformBuffer();
  void createObjDescriptionBuffer();
  void updateUniformBuffer(const VkCommandBuffer& cmdBuf);
  void rasterize(const VkCommandBuffer& cmdBuff);
  // void rasterize2(const VkCommandBuffer& cmdBuff);
  void createOffscreenRenderPass();

  // Information pushed at each draw call
  PushConstantRaster m_pcRaster{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},  // Identity matrix
      {10.f, 15.f, 8.f},                                 // light position
      0,                                                 // instance Id
      100.f,                                             // light intensity
      0                                                  // light type
  };

  // Array of objects and instances in the scene
  std::vector<ObjModel>    m_objModel;        // Model on host
  std::vector<ObjDesc>     m_objDesc;         // Model description for device access
  std::vector<ObjInstance> m_instances;       // Scene model instances
  std::vector<Emitters>    m_objEmitters;     // Scene emitters
  EmitterHandles           m_emitterHandles;  // handles to scene emitters

  // Graphic pipeline
  VkPipelineLayout m_pipelineLayout;
  VkPipeline       m_graphicsPipeline;
  // VkPipeline       m_graphicsPipeline2;

  nvvk::DescriptorSetBindings m_descSetLayoutBind;
  VkDescriptorPool            m_descPool;
  VkDescriptorSetLayout       m_descSetLayout;
  VkDescriptorSet             m_descSet;

  nvvk::Buffer m_bGlobals;  // Device-Host of the camera matrices
  nvvk::Buffer m_bObjDesc;  // Device buffer of the OBJ descriptions

  std::vector<nvvk::Texture> m_textures;  // vector of all textures of the scene

  // Attachments
  const int     m_attachmentsNum = 5;
  VkRenderPass  m_offscreenRenderPass{VK_NULL_HANDLE};
  VkFramebuffer m_offscreenFramebuffer{VK_NULL_HANDLE};
  nvvk::Texture m_offscreenColor;  // ris模式下不可以写入
  nvvk::Texture m_offscreenDepth;
  nvvk::Texture m_graphicOutColor;  // m_offscreenColor的副本，暂存fragment shader的输出
  nvvk::Texture m_gPosition, m_gNormal, m_gAlbedo;

  VkFormat m_offscreenColorFormat{VK_FORMAT_R32G32B32A32_SFLOAT};
  VkFormat m_offscreenDepthFormat{VK_FORMAT_X8_D24_UNORM_PACK32};
  VkFormat m_offscreenWorldPosFormat{VK_FORMAT_R32G32B32A32_SFLOAT};
  VkFormat m_offscreenNormFormat{VK_FORMAT_R16G16B16A16_SFLOAT};
  VkFormat m_offscreenAlbedoFormat{VK_FORMAT_R8G8B8A8_UNORM};

  /*============================================================================*/
  /*                        post Graphic pipeline                               */
  /*============================================================================*/
  // #Post - Draw the rendered image on a quad using a tonemapper
  void createPostPipeline();
  void createPostDescriptor();
  void updatePostDescriptorSet();
  void drawPost(VkCommandBuffer cmdBuf);

  nvvk::DescriptorSetBindings m_postDescSetLayoutBind;
  VkDescriptorPool            m_postDescPool{VK_NULL_HANDLE};
  VkDescriptorSetLayout       m_postDescSetLayout{VK_NULL_HANDLE};
  VkDescriptorSet             m_postDescSet{VK_NULL_HANDLE};
  VkPipeline                  m_postPipeline{VK_NULL_HANDLE};
  VkPipelineLayout            m_postPipelineLayout{VK_NULL_HANDLE};

  /*============================================================================*/
  /*                           Ray Tracing pipeline                             */
  /*============================================================================*/
  // #VKRay
  void initRayTracing();
  void findAllEmitters();
  auto objectToVkGeometryKHR(const ObjModel& model);
  void createBottomLevelAS();
  void createTopLevelAS();
  void createRtDescriptorSet();
  void updateRtDescriptorSet();
  void createRtPipeline();
  void createRtShaderBindingTable();
  void raytrace(const VkCommandBuffer& cmdBuf, const glm::vec4& clearColor);


  VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
  nvvk::RaytracingBuilderKHR                        m_rtBuilder;
  nvvk::DescriptorSetBindings                       m_rtDescSetLayoutBind;
  VkDescriptorPool                                  m_rtDescPool;
  VkDescriptorSetLayout                             m_rtDescSetLayout;
  VkDescriptorSet                                   m_rtDescSet;
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_rtShaderGroups;
  VkPipelineLayout                                  m_rtPipelineLayout;
  VkPipeline                                        m_rtPipeline;

  nvvk::Buffer                    m_rtSBTBuffer;
  VkStridedDeviceAddressRegionKHR m_rgenRegion{};
  VkStridedDeviceAddressRegionKHR m_missRegion{};
  VkStridedDeviceAddressRegionKHR m_hitRegion{};
  VkStridedDeviceAddressRegionKHR m_callRegion{};

  // Push constant for ray tracer
  PushConstantRay m_pcRay{.max_depth = 1, .spp_num = 8, .algo_type = PathTracingAlgos::NEE};


  // Record whether the settings of this frame to be rendered has been changed
  bool m_frameChange;

  /*============================================================================*/
  /*                      Compute pipeline 1: RIS                               */
  /*============================================================================*/
  void createReStir_StorageBuffer();
  void createGbuffer_DescriptorSet();
  void createReservoir_DescriptorSet();
  void updateReStir_DescriptorSet();
  void createComputePipeline_RIS();
  void computeRIS(const VkCommandBuffer& cmdBuf);


  nvvk::Buffer                m_ReStirBufferCur;
  nvvk::Buffer                m_ReStirBufferPrev;
  nvvk::DescriptorSetBindings m_ReStirDescSetLayoutBind;
  VkDescriptorPool            m_ReStirDescPool;
  VkDescriptorSetLayout       m_ReStirDescSetLayout;
  VkDescriptorSet             m_ReStirDescSet;  // 0- m_ReStirBufferCur 1- m_ReStirBufferPrev

  nvvk::DescriptorSetBindings m_GbufferDescSetLayoutBind;
  VkDescriptorPool            m_GbufferDescPool;
  VkDescriptorSetLayout       m_GbufferDescSetLayout;
  VkDescriptorSet             m_GbufferDescSet;  // 0- world position 1- normal 2- ks

  VkPipelineLayout m_RIScomputePipeLayout;
  VkPipeline       m_RIScomputePipeLine;

  PushConstantComputeRIS m_pcRIS{.CandidateNum = 32};
};
