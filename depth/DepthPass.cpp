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

#include "DepthPass.h"
#include "DepthPassParams.h"
#include "../pipelineCommonOptions.h"

#include <DepthPassEmbeddedDeviceCode.h>

#include <OptiXToolkit/Util/Exception.h>
#include <OptiXToolkit/Gui/Camera.h>

#include <optix_stack_size.h>
#include <optix_stubs.h>

// Implementation details
struct DepthPass::Impl {
    struct SbtRecord {
        __align__(OPTIX_SBT_RECORD_ALIGNMENT) char header[OPTIX_SBT_RECORD_HEADER_SIZE];
    };

    // Pipeline members
    OptixPipelineCompileOptions pipeline_compile_options = {};
    OptixModule module = nullptr;
    OptixProgramGroup raygen = nullptr;
    OptixProgramGroup miss = nullptr;
    OptixProgramGroup hitgroup = nullptr;
    OptixPipeline optix_pipeline = nullptr;
    OptixShaderBindingTable sbt = {};

    CuBuffer<DepthPassParams> dParams{1};

    PipelineCommonOptions const* commonOptions = nullptr;

    // Constructor for Impl (constructs pipeline in-place)
    Impl(OptixDeviceContext ctx, PipelineCommonOptions const& options) {
        commonOptions = &options;
        pipeline_compile_options.usesMotionBlur = commonOptions->usesMotionBlur;
        pipeline_compile_options.traversableGraphFlags = commonOptions->traversableGraph;
        pipeline_compile_options.allowClusteredGeometry = commonOptions->allowClusteredGeometry;
        pipeline_compile_options.numPayloadValues = 1;
        pipeline_compile_options.numAttributeValues = 2;
        pipeline_compile_options.exceptionFlags = OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW;
        pipeline_compile_options.pipelineLaunchParamsVariableName = "params";

        createModule(ctx);
        createProgramGroups(ctx);
        createOptixPipeline(ctx);
        createSBT(ctx);
    }

    ~Impl() {
        // Clean up SBT
        if (sbt.raygenRecord)
            CUDA_CHECK(cudaFree(reinterpret_cast<void*>(sbt.raygenRecord)));
        if (sbt.missRecordBase)
            CUDA_CHECK(cudaFree(reinterpret_cast<void*>(sbt.missRecordBase)));
        sbt.raygenRecord = 0;
        sbt.missRecordBase = 0;

        // Clean up pipeline
        if (optix_pipeline) {
            OPTIX_CHECK(optixPipelineDestroy(optix_pipeline));
            optix_pipeline = nullptr;
        }

        // Clean up program groups
        if (raygen)
            OPTIX_CHECK(optixProgramGroupDestroy(raygen));
        if (miss)
            OPTIX_CHECK(optixProgramGroupDestroy(miss));
        if (hitgroup)
            OPTIX_CHECK(optixProgramGroupDestroy(hitgroup));
        raygen = nullptr;
        miss = nullptr;
        hitgroup = nullptr;

        // Clean up module
        if (module) {
            OPTIX_CHECK(optixModuleDestroy(module));
            module = nullptr;
        }
    }

    // Delete copy and move operations
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

private:
    void createModule(OptixDeviceContext ctx);
    void createProgramGroups(OptixDeviceContext ctx);
    void createOptixPipeline(OptixDeviceContext ctx);
    void createSBT(OptixDeviceContext ctx);
};

// Implementation of DepthPass methods
DepthPass::DepthPass(OptixDeviceContext context, PipelineCommonOptions const& commonOptions)
    : impl(std::make_unique<Impl>(context, commonOptions)) {}

DepthPass::~DepthPass() = default;

void DepthPass::render(OptixDeviceContext context, 
                      CUstream stream, 
                      const OptixTraversableHandle handle, 
                      otk::Camera& camera,
                      RwFloatInterop& zbuffer)
{
    // Set up params for this frame
    DepthPassParams params = {};
    params.handle = handle;
    params.zbuffer = zbuffer;
    
    // Get camera parameters directly
    params.eye = camera.getEye();
    auto const& [u, v, w] = camera.getBasis();
    params.U = u;
    params.V = v;
    params.W = w;
    
    impl->dParams.uploadAsync(&params, 1);

    const uint2 size = zbuffer.m_size;

    OPTIX_CHECK(optixLaunch(impl->optix_pipeline, stream,
        impl->dParams.cu_ptr(), sizeof(DepthPassParams), &impl->sbt, size.x, size.y, 1));
}

// Implementation of Impl member functions
void DepthPass::Impl::createModule(OptixDeviceContext ctx)
{
    OptixModuleCompileOptions module_compile_options = {};
#if !defined(NDEBUG)
    module_compile_options.optLevel = OPTIX_COMPILE_OPTIMIZATION_LEVEL_0;
    module_compile_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_FULL;
#else
    module_compile_options.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    module_compile_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;
#endif

    // Compile module
    OPTIX_CHECK_LOG2(optixModuleCreate(ctx, 
                                      &module_compile_options, 
                                      &pipeline_compile_options,
                                      DepthPassEmbeddedDeviceCodeText(), 
                                      DepthPassEmbeddedDeviceCodeSize, 
                                      LOG, 
                                      &LOG_SIZE,
                                      &module));
}

void DepthPass::Impl::createProgramGroups(OptixDeviceContext ctx)
{
    OptixProgramGroupOptions program_group_options = {};

    {
        OptixProgramGroupDesc raygen_prog_group_desc = {};
        raygen_prog_group_desc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        raygen_prog_group_desc.raygen.module = module;
        raygen_prog_group_desc.raygen.entryFunctionName = "__raygen__pinhole__depthpass";
        OPTIX_CHECK_LOG2(optixProgramGroupCreate(ctx, 
                                                &raygen_prog_group_desc, 
                                                1, 
                                                &program_group_options,
                                                LOG, 
                                                &LOG_SIZE, 
                                                &raygen));
    }

    {
        OptixProgramGroupDesc miss_prog_group_desc = {};
        miss_prog_group_desc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
        miss_prog_group_desc.miss.module = module;
        miss_prog_group_desc.miss.entryFunctionName = "__miss__depthpass";
        OPTIX_CHECK_LOG2(optixProgramGroupCreate(ctx, 
                                                &miss_prog_group_desc, 
                                                1, 
                                                &program_group_options,
                                                LOG, 
                                                &LOG_SIZE, 
                                                &miss));
    }

    {
        OptixProgramGroupDesc hit_prog_group_desc = {};
        hit_prog_group_desc.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        hit_prog_group_desc.hitgroup.moduleCH = module;
        hit_prog_group_desc.hitgroup.entryFunctionNameCH = "__closesthit__depthpass";
        OPTIX_CHECK_LOG2(optixProgramGroupCreate(ctx,
                                                &hit_prog_group_desc,
                                                1,
                                                &program_group_options,
                                                LOG,
                                                &LOG_SIZE,
                                                &hitgroup));
    }
}

void DepthPass::Impl::createOptixPipeline(OptixDeviceContext ctx)
{
    OptixPipelineLinkOptions pipeline_link_options = {};
    pipeline_link_options.maxTraceDepth = 1;

    OptixProgramGroup program_groups[] = {raygen, miss, hitgroup};
    int numProgramGroups = sizeof(program_groups) / sizeof(OptixProgramGroup);

    OPTIX_CHECK_LOG2(optixPipelineCreate(ctx, 
                                        &pipeline_compile_options, 
                                        &pipeline_link_options,
                                        program_groups, 
                                        numProgramGroups, 
                                        LOG, 
                                        &LOG_SIZE, 
                                        &optix_pipeline));

    // Calculate stack sizes
    OptixStackSizes stack_sizes = {};
    OPTIX_CHECK(optixUtilAccumulateStackSizes(raygen, &stack_sizes, optix_pipeline));
    OPTIX_CHECK(optixUtilAccumulateStackSizes(miss, &stack_sizes, optix_pipeline));
    OPTIX_CHECK(optixUtilAccumulateStackSizes(hitgroup, &stack_sizes, optix_pipeline));

    uint32_t max_trace_depth = 1;
    uint32_t max_cc_depth = 0;
    uint32_t max_dc_depth = 0;
    uint32_t direct_callable_stack_size_from_traversal;
    uint32_t direct_callable_stack_size_from_state;
    uint32_t continuation_stack_size;
    OPTIX_CHECK(optixUtilComputeStackSizes(&stack_sizes, 
                                          max_trace_depth, 
                                          max_cc_depth, 
                                          max_dc_depth, 
                                          &direct_callable_stack_size_from_traversal,
                                          &direct_callable_stack_size_from_state, 
                                          &continuation_stack_size));

    OTK_REQUIRE( commonOptions );
    uint32_t max_traversal_depth = static_cast<uint32_t>( commonOptions->maxTraversalDepth() );

    OPTIX_CHECK(optixPipelineSetStackSize(optix_pipeline, 
                                         direct_callable_stack_size_from_traversal,
                                         direct_callable_stack_size_from_state, 
                                         continuation_stack_size, 
                                         max_traversal_depth));
}

void DepthPass::Impl::createSBT(OptixDeviceContext ctx)
{
    {
        const size_t raygen_record_size = sizeof(SbtRecord);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&sbt.raygenRecord), raygen_record_size));

        SbtRecord rg_sbt;
        OPTIX_CHECK(optixSbtRecordPackHeader(raygen, &rg_sbt));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(sbt.raygenRecord), 
                             &rg_sbt, 
                             raygen_record_size,
                             cudaMemcpyHostToDevice));
    }

    {
        const size_t miss_record_size = sizeof(SbtRecord);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&sbt.missRecordBase), miss_record_size));

        SbtRecord ms_sbt;
        OPTIX_CHECK(optixSbtRecordPackHeader(miss, &ms_sbt));

        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(sbt.missRecordBase), 
                             &ms_sbt, 
                             miss_record_size,
                             cudaMemcpyHostToDevice));
        sbt.missRecordStrideInBytes = static_cast<uint32_t>(miss_record_size);
        sbt.missRecordCount = 1;
    }

    {
        const size_t hit_record_size = sizeof(SbtRecord);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&sbt.hitgroupRecordBase), hit_record_size));

        SbtRecord hg_sbt;
        OPTIX_CHECK(optixSbtRecordPackHeader(hitgroup, &hg_sbt));

        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(sbt.hitgroupRecordBase), 
                             &hg_sbt, 
                             hit_record_size,
                             cudaMemcpyHostToDevice));
        sbt.hitgroupRecordStrideInBytes = static_cast<uint32_t>(hit_record_size);
        sbt.hitgroupRecordCount = 1;
    }
}
