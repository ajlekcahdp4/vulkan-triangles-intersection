/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <tsimmerman.ss@phystech.edu>, <alex.rom23@mail.ru> wrote this file.  As long as you
 * retain this notice you can do whatever you want with this stuff. If we meet
 * some day, and you think this stuff is worth it, you can buy me a beer in
 * return.
 * ----------------------------------------------------------------------------
 */

#pragma once

#include <engine.hpp>

#include "misc.hpp"

#include "ezvk/debug.hpp"
#include "ezvk/instance.hpp"
#include "ezvk/memory.hpp"
#include "ezvk/queues.hpp"
#include "ezvk/shaders.hpp"
#include "ezvk/window.hpp"

#include "glfw_include.hpp"
#include "vulkan_include.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <memory>

#if defined(VK_VALIDATION_LAYER) || !defined(NDEBUG)
#define USE_DEBUG_EXTENSION
#endif

namespace triangles {

constexpr int MAX_FRAMES_IN_FLIGHT = 2;
#ifdef USE_DEBUG_EXTENSION
#define INSTANCE_TYPE ezvk::debugged_instance
#else
#define INSTANCE_TYPE ezvk::instance
#endif

class application final {
  INSTANCE_TYPE m_instance;

  vk::raii::PhysicalDevice m_phys_device = nullptr;
  ezvk::unique_glfw_window m_window = nullptr;
  ezvk::surface            m_surface;
  vk::raii::Device         m_logical_device = nullptr;

  throttle::graphics::queues            m_queues = nullptr;
  throttle::graphics::swapchain_wrapper m_swapchain_data = nullptr;

  ezvk::device_buffers m_uniform_buffers;

  throttle::graphics::descriptor_set_data                       m_descriptor_set_data = nullptr;
  throttle::graphics::pipeline_data<throttle::graphics::vertex> m_pipeline_data = nullptr;

  ezvk::framebuffers    m_framebuffers;
  vk::raii::CommandPool m_command_pool = nullptr;
  ezvk::device_buffer   m_vertex_buffer;

  vk::raii::CommandBuffers         m_command_buffers = nullptr;
  std::vector<vk::raii::Semaphore> m_image_availible_semaphores;
  std::vector<vk::raii::Semaphore> m_render_finished_semaphores;
  std::vector<vk::raii::Fence>     m_in_flight_fences;
  std::size_t                      m_curr_frame = 0;
  std::size_t                      m_verices_n = 0;

public:
  bool m_triangles_loaded = false;

  application(INSTANCE_TYPE p_instance) {
    m_instance = std::move(p_instance);

    m_phys_device = throttle::graphics::pick_physical_device(m_instance());
    m_window = {"Triangles intersection", vk::Extent2D{800, 600}, true};

    m_surface = {m_instance(), m_window};

    m_logical_device = {throttle::graphics::create_device(m_phys_device, m_surface())};
    m_queues = {m_phys_device, m_logical_device, m_surface()};

    m_swapchain_data = {m_phys_device, m_logical_device, m_surface(), m_window.extent()};
    m_uniform_buffers = {MAX_FRAMES_IN_FLIGHT, sizeof(throttle::graphics::uniform_buffer_object), m_phys_device,
                         m_logical_device, vk::BufferUsageFlagBits::eUniformBuffer};
    m_descriptor_set_data = {m_logical_device, m_uniform_buffers};

    m_pipeline_data = {m_logical_device, "shaders/vertex.spv", "shaders/fragment.spv", m_window.extent(),
                       m_descriptor_set_data};
    m_framebuffers = {m_logical_device, m_swapchain_data.image_views(), m_swapchain_data.extent(),
                      m_pipeline_data.m_render_pass};

    m_command_pool = {throttle::graphics::create_command_pool(m_logical_device, m_queues)};

    create_sync_objs();
  }

  void  shutdown() { m_logical_device.waitIdle(); }
  void  loop() { render_frame(); }
  auto *window() const { return m_window(); }

  void load_triangles(const std::vector<throttle::graphics::vertex> &vertices) {
    m_verices_n = vertices.size();
    m_vertex_buffer = {m_phys_device, m_logical_device, vk::BufferUsageFlagBits::eVertexBuffer, std::span{vertices}};
    create_command_buffers();
    m_triangles_loaded = true;
  }

private:
  void create_command_buffers() {
    vk::CommandBufferAllocateInfo alloc_info{.commandPool = *m_command_pool,
                                             .level = vk::CommandBufferLevel::ePrimary,
                                             .commandBufferCount = static_cast<uint32_t>(m_framebuffers.size())};

    m_command_buffers = vk::raii::CommandBuffers(m_logical_device, alloc_info);

    for (uint32_t i = 0; i < m_command_buffers.size(); ++i) {
      const auto &buffer = m_command_buffers[i];

      buffer.begin({.flags = vk::CommandBufferUsageFlagBits::eSimultaneousUse});

      vk::ClearValue          clear_color = {std::array<float, 4>{0.2f, 0.3f, 0.3f, 1.0f}};
      vk::RenderPassBeginInfo render_pass_info{.renderPass = *m_pipeline_data.m_render_pass,
                                               .framebuffer = *m_framebuffers[i],
                                               .renderArea = {vk::Offset2D{0, 0}, m_swapchain_data.extent()},
                                               .clearValueCount = 1,
                                               .pClearValues = &clear_color};

      buffer.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
      buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline_data.m_pipeline);
      buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_pipeline_data.m_layout, 0,
                                {*m_descriptor_set_data.m_descriptor_set}, nullptr);

      buffer.bindVertexBuffers(0, *m_vertex_buffer.buffer(), {0});
      buffer.draw(m_verices_n, 1, 0, 0);
      buffer.endRenderPass();
      buffer.end();
    }
  }

  void create_sync_objs() {
    m_image_availible_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    m_render_finished_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      m_image_availible_semaphores.push_back(m_logical_device.createSemaphore({}));
      m_render_finished_semaphores.push_back(m_logical_device.createSemaphore({}));
      m_in_flight_fences.push_back(m_logical_device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}));
    }
  }

  void recreate_swap_chain() {
    auto extent = m_window.extent();

    while (extent.width == 0 || extent.height == 0) {
      extent = m_window.extent();
      glfwWaitEvents();
    }

    const auto &old_swapchain = m_swapchain_data.swapchain();
    auto        new_swapchain =
        throttle::graphics::swapchain_wrapper(m_phys_device, m_logical_device, m_surface(), extent, *old_swapchain);

    m_logical_device.waitIdle();
    m_swapchain_data.swapchain()
        .clear(); // Destroy the old swapchain before recreating another. NOTE[Sergei]: this is a dirty fix
    m_swapchain_data = std::move(new_swapchain);

    m_pipeline_data = throttle::graphics::pipeline_data<throttle::graphics::vertex>{
        m_logical_device, "shaders/vertex.spv", "shaders/fragment.spv", m_swapchain_data.extent(),
        m_descriptor_set_data};

    m_framebuffers = {m_logical_device, m_swapchain_data.image_views(), m_swapchain_data.extent(),
                      m_pipeline_data.m_render_pass};
    create_command_buffers();
  }

  // TEMPORARY
  glm::mat4x4 create_mvpc_matrix(const vk::Extent2D &extent) {
    float fov = glm::radians(45.0f);
    if (extent.width > extent.height) fov *= static_cast<float>(extent.height) / static_cast<float>(extent.width);

    glm::mat4x4 model = glm::mat4x4(1.0f);
    glm::mat4x4 view =
        glm::lookAt(glm::vec3(-5.0f, 3.0f, -10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    glm::mat4x4 proj = glm::perspective(fov, static_cast<float>(extent.width) / extent.height, 0.1f, 100.0f);
    // clang-format off
    glm::mat4x4 clip = glm::mat4x4{ // Vulkan clip space has inverted Y and half Z
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, -1.0f, 0.0f, 0.0f,
      0.0f,  0.0f, 0.5f, 0.0f,
      0.0f,  0.0f, 0.5f, 1.0f
    };
    // clang-format on
    return clip * proj * view * model;
  }

  // NOTE: uniform buffers should be updated before the descriptors set's creation
  void update_uniform_buffers() {
    static auto start_time = std::chrono::high_resolution_clock::now();
    auto        curr_time = std::chrono::high_resolution_clock::now();
    float       time = std::chrono::duration<float, std::chrono::seconds::period>(curr_time - start_time).count();
    throttle::graphics::uniform_buffer_object ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f),
                                m_swapchain_data.extent().width / (float)m_swapchain_data.extent().height, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1; // because in OpenGl y axis is inverted
    m_uniform_buffers[m_curr_frame].copy_to_device(ubo);
  }

  // INSPIRATION: https://github.com/tilir/cpp-graduate/blob/master/10-3d/vk-simplest.cc
  void render_frame() {
    m_logical_device.waitForFences({*m_in_flight_fences[m_curr_frame]}, VK_TRUE, UINT64_MAX);

    vk::AcquireNextImageInfoKHR acquire_info = {.swapchain = *m_swapchain_data.swapchain(),
                                                .timeout = UINT64_MAX,
                                                .semaphore = *m_image_availible_semaphores[m_curr_frame],
                                                .fence = nullptr,
                                                .deviceMask = 1};

    uint32_t image_index;
    try {
      image_index = m_logical_device.acquireNextImage2KHR(acquire_info).second;
    } catch (vk::OutOfDateKHRError &) {
      recreate_swap_chain();
      return;
    }

    auto mvpc = create_mvpc_matrix(m_swapchain_data.extent());
    m_uniform_buffers[m_curr_frame].copy_to_device(mvpc);

    vk::PipelineStageFlags wait_stages = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo         submit_info = {.waitSemaphoreCount = 1,
                                          .pWaitSemaphores = std::addressof(*m_image_availible_semaphores[m_curr_frame]),
                                          .pWaitDstStageMask = std::addressof(wait_stages),
                                          .commandBufferCount = 1,
                                          .pCommandBuffers = &(*m_command_buffers[image_index]),
                                          .signalSemaphoreCount = 1,
                                          .pSignalSemaphores = std::addressof(*m_render_finished_semaphores[m_curr_frame])};

    m_logical_device.resetFences(*m_in_flight_fences[m_curr_frame]); // segfault was there
    m_queues.graphics.submit(submit_info, *m_in_flight_fences[m_curr_frame]);

    vk::PresentInfoKHR present_info = {.waitSemaphoreCount = 1,
                                       .pWaitSemaphores = std::addressof(*m_render_finished_semaphores[m_curr_frame]),
                                       .swapchainCount = 1,
                                       .pSwapchains = std::addressof(*m_swapchain_data.swapchain()),
                                       .pImageIndices = &image_index};

    vk::Result result_present;
    try {
      result_present = m_queues.present.presentKHR(present_info);
    } catch (vk::OutOfDateKHRError &) {
      result_present = vk::Result::eErrorOutOfDateKHR;
    }

    if (result_present == vk::Result::eSuboptimalKHR || result_present == vk::Result::eErrorOutOfDateKHR) {
      recreate_swap_chain();
      return;
    }

    m_curr_frame = (m_curr_frame + 1) % MAX_FRAMES_IN_FLIGHT;
  }
};

} // namespace triangles