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

#pragma once

#include "GBuffer.h"

#include "shadingTypes.h"

#include <OptiXToolkit/ShaderUtil/Matrix.h>

// Forward declarations needed for DlssDenoiser implementation
struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

// DLSS quality modes, ordered from highest quality (DLAA) to lowest (UltraPerformance)
enum class DlssQualityMode {
    DLAA,            // DLAA is like Quality but without upscaling
    MaxQuality,      // Best quality with upscaling
    Balanced,        // Balanced quality/performance
    MaxPerf,         // Better performance
    UltraPerformance // Best performance
};

// Post-processing parameters for DLSS artifact correction
struct PostProcessParams {
    const otk::Matrix4x4 viewMatrix;
    const otk::Matrix4x4 projMatrix;
    const EnvLight envLight;
};

class Denoiser {
    public:
        Denoiser() = default;
        virtual ~Denoiser() = default;
        virtual void denoise(GBuffer& gbuffer, int subframe, const otk::Matrix4x4& viewMatrix, const otk::Matrix4x4& projMatrix, const float2& jitter) = 0;
        virtual void postProcess(GBuffer& gbuffer, const PostProcessParams& params) = 0;
        virtual void reset() = 0;

        // Get optimal render resolution for current denoiser settings and target resolution
        virtual uint2 getOptimalRenderResolution(uint2 targetSize) const = 0;
};

class AccumulationDenoiser : public Denoiser {
    public:
        AccumulationDenoiser() = default;
        virtual ~AccumulationDenoiser() = default;
        virtual void denoise(GBuffer& gbuffer, int subframe, const otk::Matrix4x4& viewMatrix, const otk::Matrix4x4& projMatrix, const float2& jitter) override;
        virtual void postProcess(GBuffer& gbuffer, const PostProcessParams& params) override {};
        virtual void reset() override {};
        virtual uint2 getOptimalRenderResolution(uint2 targetSize) const override { return targetSize; }  // No upscaling for accumulation denoiser
};

class DlssDenoiser : public Denoiser {
    public:
        DlssDenoiser() = default;
        virtual ~DlssDenoiser();
        virtual void denoise(GBuffer& gbuffer, int subframe, const otk::Matrix4x4& viewMatrix, const otk::Matrix4x4& projMatrix, const float2& jitter) override;
        virtual void postProcess(GBuffer& gbuffer, const PostProcessParams& params) override;
        virtual void reset() override;
        virtual uint2 getOptimalRenderResolution(uint2 targetSize) const override;

        // Quality mode control
        void setQualityMode(DlssQualityMode mode);
        DlssQualityMode getQualityMode() const { return m_qualityMode; }

    private:
        void updateDlssFeature(const uint2& rendersize, const uint2& targetsize);

        mutable NVSDK_NGX_Parameter* m_ngxParams = nullptr;
        NVSDK_NGX_Handle* m_ngxHandle = nullptr;
        uint2 m_rendersize = {0, 0};
        uint2 m_targetsize = {0, 0};
        DlssQualityMode m_qualityMode = DlssQualityMode::DLAA;  // Default to DLAA
        bool m_resetHistory = false;
};

// global DLSS initialization and cleanup
void initDlss();
void shutdownDlss();
void getFeatureReqs();