#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "wavefront.glsl"
#include "host_device.h"

layout(binding = 0) uniform _GlobalUniforms
{
  GlobalUniforms uni;
};
layout(buffer_reference, scalar) buffer EmitterTriangles{ EmitTriangle i[];}; 
layout(push_constant) uniform _PushConstantRaster
{
  PushConstantRaster pcRaster;
};



void main()
{
    if(pcRaster.emitterTriangleNum>0){
      EmitterTriangles emitBuf=EmitterTriangles(pcRaster.emitterTrianglesAddress);
      uint vid   = uint(gl_VertexIndex);
      uint triId = vid / 3;
      uint corner= vid % 3;
      if(triId >= pcRaster.emitterTriangleNum) {
          gl_Position = vec4(2.0); // 丢到屏幕外
          return;
      }
      gl_Position = uni.viewProj *vec4(emitBuf.i[triId].m_vpos[corner], 1.0);
    }
    else{
      gl_Position = vec4(2.0); // 丢到屏幕外
    }

}