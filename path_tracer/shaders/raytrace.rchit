#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

#include "raycommon.glsl"
#include "wavefront.glsl"
#include"sampling.glsl"
hitAttributeEXT vec2 attribs;

// clang-format off
layout(location = 0) rayPayloadInEXT hitPayload rpd;
layout(location = 1) rayPayloadEXT bool isShadowed;

layout(buffer_reference, scalar) buffer Vertices {Vertex v[]; }; // Positions of an object
layout(buffer_reference, scalar) buffer Indices {ivec3 i[]; }; // Triangle indices
layout(buffer_reference, scalar) buffer Materials {WaveFrontMaterial m[]; }; // Array of all materials on an object
layout(buffer_reference, scalar) buffer MatIndices {int i[]; }; // Material ID for each triangle
layout(buffer_reference, scalar) buffer EmitterTriangles{ EmitTriangle i[];}; // Array of emissive triangles
layout(buffer_reference, scalar) buffer EmitterPrefixSum{ float i[];}; // prefix sum of the emitter triangles

layout(set = 0, binding = eTlas) uniform accelerationStructureEXT topLevelAS;
layout(set = 1, binding = eObjDescs, scalar) buffer ObjDesc_ { ObjDesc i[]; } objDesc;
layout(set = 1, binding = eTextures) uniform sampler2D textureSamplers[];
layout(set=2,binding=eReservoirCur,scalar) buffer ReservoirCur_{Reservoir current[];};

layout(push_constant) uniform _PushConstantRay { PushConstantRay pcRay; };
// clang-format on

uint binarySearchEmitFace(float u)
{
    u = clamp(u, 0.0f, 1.0-EPSILON);
    EmitterPrefixSum presum_=EmitterPrefixSum(pcRay.emitterPrefixSumAddress);

    // find the first emissive triangle(idx=i-1) in etris_ such that presum_[i]>=u
    float threshold = 1.0*u;
    uint lt = 0, rt = pcRay.emitterTriangleNum - 1;

    while (lt < rt)
    {
        uint mid = (lt + rt) / 2;
        if (presum_.i[mid + 1] < threshold)
        {
            lt = mid + 1;
        }
        else
        {
            rt = mid;
        }
    }

    return lt;
}

float getLuminance(vec3 radiance_rgb)
{
  return (0.2126 * radiance_rgb[0] + 0.7152 * radiance_rgb[1] + 0.0722 * radiance_rgb[2]);
}

vec3 getDiffuseColor( WaveFrontMaterial mtl,int txtOffset,vec2 texCoord)
{
  if(mtl.textureId >= 0)
  {
    uint txtId    = mtl.textureId + txtOffset;
    return texture(textureSamplers[nonuniformEXT(txtId)], texCoord).xyz;
  }
  else{
    return mtl.diffuse;
  }
}


void main()
{
  // Object data
  ObjDesc    objResource = objDesc.i[gl_InstanceCustomIndexEXT];
  MatIndices matIndices  = MatIndices(objResource.materialIndexAddress);
  Materials  materials   = Materials(objResource.materialAddress);
  Indices    indices     = Indices(objResource.indexAddress);
  Vertices   vertices    = Vertices(objResource.vertexAddress);
  EmitterTriangles emitters=EmitterTriangles(pcRay.emitterTrianglesAddress);

  // Indices of the triangle
  ivec3 ind = indices.i[gl_PrimitiveID];
  int mtlidx= matIndices.i[gl_PrimitiveID];
  WaveFrontMaterial mtl=materials.m[mtlidx];

  // Vertex of the triangle
  Vertex v0 = vertices.v[ind.x];
  Vertex v1 = vertices.v[ind.y];
  Vertex v2 = vertices.v[ind.z];

  const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  // Computing attributes of the hit position
  const vec3 pos      = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
  vec3 worldPos = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));  // Transforming the position to world space

  const vec3 nrm      = v0.nrm * barycentrics.x + v1.nrm * barycentrics.y + v2.nrm * barycentrics.z;
  vec3 worldNrm = normalize(vec3(nrm * gl_WorldToObjectEXT));  // Transforming the normal to world space
  worldNrm=faceforward(worldNrm, rpd.ray.direction,worldNrm);

  vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
  vec3 albedo=getDiffuseColor(mtl,objResource.txtOffset,texCoord);
  
  
  // adjust the start point of the next ray
  rpd.ray.origin=worldPos+worldNrm*0.001;

  //------------------------------------//
  //             emission               //
  //------------------------------------//
  // for the (1)First-depth, hit the (2)Front face of an Emitter, then get its emission
  rpd.emit_radiance=vec3(0.f);
  if(rpd.depth==0&&dot(rpd.ray.direction,worldNrm)<0.f)
    rpd.emit_radiance=mtl.emission;


  vec3 light_pos=vec3(0.f);
  vec3 light_norm=vec3(0.f);
  vec3 light_radiance=vec3(0.f);

  if(pcRay.algo_type==RIS||pcRay.algo_type==RIS_spatial_reuse||pcRay.algo_type==RIS_spatiotemporal_reuse){
    Reservoir cur_r=current[gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x + gl_LaunchIDEXT.x];
    LightSample lightSample=cur_r.keptSample;
    light_pos=lightSample.lightPos;
    light_norm = lightSample.lightNrm;
    light_radiance=lightSample.radiance;
    float weighted_pdf=1.0/cur_r.targetPdf*cur_r.totalWeight/cur_r.sampleNum;

    vec3 shadow_ray_dir=light_pos-worldPos;
    float light_dist=length(shadow_ray_dir);
    shadow_ray_dir=normalize(shadow_ray_dir);

    float lightNdotL=max(dot(light_norm,-shadow_ray_dir),0);
    float srcNdotL=max(dot(worldNrm,shadow_ray_dir),0);

    rpd.direct_radiance=vec3(0.f);
    float G=lightNdotL*srcNdotL/(light_dist*light_dist);
    vec3 bsdf=albedo/PI;
    rpd.direct_radiance=bsdf*G*light_radiance*weighted_pdf;


  }
  else if(pcRay.algo_type==NEE||pcRay.algo_type==NEE_temporal_reuse)
  {
    //------------------------------------//
    //      direct light sampling         //
    //------------------------------------//

    // Sampling a light point with pdf(A)=pointWeight/totalWeight
    uint emitTriangleID=binarySearchEmitFace(rnd(rpd.seed));
    EmitTriangle emitTriangle=emitters.i[emitTriangleID];

    float u1=rnd(rpd.seed);
    float u2=rnd(rpd.seed);
    if(u1+u2>1.0){
      u1=1-u1;
      u2=1-u2;
    }

    light_pos = (1 - u1 - u2) * emitTriangle.m_vpos[0] + u1 *emitTriangle.m_vpos[1] + u2 * emitTriangle.m_vpos[2];
    light_norm = emitTriangle.m_normal;
    light_radiance=emitTriangle.m_radiance;

    vec3 shadow_ray_dir=light_pos-worldPos;
    float light_dist=length(shadow_ray_dir);
    shadow_ray_dir=normalize(shadow_ray_dir);

    float lightNdotL=max(dot(light_norm,-shadow_ray_dir),0);
    float srcNdotL=max(dot(worldNrm,shadow_ray_dir),0);

    rpd.direct_radiance=vec3(0.f);
    if(light_dist>EPSILON&&lightNdotL>0&&srcNdotL>0){ 

      // shadow ray test
      rayQueryEXT rayQuery;// First, initialize a ray query object
      rayQueryInitializeEXT(rayQuery,              // Ray query
                            topLevelAS,            // Top-level acceleration structure
                            gl_RayFlagsOpaqueEXT,  // Ray flags, here saying "treat all geometry as opaque"
                            0xFF,                  // 8-bit instance mask, here saying "trace against all instances"
                            rpd.ray.origin,             // Ray origin
                            0.0,                   // Minimum t-value
                            shadow_ray_dir,          // Ray direction
                            float(INF_T));              // Maximum t-value
      
      while(rayQueryProceedEXT(rayQuery));

      float tHit =float(INF_T);
      if(rayQueryGetIntersectionTypeEXT(rayQuery, true)!= gl_RayQueryCommittedIntersectionNoneEXT){
        tHit = rayQueryGetIntersectionTEXT(rayQuery, true);
      }

      // direct light radiance
      if(abs(tHit-light_dist)<0.1){
        // L_dir=(bsdf*G*Li)/pdf(A)
        float G=lightNdotL*srcNdotL/(light_dist*light_dist);
        float inv_pdf_A=pcRay.emitterTotalWeight/getLuminance(light_radiance);
        vec3 bsdf=albedo/PI;

        rpd.direct_radiance=bsdf*G*light_radiance*inv_pdf_A;
      }
    }

    //------------------------------------//
    //      Indirect light sampling       //
    //------------------------------------//

    // sampling bsdf with cos-weighted pdf
    vec3 tangent, bitangent;
    createCoordinateSystem(worldNrm, tangent, bitangent);
    rpd.ray.direction = samplingHemisphere(rpd.seed, tangent, bitangent, worldNrm);

    // const float cos_theta = dot(rpd.ray.direction, worldNrm);
    // const float pdf = cos_theta / PI;
    rpd.indirect_weight=albedo;// Lambertian Reflection Model: bsdf*cos/pdf=albedo
  }

}
