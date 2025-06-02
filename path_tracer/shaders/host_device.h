#ifndef COMMON_HOST_DEVICE
#define COMMON_HOST_DEVICE

#ifdef __cplusplus
#include <glm/glm.hpp>
// GLSL Type
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat4 = glm::mat4;
using uint = unsigned int;
#endif

// clang-format off
#ifdef __cplusplus // Descriptor binding helper for C++ and GLSL
 #define START_BINDING(a) enum a {
 #define END_BINDING() }
#else
 #define START_BINDING(a)  const uint
 #define END_BINDING() 
#endif

START_BINDING(SceneBindings)
  eGlobals  = 0,  // Global uniform containing camera matrices
  eObjDescs = 1,  // Access to the object descriptions
  eTextures = 2   // Access to textures
END_BINDING();

START_BINDING(RtxBindings)
  eTlas     = 0,  // Top-level acceleration structure
  eOutImage = 1   // Ray tracer output image
END_BINDING();

START_BINDING(GBufferBindings)
  eWorldPosition     = 0,  
  eWorldNormal       = 1,
  eMatKs             = 2,
  eComputeOutImage   = 3
END_BINDING();

START_BINDING(ReSTIRBindings)
  eReservoirCur =0,     // SSBO
  eReservoirPrev =1     // SSBO
END_BINDING();

START_BINDING(PathTracingAlgos)
  NEE                       =0,
  NEE_temporal_reuse        =1,
  RIS                       =2,
  RIS_temporal_reuse        =3,
  RIS_spatial_reuse         =4,
  RIS_spatiotemporal_reuse  =5,

  PathTracingAlgos_Count    =6
END_BINDING();

// clang-format on

// Information of a obj model when referenced in a shader
struct ObjDesc
{
  int      txtOffset;             // Texture index offset in the array of textures
  uint64_t vertexAddress;         // Address of the Vertex buffer
  uint64_t indexAddress;          // Address of the index buffer
  uint64_t materialAddress;       // Address of the material buffer
  uint64_t materialIndexAddress;  // Address of the triangle material index buffer
};

// Uniform buffer set at each frame
struct GlobalUniforms
{
  mat4 viewProj;     // Camera view * projection
  mat4 viewInverse;  // Camera inverse view matrix
  mat4 projInverse;  // Camera inverse projection matrix
};

// Push constant structure for the raster
struct PushConstantRaster
{
  mat4     modelMatrix;  // matrix of the instance
  vec3     lightPosition;
  uint     objIndex;
  float    lightIntensity;
  int      lightType;
  uint64_t emitterTrianglesAddress;
  uint64_t emitterPrefixSumAddress;
  uint     emitterTriangleNum;
};

// push constant structure for the first compute shader
struct PushConstantComputeRIS
{
  int      CandidateNum;
  uint64_t emitterTrianglesAddress;
  uint64_t emitterPrefixSumAddress;
  uint     emitterTriangleNum;
  float    emitterTotalWeight;
};

// push constant structure for the second compute shader
struct PushConstantComputeSpatial
{
  int spatial_pass_num;
  int k_neighbors_num;
};

// Push constant structure for the ray tracer
struct PushConstantRay
{
  vec4     clearColor;
  uint64_t emitterTrianglesAddress;
  uint64_t emitterPrefixSumAddress;
  uint     emitterTriangleNum;
  float    emitterTotalWeight;
  int      frame_num;
  int      restir_spp_idx;

  // SETTINGS
  int max_depth;
  int spp_num;
  int algo_type;  // PathTracingAlgos
};

struct Vertex  // See ObjLoader, copy of VertexObj, could be compressed for device
{
  vec3 pos;
  vec3 nrm;
  vec3 color;
  vec2 texCoord;
};

struct WaveFrontMaterial  // See ObjLoader, copy of MaterialObj, could be compressed for device
{
  vec3  ambient;
  vec3  diffuse;
  vec3  specular;
  vec3  transmittance;
  vec3  emission;
  float shininess;
  float ior;       // index of refraction
  float dissolve;  // 1 == opaque; 0 == fully transparent
  int   illum;     // illumination model (see http://www.fileformat.info/format/material/)
  int   textureId;
};


struct EmitTriangle
{
  vec3 m_vpos[3];   // world position of vertex
  vec3 m_normal;    // point to the front face
  vec3 m_radiance;  // radiance value for each direction
};

#define MAX_WEIGHT 100000  // max weight for Reservoir.totalWeight

struct LightSample
{
  vec3 lightPos;  // the preserved sample
  vec3 lightNrm;
  vec3 radiance;
};

struct Reservoir
{
  LightSample keptSample;
  float       totalWeight;   // current weight
  uint        sampleNum;     // seen this much of samples so far
  float       sampleWeight;  // 1/(target_pdf)*(totalWeight/sampleNum)
  float       target_pdf;
};


#define CS_WORK_GROUP_SIZE_X 8
#define CS_WORK_GROUP_SIZE_Y 8

#endif
