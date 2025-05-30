#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "wavefront.glsl"

layout(binding = 0) uniform _GlobalUniforms
{
  GlobalUniforms uni;
};
layout(push_constant) uniform _PushConstantRaster
{
  PushConstantRaster pcRaster;
};
// ps. vertex shader只能取用vertex buffer中的逐顶点数据
layout(location = 0) in vec3 i_position;
layout(location = 1) in vec3 i_normal;
layout(location = 2) in vec3 i_color;
layout(location = 3) in vec2 i_texCoord;

// 输出给fragment shader,必须对应好location
layout(location = 1) out vec3 o_worldPos;
layout(location = 2) out vec3 o_worldNrm;
layout(location = 3) out vec3 o_viewDir;
layout(location = 4) out vec2 o_texCoord;

out gl_PerVertex
{
  vec4 gl_Position;
};


void main()
{
  vec3 origin = vec3(uni.viewInverse * vec4(0, 0, 0, 1));

  o_worldPos = vec3(pcRaster.modelMatrix * vec4(i_position, 1.0));
  o_viewDir  = vec3(o_worldPos - origin);
  o_texCoord = i_texCoord;
  o_worldNrm = mat3(pcRaster.modelMatrix) * i_normal;

  gl_Position = uni.viewProj * vec4(o_worldPos, 1.0);
}
