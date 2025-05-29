/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


// ImGui - standalone example application for Glfw + Vulkan, using programmable
// pipeline If you are new to ImGui, see examples/README.txt and documentation
// at the top of imgui.cpp.

#include <array>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "imgui/imgui_helper.h"

#include "hello_vulkan.h"
#include "imgui/imgui_camera_widget.h"
#include "nvh/cameramanipulator.hpp"
#include "nvh/fileoperations.hpp"
#include "nvpsystem.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/context_vk.hpp"
#include <unordered_map>

//////////////////////////////////////////////////////////////////////////
#define UNUSED(x) (void)(x)
//////////////////////////////////////////////////////////////////////////

// Default search path for shaders
std::vector<std::string> defaultSearchPaths;


// GLFW Callback functions
static void onErrorCallback(int error, const char* description)
{
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Extra UI
void renderUI(HelloVulkan& helloVk)
{
  helloVk.m_frameChange |= ImGuiH::CameraWidget();
  if(ImGui::CollapsingHeader("Light"))
  {
    helloVk.m_frameChange |= ImGui::RadioButton("Point", &helloVk.m_pcRaster.lightType, 0);
    ImGui::SameLine();
    helloVk.m_frameChange |= ImGui::RadioButton("Infinite", &helloVk.m_pcRaster.lightType, 1);

    helloVk.m_frameChange |= ImGui::SliderFloat3("Position", &helloVk.m_pcRaster.lightPosition.x, -20.f, 20.f);
    helloVk.m_frameChange |= ImGui::SliderFloat("Intensity", &helloVk.m_pcRaster.lightIntensity, 0.f, 150.f);
  }

  if(ImGui::CollapsingHeader("PathTracer"))
  {
    helloVk.m_frameChange |= ImGui::SliderInt("SPP", &helloVk.m_pcRay.spp_num, 1, 64);
    helloVk.m_frameChange |= ImGui::SliderInt("MaxDepth", &helloVk.m_pcRay.max_depth, 1, 20);
    static const char* PathTracingAlgosNames[PathTracingAlgos::PathTracingAlgos_Count] = {
        "NEE", "NEE_temporal_reuse", "RIS", "RIS_spatial_reuse", "RIS_spatiotemporal_reuse"};

    const char* preview = PathTracingAlgosNames[helloVk.m_pcRay.algo_type];
    int         old     = helloVk.m_pcRay.algo_type;
    if(ImGui::BeginCombo("Path Tracing Algorithm", preview))
    {
      for(int i = 0; i < PathTracingAlgos_Count; ++i)
      {
        bool is_selected = (helloVk.m_pcRay.algo_type == i);
        if(ImGui::Selectable(PathTracingAlgosNames[i], is_selected))
        {
          helloVk.m_pcRay.algo_type = i;
        }
        if(is_selected)
          ImGui::SetItemDefaultFocus();  // 保持选中项高亮
      }
      ImGui::EndCombo();
    }
    helloVk.m_frameChange |= old != helloVk.m_pcRay.algo_type;
  }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static int const SAMPLE_WIDTH  = 1280;
static int const SAMPLE_HEIGHT = 720;


//--------------------------------------------------------------------------------------------------
// Application Entry
//
int main(int argc, char** argv)
{
  UNUSED(argc);

  // Setup GLFW window
  glfwSetErrorCallback(onErrorCallback);
  if(!glfwInit())
  {
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(SAMPLE_WIDTH, SAMPLE_HEIGHT, PROJECT_NAME, nullptr, nullptr);


  // Setup camera
  CameraManip.setWindowSize(SAMPLE_WIDTH, SAMPLE_HEIGHT);
  CameraManip.setLookat(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));

  // Setup Vulkan
  if(!glfwVulkanSupported())
  {
    printf("GLFW: Vulkan Not Supported\n");
    return 1;
  }

  // setup some basic things for the sample, logging file for example
  NVPSystem system(PROJECT_NAME);

  // Search path for shaders and other media
  defaultSearchPaths = {
      NVPSystem::exePath() + PROJECT_RELDIRECTORY,
      NVPSystem::exePath() + PROJECT_RELDIRECTORY "..",
      std::string(PROJECT_NAME),
  };

  // Vulkan required extensions
  assert(glfwVulkanSupported() == 1);
  uint32_t count{0};
  auto     reqExtensions = glfwGetRequiredInstanceExtensions(&count);

  // Requesting Vulkan extensions and layers
  nvvk::ContextCreateInfo contextInfo;
  contextInfo.setVersion(1, 2);                       // Using Vulkan 1.2
  for(uint32_t ext_id = 0; ext_id < count; ext_id++)  // Adding required extensions (surface, win32, linux, ..)
    contextInfo.addInstanceExtension(reqExtensions[ext_id]);
  contextInfo.addInstanceLayer("VK_LAYER_LUNARG_monitor", true);              // FPS in titlebar
  contextInfo.addInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true);  // Allow debug names
  contextInfo.addDeviceExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);            // Enabling ability to present rendering

  // #VKRay: Activate the ray tracing extension
  VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false, &accelFeature);  // To build acceleration structures
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  contextInfo.addDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, false, &rtPipelineFeature);  // To use vkCmdTraceRaysKHR
  contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);  // Required by ray tracing pipeline
  VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  contextInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false, &rayQueryFeatures);
  VkPhysicalDeviceShaderClockFeaturesKHR clockFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};
  contextInfo.addDeviceExtension(VK_KHR_SHADER_CLOCK_EXTENSION_NAME, false, &clockFeature);

  // Creating Vulkan base application
  nvvk::Context vkctx{};
  vkctx.initInstance(contextInfo);
  // Find all compatible devices
  auto compatibleDevices = vkctx.getCompatibleDevices(contextInfo);
  assert(!compatibleDevices.empty());
  // Use a compatible device
  vkctx.initDevice(compatibleDevices[0], contextInfo);

  // Create example
  HelloVulkan helloVk;

  // Window need to be opened to get the surface on which to draw
  const VkSurfaceKHR surface = helloVk.getVkSurface(vkctx.m_instance, window);
  vkctx.setGCTQueueWithPresent(surface);

  helloVk.setup(vkctx.m_instance, vkctx.m_device, vkctx.m_physicalDevice, vkctx.m_queueGCT.familyIndex);
  helloVk.createSwapchain(surface, SAMPLE_WIDTH, SAMPLE_HEIGHT);
  helloVk.createDepthBuffer();
  helloVk.createRenderPass();
  helloVk.createFrameBuffers();

  // Setup Imgui
  helloVk.initGUI(0);  // Using sub-pass 0

  // Scene Preparation
  {
    std::unordered_map<std::string, glm::mat4> trans;
    trans["buddha"] = glm::translate(glm::mat4(1.0), glm::vec3(0.39555, 1.07519, 0.44344));
    trans["buddha"] = glm::rotate(trans["buddha"], 3.14f, glm::vec3(0, 1, 0));

    trans["dragon"] = glm::translate(glm::mat4(1.0), glm::vec3(0, 1, 0));
    trans["dragon"] = glm::rotate(trans["dragon"], 3.14f * 0.7f, glm::vec3(0, 1, 0));
    trans["dragon"] = glm::scale(trans["dragon"], glm::vec3(1.2, 1.2, 1.2));

    // helloVk.loadModel(nvh::findFile("media/scenes/fireplace_room/fireplace_room.obj", defaultSearchPaths, true));
    // CameraManip.setLookat(glm::vec3(4.20767, 1.01458, -3.20028), glm::vec3(-1.06465, 1.35220, 0.32594), glm::vec3(0, 1, 0));
    helloVk.loadModel(nvh::findFile("media/scenes/CornellBox/CornellBox-Empty-Lights.obj", defaultSearchPaths, true),
                      glm::rotate(glm::mat4(1.f), 3.14f, glm::vec3(0, 1, 0)));
    helloVk.loadModel(nvh::findFile("media/scenes/CornellBox/CornellBox-Empty-CO.obj", defaultSearchPaths, true));
    // helloVk.loadModel(nvh::findFile("media/scenes/buddha/buddha.obj", defaultSearchPaths, true), trans["buddha"]);
    helloVk.loadModel(nvh::findFile("media/scenes/dragon/dragon.obj", defaultSearchPaths, true), trans["dragon"]);
    CameraManip.setLookat(glm::vec3(0.06118, 1.20128, 3.09162), glm::vec3(0.06005, 1.13624, 2.09373), glm::vec3(0, 1, 0));

    // helloVk.loadModel(nvh::findFile("media/scenes/living_room/living_room.obj", defaultSearchPaths, true));
    // CameraManip.setLookat(glm::vec3(2.08361, 1.76848, 5.29191), glm::vec3(1.55202, 1.61265, 4.45936), glm::vec3(0, 1, 0));
    helloVk.findAllEmitters();
  }

  // #VKGraphic
  helloVk.createOffscreenRender();
  helloVk.createDescriptorSetLayout();
  helloVk.createGraphicsPipeline();
  helloVk.createUniformBuffer();
  helloVk.createObjDescriptionBuffer();
  helloVk.updateGraphicDescriptorSet();

  helloVk.createGraphicsPipeline2();

  // #VKRay
  helloVk.initRayTracing();
  helloVk.createBottomLevelAS();
  helloVk.createTopLevelAS();
  helloVk.createRtDescriptorSet();  // create and write a descriptor set of out_image and TLAS
  helloVk.createRtPipeline();
  helloVk.createRtShaderBindingTable();

  // #VKpost
  helloVk.createPostDescriptor();
  helloVk.createPostPipeline();
  helloVk.updatePostDescriptorSet();


  glm::vec4 clearColor   = glm::vec4(1, 1, 1, 1.00f);
  bool      useRaytracer = true;


  helloVk.setupGlfwCallbacks(window);
  ImGui_ImplGlfw_InitForVulkan(window, true);

  // Main loop
  while(!glfwWindowShouldClose(window))
  {
    glfwPollEvents();
    if(helloVk.isMinimized())
      continue;

    // Start the Dear ImGui frame
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();


    // Show UI window.
    if(helloVk.showGui())
    {
      ImGuiH::Panel::Begin();
      helloVk.m_frameChange |= ImGui::ColorEdit3("Clear color", reinterpret_cast<float*>(&clearColor));
      helloVk.m_frameChange |= ImGui::Checkbox("Ray Tracer mode", &useRaytracer);  // Switch between raster and ray tracing

      renderUI(helloVk);

      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      ImGuiH::Control::Info("", "", "(F10) Toggle Pane", ImGuiH::Control::Flags::Disabled);
      ImGuiH::Panel::End();
    }

    // Start rendering the scene
    helloVk.prepareFrame();

    // Start command buffer of this frame
    auto                   curFrame = helloVk.getCurFrame();
    const VkCommandBuffer& cmdBuf   = helloVk.getCommandBuffers()[curFrame];

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    // Clearing screen
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};
    clearValues[1].depthStencil = {1.0f, 0};

    // Updating Per Frame Status before any pass: camera buffer, frame num...
    helloVk.updateUniformBuffer(cmdBuf);
    helloVk.updateFrame();

    // Offscreen render pass
    {
      VkRenderPassBeginInfo offscreenRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      offscreenRenderPassBeginInfo.clearValueCount = 2;
      offscreenRenderPassBeginInfo.pClearValues    = clearValues.data();
      offscreenRenderPassBeginInfo.renderPass      = helloVk.m_offscreenRenderPass;
      offscreenRenderPassBeginInfo.framebuffer     = helloVk.m_offscreenFramebuffer;
      offscreenRenderPassBeginInfo.renderArea      = {{0, 0}, helloVk.getSize()};

      // Rendering Scene
      if(useRaytracer)
      {
        helloVk.raytrace(cmdBuf, clearColor);
      }
      else
      {
        vkCmdBeginRenderPass(cmdBuf, &offscreenRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        helloVk.rasterize2(cmdBuf);
        vkCmdEndRenderPass(cmdBuf);
      }
    }

    // 2nd rendering pass: tone mapper, UI
    {
      VkRenderPassBeginInfo postRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      postRenderPassBeginInfo.clearValueCount = 2;
      postRenderPassBeginInfo.pClearValues    = clearValues.data();
      postRenderPassBeginInfo.renderPass      = helloVk.getRenderPass();
      postRenderPassBeginInfo.framebuffer     = helloVk.getFramebuffers()[curFrame];
      postRenderPassBeginInfo.renderArea      = {{0, 0}, helloVk.getSize()};

      // Rendering tonemapper
      vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
      helloVk.drawPost(cmdBuf);
      // Rendering UI
      ImGui::Render();
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
      vkCmdEndRenderPass(cmdBuf);
    }

    // Submit for display
    vkEndCommandBuffer(cmdBuf);
    helloVk.submitFrame();
  }

  // Cleanup
  vkDeviceWaitIdle(helloVk.getDevice());

  helloVk.destroyResources();
  helloVk.destroy();
  vkctx.deinit();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
