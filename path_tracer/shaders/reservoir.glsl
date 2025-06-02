#ifndef RESERVOIR_GLSL
#define RESERVOIR_GLSL

float getLuminance(vec3 radiance_rgb)
{
  return (0.2126 * radiance_rgb[0] + 0.7152 * radiance_rgb[1] + 0.0722 * radiance_rgb[2]);
}

// bool updateReservoir(inout Reservoir r,LightSample lightSample,float weight,float target_pdf,uint seed){

//     weight=clamp(weight,0,50);
//     r.sampleNum+=1;
//     r.totalWeight+=weight;
//     if(rnd(seed)<weight/r.totalWeight){
//         r.keptSample=lightSample;
//         // r.target_pdf=target_pdf;
//         return true;
//     }

//     return false;
// }

bool updateReservoir(inout Reservoir r,LightSample lightSample,float weight,uint seed){
    
    weight=clamp(weight,0,50);
    r.sampleNum+=1;
    r.totalWeight+=weight;
    if(rnd(seed)<weight/r.totalWeight){
        r.keptSample=lightSample;
        return true;
    }

    return false;
}

#endif