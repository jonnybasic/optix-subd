#ifndef LIGHTING_CUH
#define LIGHTING_CUH

#include "shadingTypes.h"
#include <OptiXToolkit/ShaderUtil/vec_math.h>
#include "utils.cuh"

//------------------------------------------------------------------------------
// BRDF Functions
//------------------------------------------------------------------------------


__device__ __inline__ float roughnessToShininess( const float roughness )
{
    // Clamp roughness to avoid singularity and fireflies from near-perfect specular.
    const float r = fmaxf( roughness, 0.025f );
    return fmaxf( 2.f, 2.f / ( r * r ) - 2.f );
}

__device__ __inline__ float3 evaluateLambertian( const float3& baseColor )
{
    return baseColor / M_PIf;
}

__device__ __inline__ float evaluateBlinnPhong( const float shininess, const float NdotH )
{
    return ( shininess + 2.f ) / ( 2.f * M_PIf ) * powf( NdotH, shininess );
}

// Schlick's approximation for Fresnel reflectance
__device__ __inline__ float3 fresnelSchlick( const float3 F0, const float cos_theta )
{
    // If F0 is black, there is no specular component.
    if( F0.x == 0.f && F0.y == 0.f && F0.z == 0.f)
        return make_float3(0.f);
    return F0 + ( make_float3( 1.f ) - F0 ) * powf( fmaxf( 0.f, 1.f - cos_theta ), 5.f );
}

// Importance sample Blinn-Phong distribution
__device__ __inline__ float3 sampleBlinnPhong( const float3 N, const float3 V, const float shininess, const float2 u, float& pdf )
{
    const Onb onb{ N };

    // sample halfway vector H
    const float cos_theta = powf( u.y, 1.f / ( shininess + 1.f ) );
    const float sin_theta = sqrtf( fmaxf( 0.f, 1.f - cos_theta * cos_theta ) );
    const float phi       = 2.f * M_PIf * u.x;
    const float3 H        = onb.toWorld( make_float3( cosf( phi ) * sin_theta, sinf( phi ) * sin_theta, cos_theta ) );

    // reflect V about H
    const float3 L = 2.f * dot( V, H ) * H - V;

    // compute pdf
    const float VdotH = fmaxf( 0.f, dot( V, H ) );
    if( VdotH > 1e-6f )
    {
        const float pdf_H = ( shininess + 1.f ) / ( 2.f * M_PIf ) * powf( cos_theta, shininess );
        pdf               = pdf_H / ( 4.f * VdotH );
    }
    else
    {
        pdf = 0.f;
    }

    return normalize(L);
}

// Importance sample Lambertian
__device__ __inline__ float3 sampleLambertian( const float3 N, const float2 u )
{
    const Onb onb{ N };
    float3    dir = cosineSampleHemisphere( u );
    dir = onb.toWorld( dir );
    return normalize(dir);
}


//------------------------------------------------------------------------------
// Lighting Functions
//------------------------------------------------------------------------------


// Calculates the radiance from the sun highlight in a given direction.
__device__ __inline__ float3 sunRadiance( const float3 L, const EnvLight& envLight )
{
    const float3 sunDir = normalize( envLight.sunDir );
    const float NdotL = dot( normalize( L ), sunDir );
    const float sharpness = envLight.sunSharpness;

    // The radiance of the sun's lobe. This is normalized so that the total power
    // of the sun is controlled by envLight.sunColor, and is independent of the sharpness.
    const float sun_lobe = (sharpness + 1.0f) / (2.0f * M_PIf) * powf( fmaxf( 0.f, NdotL ), sharpness );
 
    return envLight.sunColor * sun_lobe;
}

// Samples the sun's glossy lobe and returns the light direction.
__device__ __inline__ float3 sampleSunLobe( const float2 u, const EnvLight& envLight )
{
    const Onb onb(normalize(envLight.sunDir));
    const float sharpness = envLight.sunSharpness;
    
    const float cos_theta = powf(u.y, 1.f / (sharpness + 1.f));
    const float sin_theta = sqrtf(fmaxf(0.f, 1.f - cos_theta*cos_theta));
    const float phi = 2.f * M_PIf * u.x;
    
    const float3 L = onb.toWorld(make_float3(cosf(phi) * sin_theta, sinf(phi) * sin_theta, cos_theta));
    return L;
}

// Calculates total radiance from the environment in a given direction.
__device__ __inline__ float3 environmentLight( const float3 L, const EnvLight& envLight )
{
    return envLight.baseColor + sunRadiance( L, envLight );
}

#endif // LIGHTING_CUH 
