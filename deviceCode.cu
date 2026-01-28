//
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "GBuffer.cuh"
#include "shadingTypes.h"
#include "utils.cuh"
#include "lighting.cuh"

#include <OptiXToolkit/ShaderUtil/color.h>
#include <OptiXToolkit/ShaderUtil/vec_math.h>

#include <optix.h>

#include <cfloat>

extern "C" {
    extern __constant__ Params params;
}



// Note: we have both primary and shadow ray types, but only primary rays use CH so the 
// stride between SBT entries is 1, not 2
constexpr unsigned int SBT_STRIDE = 1;

__device__ inline
void makeCameraRay( const uint2& idx, const uint2& dims, const float2& subpixel_jitter, float3& out_ray_origin, float3& out_ray_direction )
{
    float2 d       = ( ( make_float2( idx.x, idx.y ) + subpixel_jitter ) / make_float2( dims.x, dims.y ) ) * 2.f - 1.f;
    out_ray_origin = params.eye;
    out_ray_direction = normalize( d.x * params.U + d.y * params.V + params.W );
}

__device__ inline
void traceRadiance( OptixTraversableHandle handle,
                    float3                 rayOrigin,
                    float3                 rayDirection,
                    unsigned&              seed,
                    float3&                pathWeight,
                    float3&                radiance,
                    float3&                nextDir,
                    float&                 hitT,
                    float3&                hitP,
                    unsigned               bounce )
{
    // Payload layout:
    // u0: bounce count
    // u1,u2,u3: path weight (packed as float3)
    // u4: seed
    // u5: next ray direction (packed)
    // u6: hitT
    // u7,u8,u9: hit point (packed as float3)
    // u10,u11,u12: radiance contribution
    unsigned u0 = bounce;
    unsigned u1 = __float_as_uint( pathWeight.x );
    unsigned u2 = __float_as_uint( pathWeight.y );
    unsigned u3 = __float_as_uint( pathWeight.z );
    unsigned u4 = seed;
    unsigned u5 = 0;  // out: next ray direction
    unsigned u6 = 0;  // out: hitT
    unsigned u7 = 0;  // out: hitP.x
    unsigned u8 = 0;  // out: hitP.y
    unsigned u9 = 0;  // out: hitP.z
    unsigned u10 = 0; // out: radiance.x
    unsigned u11 = 0; // out: radiance.y
    unsigned u12 = 0; // out: radiance.z

    optixTrace( handle, rayOrigin, rayDirection,
                0.0f,     // tmin
                FLT_MAX,  // tmax
                0.0f,     // time
                OptixVisibilityMask( 1 ), OPTIX_RAY_FLAG_DISABLE_ANYHIT,
                0,        // SBT offset
                SBT_STRIDE,
                RAY_TYPE_RADIANCE,
                u0, u1, u2, u3, u4, u5, u6, u7, u8, u9, u10, u11, u12 );

    pathWeight = make_float3( __uint_as_float( u1 ), __uint_as_float( u2 ), __uint_as_float( u3 ) );
    seed = u4;
    nextDir = unpackNormalizedVector( u5 );
    hitT = __uint_as_float( u6 );
    hitP = make_float3( __uint_as_float( u7 ), __uint_as_float( u8 ), __uint_as_float( u9 ) );
    radiance = make_float3( __uint_as_float( u10 ), __uint_as_float( u11 ), __uint_as_float( u12 ) );
}


extern "C" __global__
void __raygen__pinhole()
{
    const uint2 idx = make_uint2( optixGetLaunchIndex() );
    const uint2 dims = make_uint2( optixGetLaunchDimensions() );

    unsigned int image_index = dims.x * idx.y + idx.x;
    unsigned int seed = tea<16>( image_index, params.frame_index );
    float3 rayOrigin, rayDirection;

    float3 result = make_float3( 0.f );      // Radiance accumulator, starts at black
    float3 pathWeight = make_float3( 1.f );  // Path throughput, starts at 1

    makeCameraRay( idx, dims, params.jitter, rayOrigin, rayDirection );

    float3 radiance;  // Radiance from current bounce
    float3 nextDir;
    float hitT;
    float3 hitP;

    // Loop for bounces
    constexpr unsigned MAX_BOUNCES = 2;
    for( unsigned bounce = 0; bounce < MAX_BOUNCES; ++bounce )
    {
        traceRadiance( params.handle, rayOrigin, rayDirection, seed, pathWeight, radiance, nextDir, hitT, hitP, bounce );

        result += radiance;

        if( isinf( hitT ) )  // Ray hit the environment, path terminates
            break;

        // Update ray for next bounce
        rayOrigin = hitP;  // Use the hit point from payload instead of computing it
        rayDirection = nextDir;

        if( pathWeight.x == 0.f && pathWeight.y == 0.f && pathWeight.z == 0.f )
            break;
    }

    // Use the final accumulated radiance as our result
    gbuffer::write( make_float4( result, 1.0f ), params.aovColor, idx );
}

extern "C" __global__ 
void __miss__radiance()
{
    // Get bounce count
    const unsigned bounce = optixGetPayload_0();

    // Get the accumulated path weight so far
    float3 pathWeight = make_float3(
        __uint_as_float( optixGetPayload_1() ),
        __uint_as_float( optixGetPayload_2() ),
        __uint_as_float( optixGetPayload_3() )
    );

    const float3 rayDir = optixGetWorldRayDirection();

    // For primary rays, sample the full environment light so we see the sun.
    // For secondary bounces, we only want the ambient contribution, since the
    // direct sun contribution was already handled by Next Event Estimation.
    const float3 missContribution = (bounce == 0) ? environmentLight( rayDir, params.envLight ) : params.envLight.baseColor;
    float3 radiance = pathWeight * missContribution;

    // Set the radiance contribution for the raygen program to accumulate
    optixSetPayload_10( __float_as_uint( radiance.x ) );
    optixSetPayload_11( __float_as_uint( radiance.y ) );
    optixSetPayload_12( __float_as_uint( radiance.z ) );

    // The path terminates here, so the next pathWeight is 0.
    optixSetPayload_1( __float_as_uint( 0.f ) );
    optixSetPayload_2( __float_as_uint( 0.f ) );
    optixSetPayload_3( __float_as_uint( 0.f ) );
    optixSetPayload_6( __float_as_uint( std::numeric_limits<float>::infinity() ) );  // hitT

    // Only write AOVs on first bounce
    if( bounce == 0 )
    {
        // write AOVs
        const uint2 idx = make_uint2( optixGetLaunchIndex() );
        gbuffer::write( std::numeric_limits<float>::infinity(), params.aovDepth, idx );

        const float4 albedo{ 0.f, 0.f, 0.f, 0.f };
        const float4 normal{ 0.f, 0.f, 0.f, 0.f };
        const float4 specular{ 0.f, 0.f, 0.f, 0.f };

        gbuffer::write( normal, params.aovNormals, idx );
        gbuffer::write( albedo, params.aovAlbedo, idx );
        gbuffer::write( specular, params.aovSpecular, idx );
        gbuffer::write( 0.0f, params.aovRoughness, idx );
        gbuffer::write( std::numeric_limits<float>::infinity(), params.aovSpecularHitT, idx );

        const uint2  dims            = make_uint2( optixGetLaunchDimensions() );
        unsigned int linearIdx       = dims.x * idx.y + idx.x;
        params.hit_buffer[linearIdx] = HitResult{};
    }
}
