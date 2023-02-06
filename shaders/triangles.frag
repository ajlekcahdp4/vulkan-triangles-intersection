#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define COLOR_COUNT 4

layout (std140, binding = 0) uniform buffer {
  mat4 mvp;
  vec4 colors[COLOR_COUNT];
} uniform_buffer;

layout (location = 0) flat in vec4 color;
layout (location = 0) out vec4 outColor;

void main() {
  outColor = color;
}