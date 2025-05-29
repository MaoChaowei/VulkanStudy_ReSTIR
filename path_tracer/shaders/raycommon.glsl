#ifndef RAYCOMMON_HEADER
#define RAYCOMMON_HEADER

#define EPSILON 0.000001
#define INF_T 1000000
#define PI 3.14159265358979323846

struct Ray
{
  vec3 origin;
  vec3 direction;
};

struct hitPayload
{
  uint depth;
  Ray ray;
  vec3 emit_radiance;
  vec3 direct_radiance;
  vec3 indirect_weight;  // indirect coefficient

  uint seed;
};


#endif