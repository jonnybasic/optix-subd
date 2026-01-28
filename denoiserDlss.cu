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

#include "denoiser.h"

#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_helpers_dlssd_cuda.h>

#include <OptiXToolkit/ShaderUtil/color.h>
#include <OptiXToolkit/ShaderUtil/vec_math.h>
#include "shadingTypes.h"

#include "GBuffer.cuh"
#include "lighting.cuh"

#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <array>

// Track DLSS initialization state
static bool g_dlssInitialized = false;

// Simple helper to get executable directory for DLSS initialization
static std::filesystem::path getExecutableDirectory()
{
#ifdef _WIN32
    char path[2048] = { 0 };
    if (GetModuleFileNameA(nullptr, path, std::size(path)) == 0)
        return {};
    return std::filesystem::path(path).parent_path();
#else
    // On Linux, use /proc/self/exe
    char path[2048] = { 0 };
    if (readlink("/proc/self/exe", path, std::size(path)) <= 0)
        return {};
    return std::filesystem::path(path).parent_path();
#endif
}

std::string getDlssErrorString(NVSDK_NGX_Result result)
{
    switch(result) {
        case NVSDK_NGX_Result_FAIL_FeatureNotSupported:
            return "NVSDK_NGX_Result_FAIL_FeatureNotSupported";
        case NVSDK_NGX_Result_FAIL_PlatformError:
            return "NVSDK_NGX_Result_FAIL_PlatformError";
        case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists:
            return "NVSDK_NGX_Result_FAIL_FeatureAlreadyExists";
        case NVSDK_NGX_Result_FAIL_FeatureNotFound:
            return "NVSDK_NGX_Result_FAIL_FeatureNotFound";
        case NVSDK_NGX_Result_FAIL_InvalidParameter:
            return "NVSDK_NGX_Result_FAIL_InvalidParameter";
        case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall:
            return "NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall";
        case NVSDK_NGX_Result_FAIL_NotInitialized:
            return "NVSDK_NGX_Result_FAIL_NotInitialized";
        case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat:
            return "NVSDK_NGX_Result_FAIL_UnsupportedInputFormat";
        case NVSDK_NGX_Result_FAIL_RWFlagMissing:
            return "NVSDK_NGX_Result_FAIL_RWFlagMissing";
        case NVSDK_NGX_Result_FAIL_MissingInput:
            return "NVSDK_NGX_Result_FAIL_MissingInput";
        case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature:
            return "NVSDK_NGX_Result_FAIL_UnableToInitializeFeature";
        case NVSDK_NGX_Result_FAIL_OutOfDate:
            return "NVSDK_NGX_Result_FAIL_OutOfDate";
        case NVSDK_NGX_Result_FAIL_OutOfGPUMemory:
            return "NVSDK_NGX_Result_FAIL_OutOfGPUMemory";
        case NVSDK_NGX_Result_FAIL_UnsupportedFormat:
            return "NVSDK_NGX_Result_FAIL_UnsupportedFormat";
        case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath:
            return "NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath";
        case NVSDK_NGX_Result_FAIL_UnsupportedParameter:
            return "NVSDK_NGX_Result_FAIL_UnsupportedParameter";
        case NVSDK_NGX_Result_FAIL_Denied:
            return "NVSDK_NGX_Result_FAIL_Denied";
        case NVSDK_NGX_Result_FAIL_NotImplemented:
            return "NVSDK_NGX_Result_FAIL_NotImplemented";
        case NVSDK_NGX_Result_Success:
            return "NVSDK_NGX_Result_Success";
        default:
            return "Unknown Error";
    }
}

std::string getDlssFeatureSupportString(NVSDK_NGX_Feature_Support_Result result) {
    switch( result ) {
        case NVSDK_NGX_FeatureSupportResult_Supported:
            return "NVSDK_NGX_FeatureSupportResult_Supported";
        case NVSDK_NGX_FeatureSupportResult_CheckNotPresent:
            return "NVSDK_NGX_FeatureSupportResult_CheckNotPresent";
        case NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported:
            return "NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported";
        case NVSDK_NGX_FeatureSupportResult_AdapterUnsupported:
            return "NVSDK_NGX_FeatureSupportResult_AdapterUnsupported";
        case NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported:
            return "NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported";
        case NVSDK_NGX_FeatureSupportResult_NotImplemented:
            return "NVSDK_NGX_FeatureSupportResult_NotImplemented";
        default:
            return "Unknown Feature Support String";
    }
}

static void checkDlssError(NVSDK_NGX_Result result, const char* expr, const char* file, unsigned int line)
{
    if(result != NVSDK_NGX_Result_Success) {
        std::stringstream ss;
        ss << "DLSS call (" << expr << ") failed with error: '" << getDlssErrorString(result) 
           << "' (" << file << ":" << line << ")\n";
        throw std::runtime_error(ss.str());
    }
}

#define DLSS_CHECK(call) checkDlssError(call, #call, __FILE__, __LINE__)

static NVSDK_NGX_ProjectIdDescription projectDescription{ "dddbee68-a452-4fab-9371-f9575480a154",  // Random UUID is ok
                                                          NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0.0" };

void getFeatureReqs()
{
    NVSDK_NGX_PathListInfo defaultPathInfo{ nullptr, 0 };
    NVSDK_NGX_LoggingInfo       defaultLoggingInfo{ nullptr, NVSDK_NGX_LOGGING_LEVEL_VERBOSE, false };
    NVSDK_NGX_FeatureCommonInfo featureInfo{ defaultPathInfo, nullptr, defaultLoggingInfo };

    NVSDK_NGX_Application_Identifier appId{ NVSDK_NGX_Application_Identifier_Type_Project_Id, projectDescription };

    std::filesystem::path appPath  = getExecutableDirectory();
    std::wstring          wAppPath = appPath.wstring();

    NVSDK_NGX_FeatureDiscoveryInfo info{
        NVSDK_NGX_Version_API,
        NVSDK_NGX_Feature_RayReconstruction,
        appId,
        wAppPath.c_str(),
        &featureInfo
    };

    NVSDK_NGX_FeatureRequirement  Supported;
    int                            cudaDevice = 0;

    NVSDK_NGX_Result result;
    result = NVSDK_NGX_CUDA_GetFeatureRequirements( cudaDevice, &info, &Supported );

    if( result != NVSDK_NGX_Result_Success ) {
        std::cerr << "Warning: NGX GetFeatureRequirements() failed with error: " << getDlssErrorString( result )
                  << " (code " << result << ")\n";
    }
    else {
        std::cerr << "NGX GetFetureRequirements(): " << getDlssFeatureSupportString( Supported.FeatureSupported )
                  << "\n\tMinHWArchitecture: " << Supported.MinHWArchitecture
                  << "\n\tMinOSVersion: " << Supported.MinOSVersion << "\n";
    }
}

void initDlss()
{
    // Initialize DLSS
    std::filesystem::path appPath = getExecutableDirectory();
    std::wstring wAppPath = appPath.wstring();

    DLSS_CHECK(NVSDK_NGX_CUDA_Init_with_ProjectID(
        projectDescription.ProjectId,
        projectDescription.EngineType,
        projectDescription.EngineVersion,
        wAppPath.c_str()
    ));

    // Check driver version
    NVSDK_NGX_Parameter* params = nullptr;
    DLSS_CHECK( NVSDK_NGX_CUDA_GetCapabilityParameters( &params ) );

    int needsUpdatedDriver = 0;
    int minVersionMajor = 0;
    int minVersionMinor = 0;
    DLSS_CHECK( params->Get( NVSDK_NGX_Parameter_ImageSuperResolution_NeedsUpdatedDriver, &needsUpdatedDriver ) );
    DLSS_CHECK( params->Get( NVSDK_NGX_Parameter_ImageSuperResolution_MinDriverVersionMajor, &minVersionMajor ) );
    DLSS_CHECK( params->Get( NVSDK_NGX_Parameter_ImageSuperResolution_MinDriverVersionMinor, &minVersionMinor ) );

    if ( needsUpdatedDriver )
    {
        throw std::runtime_error( "NVIDIA DLSS cannot be loaded due to outdated driver." );
    }

    // Cleanup params after version check
    DLSS_CHECK( NVSDK_NGX_CUDA_DestroyParameters( params ) );

    g_dlssInitialized = true;
}

void shutdownDlss()
{
    // Ignore errors during shutdown - just try to clean up what we can
    if ( NVSDK_NGX_CUDA_Shutdown() != NVSDK_NGX_Result_Success )
    {
        // Log but don't throw - we're shutting down anyway
        std::cerr << "Warning: DLSS shutdown returned error, but continuing with application shutdown\n";
    }
    g_dlssInitialized = false;
}

static NVSDK_NGX_PerfQuality_Value toNGXQuality(DlssQualityMode mode)
{
    switch (mode) {
        case DlssQualityMode::DLAA:
            return NVSDK_NGX_PerfQuality_Value_DLAA;
        case DlssQualityMode::MaxQuality:
            return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case DlssQualityMode::Balanced:
            return NVSDK_NGX_PerfQuality_Value_Balanced;
        case DlssQualityMode::MaxPerf:
            return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case DlssQualityMode::UltraPerformance:
            return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        default:
            return NVSDK_NGX_PerfQuality_Value_DLAA;  // Default to DLAA
    }
}

void DlssDenoiser::reset()
{
    m_resetHistory = true;
}

void DlssDenoiser::setQualityMode(DlssQualityMode mode)
{
    if (mode == m_qualityMode)
        return;

    m_qualityMode = mode;

    // Force recreation of DLSS feature on next denoise call
    if (m_ngxHandle) {
        DLSS_CHECK(NVSDK_NGX_CUDA_ReleaseFeature(m_ngxHandle));
        m_ngxHandle = nullptr;
    }
}

uint2 DlssDenoiser::getOptimalRenderResolution(uint2 targetSize) const
{
    // If DLAA mode, render at target resolution (no upscaling)
    if (m_qualityMode == DlssQualityMode::DLAA) {
        return targetSize;
    }

    // Get optimal render resolution from DLSS
    uint2 optimalRes{0, 0}, minRes{0, 0}, maxRes{0, 0};
    float sharpness = 0.f;  // unused (legacy NGX API)

    // Create params if needed
    if (!m_ngxParams) {
        DLSS_CHECK(NVSDK_NGX_CUDA_GetCapabilityParameters(&m_ngxParams));
    }

    // Get optimal settings from DLSS
    DLSS_CHECK(NGX_DLSSD_GET_OPTIMAL_SETTINGS(
        m_ngxParams,
        targetSize.x, targetSize.y,
        toNGXQuality(m_qualityMode),
        &optimalRes.x, &optimalRes.y,
        &minRes.x, &minRes.y,
        &maxRes.x, &maxRes.y,
        &sharpness
    ));

    return optimalRes;
}

void DlssDenoiser::updateDlssFeature(const uint2& rendersize, const uint2& targetsize)
{
    // Destroy existing handle if dimensions changed
    if (m_ngxHandle && (rendersize != m_rendersize || targetsize != m_targetsize)) {
        DLSS_CHECK(NVSDK_NGX_CUDA_ReleaseFeature(m_ngxHandle));
        m_ngxHandle = nullptr;
    }

    // Update stored dimensions
    m_rendersize = rendersize;
    m_targetsize = targetsize;

    // Create new handle if needed, typically on first frame or when dimensions change
    if (!m_ngxHandle) {
        // Get or create DLSS parameters if needed
        if (!m_ngxParams) {
            DLSS_CHECK(NVSDK_NGX_CUDA_GetCapabilityParameters(&m_ngxParams));
        }

        const bool lowResolutionMotionVectors = true;  // we let the Snippet do the upsampling of the motion vector
        const bool jitteredMV = false;              // We don't use the jittered camera matrix to calculate motion vector
        const bool isContentHDR = true;
        const bool depthInverted = false;
        const bool autoExposure = false;

        // Set up feature flags
        int createFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
        createFlags |= lowResolutionMotionVectors ? NVSDK_NGX_DLSS_Feature_Flags_MVLowRes : 0;
        createFlags |= isContentHDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0;
        createFlags |= depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0;
        createFlags |= jitteredMV ? NVSDK_NGX_DLSS_Feature_Flags_MVJittered : 0;
        createFlags |= autoExposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0;

        // Set up DLSS creation parameters
        NVSDK_NGX_DLSSD_Create_Params dlssParams = {};
        dlssParams.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
        dlssParams.InWidth = rendersize.x;
        dlssParams.InHeight = rendersize.y;
        dlssParams.InTargetWidth = targetsize.x;
        dlssParams.InTargetHeight = targetsize.y;
        dlssParams.InPerfQualityValue = toNGXQuality(m_qualityMode);
        dlssParams.InFeatureCreateFlags = createFlags;
        dlssParams.InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_Linear;
        dlssParams.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;

        // Set render preset for all quality modes
        NVSDK_NGX_RayReconstruction_Hint_Render_Preset dlssdModel = NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D;
        m_ngxParams->Set(NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA, dlssdModel);
        m_ngxParams->Set(NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality, dlssdModel);
        m_ngxParams->Set(NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced, dlssdModel);
        m_ngxParams->Set(NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance, dlssdModel);
        m_ngxParams->Set(NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance, dlssdModel);

        // Get current CUDA context
        CUcontext context = 0;
        cuCtxGetCurrent(&context);

        m_ngxParams->Set(NVSDK_NGX_Parameter_FreeMemOnReleaseFeature, 1);

        // Create DLSS feature with CUDA context
        NVSDK_NGX_CUDA_DLSSD_Create_Params cudaDlssParams = {};
        cudaDlssParams.Feature = dlssParams;
        cudaDlssParams.InCUContext = (void*)context;
        cudaDlssParams.InCUStream = 0;
        DLSS_CHECK(NGX_CUDA_CREATE_DLSSD_EXT( &m_ngxHandle, m_ngxParams, &cudaDlssParams ));
    }
}

void DlssDenoiser::denoise(GBuffer& gbuffer, int subframe, const otk::Matrix4x4& viewMatrix, const otk::Matrix4x4& projMatrix, const float2& jitter)
{
    if( !gbuffer.m_color.isValid() || !gbuffer.m_denoised.isValid() ) 
        return;

    // Update DLSS feature state
    updateDlssFeature(gbuffer.m_rendersize, gbuffer.m_targetsize);

    // Set up evaluation parameters
    NVSDK_NGX_CUDA_DLSSD_Eval_Params evalParams = {};
    evalParams.InReset = m_resetHistory ? 1 : 0;

    // Set up input textures from GBuffer
    evalParams.pInDiffuseAlbedo = &gbuffer.m_albedo.m_pointSampleTexObj;
    evalParams.pInSpecularAlbedo = &gbuffer.m_specular.m_pointSampleTexObj;
    evalParams.pInNormals = &gbuffer.m_normals.m_pointSampleTexObj;
    evalParams.pInRoughness = &gbuffer.m_roughness.m_pointSampleTexObj;
    evalParams.pInDepth = &gbuffer.m_depth.m_pointSampleTexObj;
    evalParams.pInMotionVectors = &gbuffer.m_motionvecs.m_pointSampleTexObj;
    evalParams.pInSpecularHitDistance = &gbuffer.m_specularHitT.m_pointSampleTexObj;
    evalParams.pInColor = &gbuffer.m_color.m_pointSampleTexObj;
    evalParams.pInOutput = &gbuffer.m_denoised.m_surfaceObj;

    // Set up matrices from parameters
    evalParams.pInWorldToViewMatrix = const_cast<float*>(viewMatrix.getData());
    evalParams.pInViewToClipMatrix = const_cast<float*>(projMatrix.getData());

    // Set up jitter from parameter
    evalParams.InJitterOffsetX = -(jitter.x - .5f);
    evalParams.InJitterOffsetY = -(jitter.y - .5f);

    // Set up motion vector scaling and inversion
    evalParams.InIndicatorInvertXAxis = 0;
    evalParams.InIndicatorInvertYAxis = 1;
    evalParams.InMVScaleX = -1.0f;
    evalParams.InMVScaleY = -1.0f;

    // Set render dimensions
    evalParams.InRenderSubrectDimensions = { gbuffer.m_rendersize.x, gbuffer.m_rendersize.y };

    // Evaluate DLSS
    DLSS_CHECK(NGX_CUDA_EVALUATE_DLSSD_EXT(m_ngxHandle, m_ngxParams, &evalParams));

    m_resetHistory = false;
}


__device__ inline
float3 unprojectPixelToWorldDirection( float2 pixel,uint2 dims, const otk::Matrix4x4& viewInv, const otk::Matrix4x4& projInv )
{
    float4 v_ndc =
        make_float4( ( pixel / make_float2( dims.x, dims.y ) ) * 2.f - 1.f,
                     0.f, 1.f );
    float4 v_clip = v_ndc;
    float4 v_cam  = projInv * v_clip;
    float4 vw = viewInv * make_float4( v_cam.x, v_cam.y, v_cam.z, 0.0f );

    return make_float3( vw.x, vw.y, vw.z );
}

template <typename T> 
__global__ void postProcessKernel( const T depth, RwFloat4 output, otk::Matrix4x4 viewInv, otk::Matrix4x4 projInv, EnvLight envLight )
{
    uint2 idx = {
        .x = blockIdx.x * blockDim.x + threadIdx.x,
        .y = blockIdx.y * blockDim.y + threadIdx.y,
    };

    uint2 outputSize = output.m_size;
    assert( depth.m_size == outputSize );

    if( ( idx.x < outputSize.x ) && ( idx.y < outputSize.y ) )
    {
        const float2 curPixel = make_float2( idx.x + 0.5f, idx.y + 0.5f );

        const std::array<int2, 9> offsets = { {
            { -1, -1 }, { -1, 0 }, { -1, 1 },
            { 0, -1 }, { 0, 0 }, { 0, 1 },
            { 1, -1 }, { 1, 0 }, { 1, 1 }
        } };
        
        // Preserve DLSS edges: mark background pixels using a 3x3 dilation to avoid object boundaries
        bool background = true;
        for ( const auto& offset : offsets )
        {
            float depthVal = tex2D<float>( depth.m_pointSampleTexObj, 
                                        curPixel.x + offset.x, 
                                        curPixel.y + offset.y );
            if ( depthVal < std::numeric_limits<float>::infinity() )
            {
                background = false;
                break;
            }
        }

        if ( background )
        {
            // Re-shade environment lighting on background
            float3 rayDir = unprojectPixelToWorldDirection( curPixel, outputSize, viewInv, projInv );
            float3 c = environmentLight( rayDir, envLight );
            gbuffer::write( make_float4(c, 1.0f), output, idx );
        }
    }
}

void DlssDenoiser::postProcess( GBuffer& gbuffer, const PostProcessParams& params )
{
    // WAR for DLSS artifacts on background pixels on some scenes:
    // Re-shade the background with the original procedural shading, using the high-res depth buffer to classify background pixels

    if ( !gbuffer.m_depthHires.isValid() || !gbuffer.m_denoised.isValid() ) 
        return;

    const int blockSize1D = 32;
    const uint2 targetsize = gbuffer.m_depthHires.m_size;
    dim3 numBlocks( targetsize.x / blockSize1D + 1, targetsize.y / blockSize1D + 1, 1 );
    dim3 numThreadsPerBlock( 32, 32, 1 );

    otk::Matrix4x4 viewInv = params.viewMatrix.inverse();
    otk::Matrix4x4 projInv = params.projMatrix.inverse();

    postProcessKernel<<<numBlocks, numThreadsPerBlock>>>( gbuffer.m_depthHires, gbuffer.m_denoised, viewInv, projInv, params.envLight );
}

DlssDenoiser::~DlssDenoiser()
{
    // Require DLSS to still be initialized when destroying a denoiser
    // This helps catch programming errors where shutdown is called too early
    OTK_REQUIRE( g_dlssInitialized );

    if ( m_ngxHandle )
    {
        NVSDK_NGX_CUDA_ReleaseFeature( m_ngxHandle );
    }
    m_ngxHandle = nullptr;

    if ( m_ngxParams )
    {
        NVSDK_NGX_CUDA_DestroyParameters( m_ngxParams );
    }
    m_ngxParams = nullptr;
}


