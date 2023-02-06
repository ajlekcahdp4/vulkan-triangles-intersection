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

#include "glm_inlcude.hpp"
#include <array>

namespace triangles {

static constexpr uint32_t c_color_count = 4;

struct ubo {
  glm::mat4 vp;

  std::array<glm::vec4, c_color_count> colors;
};

} // namespace triangles