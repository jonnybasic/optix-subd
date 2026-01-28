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

#include "DepthPassParams.h"
#include "GBuffer.cuh"

#include <OptiXToolkit/ShaderUtil/vec_math.h>

#include <optix.h>

#include <cfloat>
#include <limits>

extern "C" {
    extern __constant__ DepthPassParams params;
}

constexpr unsigned int SBT_STRIDE = 1;

__device__ inline
void makeCameraRay( const uint2& idx, const uint2& dims, const float2& subpixel_jitter, float3& out_ray_origin, float3& out_ray_direction )
{
    float2 d       = ( ( make_float2( idx.x, idx.y ) + subpixel_jitter ) / make_float2( dims.x, dims.y ) ) * 2.f - 1.f;
    out_ray_origin = params.eye;
    out_ray_direction = normalize( d.x * params.U + d.y * params.V + params.W );
}

extern "C" __global__
void __raygen__pinhole__depthpass()
{
    const uint2 idx = make_uint2( optixGetLaunchIndex() );
    const uint2 dims = make_uint2( optixGetLaunchDimensions() );

    float3 rayOrigin, rayDirection;
    makeCameraRay(idx, dims, {0.5f, 0.5f}, rayOrigin, rayDirection);

    optixTraverse(params.handle, rayOrigin, rayDirection,
                  0.0f,    // tmin
                  FLT_MAX, // tmax
                  0.0f,    // time
                  OptixVisibilityMask(1), OPTIX_RAY_FLAG_DISABLE_ANYHIT,
                  0, // SBToffset
                  SBT_STRIDE,
                  0);

    float depth = std::numeric_limits<float>::infinity();
    if (optixHitObjectIsHit())
    {  
        float t = optixHitObjectGetRayTmax();
        depth = dot(normalize(params.W), t * rayDirection);
    }

    gbuffer::write(depth, params.zbuffer, idx);
}

extern "C" __global__ 
void __miss__depthpass()
{
    // Not needed but required by the pipeline
}

extern "C" __global__
void __closesthit__depthpass()
{
    // No-op; CH disabled via ray flags, present only to satisfy validation/SBT layout
}
