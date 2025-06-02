#pragma once
#include <glm/vec3.hpp>                  // glm::vec3
#include <glm/vec4.hpp>                  // glm::vec4
#include <glm/mat4x4.hpp>                // glm::mat4
#include <glm/gtc/matrix_transform.hpp>  // glm::translate, glm::rotate, glm::scale, glm::perspective
#include <glm/gtc/constants.hpp>         // glm::pi
#include <iostream>
#include <vector>

#include "shaders/host_device.h"

/**
 * @brief takes charge of all the emittive triangles and provides corresponding sampling methods
 * 
 */
class Emitters
{
public:
  void addEmitter(const EmitTriangle& emitTriangle) { m_emitTriangles.emplace_back(emitTriangle); }

  void clear()
  {
    m_emitTriangles.clear();
    m_preSum.clear();
  }

  static float calculateArea(const EmitTriangle& t)
  {
    return 0.5 * glm::length(glm::cross(t.m_vpos[1] - t.m_vpos[0], t.m_vpos[2] - t.m_vpos[0]));
  }

  static float getLuminance(const glm::vec3 radiance_rgb)
  {
    return (0.2126 * radiance_rgb[0] + 0.7152 * radiance_rgb[1] + 0.0722 * radiance_rgb[2]);
  }

  float getWeight(const EmitTriangle& t) { return calculateArea(t) * getLuminance(t.m_radiance); }


  void calculatePreSum()
  {
    int num = m_emitTriangles.size();
    m_preSum.resize(num + 1);

    m_preSum[0] = 0;
    for(int i = 0; i < num; ++i)
    {
      m_preSum[i + 1] = m_preSum[i] + getWeight(m_emitTriangles[i]);
    }
    m_totalWeight = m_preSum[num];

    for(int i = 0; i < num; ++i)
    {
      m_preSum[i + 1] /= m_totalWeight;
    }
    assert(abs(m_preSum[num] - 1.0) < 0.001);
  }

public:
  std::vector<float> m_preSum;  // prefix sum of radiance_rgb*area. the i-th number in presum_ means the sum of weights in etris_[0~i-1]
  std::vector<EmitTriangle> m_emitTriangles;
  float                     m_totalWeight;

  static Emitters& Singleton()
  {
    static Emitters singleton;
    return singleton;
  }
};

#define SceneEmitters Emitters::Singleton()